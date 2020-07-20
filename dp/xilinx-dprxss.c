// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DP Rx Subsystem
 *
 * Copyright (C) 2020 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *
 */
#include <dt-bindings/media/xilinx-vip.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h> 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/v4l2-subdev.h>
#include <linux/xilinx-v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dv-timings.h>

#include "xilinx-vip.h"
/*
 * DP Rx subsysetm register map, bitmask and offsets
 */
#define XDP_RX_LANE_CNT_SET_TPS3_MASK			0x40	
#define XDP_RX_LANE_CNT_SET_ENHANCED_FRAME_CAP_MASK	0x80	
#define DPRXSS_LANE_COUNT_SET_REG			0x04
#define XDP_RX_LINK_ENABLE_REG				0x000	
#define XDP_RX_DTG_REG					0x00C	
#define XDP_RX_HPD_INTERRUPT_REG			0x02C
#define XDP_RX_CRC_CONFIG_REG 				0x074
#define XDP_RX_AUX_CLK_DIVIDER_REG			0x004	
#define XDP_RX_INTERRUPT_CAUSE_REG			0x040
#define XDP_RX_INTERRUPT_CAUSE_1_REG			0x048
#define XDP_RX_INTERRUPT_MASK_REG                       0x014   
#define XDP_RX_INTERRUPT_MASK_1_REG                     0x044   
#define XDP_RX_SOFT_RESET_REG                           0x01C 
#define XDP_RX_LINE_RESET_DISABLE			0x008
#define XDP_RX_PIXEL_WIDTH				0x010
#define XDP_RX_VIDEO_UNSUPPORTED_REG			0x094
#define XDP_RX_CTRL_DPCD_REG				0x0B8
#define XDP_RX_LINK_BW_SET_REG				0x09C
#define XDP_RX_LANE_CNT_SET_REG				0x0A0	
#define XDP_RX_LOCAL_EDID_VIDEO_REG			0x084
#define XDP_RX_MST_CAP_REG				0x0D0	
#define XDP_RX_TP_SET_REG				0x0A4	
#define XDP_RX_SINK_COUNT_REG				0x0D4
#define XDP_RX_LANE_COUNT_SET				0x404
#define XDP_SET_POWER_STATE_REG				0x438
#define XDP_LINK_BW_SET_REG				0x400
#define XDP_RX_MSA_HRES					0x500	
#define XDP_RX_MSA_VHEIGHT				0x514	
#define XDP_RX_MSA_HTOTAL				0x510
#define XDP_RX_MSA_VTOTAL				0x524
#define XDP_RX_MSA_MISC0				0x528
#define XDP_RX_MSA_MISC1				0x52C
#define XDP_RX_MSA_MVID					0x530
#define XDP_RX_MSA_NVID					0x534
#define XDP_RX_PHY_REG					0x200	
#define XDP_RX_MIN_VOLTAGE_SWING			0x214
#define XDP_RX_CDR_CONTROL_CONFIG			0x21C
#define XDP_RX_BS_IDLE_TIME				0x220
#define DPRXSS_LINK_RATE				0x1E

#define XDP_MASK_INTERLACE				BIT(0)
#define XDP_CABLE_POWER_UP_MASK				BIT(0)
#define XDP_RX_VIDEO_SOFT_RESET_MASK			BIT(0)
#define XDP_RX_INTERRUPT_POWER_STATE_MASK		BIT(1)
#define XDP_RX_HPD_INTERRUPT_ASSERT_MASK 		BIT(1)
#define XDP_RX_INTERRUPT_NO_VIDEO_MASK			BIT(2)
#define XDP_RX_INTERRUPT_VBLANK_MASK			BIT(3)
#define XDP_RX_INTERRUPT_TRAINING_LOST_MASK		BIT(4)
#define XDP_RX_CRC_EN_MASK				BIT(5)
#define XDP_RX_INTERRUPT_VIDEO_MASK			BIT(6)
#define XDP_RX_AUX_SOFT_RESET_MASK			BIT(7)
#define XDP_RX_INTERRUPT_TRAINING_DONE_MASK		BIT(14)
#define XDP_RX_INTERRUPT_BW_CHANGE_MASK			BIT(15)
#define XDP_RX_TRNG_SET_AUX_RD_INTERVAL_SET		BIT(15)
#define XDP_RX_INTERRUPT_TP1_MASK			BIT(16)
#define XDP_RX_INTERRUPT_T2_MASK			BIT(17)
#define XDP_RX_INTERRUPT_TP3_MASK			BIT(18)
#define XDP_RX_INTERRUPT_ACCESS_ERROR_CNT_MASK		BIT(28)
#define XDP_RX_INTERRUPT_ACCESS_LINK_QUAL_MASK		BIT(29)
#define XDP_RX_INTERRUPT_TP4_MASK_1			BIT(31)
#define XDP_RX_INTERRUPT_UNPLUG_MASK			BIT(31)
#define XDP_RX_INTERRUPT_CRC_TEST_MASK			BIT(30)
#define XDP_RX_INTERRUPT_ACCESS_LANE_SET_MASK 		BIT(30)

#define XDP_RX_PHY_REG_GTPLL_RESET_MASK				BIT(0)
#define XDP_RX_PHY_REG_GTRX_RESET_MASK				BIT(1)
#define XDP_RX_PHY_REG_RESET_AT_TRAIN_ITER_MASK			BIT(23)
#define XDP_RX_PHY_REG_RESET_AT_LINK_RATE_CHANGE_MASK		BIT(24)
#define XDP_RX_PHY_REG_RESET_AT_TP1_START_MASK 			BIT(25)

#define XDP_RX_TRAINING_INTERRUPT_MASK		(XDP_RX_INTERRUPT_TP1_MASK | \
						XDP_RX_INTERRUPT_T2_MASK |\
						XDP_RX_INTERRUPT_TP3_MASK | \
						XDP_RX_INTERRUPT_POWER_STATE_MASK |\
						XDP_RX_INTERRUPT_CRC_TEST_MASK |\
						XDP_RX_INTERRUPT_BW_CHANGE_MASK)


#define XDP_RX_TRAINING_INTERRUPT_MASK_1	(XDP_RX_INTERRUPT_TP4_MASK_1 |\
    						XDP_RX_INTERRUPT_ACCESS_LANE_SET_MASK |\
    						XDP_RX_INTERRUPT_ACCESS_LINK_QUAL_MASK |\
    						XDP_RX_INTERRUPT_ACCESS_ERROR_CNT_MASK)

#define XDP_RX_MIN_VOLTAGE_SWING_SET_PE_SHIFT			12
#define XDP_RX_MIN_VOLTAGE_SWING_VS_SWEEP_CNT_SHIFT 		4
#define XDP_RX_MIN_VOLTAGE_SWING_CR_OPT_SHIFT 			2
#define XDP_RX_MIN_VOLTAGE_SWING_CR_OPT_VS_INC_4CNT  		1
#define XDP_RX_MIN_VOLTAGE_SWING_MASK	\
  				(1 | (XDP_RX_MIN_VOLTAGE_SWING_CR_OPT_VS_INC_4CNT << \
				XDP_RX_MIN_VOLTAGE_SWING_CR_OPT_SHIFT) | \
   				(4 << XDP_RX_MIN_VOLTAGE_SWING_VS_SWEEP_CNT_SHIFT) | \
   				(1 << XDP_RX_MIN_VOLTAGE_SWING_SET_PE_SHIFT))





#define EDID_NUM_BLOCKS	 	3
#define EDID_LENGTH		(EDID_BLOCK_SIZE * EDID_NUM_BLOCKS * 4)

