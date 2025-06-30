/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesd-circular-buffer.h"
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Andy Carlson");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    struct aesd_dev *aesd_device = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = aesd_device;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *aesd_device = (struct aesd_dev *)filp->private_data;
    int status = mutex_lock_interruptible(&aesd_device->buffer_lock);
    if (status != 0) {
        return status;
    }
    size_t entry_offset;
    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device->buffer, *f_pos, &entry_offset);
    if (entry == NULL) {
        retval = 0;
        goto cleanup1;
    }
    ulong bytes_not_written = copy_to_user(buf, entry->buffptr + entry_offset, entry->size - entry_offset);
    if (bytes_not_written != 0) {
        retval = -EFAULT;
        goto cleanup1;
    }
    retval = entry->size - entry_offset;
    *f_pos += retval;

cleanup1:
    mutex_unlock(&aesd_device->buffer_lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    struct aesd_dev *aesd_device = (struct aesd_dev *)filp->private_data;

    int status = mutex_lock_interruptible(&aesd_device->buffer_lock);
    if (status != 0) {
        retval = status;
        goto cleanup0;
    }
    aesd_device->pending_write = (char*)krealloc(aesd_device->pending_write, aesd_device->pending_bytes + count, GFP_KERNEL);
    if (aesd_device->pending_write == NULL) {
        return retval;
    }
    ulong bytes_not_written = copy_from_user(aesd_device->pending_write + aesd_device->pending_bytes, buf, count);
    if (bytes_not_written != 0) {
        retval = -EFAULT;
        goto cleanup1;
    }

    aesd_device->pending_bytes += count;
    aesd_device->pending_write[aesd_device->pending_bytes] = 0;

    if (aesd_device->pending_write[aesd_device->pending_bytes - 1] == '\n') {
        struct aesd_buffer_entry entry;
        entry.buffptr = aesd_device->pending_write;
        entry.size = aesd_device->pending_bytes;
        const char *evicted = aesd_circular_buffer_add_entry(&aesd_device->buffer, &entry);
        if (evicted != NULL) {
            kfree(evicted);
        }
        aesd_device->pending_bytes = 0;
        aesd_device->pending_write = NULL;
    }
    retval = count;

cleanup1:
    mutex_unlock(&aesd_device->buffer_lock);
cleanup0:
    return retval;
}

static loff_t aesd_llseek(struct file *filp, loff_t f_pos, int whence) {
    int status = mutex_lock_interruptible(&aesd_device.buffer_lock);
    if (status != 0) {
        return status;
    }
    loff_t size = aesd_circular_buffer_len(&aesd_device.buffer);
    mutex_unlock(&aesd_device.buffer_lock);
    return fixed_size_llseek(filp, f_pos, whence, size);
}

static long aesd_ioctl(struct file *filp, uint cmd, ulong arg) {
    PDEBUG("aesd_ioctl");
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch(cmd) {
    case AESDCHAR_IOCSEEKTO:
        struct aesd_seekto seekto;
        if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(struct aesd_seekto))) {
            return -EFAULT;
        }
        PDEBUG("seekto: %i, %i\n", seekto.write_cmd, seekto.write_cmd_offset);
        mutex_lock_interruptible(&aesd_device.buffer_lock);
        long long f_pos = aesd_circular_buffer_find_fpos_for_entry_offset(&aesd_device.buffer, seekto.write_cmd, seekto.write_cmd_offset);
        PDEBUG("f_pos: %i", f_pos);
        filp->f_pos = f_pos;
        mutex_unlock(&aesd_device.buffer_lock);
        break;
    default:
		return -ENOTTY;
    }
    return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
    .compat_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.buffer_lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    mutex_destroy(&aesd_device.buffer_lock);
    aesd_circular_buffer_destroy(&aesd_device.buffer);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
