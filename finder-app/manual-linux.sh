#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

make() {
    command make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" "$@"
}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    make mrproper
    make defconfig
    make -j4 --silent all
    make modules
    make dtbs
    cd "$OUTDIR"
fi

echo "Adding the Image in outdir"

cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" .

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

mkdir "$OUTDIR/rootfs"
cd "$OUTDIR/rootfs"

mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
else
    cd busybox
fi

make
make CONFIG_PREFIX="$OUTDIR/rootfs" install

cd "$OUTDIR/rootfs"

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"


SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
cp "$SYSROOT/lib/ld-linux-aarch64.so.1" lib
cp "$SYSROOT/lib64/"{libm.so.6,libresolv.so.2,libc.so.6} lib64

sudo mknod -m 666 dev/null c 1 3

cd "$FINDER_APP_DIR"
make clean
make

cd "$OUTDIR/rootfs"
cp "$FINDER_APP_DIR/writer" home
cp "$FINDER_APP_DIR"/*.sh home
cp -r "$FINDER_APP_DIR/../conf" conf

sudo chown -R root:root "$OUTDIR/rootfs"

rm -f "$OUTDIR"/initramfs.cpio{.gz,}

find . | cpio -H newc -o --owner root:root > "$OUTDIR/initramfs.cpio"
cd "$OUTDIR"
gzip -f initramfs.cpio