#define XDPRX_DEFAULT_WIDTH		(7680)
#define XDPRX_DEFAULT_HEIGHT		(4320)


#define XDP_RX_INTERRUPT_ALL_MASK_1			0xFFFFFFFF
#define XDP_RX_INTERRUPT_ALL_MASK			0xF9FFFFFF
#define XDP_RX_AUX_DEFER_COUNT				6
#define XDP_RX_AUX_DEFER_SHIFT				24	
#define XDP_RX_HPD_PLUSE_DURATION_750			750
#define XDP_RX_HPD_PLUSE_DURATION_5000			5000
#define XDP_RX_HPD_INTERRUPT_LENGTH_US_SHIFT 		16
#define XDP_DPCD_TRAIN_AUX_RD_INT_16MS			4
#define Enable 						1
#define Disable						0
#define MHZ 						1000000
#define EDID_BLOCKS_MAX 				10
#define EDID_BLOCK_SIZE 				128
#define XDP_RX_PHY_REG_INIT_MASK			0x38000000
#define DP_BS_IDLE_TIMEOUT       			0x0091FFFF 
#define XDP_RX_PHY_REG_RESET_ENBL_MASK			0x0
#define XDP_RX_COLOR_FORMAT_MASK			GENMASK(2,1)
#define XDP_RX_MSA_MISC0_MASK				GENMASK(2,0)
#define XDP_RX_CDR_CONTROL_TDLOCK_DP159 		0x1388
#define XDP_RX_CDR_CONTROL_DISABLE_TIMEOUT		0x40000000	
#define XDP_RX_SET_TRNG_AUX_RD_INTERVAL_SHIFT  		8

/*Vphy driver calls */

void DpRxSs_LinkBandwidthHandler(u8 linkrate);
void DpRxSs_PllResetHandler(void);

enum xdprxss_pads {
	XDPRX_PAD_SOURCE,
	XDPRX_MEDIA_PADS,
};

u8 bpc[] = {6, 8, 10, 12, 16};

/**
 * enum xdprxss_color_formats - DP RX supported Video Formats
 * @XDP_RX_COLOR_FORMAT_RGB: RGB color format
 * @XDP_RX_COLOR_FORMAT_422:YUV 422 color format
 * @XDP_RX_COLOR_FORMAT_444:YUV 444 color format
 */
enum xdprxss_color_formats {
	XDP_RX_COLOR_FORMAT_RGB,
	XDP_RX_COLOR_FORMAT_422,
	XDP_RX_COLOR_FORMAT_444,
};

/**
 * struct xdprxss_core - Core configuration DP Rx Subsystem device structure
 * @dev: Platform structure
 * @iomem: Base address of DP Rx Subsystem
 * @vid_edid_base: Bare Address of EDID block  
 * @irq: requested irq number
 * @lock: Mutex lock 
 * @axi_clk: Axi lite interface clock
 * @rx_lnk_clk: DP Rx GT clock
 * @rx_vid_clk: DP RX Video clock
 */

struct xdprxss_core {
	struct device *dev;
	void __iomem *iomem;
	void __iomem *vid_edid_base;
	int irq;
	struct mutex lock;
	struct clk *axi_clk;
	struct clk *rx_lnk_clk;
	struct clk *rx_vid_clk;
};

/**
 * struct xdprxss_state - DP Rx Subsystem device structure
 * @core: Core structure for DP Rx Subsystem
 * @subdev: The v4l2 subdev structure
 * @ctrl_handler: control handler
 * @event: Holds the video unlock event
 * @formats: Active V4L2 formats on each pad
 * @default_format: default V4L2 media bus format
 * @detected_format: Detected stream format
 * @detected_timings: Detected Video timings
 * @frame_interval: Captures the frame rate
 * @vip_format: format information corresponding to the active format
 * @lock: Mutex lock
 * @pads: media pads
 * @vip_format:Video Frame format
 * @streaming: Flag for storing streaming state
 * @Valid_Stream: To indicate valid video
 * @cable_connected: DP Rx cable connection state
 * @edid_user_blocks:Custom EDID blocks
 * @edid_blocks_max : Max EDID blocks supported 
 * @height:Active video frame height 
 * @width:Active video frame width
 * fmt:Frame format
 * @bpc:Bits per component
 * @framerate: Frame rate of detected video 
 * This structure contains the device driver related parameters
 */
struct xdprxss_state {
	struct xdprxss_core core;
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_event event;
	struct v4l2_mbus_framefmt formats[XDPRX_MEDIA_PADS];
	struct v4l2_mbus_framefmt default_format;
	struct v4l2_mbus_framefmt detected_format;
	struct v4l2_dv_timings detected_timings;
	struct v4l2_fract frame_interval;
	struct workqueue_struct *work_queue;
	struct delayed_work delayed_work_enable_hotplug;
	struct mutex lock;
	struct media_pad pads[XDPRX_MEDIA_PADS];
	const struct xvip_video_format *vip_format;
	bool streaming;
	bool Valid_Stream;
	bool cable_connected;
	int edid_user_blocks;
	int edid_blocks_max;
	int height;
	int width;
	int fmt;
	int bpc;
	int framerate;
	unsigned int  edid_user[EDID_BLOCKS_MAX * EDID_BLOCK_SIZE];
};

 static const u8 xilinx_edid[384] = {
0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x10, 0xAC, 0x47, 0x41,
0x4C, 0x35, 0x37, 0x30, 0x20, 0x1B, 0x01, 0x04, 0xB5, 0x46, 0x27, 0x78,
0x3A, 0x76, 0x45, 0xAE, 0x51, 0x33, 0xBA, 0x26, 0x0D, 0x50, 0x54, 0xA5,
0x4B, 0x00, 0x81, 0x00, 0xB3, 0x00, 0xD1, 0x00, 0xA9, 0x40, 0x81, 0x80,
0xD1, 0xC0, 0x01, 0x01, 0x01, 0x01, 0x4D, 0xD0, 0x00, 0xA0, 0xF0, 0x70,
0x3E, 0x80, 0x30, 0x20, 0x35, 0x00, 0xBA, 0x89, 0x21, 0x00, 0x00, 0x1A,
0x00, 0x00, 0x00, 0xFF, 0x00, 0x46, 0x46, 0x4E, 0x58, 0x4D, 0x37, 0x38,
0x37, 0x30, 0x37, 0x35, 0x4C, 0x0A, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x44,
0x45, 0x4C, 0x4C, 0x20, 0x55, 0x50, 0x33, 0x32, 0x31, 0x38, 0x4B, 0x0A,
0x00, 0x00, 0x00, 0xFD, 0x00, 0x18, 0x4B, 0x1E, 0xB4, 0x6C, 0x01, 0x0A,
0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0x70, 0x02, 0x03, 0x1D, 0xF1,
0x50, 0x10, 0x1F, 0x20, 0x05, 0x14, 0x04, 0x13, 0x12, 0x11, 0x03, 0x02,
0x16, 0x15, 0x07, 0x06, 0x01, 0x23, 0x09, 0x1F, 0x07, 0x83, 0x01, 0x00,
0x00, 0xA3, 0x66, 0x00, 0xA0, 0xF0, 0x70, 0x1F, 0x80, 0x30, 0x20, 0x35,
0x00, 0xBA, 0x89, 0x21, 0x00, 0x00, 0x1A, 0x56, 0x5E, 0x00, 0xA0, 0xA0,
0xA0, 0x29, 0x50, 0x30, 0x20, 0x35, 0x00, 0xBA, 0x89, 0x21, 0x00, 0x00,
0x1A, 0x7C, 0x39, 0x00, 0xA0, 0x80, 0x38, 0x1F, 0x40, 0x30, 0x20, 0x3A,
0x00, 0xBA, 0x89, 0x21, 0x00, 0x00, 0x1A, 0xA8, 0x16, 0x00, 0xA0, 0x80,
0x38, 0x13, 0x40, 0x30, 0x20, 0x3A, 0x00, 0xBA, 0x89, 0x21, 0x00, 0x00,
0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x47, 0x70, 0x12, 0x79, 0x00, 0x00, 0x12, 0x00, 0x16,
0x82, 0x10, 0x10, 0x00, 0xFF, 0x0E, 0xDF, 0x10, 0x00, 0x00, 0x00, 0x00,
0x00, 0x44, 0x45, 0x4C, 0x47, 0x41, 0x4C, 0x35, 0x37, 0x30, 0x03, 0x01,
0x50, 0x70, 0x92, 0x01, 0x84, 0xFF, 0x1D, 0xC7, 0x00, 0x1D, 0x80, 0x09,
0x00, 0xDF, 0x10, 0x2F, 0x00, 0x02, 0x00, 0x04, 0x00, 0xC1, 0x42, 0x01,
0x84, 0xFF, 0x1D, 0xC7, 0x00, 0x2F, 0x80, 0x1F, 0x00, 0xDF, 0x10, 0x30,
0x00, 0x02, 0x00, 0x04, 0x00, 0xA8, 0x4E, 0x01, 0x04, 0xFF, 0x0E, 0xC7,
0x00, 0x2F, 0x80, 0x1F, 0x00, 0xDF, 0x10, 0x61, 0x00, 0x02, 0x00, 0x09,
0x00, 0x97, 0x9D, 0x01, 0x04, 0xFF, 0x0E, 0xC7, 0x00, 0x2F, 0x80, 0x1F,
0x00, 0xDF, 0x10, 0x2F, 0x00, 0x02, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x90,
};

