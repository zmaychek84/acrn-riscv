/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_VPLIC_H__
#define __RISCV_VPLIC_H__

#include <asm/page.h>
#include <asm/timer.h>
#include <asm/apicreg.h>

struct acrn_vcpu;
struct acrn_apicv_ops {
	void (*accept_intr)(struct acrn_vclint*vclint, uint32_t vector, bool level);
	void (*inject_intr)(struct acrn_vclint*vclint, bool guest_irq_enabled, bool injected);
	bool (*has_pending_delivery_intr)(struct acrn_vcpu *vcpu);
	bool (*has_pending_intr)(struct acrn_vcpu *vcpu);
	bool (*apic_read_access_may_valid)(uint32_t offset);
	bool (*apic_write_access_may_valid)(uint32_t offset);
	bool (*x2apic_read_csr_may_valid)(uint32_t offset);
	bool (*x2apic_write_csr_may_valid)(uint32_t offset);
};

struct acrn_vplic {
	struct clint_regs	clint_page;
	struct acrn_vm *vm;
	struct vclint_timer	vtimer[5];

	uint64_t	clint_base;
	uint32_t	clint_irr;

	const struct acrn_apicv_ops *ops;

	uint32_t	svr_last;
	uint32_t	lvt_last[VCLINT_MAXLVT_INDEX + 1];
} __aligned(PAGE_SIZE);

enum reset_mode;

#define	LAPIC_TRIG_LEVEL	true
#define	LAPIC_TRIG_EDGE		false

int32_t apic_access_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t apic_write_vmexit_handler(struct acrn_vcpu *vcpu);

#endif /* __RISCV_VLAPIC_H__ */
