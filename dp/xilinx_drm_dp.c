// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA DisplayPort TX Subsystem Driver
 *
 *  Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 *	Venkateshwar Rao Gannavarapu <vgannava@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_of.h>

#include <linux/gpio/consumer.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>
#include <video/videomode.h>

#include "include/linux/phy/phy-vphy.h"

static uint xlnx_dp_aux_timeout_ms = 50;
module_param_named(aux_timeout_ms, xlnx_dp_aux_timeout_ms, uint, 0444);
MODULE_PARM_DESC(aux_timeout_ms, "DP aux timeout value in msec (default: 50)");

/*
 * Some sink requires a delay after power on request
 */
static uint xlnx_dp_power_on_delay_ms = 4;
module_param_named(power_on_delay_ms, xlnx_dp_power_on_delay_ms, uint, 0444);
MODULE_PARM_DESC(aux_timeout_ms, "DP power on delay in msec (default: 4)");

/*
 * Dynamic Phy Configuration
 */
u32 set_vphy(int);

/* Link configuration registers */

#define XLNX_DP_TX_LINK_BW_SET				0x0
#define XLNX_DP_TX_LANE_CNT_SET				0x4
#define XLNX_DP_TX_ENHANCED_FRAME_EN			0x8
#define XLNX_DP_TX_TRAINING_PATTERN_SET			0xc
#define XLNX_DP_TX_LINK_QUAL_PATTERN_SET		0x10
#define XLNX_DP_TX_SCRAMBLING_DISABLE			0x14
#define XLNX_DP_TX_DOWNSPREAD_CTL			0x18
#define XLNX_DP_TX_SW_RESET				0x1c

#define XLNX_DP_TX_SW_RESET_STREAM1			BIT(0)
#define XLNX_DP_TX_SW_RESET_STREAM2			BIT(1)
#define XLNX_DP_TX_SW_RESET_STREAM3			BIT(2)
#define XLNX_DP_TX_SW_RESET_STREAM4			BIT(3)
#define XLNX_DP_TX_SW_RESET_AUX				BIT(7)
#define XLNX_DP_TX_SW_RESET_ALL	(XLNX_DP_TX_SW_RESET_STREAM1 | \
					XLNX_DP_TX_SW_RESET_STREAM2 | \
					XLNX_DP_TX_SW_RESET_STREAM3 | \
					XLNX_DP_TX_SW_RESET_STREAM4 | \
					XLNX_DP_TX_SW_RESET_AUX)

/* DPTX Core enable registers */
#define XLNX_DP_TX_ENABLE				0x80
#define XLNX_DP_TX_ENABLE_MAIN_STREAM			0x84
#define XLNX_DP_TX_ENABLE_SEC_STREAM			0x88
#define XLNX_DP_TX_FORCE_SCRAMBLER_RESET		0xc0
#define XLNX_DP_TX_MST_CONFIG				0xd0
#define XLNX_DP_TX_LINE_RESET_DISABLE			0xf0

/*DPTX Core ID registers */
#define XLNX_DP_TX_VERSION				0xf8
#define XLNX_DP_TX_VERSION_MAJOR_MASK			GENMASK(31, 24)
#define XLNX_DP_TX_VERSION_MAJOR_SHIFT			0x18
#define XLNX_DP_TX_VERSION_MINOR_MASK			GENMASK(23, 16)
#define XLNX_DP_TX_VERSION_MINOR_SHIFT			0x10
#define XLNX_DP_TX_VERSION_REVISION_MASK		GENMASK(15, 12)
#define XLNX_DP_TX_VERSION_REVISION_SHIFT		0xc
#define XLNX_DP_TX_VERSION_PATCH_MASK			GENMASK(11, 8)
#define XLNX_DP_TX_VERSION_PATCH_SHIFT			0x8
#define XLNX_DP_TX_VERSION_INTERNAL_MASK		GENMASK(7, 0)
#define XLNX_DP_TX_VERSION_INTERNAL_SHIFT		0x0

/* Core ID registers */
#define XLNX_DP_TX_CORE_ID				0xfc
#define XLNX_DP_TX_CORE_ID_MAJOR_MASK			GENMASK(31, 24)
#define XLNX_DP_TX_CORE_ID_MAJOR_SHIFT			0x18
#define XLNX_DP_TX_CORE_ID_MINOR_MASK			GENMASK(23, 16)
#define XLNX_DP_TX_CORE_ID_MINOR_SHIFT			0x10
#define XLNX_DP_TX_CORE_ID_REVISION_MASK		GENMASK(15, 8)
#define XLNX_DP_TX_CORE_ID_REVISION_SHIFT		0x8
#define XLNX_DP_TX_CORE_ID_DIRECTION			GENMASK(1)

/* AUX channel interface registers */
#define XLNX_DP_TX_AUX_COMMAND				0x100
#define XLNX_DP_TX_AUX_COMMAND_CMD_SHIFT		0x8
#define XLNX_DP_TX_AUX_COMMAND_ADDRESS_ONLY		BIT(12)
#define XLNX_DP_TX_AUX_COMMAND_BYTES_SHIFT		0x0
#define XLNX_DP_TX_AUX_WRITE_FIFO			0x104
#define XLNX_DP_TX_AUX_ADDRESS				0x108
#define XLNX_DP_TX_CLK_DIVIDER				0x10c
#define XLNX_DP_TX_CLK_DIVIDER_MHZ			1000000
#define XLNX_DP_TX_CLK_DIVIDER_AUX_FILTER_SHIFT		0x8
#define XLNX_DP_TX_USER_FIFO_OVERFLOW			0x110
#define XLNX_DP_TX_INTR_SIGNAL_STATE			0x130
#define XLNX_DP_TX_INTR_SIGNAL_STATE_HPD		BIT(0)
#define XLNX_DP_TX_INTR_SIGNAL_STATE_REQUEST		BIT(1)
#define XLNX_DP_TX_INTR_SIGNAL_STATE_REPLY		BIT(2)
#define XLNX_DP_TX_INTR_SIGNAL_STATE_REPLY_TIMEOUT	BIT(3)
#define XLNX_DP_TX_AUX_REPLY_DATA			0x134
#define XLNX_DP_TX_AUX_REPLY_CODE			0x138
#define XLNX_DP_TX_AUX_REPLY_CODE_AUX_ACK		(0)
#define XLNX_DP_TX_AUX_REPLY_CODE_AUX_NACK		BIT(0)
#define XLNX_DP_TX_AUX_REPLY_CODE_AUX_DEFER		BIT(1)
#define XLNX_DP_TX_AUX_REPLY_CODE_I2C_ACK		(0)
#define XLNX_DP_TX_AUX_REPLY_CODE_I2C_NACK		BIT(2)
#define XLNX_DP_TX_AUX_REPLY_CODE_I2C_DEFER		BIT(3)
#define XLNX_DP_TX_AUX_REPLY_CNT			0x13c
#define XLNX_DP_TX_AUX_REPLY_CNT_MASK			0xff
#define XLNX_DP_TX_INTR_STATUS				0x140
#define XLNX_DP_TX_INTR_MASK				0x144
#define XLNX_DP_TX_INTR_HPD_IRQ				BIT(0)
#define XLNX_DP_TX_INTR_HPD_EVENT			0x2
#define XLNX_DP_TX_INTR_REPLY_RECV			BIT(2)
#define XLNX_DP_TX_INTR_REPLY_TIMEOUT			BIT(3)
#define XLNX_DP_TX_INTR_HPD_PULSE			0x10
#define XLNX_DP_TX_INTR_EXT_PKT_TXD			BIT(5)
#define XLNX_DP_TX_INTR_CHBUF_UNDERFLW_MASK		0x3f0000
#define XLNX_DP_TX_INTR_CHBUF_OVERFLW_MASK		0xfc00000
#define XLNX_DP_TX_INTR_ALL		(XLNX_DP_TX_INTR_HPD_IRQ | \
					 XLNX_DP_TX_INTR_HPD_EVENT | \
					 XLNX_DP_TX_INTR_REPLY_RECV | \
					 XLNX_DP_TX_INTR_REPLY_TIMEOUT | \
					 XLNX_DP_TX_INTR_HPD_PULSE | \
					 XLNX_DP_TX_INTR_EXT_PKT_TXD)
#define XLNX_DP_TX_REPLY_DATA_CNT			0x148
#define XLNX_DP_TX_REPLY_STATUS				0x14c
#define XLNX_DP_TX_HPD_DURATION				0x150

#define XLNX_DP_SUB_TX_INTR_STATUS			0x3a0
#define XLNX_DP_SUB_TX_INTR_MASK			0x3a4
#define XLNX_DP_SUB_TX_INTR_EN				0x3a8
#define XDP_TX_LINE_RESET_DISABLE			0x0F0
/* Main stream attribute registers */
#define XLNX_DP_TX_MAIN_STREAM_HTOTAL			0x180
#define XLNX_DP_TX_MAIN_STREAM_VTOTAL			0x184
#define XLNX_DP_TX_MAIN_STREAM_POLARITY			0x188
#define XLNX_DP_TX_MAIN_STREAM_POLARITY_HSYNC_SHIFT	0x0
#define XLNX_DP_TX_MAIN_STREAM_POLARITY_VSYNC_SHIFT	0x1
#define XLNX_DP_TX_MAIN_STREAM_HSWIDTH			0x18c
#define XLNX_DP_TX_MAIN_STREAM_VSWIDTH			0x190
#define XLNX_DP_TX_MAIN_STREAM_HRES			0x194
#define XLNX_DP_TX_MAIN_STREAM_VRES			0x198
#define XLNX_DP_TX_MAIN_STREAM_HSTART			0x19c
#define XLNX_DP_TX_MAIN_STREAM_VSTART			0x1a0
#define XLNX_DP_TX_MAIN_STREAM_MISC0			0x1a4
#define XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC		BIT(0)
#define XLNX_DP_TX_MAIN_STREAM_MISC0_FORMAT_SHIFT	0x1
#define XLNX_DP_TX_MAIN_STREAM_MISC0_DYNAMIC_RANGE	BIT(3)
#define XLNX_DP_TX_MAIN_STREAM_MISC0_YCBCR_COLRIMETRY	BIT(4)
#define XLNX_DP_TX_MAIN_STREAM_MISC0_BPC_SHIFT		0x5
#define XLNX_DP_TX_MAIN_STREAM_MISC1			0x1a8
#define XLNX_DP_TX_MAIN_STREAM_MISC0_INTERLACED_VERT	BIT(0)
#define XLNX_DP_TX_MAIN_STREAM_MISC0_STEREO_VID_SHIFT	0x1
#define XLNX_DP_TX_M_VID				0x1ac
#define XLNX_DP_TX_TRANSFER_UNIT_SIZE			0x1b0
#define XLNX_DP_TX_DEF_TRANSFER_UNIT_SIZE		0x40
#define XLNX_DP_TX_N_VID				0x1b4
#define XLNX_DP_TX_USER_PIXEL_WIDTH			0x1b8
#define XLNX_DP_TX_USER_DATA_CNT_PER_LANE		0x1bc
#define XLNX_DP_TX_MAIN_STREAM_INTERLACED		0x1c0
#define XLNX_DP_TX_MIN_BYTES_PER_TU			0x1c4
#define XLNX_DP_TX_FRAC_BYTES_PER_TU			0x1c8
#define XLNX_DP_TX_INIT_WAIT				0x1cc

/* PHY configuration and status registers */
#define XLNX_DP_TX_PHY_CONFIG				0x200
#define XLNX_DP_TX_PHY_CONFIG_PHY_RESET			BIT(0)
#define XLNX_DP_TX_PHY_CONFIG_GTTX_RESET		BIT(1)
#define XLNX_DP_TX_PHY_CONFIG_PHY_PMA_RESET		BIT(8)
#define XLNX_DP_TX_PHY_CONFIG_PHY_PCS_RESET		BIT(9)
#define XLNX_DP_TX_PHY_CONFIG_ALL_RESET	(XLNX_DP_TX_PHY_CONFIG_PHY_RESET | \
					XLNX_DP_TX_PHY_CONFIG_GTTX_RESET | \
					XLNX_DP_TX_PHY_CONFIG_PHY_PMA_RESET | \
					XLNX_DP_TX_PHY_CONFIG_PHY_PCS_RESET)
#define XLNX_DP_TX_PHY_CONFIG_TX_PHY_POLARITY_LANE0_MASK	0x0020000
#define XLNX_DP_TX_PHY_PREEMPHASIS_LANE_0		0x210
#define XLNX_DP_TX_PHY_PREEMPHASIS_LANE_1		0x214
#define XLNX_DP_TX_PHY_PREEMPHASIS_LANE_2		0x218
#define XLNX_DP_TX_PHY_PREEMPHASIS_LANE_3		0x21c
#define XLNX_DP_TX_PHY_VOLTAGE_DIFF_LANE_0		0x220
#define XLNX_DP_TX_PHY_VOLTAGE_DIFF_LANE_1		0x224
#define XLNX_DP_TX_PHY_VOLTAGE_DIFF_LANE_2		0x228
#define XLNX_DP_TX_PHY_VOLTAGE_DIFF_LANE_3		0x22c
#define XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING		0x234
#define XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_162	0x1
#define XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_270	0x3
#define XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_540	0x5
#define XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_810	0x5 /* DP 1.4 */

