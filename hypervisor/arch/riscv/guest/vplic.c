/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define pr_prefix		"vplic: "

#include <types.h>
#include <errno.h>
#include <acrn/bitops.h>
#include <asm/atomic.h>
#include <acrn/percpu.h>
#include <asm/pgtable.h>
#include <asm/csr.h>
#include <asm/irq.h>
#include <asm/guest/vmcs.h>
#include <asm/guest/vplic.h>
#include <asm/vmx.h>
#include <asm/guest/vm.h>
#include <asm/guest/s2mm.h>
#include <ptdev.h>
#include <trace.h>
#include <logmsg.h>
#include "vplic_priv.h"

#define VLAPIC_VERBOS 0

static inline uint32_t prio(uint32_t x)
{
	return (x >> 4U);
}

#define VLAPIC_VERSION		(16U)
#define	APICBASE_BSP		0x00000100UL
#define	APICBASE_X2APIC		0x00000400UL
#define APICBASE_XAPIC		0x00000800UL
#define APICBASE_LAPIC_MODE	(APICBASE_XAPIC | APICBASE_X2APIC)
#define	APICBASE_ENABLED	0x00000800UL
#define LOGICAL_ID_MASK		0xFU
#define CLUSTER_ID_MASK		0xFFFF0U

#define DBG_LEVEL_VLAPIC		6U

static inline struct acrn_vcpu *vplic2vcpu(const struct acrn_vplic *vplic)
{
	return container_of(container_of(vplic, struct acrn_vcpu_arch, vplic), struct acrn_vcpu, arch);
}

#if VLAPIC_VERBOS
static inline void vplic_dump_irr(const struct acrn_vplic *vplic, const char *msg)
{
	const struct lapic_reg *irrptr = &(vplic->apic_page.irr[0]);

	for (uint8_t i = 0U; i < 8U; i++) {
		dev_dbg(DBG_LEVEL_VLAPIC, "%s irr%u 0x%08x", msg, i, irrptr[i].v);
	}
}

static inline void vplic_dump_isr(const struct acrn_vplic *vplic, const char *msg)
{
	const struct lapic_reg *isrptr = &(vplic->apic_page.isr[0]);

	for (uint8_t i = 0U; i < 8U; i++) {
		dev_dbg(DBG_LEVEL_VLAPIC, "%s isr%u 0x%08x", msg, i, isrptr[i].v);
	}
}
#else
static inline void vplic_dump_irr(__unused const struct acrn_vplic *vplic, __unused const char *msg) {}

static inline void vplic_dump_isr(__unused const struct acrn_vplic *vplic, __unused const char *msg) {}
#endif

const struct acrn_apicv_ops *vplic_ops;

static bool apicv_set_intr_ready(struct acrn_vplic *vplic, uint32_t vector);

static void apicv_trigger_pi_anv(uint16_t dest_pcpu_id, uint32_t anv);

/*
 * Post an interrupt to the vcpu running on 'hostcpu'. This will use a
 * hardware assist if available (e.g. Posted Interrupt) or fall back to
 * sending an 'ipinum' to interrupt the 'hostcpu'.
 */
static void vplic_set_error(struct acrn_vplic *vplic, uint32_t mask);

static void vplic_timer_expired(void *data);

static inline bool vplic_enabled(const struct acrn_vplic *vplic)
{
	const struct lapic_regs *lapic = &(vplic->apic_page);

	return (((vplic->csr_apicbase & APICBASE_ENABLED) != 0UL) &&
			((lapic->svr.v & APIC_SVR_ENABLE) != 0U));
}

static struct acrn_vplic *
vm_lapic_from_vcpu_id(struct acrn_vm *vm, uint16_t vcpu_id)
{
	struct acrn_vcpu *vcpu;

	vcpu = vcpu_from_vid(vm, vcpu_id);

	return vcpu_vplic(vcpu);
}

static uint16_t vm_apicid2vcpu_id(struct acrn_vm *vm, uint32_t lapicid)
{
	uint16_t i;
	struct acrn_vcpu *vcpu;
	uint16_t cpu_id = INVALID_CPU_ID;

	foreach_vcpu(i, vm, vcpu) {
		if (vcpu_vplic(vcpu)->vapic_id == lapicid) {
			cpu_id = vcpu->vcpu_id;
			break;
		}
	}

	if (cpu_id == INVALID_CPU_ID) {
		pr_err("%s: bad lapicid %lu", __func__, lapicid);
	}

	return cpu_id;
}

static inline uint32_t vplic_find_isrv(const struct acrn_vplic *vplic)
{
	const struct lapic_regs *lapic = &(vplic->apic_page);
	uint32_t i, val, bitpos, isrv = 0U;
	const struct lapic_reg *isrptr;

	isrptr = &lapic->isr[0];

	/* i ranges effectively from 7 to 1 */
	for (i = 7U; i > 0U; i--) {
		val = isrptr[i].v;
		if (val != 0U) {
			//bitpos = (uint32_t)fls32(val);
			isrv = (i << 5U) | bitpos;
			break;
		}
	}

	return isrv;
}

