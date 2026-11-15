#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 <srctree> <build-dir>" >&2
	exit 2
fi

srctree=$1
builddir=$2
workdir="$builddir/mprotect-userspace"
initramfs="$workdir/initramfs.cpio"
qemu_log=${QEMU_LOG:-"$workdir/qemu.log"}
qemu_stderr=${QEMU_STDERR:-"$workdir/qemu.stderr"}
qemu_timeout=${QEMU_TIMEOUT:-600}
qemu_smp=${QEMU_SMP:-4}
qemu_cpu_args=
if [ -n "${QEMU_CPU:-}" ]; then
	qemu_cpu_args="-cpu $QEMU_CPU"
fi
default_append="console=ttyS0 earlyprintk=ttyS0 loglevel=4"
default_append="$default_append panic=-1 kunit.enable=0 rdinit=/init"
qemu_append=${QEMU_APPEND:-$default_append}

case "${QEMU_ACCEL:-auto}" in
	kvm)
		qemu_accel_args="-accel kvm"
		;;
	tcg)
		qemu_accel_args="-accel tcg,thread=multi"
		;;
	tcg-single)
		qemu_accel_args="-accel tcg,thread=single"
		qemu_smp=1
		;;
	auto)
		qemu_accel_args="-accel kvm -accel hvf -accel tcg"
		;;
	*)
		echo "unsupported QEMU_ACCEL: ${QEMU_ACCEL}" >&2
		exit 2
		;;
esac

mkdir -p "$workdir/root"
${CC:-gcc} -static -pthread -O2 -Wall -Wextra -Werror \
	"$srctree/tools/testing/selftests/mprotect/mprotect_test.c" \
	-o "$workdir/root/init"

(
	cd "$workdir/root"
	find . -print | cpio -o -H newc > "$initramfs"
)

qemu_status=0
timeout "$qemu_timeout" qemu-system-x86_64 \
		-nodefaults -m 1536 \
		-kernel "$builddir/arch/x86/boot/bzImage" \
		-initrd "$initramfs" \
		-append "$qemu_append" \
		-no-reboot -display none -monitor none \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		$qemu_accel_args $qemu_cpu_args \
		-serial "file:$qemu_log" -smp "$qemu_smp" > "$qemu_stderr" 2>&1 || qemu_status=$?

case "$qemu_status" in
	33)
		;;
	35)
		cat "$qemu_log"
		cat "$qemu_stderr"
		exit 1
		;;
	*)
		cat "$qemu_log"
		cat "$qemu_stderr"
		echo "unexpected QEMU exit status: $qemu_status" >&2
		exit 1
		;;
esac

cat "$qemu_log"
grep -a -q 'mprotect userspace: PASS' "$qemu_log"
