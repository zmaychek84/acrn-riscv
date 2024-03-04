/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/guest/vmcs.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/vmx.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/per_cpu.h>
#include <asm/init.h>
//#include <cpu_caps.h>
//#include <cpufeatures.h>
#include <asm/guest/vcsr.h>
#include <asm/guest/vmexit.h>
#include <logmsg.h>

#ifndef CONFIG_MACRN
static void init_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	vcpu_set_gpreg(vcpu, OFFSET_REG_A0, vcpu->vcpu_id);
	//cpu_csr_write(vsstatus, ctx->run_ctx.sstatus);
	cpu_csr_write(vsstatus, 0x2000C0000);
	cpu_csr_write(vsepc, ctx->run_ctx.sepc);

	/* use magic # to check if vs-mode switching succeeds or not */
	//cpu_csr_write(vsepc, 0xaaaabbbb);
	cpu_csr_write(vsip, ctx->run_ctx.sip);
	cpu_csr_write(vsie, ctx->run_ctx.sie);
	cpu_csr_write(vstvec, ctx->run_ctx.stvec);
	cpu_csr_write(vsscratch, ctx->run_ctx.sscratch);
	cpu_csr_write(vstval, ctx->run_ctx.stval);
	cpu_csr_write(vscause, ctx->run_ctx.scause);
	cpu_csr_write(vsatp, ctx->run_ctx.satp);
}

static void load_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	cpu_csr_write(vsstatus, ctx->run_ctx.sstatus);
//	cpu_csr_write(vsstatus, 0x2000C0000);
	cpu_csr_write(vsepc, ctx->run_ctx.sepc);

	/* use magic # to check if vs-mode switching succeeds or not */
	//cpu_csr_write(vsepc, 0xaaaabbbb);
	cpu_csr_write(vsip, ctx->run_ctx.sip);
	cpu_csr_write(vsie, ctx->run_ctx.sie);
	cpu_csr_write(vstvec, ctx->run_ctx.stvec);
	cpu_csr_write(vsscratch, ctx->run_ctx.sscratch);
	cpu_csr_write(vstval, ctx->run_ctx.stval);
	cpu_csr_write(vscause, ctx->run_ctx.scause);
	cpu_csr_write(vsatp, ctx->run_ctx.satp);
}

static void save_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	ctx->run_ctx.sstatus = cpu_csr_read(vsstatus);
	ctx->run_ctx.sepc = cpu_csr_read(vsepc);
	ctx->run_ctx.sip = cpu_csr_read(vsip);
	ctx->run_ctx.sie = cpu_csr_read(vsie);
	ctx->run_ctx.stvec = cpu_csr_read(vstvec);
	ctx->run_ctx.sscratch = cpu_csr_read(vsscratch);
	ctx->run_ctx.stval = cpu_csr_read(vstval);
	ctx->run_ctx.scause = cpu_csr_read(vscause);
	ctx->run_ctx.satp = cpu_csr_read(vsatp);
}

static void init_host_state(struct acrn_vcpu *vcpu)
{
	uint64_t value64;
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	pr_dbg("Initialize host state");
	pr_dbg("guest kernel entry: %lx\n", _vboot);

	value64 = 0x200000180;
	//value64 = 0x200200080;
	cpu_csr_set(hstatus, value64);
	ctx->run_ctx.cpu_gp_regs.regs.hstatus = value64;

	/* must set the SPP in order to enter into guest s-mode */
	value64 = 0x2000C0100;
	cpu_csr_set(sstatus, value64);
	ctx->run_ctx.cpu_gp_regs.regs.status = value64;
	//value64 = (uint64_t)_vkernel;
//	cpu_csr_write(sepc, value64);

	value64 = 0xf0bfff;
	cpu_csr_write(hedeleg, value64);
}
#else
static void init_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	vcpu_set_gpreg(vcpu, OFFSET_REG_A0, vcpu->vcpu_id);
	//cpu_csr_write(vsstatus, ctx->run_ctx.sstatus);
	cpu_csr_write(sstatus, 0x2000C0000);
	cpu_csr_write(sepc, ctx->run_ctx.sepc);

	/* use magic # to check if vs-mode switching succeeds or not */
	//cpu_csr_write(vsepc, 0xaaaabbbb);
	cpu_csr_write(sip, ctx->run_ctx.sip);
	cpu_csr_write(sie, ctx->run_ctx.sie);
	cpu_csr_write(stvec, ctx->run_ctx.stvec);
	cpu_csr_write(sscratch, ctx->run_ctx.sscratch);
	cpu_csr_write(stval, ctx->run_ctx.stval);
	cpu_csr_write(scause, ctx->run_ctx.scause);
	cpu_csr_write(satp, ctx->run_ctx.satp);
}

