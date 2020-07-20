/*******************************************************************************
* Copyright (C) 2015 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: GPL-2.0
*******************************************************************************/

#ifndef SLEEP_H
#define SLEEP_H

#include "xil_types.h"
#include "xil_io.h"

#ifdef __cplusplus
extern "C" {
#endif

int usleep(unsigned long useconds);
unsigned sleep(unsigned int seconds);

#ifdef __cplusplus
}
#endif

#endif