static const struct v4l2_dv_timings_cap xdprxss_dv_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },

	V4L2_INIT_BT_TIMINGS(
		800, 7680,			/* min/max width */
		600, 4320,			/* min/max height */
		25000000, 297000000,		/* min/max pixelclock */
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM
	)
};
static inline struct xdprxss_state *
to_xdprxssstate(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xdprxss_state, subdev);
}

/*
 * Register related operations
 */
static inline u32 xdprxss_read(struct xdprxss_core *xdprxss, u32 addr)
{
	return ioread32(xdprxss->iomem + addr);
}

static inline void xdprxss_write(struct xdprxss_core *xdprxss, u32 addr,
				u32 value)
{
	iowrite32(value, xdprxss->iomem + addr);
}

static inline void xdprxss_clr(struct xdprxss_core *xdprxss, u32 addr,
				u32 clr)
{
	xdprxss_write(xdprxss, addr, xdprxss_read(xdprxss, addr) & ~clr);
}

static inline void xdprxss_set(struct xdprxss_core *xdprxss, u32 addr,
				u32 set)
{
	xdprxss_write(xdprxss, addr, xdprxss_read(xdprxss, addr) | set);
}

static void xdprxss_enablelnk(struct xdprxss_core *core, u32 mask)
{
	xdprxss_write(core,XDP_RX_LINK_ENABLE_REG,mask);
}
static inline u32 xdprxss_power_preset(struct xdprxss_core *core)
{
	return ( XDP_CABLE_POWER_UP_MASK & xdprxss_read(core,XDP_SET_POWER_STATE_REG));
}

static inline void xedid_write(struct xdprxss_core *xdprxss, u32 addr, u32 value)
{
 	iowrite32(value,xdprxss->vid_edid_base + addr);
}

static void xdprxss_dpcd_write(struct xdprxss_core *core,u32 addr,u32 val)
{
	xdprxss_write(core,XDP_RX_CTRL_DPCD_REG,Enable);
	xdprxss_write(core,addr,val);	
	xdprxss_write(core,XDP_RX_CTRL_DPCD_REG,Disable);
}

static void xdprxss_set_linkrate(struct xdprxss_core *core, u32 value)
{
	xdprxss_dpcd_write(core,XDP_RX_LINK_BW_SET_REG,value);
}

static void xdprxss_set_lane_count(struct xdprxss_core *core, u32 value)
{
	value |=(XDP_RX_LANE_CNT_SET_ENHANCED_FRAME_CAP_MASK |
				XDP_RX_LANE_CNT_SET_TPS3_MASK);
	xdprxss_dpcd_write(core,XDP_RX_LANE_CNT_SET_REG,value);
}

static void xdprxss_disableintr(struct xdprxss_core *core,u32 addr,u32 mask)
{
	xdprxss_set(core,addr,mask);
}

static void xdprxss_enableintr(struct xdprxss_core *core,u32 addr,u32 mask)
{
	xdprxss_clr(core,addr,mask);
}

static void xdprxss_dtg_reset(struct xdprxss_core * core)
{
	xdprxss_set(core,XDP_RX_DTG_REG,Disable);
	xdprxss_set(core,XDP_RX_DTG_REG,Enable);
}

	/*  Assert HPD Interrupt*/
static void xdprxss_generate_hpd(struct xdprxss_core *core,u32 PluseDuration)
{
	xdprxss_write(core,XDP_RX_HPD_INTERRUPT_REG,
			(PluseDuration << XDP_RX_HPD_INTERRUPT_LENGTH_US_SHIFT) | 
			XDP_RX_HPD_INTERRUPT_ASSERT_MASK);

}

/**
 * xdprxss_soft_reset - software reset
 * @core: pointer to driver core structure
 * @addr: software reset register address
 * @val : value to be written in the register 
 * This function resets the AUX logic and video logic in DP-RX core
 *
 */
static void xdprxss_soft_reset(struct xdprxss_core *core,u32 addr,u8 val)
{

	if(val & XDP_RX_AUX_SOFT_RESET_MASK){
		xdprxss_set(core,addr,XDP_RX_AUX_SOFT_RESET_MASK);
		xdprxss_clr(core,addr,XDP_RX_AUX_SOFT_RESET_MASK);
  	}
	else if (val & XDP_RX_VIDEO_SOFT_RESET_MASK){
		xdprxss_set(core,addr,XDP_RX_VIDEO_SOFT_RESET_MASK);
		xdprxss_clr(core,addr,XDP_RX_VIDEO_SOFT_RESET_MASK);
	}
}
 /**
 *@core: pointer to driver core structure
 *@pixel_width:pixel width to be configured
 * This function configures the number of pixels output 
 */
static void xdprxss_set_pixel_width(struct xdprxss_core *core,u32 pixel_width){

	u32 ReadVal;

	ReadVal = xdprxss_read(core, XDP_RX_DTG_REG);
	xdprxss_write(core, XDP_RX_DTG_REG,(ReadVal & 0xFFFFFFFE));	
	xdprxss_write(core,XDP_RX_PIXEL_WIDTH,pixel_width);
	ReadVal = xdprxss_read(core,XDP_RX_DTG_REG);
	xdprxss_write(core, XDP_RX_DTG_REG,(ReadVal | 0x1));
	
}

/**
 * xdprxss_get_detected_timings - Get detected dv timings  
 * @state: pointer to driver state
 *
 * This function updates the dv timings structure and frame rate by reading MSA values
 *
 * Return: 0 for success else errors
 */
