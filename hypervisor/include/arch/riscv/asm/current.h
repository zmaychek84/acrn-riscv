/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_CURRENT_H__
#define __RISCV_CURRENT_H__

#ifndef __ASSEMBLY__
#include <asm/cpu.h>
#include <asm/guest/vcpu.h>

register struct acrn_vcpu *curr_vcpu asm ("tp");
static inline struct thread_object *get_current(void)
{
	return &curr_vcpu->thread_obj;
}

static inline void set_current(struct acrn_vcpu *vcpu)
{
	curr_vcpu = vcpu;
}

#define get_pcpu_id()		(curr_vcpu->pcpu_id)

#define set_pcpu_id(id)			\
do {					\
	curr_vcpu->pcpu_id = id;	\
} while ( 0 )

static inline struct cpu_info *get_cpu_info(void)
{
#ifdef __clang__
	unsigned long sp;

	asm ("mv %0, sp" : "=r" (sp));
#else
	register unsigned long sp asm ("sp");
#endif

	return (struct cpu_info *)((sp & ~(STACK_SIZE - 1)) +
		STACK_SIZE - sizeof(struct cpu_info));
}

#define guest_cpu_ctx_regs() (&get_cpu_info()->guest_cpu_ctx_regs)

#define switch_stack_and_jump(stack, fn) \
	asm volatile ("mv sp,%0; b " STR(fn) : : "r" (stack) : "memory" )

#define reset_stack_and_jump(fn) switch_stack_and_jump(get_cpu_info(), fn)

#endif /* !__ASSEMBLY__ */

#endif /* __RISCV_CURRENT_H__ */
