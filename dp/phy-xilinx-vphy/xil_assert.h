/*******************************************************************************
* Copyright (C) 2015 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: GPL-2.0
*******************************************************************************/
/*****************************************************************************/
/**
*
* @file xil_assert.h
*
* This file contains assert related functions.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date   Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a hbm  07/14/09 First release
* </pre>
*
******************************************************************************/

#ifndef XIL_ASSERT_H	/* prevent circular inclusions */
#define XIL_ASSERT_H	/* by using protection macros */

#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/


/************************** Constant Definitions *****************************/

#define XIL_ASSERT_NONE     0U
#define XIL_ASSERT_OCCURRED 1U
#define XNULL NULL

/**
 * This data type defines a callback to be invoked when an
 * assert occurs. The callback is invoked only when asserts are enabled
 */
//typedef void (*Xil_AssertCallback) (const char8 *File, s32 Line);

/***************** Macros (Inline Functions) Definitions *********************/

//#define Xil_Assert(const char8 *File, s32 Line)
//#define Xil_AssertSetCallback(Xil_AssertCallback Routine)

#define Xil_AssertVoid(Expression)
#define Xil_AssertVoidAlways()
#define Xil_AssertNonvoid(Expression)
#define Xil_AssertNonvoidAlways()


/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif	/* end of protection macro */
