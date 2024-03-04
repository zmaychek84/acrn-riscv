#include <asm/config.h>

	.text

	.globl _start
_start:
	csrr a0, mhartid

	jal init_mstack
	call reset_mtimer
	csrw mip, 0x0
.if !CONFIG_MACRN
	li t0, 0x08
	csrw pmpcfg0, t0
	li t0, 0xffffffff
	csrw pmpaddr0, t0

	li t0, 0x9aa
	csrs mstatus, t0
.else
	li t0, 0x0f08
	csrw pmpcfg0, t0
	li t0, 0x20000000
	csrw pmpaddr0, t0
	li t0, 0xffffffff
	csrw pmpaddr1, t0

	li t0, 0x19aa
	csrs mstatus, t0
.endif

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
.if !CONFIG_MACRN
	jal init_stack
.endif
	bnez a0, secondary
	call kernel_init
1:
	la ra, 1b
	ret

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
	lw a0, g_vcpus
	call smp_start_cpus
	call get_tick
	la a0, _vkernel_msg
	call early_printk
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

init_vstack:
	li sp, ACRN_VSTACK_START
	li t0, ACRN_VSTACK_SIZE
	mul t0, a0, t0
	sub sp, sp, t0
	#csrw sscrach, sp
	ret

	.globl boot_idle
boot_idle:
	wfi
	ret

	.globl _end_boot
_end_boot:
	nop
	ret

_vkernel_msg:
	.string "i'm _vkernel\n"
_vkernel_api:
	.dword 0