/* 0x0220, 0x224, 0x228, 0x22C: XLNX_DP_TX_PHY_VOLTAGE_DIFF_LANE_[0-3] */
#define XLNX_DP_TX_VS_LEVEL_0				0x2
#define XLNX_DP_TX_VS_LEVEL_1                           0x5
#define XLNX_DP_TX_VS_LEVEL_2                           0x8
#define XLNX_DP_TX_VS_LEVEL_3                           0xF
#define XLNX_DP_TX_VS_LEVEL_OFFSET			0x4
#define XDP_TX_MAXIMUM_VS_LEVEL                         0x3

#define XLNX_DP_TX_PHY_POWER_DOWN			0x238
#define XLNX_DP_TX_PHY_POWER_DOWN_LANE_0		BIT(0)
#define XLNX_DP_TX_PHY_POWER_DOWN_LANE_1		BIT(1)
#define XLNX_DP_TX_PHY_POWER_DOWN_LANE_2		BIT(2)
#define XLNX_DP_TX_PHY_POWER_DOWN_LANE_3		BIT(3)
#define XLNX_DP_TX_PHY_POWER_DOWN_ALL			0xf
#define XLNX_DP_TX_PHY_PRECURSOR_LANE_0			0x23c
#define XLNX_DP_TX_PHY_PRECURSOR_LANE_1			0x240
#define XLNX_DP_TX_PHY_PRECURSOR_LANE_2			0x244
#define XLNX_DP_TX_PHY_PRECURSOR_LANE_3			0x248
#define XLNX_DP_TX_PHY_POSTCURSOR_LANE_0		0x24c
#define XLNX_DP_TX_PHY_POSTCURSOR_LANE_1		0x250
#define XLNX_DP_TX_PHY_POSTCURSOR_LANE_2		0x254
#define XLNX_DP_TX_PHY_POSTCURSOR_LANE_3		0x258

#define XLNX_DP_TX_PE_LEVEL_0				0x0
#define XLNX_DP_TX_PE_LEVEL_1                           0xE
#define XLNX_DP_TX_PE_LEVEL_2                           0x14
#define XLNX_DP_TX_PE_LEVEL_3                           0x1B
#define XDP_TX_MAXIMUM_PE_LEVEL                         0x3

#define XLNX_DP_TX_PHY_STATUS				0x280
#define XLNX_DP_TX_PHY_STATUS_PLL_LOCKED_SHIFT		0x4
#define XLNX_DP_TX_PHY_STATUS_FPGA_PLL_LOCKED		BIT(6)
#define XLNX_DP_TX_GT_DRP_COMMAND			0x2a0
#define XLNX_DP_TX_GT_DRP_READ_DATA			0x2a4
#define XLNX_DP_TX_GT_DRP_CHANNEL_STATUS		0x2a8

/* Audio registers */
#define XLNX_DP_TX_AUDIO_CONTROL			0x300
#define XLNX_DP_TX_AUDIO_CHANNELS			0x304
#define XLNX_DP_TX_AUDIO_INFO_DATA			0x308
#define XLNX_DP_TX_AUDIO_M_AUD				0x328
#define XLNX_DP_TX_AUDIO_N_AUD				0x32c
#define XLNX_DP_TX_AUDIO_EXT_DATA			0x330

#define XLNX_DP_TX_VIDEO_PACKING_CLOCK_CONTROL		0x90

#define XLNX_DP_MISC0_RGB				(0)
#define XLNX_DP_MISC0_YCRCB_422				(5 << 1)
#define XLNX_DP_MISC0_YCRCB_444				(6 << 1)
#define XLNX_DP_MISC0_FORMAT_MASK			0xe
#define XLNX_DP_MISC0_BPC_6				(0 << 5)
#define XLNX_DP_MISC0_BPC_8				(1 << 5)
#define XLNX_DP_MISC0_BPC_10				(2 << 5)
#define XLNX_DP_MISC0_BPC_12				(3 << 5)
#define XLNX_DP_MISC0_BPC_16				(4 << 5)
#define XLNX_DP_MISC0_BPC_MASK				0xe0
#define XLNX_DP_MISC1_Y_ONLY				(1 << 7)

#define XLNX_DP_MAX_LANES				0x4
#define XLNX_MAX_FREQ					3000000

#define DP_REDUCED_BIT_RATE				162000
#define DP_HIGH_BIT_RATE				270000
#define DP_HIGH_BIT_RATE2				540000
#define DP_HIGH_BIT_RATE3				810000
#define DP_MAX_TRAINING_TRIES				0x5
#define DP_V1_2						0x12
#define DP_V1_4						0x14

/* 0x00103-0x00106: TRAINING_LANE[0-3]_SET */
#define XDP_DPCD_TRAINING_LANEX_SET_VS_MASK		0x03
#define XDP_DPCD_TRAINING_LANEX_SET_MAX_VS_MASK		0x04
#define XDP_DPCD_TRAINING_LANEX_SET_PE_MASK		0x18
#define XDP_DPCD_TRAINING_LANEX_SET_PE_SHIFT		0x3
#define XDP_DPCD_TRAINING_LANEX_SET_MAX_PE_MASK		0x20

#define XVPHY_CHANNEL_ID_CH1				0x1
#define XVPHY_CHANNEL_ID_CH2				0x2
#define	XVPHY_CHANNEL_ID_CH3				0x3
#define	XVPHY_CHANNEL_ID_CH4				0x4

/* GTHE3. */
#define XVPHY_GTHE3_DIFF_SWING_DP_L0			0x03
#define XVPHY_GTHE3_DIFF_SWING_DP_L1			0x06
#define XVPHY_GTHE3_DIFF_SWING_DP_L2			0x09
#define XVPHY_GTHE3_DIFF_SWING_DP_L3			0x0F

#define XVPHY_GTHE3_PREEMP_DP_L0			0x00
#define XVPHY_GTHE3_PREEMP_DP_L1			0x0E
#define XVPHY_GTHE3_PREEMP_DP_L2			0x14
#define XVPHY_GTHE3_PREEMP_DP_L3			0x14

/* 0x00206, 0x00207: ADJ_REQ_LANE_[0,2]_[1,3] */
#define XDP_DPCD_ADJ_REQ_LANE_0_2_VS_MASK		0x03
#define XDP_DPCD_ADJ_REQ_LANE_0_2_PE_MASK		0x0C
#define XDP_DPCD_ADJ_REQ_LANE_0_2_PE_SHIFT		0x2
#define XDP_DPCD_ADJ_REQ_LANE_1_3_VS_MASK		0x30
#define XDP_DPCD_ADJ_REQ_LANE_1_3_VS_SHIFT		0x4
#define XDP_DPCD_ADJ_REQ_LANE_1_3_PE_MASK		0xC0
#define XDP_DPCD_ADJ_REQ_LANE_1_3_PE_SHIFT		0x6
/* 0x0020C: ADJ_REQ_PC2 */
#define XDP_DPCD_ADJ_REQ_PC2_LANE_0_MASK		0x03
#define XDP_DPCD_ADJ_REQ_PC2_LANE_1_MASK		0x0C
#define XDP_DPCD_ADJ_REQ_PC2_LANE_1_SHIFT		0x2
#define XDP_DPCD_ADJ_REQ_PC2_LANE_2_MASK		0x30
#define XDP_DPCD_ADJ_REQ_PC2_LANE_2_SHIFT		0x4
#define XDP_DPCD_ADJ_REQ_PC2_LANE_3_MASK		0xC0
#define XDP_DPCD_ADJ_REQ_PC2_LANE_3_SHIFT		0x6

#define XDP_TX_PHY_PRECURSOR_LANE_0			0x23C
#define XDP_TX_PHY_PRECURSOR_LANE_1			0x240
#define XDP_TX_PHY_PRECURSOR_LANE_2			0x244
#define XDP_TX_PHY_PRECURSOR_LANE_3			0x248
#define XDP_TX_PHY_POSTCURSOR_LANE_0			0x24C
#define XDP_TX_PHY_POSTCURSOR_LANE_1			0x250
#define XDP_TX_PHY_POSTCURSOR_LANE_2			0x254
#define XDP_TX_PHY_POSTCURSOR_LANE_3			0x258