static void
vplic_write_dfr(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic;

	lapic = &(vplic->apic_page);
	lapic->dfr.v &= APIC_DFR_MODEL_MASK;
	lapic->dfr.v |= APIC_DFR_RESERVED;

	if ((lapic->dfr.v & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_FLAT) {
		dev_dbg(DBG_LEVEL_VLAPIC, "vplic DFR in Flat Model");
	} else if ((lapic->dfr.v & APIC_DFR_MODEL_MASK)
			== APIC_DFR_MODEL_CLUSTER) {
		dev_dbg(DBG_LEVEL_VLAPIC, "vplic DFR in Cluster Model");
	} else {
		dev_dbg(DBG_LEVEL_VLAPIC, "DFR in Unknown Model %#x", lapic->dfr);
	}
}

static void
vplic_write_ldr(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic;

	lapic = &(vplic->apic_page);
	lapic->ldr.v &= ~APIC_LDR_RESERVED;
	dev_dbg(DBG_LEVEL_VLAPIC, "vplic LDR set to %#x", lapic->ldr);
}

static inline uint32_t
vplic_timer_divisor_shift(uint32_t dcr)
{
	uint32_t val;

	val = ((dcr & 0x3U) | ((dcr & 0x8U) >> 1U));
	return ((val + 1U) & 0x7U);
}

static inline bool
vplic_lvtt_oneshot(const struct acrn_vplic *vplic)
{
	return (((vplic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_TM)
				== APIC_LVTT_TM_ONE_SHOT);
}

static inline bool
vplic_lvtt_period(const struct acrn_vplic *vplic)
{
	return (((vplic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_TM)
				==  APIC_LVTT_TM_PERIODIC);
}

static inline bool
vplic_lvtt_tsc_deadline(const struct acrn_vplic *vplic)
{
	return (((vplic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_TM)
				==  APIC_LVTT_TM_TSCDLT);
}

static inline bool
vplic_lvtt_masked(const struct acrn_vplic *vplic)
{
	return (((vplic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_M) != 0U);
}

/**
 * @pre vplic != NULL
 */
static void vplic_init_timer(struct acrn_vplic *vplic)
{
	struct vplic_timer *vtimer;

	vtimer = &vplic->vtimer;
	(void)memset(vtimer, 0U, sizeof(struct vplic_timer));

	initialize_timer(&vtimer->timer,
			vplic_timer_expired, vplic2vcpu(vplic),
			0UL, 0UL);
}

/**
 * @pre vplic != NULL
 */
static void vplic_reset_timer(struct acrn_vplic *vplic)
{
	struct hv_timer *timer;

	timer = &vplic->vtimer.timer;
	del_timer(timer);
	timer->mode = TICK_MODE_ONESHOT;
	timer->fire_tick = 0UL;
	timer->period_in_cycle = 0UL;
}

static bool
set_expiration(struct acrn_vplic *vplic)
{
	uint64_t now = cpu_ticks();
	uint64_t delta;
	struct vplic_timer *vtimer;
	struct hv_timer *timer;
	uint32_t tmicr, divisor_shift;
	bool ret;

	vtimer = &vplic->vtimer;
	tmicr = vtimer->tmicr;
	divisor_shift = vtimer->divisor_shift;

	if ((tmicr == 0U) || (divisor_shift > 8U)) {
		ret = false;
	} else {
		delta = (uint64_t)tmicr << divisor_shift;
		timer = &vtimer->timer;

		if (vplic_lvtt_period(vplic)) {
			timer->period_in_cycle = delta;
		}
		timer->fire_tick = now + delta;
		ret = true;
	}
	return ret;
}

static void vplic_update_lvtt(struct acrn_vplic *vplic,
			uint32_t val)
{
	uint32_t timer_mode = val & APIC_LVTT_TM;
	struct vplic_timer *vtimer = &vplic->vtimer;

	if (vtimer->mode != timer_mode) {
		struct hv_timer *timer = &vtimer->timer;

		/*
		 * A write to the LVT Timer Register that changes
		 * the timer mode disarms the local APIC timer.
		 */
		del_timer(timer);
		timer->mode = (timer_mode == APIC_LVTT_TM_PERIODIC) ?
				TICK_MODE_PERIODIC: TICK_MODE_ONESHOT;
		timer->fire_tick = 0UL;
		timer->period_in_cycle = 0UL;

		vtimer->mode = timer_mode;
	}
}

static uint32_t vplic_get_ccr(const struct acrn_vplic *vplic)
{
	uint64_t now = cpu_ticks();
	uint32_t remain_count = 0U;
	const struct vplic_timer *vtimer;

	vtimer = &vplic->vtimer;

	if ((vtimer->tmicr != 0U) && (!vplic_lvtt_tsc_deadline(vplic))) {
		uint64_t fire_tick = vtimer->timer.fire_tick;

		if (now < fire_tick) {
			uint32_t divisor_shift = vtimer->divisor_shift;
			uint64_t shifted_delta =
				(fire_tick - now) >> divisor_shift;
			remain_count = (uint32_t)shifted_delta;
		}
	}

	return remain_count;
}

static void vplic_write_dcr(struct acrn_vplic *vplic)
{
	uint32_t divisor_shift;
	struct vplic_timer *vtimer;
	struct lapic_regs *lapic = &(vplic->apic_page);

	vtimer = &vplic->vtimer;
	divisor_shift = vplic_timer_divisor_shift(lapic->dcr_timer.v);

	vtimer->divisor_shift = divisor_shift;
}

static void vplic_write_icrtmr(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic;
	struct vplic_timer *vtimer;

	if (!vplic_lvtt_tsc_deadline(vplic)) {
		lapic = &(vplic->apic_page);
		vtimer = &vplic->vtimer;
		vtimer->tmicr = lapic->icr_timer.v;

		del_timer(&vtimer->timer);
		if (set_expiration(vplic)) {
			/* vplic_init_timer has been called,
			 * and timer->fire_tick is not 0, here
			 * add_timer should not return error
			 */
			(void)add_timer(&vtimer->timer);
		}
	}
}

uint64_t vplic_get_tsc_deadline_csr(const struct acrn_vplic *vplic)
{
	uint64_t ret;
	struct acrn_vcpu *vcpu = vplic2vcpu(vplic);

	if (!vplic_lvtt_tsc_deadline(vplic)) {
		ret = 0UL;
	} else {
		ret = (vplic->vtimer.timer.fire_tick == 0UL) ? 0UL :
			vcpu_get_guest_csr(vcpu, MSR_IA32_TSC_DEADLINE);
	}

	return ret;
}

void vplic_set_tsc_deadline_csr(struct acrn_vplic *vplic, uint64_t val_arg)
{
	struct hv_timer *timer;
	uint64_t val = val_arg;
	struct acrn_vcpu *vcpu = vplic2vcpu(vplic);

	if (vplic_lvtt_tsc_deadline(vplic)) {
		vcpu_set_guest_csr(vcpu, MSR_IA32_TSC_DEADLINE, val);

		timer = &vplic->vtimer.timer;
		del_timer(timer);

		if (val != 0UL) {
			/* transfer guest tsc to host tsc */
			val -= exec_vmread64(VMX_TSC_OFFSET_FULL);
			timer->fire_tick = val;
			/* vplic_init_timer has been called,
			 * and timer->fire_tick is not 0,here
			 * add_timer should not return error
			 */
			(void)add_timer(timer);
		} else {
			timer->fire_tick = 0UL;
		}
	} else {
		/* No action required */
	}
}

static void
vplic_write_esr(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic;

	lapic = &(vplic->apic_page);
	lapic->esr.v = vplic->esr_pending;
	vplic->esr_pending = 0U;
}

static void
vplic_set_tmr(struct acrn_vplic *vplic, uint32_t vector, bool level)
{
	struct lapic_reg *tmrptr = &(vplic->apic_page.tmr[0]);
	if (level) {
		if (!bitmap32_test_and_set_lock((uint16_t)(vector & 0x1fU), &tmrptr[(vector & 0xffU) >> 5U].v)) {
			vcpu_set_eoi_exit_bitmap(vplic2vcpu(vplic), vector);
		}
	} else {
		if (bitmap32_test_and_clear_lock((uint16_t)(vector & 0x1fU), &tmrptr[(vector & 0xffU) >> 5U].v)) {
			vcpu_clear_eoi_exit_bitmap(vplic2vcpu(vplic), vector);
		}
	}
}

static void
vplic_reset_tmr(struct acrn_vplic *vplic)
{
	int16_t i;
	struct lapic_regs *lapic;

	dev_dbg(DBG_LEVEL_VLAPIC,
			"vplic resetting all vectors to edge-triggered");

	lapic = &(vplic->apic_page);
	for (i = 0; i < 8; i++) {
		lapic->tmr[i].v = 0U;
	}

	vcpu_reset_eoi_exit_bitmaps(vplic2vcpu(vplic));
}

static void vplic_accept_intr(struct acrn_vplic *vplic, uint32_t vector, bool level)
{
	struct lapic_regs *lapic;
	struct lapic_reg *irrptr;
	uint32_t idx;

	lapic = &(vplic->apic_page);
	idx = vector >> 5U;
	irrptr = &lapic->irr[0];

	/* If the interrupt is set, don't try to do it again */
	if (!bitmap32_test_and_set_lock((uint16_t)(vector & 0x1fU), &irrptr[idx].v)) {
		/* update TMR if interrupt trigger mode has changed */
		vplic_set_tmr(vplic, vector, level);
		vcpu_make_request(vplic2vcpu(vplic), ACRN_REQUEST_EVENT);
	}
}

/*
 * @pre vector >= 16
 */
static void vplic_accept_intr(struct acrn_vplic *vplic, uint32_t vector, bool level)
{
	struct lapic_regs *lapic;
//	ASSERT(vector <= NR_MAX_VECTOR, "invalid vector %u", vector);

	lapic = &(vplic->apic_page);
	if ((lapic->svr.v & APIC_SVR_ENABLE) == 0U) {
		dev_dbg(DBG_LEVEL_VLAPIC, "vplic is software disabled, ignoring interrupt %u", vector);
	} else {
		signal_event(&vplic2vcpu(vplic)->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
		vplic->ops->accept_intr(vplic, vector, level);
	}
}

/**
 * @brief Send notification vector to target pCPU.
 *
 * If APICv Posted-Interrupt is enabled and target pCPU is in non-root mode,
 * pCPU will sync pending virtual interrupts from PIR to vIRR automatically,
 * without VM exit.
 * If pCPU in root-mode, virtual interrupt will be injected in next VM entry.
 *
 * @param[in] dest_pcpu_id Target CPU ID.
 * @param[in] anv Activation Notification Vectors (ANV)
 *
 * @return None
 */
static void apicv_trigger_pi_anv(uint16_t dest_pcpu_id, uint32_t anv)
{
	send_single_swi(dest_pcpu_id, anv);
}

/**
 * @pre offset value shall be one of the folllowing values:
 *	APIC_OFFSET_CMCI_LVT
 *	APIC_OFFSET_TIMER_LVT
 *	APIC_OFFSET_THERM_LVT
 *	APIC_OFFSET_PERF_LVT
 *	APIC_OFFSET_LINT0_LVT
 *	APIC_OFFSET_LINT1_LVT
 *	APIC_OFFSET_ERROR_LVT
 */
static inline uint32_t
lvt_off_to_idx(uint32_t offset)
{
	uint32_t index;

	switch (offset) {
	case APIC_OFFSET_CMCI_LVT:
		index = APIC_LVT_CMCI;
		break;
	case APIC_OFFSET_TIMER_LVT:
		index = APIC_LVT_TIMER;
		break;
	case APIC_OFFSET_THERM_LVT:
		index = APIC_LVT_THERMAL;
		break;
	case APIC_OFFSET_PERF_LVT:
		index = APIC_LVT_PMC;
		break;
	case APIC_OFFSET_LINT0_LVT:
		index = APIC_LVT_LINT0;
		break;
	case APIC_OFFSET_LINT1_LVT:
		index = APIC_LVT_LINT1;
		break;
	case APIC_OFFSET_ERROR_LVT:
	default:
		/*
		 * The function caller could guarantee the pre condition.
		 * So, all of the possible 'offset' other than
		 * APIC_OFFSET_ERROR_LVT has been handled in prior cases.
		 */
		index = APIC_LVT_ERROR;
		break;
	}

	return index;
}

/**
 * @pre offset value shall be one of the folllowing values:
 *	APIC_OFFSET_CMCI_LVT
 *	APIC_OFFSET_TIMER_LVT
 *	APIC_OFFSET_THERM_LVT
 *	APIC_OFFSET_PERF_LVT
 *	APIC_OFFSET_LINT0_LVT
 *	APIC_OFFSET_LINT1_LVT
 *	APIC_OFFSET_ERROR_LVT
 */
static inline uint32_t *
vplic_get_lvtptr(struct acrn_vplic *vplic, uint32_t offset)
{
	struct lapic_regs *lapic = &(vplic->apic_page);
	uint32_t i;
	uint32_t *lvt_ptr;

	switch (offset) {
	case APIC_OFFSET_CMCI_LVT:
		lvt_ptr = &lapic->lvt_cmci.v;
		break;
	default:
		/*
		 * The function caller could guarantee the pre condition.
		 * All the possible 'offset' other than APIC_OFFSET_CMCI_LVT
		 * could be handled here.
		 */
		i = lvt_off_to_idx(offset);
		lvt_ptr = &(lapic->lvt[i].v);
		break;
	}
	return lvt_ptr;
}

static inline uint32_t
vplic_get_lvt(const struct acrn_vplic *vplic, uint32_t offset)
{
	uint32_t idx;

	idx = lvt_off_to_idx(offset);
	return vplic->lvt_last[idx];
}

static void
vplic_write_lvt(struct acrn_vplic *vplic, uint32_t offset)
{
	uint32_t *lvtptr, mask, val, idx;
	struct lapic_regs *lapic;
	bool error = false;

	lapic = &(vplic->apic_page);
	lvtptr = vplic_get_lvtptr(vplic, offset);
	val = *lvtptr;

	if ((lapic->svr.v & APIC_SVR_ENABLE) == 0U) {
		val |= APIC_LVT_M;
	}
	mask = APIC_LVT_M | APIC_LVT_DS | APIC_LVT_VECTOR;
	switch (offset) {
	case APIC_OFFSET_TIMER_LVT:
		mask |= APIC_LVTT_TM;
		break;
	case APIC_OFFSET_ERROR_LVT:
		break;
	case APIC_OFFSET_LINT0_LVT:
	case APIC_OFFSET_LINT1_LVT:
		mask |= APIC_LVT_TM | APIC_LVT_RIRR | APIC_LVT_IIPP;
		/* FALLTHROUGH */
	default:
		mask |= APIC_LVT_DM;
		break;
	}
	val &= mask;

	/* vplic mask/unmask LINT0 for ExtINT? */
	if ((offset == APIC_OFFSET_LINT0_LVT) &&
		((val & APIC_LVT_DM) == APIC_LVT_DM_EXTINT)) {
		uint32_t last = vplic_get_lvt(vplic, offset);
		struct acrn_vm *vm = vplic2vcpu(vplic)->vm;

		/* mask -> unmask: may from every vplic in the vm */
		if (((last & APIC_LVT_M) != 0U) && ((val & APIC_LVT_M) == 0U)) {
			if ((vm->wire_mode == VPIC_WIRE_INTR) ||
				(vm->wire_mode == VPIC_WIRE_NULL)) {
				vm->wire_mode = VPIC_WIRE_LAPIC;
				dev_dbg(DBG_LEVEL_VLAPIC,
					"vpic wire mode -> LAPIC");
			} else {
				pr_err("WARNING:invalid vpic wire mode change");
				error = true;
			}
		/* unmask -> mask: only from the vplic LINT0-ExtINT enabled */
		} else if (((last & APIC_LVT_M) == 0U) && ((val & APIC_LVT_M) != 0U)) {
			if (vm->wire_mode == VPIC_WIRE_LAPIC) {
				vm->wire_mode = VPIC_WIRE_NULL;
				dev_dbg(DBG_LEVEL_VLAPIC,
						"vpic wire mode -> NULL");
			}
		} else {
			/* APIC_LVT_M unchanged. No action required. */
		}
	} else if (offset == APIC_OFFSET_TIMER_LVT) {
		vplic_update_lvtt(vplic, val);
	} else {
		/* No action required. */
	}

	if (error == false) {
		*lvtptr = val;
		idx = lvt_off_to_idx(offset);
		vplic->lvt_last[idx] = val;
	}
}

static void
vplic_mask_lvts(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic = &(vplic->apic_page);

	lapic->lvt_cmci.v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_CMCI_LVT);

	lapic->lvt[APIC_LVT_TIMER].v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_TIMER_LVT);

	lapic->lvt[APIC_LVT_THERMAL].v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_THERM_LVT);

	lapic->lvt[APIC_LVT_PMC].v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_PERF_LVT);

	lapic->lvt[APIC_LVT_LINT0].v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_LINT0_LVT);

	lapic->lvt[APIC_LVT_LINT1].v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_LINT1_LVT);

	lapic->lvt[APIC_LVT_ERROR].v |= APIC_LVT_M;
	vplic_write_lvt(vplic, APIC_OFFSET_ERROR_LVT);
}