static int xdprxss_get_detected_timings(struct xdprxss_state *state){
	
	struct xdprxss_core *core = &state->core;
	struct v4l2_bt_timings *bt = &state->detected_timings.bt;

	u32 rxMsaMVid,rxMsaNVid,rxMsamisc,recv_clk_freq,linkrate;
	u16 vres_total,hres_total,framerate,hres,vres,lanecount;
	u8 rxMsabpc,pixel_width;

	rxMsaMVid = xdprxss_read(core,XDP_RX_MSA_MVID);
	rxMsaNVid = xdprxss_read(core,XDP_RX_MSA_NVID);
	
	hres = xdprxss_read(core,XDP_RX_MSA_HRES);
	vres = xdprxss_read(core,XDP_RX_MSA_VHEIGHT);
	rxMsamisc = xdprxss_read(core,XDP_RX_MSA_MISC0);

	vres_total = xdprxss_read(core,XDP_RX_MSA_VTOTAL);
	hres_total = xdprxss_read(core,XDP_RX_MSA_HTOTAL);
	linkrate = xdprxss_read(core,XDP_LINK_BW_SET_REG);	
	lanecount = xdprxss_read(core,XDP_RX_LANE_COUNT_SET);	
	
	recv_clk_freq = ((linkrate*27)*rxMsaMVid)/rxMsaNVid;
	
	if((recv_clk_freq*MHZ) > 540000000 && (lanecount==4)){
		pixel_width = 0x4;
	}
	else if((recv_clk_freq*MHZ) > 270000000 && (lanecount!=1)){
		pixel_width = 0x2;
	}
	else{
		pixel_width = 0x1;
	}
	xdprxss_write(core,XDP_RX_LINE_RESET_DISABLE,0x1);
	xdprxss_set_pixel_width(core,pixel_width);
	
	framerate  = ((recv_clk_freq*MHZ)/(hres_total * vres_total) < 0 ?
		(recv_clk_freq*MHZ)/(hres_total * vres_total) :
		(recv_clk_freq*MHZ)/(hres_total * vres_total));

	framerate = (framerate > 0) ? 
		roundup(framerate, 5) : 0;
	
	state->width = hres;
	state->height = vres;
	state->fmt = (0x3 & rxMsamisc >> 1);
	rxMsabpc =((rxMsamisc >> 5) & XDP_RX_MSA_MISC0_MASK);
	state->bpc = bpc[rxMsabpc];
	state->detected_timings.type = V4L2_DV_BT_656_1120;
	state->detected_timings.bt.standards = V4L2_DV_BT_STD_CEA861;
	state->detected_timings.bt.flags = V4L2_DV_FL_IS_CE_VIDEO;	
	
	bt->interlaced = xdprxss_read(core, XDP_RX_MSA_MISC1) & XDP_MASK_INTERLACE ?
		V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
	bt->width = hres;
	bt->height = vres;
	bt->vsync = vres_total - vres;
	bt->hsync = hres_total - hres;
	bt->pixelclock = vres_total * hres_total * framerate;
	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
		bt->pixelclock /= 2;
	}
	
	state->framerate = framerate;
	dev_dbg(core->dev, "Stream width = %d height = %d framerate %d \n",
			hres,vres,framerate);
	
	return 0;

}
/**
 * xdprxss_get_stream_properties - Get DP Rx stream properties
 * @state: pointer to driver state
 *
 * This function decodes the stream  to get
 * stream properties like width, height,format, picture type (interlaced/progressive),
 * etc.
 *
 * Return: 0 for success else errors
 */
static int xdprxss_get_stream_properties(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;
	struct v4l2_mbus_framefmt *format = &state->formats[0];
	
	xdprxss_get_detected_timings(state);
	switch (state->fmt){
	case XDP_RX_COLOR_FORMAT_422:
		if(state->bpc == 10){
			format->code = MEDIA_BUS_FMT_UYVY10_1X20;
			dev_dbg(core->dev,"XVIDC_CSF_YCRCB_422 -> MEDIA_BUS_FMT_UYVY10_1X20\n");
		}else{
			format->code = MEDIA_BUS_FMT_UYVY8_1X16;
			dev_dbg(core->dev,"XVIDC_CSF_YCRCB_422 -> MEDIA_BUS_FMT_UYVY8_1X16\n");
		}
	break;
	case XDP_RX_COLOR_FORMAT_444:
		if(state->bpc == 10){
			format->code = MEDIA_BUS_FMT_VUY10_1X30;
			dev_dbg(core->dev,"XVIDC_CSF_YCRCB_444 -> MEDIA_BUS_FMT_VUY10_1X30\n");
		}else {
			format->code = MEDIA_BUS_FMT_VUY8_1X24;
			dev_dbg(core->dev,"XVIDC_CSF_YCRCB_444 -> MEDIA_BUS_FMT_VUY8_1X24\n");
		}
	break;
	case XDP_RX_COLOR_FORMAT_RGB:
		if(state->bpc == 10){
			format->code = MEDIA_BUS_FMT_RBG101010_1X30;
			dev_dbg(core->dev,"XVIDC_CSF_RGB -> MEDIA_BUS_FMT_RBG101010_1X30\n");
		}else {
			format->code = MEDIA_BUS_FMT_RBG888_1X24;
			dev_dbg(core->dev,"XVIDC_CSF_RGB -> ,MEDIA_BUS_FMT_RBG888_1X24\n");
		}
	break;
	default:

	dev_err(core->dev, "Unsupported color format \n");
	return -EINVAL;
	}

	format->width = state->width;
	format->height = state->height;
	
	state->detected_format.width =  format->width;
	state->detected_format.height = format->height;
	state->detected_format.code = format->code;
	state->detected_format.field = V4L2_FIELD_NONE;
	state->detected_format.colorspace = V4L2_COLORSPACE_SRGB; 
	state->detected_format.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	state->detected_format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	state->detected_format.quantization = V4L2_QUANTIZATION_DEFAULT;
	state->Valid_Stream = true;		
	
	dev_dbg(core->dev,"detected properties : width%d height %d framerate %d \n\r",
					state->width,state->height,state->framerate);
	return 0;
}
 /**
  * @core: pointer to driver core structure
  * @edid_ptr: pointer to the edid block
  * This function loads the edid data to EDID IP
  */
static void xdprxss_load_edid(struct xdprxss_core *core , u8 *edid_ptr)
{
	unsigned int Index,Offset;
	for (Index = 0 ; Index < (EDID_LENGTH) ; Index = Index + (16 * 4)) {
    		for (Offset = Index ; Offset < (Index + (16 * 4)) ; Offset = Offset + 4) {
		xedid_write(core, Offset, edid_ptr[(Index/4)+1]);
		}
	}

	for (Offset = 0 ; Offset < (EDID_LENGTH) ; Offset = Offset + 4) {
    		xedid_write(core, Offset, edid_ptr[Offset/4]);
  	}
}
 /**
  * This funtion configures the DP RX training paramerters 
  */