/* 0x07C, 0x080: TX_DRIVER_CH12, TX_DRIVER_CH34 */
#define XVPHY_TX_DRIVER_TXDIFFCTRL_MASK(Ch) \
		(0x000F << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(Ch) \
		(16 * ((Ch - 1) % 2))
#define XVPHY_TX_DRIVER_TXELECIDLE_MASK(Ch) \
		(0x0010 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXELECIDLE_SHIFT(Ch) \
		(4 + (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXINHIBIT_MASK(Ch) \
		(0x0020 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXINHIBIT_SHIFT(Ch) \
		(5 + (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPOSTCURSOR_MASK(Ch) \
		(0x07C0 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPOSTCURSOR_SHIFT(Ch) \
		(6 + (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPRECURSOR_MASK(Ch) \
		(0xF800 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPRECURSOR_SHIFT(Ch) \
		(11 + (16 * ((Ch - 1) % 2)))

/* Transceiver PHY reset and Differential voltage swing */
#define XDP_TX_PHY_CONFIG				0x200
#define XDP_TX_PHY_VOLTAGE_DIFF_LANE_0			0x220
#define XDP_TX_PHY_VOLTAGE_DIFF_LANE_1			0x224
#define XDP_TX_PHY_VOLTAGE_DIFF_LANE_2			0x228
#define XDP_TX_PHY_VOLTAGE_DIFF_LANE_3			0x22C

/* 0x0220, 0x0224, 0x0228, 0x022C: XDP_TX_PHY_VOLTAGE_DIFF_LANE_[0-3] */
#define XDP_TX_VS_LEVEL_0				0x2
#define XDP_TX_VS_LEVEL_1				0x5
#define XDP_TX_VS_LEVEL_2				0x8
#define XDP_TX_VS_LEVEL_3				0xF
#define XDP_TX_VS_LEVEL_OFFSET				0x4

/* 0x024C, 0x0250, 0x0254, 0x0258: XDP_TX_PHY_POSTCURSOR_LANE_[0-3] */
#define XDP_TX_PE_LEVEL_0				0x00
#define XDP_TX_PE_LEVEL_1				0x0E
#define XDP_TX_PE_LEVEL_2				0x14
#define XDP_TX_PE_LEVEL_3				0x1B

#define VTC_BASE					0x1000

/*remaper color format*/
#define XREMAP_COLOR_FORMAT_RGB			0	
#define XREMAP_COLOR_FORMAT_YUV_444		1
#define XREMAP_COLOR_FORMAT_YUV_422		2

/* register offsets */
#define XVTC_CTL					0x000
#define XVTC_VER					0x010
#define XVTC_GASIZE					0x060
#define XVTC_GENC					0x068
#define XVTC_GPOL					0x06c
#define XVTC_GHSIZE					0x070
#define XVTC_GVSIZE					0x074
#define XVTC_GHSYNC					0x078
#define XVTC_GVBHOFF_F0					0x07c
#define XVTC_GVSYNC_F0					0x080
#define XVTC_GVSHOFF_F0					0x084
#define XVTC_GVBHOFF_F1					0x088
#define XVTC_GVSYNC_F1					0x08C
#define XVTC_GVSHOFF_F1					0x090
#define XVTC_GASIZE_F1					0x094

/* vtc control register bits */
#define XVTC_CTL_SWRESET				BIT(31)
#define XVTC_CTL_FIPSS					BIT(26)
#define XVTC_CTL_ACPSS					BIT(25)
#define XVTC_CTL_AVPSS					BIT(24)
#define XVTC_CTL_HSPSS					BIT(23)
#define XVTC_CTL_VSPSS					BIT(22)
#define XVTC_CTL_HBPSS					BIT(21)
#define XVTC_CTL_VBPSS					BIT(20)
#define XVTC_CTL_VCSS					BIT(18)
#define XVTC_CTL_VASS					BIT(17)
#define XVTC_CTL_VBSS					BIT(16)
#define XVTC_CTL_VSSS					BIT(15)
#define XVTC_CTL_VFSS					BIT(14)
#define XVTC_CTL_VTSS					BIT(13)
#define XVTC_CTL_HBSS					BIT(11)
#define XVTC_CTL_HSSS					BIT(10)
#define XVTC_CTL_HFSS					BIT(9)
#define XVTC_CTL_HTSS					BIT(8)
#define XVTC_CTL_GE					BIT(2)
#define XVTC_CTL_RU					BIT(1)

/* vtc generator polarity register bits */
#define XVTC_GPOL_FIP					BIT(6)
#define XVTC_GPOL_ACP					BIT(5)
#define XVTC_GPOL_AVP					BIT(4)
#define XVTC_GPOL_HSP					BIT(3)
#define XVTC_GPOL_VSP					BIT(2)
#define XVTC_GPOL_HBP					BIT(1)
#define XVTC_GPOL_VBP					BIT(0)

/* vtc generator horizontal 1 */
#define XVTC_GH1_BPSTART_MASK				GENMASK(28, 16)
#define XVTC_GH1_BPSTART_SHIFT				16
#define XVTC_GH1_SYNCSTART_MASK				GENMASK(12, 0)
/* vtc generator vertical 1 (field 0) */
#define XVTC_GV1_BPSTART_MASK				GENMASK(28, 16)
#define XVTC_GV1_BPSTART_SHIFT				16
#define XVTC_GV1_SYNCSTART_MASK				GENMASK(12, 0)
/* vtc generator/detector vblank/vsync horizontal offset registers */
#define XVTC_XVXHOX_HEND_MASK				GENMASK(28, 16)
#define XVTC_XVXHOX_HEND_SHIFT				16
#define XVTC_XVXHOX_HSTART_MASK				GENMASK(12, 0)

#define XVTC_GHFRAME_HSIZE				GENMASK(12, 0)
#define XVTC_GVFRAME_HSIZE_F1				GENMASK(12, 0)
#define XVTC_GA_ACTSIZE_MASK				GENMASK(12, 0)

/* vtc generator encoding register bits */
#define XVTC_GENC_INTERL				BIT(6)

/*
 * struct xlnx_dp_link_config - Common link config between source and sink
 * @max_rate: maximum link rate
 * @max_lanes: maximum number of lanes
 */
struct xlnx_dp_link_config {
	int max_rate;
	u8 max_lanes;
};

/*
 * struct xlnx_dp_tx_link_config - configuration information of the source
 * @vs_level: voltage swing level
 * @pe_level: pre emphasis level
 * @pattern: training pattern
 * @tx_vs_levels: an array of supported voltage swing values
 * @tx_pe_levels: an array of supported pre emphasis values
 * @tx_vs_offset: offset value
 *
 */
struct xlnx_dp_tx_link_config {
	u8 vs_level;
	u8 pe_level;
	u8 pattern;
	u8 tx_vs_levels[4];
        u8 tx_pe_levels[4];
        u8 tx_vs_offset;
};

/**
 *  struct xlnx_dp_rx_sink_config - Sink configuration
 *  @dpcd_rx_caps_field: an array of receiver capabilities
 *  @lane_status_adj_reqs: an array of sink response
 */
struct xlnx_dp_rx_sink_config {
	u8 dpcd_rx_caps_field[16];
	u8 lane_status_adj_reqs[6];
};

/**
 * struct xlnx_dp_mode - Configured mode of DisplayPort
 * @bw_code: code for bandwidth(link rate)
 * @lane_cnt: number of lanes
 * @pclock: pixel clock frequency of current mode
 * @fmt: format identifier string
 */
struct xlnx_dp_mode {
	u8 bw_code;
	u8 lane_cnt;
	int pclock;
	const char *fmt;
};

/**
 * struct xlnx_dp_config - Configuration of DisplayPort from DTS
 * @max_lanes: maximum number of lanes
 * @max_link_rate: maximum supported link rate
 * @max_bpc: maximum bpc value
 * @max_pclock: maximum pixel clock value
 * @enable_yonly: boolean value of yonly color format support
 * @enable_ycrcb: boolean value of yuv color format support
 * @misc0: misc0 configuration
 * @misc1: misc1 configuration
 * @bpp: bits per pixel
 * @bpc: bits per component
 * @num_colors: number of color components
 * @ppc: pixels per component
 */
struct xlnx_dp_config {
	u32 max_lanes;
	u32 max_link_rate;
	u32 max_bpc;
	u32 max_pclock;
	bool enable_yonly;
	bool enable_ycrcb;
	u8 misc0;
	u8 misc1;
	u8 bpp;
	u8 bpc;
	u8 num_colors;
	u8 ppc;
	u8 fmt;
};

/**
 * struct xlnx_dp - Xilinx DisplayPort core
 * @encoder: the drm encoder structure
 * @connector: the drm connector structure
 * @sync_prop: synchronous mode property
 * @bpc_prop: bpc mode property
 * @dev: device structure
 * @drm: DRM core
 * @iomem: device I/O memory for register access
 * @irq: irq
 * @config: IP core configuration from DTS
 * @aux: aux channel
 * @phy: PHY handles for DP lanes
 * @num_lanes: number of enabled phy lanes
 * @hpd_work: hot plug detection worker
 * @status: connection status
 * @enabled: flag to indicate if the device is enabled
 * @dpms: current dpms state
 * @dpcd: DP configuration data from currently connected sink device
 * @link_config: common link configuration between IP core and sink device
 * @tx_link_config: source configuration
 * @mode: current mode between IP core and sink device
 * @train_set: set of training data
 * @aclk: aux clock
 * @axi_lite_clk: axi lite clock
 * @xvphy_dev: pointer to video phy
 * @aclk_en: boolean value of aux clock enable status
 *
 */
struct xlnx_dp {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_property *sync_prop;
	struct drm_property *bpc_prop;
	struct device *dev;
	struct drm_device *drm;
	void __iomem *iomem, *remap_iomem, *clkwiz_iomem;
	struct gpio_desc *reset_gpio;	
	int irq;
	struct xlnx_dp_config config;
	struct drm_dp_aux aux;
	u8 num_lanes;
	struct delayed_work hpd_work;
	enum drm_connector_status status;
	bool enabled;
	int dpms;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	struct xlnx_dp_link_config link_config;
	struct xlnx_dp_tx_link_config tx_link_config;
	struct xlnx_dp_rx_sink_config rx_config;
	struct xlnx_dp_mode mode;
	u8 train_set[XLNX_DP_MAX_LANES];
	struct clk *aclk;
	struct clk *axi_lite_clk;
	struct clk *tx_vid_clk;
	struct phy *phy[XLNX_DP_MAX_LANES];
	struct xvphy_dev *dp_phy_dev;
	bool aclk_en;
};

/* Function prototypes */
void xlnx_dp_phy_set_tx_pre_emphasis(struct xlnx_dp *dp, u32 ChId, u8 Pe);
void xlnx_dp_phy_set_tx_voltage_swing(struct xlnx_dp *dp, u32 ChId, u8 Vs);
void xlnx_dp_pt_pe_vs_adjust_handler(struct xlnx_dp *dp);
static void xlnx_dp_tx_set_vswing_preemp(struct xlnx_dp *dp, u8 *AuxData);
int xlnx_dp_istxconnected(struct xlnx_dp *dp);

static inline struct xlnx_dp *encoder_to_dp(struct drm_encoder *encoder)
{
	return container_of(encoder, struct xlnx_dp, encoder);
}

static inline struct xlnx_dp *connector_to_dp(struct drm_connector *connector)
{
	return container_of(connector, struct xlnx_dp, connector);
}

static void xlnx_dp_write(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static u32 xlnx_dp_read(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static void xlnx_dp_clr(void __iomem *base, int offset, u32 clr)
{
	xlnx_dp_write(base, offset, xlnx_dp_read(base, offset) & ~clr);
}

/**
 * xlnx_vtc_enable - Enable timing controller
 * @base:       Base address of DP Tx subsystem
 *
 * This function enables the DP Tx subsystem's timing controller
 */
static void xlnx_vtc_enable(struct xlnx_dp *dp)
{
	u32 reg = 0;

	reg = xlnx_dp_read(dp->iomem, VTC_BASE + XVTC_CTL);
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_CTL, reg | XVTC_CTL_GE);
}

/**
 * xlnx_vtc_disable - Disable timing controller
 * @base:       Base address of DP Tx subsystem
 *
 * This function disables the DP Tx subsystem's timing controller
 */
static void xlnx_vtc_disable(struct xlnx_dp *dp)
{
	u32 reg;

	reg = xlnx_dp_read(dp->iomem, VTC_BASE + XVTC_CTL);
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_CTL, reg & ~XVTC_CTL_GE);
}

/**
 * remap_reset  - Reset the remapper IP before stream on 
 * @remap:Remapper device instance
 * Toggle the reset gpio .
 */
static void remap_reset(struct xlnx_dp *dp)
{
	
	gpiod_set_raw_value(dp->reset_gpio, 0);
	udelay(1);
	gpiod_set_raw_value(dp->reset_gpio, 1);

}
static int xlnx_vtc_set_timing(struct xlnx_dp *dp, struct videomode *vm)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;

	htotal = vm->hactive + vm->hfront_porch + vm->hsync_len +
			vm->hback_porch;
	vtotal = vm->vactive + vm->vfront_porch + vm->vsync_len +
		vm->vback_porch;

	hactive = vm->hactive;
	vactive = vm->vactive;

	hsync_start = vm->hactive + vm->hfront_porch;
	vsync_start = vm->vactive + vm->vfront_porch;

	hbackporch_start = hsync_start + vm->hsync_len;
	vbackporch_start = vsync_start + vm->vsync_len - 1;

	reg = htotal & XVTC_GHFRAME_HSIZE;
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GHSIZE, reg);

	reg = vtotal & XVTC_GVFRAME_HSIZE_F1;
	reg |= reg << XVTC_GV1_BPSTART_SHIFT;
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVSIZE, reg);
	reg = hactive & XVTC_GA_ACTSIZE_MASK;
	reg |= (vactive & XVTC_GA_ACTSIZE_MASK) << 16;
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GASIZE, reg);

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GASIZE_F1, reg);

	reg = hsync_start & XVTC_GH1_SYNCSTART_MASK;
	reg |= (hbackporch_start << XVTC_GH1_BPSTART_SHIFT) &
		XVTC_GH1_BPSTART_MASK;

	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GHSYNC, reg);
	vsync_start = vsync_start - 1;
	reg = vsync_start & XVTC_GV1_SYNCSTART_MASK;
	reg |= (vbackporch_start << XVTC_GV1_BPSTART_SHIFT) &
		XVTC_GV1_BPSTART_MASK;
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVSYNC_F0, reg);

	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVSYNC_F1, reg);
		reg = xlnx_dp_read(dp->iomem, VTC_BASE + XVTC_GENC) |
					XVTC_GENC_INTERL;
		xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GENC, reg);
	} else {
		reg = xlnx_dp_read(dp->iomem, VTC_BASE + XVTC_GENC) &
					~XVTC_GENC_INTERL;
		xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GENC, reg);
	}

	/* configure horizontal offset */
	/* Calculate and update Generator VBlank Hori field 0 */
	reg = hactive & XVTC_XVXHOX_HSTART_MASK;
	reg |= (hactive << XVTC_XVXHOX_HEND_SHIFT) &
		XVTC_XVXHOX_HEND_MASK;

	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVBHOFF_F0, reg);

	/* Calculate and update Generator VSync Hori field 0 */
	reg = hsync_start & XVTC_XVXHOX_HSTART_MASK;
	reg |= (hsync_start << XVTC_XVXHOX_HEND_SHIFT) &
		XVTC_XVXHOX_HEND_MASK;

	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVSHOFF_F0, reg);

	/* Calculate and update Generator VBlank Hori field 1 */
	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		reg = hactive & XVTC_XVXHOX_HSTART_MASK;
		reg |= (hactive << XVTC_XVXHOX_HEND_SHIFT) &
			XVTC_XVXHOX_HEND_MASK;
		xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVBHOFF_F1, reg);
	}
	/* Calculate and update Generator VBlank Hori field 1 */
	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		reg =  (hsync_start - (htotal / 2)) & XVTC_XVXHOX_HSTART_MASK;
		reg |= ((hsync_start - (htotal / 2)) <<
			XVTC_XVXHOX_HEND_SHIFT) & XVTC_XVXHOX_HEND_MASK;
	} else {
		reg =  hsync_start & XVTC_XVXHOX_HSTART_MASK;
		reg |= (hsync_start << XVTC_XVXHOX_HEND_SHIFT) &
			XVTC_XVXHOX_HEND_MASK;
	}

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GVSHOFF_F1, reg);

	/* configure polarity of signals */
	reg = 0;
	reg |= XVTC_GPOL_ACP;
	reg |= XVTC_GPOL_AVP;
	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		reg |= XVTC_GPOL_FIP;
	reg |= XVTC_GPOL_VBP;
	reg |= XVTC_GPOL_VSP;
	reg |= XVTC_GPOL_HBP;
	reg |= XVTC_GPOL_HSP;
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_GPOL, reg);

	/* configure timing source */
	reg = xlnx_dp_read(dp->iomem, VTC_BASE + XVTC_CTL);
	reg |= XVTC_CTL_VCSS;
	reg |= XVTC_CTL_VASS;
	reg |= XVTC_CTL_VBSS;
	reg |= XVTC_CTL_VSSS;
	reg |= XVTC_CTL_VFSS;
	reg |= XVTC_CTL_VTSS;
	reg |= XVTC_CTL_HBSS;
	reg |= XVTC_CTL_HSSS;
	reg |= XVTC_CTL_HFSS;
	reg |= XVTC_CTL_HTSS;

	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_CTL, reg);

	reg = xlnx_dp_read(dp->iomem, VTC_BASE + XVTC_CTL);
	xlnx_dp_write(dp->iomem, VTC_BASE + XVTC_CTL, reg | XVTC_CTL_RU);

	return 0;
}

