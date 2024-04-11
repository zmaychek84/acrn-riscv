/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_APP_H__
#define __RISCV_APP_H__
#define __userfunc __attribute__((__section__(".app.text"))) 
#define __userdata __attribute__((__section__(".app.data"))) 
#endif