static void set_dplink_trining_params(struct xdprxss_core *core)
{

	/* Set voltage swing */	
	xdprxss_write(core,XDP_RX_MIN_VOLTAGE_SWING,
				XDP_RX_MIN_VOLTAGE_SWING_MASK);
	/* Set the AUX training interval */
	xdprxss_dpcd_write(core,XDP_RX_AUX_CLK_DIVIDER_REG,
			(xdprxss_read(core, XDP_RX_AUX_CLK_DIVIDER_REG) | 
			(XDP_RX_AUX_DEFER_COUNT << XDP_RX_AUX_DEFER_SHIFT)));

	/*Set Aux read interval as per the dp 1.4a */
	xdprxss_dpcd_write(core,XDP_RX_TP_SET_REG,
			((XDP_DPCD_TRAIN_AUX_RD_INT_16MS << 
			XDP_RX_SET_TRNG_AUX_RD_INTERVAL_SHIFT)| 
			XDP_RX_TRNG_SET_AUX_RD_INTERVAL_SET));

	/* Set the interrupt mask */
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_ALL_MASK);
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_1_REG,
				XDP_RX_INTERRUPT_ALL_MASK_1);	
	/*Enable the Rx*/
	xdprxss_enablelnk(core,Enable);

	/*Enable DTG*/
	xdprxss_set(core,XDP_RX_DTG_REG,Enable);

	/*Disable the Rx*/
	xdprxss_enablelnk(core,Disable);
	/* loading default EDID information */
	xdprxss_load_edid(core,(u8 *)&xilinx_edid[0]);	

	/* Disable All the Interrupts */
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_ALL_MASK);
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_1_REG,
				XDP_RX_INTERRUPT_ALL_MASK_1); 
	/* Enable Training related Interrupts */
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_ALL_MASK);
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_1_REG,
				XDP_RX_INTERRUPT_ALL_MASK_1);
	xdprxss_write(core,XDP_RX_AUX_CLK_DIVIDER_REG,
		(( xdprxss_read(core, XDP_RX_AUX_CLK_DIVIDER_REG) & 0xF0FF00FF) |
		(XDP_RX_AUX_DEFER_COUNT << XDP_RX_AUX_DEFER_SHIFT)));
	/* Set long value for Balnking start symbol idle time */
	xdprxss_write(core,XDP_RX_BS_IDLE_TIME,
				DP_BS_IDLE_TIMEOUT);
	/*Enable CRC support in DPRX */
	xdprxss_clr(core,XDP_RX_CRC_CONFIG_REG,XDP_RX_CRC_EN_MASK);

	/*Enable the Rx*/
	xdprxss_enablelnk(core,Enable);

}
  /**
   * This function initializes the DP subsystems 
   */
static void  xdprxss_core_init(struct xdprxss_core *core)
{
	u32 axi_clk;
	axi_clk = clk_get_rate(core->axi_clk);
	
	/*Set the max link rate and lane count for the first time*/
	xdprxss_set_linkrate(core,DPRXSS_LINK_RATE);
	xdprxss_set_lane_count(core,DPRXSS_LANE_COUNT_SET_REG);

	xdprxss_enablelnk(core,Disable);
	xdprxss_write(core,XDP_RX_AUX_CLK_DIVIDER_REG,(axi_clk/MHZ));
	xdprxss_write(core, XDP_RX_PHY_REG,XDP_RX_PHY_REG_GTPLL_RESET_MASK |
					XDP_RX_PHY_REG_GTRX_RESET_MASK);
	xdprxss_write(core,XDP_RX_PHY_REG,XDP_RX_PHY_REG_GTRX_RESET_MASK);
	xdprxss_write(core,XDP_RX_CDR_CONTROL_CONFIG,
			XDP_RX_CDR_CONTROL_TDLOCK_DP159);

	xdprxss_write(core,XDP_RX_PHY_REG,
				XDP_RX_PHY_REG_RESET_ENBL_MASK | 
				XDP_RX_PHY_REG_RESET_AT_TRAIN_ITER_MASK |
				XDP_RX_PHY_REG_RESET_AT_LINK_RATE_CHANGE_MASK | 
				XDP_RX_PHY_REG_RESET_AT_TP1_START_MASK);

	xdprxss_write(core,XDP_RX_MST_CAP_REG,Disable);	
	xdprxss_write(core,XDP_RX_LOCAL_EDID_VIDEO_REG,Enable);
	/* In SST mode sync count is 1 */
	xdprxss_write(core,XDP_RX_SINK_COUNT_REG,1);
	/*disable the the CRD tDCLOCK */
	xdprxss_clr(core,XDP_RX_CDR_CONTROL_CONFIG,
				XDP_RX_CDR_CONTROL_DISABLE_TIMEOUT);
	
	/*set display port rx traning paramerters */
	set_dplink_trining_params(core); 
}

static void xdprxss_irq_unplug(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;

	dev_dbg(core->dev,"Asserted cable unplug interrupt \n");
	printk("Asserted cable unplug interrupt \n");
	
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_REG,
			XDP_RX_INTERRUPT_UNPLUG_MASK);
	/*Generate HPD interrupt*/	
	xdprxss_generate_hpd(core,XDP_RX_HPD_PLUSE_DURATION_750);
	/*Reset the video logic . Need for some type c connectors*/
	xdprxss_soft_reset(core,XDP_RX_SOFT_RESET_REG,
			XDP_RX_VIDEO_SOFT_RESET_MASK);
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_ALL_MASK);
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_1_REG,
			XDP_RX_INTERRUPT_ALL_MASK_1);
	/*Enable training related interrupts*/ 
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_REG,
			XDP_RX_TRAINING_INTERRUPT_MASK);
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_1_REG,
			XDP_RX_TRAINING_INTERRUPT_MASK_1);
	/*Generate a HPD of 5ms */
	xdprxss_generate_hpd(core,XDP_RX_HPD_PLUSE_DURATION_5000);

}
static void xdprxss_irq_tp1(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;
	u32 linkrate;

	dev_dbg(core->dev,"Asserted traning pattern 1  \n");
	
	linkrate = xdprxss_read(core,XDP_LINK_BW_SET_REG);
	DpRxSs_LinkBandwidthHandler(linkrate);
	/* Initialize phy logic of DP-RX core */
	xdprxss_write(core,XDP_RX_PHY_REG,
	                        XDP_RX_PHY_REG_INIT_MASK);
	DpRxSs_PllResetHandler();
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_ALL_MASK);
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_REG,
					XDP_RX_INTERRUPT_UNPLUG_MASK);
}

static void xdprxss_irq_training_lost(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;

	dev_dbg(core->dev,"Traning Lost !!  \n");
	state->Valid_Stream = false;
	
	xdprxss_generate_hpd(core,XDP_RX_HPD_PLUSE_DURATION_750);
	xdprxss_soft_reset(core,XDP_RX_SOFT_RESET_REG,
				XDP_RX_AUX_SOFT_RESET_MASK);

  }

static void xdprxss_irq_no_video(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;	
	
	dev_dbg(core->dev,"No Valid video received !! \n");
	state->Valid_Stream = false;
	
	xdprxss_write(core,XDP_RX_VIDEO_UNSUPPORTED_REG, Enable);
	xdprxss_enableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_VBLANK_MASK);
	xdprxss_disableintr(core,XDP_RX_INTERRUPT_MASK_REG,
				XDP_RX_INTERRUPT_NO_VIDEO_MASK);
	xdprxss_dtg_reset(core);
	xdprxss_soft_reset(core,XDP_RX_SOFT_RESET_REG,
				XDP_RX_VIDEO_SOFT_RESET_MASK);
}
static void xdprxss_irq_power_state(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;
	dev_dbg(core->dev,"Cable power state changed  !! \n");
	
	if(xdprxss_power_preset(core))
		state->cable_connected = true;
	else 
		state->cable_connected = false;
}

static void xdprxss_irq_valid_video(struct xdprxss_state *state){

	struct xdprxss_core *core = &state->core;
	
	dev_dbg(core->dev,"Valid Video received !!  \n");
	
	xdprxss_write(core,XDP_RX_VIDEO_UNSUPPORTED_REG, Disable);
	
	if(!xdprxss_get_stream_properties(state)){
		memset(&state->event, 0, sizeof(state->event));
		state->event.type = V4L2_EVENT_SOURCE_CHANGE;
		state->event.u.src_change.changes =
				V4L2_EVENT_SRC_CH_RESOLUTION;
		v4l2_subdev_notify_event(&state->subdev,&state->event);
		state->Valid_Stream = true;

	} else {

		dev_err(core->dev, "Unable to get stream properties!\n");
		state->Valid_Stream = false;
	}
}	