/**
 * xlnx_dp_update_bpp - Update the current bpp config
 * @dp: DisplayPort IP core structure
 *
 * Update the current bpp based on the color format: bpc & num_colors.
 * Any function that changes bpc or num_colors should call this
 * to keep the bpp value in sync.
 */
static void xlnx_dp_update_bpp(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;

	config->bpp = dp->config.bpc * dp->config.num_colors;
}

/**
 * xlnx_dp_set_color - Set the color
 * @dp: DisplayPort IP core structure
 * @color: color string, from xlnx_disp_color_enum
 *
 * Update misc register values based on @color string.
 *
 * Return: 0 on success, or -EINVAL.
 */
static int xlnx_dp_set_color(struct xlnx_dp *dp, u32 drm_fourcc)
{
	struct xlnx_dp_config *config = &dp->config;

	config->misc0 &= ~XLNX_DP_MISC0_FORMAT_MASK;
	config->misc1 &= ~XLNX_DP_MISC1_Y_ONLY;
	
	switch(drm_fourcc) {

	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XBGR2101010:
		config->misc0 |= XLNX_DP_MISC0_RGB;
		config->num_colors = 3;
		config->fmt = XREMAP_COLOR_FORMAT_RGB;
		break; 
	case DRM_FORMAT_VUY888:
	case DRM_FORMAT_XVUY8888:
	case DRM_FORMAT_Y8:
	case DRM_FORMAT_XVUY2101010:
	case DRM_FORMAT_Y10:
		config->misc0 |= XLNX_DP_MISC0_YCRCB_444;
		config->num_colors = 3;
		config->fmt = XREMAP_COLOR_FORMAT_YUV_444;
		break;
	case DRM_FORMAT_YUYV: 
	case DRM_FORMAT_UYVY: 
	case DRM_FORMAT_NV16: 
	case DRM_FORMAT_XV20: 
		config->misc0 |= XLNX_DP_MISC0_YCRCB_422;
		config->num_colors = 2;
		config->fmt = XREMAP_COLOR_FORMAT_YUV_422;
		break;
          default:
		printk("Warning: Unknown drm_fourcc format code: %d\n", drm_fourcc);
		config->misc0 |= XLNX_DP_MISC0_RGB;
         }
	xlnx_dp_update_bpp(dp);

	return 0;
}

/*
 * DP PHY functions
 */

/**
 * xlnx_dp_init_phy - Initialize the phy
 * @dp: DisplayPort IP core structure
 *
 * Initialize the phy.
 *
 * Return: 0 if the phy instances are initialized correctly, or the error code
 * returned from the callee functions.
 */
static int xlnx_dp_init_phy(struct xlnx_dp *dp)
{

	xlnx_dp_clr(dp->iomem, XLNX_DP_TX_PHY_CONFIG,
			XLNX_DP_TX_PHY_CONFIG_ALL_RESET);

	return 0;
}

/**
 * xlnx_dp_exit_phy - Exit the phy
 * @dp: DisplayPort IP core structure
 *
 * Exit the phy.
 */
static void xlnx_dp_exit_phy(struct xlnx_dp *dp)
{
	unsigned int i;
	int ret;

	for (i = 0; i < XLNX_DP_MAX_LANES; i++) {
		ret = phy_exit(dp->phy[i]);
		if (ret)
			dev_err(dp->dev, "fail to exit phy(%d) %d\n", i, ret);
	}
}

/**
 * xlnx_dp_phy_ready - Check if PHY is ready
 * @dp: DisplayPort IP core structure
 *
 * Check if PHY is ready. If PHY is not ready, wait 1ms to check for 100 times.
 * This amount of delay was suggested by IP designer.
 *
 * Return: 0 if PHY is ready, or -ENODEV if PHY is not ready.
 */
static int xlnx_dp_phy_ready(struct xlnx_dp *dp)
{
	u32 i, reg, ready;

	ready = (1 << XLNX_DP_MAX_LANES) - 1;
	ready |= XLNX_DP_TX_PHY_STATUS_FPGA_PLL_LOCKED;

	/* Wait for 100ms. This should be enough time for PHY to be ready */
	for (i = 0; ; i++) {
		reg = xlnx_dp_read(dp->iomem, XLNX_DP_TX_PHY_STATUS);
		if ((reg & ready) == ready)
			return 0;
		if (i == 100) {
			dev_err(dp->dev, "PHY isn't ready\n");
			return -ENODEV;
		}
		usleep_range(1000, 1100);
	}

	return 0;
}

/*
 * DP functions
 */

/**
 * xlnx_dp_max_rate - Calculate and return available max pixel clock
 * @link_rate: link rate (Kilo-bytes / sec)
 * @lane_num: number of lanes
 * @bpp: bits per pixel
 *
 * Return: max pixel clock (KHz) supported by current link config.
 */
static inline int xlnx_dp_max_rate(int link_rate, u8 lane_num, u8 bpp)
{
	return link_rate * lane_num * 8 / bpp;
}

/**
 * xlnx_dp_mode_configure - Configure the link values
 * @dp: DisplayPort IP core structure
 * @pclock: pixel clock for requested display mode
 * @current_bw: current link rate
 *
 * Find the link configuration values, rate and lane count for requested pixel
 * clock @pclock. The @pclock is stored in the mode to be used in other
 * functions later. The returned rate is downshifted from the current rate
 * @current_bw.
 *
 * Return: Current link rate code, or -EINVAL.
 */
static int xlnx_dp_mode_configure(struct xlnx_dp *dp, int pclock,
				    u8 current_bw)
{

	int max_rate = dp->link_config.max_rate;
	u8 bws[4] = { DP_LINK_BW_1_62, DP_LINK_BW_2_7,
			DP_LINK_BW_5_4, DP_LINK_BW_8_1 };
	u8 max_lanes = dp->link_config.max_lanes;
	u8 max_link_rate_code = drm_dp_link_rate_to_bw_code(max_rate);
	u8 bpp = dp->config.bpp;
	u8 lane_cnt;
	s8 i;

	if (current_bw == DP_LINK_BW_1_62) {
		dev_info(dp->dev, "can't downshift. already lowest\n");
		return -EINVAL;
	}
	for (i = ARRAY_SIZE(bws) - 1; i >= 0; i--) {
		if (current_bw && bws[i] >= current_bw)
			continue;
		if (bws[i] <= max_link_rate_code)
			break;
	}
	for (lane_cnt = max_lanes; lane_cnt >= 1; lane_cnt >>= 1) {
		int bw;
		u32 rate;

		bw = drm_dp_bw_code_to_link_rate(bws[i]);
		rate = xlnx_dp_max_rate(bw, lane_cnt, bpp);
		if (pclock <= rate) {
			dp->mode.bw_code = bws[i];
			dp->mode.lane_cnt = lane_cnt;
			dp->mode.pclock = pclock;
			return dp->mode.bw_code;
		}
	}

	dev_err(dp->dev, "failed to configure link values\n");

	return -EINVAL;
}

/**
 * xlnx_dp_phy_set_tx_pre_emphasis - Configure the pe values
 * @dp: DisplayPort IP core structure
 * @chid: channel index
 * @pe: pe value to be set
 *
 * This function sets the preemphasis value of the phy
 */

void xlnx_dp_phy_set_tx_pre_emphasis(struct xlnx_dp *dp, u32 chid, u8 pe)
{
	u32 regval;
	u32 maskval;
	u32 regoffset;


	if (chid == XVPHY_CHANNEL_ID_CH1 || chid == XVPHY_CHANNEL_ID_CH2)
		regoffset = XVPHY_TX_DRIVER_CH12_REG;
	else
		regoffset = XVPHY_TX_DRIVER_CH34_REG;

	regval = xlnx_dp_read(dp->dp_phy_dev->iomem, regoffset);
	maskval = XVPHY_TX_DRIVER_TXPRECURSOR_MASK(chid);
	regval &= ~maskval;
	regval |= (pe << XVPHY_TX_DRIVER_TXPRECURSOR_SHIFT(chid));
	xlnx_dp_write(dp->dp_phy_dev->iomem, regoffset, regval);
}

/**
 * xlnx_dp_phy_set_tx_voltage_swing - Configure the vs values
 * @dp: DisplayPort IP core structure
 * @chid: channel index
 * @vs: vs value to be set
 * 
 * This function sets the voltage swing value of the phy
 */

void xlnx_dp_phy_set_tx_voltage_swing(struct xlnx_dp *dp, u32 chid, u8 vs)
{
	u32 regval;
	u32 maskval;
	u32 regoffset;

	if (chid == XVPHY_CHANNEL_ID_CH1 || chid == XVPHY_CHANNEL_ID_CH2)
		regoffset = XVPHY_TX_DRIVER_CH12_REG;
	else
		regoffset = XVPHY_TX_DRIVER_CH34_REG;

	regval = xlnx_dp_read(dp->dp_phy_dev->iomem, regoffset);
	maskval = XVPHY_TX_DRIVER_TXDIFFCTRL_MASK(chid);
	regval &= ~maskval;
	regval |= (vs << XVPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(chid));
	xlnx_dp_write(dp->dp_phy_dev->iomem, regoffset, regval);
}

/**
 * xlnx_dp_pt_pe_vs_adjust_handler - Calculate and configure pe and vs values
 * @dp: DisplayPort IP core structure
 *
 * This function adjusts the pre emphasis and voltage swing values of phy.
 */

void xlnx_dp_pt_pe_vs_adjust_handler(struct xlnx_dp *dp)
{
	unsigned char preemp = 0, diff_swing = 0;

	switch (dp->tx_link_config.pe_level) {
	case 0:
		preemp = XVPHY_GTHE3_PREEMP_DP_L0; break;
	case 1:
		preemp = XVPHY_GTHE3_PREEMP_DP_L1; break;
	case 2:
		preemp = XVPHY_GTHE3_PREEMP_DP_L2; break;
	case 3:
		preemp = XVPHY_GTHE3_PREEMP_DP_L3; break;
	}

	xlnx_dp_phy_set_tx_pre_emphasis(dp, XVPHY_CHANNEL_ID_CH1, preemp);
	xlnx_dp_phy_set_tx_pre_emphasis(dp, XVPHY_CHANNEL_ID_CH2, preemp);
	xlnx_dp_phy_set_tx_pre_emphasis(dp, XVPHY_CHANNEL_ID_CH3, preemp);
	xlnx_dp_phy_set_tx_pre_emphasis(dp, XVPHY_CHANNEL_ID_CH4, preemp);

	switch (dp->tx_link_config.vs_level) {
	case 0:
		switch (dp->tx_link_config.pe_level) {
		case 0:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L0;
			break;
		case 1:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L1;
			break;
		case 2:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L2;
			break;
		case 3:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L3;
			break;
		}
		break;
	case 1:
		switch (dp->tx_link_config.pe_level) {
		case 0:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L1;
			break;
		case 1:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L2;
			break;
		case 2:
		case 3:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L3;
			break;
		}
		break;
	case 2:
		switch (dp->tx_link_config.pe_level) {
		case 0:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L2;
			break;
		case 1:
		case 2:
		case 3:
			diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L3;
			break;
		}
		break;
	case 3:
		diff_swing = XVPHY_GTHE3_DIFF_SWING_DP_L3;
		break;
	}
	xlnx_dp_phy_set_tx_voltage_swing(dp, XVPHY_CHANNEL_ID_CH1, diff_swing);
	xlnx_dp_phy_set_tx_voltage_swing(dp, XVPHY_CHANNEL_ID_CH2, diff_swing);
	xlnx_dp_phy_set_tx_voltage_swing(dp, XVPHY_CHANNEL_ID_CH3, diff_swing);
	xlnx_dp_phy_set_tx_voltage_swing(dp, XVPHY_CHANNEL_ID_CH4, diff_swing);

}

/**
 * xlnx_dp_tx_adj_vswing_preemp - This function sets new voltage swing and
 * pre-emphasis levels using the adjustment requests obtained from the sink.
 *
 * @param	Ptr is a pointer to the xlnx_dp instance.
 * @link_status an array of link status register
 * @return
 * zero if the new levels were written successfully.
 * error value on failure.
 */
