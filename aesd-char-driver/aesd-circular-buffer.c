/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

#define BUFFER_EACH(buffer, entry, body)                                       \
  bool __wraps = buffer->in_offs < buffer->out_offs ||                         \
                 (buffer->in_offs == buffer->out_offs && buffer->full);        \
  uint8_t __end;                                                               \
  if (__wraps) {                                                               \
    __end = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;                           \
  } else {                                                                     \
    __end = buffer->in_offs;                                                   \
  }                                                                            \
  for (uint8_t __i = buffer->out_offs; __i < __end; __i++) {                   \
    struct aesd_buffer_entry *entry = &buffer->entry[__i];                     \
    body                                                                       \
  }                                                                            \
  if (__wraps) {                                                               \
    for (uint8_t __i = 0; __i < buffer->in_offs; __i++) {                      \
      struct aesd_buffer_entry *entry = &buffer->entry[__i];                   \
      body                                                                     \
    }                                                                          \
  }

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    size_t char_len = 0;
    BUFFER_EACH(buffer, entry, {
        if (char_len + entry->size > char_offset) {
            *entry_offset_byte_rtn = char_offset - char_len;
            return entry;
        }
        char_len += entry->size;
    })
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    struct aesd_buffer_entry *in_offset = &(buffer->entry)[buffer->in_offs];
    struct aesd_buffer_entry old_entry = *in_offset;
    *(in_offset) = *add_entry;
    buffer->in_offs++;
    buffer->in_offs = buffer->in_offs % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    } else if (buffer->full) {
        buffer->out_offs++;
        buffer->out_offs = buffer->out_offs % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        return old_entry.buffptr;
    }
    return NULL;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

#ifdef __KERNEL__
void aesd_circular_buffer_destroy(struct aesd_circular_buffer *buffer)
{
    BUFFER_EACH(buffer, entry, {
        kfree(entry->buffptr);
    })
    kfree(buffer);
}
#endif

size_t aesd_circular_buffer_len(struct aesd_circular_buffer *buffer)
{
    size_t len = 0;
    BUFFER_EACH(buffer, entry, {
        len += entry->size;
    })
    return len;
}

long long aesd_circular_buffer_find_fpos_for_entry_offset(struct aesd_circular_buffer *buffer,
            size_t entry_index, size_t entry_offset)
{
    size_t i = 0;
    long long fpos = 0;
    BUFFER_EACH(buffer, entry, {
        if (i == entry_index) {
            if (entry->size < entry_offset) {
                fpos += entry->size;
            } else {
                fpos += entry_offset;
            }
            return fpos;
        } else {
            fpos += entry->size;
        }
        i++;
    })
    return fpos;
}