/**
 * xdprxss_irq_handler - Interrupt handler for DP Rx
 * @irq: IRQ number
 * @dev_id: Pointer to device state
 *
 * The DP Rx interrupts are cleared by first setting and then clearing the bits
 * in the interrupt clear register. The interrupt status register is read only.  *
 * Return: IRQ_HANDLED after handling interrupts
 */
static irqreturn_t xdprxss_irq_handler(int irq, void *dev_id)
{
	struct xdprxss_state *state = (struct xdprxss_state *)dev_id;
	struct xdprxss_core *core = &state->core;
	u32 status, status_1;

	status = xdprxss_read(core, XDP_RX_INTERRUPT_CAUSE_REG);
	status &= (~ xdprxss_read(core,XDP_RX_INTERRUPT_MASK_REG));

	status_1 = xdprxss_read(core, XDP_RX_INTERRUPT_CAUSE_1_REG);
	status_1 &=(~xdprxss_read(core,XDP_RX_INTERRUPT_MASK_1_REG));

	if (!status)
		return IRQ_NONE;

	if (status & XDP_RX_INTERRUPT_UNPLUG_MASK) 
		xdprxss_irq_unplug(state);

	else if (status & XDP_RX_INTERRUPT_TP1_MASK)
		xdprxss_irq_tp1(state);

	else if (status & XDP_RX_INTERRUPT_TRAINING_LOST_MASK) 
		xdprxss_irq_training_lost(state);

	else if (status & XDP_RX_INTERRUPT_TRAINING_DONE_MASK)
    		dev_dbg(core->dev,"DP Link training is done !!  \n");

	else if (status & XDP_RX_INTERRUPT_NO_VIDEO_MASK)
		xdprxss_irq_no_video(state);

	else if (status & XDP_RX_INTERRUPT_VIDEO_MASK) 
		xdprxss_irq_valid_video(state);

	else if (status & XDP_RX_INTERRUPT_POWER_STATE_MASK)
		xdprxss_irq_power_state(state);

	return IRQ_HANDLED;

}

/**
 * xdprxss_subscribe_event - Subscribe to video source change event
 * @sd: V4L2 Sub device
 * @fh: V4L2 File Handle
 * @sub: Subcribe event structure
 *
 * Return: 0 on success, errors otherwise
 */
static int xdprxss_subscribe_event(struct v4l2_subdev *sd,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	int rc;
  	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
  	struct xdprxss_core *core = &xdprxss->core;

	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		{
		rc = v4l2_src_change_event_subscribe(fh, sub);
		dev_dbg(core->dev,
		"xdprxss_subscribe_event(V4L2_EVENT_SOURCE_CHANGE) = %d\n", rc);
			return rc;
		}
	default:
		{
		dev_dbg(core->dev, "xdprxss_subscribe_event() default: -EINVAL\n");
			return -EINVAL;
		}	
	}
}
/**
 * xdprxss_unsubscribe_event - Unsubscribe from all events registered
 * @sd: V4L2 Sub device
 * @fh: V4L2 file handle
 * @sub: pointer to Event unsubscription structure
 *
 * Return: zero on success, else a negative error code.
 */
static int xdprxss_unsubscribe_event(struct v4l2_subdev *sd,
				     struct v4l2_fh *fh,
    				     struct v4l2_event_subscription *sub)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	struct xdprxss_core *core = &xdprxss->core;

	dev_dbg(core->dev, "Event unsubscribe : 0x%08x\n", sub->type);
	return v4l2_event_unsubscribe(fh, sub);
}

/**
 * xdprxss_s_stream - It is used to start/stop the streaming.
 * @sd: V4L2 Sub device
 * @enable: Flag (True / False)
 *
 * This function controls the start or stop of streaming for the
 * Xilinx DP Rx Subsystem.
 *
 * Return: 0 on success, errors otherwise
 */
static int xdprxss_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	struct xdprxss_core *core = &xdprxss->core;
	/* DP does not need to be enabled when we start streaming */
	if (enable) {
    		if(!xdprxss->Valid_Stream){
			dev_dbg(core->dev, "No Valid Stream Received \n");
			return -EINVAL;
		}
		if (xdprxss->streaming) { 
			dev_dbg(core->dev, "Already streaming\n");
			return -EINVAL;
		}

	xdprxss->streaming = true;
	dev_dbg(core->dev, "Streaming started\n");
	
	} else {
		if (!xdprxss->streaming) {
			dev_dbg(core->dev, "Stopped streaming already\n");
			return -EINVAL;
    		}
	xdprxss->streaming = false;
	dev_dbg(core->dev, "Streaming stopped\n");
  	}

  	return 0;
}

static struct v4l2_mbus_framefmt *
__xdprxss_get_pad_format(struct xdprxss_state *xdprxss,
			 struct v4l2_subdev_pad_config *cfg,
			unsigned int pad, u32 which)
{
	struct xdprxss_core *core = &xdprxss->core;
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		dev_dbg(core->dev, 
			"%s: V4L2_SUBDEV_FORMAT_TRY\n",__func__);
      		return v4l2_subdev_get_try_format(&xdprxss->subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		dev_dbg(core->dev,
			"%s: V4L2_SUBDEV_FORMAT_ACTIVE\n",__func__);
		return &xdprxss->formats[pad];
	default:
      		return NULL;
	}
}

/**
 * xdprxss_get_format - Get the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to get the pad format information.
 *
 * Return: 0 on success
 */
static int xdprxss_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);

	fmt->format = *__xdprxss_get_pad_format(xdprxss, cfg,
						fmt->pad, fmt->which);
	dev_dbg(xdprxss->core.dev, "Stream width = %d height = %d Field = %d\n",
      			fmt->format.width, fmt->format.height, fmt->format.field);

	return 0;
}

/**
 * xdprxss_set_format - This is used to set the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to set the pad format.
 * Since the pad format is fixed in hardware, it can't be
 * modified on run time.
 *
 * Return: 0 on success
 */

/* we must modify the requested format to match what the hardware can provide */
static int xdprxss_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *__format;
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);

	dev_dbg(xdprxss->core.dev,
      		"set width %d height %d code %d field %d colorspace %d\n",
	fmt->format.width, fmt->format.height,
	fmt->format.code, fmt->format.field,
	fmt->format.colorspace);

	mutex_lock(&xdprxss->core.lock);
	__format = __xdprxss_get_pad_format(xdprxss, cfg,
						fmt->pad, fmt->which);
  	fmt->format = *__format;
	mutex_unlock(&xdprxss->core.lock);
  	return 0;
}


/**
 * xdprxss_open - Called on v4l2_open()
 * @sd: Pointer to V4L2 sub device structure
 * @fh: Pointer to V4L2 File handle
 *
 * This function is called on v4l2_open(). It sets the default format for pad.
 *
 * Return: 0 on success
 */
static int xdprxss_open(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);

	format = v4l2_subdev_get_try_format(sd, fh->pad, 0);
	*format = xdprxss->default_format;
  	return 0;
}

/**
 * xdprxss_close - Called on v4l2_close()
 * @sd: Pointer to V4L2 sub device structure
 * @fh: Pointer to V4L2 File handle
 *
 * This function is called on v4l2_close().
 *
 * Return: 0 on success
 */
static int xdprxss_close(struct v4l2_subdev *sd,
			 struct v4l2_subdev_fh *fh)
{

	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	dev_dbg(xdprxss->core.dev,"xdprxss_close\n");