static u32 xlnx_dp_tx_adj_vswing_preemp(struct xlnx_dp *dp, u8 link_status[6])
{
	u32 ret;
	u8 index, aux_data[4];
	u8 vs_level_adj_req[4];
	u8 pe_level_adj_req[4];

	/* Analyze the adjustment requests for changes in voltage swing and
	 * pre-emphasis levels.
	 */
	vs_level_adj_req[0] = link_status[4] & XDP_DPCD_ADJ_REQ_LANE_0_2_VS_MASK;
	vs_level_adj_req[1] = (link_status[4] &
				XDP_DPCD_ADJ_REQ_LANE_1_3_VS_MASK) >>
				XDP_DPCD_ADJ_REQ_LANE_1_3_VS_SHIFT;
	vs_level_adj_req[2] = link_status[5] & XDP_DPCD_ADJ_REQ_LANE_0_2_VS_MASK;
	vs_level_adj_req[3] = (link_status[5] &
				XDP_DPCD_ADJ_REQ_LANE_1_3_VS_MASK) >>
				XDP_DPCD_ADJ_REQ_LANE_1_3_VS_SHIFT;
	pe_level_adj_req[0] = (link_status[4] &
				XDP_DPCD_ADJ_REQ_LANE_0_2_PE_MASK) >>
				XDP_DPCD_ADJ_REQ_LANE_0_2_PE_SHIFT;
	pe_level_adj_req[1] = (link_status[4] &
				XDP_DPCD_ADJ_REQ_LANE_1_3_PE_MASK) >>
				XDP_DPCD_ADJ_REQ_LANE_1_3_PE_SHIFT;
	pe_level_adj_req[2] = (link_status[5] &
				XDP_DPCD_ADJ_REQ_LANE_0_2_PE_MASK) >>
				XDP_DPCD_ADJ_REQ_LANE_0_2_PE_SHIFT;
	pe_level_adj_req[3] = (link_status[5] &
				XDP_DPCD_ADJ_REQ_LANE_1_3_PE_MASK) >>
				XDP_DPCD_ADJ_REQ_LANE_1_3_PE_SHIFT;

	/* Change the drive settings to match the adjustment requests. Use the
	 * greatest level requested.
	 */
	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;
	for (index = 0; index < dp->mode.lane_cnt ; index++) {
		if (vs_level_adj_req[index] > dp->tx_link_config.vs_level)
			dp->tx_link_config.vs_level = vs_level_adj_req[index];
		if (pe_level_adj_req[index] > dp->tx_link_config.pe_level)
			dp->tx_link_config.pe_level = pe_level_adj_req[index];
	}

	/* Verify that the voltage swing and pre-emphasis combination is
	 * allowed. Some combinations will result in differential peak-to-peak
	 * voltage that is outside the permissible range. See the VESA
	 * DisplayPort v1.4 Specification.
	 * The valid combinations are:
	 * PE=0    PE=1    PE=2    PE=3
	 * VS=0 Valid   Valid   Valid   Valid
	 * VS=1 Valid   Valid   Valid
	 * VS=2 Valid   Valid
	 * VS=3 Valid
	 */
	if (dp->tx_link_config.pe_level > (4 - dp->tx_link_config.vs_level)) {
		dp->tx_link_config.pe_level = 4 - dp->tx_link_config.vs_level;
	}
	/* Make the adjustments to both the DisplayPort TX core and the RX
	 * device.
	 */
	xlnx_dp_tx_set_vswing_preemp(dp, aux_data);
	/* Write the voltage swing and pre-emphasis levels for each lane to the
	 * RX device.
	 */
	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
					&aux_data[0], 4);
	if (ret < 0)
		return ret;

	xlnx_dp_pt_pe_vs_adjust_handler(dp);

	return 0;
}

/**
 * xlnx_dp_tx_set_vswing_preemp - This function sets current voltage swing and
 * pre-emphasis level settings from the LinkConfig structure to hardware.
 *
 * @dp		pointer to the xlnx_dp instance.
 * @aux_data	AuxData is a pointer to the array used for preparing a burst
 *		write over the AUX channel.
 *
 *
 */
static void xlnx_dp_tx_set_vswing_preemp(struct xlnx_dp *dp, u8 *aux_data)
{
	u8 data, vs_level_rx = dp->tx_link_config.vs_level;
	u8 index, pe_level_rx = dp->tx_link_config.pe_level;
	u32 pe_level, vs_level;

	pe_level = dp->tx_link_config.tx_pe_levels[pe_level_rx];
	vs_level = dp->tx_link_config.tx_vs_levels[vs_level_rx];

	/* Need to compensate due to no redriver in the path. */
	if (pe_level_rx != 0)
		vs_level += dp->tx_link_config.tx_vs_offset;

	/* Set up the data buffer for writing to the RX device. */
	data = (pe_level_rx << XDP_DPCD_TRAINING_LANEX_SET_PE_SHIFT) |
		vs_level_rx;
	/* The maximum voltage swing has been reached. */
	if (vs_level_rx == XDP_TX_MAXIMUM_VS_LEVEL)
		data |= XDP_DPCD_TRAINING_LANEX_SET_MAX_VS_MASK;
	/* The maximum pre-emphasis level has been reached. */
	if (pe_level_rx == XDP_TX_MAXIMUM_PE_LEVEL)
		data |= XDP_DPCD_TRAINING_LANEX_SET_MAX_PE_MASK;
	memset(aux_data, data, 4);

	for (index = 0; index < 4; index++) {
		/* Disable pre-cursor levels. */
		xlnx_dp_write(dp->iomem,
				XDP_TX_PHY_PRECURSOR_LANE_0 + 4 * index, 0x0);
		/* Write new voltage swing levels to the TX registers. */
		xlnx_dp_write(dp->iomem,
				XDP_TX_PHY_VOLTAGE_DIFF_LANE_0 + (4 * index),
				vs_level);
		/* Write new pre-emphasis levels to the TX registers. */
		xlnx_dp_write(dp->iomem,
				XDP_TX_PHY_POSTCURSOR_LANE_0 + 4 * index,
				pe_level);
	}
}

/**
 * xlnx_dp_link_train_cr - Train clock recovery
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if clock recovery train is done successfully, or corresponding
 * error code.
 *
 */
static int xlnx_dp_link_train_cr(struct xlnx_dp *dp)
{
	u8 PrevVsLevel = 0;
	u8 SameVsLevelCount = 0;
	u8 AuxData[5];
	u8 *auxdata = &AuxData[0];
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	bool cr_done = 0;
	int ret;

	struct xlnx_dp_tx_link_config *LinkConfig = &dp->tx_link_config;
	/* Start from minimal voltage swing and pre-emphasis levels. */
	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;

	/* Transmit training pattern 1. */
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_TRAINING_PATTERN_SET,
			DP_TRAINING_PATTERN_1);
	/* Disable the scrambler. */
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_SCRAMBLING_DISABLE, 1);
	AuxData[0] = DP_TRAINING_PATTERN_1 | DP_LINK_SCRAMBLING_DISABLE;

	xlnx_dp_tx_set_vswing_preemp(dp, &AuxData[1]);
	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET,
				&AuxData[0], 5);
	if (ret < 0)
		return ret;

	while (1) {
		/* Obtain the required delay for clock recovery as specified
		 * by the RX device.
		 */
		/* Wait delay specified in TRAINING_AUX_RD_INTERVAL(0x0E) */
		drm_dp_link_train_clock_recovery_delay(dp->dpcd);
		/* Check if all lanes have realized and maintained the
		 * frequency lock and get adjustment requests.
		 */
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		cr_done = drm_dp_clock_recovery_ok(link_status, lane_cnt);
		if (cr_done)
			break;
		/* Check if the same voltage swing for each lane has been
		 * used 5 consecutive times.
		 */
		if (PrevVsLevel == LinkConfig->vs_level)
			SameVsLevelCount++;
		else {
			SameVsLevelCount = 0;
			PrevVsLevel = LinkConfig->vs_level;
		}
		if (SameVsLevelCount >= 5)
			break;
		/* Only try maximum voltage swing once. */
		if (LinkConfig->vs_level == XDP_TX_MAXIMUM_VS_LEVEL)
			break;

		/* Adjust the drive settings as requested by the RX device. */
		ret = xlnx_dp_tx_adj_vswing_preemp(dp, link_status);
		if (ret < 0)
			return ret;
	}
	if (cr_done)
		return 0;

	printk("training cr failed \n");
	return -1;
}

/**
 * xlnx_dp_link_train_ce - Train channel equalization
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if channel equalization train is done successfully, or
 * corresponding error code.
 */

static int xlnx_dp_link_train_ce(struct xlnx_dp *dp)
{

	u32 ret = 0;
	u32 IterationCount = 0;
	u8 pat;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 AuxData[5];
	bool ce_done;

	if (dp->dpcd[DP_DPCD_REV] == DP_V1_4 &&
		dp->dpcd[DP_MAX_DOWNSPREAD] & DP_TPS4_SUPPORTED){
		pat = DP_TRAINING_PATTERN_4;
	} else if (dp->dpcd[DP_DPCD_REV] >= DP_V1_2 &&
			dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED)
			pat = DP_TRAINING_PATTERN_3;
	else
		pat = DP_TRAINING_PATTERN_2;

	/* Write to the DisplayPort TX core. Transmit training pattern 2/3/4*/
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_TRAINING_PATTERN_SET, pat);

	/* Enable/Discable the scrambler based on DP version */
	if (dp->dpcd[DP_DPCD_REV] == DP_V1_4) {
		xlnx_dp_write(dp->iomem, XLNX_DP_TX_SCRAMBLING_DISABLE, 0);
		AuxData[0] = DP_TRAINING_PATTERN_4;
	} else {
		xlnx_dp_write(dp->iomem, XLNX_DP_TX_SCRAMBLING_DISABLE, 1);
		AuxData[0] = pat | DP_LINK_SCRAMBLING_DISABLE;
	}
	xlnx_dp_tx_set_vswing_preemp(dp, &AuxData[1]);
	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET,
					&AuxData[0], 5);
	if (ret < 0)
		return ret;

	while (IterationCount < 8) {

		/* Obtain the required delay for channel equalization as
		 * specified by the RX device.
		 */

		/* Wait delay specified in TRAINING_AUX_RD_INTERVAL. */
		drm_dp_link_train_channel_eq_delay(dp->dpcd);

		/* Check that all lanes still have their clocks locked. */
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		/* Check if all lanes have accomplished channel equalization,
		 * symbol lock, and interlane alignment.
		 */
		ce_done = drm_dp_channel_eq_ok(link_status, lane_cnt);
		if (ce_done)
			break;

		/* Check that all lanes still have their clocks locked. */
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		/* Adjust the drive settings as requested by the RX device. */
		ret = xlnx_dp_tx_adj_vswing_preemp(dp, link_status);
		if (ret != 0)
			return ret;
		IterationCount++;
	}
	/* Tried 8 times with no success. Try a reduced bitrate first, then
	 * reduce the number of lanes.
	 */
	if (ce_done)
		return 0;

	printk("train ce failed\n");
	return -1;
}

/**
 * xlnx_dp_link_train - Train the link
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if all trains are done successfully, or corresponding error code.
 */
static int xlnx_dp_train(struct xlnx_dp *dp)
{
	u32 reg;
	u8 bw_code = dp->mode.bw_code;
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 aux_read, aux_lane_cnt = lane_cnt;
	u8 Data;
	u8 relink_status[DP_LINK_STATUS_SIZE];
	bool enhanced;
	int ret;

	xlnx_dp_write(dp->iomem, XLNX_DP_TX_LANE_CNT_SET, lane_cnt);
	ret = drm_dp_dpcd_readb(&dp->aux, DP_MAX_LANE_COUNT, &aux_read);
	if (aux_read & (1 << 7)) {
		xlnx_dp_write(dp->iomem, XLNX_DP_TX_ENHANCED_FRAME_EN, 1);
		aux_lane_cnt |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}
	if (dp->dpcd[3] & 0x1) {
		xlnx_dp_write(dp->iomem, XLNX_DP_TX_DOWNSPREAD_CTL, 1);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL,
					DP_SPREAD_AMP_0_5);
	} else {
		xlnx_dp_write(dp->iomem, XLNX_DP_TX_DOWNSPREAD_CTL, 0);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL, 0);
	}
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, aux_lane_cnt);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return ret;
	}
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
					DP_SET_ANSI_8B10B);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set ANSI 8B/10B encoding\n");
		return ret;
	}
	enhanced = drm_dp_dpcd_readb(&dp->aux, DP_MAX_LANE_COUNT, &aux_read);
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LINK_BW_SET, bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set DP bandwidth\n");
		return ret;
	}
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_LINK_BW_SET, bw_code);

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		reg = XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_162;
		break;
	case DP_LINK_BW_2_7:
		reg = XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_270;
		break;
	case DP_LINK_BW_5_4:
		reg = XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_540;
		break;
	case DP_LINK_BW_8_1:
		reg = XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_810;
		break;
	default:
		reg = XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_810;
		break;
	}
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING,
			reg);
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return ret;
	memset(dp->train_set, 0, 4);
	ret = xlnx_dp_link_train_cr(dp);
	if (ret)
		return ret;

	ret = xlnx_dp_link_train_ce(dp);
	if (ret)
		return ret;
	if (dp->dpcd[DP_DPCD_REV] == DP_V1_4) {
		ret = drm_dp_dpcd_readb(&dp->aux, DP_LANE_COUNT_SET, &Data);
		if (ret < 0)
			dev_info(dp->dev, "aux read failed\n");
		Data = Data | 0x20;
		ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, Data);
		if (ret < 0)
			dev_info(dp->dev, "aux read failed\n");
	}
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_SCRAMBLING_DISABLE, 0);
	/* Check channel equalization, symbol lock, and interlane alignment.*/
	ret = drm_dp_channel_eq_ok(relink_status, lane_cnt);
	if (!ret)
		dev_info(dp->dev, "connection recheck failed\n");

	xlnx_dp_write(dp->iomem, XLNX_DP_TX_TRAINING_PATTERN_SET,
			DP_TRAINING_PATTERN_DISABLE);
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
					DP_TRAINING_PATTERN_DISABLE);
	if (ret < 0) {
		dev_err(dp->dev, "failed to disable training pattern\n");
		return ret;
	}
	/* Reset the scrambler.*/
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_SCRAMBLING_DISABLE, 0);

	return 0;
}