static void load_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	cpu_csr_write(sstatus, ctx->run_ctx.sstatus);
//	cpu_csr_write(vsstatus, 0x2000C0000);
	cpu_csr_write(sepc, ctx->run_ctx.sepc);

	/* use magic # to check if vs-mode switching succeeds or not */
	//cpu_csr_write(vsepc, 0xaaaabbbb);
	cpu_csr_write(sip, ctx->run_ctx.sip);
	cpu_csr_write(sie, ctx->run_ctx.sie);
	cpu_csr_write(stvec, ctx->run_ctx.stvec);
	cpu_csr_write(sscratch, ctx->run_ctx.sscratch);
	cpu_csr_write(stval, ctx->run_ctx.stval);
	cpu_csr_write(scause, ctx->run_ctx.scause);
	cpu_csr_write(satp, ctx->run_ctx.satp);
}

static void save_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	ctx->run_ctx.sstatus = cpu_csr_read(sstatus);
	ctx->run_ctx.sepc = cpu_csr_read(sepc);
	ctx->run_ctx.sip = cpu_csr_read(sip);
	ctx->run_ctx.sie = cpu_csr_read(sie);
	ctx->run_ctx.stvec = cpu_csr_read(stvec);
	ctx->run_ctx.sscratch = cpu_csr_read(sscratch);
	ctx->run_ctx.stval = cpu_csr_read(stval);
	ctx->run_ctx.scause = cpu_csr_read(scause);
	ctx->run_ctx.satp = cpu_csr_read(satp);
}

static void init_host_state(struct acrn_vcpu *vcpu)
{
	uint64_t value64;
	struct guest_cpu_context *ctx = &vcpu->arch.contexts[vcpu->arch.cur_context];

	pr_dbg("Initialize host state");
	pr_dbg("guest kernel entry: %lx\n", _vboot);

/*
	value64 = 0x200000800;
	cpu_csr_set(mstatus, value64);
	ctx->run_ctx.cpu_gp_regs.regs.hstatus = value64;
*/

	/* must set the MPP in order to enter into guest s-mode */
	value64 = 0x200000800;
	cpu_csr_set(mstatus, value64);
	ctx->run_ctx.cpu_gp_regs.regs.status = value64;
	value64 = 0xf0b55f;
	cpu_csr_write(medeleg, value64);
}
#endif

/**
 * @pre vcpu != NULL
 */
void init_vmcs(struct acrn_vcpu *vcpu)
{
	void **vcpu_ptr = &get_cpu_var(vcpu_run);

	/* Log message */
	pr_dbg("Initializing VMCS");
	/* Initialize the Virtual Machine Control Structure (VMCS) */
	init_host_state(vcpu);
	init_guest_state(vcpu);
	*vcpu_ptr = (void *)vcpu;
}

/**
 * @pre vcpu != NULL
 */
void load_vmcs(struct acrn_vcpu *vcpu)
{
	void **vcpu_ptr = &get_cpu_var(vcpu_run);
	load_guest_state(vcpu);
	*vcpu_ptr = (void *)vcpu;
}

void save_vmcs(struct acrn_vcpu *vcpu)
{
	save_guest_state(vcpu);
}
