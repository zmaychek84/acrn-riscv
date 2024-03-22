/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <lib/types.h>
#include <asm/cpu.h>
#include <asm/guest/vcpu.h>
#include "sbi.h"

static void sbi_ecall_base_probe(unsigned long id, unsigned long *out_val);
static void sbi_base_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	int *ret = &regs->a0;
	bool fail = false;
	unsigned long funcid = regs->a6;
	unsigned long *out_val = &regs->a1;

	switch (funcid) {
	case SBI_TYPE_BASE_GET_SPEC_VERSION:
		*out_val = SBI_SPEC_VERSION_MAJOR << 24;
		*out_val = *out_val | SBI_SPEC_VERSION_MINOR;
		break;
	case SBI_TYPE_BASE_GET_IMP_ID:
		*out_val = SBI_ACRN_IMPID;
		break;
	case SBI_TYPE_BASE_GET_IMP_VERSION:
		*out_val = SBI_ACRN_VERSION_MAJOR << 24;
		*out_val = *out_val | SBI_ACRN_VERSION_MINOR;
		break;
	case SBI_TYPE_BASE_GET_MVENDORID:
		*out_val = cpu_csr_read(mvendorid);
		break;
	case SBI_TYPE_BASE_GET_MARCHID:
		*out_val = cpu_csr_read(marchid);
		break;
	case SBI_TYPE_BASE_GET_MIMPID:
		*out_val = cpu_csr_read(mimpid);
		break;
	case SBI_TYPE_BASE_PROBE_EXT:
		sbi_ecall_base_probe(regs->a0, out_val);
		break;
	default:
		fail = true;
		break;
	}

	if (fail)
		*ret = SBI_ENOTSUPP;
	else
		*ret = SBI_SUCCESS;

	return;
}

static void sbi_timer_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	int *ret = &regs->a0;
	unsigned long funcid = regs->a6;
	unsigned long *out_val = &regs->a1;

	if (funcid == SBI_TYPE_TIME_SET_TIMER) {
//		sbi_timer_event_start((u64)regs->a0);
		*ret = SBI_SUCCESS;
	} else {
		*ret = SBI_ENOTSUPP;
	}

	return;
}

static void sbi_ipi_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	int *ret = &regs->a0;
	unsigned long funcid = regs->a6;
	unsigned long *out_val = &regs->a1;

	if (funcid == SBI_TYPE_IPI_SEND_IPI) {
		unsigned long cpu = regs->a0;
	//	*ret = sbi_ipi_send_smode(regs->a0, regs->a1);
		*ret = SBI_SUCCESS;
	} else {
		*ret = SBI_ENOTSUPP;
	}

	return;
}

static void sbi_rfence_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_ENOTSUPP;

	return;
}

static void sbi_hsm_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_ENOTSUPP;

	return;
}

static void sbi_srst_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_ENOTSUPP;

	return;
}

static void sbi_pmu_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_ENOTSUPP;

	return;
}

static void sbi_undefined_handler(struct acrn_vcpu *vcpu, struct cpu_regs *regs)
{
	regs->a0 = SBI_ENOTSUPP;

	return 0;
}

static const struct sbi_ecall_dispatch sbi_dispatch_table[NR_HX_EXIT_REASONS] = {
	[SBI_TYPE_BASE] = {
		.ext_id = SBI_ID_BASE,
		.handler = sbi_base_handler},
	[SBI_TYPE_TIMER] = {
		.ext_id = SBI_ID_TIMER,
		.handler = sbi_timer_handler},
	[SBI_TYPE_IPI] = {
		.ext_id = SBI_ID_IPI,
		.handler = sbi_ipi_handler},
	[SBI_TYPE_RFENCE] = {
		.ext_id = SBI_ID_RFENCE,
		.handler = sbi_rfence_handler},
	[SBI_TYPE_HSM] = {
		.ext_id = SBI_ID_HSM,
		.handler = sbi_hsm_handler},
	[SBI_TYPE_SRST] = {
		.ext_id = SBI_ID_SRST,
		.handler = sbi_srst_handler},
	[SBI_TYPE_PMU] = {
		.ext_id = SBI_ID_PMU,
		.handler = sbi_pmu_handler},
	[SBI_MAX_TYPES] = {
		.ext_id = SBI_VENDOR_START,
		.handler = sbi_undefined_handler},
};

static void sbi_ecall_base_probe(unsigned long id, unsigned long *out_val)
{
	struct sbi_ecall_dispatch *d = &sbi_dispatch_table[SBI_MAX_TYPES];

	*out_val = 0;
	switch (id) {
	case SBI_ID_BASE:
		*out_val = 1;
		break;
	default:
		break;
	}

	return;
}

int sbi_ecall_handler(struct acrn_vcpu *vcpu)
{
	const struct run_context *ctx =
		&vcpu->arch.contexts[vcpu->arch.cur_context].run_ctx;
	struct cpu_regs *regs = &ctx->cpu_gp_regs.regs;
	uint32_t id = regs->a7;
	struct sbi_ecall_dispatch *d = &sbi_dispatch_table[SBI_MAX_TYPES];

	for (uint32_t i = 0; i < SBI_MAX_TYPES; i++) {
		if (id == sbi_dispatch_table[i].ext_id) {
			d = &sbi_dispatch_table[i];
			break;
		}
	}

	d->handler(vcpu, regs);

	return 0;
}