/**
 * xlnx_dp_train_loop - Downshift the link rate during training
 * @dp: DisplayPort IP core structure
 *
 * Train the link by downshifting the link rate if training is not successful.
 */
static void xlnx_dp_train_loop(struct xlnx_dp *dp)
{
	struct xlnx_dp_mode *mode = &dp->mode;
	u8 bw = mode->bw_code;
	int ret;

	do {
		if (dp->status == connector_status_disconnected ||
		    !dp->enabled)
			return;
		ret = xlnx_dp_train(dp);
		if (!ret)
			return;
		ret = xlnx_dp_mode_configure(dp, mode->pclock, bw);
		if (ret < 0)
			goto err_out;
		bw = ret;
	} while (bw >= DP_LINK_BW_1_62);

err_out:
	dev_err(dp->dev, "failed to train the DP link\n");
}

/*
 * DP Aux functions
 */

#define AUX_READ_BIT	0x1

/**
 * xlnx_dp_aux_cmd_submit - Submit aux command
 * @dp: DisplayPort IP core structure
 * @cmd: aux command
 * @addr: aux address
 * @buf: buffer for command data
 * @bytes: number of bytes for @buf
 * @reply: reply code to be returned
 *
 * Submit an aux command. All aux related commands, native or i2c aux
 * read/write, are submitted through this function. The function is mapped to
 * the transfer function of struct drm_dp_aux. This function involves in
 * multiple register reads/writes, thus synchronization is needed, and it is
 * done by drm_dp_helper using @hw_mutex. The calling thread goes into sleep
 * if there's no immediate reply to the command submission. The reply code is
 * returned at @reply if @reply != NULL.
 *
 * Return: 0 if the command is submitted properly, or corresponding error code:
 * -EBUSY when there is any request already being processed
 * -ETIMEDOUT when receiving reply is timed out
 * -EIO when received bytes are less than requested
 */
static int xlnx_dp_aux_cmd_submit(struct xlnx_dp *dp, u32 cmd, u16 addr,
				    u8 *buf, u8 bytes, u8 *reply)
{
	bool is_read = (cmd & AUX_READ_BIT) ? true : false;
	void __iomem *iomem = dp->iomem;
	u32 reg, i;

	reg = xlnx_dp_read(iomem, XLNX_DP_TX_INTR_SIGNAL_STATE);
	if (reg & XLNX_DP_TX_INTR_SIGNAL_STATE_REQUEST)
		return -EBUSY;

	xlnx_dp_write(iomem, XLNX_DP_TX_AUX_ADDRESS, addr);
	if (!is_read)
		for (i = 0; i < bytes; i++)
			xlnx_dp_write(iomem, XLNX_DP_TX_AUX_WRITE_FIFO,
					buf[i]);

	reg = cmd << XLNX_DP_TX_AUX_COMMAND_CMD_SHIFT;
	if (!buf || !bytes)
		reg |= XLNX_DP_TX_AUX_COMMAND_ADDRESS_ONLY;
	else
		reg |= (bytes - 1) << XLNX_DP_TX_AUX_COMMAND_BYTES_SHIFT;
	xlnx_dp_write(iomem, XLNX_DP_TX_AUX_COMMAND, reg);

	/* Wait for reply to be delivered upto 2ms */
	for (i = 0; ; i++) {
		reg = xlnx_dp_read(iomem, XLNX_DP_TX_INTR_SIGNAL_STATE);
		if (reg & XLNX_DP_TX_INTR_SIGNAL_STATE_REPLY)
			break;

		if (reg & XLNX_DP_TX_INTR_SIGNAL_STATE_REPLY_TIMEOUT ||
		    i == 2)
			return -ETIMEDOUT;

		usleep_range(1000, 1100);
	}

	reg = xlnx_dp_read(iomem, XLNX_DP_TX_AUX_REPLY_CODE);
	if (reply)
		*reply = reg;

	if (is_read &&
	    (reg == XLNX_DP_TX_AUX_REPLY_CODE_AUX_ACK ||
	     reg == XLNX_DP_TX_AUX_REPLY_CODE_I2C_ACK)) {
		reg = xlnx_dp_read(iomem, XLNX_DP_TX_REPLY_DATA_CNT);
		if ((reg & XLNX_DP_TX_AUX_REPLY_CNT_MASK) != bytes)
			return -EIO;

		for (i = 0; i < bytes; i++) {
			buf[i] = xlnx_dp_read(iomem,
						XLNX_DP_TX_AUX_REPLY_DATA);
		}
	}

	return 0;
}

static ssize_t
xlnx_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct xlnx_dp *dp = container_of(aux, struct xlnx_dp, aux);
	int ret;
	unsigned int i, iter;

	/* Number of loops = timeout in msec / aux delay (400 usec) */
	iter = xlnx_dp_aux_timeout_ms * 1000 / 400;
	iter = iter ? iter : 1;

	for (i = 0; i < iter; i++) {
		ret = xlnx_dp_aux_cmd_submit(dp, msg->request, msg->address,
					       msg->buffer, msg->size,
					       &msg->reply);
		if (!ret) {
			dev_dbg(dp->dev, "aux %d retries\n", i);
			return msg->size;
		}

		if (dp->status == connector_status_disconnected) {
			dev_info(dp->dev, "no connected aux device\n");
			return -ENODEV;
		}

		usleep_range(400, 500);
	}

	dev_info(dp->dev, "fail aux transfer (%d)iteration =%d\n", ret, iter);

	return ret;
}

/**
 * xlnx_dp_init_aux - Initialize the DP aux
 * @dp: DisplayPort IP core structure
 *
 * Initialize the DP aux. The aux clock is derived from the axi clock, so
 * this function gets the axi clock frequency and calculates the filter
 * value. Additionally, the interrupts and transmitter are enabled.
 *
 * Return: 0 on success, error value otherwise
 */
static int xlnx_dp_init_aux(struct xlnx_dp *dp)
{
	unsigned long rate;
	u32 reg, w;

	rate = clk_get_rate(dp->axi_lite_clk);
	if (rate < XLNX_DP_TX_CLK_DIVIDER_MHZ) {
		dev_err(dp->dev, "aclk should be higher than 1MHz\n");
		return -EINVAL;
	}

	/* Allowable values for this register are: 8, 16, 24, 32, 40, 48 */
	for (w = 8; w <= 48; w += 8) {
		/* AUX pulse width should be between 0.4 to 0.6 usec */
		if (w >= (4 * rate / 10000000) &&
		    w <= (6 * rate / 10000000))
			break;
	}

	if (w > 48) {
		dev_err(dp->dev, "aclk frequency too high\n");
		return -EINVAL;
	}
	reg = w << XLNX_DP_TX_CLK_DIVIDER_AUX_FILTER_SHIFT;
	reg |= rate / XLNX_DP_TX_CLK_DIVIDER_MHZ;
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_CLK_DIVIDER, reg);

	xlnx_dp_write(dp->iomem, XLNX_DP_TX_ENABLE, 1);
	return 0;
}

/**
 * xlnx_dp_exit_aux - De-initialize the DP aux
 * @dp: DisplayPort IP core structure
 *
 * De-initialize the DP aux. Disable all interrupts which are enabled
 * through aux initialization, as well as the transmitter.
 */
static void xlnx_dp_exit_aux(struct xlnx_dp *dp)
{
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_ENABLE, 0);
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_INTR_MASK, 0xFFF);
}

/*
 * Generic DP functions
 */

/**
 * xlnx_dp_update_misc - Write the misc registers
 * @dp: DisplayPort IP core structure
 *
 * The misc register values are stored in the structure, and this
 * function applies the values into the registers.
 */
static void xlnx_dp_update_misc(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_MAIN_STREAM_MISC0, config->misc0); /*RGB24=0x20,yuv422=3A,yuv444=3C*/
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_MAIN_STREAM_MISC1, 0x0);
}

/**
 * xlnx_dp_set_sync_mode - Set the sync mode bit in the software misc state
 * @dp: DisplayPort IP core structure
 * @mode: flag if the sync mode should be on or off
 *
 * Set the bit in software misc state. To apply to hardware,
 * xlnx_dp_update_misc() should be called.
 */
static void xlnx_dp_set_sync_mode(struct xlnx_dp *dp, bool mode)
{
	struct xlnx_dp_config *config = &dp->config;

	if (mode)
		config->misc0 |= XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC;
	else
		config->misc0 &= ~XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC;
}

/**
 * xlnx_dp_get_sync_mode - Get the sync mode state
 * @dp: DisplayPort IP core structure
 *
 * Return: true if the sync mode is on, or false
 */
static bool xlnx_dp_get_sync_mode(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;

	return !!(config->misc0 & XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC);
}

/**
 * xlnx_dp_set_bpc - Set bpc value in software misc state
 * @dp: DisplayPort IP core structure
 * @bpc: bits per component
 *
 * Return: 0 on success, or the fallback bpc value
 */
static u8 xlnx_dp_set_bpc(struct xlnx_dp *dp, u8 bpc)
{
	struct xlnx_dp_config *config = &dp->config;
	u8 ret = 0;

	if (dp->connector.display_info.bpc &&
	    dp->connector.display_info.bpc != bpc) {
		dev_err(dp->dev, "requested bpc (%u) != display info (%u)\n",
			bpc, dp->connector.display_info.bpc);
		bpc = dp->connector.display_info.bpc;
	}

	config->misc0 &= ~XLNX_DP_MISC0_BPC_MASK;
	switch (bpc) {
	case 6:
		config->misc0 |= XLNX_DP_MISC0_BPC_6;
		break;
	case 8:
		config->misc0 |= XLNX_DP_MISC0_BPC_8;
		break;
	case 10:
		config->misc0 |= XLNX_DP_MISC0_BPC_10;
		break;
	case 12:
		config->misc0 |= XLNX_DP_MISC0_BPC_12;
		break;
	case 16:
		config->misc0 |= XLNX_DP_MISC0_BPC_16;
		break;
	default:
		dev_err(dp->dev, "Not supported bpc (%u). fall back to 8bpc\n",
			bpc);
		config->misc0 |= XLNX_DP_MISC0_BPC_8;
		ret = 8;
		break;
	}
	config->bpc = bpc;
	xlnx_dp_update_bpp(dp);

	return ret;
}

/**
 * xlnx_dp_get_bpc - Set bpc value from software state
 * @dp: DisplayPort IP core structure
 *
 * Return: current bpc value
 */
static u8 xlnx_dp_get_bpc(struct xlnx_dp *dp)
{
	return dp->config.bpc;
}

/**
 * xlnx_dp_encoder_mode_set_transfer_unit - Set the transfer unit values
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Set the transfer unit, and caculate all transfer unit size related values.
 * Calculation is based on DP and IP core specification.
 */
static void
xlnx_dp_encoder_mode_set_transfer_unit(struct xlnx_dp *dp,
					 struct drm_display_mode *mode)
{
	u32 tu = XLNX_DP_TX_DEF_TRANSFER_UNIT_SIZE, temp;
	u32 bw, vid_kbytes, avg_bytes_per_tu, init_wait;

	/* Use the max transfer unit size (default) */
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_TRANSFER_UNIT_SIZE, tu);

	vid_kbytes = (mode->clock / 1000) * (dp->config.bpp / 8);
	bw = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	//avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * bw / 1000);
	avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * bw);

	temp = avg_bytes_per_tu / 1000;
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_MIN_BYTES_PER_TU,
			avg_bytes_per_tu / 1000);

	temp = (avg_bytes_per_tu % 1000) * 1024 / 1000;
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_FRAC_BYTES_PER_TU, temp);

	/* Configure the initial wait cycle based on transfer unit size */
	if (tu < (avg_bytes_per_tu / 1000))
		init_wait = 0;
	else if ((avg_bytes_per_tu / 1000) <= 4)
		init_wait = tu;
	else
		init_wait = tu - avg_bytes_per_tu / 1000;

	xlnx_dp_write(dp->iomem, XLNX_DP_TX_INIT_WAIT, init_wait);
}

/**
 * xlnx_dp_encoder_mode_set_stream - Configure the main stream
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Configure the main stream based on the requested mode @mode. Calculation is
 * based on IP core specification.
 */
