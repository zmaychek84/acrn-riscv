#!/bin/bash

set -x

#gdb build/hypervisor/acrn.out
#gdb -tui build/hypervisor/acrn.out
#gdb-multiarch -tui -x acrn.gdb -s build/acrn.elf
#gdb-multiarch -tui -x acrn.gdb -s vmlinux.boot
gdb-multiarch -tui -x acrn.gdb -s vmlinux
