/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/atomic.h>
#include <asm/apicreg.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/guest/instr_emul.h>
#include <asm/guest/vmexit.h>
#include <asm/vmx.h>
#include <asm/guest/s2vm.h>
#include <asm/pgtable.h>
#include <io_req.h>
#include <trace.h>
#include <logmsg.h>

void arch_fire_hsm_interrupt(void)
{
	struct acrn_vm *sos_vm;
	struct acrn_vcpu *vcpu;

	sos_vm = get_sos_vm();
	vcpu = vcpu_from_vid(sos_vm, BSP_CPU_ID);

	//vclint_set_intr(vcpu, get_hsm_notification_vector());
	vclint_set_intr(vcpu);
}

/**
 * Dummy handler, riscv doesn't support pio.
 */
void
emulate_pio_complete(struct acrn_vcpu *vcpu, const struct io_request *io_req)
{
	pr_fatal("Wrong state, should not reach here!\n");
}

int32_t mmio_access_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int ret;
	int32_t status = -1;
	uint64_t exit_qual;
	uint64_t gpa;
	uint32_t ins;
	struct io_request *io_req = &vcpu->req;
	struct acrn_mmio_request *mmio_req = &io_req->reqs.mmio_request;
	struct run_context *ctx =
		&vcpu->arch.contexts[vcpu->arch.cur_context].run_ctx;

	/* Handle page fault from guest */
	exit_qual = vcpu->arch.exit_qualification;
	ins = *(uint32_t *)(ctx->cpu_gp_regs.regs.ip);
	gpa = ctx->cpu_gp_regs.regs.tval;
	io_req->io_type = ACRN_IOREQ_TYPE_MMIO;

	/* Specify if read or write operation */
	switch (exit_qual) {
	case HX_EXIT_PF_GUEST_STORE:
	case HX_EXIT_STORE_ACCESS:
		/* Write operation */
		mmio_req->direction = ACRN_IOREQ_DIR_WRITE;
		mmio_req->value = 0UL;
		break;
	case HX_EXIT_PF_GUEST_LOAD:
	case HX_EXIT_LOAD_ACCESS:
		/* Read operation */
		mmio_req->direction = ACRN_IOREQ_DIR_READ;
		break;
	default:
		status = -1;
		pr_acrnlog("unsupported access to address: 0x%016lx", gpa);
		return status;
		break;
	}

	mmio_req->address = gpa;
	ret = decode_instruction(vcpu, ins);
	if (ret > 0) {
		mmio_req->size = (uint64_t)ret;
		if (mmio_req->address > CLINT_MEM_ADDR && mmio_req->address + ret < CLINT_MEM_REGION) {
			status = clint_access_vmexit_handler(vcpu);
			return status;
		}

		/*
		 * For MMIO write, ask DM to run MMIO emulation after
		 * instruction emulation. For MMIO read, ask DM to run MMIO
		 * emulation at first.
		 */

		/* Determine value being written. */
		if (mmio_req->direction == ACRN_IOREQ_DIR_WRITE) {
			status = emulate_instruction(vcpu, ins);
			if (status != 0) {
				ret = -EFAULT;
			}
		}

		if (ret > 0) {
			status = emulate_io(vcpu, io_req);
		}
	} else {
		if (ret == -EFAULT) {
			pr_info("page fault happen during decode_instruction");
			status = 0;
		}
	}
	if (ret <= 0) {
		//pr_acrnlog("Guest Linear Address: 0x%016lx", exec_vmread(VMX_GUEST_LINEAR_ADDR));
		pr_acrnlog("Guest Physical Address address: 0x%016lx", gpa);
	}

	return status;
}

/**
 * Dummy handler, riscv doesn't support pio.
 */
void allow_guest_pio_access(struct acrn_vm *vm, uint16_t port_address,
		uint32_t nbytes)
{
	pr_fatal("Wrong state, should not reach here!\n");
}

void deny_guest_pio_access(struct acrn_vm *vm, uint16_t port_address,
		uint32_t nbytes)
{
	pr_fatal("Wrong state, should not reach here!\n");
}