static void xlnx_dp_encoder_mode_set_stream(struct xlnx_dp *dp,
				       struct drm_display_mode *mode)
{
	void __iomem *iomem = dp->iomem;
	u8 lane_cnt = dp->mode.lane_cnt;
	u32 reg, wpl;
	unsigned int temp, ppc;

	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_HTOTAL, mode->htotal);
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_VTOTAL, mode->vtotal);
	temp = (!!(mode->flags & DRM_MODE_FLAG_PVSYNC) <<
			XLNX_DP_TX_MAIN_STREAM_POLARITY_VSYNC_SHIFT) |
			(!!(mode->flags & DRM_MODE_FLAG_PHSYNC) <<
				XLNX_DP_TX_MAIN_STREAM_POLARITY_HSYNC_SHIFT);

	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_POLARITY,
			(!!(mode->flags & DRM_MODE_FLAG_PVSYNC) <<
			XLNX_DP_TX_MAIN_STREAM_POLARITY_VSYNC_SHIFT) |
			(!!(mode->flags & DRM_MODE_FLAG_PHSYNC) <<
			XLNX_DP_TX_MAIN_STREAM_POLARITY_HSYNC_SHIFT));

	temp = mode->hsync_end - mode->hsync_start;
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_HSWIDTH,
			mode->hsync_end - mode->hsync_start);

	temp = mode->vsync_end - mode->vsync_start;
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_VSWIDTH,
			mode->vsync_end - mode->vsync_start);
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_HRES, mode->hdisplay);
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_VRES, mode->vdisplay);

	temp = mode->htotal - mode->hsync_start;
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_HSTART,
			mode->htotal - mode->hsync_start);
	temp = mode->vtotal - mode->vsync_start;
	xlnx_dp_write(iomem, XLNX_DP_TX_MAIN_STREAM_VSTART,
			mode->vtotal - mode->vsync_start);
	xlnx_dp_update_misc(dp);

	reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	xlnx_dp_write(iomem, XLNX_DP_TX_N_VID, reg);
	xlnx_dp_write(iomem, XLNX_DP_TX_M_VID, mode->clock);

	/* In synchronous mode, set the diviers */
	if (dp->config.misc0 & XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC) {
		reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
		xlnx_dp_write(iomem, XLNX_DP_TX_N_VID, reg);
		xlnx_dp_write(iomem, XLNX_DP_TX_M_VID, mode->clock);
	}
	if (mode->clock > 530000)
		ppc = 4;
	else if (mode->clock > 270000)
		ppc = 2;
	else
		ppc = 1;

	xlnx_dp_write(iomem, XLNX_DP_TX_USER_PIXEL_WIDTH, ppc);
	dp->config.ppc = ppc;

	/* Translate to the native 16 bit datapath based on IP core spec */
	wpl = (mode->hdisplay * dp->config.bpp + 15) / 16;
	reg = wpl + wpl % lane_cnt - lane_cnt;
	xlnx_dp_write(iomem, XLNX_DP_TX_USER_DATA_CNT_PER_LANE, reg);
	xlnx_dp_write(iomem, XLNX_DP_TX_TRANSFER_UNIT_SIZE, 0x40);
}

/*
 * DRM connector functions
 */
static enum drm_connector_status
xlnx_dp_connector_detect(struct drm_connector *connector, bool force)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	struct xlnx_dp_link_config *link_config = &dp->link_config;
	u32 state, i;
	int ret;

	/*
	 * This is from heuristic. It takes some delay (ex, 100 ~ 500 msec) to
	 * get the HPD signal with some monitors.
	 */
	for (i = 0; i < 10; i++) {
		state = xlnx_dp_read(dp->iomem, XLNX_DP_TX_INTR_SIGNAL_STATE);
		if (state & XLNX_DP_TX_INTR_SIGNAL_STATE_HPD)
			break;
		msleep(100);
	}
	if (state & XLNX_DP_TX_INTR_SIGNAL_STATE_HPD) {
		ret = drm_dp_dpcd_read(&dp->aux, 0x0, dp->dpcd,
					sizeof(dp->dpcd));
		if (ret < 0) {
			dev_info(dp->dev, "DPCD read failes");
			goto disconnected;
		}
		link_config->max_rate = min_t(int,
						drm_dp_max_link_rate(dp->dpcd),
						dp->config.max_link_rate);
		link_config->max_lanes = min_t(u8,
						drm_dp_max_lane_count(dp->dpcd),
						dp->config.max_lanes);
		dp->status = connector_status_connected;
		set_vphy(dp->dpcd[1]);

		return connector_status_connected;
	}
disconnected:
	dp->status = connector_status_disconnected;

	return connector_status_disconnected;
}

static int xlnx_dp_connector_get_modes(struct drm_connector *connector)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	struct edid *edid;
	int ret;

	edid = drm_get_edid(connector, &dp->aux.ddc);
	if (!edid)
		return 0;

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);
	return ret;
}

static struct drm_encoder *
xlnx_dp_connector_best_encoder(struct drm_connector *connector)
{
	struct xlnx_dp *dp = connector_to_dp(connector);

	return &dp->encoder;
}

static int xlnx_dp_connector_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->config.bpp;
	int max_rate = dp->link_config.max_rate;
	int rate;

	if (mode->clock > XLNX_MAX_FREQ) {
		dev_info(dp->dev, "filtered the mode, %s,for high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);
		return MODE_CLOCK_HIGH;
	}

	/* Check with link rate and lane count */
	rate = xlnx_dp_max_rate(max_rate, max_lanes, bpp);
	if (mode->clock > rate) {
		dev_dbg(dp->dev, "filtered the mode, %s,for high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);

		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static void xlnx_dp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int
xlnx_dp_connector_atomic_set_property(struct drm_connector *connector,
					struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t val)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	int ret;

	if (property == dp->sync_prop)
		xlnx_dp_set_sync_mode(dp, val);
	else if (property == dp->bpc_prop) {
		u8 bpc;

		bpc = xlnx_dp_set_bpc(dp, val);
		if (bpc) {
			drm_object_property_set_value(&connector->base,
							property, bpc);
			ret = -EINVAL;
		}
	} else
		return -EINVAL;

	return 0;
}

static int
xlnx_dp_connector_atomic_get_property(struct drm_connector *connector,
					const struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct xlnx_dp *dp = connector_to_dp(connector);

	if (property == dp->sync_prop)
		*val = xlnx_dp_get_sync_mode(dp);
	else if (property == dp->bpc_prop)
		*val =  xlnx_dp_get_bpc(dp);
	else
		return -EINVAL;
	return 0;
}

static const struct drm_connector_funcs xlnx_dp_connector_funcs = {
	.detect			= xlnx_dp_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= xlnx_dp_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_set_property	= xlnx_dp_connector_atomic_set_property,
	.atomic_get_property	= xlnx_dp_connector_atomic_get_property,
};

static struct drm_connector_helper_funcs xlnx_dp_connector_helper_funcs = {
	.get_modes	= xlnx_dp_connector_get_modes,
	.best_encoder	= xlnx_dp_connector_best_encoder,
	.mode_valid	= xlnx_dp_connector_mode_valid,
};

static void xlnx_dp_encoder_enable(struct drm_encoder *encoder)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);
	struct drm_dp_link link;

	void __iomem *iomem = dp->iomem;
	unsigned int i;
	int ret = 0;
	pm_runtime_get_sync(dp->dev);
	dp->enabled = true;
	xlnx_dp_init_aux(dp);
	if (dp->status == connector_status_connected) {
		for (i = 0; i < 3; i++) {
			ret = drm_dp_link_power_down(&dp->aux, &link);
			ret = drm_dp_link_power_up(&dp->aux, &link);
			if (ret == 0)
				break;
			usleep_range(300, 500);
		}
		/* Some monitors take time to wake up properly */
		msleep(xlnx_dp_power_on_delay_ms);
	}
	if (ret < 0)
		dev_info(dp->dev, "DP aux failed\n");
	else
		xlnx_dp_train_loop(dp);

	xlnx_dp_write(iomem, 0xF0, 0);
	xlnx_dp_write(iomem, 0xF0, 1);
	xlnx_vtc_enable(dp);
	xlnx_dp_write(iomem, XLNX_DP_TX_ENABLE_MAIN_STREAM, 1);
}

static void xlnx_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);
	void __iomem *iomem = dp->iomem;

	dp->enabled = false;
	cancel_delayed_work(&dp->hpd_work);
	xlnx_dp_write(iomem, XLNX_DP_TX_ENABLE_MAIN_STREAM, 0);
	pm_runtime_put_sync(dp->dev);
	xlnx_vtc_disable(dp);
}

static void
xlnx_dp_encoder_atomic_mode_set(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *connector_state)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->config.bpp;
	int rate, max_rate = dp->link_config.max_rate;
	int ret;
	u32 drm_fourcc;
	struct videomode vm;

	drm_fourcc = encoder->crtc->primary->state->fb->format->format;

	xlnx_dp_set_color(dp,drm_fourcc);
	
	/* Check again as bpp or format might have been chagned */
	rate = xlnx_dp_max_rate(max_rate, max_lanes, bpp);
	if (mode->clock > rate) {
		dev_err(dp->dev, "the mode, %s,has too high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);
	}
	ret = xlnx_dp_mode_configure(dp, adjusted_mode->clock, 0);
	if (ret < 0)
		return;


	/* The timing register should be programmed always */
	xlnx_dp_encoder_mode_set_stream(dp, adjusted_mode);
	xlnx_dp_encoder_mode_set_transfer_unit(dp, adjusted_mode);

	vm.hactive = adjusted_mode->hdisplay / dp->config.ppc;
	vm.hfront_porch = (adjusted_mode->hsync_start -
				adjusted_mode->hdisplay) / dp->config.ppc;
	vm.hback_porch = (adjusted_mode->htotal -
				adjusted_mode->hsync_end) / dp->config.ppc;
	vm.hsync_len = (adjusted_mode->hsync_end -
			adjusted_mode->hsync_start) / dp->config.ppc;

	vm.vactive = adjusted_mode->vdisplay;
	vm.vfront_porch = adjusted_mode->vsync_start -
				adjusted_mode->vdisplay;
	vm.vback_porch = adjusted_mode->vtotal -
				adjusted_mode->vsync_end;
	vm.vsync_len = adjusted_mode->vsync_end -
				adjusted_mode->vsync_start;
	vm.flags = 0;
	vm.pixelclock = adjusted_mode->clock * 1000;

	clk_set_rate(dp->tx_vid_clk,(vm.pixelclock / dp->config.ppc));

	xlnx_vtc_set_timing(dp, &vm);

	/* configure remap */
	if (dp->remap_iomem) {

		/* reset the remapper before configuration */
		remap_reset(dp);

		xlnx_dp_write(dp->remap_iomem, 0x18, adjusted_mode->hdisplay);
        	xlnx_dp_write(dp->remap_iomem, 0x10, adjusted_mode->vdisplay);
        	xlnx_dp_write(dp->remap_iomem, 0x20, dp->config.fmt);
        	xlnx_dp_write(dp->remap_iomem, 0x28, 4);
        	xlnx_dp_write(dp->remap_iomem, 0x30, dp->config.ppc);
        	xlnx_dp_write(dp->remap_iomem, 0x0, 0x80);
        	xlnx_dp_write(dp->remap_iomem, 0x0, 0x81);
	}
		
}

#define XLNX_DP_MIN_H_BACKPORCH	20

static int
xlnx_dp_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int diff = mode->htotal - mode->hsync_end;

	/*
	 * Xilinx DP requires horizontal backporch to be greater than 12.
	 * This limitation may not be compatible with the sink device.
	 */
	if (diff < XLNX_DP_MIN_H_BACKPORCH) {
		int vrefresh = (adjusted_mode->clock * 1000) /
				(adjusted_mode->vtotal * adjusted_mode->htotal);
		dev_info(encoder->dev->dev, "hbackporch adjusted: %d to %d",
				diff, XLNX_DP_MIN_H_BACKPORCH - diff);
		diff = XLNX_DP_MIN_H_BACKPORCH - diff;
		adjusted_mode->htotal += diff;
		adjusted_mode->clock = adjusted_mode->vtotal *
					adjusted_mode->htotal * vrefresh / 1000;
	}

	return 0;
}

static const struct drm_encoder_funcs xlnx_dp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_encoder_helper_funcs xlnx_dp_encoder_helper_funcs = {
	.enable			= xlnx_dp_encoder_enable,
	.disable		= xlnx_dp_encoder_disable,
	.atomic_mode_set	= xlnx_dp_encoder_atomic_mode_set,
	.atomic_check		= xlnx_dp_encoder_atomic_check,
};

static void xlnx_dp_hpd_work_func(struct work_struct *work)
{
	struct xlnx_dp *dp;

	dp = container_of(work, struct xlnx_dp, hpd_work.work);

	if (dp->drm)
		drm_helper_hpd_irq_event(dp->drm);
}

static struct drm_prop_enum_list xlnx_dp_bpc_enum[] = {
	{ 6, "6BPC" },
	{ 8, "8BPC" },
	{ 10, "10BPC" },
	{ 12, "12BPC" },
};

