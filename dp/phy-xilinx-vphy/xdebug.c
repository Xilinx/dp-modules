/*******************************************************************************
* Copyright (C) 2015 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: GPL-2.0
*******************************************************************************/
#include <linux/kernel.h>
#include <linux/stdarg.h>
#include "xil_types.h"
#include "xdebug.h"

void XHdcp1x_SetDebugPrintf(XDebug_Printf PrintfFunc);

XDebug_Printf xdebugPrintf = NULL;	/**< Instance of function
											  *  interface used for debug
											  *  print statement */

char *xdebugBuff = NULL;
int xdebugBuffSize = 0;
int *xdebugBuffPos = NULL;

static void XDebug_DebugBufPrintf(const char *fmt, ...)
{
	if(xdebugBuff != NULL)
	{
		va_list args;
		va_start(args, fmt);
		*xdebugBuffPos += vscnprintf(xdebugBuff + *xdebugBuffPos,
				xdebugBuffSize - *xdebugBuffPos, fmt, args);
		va_end(args);
	}
}


/*****************************************************************************/
/**
* This function sets the debug printf function for the module to print to the
* supplied buffer.
*
* @param	buff is the buffer to print to.
* @param	buff_size is the maximum size of the buffer
* @param	buff_pos is the current (and will be updated) position in the buffer
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XDebug_SetDebugBufPrintf(char *buff, int buff_size, int *buff_pos)
{
	if(buff)
	{
		xdebugBuff = buff;
		xdebugBuffSize = buff_size;
		xdebugBuffPos = buff_pos;
		XDebug_SetDebugPrintf(XDebug_DebugBufPrintf);
	} else {
		XDebug_SetDebugPrintf(NULL);
		xdebugBuff = NULL;
		xdebugBuffSize = 0;
		xdebugBuffPos = NULL;
	}
}

/*****************************************************************************/
/**
* This function sets the debug printf function for the module.
*
* @param	PrintfFunc is the printf function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XDebug_SetDebugPrintf(XDebug_Printf PrintfFunc)
{
	xdebugPrintf = PrintfFunc;
}