	return 0;
}
static int xdprxss_enum_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
			&xdprxss_dv_timings_cap, NULL, NULL);
}
/**
 * xdprxss_get_dv_timings_cap - This is used to set the dv timing capabilities 
 * @subdev: Pointer to V4L2 Sub device structure
 * @cap: Pointer to dv timing capability structure
 *
 * Return: 0 on success
 */
static int xdprxss_get_dv_timings_cap(struct v4l2_subdev *subdev,
				struct v4l2_dv_timings_cap *cap)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(subdev);
	
	if (cap->pad != 0)
		return -EINVAL;
	mutex_lock(&xdprxss->lock);
	*cap = xdprxss_dv_timings_cap;
	mutex_unlock(&xdprxss->lock);
	return 0;
}

static int xdprxss_query_dv_timings(struct v4l2_subdev *sd,
					struct v4l2_dv_timings *timings)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	
	if (!timings)
		return -EINVAL;
	mutex_lock(&xdprxss->lock);
	if (!xdprxss->Valid_Stream) {
		pr_info("Stream is not up \n");
		mutex_unlock(&xdprxss->lock);
    		return -ENOLINK;
	}
	
	xdprxss_get_detected_timings(xdprxss);
	
	/* copy detected timings into destination */
	*timings = xdprxss->detected_timings;
	mutex_unlock(&xdprxss->lock);
	return 0;
}

static int xdprxss_get_edid(struct v4l2_subdev *subdev, struct v4l2_edid *edid) {

	struct xdprxss_state *xdprxss = to_xdprxssstate(subdev);
	int do_copy = 1;
	if (edid->pad > 0)
		return -EINVAL;
	if (edid->start_block != 0)
		return -EINVAL;
	/* caller is only interested in the size of the EDID? */
	if ((edid->start_block == 0) && (edid->blocks == 0)) do_copy = 0;
        mutex_lock(&xdprxss->lock);
        /* user EDID active? */
	if (xdprxss->edid_user_blocks) {
		if (do_copy)
			memcpy(edid->edid, xdprxss->edid_user, 128 * xdprxss->edid_user_blocks);
		edid->blocks = xdprxss->edid_user_blocks;
	} else {
		if (do_copy)
			memcpy(edid->edid, &xilinx_edid[0], sizeof(xilinx_edid));
		edid->blocks = sizeof(xilinx_edid) / 128;
	}
	mutex_unlock(&xdprxss->lock);
        return 0;
}
static void xdprxss_sethpd(struct xdprxss_core *core,int setclr){

	xdprxss_write(core,XDP_RX_LINK_ENABLE_REG,setclr);

}
static void xdprxss_delayed_work_enable_hotplug(struct work_struct *work)
{
        struct delayed_work *dwork = to_delayed_work(work);
        struct xdprxss_state *xdprxss = container_of(dwork, struct xdprxss_state,
                                                delayed_work_enable_hotplug);

	struct xdprxss_core *core = &xdprxss->core;
	/* toggle source device with HPD singnal */
        xdprxss_sethpd(core,1);
}

static int xdprxss_set_edid(struct v4l2_subdev *subdev, struct v4l2_edid *edid) {
	
	struct xdprxss_state *xdprxss = to_xdprxssstate(subdev);
	struct xdprxss_core *core = &xdprxss->core;
	
	if (edid->pad > 0)
	        return -EINVAL;
	if (edid->start_block != 0)
	        return -EINVAL;
	if (edid->blocks > xdprxss->edid_blocks_max) {
	        /* notify caller of how many EDID blocks this driver supports */
	        edid->blocks = xdprxss->edid_blocks_max;
	        return -E2BIG;
	}
	mutex_lock(&xdprxss->lock);
	xdprxss->edid_user_blocks = edid->blocks;
	
	/* Disable hotplug and I2C access to EDID RAM from DDC port */
	cancel_delayed_work_sync(&xdprxss->delayed_work_enable_hotplug);
	/* Disables the main link */
	xdprxss_sethpd(core,0);
	
	if (edid->blocks) {
	        memcpy(xdprxss->edid_user, edid->edid, 128 * edid->blocks);
	               xdprxss_load_edid(core, (u8 *)&xdprxss->edid_user);
	                /* enable hotplug after 100 ms */
	                queue_delayed_work(xdprxss->work_queue,
	                        &xdprxss->delayed_work_enable_hotplug, HZ / 10);
	}
	mutex_unlock(&xdprxss->lock);
	return 0;
}
 

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xdprxss_media_ops = {
	.link_validate = v4l2_subdev_link_validate
};
 
static const struct v4l2_subdev_core_ops xdprxss_core_ops = {
  	.subscribe_event = xdprxss_subscribe_event,
  	.unsubscribe_event = xdprxss_unsubscribe_event
};

static struct v4l2_subdev_video_ops xdprxss_video_ops = {
	.query_dv_timings = xdprxss_query_dv_timings,
  	.s_stream = xdprxss_s_stream,
};

static struct v4l2_subdev_pad_ops xdprxss_pad_ops = {
	.get_fmt 		= xdprxss_get_format,
	.set_fmt		= xdprxss_set_format,
	.get_edid		= xdprxss_get_edid,
	.set_edid		= xdprxss_set_edid,
	.enum_dv_timings 	= xdprxss_enum_dv_timings,
  	.dv_timings_cap         = xdprxss_get_dv_timings_cap,
};

static struct v4l2_subdev_ops xdprxss_ops = {
  	.core = &xdprxss_core_ops,
  	.video = &xdprxss_video_ops,
  	.pad = &xdprxss_pad_ops
};

static const struct v4l2_subdev_internal_ops xdprxss_internal_ops = {
  	.open = xdprxss_open,
	.close = xdprxss_close
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

/*This function parse the EDID block DT node */
void __iomem *xdprxss_parse_vid_edid_dt(struct device *dev)
{
	struct device_node *vid_edid_node;
	struct resource res;
	void __iomem *vid_edid_base;
	int rc;
	struct device_node *node = dev->of_node;

	vid_edid_node = of_parse_phandle(node, "xlnx,xlnx-vid-edid", 0);
	if (!vid_edid_node) {
		dev_err(dev, "vid_edid_node not found\n");
		vid_edid_base= NULL;
	} else {
		rc = of_address_to_resource(vid_edid_node, 0, &res);
		if (rc) {
			dev_err(dev, "vid_edid_node:addr to resource failed\n");
			vid_edid_base = NULL;
		} else {
			vid_edid_base = devm_ioremap_resource(dev, &res);
			if (IS_ERR(vid_edid_base)) {
				dev_err(dev, "vid_edid_base ioremap failed\n");
				vid_edid_base = NULL;
			} 
		}
	of_node_put(vid_edid_node);
	}
	return vid_edid_base;
}
/*This function parese the device -tree data */
static int xdprxss_parse_of(struct xdprxss_state *xdprxss)
{
	struct device_node *node = xdprxss->core.dev->of_node;
	struct device_node *ports = NULL;
	struct device_node *port = NULL;
	unsigned int nports = 0;
	struct xdprxss_core *core = &xdprxss->core;
	struct device *dev = xdprxss->core.dev;
	const struct xvip_video_format *format;
	struct device_node *endpoint;
	int ret;
	
	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;
	for_each_child_of_node(ports, port) {
		if (!port->name || of_node_cmp(port->name, "port"))
			continue;
		format = xvip_of_get_format(port);
		if (IS_ERR(format)) {
			dev_err(core->dev, "invalid format in DT");
			return PTR_ERR(format);
		}
		dev_dbg(core->dev, "vf_code = %d bpc = %d bpp = %d\n",
		format->vf_code, format->width, format->bpp);

		if (format->vf_code != XVIP_VF_YUV_422 &&
			format->vf_code != XVIP_VF_YUV_420 && format->vf_code != XVIP_VF_RBG) {
			dev_err(core->dev, "Incorrect UG934 video format set.\n");
			return -EINVAL;
		}
		xdprxss->vip_format = format;
		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(core->dev, "No port at\n");
			return -EINVAL;
}

		/* Count the number of ports. */
		nports++;
	}

	if (nports != 1) {
		dev_err(core->dev, "invalid number of ports %u\n", nports);
		return -EINVAL;
	}
	core->vid_edid_base = xdprxss_parse_vid_edid_dt(dev);
	if (!core->vid_edid_base) {
    		dev_err(dev, " Video EDID  not found!\n");
  	}
	/* Register interrupt handler */
	core->irq = irq_of_parse_and_map(node, 0);

	ret = devm_request_irq(core->dev, core->irq, xdprxss_irq_handler,
      		IRQF_SHARED, "xilinx-dprxss", xdprxss);
	if (ret) {
    		dev_err(core->dev, "Err = %d Interrupt handler reg failed!\n",ret);
	return ret;
	}

	return 0;
}