static int xlnx_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_connector *connector = &dp->connector;
	struct drm_device *drm = data;
	int ret;

	encoder->possible_crtcs = 1;
	drm_encoder_init(drm, encoder, &xlnx_dp_encoder_funcs,
				DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(encoder, &xlnx_dp_encoder_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_HPD;
	ret = drm_connector_init(encoder->dev, connector,
				 &xlnx_dp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		dev_err(dp->dev, "failed to initialize the drm connector");
		goto error_encoder;
	}

	drm_connector_helper_add(connector, &xlnx_dp_connector_helper_funcs);
	drm_connector_register(connector);
	drm_connector_attach_encoder(connector, encoder);
	connector->dpms = DRM_MODE_DPMS_OFF;

	dp->drm = drm;
	dp->sync_prop = drm_property_create_bool(drm, 0, "sync");
	dp->bpc_prop = drm_property_create_enum(drm, 0, "bpc",
						xlnx_dp_bpc_enum,
						ARRAY_SIZE(xlnx_dp_bpc_enum));

	dp->config.misc0 &= ~XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC;
	drm_object_attach_property(&connector->base, dp->sync_prop, false);
	ret = xlnx_dp_set_bpc(dp, 8);
	drm_object_attach_property(&connector->base, dp->bpc_prop,
				   ret ? ret : 8);
	xlnx_dp_update_bpp(dp);

	/* This enables interrupts, so should be called after DRM init */
	ret = xlnx_dp_init_aux(dp);
	if (ret) {
		dev_err(dp->dev, "failed to initialize DP aux");
		goto error_prop;
	}
	INIT_DELAYED_WORK(&dp->hpd_work, xlnx_dp_hpd_work_func);

	return 0;

error_prop:
	drm_property_destroy(dp->drm, dp->bpc_prop);
	drm_property_destroy(dp->drm, dp->sync_prop);
	xlnx_dp_connector_destroy(&dp->connector);
error_encoder:
	drm_encoder_cleanup(&dp->encoder);
	return ret;
}

static void xlnx_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&dp->hpd_work);
	disable_irq(dp->irq);
	xlnx_dp_exit_aux(dp);
	drm_property_destroy(dp->drm, dp->bpc_prop);
	drm_property_destroy(dp->drm, dp->sync_prop);
	xlnx_dp_connector_destroy(&dp->connector);
	drm_encoder_cleanup(&dp->encoder);
}

int xlnx_dp_istxconnected(struct xlnx_dp *dp)
{
	u32 status;
	u8 retries = 0;

	do {
		status = xlnx_dp_read(dp->iomem,
					XLNX_DP_TX_INTR_SIGNAL_STATE) & 0x1;
		if (retries > 50)
			return 0;
		retries++;
		usleep_range(1000, 1100);
	} while (status == 0);

	return 1;
}

static irqreturn_t xlnx_dp_irq_handler(int irq, void *data)
{
	struct xlnx_dp *dp = (struct xlnx_dp *)data;
	u32 intrstatus;
	u32 hpdeventdetected, hpdpulsedetected;
	u32 hpdduration;

	/* Determine what kind of interrupt occurred. */
	intrstatus = xlnx_dp_read(dp->iomem, XLNX_DP_TX_INTR_STATUS);
	intrstatus &= ~xlnx_dp_read(dp->iomem, XLNX_DP_TX_INTR_MASK);
	hpdeventdetected = intrstatus & XLNX_DP_TX_INTR_HPD_EVENT;
	hpdpulsedetected = intrstatus & XLNX_DP_TX_INTR_HPD_PULSE;

	if (!intrstatus)
		return IRQ_NONE;
	if (hpdeventdetected)
		dev_info(dp->dev, "hpdevent detected\n");
	else if (hpdpulsedetected && xlnx_dp_istxconnected(dp)) {
		hpdduration = xlnx_dp_read(dp->iomem, XLNX_DP_TX_HPD_DURATION);
		if (hpdduration >= 500)
			xlnx_dp_write(dp->iomem, XLNX_DP_TX_INTR_MASK, 0x10);
	}

	/* dbg for diagnostic, but not much that the driver can do */
	if (intrstatus & XLNX_DP_TX_INTR_CHBUF_UNDERFLW_MASK)
		dev_info(dp->dev, "underflow interrupt\n");
	if (intrstatus & XLNX_DP_TX_INTR_CHBUF_OVERFLW_MASK)
		dev_info(dp->dev, "overflow interrupt\n");

	/* The DP vblank should be enabled with xlnx_pl_disp */
	if (intrstatus & XLNX_DP_TX_INTR_HPD_EVENT)
		schedule_delayed_work(&dp->hpd_work, 0);

	return IRQ_HANDLED;
}

static const struct component_ops xlnx_dp_component_ops = {
	.bind	= xlnx_dp_bind,
	.unbind	= xlnx_dp_unbind,
};

static int xlnx_dp_parse_of(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;
	struct device_node *node = dp->dev->of_node;
	const char *string;
	u32 num_colors, bpc;
	bool sync;
	int ret;

	ret = of_property_read_u32(node, "xlnx,max-lanes", &config->max_lanes);

	if (ret < 0) {
		dev_err(dp->dev, "No lane count in DT\n");
		return ret;
	}
	if (config->max_lanes != 1 && config->max_lanes != 2 &&
		config->max_lanes != 4) {
		dev_err(dp->dev, "Invalid max lanes in DT\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,max-link-rate",
					&config->max_link_rate);
		
	if (ret < 0) {
		dev_err(dp->dev, "No link rate in DT\n");
		return ret;
	}

	if (config->max_link_rate != DP_REDUCED_BIT_RATE &&
		config->max_link_rate != DP_HIGH_BIT_RATE &&
		config->max_link_rate != DP_HIGH_BIT_RATE2 &&
		config->max_link_rate != DP_HIGH_BIT_RATE3) {
		dev_err(dp->dev, "Invalid link rate in DT\n");
		return -EINVAL;
	}

	sync = of_property_read_bool(node, "xlnx,sync");
	if (sync)
		config->misc0 |= XLNX_DP_TX_MAIN_STREAM_MISC0_SYNC;

	ret = of_property_read_string(node, "xlnx,colormetry", &string);
	if (ret < 0) {
		dev_err(dp->dev, "No colormetry in DT\n");
		return ret;
	}
	xlnx_dp_set_color(dp,DRM_FORMAT_RGB888);

	ret = of_property_read_u32(node, "xlnx,max-bpc", &config->max_bpc);
	if (ret < 0) {
		dev_err(dp->dev, "No max bpc in DT\n");
		return ret;
	}
	if (config->max_bpc != 8 && config->max_bpc != 10 &&
		config->max_bpc != 12 && config->max_bpc != 16) {
		dev_err(dp->dev, "Invalid max bpc in DT\n");
		return -EINVAL;
	}
	
	ret = of_property_read_u32(node, "xlnx,bpc", &bpc);
	if (ret < 0) {
		dev_err(dp->dev, "No color depth(bpc) in DT\n");
		return ret;
	}
	if (bpc > config->max_bpc) {
		dev_err(dp->dev, "Invalid color depth(bpc) in DT\n");
		return -EINVAL;
	}

	switch (bpc) {
	case 6:
		config->misc0 |= XLNX_DP_MISC0_BPC_6;
		break;
	case 8:
		config->misc0 |= XLNX_DP_MISC0_BPC_8;
		break;
	case 10:
		config->misc0 |= XLNX_DP_MISC0_BPC_10;
		break;
	case 12:
		config->misc0 |= XLNX_DP_MISC0_BPC_12;
		break;
	case 16:
		config->misc0 |= XLNX_DP_MISC0_BPC_16;
		break;
	default:
		dev_err(dp->dev, "Not supported color depth in DT\n");
		return -EINVAL;
	}
	config->bpp = num_colors * bpc;
	of_property_read_u32(node, "xlnx,max-pclock-frequency",
				&config->max_pclock);

	return 0;
}

static int xlnx_dp_probe(struct platform_device *pdev)
{
	struct xlnx_dp *dp;
	struct resource *res;
	struct device_node *node, *remap_node, *clkwiz_node;
        void __iomem *remap_base, *clkwiz_base;
        struct resource remap_res, clkwiz_res;

	int irq, ret;

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->dpms = DRM_MODE_DPMS_OFF;
	dp->status = connector_status_disconnected;
	dp->dev = &pdev->dev;

	ret = xlnx_dp_parse_of(dp);
	if (ret < 0)
		return ret;

	dp->axi_lite_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(dp->axi_lite_clk))
		return PTR_ERR(dp->axi_lite_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dp->iomem = devm_ioremap_resource(dp->dev, res);
	if (IS_ERR(dp->iomem))
		return PTR_ERR(dp->iomem);

	platform_set_drvdata(pdev, dp);
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_ENABLE, 0);
	xlnx_dp_write(dp->iomem, XLNX_DP_TX_ENABLE_MAIN_STREAM, 0);

	dp->dp_phy_dev = register_dp_cb();
	if (!dp->dp_phy_dev)
		return -EINVAL;

	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;

	ret = xlnx_dp_init_phy(dp);
	if (ret)
		goto error_phy;
	dp->aclk = devm_clk_get(dp->dev, "s_axi_aclk");
	if (IS_ERR(dp->aclk))
		return PTR_ERR(dp->aclk);

	dp->reset_gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dp->reset_gpio)) {
		ret = PTR_ERR(dp->reset_gpio);
		if (ret == -EPROBE_DEFER)
			dev_dbg(&pdev->dev, "No gpio probed for remapper-tx. Deferring\n");
		else
			dev_err(&pdev->dev, "No reset gpio info from dts for remapper-tx\n");
		return ret;
	}
	remap_reset(dp);
	dp->aux.name = "Xlnx DP AUX";
	dp->aux.dev = dp->dev;
	dp->aux.transfer = xlnx_dp_aux_transfer;
	ret = drm_dp_aux_register(&dp->aux);
	if (ret < 0) {
		dev_err(dp->dev, "failed to initialize DP aux\n");
		goto error;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto error;
	}
	ret = devm_request_threaded_irq(dp->dev, irq, NULL,
					xlnx_dp_irq_handler, IRQF_ONESHOT,
					dev_name(dp->dev), dp);

	if (ret < 0)
		goto error;
	dp->irq = irq;
	dp->tx_link_config.tx_vs_offset = XDP_TX_VS_LEVEL_OFFSET;
	dp->tx_link_config.tx_vs_levels[0] = XDP_TX_VS_LEVEL_0;
	dp->tx_link_config.tx_vs_levels[1] = XDP_TX_VS_LEVEL_1;
	dp->tx_link_config.tx_vs_levels[2] = XDP_TX_VS_LEVEL_2;
	dp->tx_link_config.tx_vs_levels[3] = XDP_TX_VS_LEVEL_3;

	dp->tx_link_config.tx_pe_levels[0] = XDP_TX_PE_LEVEL_0;
	dp->tx_link_config.tx_pe_levels[1] = XDP_TX_PE_LEVEL_1;
	dp->tx_link_config.tx_pe_levels[2] = XDP_TX_PE_LEVEL_2;
	dp->tx_link_config.tx_pe_levels[3] = XDP_TX_PE_LEVEL_3;

	dp->tx_vid_clk = devm_clk_get(&pdev->dev, "tx_vid_clk_stream1");
	if (IS_ERR(dp->tx_vid_clk))
			dev_err(dp->dev, "failed to get vid clk stream1 \n");
    
	/* remap node base */ 
	node = dp->dev->of_node;
        remap_node = of_parse_phandle(node, "xlnx,remap-tx", 0);
        if (!remap_node) {
                dev_err(dp->dev, "failed to get remap_node!\n");
                remap_base = NULL;
        } else {
                ret = of_address_to_resource(remap_node, 0, &remap_res);
                if (ret) {
                        dev_err(dp->dev, "remap resource failed: %d\n", ret);
                        remap_base = NULL;
                } else {
                        remap_base = devm_ioremap_resource(dp->dev, &remap_res);
                        if (IS_ERR(remap_base)) {
                                dev_err(dp->dev, "remap ioremap failed\n");
                                remap_base = NULL;
                        }
                }
                of_node_put(remap_node);
        }

	dp->remap_iomem = remap_base;


	return component_add(&pdev->dev, &xlnx_dp_component_ops);
error:
	drm_dp_aux_unregister(&dp->aux);
error_phy:
	xlnx_dp_exit_phy(dp);
	return ret;
}

static int xlnx_dp_remove(struct platform_device *pdev)
{
	struct xlnx_dp *dp = platform_get_drvdata(pdev);

	xlnx_dp_write(dp->iomem, XLNX_DP_TX_ENABLE, 0);
	drm_dp_aux_unregister(&dp->aux);
	xlnx_dp_exit_phy(dp);
	component_del(&pdev->dev, &xlnx_dp_component_ops);

	return 0;
}

static const struct of_device_id xlnx_dp_of_match[] = {
	{ .compatible = "xlnx,v-dp-txss1-2.1"},
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_dp_of_match);

static struct platform_driver dp_tx_driver = {
	.probe = xlnx_dp_probe,
	.remove = xlnx_dp_remove,
	.driver = {
		.name = "xlnx-dp-tx",
		.of_match_table = xlnx_dp_of_match,
	},
};

module_platform_driver(dp_tx_driver);

MODULE_AUTHOR("GV Rao <vgannava@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA DisplayPort Tx Driver");
MODULE_LICENSE("GPL v2");

