/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/config.h>

	.text

	.globl _vboot
_vboot:
	csrw sscratch, a0
	jal init_vstack
	bnez a0, _vsecondary
	call _vkernel
1:
	la ra, 1b
	ret

	.globl _vkernel
_vkernel:
#	jal init_stack
	li a0, 0
	csrw sie, a0
	call setup_vtrap
	#jal setup_mmu
#	la a0, _vkernel_api
#	li a7, 0x0A000000
#	li a6, 0x80000000
#	ecall
	call get_tick
	la a0, _vkernel_msg
#	call early_printk
	lw a0, g_vcpus
	call smp_start_cpus
	li a0, 0x100
	csrc sstatus, a0
	la a0, guest
	csrw sepc, a0
	li a0, 0
	sret

_vsecondary:
	li a0, 0
	csrw sie, a0
	lw t0, g_vcpus
	addi t0, t0, 1
	sw t0, g_vcpus, t1
	call setup_vtrap
	jal boot_idle
	li a0, 0x100
	csrc sstatus, a0
	la a0, guest
	csrw sepc, a0
	li a0, 0
	sret

init_vstack:
	li sp, ACRN_VSTACK_TOP
	li t0, ACRN_VSTACK_SIZE
	mul t0, a0, t0
	sub sp, sp, t0
	#csrw sscrach, sp
	ret

_vkernel_msg:
	.string "i'm _vkernel\n"
_vkernel_api:
	.dword 0
