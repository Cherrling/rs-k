#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 <srctree> <build-dir>" >&2
	exit 2
fi

srctree=$1
builddir=$2
workdir="$builddir/exit-mmap-userspace"
initramfs="$workdir/initramfs.cpio"
log="$workdir/qemu.log"
qemu_timeout=${QEMU_TIMEOUT:-60}

mkdir -p "$workdir/root"
${CC:-gcc} -static -O2 -Wall -Wextra -Werror \
	"$srctree/tools/testing/selftests/exit_mmap/exit_mmap_test.c" \
	-o "$workdir/root/init"

(
	cd "$workdir/root"
	find . -print | cpio -o -H newc > "$initramfs"
)

if ! timeout "$qemu_timeout" qemu-system-x86_64 \
		-nodefaults -m 1536 \
		-kernel "$builddir/arch/x86/boot/bzImage" \
		-initrd "$initramfs" \
		-append "console=ttyS0 panic=-1 kunit.enable=0 rdinit=/init" \
		-no-reboot -nographic \
		-accel kvm -accel hvf -accel tcg \
		-serial stdio -smp 4 > "$log" 2>&1; then
	cat "$log"
	exit 1
fi

cat "$log"
grep -a -q 'exit_mmap userspace: PASS' "$log"
