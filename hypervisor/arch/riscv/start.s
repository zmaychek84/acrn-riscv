/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/config.h>

	.text

	.globl _start
_start:
	csrr a0, mhartid

	csrr t0, menvcfg
	li t1, 0x8000000000000000
	or t0, t0, t1
	csrw menvcfg, t1
	csrwi mcounteren, 0x7
	csrwi scounteren, 0x7
	jal init_mstack
	call reset_mtimer
	csrw mip, 0x0
#ifndef CONFIG_MACRN
	li t0, 0x08
	csrw pmpcfg0, t0
	li t0, 0xffffffff
	csrw pmpaddr0, t0

	li t0, 0x9aa
	csrs mstatus, t0
#else
	li t0, 0x0f08
	csrw pmpcfg0, t0
	li t0, 0x20000000
	csrw pmpaddr0, t0
	li t0, 0xffffffff
	csrw pmpaddr1, t0

	li t0, 0x19aa
	csrs mstatus, t0
#endif

	call init_mtrap

	li t0, 0xaaa
	csrs mideleg, t0

	li t0, 0xaaa
	csrw mie, t0

	li t0, 0xf0f5ff
	csrw medeleg, t0

	csrw mscratch, sp
	la t0, _boot
	csrw mepc, t0
	mret

	.globl _boot
_boot:
#ifndef CONFIG_MACRN
	jal init_stack
#endif
	bnez a0, secondary
	call kernel_init
1:
	la ra, 1b
	ret

secondary:
	lw t0, g_cpus
	addi t0, t0, 1
	sw t0, g_cpus, t1
	call boot_trap
	jal boot_idle
	call start_secondary 

init_stack:
	li sp, ACRN_STACK_START
	li t0, ACRN_STACK_SIZE
	mul t0, a0, t0
	sub sp, sp, t0
	csrw sscratch, sp
	ret

init_mstack:
	li sp, ACRN_MSTACK_START
	li t0, ACRN_MSTACK_SIZE
	mul t0, a0, t0
	sub sp, sp, t0
	csrw mscratch, sp
	ret

	.globl boot_idle
boot_idle:
	wfi
	ret

	.globl _end_boot
_end_boot:
	nop
	ret