/*
 * @pre vec = (lvt & APIC_LVT_VECTOR) >=16
 */
static void
vplic_fire_lvt(struct acrn_vplic *vplic, uint32_t lvt)
{
	if ((lvt & APIC_LVT_M) == 0U) {
		struct acrn_vcpu *vcpu = vplic2vcpu(vplic);
		uint32_t vec = lvt & APIC_LVT_VECTOR;
		uint32_t mode = lvt & APIC_LVT_DM;

		switch (mode) {
		case APIC_LVT_DM_FIXED:
			vplic_set_intr(vcpu, vec, LAPIC_TRIG_EDGE);
			break;
		case APIC_LVT_DM_NMI:
			vcpu_inject_nmi(vcpu);
			break;
		case APIC_LVT_DM_EXTINT:
			vcpu_inject_extint(vcpu);
			break;
		default:
			/* Other modes ignored */
			pr_warn("func:%s other mode is not support\n",__func__);
			break;
		}
	}
	return;
}

/*
 * Algorithm adopted from section "Interrupt, Task and Processor Priority"
 * in Intel Architecture Manual Vol 3a.
 */
static void
vplic_update_ppr(struct acrn_vplic *vplic)
{
	uint32_t isrv, tpr, ppr;

	isrv = vplic->isrv;
	tpr = vplic->apic_page.tpr.v;

	if (prio(tpr) >= prio(isrv)) {
		ppr = tpr;
	} else {
		ppr = isrv & 0xf0U;
	}

	vplic->apic_page.ppr.v = ppr;
	dev_dbg(DBG_LEVEL_VLAPIC, "%s 0x%02x", __func__, ppr);
}

void vioapic_broadcast_eoi(const struct acrn_vm *vm, uint32_t vector)
{}

static void
vplic_process_eoi(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic = &(vplic->apic_page);
	struct lapic_reg *isrptr, *tmrptr;
	uint32_t i, vector, bitpos;

	isrptr = &lapic->isr[0];
	tmrptr = &lapic->tmr[0];

	if (vplic->isrv != 0U) {
		vector = vplic->isrv;
		i = (vector >> 5U);
		bitpos = (vector & 0x1fU);
		bitmap32_clear_nolock((uint16_t)bitpos, &isrptr[i].v);

		dev_dbg(DBG_LEVEL_VLAPIC, "EOI vector %u", vector);
		vplic_dump_isr(vplic, "vplic_process_eoi");

		vplic->isrv = vplic_find_isrv(vplic);
		vplic_update_ppr(vplic);

		if (bitmap32_test((uint16_t)bitpos, &tmrptr[i].v)) {
			/*
			 * Per Intel SDM 10.8.5, Software can inhibit the broadcast of
			 * EOI by setting bit 12 of the Spurious Interrupt Vector
			 * Register of the LAPIC.
			 * TODO: Check if the bit 12 "Suppress EOI Broadcasts" is set.
			 */
			vioapic_broadcast_eoi(vplic2vcpu(vplic)->vm, vector);
		}

		vcpu_make_request(vplic2vcpu(vplic), ACRN_REQUEST_EVENT);
	}

	dev_dbg(DBG_LEVEL_VLAPIC, "Gratuitous EOI");
}

static void
vplic_set_error(struct acrn_vplic *vplic, uint32_t mask)
{
	uint32_t lvt, vec;

	vplic->esr_pending |= mask;
	if (vplic->esr_firing == 0) {
		vplic->esr_firing = 1;

		/* The error LVT always uses the fixed delivery mode. */
		lvt = vplic_get_lvt(vplic, APIC_OFFSET_ERROR_LVT);
		if ((lvt & APIC_LVT_M) == 0U) {
			vec = lvt & APIC_LVT_VECTOR;
			if (vec >= 16U) {
				vplic_accept_intr(vplic, vec, LAPIC_TRIG_EDGE);
			}
		}
		vplic->esr_firing = 0;
	}
}
/*
 * @pre APIC_LVT_TIMER <= lvt_index <= APIC_LVT_MAX
 */
