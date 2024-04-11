/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/offset.h>
#include <asm/cpu.h>

	.text

	.globl setup_vtrap
setup_vtrap:
	la t0, vstrap_handler
	csrw stvec, t0
	ret

	.balign 4
vstrap_handler:
	cpu_disable_irq
#	csrrw sp, sscratch, sp
	vcpu_ctx_save
	li a0, 0
	csrw sip, a0

	#check if it's in VS mode
	#csrr t1, 0x600

	csrr a0, scause
	li a1, 0x8000000000000000
	and a1, a0, a1
	andi a0, a0, 0xff
	beqz a1, vexcept
	call sint_handler
	j vout
vexcept:
	csrr a0, sepc
	addi a0, a0, 4
	sd a0, REG_EPC(sp)
vout:
	vcpu_ctx_restore
#	li a0, 0
#	ecall
#	csrw sstatus, a0
#	la a0, vstrap_msg
#	call printk
#	csrrw sp, sscratch, sp
	cpu_enable_irq
	sret

	.balign 4
vkernel_msg:
	.string "i'm vkernel\n"

vstrap_msg:
	.string "vstrap handler\n"
