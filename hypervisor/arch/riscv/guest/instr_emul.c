/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/instr_emul.h>
#include <asm/guest/vcpu.h>

int32_t emulate_instruction(struct acrn_vcpu *vcpu)
{
	uint64_t pc;
	pc = vcpu_get_gpreg(vcpu, CPU_REG_IP);
	vcpu_set_gpreg(vcpu, CPU_REG_IP, pc + 4);
	return 0;
}

int32_t decode_instruction(struct acrn_vcpu *vcpu)
{
	return 8;
}
