#!/bin/bash

set -x

#qemu-system-x86_64 -smp 4 -kernel build/hypervisor/acrn.32.out -gdb tcp::1235 -S -serial pty -machine kernel-irqchip=split -cpu Denverton -m 1G,slots=3,maxmem=4G
#qemu-system-riscv64 -smp 4 -kernel build/acrn.elf -gdb tcp::1235 -S -serial pty -M virt -m 1G,slots=3,maxmem=4G
#qemu-system-riscv64 -kernel build/acrn.elf -gdb tcp::1235 -S -serial stdio -M virt -m 1G,slots=3,maxmem=4G -bios none
#qemu-system-riscv64 -smp 4 -kernel build/acrn.elf -gdb tcp::1235 -S -serial stdio -M virt -m 4G,slots=3,maxmem=8G -bios none
#qemu-system-riscv64 -smp 4 -bios build/acrn.elf -gdb tcp::1235 -S -serial stdio -M virt -m 4G,slots=3,maxmem=8G -kernel build/acrn.elf
#qemu-system-riscv64 -smp 4 -bios build/acrn.elf -gdb tcp::1235 -S -M virt -m 4G,slots=3,maxmem=8G -kernel ./Image -nographic
#qemu-system-riscv64 -smp 4 -bios build/acrn.elf -gdb tcp::1235 -S -M virt -m 4G,slots=3,maxmem=8G -kernel ./Image -nographic
#qemu-system-riscv64 -smp 4 -bios build/acrn.elf -M virt -m 4G,slots=3,maxmem=8G -kernel ./Image -nographic
#qemu-system-riscv64 -smp 4 -bios build/acrn.elf -gdb tcp::1235 -S -serial stdio -M virt -m 4G,slots=3,maxmem=8G -kernel ./hc.elf
qemu-system-riscv64 -smp 4 -bios build/acrn.elf -gdb tcp::1235 -S -serial stdio -M virt -m 4G,slots=3,maxmem=8G -kernel ./vmlinux
#qemu-system-riscv64 -smp 4 -bios build/acrn.elf -gdb tcp::1235 -S -serial stdio -M virt -m 4G,slots=3,maxmem=8G -kernel ./Image
