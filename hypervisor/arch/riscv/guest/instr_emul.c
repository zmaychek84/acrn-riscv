/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <asm/guest/instr_emul.h>
#include <asm/guest/vcpu.h>

#define OPCODE_MASK	0x7F
#define OPRD_MASK	0xF80
#define OPRS1_MASK	0xF8000
#define OPRS2_MASK	0x1F00000
#define OPSIZE_MASK	0xF000

#define OPCODE_LD	0x3
#define OPCODE_ST	0x23
#define OPSIZE_BYTE	0x0
#define OPSIZE_HALF	0x1
#define OPSIZE_WORD	0x2
#define OPSIZE_DWORD	0x3

int32_t emulate_instruction(struct acrn_vcpu *vcpu, uint32_t ins)
{
	struct acrn_mmio_request *mmio_req = &vcpu->req.reqs.mmio_request;
	uint64_t pc;
	int32_t reg, rc = 0;

	pc = vcpu_get_gpreg(vcpu, CPU_REG_IP);
	vcpu_set_gpreg(vcpu, CPU_REG_IP, pc + 4);

	if ((OPCODE_MASK & ins) == 0x3) {
		reg = (ins & OPRD_MASK) >> 7;
		vcpu_set_gpreg(vcpu, reg, mmio_req->value);
	} else if ((OPCODE_MASK	& ins) == 0x23) {
		reg = (ins & OPRS2_MASK) >> 20;
		mmio_req->value = vcpu_get_gpreg(vcpu, reg);
	} else {
		rc = -EFAULT;
	}

	return rc;
}

int32_t decode_instruction(struct acrn_vcpu *vcpu, uint32_t ins)
{
	struct acrn_mmio_request *mmio_req = &vcpu->req.reqs.mmio_request;
	int size = (ins & OPSIZE_MASK) >> 12;

	switch (size) {
	case OPSIZE_BYTE:
		size = 1;
		break;
	case OPSIZE_HALF:
		size = 2;
		break;
	case OPSIZE_WORD:
		size = 4;
		break;
	case OPSIZE_DWORD:
	default:
		size = 8;
		break;
	}

	return size;
}
