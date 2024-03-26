/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <asm/guest/instr_emul.h>
#include <asm/guest/vcpu.h>

#define INS16_OPCODE_MASK	0xC000
#define INS16_OPRD_MASK		0x1C
#define INS16_OPRS2_MASK	0x1C

#define INS16_OPCODE_LQ		0x1
#define INS16_OPCODE_LW		0x2
#define INS16_OPCODE_LD		0x3
#define INS16_OPCODE_SQ		0x5
#define INS16_OPCODE_SW		0x6
#define INS16_OPCODE_SD		0x7

#define INS32_OPCODE_MASK	0x7F
#define INS32_OPRD_MASK		0xF80
#define INS32_OPRS1_MASK	0xF8000
#define INS32_OPRS2_MASK	0x1F00000
#define INS32_OPSIZE_MASK	0xF000

#define INS32_OPCODE_LD		0x3
#define INS32_OPCODE_ST		0x23
#define INS32_OPSIZE_BYTE	0x0
#define INS32_OPSIZE_HALF	0x1
#define INS32_OPSIZE_WORD	0x2
#define INS32_OPSIZE_DWORD	0x3

static int32_t emulate_ins16(struct acrn_vcpu *vcpu, uint32_t ins)
{
	struct acrn_mmio_request *mmio_req = &vcpu->req.reqs.mmio_request;
	uint64_t pc;
	int32_t reg, rc = 0;

	pc = vcpu_get_gpreg(vcpu, CPU_REG_IP);
	vcpu_set_gpreg(vcpu, CPU_REG_IP, pc + 2);

	if ((INS16_OPCODE_MASK & ins) > 0x8000) {
		reg = (ins & INS16_OPRD_MASK) >> 2;
		vcpu_set_gpreg(vcpu, reg, mmio_req->value);
	} else {
		reg = (ins & INS16_OPRS2_MASK) >> 2;
		mmio_req->value = vcpu_get_gpreg(vcpu, reg);
	}

	return rc;
}

int32_t emulate_ins32(struct acrn_vcpu *vcpu, uint32_t ins)
{
	struct acrn_mmio_request *mmio_req = &vcpu->req.reqs.mmio_request;
	uint64_t pc;
	int32_t reg, rc = 0;

	pc = vcpu_get_gpreg(vcpu, CPU_REG_IP);
	vcpu_set_gpreg(vcpu, CPU_REG_IP, pc + 4);

	if ((INS32_OPCODE_MASK & ins) == 0x3) {
		reg = (ins & INS32_OPRD_MASK) >> 7;
		vcpu_set_gpreg(vcpu, reg, mmio_req->value);
	} else if ((INS32_OPCODE_MASK & ins) == 0x23) {
		reg = (ins & INS32_OPRS2_MASK) >> 20;
		mmio_req->value = vcpu_get_gpreg(vcpu, reg);
	} else {
		rc = -EFAULT;
	}

	return rc;
}

int32_t emulate_instruction(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen)
{
	if (xlen == 32)
		return emulate_ins32(vcpu, ins);
	else
		return emulate_ins16(vcpu, ins);
}

static int32_t decode_ins16(struct acrn_vcpu *vcpu, uint32_t ins)
{
	int size = (ins & INS16_OPCODE_MASK) >> 13;

	switch (size) {
	case INS16_OPCODE_LQ:
	case INS16_OPCODE_SQ:
		size = 16;
		break;
	case INS16_OPCODE_LW:
	case INS16_OPCODE_SW:
		size = 4;
		break;
	case INS16_OPCODE_LD:
	case INS16_OPCODE_SD:
	default:
		size = 8;
		break;
	}

	return size;
}

static int32_t decode_ins32(struct acrn_vcpu *vcpu, uint32_t ins)
{
	int size = (ins & INS32_OPSIZE_MASK) >> 12;

	switch (size) {
	case INS32_OPSIZE_BYTE:
		size = 1;
		break;
	case INS32_OPSIZE_HALF:
		size = 2;
		break;
	case INS32_OPSIZE_WORD:
		size = 4;
		break;
	case INS32_OPSIZE_DWORD:
	default:
		size = 8;
		break;
	}

	return size;
}

int32_t decode_instruction(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen)
{
	if (xlen == 32)
		return decode_ins32(vcpu, ins);
	else
		return decode_ins16(vcpu, ins);
}
