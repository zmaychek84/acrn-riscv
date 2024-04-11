/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

	.text

	.globl setup_mmu
setup_mmu:

vpn3:
	ld a0, vpn3_ppn
	slli a0, a0, 10
	addi a0, a0, 0x1
	li a1, 0x90001000
	sd a0, 0(a1)

	#setup io mem ppn1
	ld a0, io_ppn
	li t2, 0x40000000000000f
	slli a0, a0, 10
	add a0, a0, t2
	li a1, 0xC0000000
	li t0, 0
	li t1, 2
vpn2_io:
	sd a0, 0(a1)
	addi a1, a1, 8
	li t2, 0x10000000
	add a0, a0, t2
	addi t0, t0, 1
	blt t0, t1, vpn2_io

	ld a0, mem_ppn
	slli a0, a0, 10
	addi a0, a0, 0xf
	li a1, 0xC0000010
	sd a0, 0(a1)

mmu_jump:
	li a0, 0x90001
	#slli a0, a0, 2
	li t0, 0x9000000000000000
	add a0, a0, t0 
	#sfence.vma
	#fence
	csrw satp, a0
	#fence.i
	sfence.vma
	ret

	.globl switch_satp 
switch_satp:
	csrw satp, a0
	sfence.vma
	ret

vpn3_ppn:
	.dword 0xC0000

mem_ppn:
	.dword 0x80000

io_ppn:
	.dword 0x0