static int32_t
vplic_trigger_lvt(struct acrn_vplic *vplic, uint32_t lvt_index)
{
	uint32_t lvt;
	int32_t ret = 0;

	if (vplic_enabled(vplic) == false) {
		struct acrn_vcpu *vcpu = vplic2vcpu(vplic);
		/*
		 * When the local APIC is global/hardware disabled,
		 * LINT[1:0] pins are configured as INTR and NMI pins,
		 * respectively.
		 */
		switch (lvt_index) {
		case APIC_LVT_LINT0:
			vcpu_inject_extint(vcpu);
			break;
		case APIC_LVT_LINT1:
			vcpu_inject_nmi(vcpu);
			break;
		default:
			/*
			 * Only LINT[1:0] pins will be handled here.
			 * Gracefully return if prior case clauses have not
			 * been met.
			 */
			break;
		}
	} else {

		switch (lvt_index) {
		case APIC_LVT_LINT0:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_LINT0_LVT);
			break;
		case APIC_LVT_LINT1:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_LINT1_LVT);
			break;
		case APIC_LVT_TIMER:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_TIMER_LVT);
			lvt |= APIC_LVT_DM_FIXED;
			break;
		case APIC_LVT_ERROR:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_ERROR_LVT);
			lvt |= APIC_LVT_DM_FIXED;
			break;
		case APIC_LVT_PMC:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_PERF_LVT);
			break;
		case APIC_LVT_THERMAL:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_THERM_LVT);
			break;
		case APIC_LVT_CMCI:
			lvt = vplic_get_lvt(vplic, APIC_OFFSET_CMCI_LVT);
			break;
		default:
			lvt = 0U; /* make MISRA happy */
			ret =  -EINVAL;
			break;
		}

		if (ret == 0) {
			vplic_fire_lvt(vplic, lvt);
		}
	}
	return ret;
}

static inline void set_dest_mask_phys(struct acrn_vm *vm, uint64_t *dmask, uint32_t dest)
{
	uint16_t vcpu_id;

	vcpu_id = vm_apicid2vcpu_id(vm, dest);
	if (vcpu_id < vm->hw.created_vcpus) {
		bitmap_set_nolock(vcpu_id, dmask);
	}
}

/*
 * This function tells if a vplic belongs to the destination.
 * If yes, return true, else reture false.
 *
 * @pre vplic != NULL
 */
