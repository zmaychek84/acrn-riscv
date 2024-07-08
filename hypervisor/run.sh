#!/bin/bash

set -x
set -e

DEFAULT_QEMU=qemu-system-riscv64

if [[ x"${QEMU}" = x"" ]]; then
    QEMU=${DEFAULT_QEMU}
fi

#${QEMU} -smp 5 -bios build/acrn.elf -gdb tcp::1235 -S -M virt -m 4G,slots=3,maxmem=8G -kernel ./vmlinux.sos -initrd ./initrd -device loader,file=./Image.uos,addr=0xC1000000 -device loader,file=./initrd,addr=0xC9000000 -nographic
${QEMU} -smp 5 -bios build/acrn.elf -gdb tcp::1235 -S -M virt -m 4G,slots=3,maxmem=8G -kernel ./vmlinux -initrd ./initrd -nographic
