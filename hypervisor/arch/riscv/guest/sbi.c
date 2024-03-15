/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <lib/types.h>
#include <asm/cpu.h>
#include <asm/guest/vcpu.h>
#include "sbi.h"

static void sbi_base_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_SUCCESS;
	return 0;
}

static uint64_t sbi_timer_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_SUCCESS;
	return 0;
}

static uint64_t sbi_ipi_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_SUCCESS;
	return 0;
}

int sbi_ecall_handler(struct acrn_vcpu *vcpu)
{
	const struct run_context *ctx =
		&vcpu->arch.contexts[vcpu->arch.cur_context].run_ctx;
	struct cpu_regs *regs = &ctx->cpu_gp_regs.regs;

	switch (regs->a7) {
	case SBI_BASE:
		sbi_base_handler(vcpu, regs);
		break;
	case SBI_TIMER:
		sbi_timer_handler(vcpu, regs);
		break;
	case SBI_IPI:
		break;
	case SBI_RFENCE:
	case SBI_HSM:
	case SBI_SRST:
		break;
	case SBI_PMU:
		break;
	default:
		break;
	}

	return 0;
}