static int xdprxss_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
  	struct xdprxss_state *xdprxss;
	struct xdprxss_core *core;
  	struct resource *res;
	struct device_node *node;
  	int ret;

	xdprxss = devm_kzalloc(&pdev->dev, sizeof(*xdprxss), GFP_KERNEL);
  	if (!xdprxss)
    		return -ENOMEM;

	xdprxss->core.dev = &pdev->dev;
	node = xdprxss->core.dev->of_node;
	core = &xdprxss->core;

	core->axi_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(core->axi_clk)) {
		ret = PTR_ERR(core->axi_clk);
		dev_err(&pdev->dev, "failed to get s_axi_clk (%d)\n", ret);
    		return ret;
	}

	core->rx_lnk_clk = devm_clk_get(&pdev->dev, "rx_lnk_clk");
  	if (IS_ERR(core->rx_lnk_clk)) {
    		ret = PTR_ERR(core->rx_lnk_clk);
    		dev_err(&pdev->dev, "failed to get rx_lnk_clk (%d)\n", ret);
    		return ret;
	}

	core->rx_vid_clk = devm_clk_get(&pdev->dev, "rx_vid_clk");
  	if (IS_ERR(core->rx_vid_clk)) {
    		ret = PTR_ERR(core->rx_vid_clk);
    		dev_err(&pdev->dev, "failed to get rx_vid_clk (%d)\n", ret);
    		return ret;
  	}

	ret = clk_prepare_enable(core->axi_clk);
	if (ret) {
    		dev_err(&pdev->dev, "failed to enable axi_clk (%d)\n", ret);
    		return ret;
  	}

	ret = clk_prepare_enable(core->rx_lnk_clk);
	if (ret) {
    		dev_err(&pdev->dev, "failed to enable rx_lnk_clk (%d)\n", ret);
    		goto rx_lnk_clk_err;
  	}

	ret = clk_prepare_enable(core->rx_vid_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable rx_vid_clk (%d)\n", ret);
		goto rx_vid_clk_err;
	}

	
	xdprxss->edid_blocks_max = 2;
	INIT_DELAYED_WORK(&xdprxss->delayed_work_enable_hotplug,
					xdprxss_delayed_work_enable_hotplug);

	ret = xdprxss_parse_of(xdprxss);
	if (ret < 0)
    		goto clk_err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdprxss->core.iomem = devm_ioremap_resource(xdprxss->core.dev, res);
	if (IS_ERR(xdprxss->core.iomem)) {
		ret = PTR_ERR(xdprxss->core.iomem);
		goto clk_err;
	}

	/*Initialize the DP core*/	
	xdprxss_core_init(core);

	/* Initialize V4L2 subdevice and media entity */
	xdprxss->pads[XDPRX_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize the default format */
	xdprxss->default_format.code = xdprxss->vip_format->code;
	xdprxss->default_format.field = V4L2_FIELD_NONE;
	xdprxss->default_format.colorspace = V4L2_COLORSPACE_DEFAULT;
	xdprxss->default_format.width = XDPRX_DEFAULT_WIDTH;
	xdprxss->default_format.height = XDPRX_DEFAULT_HEIGHT;
	xdprxss->formats[0] = xdprxss->default_format;
	/* Initialize V4L2 subdevice and media entity */
	subdev = &xdprxss->subdev;
	v4l2_subdev_init(subdev, &xdprxss_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xdprxss_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));

	subdev->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &xdprxss_media_ops;
	v4l2_set_subdevdata(subdev, xdprxss);

	ret = media_entity_pads_init(&subdev->entity, 1, xdprxss->pads);	
		if (ret < 0)
    		goto error;

	/* Initialise and register the controls */
	v4l2_ctrl_handler_init(&xdprxss->ctrl_handler,0);

	xdprxss->streaming = false;
	subdev->ctrl_handler = &xdprxss->ctrl_handler;
	ret = v4l2_ctrl_handler_setup(&xdprxss->ctrl_handler);
	if (ret < 0) {
    		dev_err(&pdev->dev, "failed to set controls\n");
    		goto error;
  	}

	platform_set_drvdata(pdev, xdprxss);

	/* assume detected format */
	xdprxss->detected_format.width = 1280;
	xdprxss->detected_format.height = 720;
  	xdprxss->detected_format.field = V4L2_FIELD_NONE;
	xdprxss->detected_format.colorspace = V4L2_COLORSPACE_REC709;
  	xdprxss->detected_format.code = MEDIA_BUS_FMT_RBG888_1X24;
  	xdprxss->detected_format.colorspace = V4L2_COLORSPACE_SRGB;
  	xdprxss->detected_format.xfer_func = V4L2_XFER_FUNC_DEFAULT;
  	xdprxss->detected_format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
  	xdprxss->detected_format.quantization = V4L2_QUANTIZATION_DEFAULT;

  	ret = v4l2_async_register_subdev(subdev);
  	if (ret < 0) {
    		dev_err(&pdev->dev, "failed to register subdev\n");
    		goto error;
  	}

	xdprxss->Valid_Stream = false ;	
	dev_info(xdprxss->core.dev, "Xilinx DP Rx Subsystem device found!\n");

	return 0;
error:
	v4l2_ctrl_handler_free(&xdprxss->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

clk_err:
	clk_disable_unprepare(core->rx_vid_clk);
rx_vid_clk_err:
	clk_disable_unprepare(core->rx_lnk_clk);
rx_lnk_clk_err:
	clk_disable_unprepare(core->axi_clk);
	return ret;
}

static int xdprxss_remove(struct platform_device *pdev)
{
	struct xdprxss_state *xdprxss = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xdprxss->subdev;
	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xdprxss->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	clk_disable_unprepare(xdprxss->core.rx_vid_clk);
	clk_disable_unprepare(xdprxss->core.rx_lnk_clk);
	clk_disable_unprepare(xdprxss->core.axi_clk);
	kfree(xdprxss);
	return 0;
}

static const struct of_device_id xdprxss_of_id_table[] = {
  { .compatible = "xlnx,v-dp-rxss1-2.1"}, 
  { }
};
MODULE_DEVICE_TABLE(of, xdprxss_of_id_table);

static struct platform_driver xdprxss_driver = {
  .driver = {
    .name		= "xilinx-dprxss",
    .of_match_table	= xdprxss_of_id_table,
  },
  .probe			= xdprxss_probe,
  .remove			= xdprxss_remove,
};

module_platform_driver(xdprxss_driver);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("Xilinx DP Rx Subsystem Driver");
MODULE_LICENSE("GPL v2");