static inline bool is_dest_field_matched(const struct acrn_vplic *vplic, uint32_t dest)
{
	uint32_t logical_id, cluster_id, dest_logical_id, dest_cluster_id;
	uint32_t ldr = vplic->apic_page.ldr.v;
	bool ret = false;

	uint32_t dfr = vplic->apic_page.dfr.v;
	if ((dfr & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_FLAT) {
		/*
		 * In the "Flat Model" the MDA is interpreted as an 8-bit wide
		 * bitmask. This model is available in the xAPIC mode only.
		 */
		logical_id = ldr >> 24U;
		dest_logical_id = dest & 0xffU;
		if ((logical_id & dest_logical_id) != 0U) {
			ret = true;
		}
	} else if ((dfr & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_CLUSTER) {
		/*
		 * In the "Cluster Model" the MDA is used to identify a
		 * specific cluster and a set of APICs in that cluster.
		 */
		logical_id = (ldr >> 24U) & 0xfU;
		cluster_id = ldr >> 28U;
		dest_logical_id = dest & 0xfU;
		dest_cluster_id = (dest >> 4U) & 0xfU;
		if ((cluster_id == dest_cluster_id) && ((logical_id & dest_logical_id) != 0U)) {
			ret = true;
		}
	} else {
		/* Guest has configured a bad logical model for this vcpu. */
		dev_dbg(DBG_LEVEL_VLAPIC, "vplic has bad logical model %x", dfr);
	}

	return ret;
}

/*
 * This function populates 'dmask' with the set of vcpus that match the
 * addressing specified by the (dest, phys, lowprio) tuple.
 */
void
vplic_calc_dest(struct acrn_vm *vm, uint64_t *dmask, bool is_broadcast,
		uint32_t dest, bool phys, bool lowprio)
{
	struct acrn_vplic *vplic, *lowprio_dest = NULL;
	struct acrn_vcpu *vcpu;
	uint16_t vcpu_id;

	*dmask = 0UL;
	if (is_broadcast) {
		/* Broadcast in both logical and physical modes. */
		*dmask = vm_active_cpus(vm);
	} else if (phys) {
		/* Physical mode: "dest" is local APIC ID. */
		set_dest_mask_phys(vm, dmask, dest);
	} else {
		/*
		 * Logical mode: "dest" is message destination addr
		 * to be compared with the logical APIC ID in LDR.
		 */
		foreach_vcpu(vcpu_id, vm, vcpu) {
			vplic = vm_lapic_from_vcpu_id(vm, vcpu_id);
			if (!is_dest_field_matched(vplic, dest)) {
				continue;
			}

			if (lowprio) {
				/*
				 * for lowprio delivery mode, the lowest-priority one
				 * among all "dest" matched processors accepts the intr.
				 */
				if (lowprio_dest == NULL) {
					lowprio_dest = vplic;
				} else if (lowprio_dest->apic_page.ppr.v > vplic->apic_page.ppr.v) {
					lowprio_dest = vplic;
				} else {
					/* No other state currently, do nothing */
				}
			} else {
				bitmap_set_nolock(vcpu_id, dmask);
			}
		}

		if (lowprio && (lowprio_dest != NULL)) {
			bitmap_set_nolock(vplic2vcpu(lowprio_dest)->vcpu_id, dmask);
		}
	}
}

static void
vplic_process_init_sipi(struct acrn_vcpu* target_vcpu, uint32_t mode, uint32_t icr_low)
{
	get_vm_lock(target_vcpu->vm);
	if (mode == APIC_DELMODE_INIT) {
		if ((icr_low & APIC_LEVEL_MASK) != APIC_LEVEL_DEASSERT) {

			dev_dbg(DBG_LEVEL_VLAPIC,
				"Sending INIT to %hu",
				target_vcpu->vcpu_id);

			if (target_vcpu->state != VCPU_INIT) {
				/* put target vcpu to INIT state and wait for SIPI */
				zombie_vcpu(target_vcpu, VCPU_ZOMBIE);
				reset_vcpu(target_vcpu, INIT_RESET);
			}
			/* new cpu model only need one SIPI to kick AP run,
			 * the second SIPI will be ignored as it move out of
			 * wait-for-SIPI state.
			*/
			target_vcpu->arch.nr_sipi = 1U;
		}
	} else if (mode == APIC_DELMODE_STARTUP) {
		/* Ignore SIPIs in any state other than wait-for-SIPI */
		if ((target_vcpu->state == VCPU_INIT) &&
			(target_vcpu->arch.nr_sipi != 0U)) {

			dev_dbg(DBG_LEVEL_VLAPIC,
				"Sending SIPI to %hu with vector %u",
				 target_vcpu->vcpu_id,
				(icr_low & APIC_VECTOR_MASK));

			target_vcpu->arch.nr_sipi--;
			if (target_vcpu->arch.nr_sipi <= 0U) {

				pr_err("Start Secondary VCPU%hu for VM[%d]...",
					target_vcpu->vcpu_id,
					target_vcpu->vm->vm_id);

				set_vcpu_startup_entry(target_vcpu, (icr_low & APIC_VECTOR_MASK) << 12U);
				vcpu_make_request(target_vcpu, ACRN_REQUEST_INIT_VMCS);
				launch_vcpu(target_vcpu);
			}
		}
	} else {
		/* No other state currently, do nothing */
	}
	put_vm_lock(target_vcpu->vm);
	return;
}

static void vplic_write_icrlo(struct acrn_vplic *vplic)
{
	uint16_t vcpu_id;
	bool phys = false, is_broadcast = false;
	uint64_t dmask = 0UL;
	uint32_t icr_low, icr_high, dest;
	uint32_t vec, mode, shorthand;
	struct lapic_regs *lapic;
	struct acrn_vcpu *target_vcpu;

	lapic = &(vplic->apic_page);
	lapic->icr_lo.v &= ~APIC_DELSTAT_PEND;

	icr_low = lapic->icr_lo.v;
	icr_high = lapic->icr_hi.v;
	dest = icr_high >> APIC_ID_SHIFT;
	is_broadcast = (dest == 0xffU);
	vec = icr_low & APIC_VECTOR_MASK;
	mode = icr_low & APIC_DELMODE_MASK;
	phys = ((icr_low & APIC_DESTMODE_LOG) == 0UL);
	shorthand = icr_low & APIC_DEST_MASK;

	if ((mode == APIC_DELMODE_FIXED) && (vec < 16U)) {
		vplic_set_error(vplic, APIC_ESR_SEND_ILLEGAL_VECTOR);
		dev_dbg(DBG_LEVEL_VLAPIC, "Ignoring invalid IPI %u", vec);
	} else if (((shorthand == APIC_DEST_SELF) || (shorthand == APIC_DEST_ALLISELF))
			&& ((mode == APIC_DELMODE_NMI) || (mode == APIC_DELMODE_INIT)
			|| (mode == APIC_DELMODE_STARTUP))) {
		dev_dbg(DBG_LEVEL_VLAPIC, "Invalid ICR value");
	} else {
		struct acrn_vcpu *vcpu = vplic2vcpu(vplic);

		dev_dbg(DBG_LEVEL_VLAPIC,
			"icrlo 0x%08x icrhi 0x%08x triggered ipi %u",
				icr_low, icr_high, vec);

		switch (shorthand) {
		case APIC_DEST_DESTFLD:
			vplic_calc_dest(vcpu->vm, &dmask, is_broadcast, dest, phys, false);
			break;
		case APIC_DEST_SELF:
			bitmap_set_nolock(vcpu->vcpu_id, &dmask);
			break;
		case APIC_DEST_ALLISELF:
			dmask = vm_active_cpus(vcpu->vm);
			break;
		case APIC_DEST_ALLESELF:
			dmask = vm_active_cpus(vcpu->vm);
			bitmap_clear_nolock(vplic2vcpu(vplic)->vcpu_id, &dmask);
			break;
		default:
			/*
			 * All possible values of 'shorthand' has been handled in prior
			 * case clauses.
			 */
			break;
		}

		for (vcpu_id = 0U; vcpu_id < vcpu->vm->hw.created_vcpus; vcpu_id++) {
			if ((dmask & (1UL << vcpu_id)) != 0UL) {
				target_vcpu = vcpu_from_vid(vcpu->vm, vcpu_id);

				if (mode == APIC_DELMODE_FIXED) {
					vplic_set_intr(target_vcpu, vec, LAPIC_TRIG_EDGE);
					dev_dbg(DBG_LEVEL_VLAPIC,
						"vplic sending ipi %u to vcpu_id %hu",
						vec, vcpu_id);
				} else if (mode == APIC_DELMODE_NMI) {
					vcpu_inject_nmi(target_vcpu);
					dev_dbg(DBG_LEVEL_VLAPIC,
						"vplic send ipi nmi to vcpu_id %hu", vcpu_id);
				} else if (mode == APIC_DELMODE_INIT) {
					vplic_process_init_sipi(target_vcpu, mode, icr_low);
				} else if (mode == APIC_DELMODE_STARTUP) {
					vplic_process_init_sipi(target_vcpu, mode, icr_low);
				} else if (mode == APIC_DELMODE_SMI) {
					pr_info("vplic: SMI IPI do not support\n");
				} else {
					pr_err("Unhandled icrlo write with mode %u\n", mode);
				}
			}
		}
	}
}

static inline uint32_t vplic_find_highest_irr(const struct acrn_vplic *vplic)
{
	const struct lapic_regs *lapic = &(vplic->apic_page);
	uint32_t i, val, bitpos, vec = 0U;
	const struct lapic_reg *irrptr;

	irrptr = &lapic->irr[0];

	/* i ranges effectively from 7 to 1 */
	for (i = 7U; i > 0U; i--) {
		val = irrptr[i].v;
		if (val != 0U) {
			//bitpos = (uint32_t)fls32(val);
			vec = (i * 32U) + bitpos;
			break;
		}
	}

	return vec;
}

/**
 * @brief Find a deliverable virtual interrupts for vLAPIC in irr.
 *
 * @param[in]    vplic Pointer to target vLAPIC data structure
 * @param[inout] vecptr Pointer to vector buffer and will be filled
 *               with eligible vector if any.
 *
 * @retval false There is no deliverable pending vector.
 * @retval true There is deliverable vector.
 *
 * @remark The vector does not automatically transition to the ISR as a
 *	   result of calling this function.
 *	   This function is only for case that APICv/VID is NOT supported.
 */
static bool vplic_find_deliverable_intr(const struct acrn_vplic *vplic, uint32_t *vecptr)
{
	const struct lapic_regs *lapic = &(vplic->apic_page);
	uint32_t vec;
	bool ret = false;

	vec = vplic_find_highest_irr(vplic);
	if (prio(vec) > prio(lapic->ppr.v)) {
		ret = true;
		if (vecptr != NULL) {
			*vecptr = vec;
		}
	}

	return ret;
}

/**
 * @brief Get a deliverable virtual interrupt from irr to isr.
 *
 * Transition 'vector' from IRR to ISR. This function is called with the
 * vector returned by 'vplic_find_deliverable_intr()' when the guest is able to
 * accept this interrupt (i.e. RFLAGS.IF = 1 and no conditions exist that
 * block interrupt delivery).
 *
 * @param[in] vplic Pointer to target vLAPIC data structure
 * @param[in] vector Target virtual interrupt vector
 *
 * @return None
 *
 * @pre vplic != NULL
 */
static void vplic_get_deliverable_intr(struct acrn_vplic *vplic, uint32_t vector)
{
	struct lapic_regs *lapic = &(vplic->apic_page);
	struct lapic_reg *irrptr, *isrptr;
	uint32_t idx;

	/*
	 * clear the ready bit for vector being accepted in irr
	 * and set the vector as in service in isr.
	 */
	idx = vector >> 5U;

	irrptr = &lapic->irr[0];
	bitmap32_clear_lock((uint16_t)(vector & 0x1fU), &irrptr[idx].v);

	vplic_dump_irr(vplic, "vplic_get_deliverable_intr");

	isrptr = &lapic->isr[0];
	bitmap32_set_nolock((uint16_t)(vector & 0x1fU), &isrptr[idx].v);
	vplic_dump_isr(vplic, "vplic_get_deliverable_intr");

	vplic->isrv = vector;

	/*
	 * Update the PPR
	 */
	vplic_update_ppr(vplic);
}

static void
vplic_write_svr(struct acrn_vplic *vplic)
{
	struct lapic_regs *lapic;
	uint32_t old, new, changed;

	lapic = &(vplic->apic_page);

	new = lapic->svr.v;
	old = vplic->svr_last;
	vplic->svr_last = new;

	changed = old ^ new;
	if ((changed & APIC_SVR_ENABLE) != 0U) {
		if ((new & APIC_SVR_ENABLE) == 0U) {
			struct acrn_vm *vm = vplic2vcpu(vplic)->vm;
			/*
			 * The apic is now disabled so stop the apic timer
			 * and mask all the LVT entries.
			 */
			dev_dbg(DBG_LEVEL_VLAPIC, "vplic is software-disabled");
			del_timer(&vplic->vtimer.timer);

			vplic_mask_lvts(vplic);
			/* the only one enabled LINT0-ExtINT vplic disabled */
			if (vm->wire_mode == VPIC_WIRE_NULL) {
				vm->wire_mode = VPIC_WIRE_INTR;
				dev_dbg(DBG_LEVEL_VLAPIC,
					"vpic wire mode -> INTR");
			}
		} else {
			/*
			 * The apic is now enabled so restart the apic timer
			 * if it is configured in periodic mode.
			 */
			dev_dbg(DBG_LEVEL_VLAPIC, "vplic is software-enabled");
			if (vplic_lvtt_period(vplic)) {
				if (set_expiration(vplic)) {
					/* vplic_init_timer has been called,
					 * and timer->fire_tick is not 0,here
					 *  add_timer should not return error
					 */
					(void)add_timer(&vplic->vtimer.timer);
				}
			}
		}
	}
}

static int32_t vplic_read(struct acrn_vplic *vplic, uint32_t offset_arg, uint64_t *data)
{
	int32_t ret = 0;
	struct lapic_regs *lapic = &(vplic->apic_page);
	uint32_t i;
	uint32_t offset = offset_arg;
	*data = 0UL;

	if (offset > sizeof(*lapic)) {
		ret = -EACCES;
	} else {

		offset &= ~0x3UL;
		switch (offset) {
		case APIC_OFFSET_ID:
			*data = lapic->id.v;
			break;
		case APIC_OFFSET_VER:
			*data = lapic->version.v;
			break;
		case APIC_OFFSET_PPR:
			*data = lapic->ppr.v;
			break;
		case APIC_OFFSET_EOI:
			*data = lapic->eoi.v;
			break;
		case APIC_OFFSET_LDR:
			*data = lapic->ldr.v;
			break;
		case APIC_OFFSET_DFR:
			*data = lapic->dfr.v;
			break;
		case APIC_OFFSET_SVR:
			*data = lapic->svr.v;
			break;
		case APIC_OFFSET_ISR0:
		case APIC_OFFSET_ISR1:
		case APIC_OFFSET_ISR2:
		case APIC_OFFSET_ISR3:
		case APIC_OFFSET_ISR4:
		case APIC_OFFSET_ISR5:
		case APIC_OFFSET_ISR6:
		case APIC_OFFSET_ISR7:
			i = (offset - APIC_OFFSET_ISR0) >> 4U;
			*data = lapic->isr[i].v;
			break;
		case APIC_OFFSET_TMR0:
		case APIC_OFFSET_TMR1:
		case APIC_OFFSET_TMR2:
		case APIC_OFFSET_TMR3:
		case APIC_OFFSET_TMR4:
		case APIC_OFFSET_TMR5:
		case APIC_OFFSET_TMR6:
		case APIC_OFFSET_TMR7:
			i = (offset - APIC_OFFSET_TMR0) >> 4U;
			*data = lapic->tmr[i].v;
			break;
		case APIC_OFFSET_IRR0:
		case APIC_OFFSET_IRR1:
		case APIC_OFFSET_IRR2:
		case APIC_OFFSET_IRR3:
		case APIC_OFFSET_IRR4:
		case APIC_OFFSET_IRR5:
		case APIC_OFFSET_IRR6:
		case APIC_OFFSET_IRR7:
			i = (offset - APIC_OFFSET_IRR0) >> 4U;
			*data = lapic->irr[i].v;
			break;
		case APIC_OFFSET_ESR:
			*data = lapic->esr.v;
			break;
		case APIC_OFFSET_ICR_LOW:
			*data = lapic->icr_lo.v;
			break;
		case APIC_OFFSET_ICR_HI:
			*data = lapic->icr_hi.v;
			break;
		case APIC_OFFSET_CMCI_LVT:
		case APIC_OFFSET_TIMER_LVT:
		case APIC_OFFSET_THERM_LVT:
		case APIC_OFFSET_PERF_LVT:
		case APIC_OFFSET_LINT0_LVT:
		case APIC_OFFSET_LINT1_LVT:
		case APIC_OFFSET_ERROR_LVT:
			*data = vplic_get_lvt(vplic, offset);
#ifdef INVARIANTS
			reg = vplic_get_lvtptr(vplic, offset);
			ASSERT(*data == *reg, "inconsistent lvt value at offset %#x: %#lx/%#x", offset, *data, *reg);
#endif
			break;
		case APIC_OFFSET_TIMER_ICR:
			/* if TSCDEADLINE mode always return 0*/
			if (vplic_lvtt_tsc_deadline(vplic)) {
				*data = 0UL;
			} else {
				*data = lapic->icr_timer.v;
			}
			break;
		case APIC_OFFSET_TIMER_CCR:
			*data = vplic_get_ccr(vplic);
			break;
		case APIC_OFFSET_TIMER_DCR:
			*data = lapic->dcr_timer.v;
			break;
		default:
			ret = -EACCES;
			break;
		}
	}

	dev_dbg(DBG_LEVEL_VLAPIC, "vplic read offset %x, data %lx", offset, *data);
	return ret;
}

static int32_t vplic_write(struct acrn_vplic *vplic, uint32_t offset, uint64_t data)
{
	struct lapic_regs *lapic = &(vplic->apic_page);
	uint32_t *regptr;
	uint32_t data32 = (uint32_t)data;
	int32_t ret = 0;

//	ASSERT(((offset & 0xfU) == 0U) && (offset < PAGE_SIZE),
//		"%s: invalid offset %#x", __func__, offset);

	dev_dbg(DBG_LEVEL_VLAPIC, "vplic write offset %#x, data %#lx", offset, data);

	if (offset <= sizeof(*lapic)) {
		switch (offset) {
		case APIC_OFFSET_ID:
			/* Force APIC ID as read only */
			break;
		case APIC_OFFSET_EOI:
			vplic_process_eoi(vplic);
			break;
		case APIC_OFFSET_LDR:
			lapic->ldr.v = data32;
			vplic_write_ldr(vplic);
			break;
		case APIC_OFFSET_DFR:
			lapic->dfr.v = data32;
			vplic_write_dfr(vplic);
			break;
		case APIC_OFFSET_SVR:
			lapic->svr.v = data32;
			vplic_write_svr(vplic);
			break;
		case APIC_OFFSET_ICR_LOW:
			lapic->icr_lo.v = data32;
			vplic_write_icrlo(vplic);
			break;
		case APIC_OFFSET_ICR_HI:
			lapic->icr_hi.v = data32;
			break;
		case APIC_OFFSET_CMCI_LVT:
		case APIC_OFFSET_TIMER_LVT:
		case APIC_OFFSET_THERM_LVT:
		case APIC_OFFSET_PERF_LVT:
		case APIC_OFFSET_LINT0_LVT:
		case APIC_OFFSET_LINT1_LVT:
		case APIC_OFFSET_ERROR_LVT:
			regptr = vplic_get_lvtptr(vplic, offset);
			*regptr = data32;
			vplic_write_lvt(vplic, offset);
			break;
		case APIC_OFFSET_TIMER_ICR:
			/* if TSCDEADLINE mode ignore icr_timer */
			if (vplic_lvtt_tsc_deadline(vplic)) {
				break;
			}
			lapic->icr_timer.v = data32;
			vplic_write_icrtmr(vplic);
			break;

		case APIC_OFFSET_TIMER_DCR:
			lapic->dcr_timer.v = data32;
			vplic_write_dcr(vplic);
			break;
		case APIC_OFFSET_ESR:
			vplic_write_esr(vplic);
			break;

		case APIC_OFFSET_SELF_IPI:
			/* falls through */

		default:
			ret = -EACCES;
			/* Read only */
			break;
		}
	} else {
		ret = -EACCES;
	}

	return ret;
}

/*
 * @pre vplic != NULL && ops != NULL
 */
void
vplic_reset(struct acrn_vplic *vplic, const struct acrn_apicv_ops *ops, enum reset_mode mode)
{
	struct lapic_regs *lapic;
	uint64_t preserved_lapic_mode = vplic->csr_apicbase & APICBASE_LAPIC_MODE;
	uint32_t preserved_apic_id = vplic->apic_page.id.v;

	vplic->csr_apicbase = DEFAULT_APIC_BASE;

	if (vplic2vcpu(vplic)->vcpu_id == BSP_CPU_ID) {
		vplic->csr_apicbase |= APICBASE_BSP;
	}
	if (mode == INIT_RESET) {
		if ((preserved_lapic_mode & APICBASE_ENABLED) != 0U ) {
			/* Per SDM 10.12.5.1 vol.3, need to preserve lapic mode after INIT */
			vplic->csr_apicbase |= preserved_lapic_mode;
		}
	} else {
		/* Upon reset, vplic is set to xAPIC mode. */
		vplic->csr_apicbase |= APICBASE_XAPIC;
	}

	lapic = &(vplic->apic_page);
	(void)memset((void *)lapic, 0U, sizeof(struct lapic_regs));

	if (mode == INIT_RESET) {
		if ((preserved_lapic_mode & APICBASE_ENABLED) != 0U ) {
			/* the local APIC ID register should be preserved in XAPIC or X2APIC mode */
			lapic->id.v = preserved_apic_id;
		}
	} else {
		lapic->id.v = vplic->vapic_id;
	}
	lapic->version.v = VLAPIC_VERSION;
	lapic->version.v |= (VLAPIC_MAXLVT_INDEX << MAXLVTSHIFT);
	lapic->dfr.v = 0xffffffffU;
	lapic->svr.v = APIC_SVR_VECTOR;
	vplic_mask_lvts(vplic);
	vplic_reset_tmr(vplic);

	lapic->icr_timer.v = 0U;
	lapic->dcr_timer.v = 0U;
	vplic_write_dcr(vplic);
	vplic_reset_timer(vplic);

	vplic->svr_last = lapic->svr.v;

	vplic->isrv = 0U;

	vplic->ops = ops;
}

void vplic_restore(struct acrn_vplic *vplic, const struct lapic_regs *regs)
{
	struct lapic_regs *lapic;
	int32_t i;

	lapic = &(vplic->apic_page);

	lapic->tpr = regs->tpr;
	lapic->apr = regs->apr;
	lapic->ppr = regs->ppr;
	lapic->ldr = regs->ldr;
	lapic->dfr = regs->dfr;
	for (i = 0; i < 8; i++) {
		lapic->tmr[i].v = regs->tmr[i].v;
	}
	lapic->svr = regs->svr;
	vplic_write_svr(vplic);
	lapic->lvt[APIC_LVT_TIMER].v = regs->lvt[APIC_LVT_TIMER].v;
	lapic->lvt[APIC_LVT_LINT0].v = regs->lvt[APIC_LVT_LINT0].v;
	lapic->lvt[APIC_LVT_LINT1].v = regs->lvt[APIC_LVT_LINT1].v;
	lapic->lvt[APIC_LVT_ERROR].v = regs->lvt[APIC_LVT_ERROR].v;
	lapic->icr_timer = regs->icr_timer;
	lapic->ccr_timer = regs->ccr_timer;
	lapic->dcr_timer = regs->dcr_timer;
	vplic_write_dcr(vplic);
}

uint64_t vplic_get_apicbase(const struct acrn_vplic *vplic)
{
	return vplic->csr_apicbase;
}

int32_t vplic_set_apicbase(struct acrn_vplic *vplic, uint64_t new)
{
	int32_t ret = 0;
	uint64_t changed;
	bool change_in_vplic_mode = false;

	if (vplic->csr_apicbase != new) {
		changed = vplic->csr_apicbase ^ new;
		change_in_vplic_mode = ((changed & APICBASE_LAPIC_MODE) != 0U);

		/*
		 * TODO: Logic to check for change in Reserved Bits and Inject GP
		 */

		/*
		 * Logic to check for change in Bits 11:10 for vLAPIC mode switch
		 */
		if (change_in_vplic_mode) {
			if ((new & APICBASE_LAPIC_MODE) ==
						(APICBASE_XAPIC | APICBASE_X2APIC)) {
				struct acrn_vcpu *vcpu = vplic2vcpu(vplic);

				vplic->csr_apicbase = new;
				update_vm_vplic_state(vcpu->vm);
			} else {
				/*
				 * TODO: Logic to check for Invalid transitions, Invalid State
				 * and mode switch according to SDM 10.12.5
				 * Fig. 10-27
				 */
			}
		}

		/*
		 * TODO: Logic to check for change in Bits 35:12 and Bit 7 and emulate
		 */
	}

	return ret;
}

void
vplic_receive_intr(struct acrn_vm *vm, bool level, uint32_t dest, bool phys,
		uint32_t delmode, uint32_t vec, bool rh)
{
	bool lowprio;
	uint16_t vcpu_id;
	uint64_t dmask;
	struct acrn_vcpu *target_vcpu;

	if ((delmode != IOAPIC_RTE_DELMODE_FIXED) &&
			(delmode != IOAPIC_RTE_DELMODE_LOPRI) &&
			(delmode != IOAPIC_RTE_DELMODE_EXINT)) {
		dev_dbg(DBG_LEVEL_VLAPIC,
			"vplic intr invalid delmode %#x", delmode);
	} else {
		lowprio = (delmode == IOAPIC_RTE_DELMODE_LOPRI) || rh;

		/*
		 * We don't provide any virtual interrupt redirection hardware so
		 * all interrupts originating from the ioapic or MSI specify the
		 * 'dest' in the legacy xAPIC format.
		 */
		vplic_calc_dest(vm, &dmask, false, dest, phys, lowprio);

		for (vcpu_id = 0U; vcpu_id < vm->hw.created_vcpus; vcpu_id++) {
			struct acrn_vplic *vplic;
			if ((dmask & (1UL << vcpu_id)) != 0UL) {
				target_vcpu = vcpu_from_vid(vm, vcpu_id);

				/* only make request when vplic enabled */
				vplic = vcpu_vplic(target_vcpu);
				if (vplic_enabled(vplic)) {
					if (delmode == IOAPIC_RTE_DELMODE_EXINT) {
						vcpu_inject_extint(target_vcpu);
					} else {
						vplic_set_intr(target_vcpu, vec, level);
					}
				}
			}
		}
	}
}

/*
 *  @pre vcpu != NULL
 *  @pre vector <= 255U
 */
void
vplic_set_intr(struct acrn_vcpu *vcpu, uint32_t vector, bool level)
{
	struct acrn_vplic *vplic;

	vplic = vcpu_vplic(vcpu);
	if (vector < 16U) {
		vplic_set_error(vplic, APIC_ESR_RECEIVE_ILLEGAL_VECTOR);
		dev_dbg(DBG_LEVEL_VLAPIC,
		    "vplic ignoring interrupt to vector %u", vector);
	} else {
		vplic_accept_intr(vplic, vector, level);
	}
}

/**
 * @brief Triggers LAPIC local interrupt(LVT).
 *
 * @param[in] vm           Pointer to VM data structure
 * @param[in] vcpu_id_arg  ID of vCPU, BROADCAST_CPU_ID means triggering
 *			   interrupt to all vCPUs.
 * @param[in] lvt_index    The index which LVT would be to be fired.
 *
 * @retval 0 on success.
 * @retval -EINVAL on error that vcpu_id_arg or vector of the LVT is invalid.
 *
 * @pre vm != NULL
 */
int32_t
vplic_set_local_intr(struct acrn_vm *vm, uint16_t vcpu_id_arg, uint32_t lvt_index)
{
	struct acrn_vplic *vplic;
	uint64_t dmask = 0UL;
	int32_t error;
	uint16_t vcpu_id = vcpu_id_arg;

	if ((vcpu_id != BROADCAST_CPU_ID) && (vcpu_id >= vm->hw.created_vcpus)) {
	        error = -EINVAL;
	} else {
		if (vcpu_id == BROADCAST_CPU_ID) {
			dmask = vm_active_cpus(vm);
		} else {
			bitmap_set_nolock(vcpu_id, &dmask);
		}
		error = 0;
		for (vcpu_id = 0U; vcpu_id < vm->hw.created_vcpus; vcpu_id++) {
			if ((dmask & (1UL << vcpu_id)) != 0UL) {
				vplic = vm_lapic_from_vcpu_id(vm, vcpu_id);
				error = vplic_trigger_lvt(vplic, lvt_index);
				if (error != 0) {
					break;
				}
			}
		}
	}

	return error;
}

/**
 * @brief Inject MSI to target VM.
 *
 * @param[in] vm   Pointer to VM data structure
 * @param[in] addr MSI address.
 * @param[in] msg  MSI data.
 *
 * @retval 0 on success.
 * @retval -1 on error that addr is invalid.
 *
 * @pre vm != NULL
 */
int32_t
vplic_intr_msi(struct acrn_vm *vm, uint64_t addr, uint64_t msg)
{
	uint32_t delmode, vec;
	uint32_t dest;
	bool phys, rh;
	int32_t ret;
	union msi_addr_reg address;
	union msi_data_reg data;

	address.full = addr;
	data.full = (uint32_t) msg;
	dev_dbg(DBG_LEVEL_VLAPIC, "lapic MSI addr: %#lx msg: %#lx", address.full, data.full);

	if (address.bits.addr_base == MSI_ADDR_BASE) {
		/*
		 * Extract the x86-specific fields from the MSI addr/msg
		 * params according to the Intel Arch spec, Vol3 Ch 10.
		 *
		 * The PCI specification does not support level triggered
		 * MSI/MSI-X so ignore trigger level in 'msg'.
		 *
		 * The 'dest' is interpreted as a logical APIC ID if both
		 * the Redirection Hint and Destination Mode are '1' and
		 * physical otherwise.
		 */
		dest = address.bits.dest_field;
		phys = (address.bits.dest_mode == MSI_ADDR_DESTMODE_PHYS);
		rh = (address.bits.rh == MSI_ADDR_RH);

		delmode = (uint32_t)(data.bits.delivery_mode);
		vec = (uint32_t)(data.bits.vector);

		dev_dbg(DBG_LEVEL_VLAPIC, "lapic MSI %s dest %#x, vec %u",
			phys ? "physical" : "logical", dest, vec);

		vplic_receive_intr(vm, LAPIC_TRIG_EDGE, dest, phys, delmode, vec, rh);
		ret = 0;
	} else {
		dev_dbg(DBG_LEVEL_VLAPIC, "lapic MSI invalid addr %#lx", address.full);
	        ret = -1;
	}

	return ret;
}

/* interrupt context */
static void vplic_timer_expired(void *data)
{
	struct acrn_vcpu *vcpu = (struct acrn_vcpu *)data;
	struct acrn_vplic *vplic;
	struct lapic_regs *lapic;

	vplic = vcpu_vplic(vcpu);
	lapic = &(vplic->apic_page);

	/* inject vcpu timer interrupt if not masked */
	if (!vplic_lvtt_masked(vplic)) {
		vplic_set_intr(vcpu, lapic->lvt[APIC_LVT_TIMER].v & APIC_LVTT_VECTOR, LAPIC_TRIG_EDGE);
	}

	if (!vplic_lvtt_period(vplic)) {
		vplic->vtimer.timer.fire_tick = 0UL;
	}
}

/**
 *  @pre vcpu != NULL
 */
void vplic_create(struct acrn_vcpu *vcpu, uint16_t pcpu_id)
{
	struct acrn_vplic *vplic = vcpu_vplic(vcpu);

	if (is_vcpu_bsp(vcpu)) {
		uint64_t *pml4_page =
			(uint64_t *)vcpu->vm->arch_vm.s2ptp;
		/* only need unmap it from SOS as UOS never mapped it */
		if (is_sos_vm(vcpu->vm)) {
			s2pt_del_mr(vcpu->vm, pml4_page,
				DEFAULT_APIC_BASE, PAGE_SIZE);
		}

		s2pt_add_mr(vcpu->vm, pml4_page,
			vplic_apicv_get_apic_access_addr(),
			DEFAULT_APIC_BASE, PAGE_SIZE,
			EPT_WR | EPT_RD | EPT_UNCACHED);
	}

	vplic_init_timer(vplic);

	if (is_sos_vm(vcpu->vm)) {
		/*
		 * For SOS_VM type, pLAPIC IDs need to be used because
		 * host ACPI tables are passthru to SOS.
		 * Get APIC ID sequence format from cpu_storage
		 */
		vplic->vapic_id = per_cpu(lapic_id, pcpu_id);
	} else {
		vplic->vapic_id = (uint32_t)vcpu->vcpu_id;
	}

	dev_dbg(DBG_LEVEL_VLAPIC, "vplic APIC ID : 0x%04x", vplic->vapic_id);
}

/*
 *  @pre vcpu != NULL
 */
void vplic_free(struct acrn_vcpu *vcpu)
{
	struct acrn_vplic *vplic = vcpu_vplic(vcpu);

	del_timer(&vplic->vtimer.timer);

}

/**
 * APIC-v functions
 * @pre get_pi_desc(vplic2vcpu(vplic)) != NULL
 */
static bool
apicv_set_intr_ready(struct acrn_vplic *vplic, uint32_t vector)
{
	struct pi_desc *pid;
	uint32_t idx;
	bool notify = false;

	pid = get_pi_desc(vplic2vcpu(vplic));
	idx = vector >> 6U;
	if (!bitmap_test_and_set_lock((uint16_t)(vector & 0x3fU), &pid->pir[idx])) {
		notify = (bitmap_test_and_set_lock(POSTED_INTR_ON, &pid->control.value) == false);
	}
	return notify;
}

/**
 *APIC-v: Get the HPA to APIC-access page
 * **/
uint64_t
vplic_apicv_get_apic_access_addr(void)
{
	/*APIC-v APIC-access address */
	static uint8_t apicv_apic_access_addr[PAGE_SIZE] __aligned(PAGE_SIZE);

	return hva2hpa(apicv_apic_access_addr);
}

/**
 *APIC-v: Get the HPA to virtualized APIC registers page
 * **/
uint64_t
vplic_apicv_get_apic_page_addr(struct acrn_vplic *vplic)
{
	return hva2hpa(&(vplic->apic_page));
}

static void vplic_inject_intr(struct acrn_vplic *vplic,
		bool guest_irq_enabled, bool injected)
{
	uint32_t vector = 0U;

	if (guest_irq_enabled && (!injected)) {
		vplic_update_ppr(vplic);
		if (vplic_find_deliverable_intr(vplic, &vector)) {
			exec_vmwrite32(VMX_ENTRY_INT_INFO_FIELD, VMX_INT_INFO_VALID | vector);
			vplic_get_deliverable_intr(vplic, vector);
		}
	}

	vplic_update_tpr_threshold(vplic);
}

void vplic_inject_intr(struct acrn_vplic *vplic, bool guest_irq_enabled, bool injected)
{
	vplic->ops->inject_intr(vplic, guest_irq_enabled, injected);
}

static bool vplic_has_pending_delivery_intr(struct acrn_vcpu *vcpu)
{
	uint32_t vector;
	struct acrn_vplic *vplic = vcpu_vplic(vcpu);

	vplic_update_ppr(vplic);

	/* check and raise request if we have a deliverable irq in LAPIC IRR */
	if (vplic_find_deliverable_intr(vplic, &vector)) {
		/* we have pending IRR */
		vcpu_make_request(vcpu, ACRN_REQUEST_EVENT);
	}

	return vcpu->arch.pending_req != 0UL;
}

bool vplic_has_pending_delivery_intr(struct acrn_vcpu *vcpu)
{
	struct acrn_vplic *vplic = vcpu_vplic(vcpu);
	return vplic->ops->has_pending_delivery_intr(vcpu);
}

static bool vplic_has_pending_intr(struct acrn_vcpu *vcpu)
{
	struct acrn_vplic *vplic = vcpu_vplic(vcpu);
	uint32_t vector;

	vector = vplic_find_highest_irr(vplic);

	return vector != 0UL;
}

bool vplic_has_pending_intr(struct acrn_vcpu *vcpu)
{
	struct acrn_vplic *vplic = vcpu_vplic(vcpu);
	return vplic->ops->has_pending_intr(vcpu);
}

static bool vplic_apic_read_access_may_valid(__unused uint32_t offset)
{
	return true;
}

static bool vplic_apic_write_access_may_valid(uint32_t offset)
{
	return (offset != APIC_OFFSET_SELF_IPI);
}

int32_t apic_access_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t err;
	uint32_t offset;
	uint64_t qual, access_type;
	struct acrn_vplic *vplic;
	struct mmio_request *mmio;

	qual = vcpu->arch.exit_qualification;
	access_type = apic_access_type(qual);

	/*
	 * We only support linear access for a data read/write during instruction execution.
	 * for other access types:
	 * a) we don't support vLAPIC work in real mode;
	 * 10 = guest-physical access during event delivery
	 * 15 = guest-physical access for an instruction fetch or during instruction execution
	 * b) we don't support fetch from APIC-access page since its memory type is UC;
	 * 2 = linear access for an instruction fetch
	 * c) we suppose the guest goes wrong when it will access the APIC-access page
	 * when process event-delivery. According chap 26.5.1.2 VM Exits During Event Injection,
	 * vol 3, sdm: If the "virtualize APIC accesses" VM-execution control is 1 and
	 * event delivery generates an access to the APIC-access page, that access is treated as
	 * described in Section 29.4 and may cause a VM exit.
	 * 3 = linear access (read or write) during event delivery
	 */
	if (((access_type == TYPE_LINEAR_APIC_INST_READ) || (access_type == TYPE_LINEAR_APIC_INST_WRITE)) &&
			(decode_instruction(vcpu) >= 0)) {
		vplic = vcpu_vplic(vcpu);
		offset = (uint32_t)apic_access_offset(qual);
		mmio = &vcpu->req.reqs.mmio;
		if (access_type == TYPE_LINEAR_APIC_INST_WRITE) {
/*
			err = emulate_instruction(vcpu);
			if (err == 0) {
				if (vplic->ops->apic_write_access_may_valid(offset)) {
					(void)vplic_write(vplic, offset, mmio->value);
				}
			}
*/
		} else {
			if (vplic->ops->apic_read_access_may_valid(offset)) {
				(void)vplic_read(vplic, offset, &mmio->value);
			} else {
				mmio->value = 0UL;
			}
//			err = emulate_instruction(vcpu);
		}
//		TRACE_2L(TRACE_VMEXIT_APICV_ACCESS, qual, (uint64_t)vplic);
	} else {
		pr_err("%s, unhandled access type: %lu\n", __func__, access_type);
		err = -EINVAL;
	}

	return err;
}

int32_t veoi_vmexit_handler(struct acrn_vcpu *vcpu)
{
	struct acrn_vplic *vplic = NULL;

	uint32_t vector;
	struct lapic_regs *lapic;
	struct lapic_reg *tmrptr;
	uint32_t idx;

	vcpu_retain_rip(vcpu);

	vplic = vcpu_vplic(vcpu);
	lapic = &(vplic->apic_page);
	vector = (uint32_t)(vcpu->arch.exit_qualification & 0xFFUL);

	tmrptr = &lapic->tmr[0];
	idx = vector >> 5U;

	if (bitmap32_test((uint16_t)(vector & 0x1fU), &tmrptr[idx].v)) {
		/* hook to vIOAPIC */
		vioapic_broadcast_eoi(vcpu->vm, vector);
	}

//	TRACE_2L(TRACE_VMEXIT_APICV_VIRT_EOI, vector, 0UL);

	return 0;
}

int32_t apic_write_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint64_t qual;
	int32_t err = 0;
	uint32_t offset;
	struct acrn_vplic *vplic = NULL;

	qual = vcpu->arch.exit_qualification;
	offset = (uint32_t)(qual & 0xFFFUL);

	vcpu_retain_rip(vcpu);
	vplic = vcpu_vplic(vcpu);

	switch (offset) {
	case APIC_OFFSET_ID:
		/* Force APIC ID as read only */
		break;
	case APIC_OFFSET_LDR:
		vplic_write_ldr(vplic);
		break;
	case APIC_OFFSET_DFR:
		vplic_write_dfr(vplic);
		break;
	case APIC_OFFSET_SVR:
		vplic_write_svr(vplic);
		break;
	case APIC_OFFSET_ESR:
		vplic_write_esr(vplic);
		break;
	case APIC_OFFSET_ICR_LOW:
		vplic_write_icrlo(vplic);
		break;
	case APIC_OFFSET_CMCI_LVT:
	case APIC_OFFSET_TIMER_LVT:
	case APIC_OFFSET_THERM_LVT:
	case APIC_OFFSET_PERF_LVT:
	case APIC_OFFSET_LINT0_LVT:
	case APIC_OFFSET_LINT1_LVT:
	case APIC_OFFSET_ERROR_LVT:
		vplic_write_lvt(vplic, offset);
		break;
	case APIC_OFFSET_TIMER_ICR:
		vplic_write_icrtmr(vplic);
		break;
	case APIC_OFFSET_TIMER_DCR:
		vplic_write_dcr(vplic);
		break;
	case APIC_OFFSET_SELF_IPI:
		/* falls through */
	default:
		err = -EACCES;
		pr_err("Unhandled APIC-Write, offset:0x%x", offset);
		break;
	}

//	TRACE_2L(TRACE_VMEXIT_APICV_WRITE, offset, 0UL);

	return err;
}

static const struct acrn_apicv_ops acrn_vplic_ops = {
	.accept_intr = vplic_accept_intr,
	.inject_intr = vplic_inject_intr,
	.has_pending_delivery_intr = vplic_has_pending_delivery_intr,
	.has_pending_intr = vplic_has_pending_intr,
	.apic_read_access_may_valid = vplic_apic_read_access_may_valid,
	.apic_write_access_may_valid = vplic_apic_write_access_may_valid,
	.x2apic_read_csr_may_valid = NULL,
	.x2apic_write_csr_may_valid = NULL,
};

/*
 * set apicv ops for apicv basic mode or apicv advenced mode.
 */
void vplic_set_apicv_ops(void)
{
	vplic_ops = &acrn_vplic_ops;
}
