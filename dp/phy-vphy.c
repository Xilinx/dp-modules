/*
 * Xilinx VPHY driver
 *
 * The Video Phy is a high-level wrapper around the GT to configure it
 * for video applications. The driver also provides common functionality
 * for its tightly-bound video protocol drivers such as HDMI RX/TX.
 *
 * Copyright (C) 2016, 2017 Leon Woestenberg <leon@sidebranch.com>
 * Copyright (C) 2015, 2020 Xilinx, Inc.
 *
 * Authors: Leon Woestenberg <leon@sidebranch.com>
 *          Rohit Consul <rohitco@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* if both both DEBUG and DEBUG_TRACE are defined, trace_printk() is used */
#define DEBUG
#define DEBUG_TRACE

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <dt-bindings/phy/phy.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "linux/phy/phy-vphy.h"

/* baseline driver includes */
#include "phy-xilinx-vphy/xvphy.h"
#include "phy-xilinx-vphy/xvphy_i.h"

/* common RX/TX */
#include "phy-xilinx-vphy/xdebug.h"
#include "phy-xilinx-vphy/xvidc.h"
#include "phy-xilinx-vphy/xvidc_edid.h"

#define XPAR_XDP_0_DEVICE_ID XPAR_DP_RX_HIER_0_V_DP_RXSS1_0_DP_DEVICE_ID
/* 0x000: LINK_BW_SET */
#define XDP_TX_LINK_BW_SET_162GBPS	0x06	/**< 1.62 Gbps link rate. */
#define XDP_TX_LINK_BW_SET_270GBPS	0x0A	/**< 2.70 Gbps link rate. */
#define XDP_TX_LINK_BW_SET_540GBPS	0x14	/**< 5.40 Gbps link rate. */
#define XDP_TX_LINK_BW_SET_810GBPS	0x1E	/**< 8.10 Gbps link rate. */
 /* VPHY Specific Defines
 */
#define XVPHY_RX_SYM_ERR_CNTR_CH1_2_REG    0x084
#define XVPHY_RX_SYM_ERR_CNTR_CH3_4_REG    0x088

#define XVPHY_DRP_CPLL_FBDIV            0x28
#define XVPHY_DRP_CPLL_REFCLK_DIV       0x2A
#define XVPHY_DRP_RXOUT_DIV             0x63
#define XVPHY_DRP_RXCLK25               0x6D
#define XVPHY_DRP_TXCLK25               0x7A
#define XVPHY_DRP_TXOUT_DIV             0x7C
#define XVPHY_DRP_RX_DATA_WIDTH         0x03
#define XVPHY_DRP_RX_INT_DATA_WIDTH     0x66
#define XVPHY_DRP_GTHE4_PRBS_ERR_CNTR_LOWER 0x25E
#define XVPHY_DRP_GTHE4_PRBS_ERR_CNTR_UPPER 0x25F
#define TX_DATA_WIDTH_REG 		0x7A
#define TX_INT_DATAWIDTH_REG 		0x85

#define XVPHY_GTHE4_DIFF_SWING_DP_V0P0 0x1
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P1 0x2
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P2 0x5
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P3 0xB

#define XVPHY_GTHE4_DIFF_SWING_DP_V1P0 0x2
#define XVPHY_GTHE4_DIFF_SWING_DP_V1P1 0x5
#define XVPHY_GTHE4_DIFF_SWING_DP_V1P2 0x7

#define XVPHY_GTHE4_DIFF_SWING_DP_V2P0 0x4
#define XVPHY_GTHE4_DIFF_SWING_DP_V2P1 0x7

#define XVPHY_GTHE4_DIFF_SWING_DP_V3P0 0x8

#define XVPHY_GTHE4_PREEMP_DP_L0    0x3
#define XVPHY_GTHE4_PREEMP_DP_L1    0xD
#define XVPHY_GTHE4_PREEMP_DP_L2    0x16
#define XVPHY_GTHE4_PREEMP_DP_L3    0x1D

#define XPAR_XDP_0_GT_DATAWIDTH 2
#define XVPHY_DRP_REF_CLK_HZ	40000000
#define SET_RX_TO_2BYTE		\
		(XPAR_XDP_0_GT_DATAWIDTH/2)
/* select either trace or printk logging */
#ifdef DEBUG_TRACE
#define do_hdmi_dbg(format, ...) do { \
  trace_printk("xlnx-hdmi-vphy: " format, ##__VA_ARGS__); \
} while(0)
#else
#define do_hdmi_dbg(format, ...) do { \
  printk(KERN_DEBUG "xlnx-hdmi-vphy: " format, ##__VA_ARGS__); \
} while(0)
#endif

/* either enable or disable debugging */
#ifdef DEBUG
#  define hdmi_dbg(x...) do_hdmi_dbg(x)
#else
#  define hdmi_dbg(x...)
#endif

#define hdmi_mutex_lock(x) mutex_lock(x)
#define hdmi_mutex_unlock(x) mutex_unlock(x)

static void xvphy_pe_vs_adjust_handler(XVphy *InstancePtr,
					struct phy_configure_opts_dp *dp);
typedef enum {
        ONBOARD_REF_CLK = 1,
        DP159_FORWARDED_CLK = 3,
} XVphy_User_GT_RefClk_Src;

typedef struct {
        u8 Index;
        XVphy_PllType  TxPLL;
        XVphy_PllType  RxPLL;
        XVphy_ChannelId TxChId;
        XVphy_ChannelId RxChId;
        u32 LineRate;
        u64 LineRateHz;
        XVphy_User_GT_RefClk_Src QPLLRefClkSrc;
        XVphy_User_GT_RefClk_Src CPLLRefClkSrc;
        u64 QPLLRefClkFreqHz;
        u64 CPLLRefClkFreqHz;
} XVphy_User_Config;


/**
 * struct xvphy_lane - representation of a lane
 * @phy: pointer to the kernel PHY device
 *
 * @type: controller which uses this lane
 * @lane: lane number
 * @protocol: protocol in which the lane operates
 * @ref_clk: enum of allowed ref clock rates for this lane PLL
 * @pll_lock: PLL status
 * @data: pointer to hold private data
 * @direction: 0=rx, 1=tx
 * @share_laneclk: lane number of the clock to be shared
 */
struct xvphy_lane {
	struct phy *phy;
	u8 type;
	u8 lane;
	u8 protocol;
	bool pll_lock;
	/* data is pointer to parent xvphy_dev */
	void *data;
	bool direction_tx;
	u32 share_laneclk;
};

struct xvphy_dev *vphydev_g;
struct xvphy_dev *vphydev;
static XVphy_User_Config PHY_User_Config_Table[] =
{
/* Index,         TxPLL,               RxPLL,
 * TxChId,         RxChId,
 * LineRate,         LineRateHz,
 * QPLLRefClkSrc,          CPLLRefClkSrc,    QPLLRefClkFreqHz,CPLLRefClkFreqHz
 * */
  {   0,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
          1620,    XVPHY_DP_LINK_RATE_HZ_162GBPS,
		  ONBOARD_REF_CLK,    ONBOARD_REF_CLK,     270000000,270000000},
  {   1,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
          2700,    XVPHY_DP_LINK_RATE_HZ_270GBPS,
		  ONBOARD_REF_CLK,    ONBOARD_REF_CLK,     270000000,270000000},
  {   2,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
          5400,    XVPHY_DP_LINK_RATE_HZ_540GBPS,
		  ONBOARD_REF_CLK,    ONBOARD_REF_CLK,     270000000,270000000},
  {   3,     XVPHY_PLL_TYPE_QPLL1,  XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CMN1,    XVPHY_CHANNEL_ID_CHA,
          1620,    XVPHY_DP_LINK_RATE_HZ_162GBPS,
          ONBOARD_REF_CLK,        ONBOARD_REF_CLK,     270000000,270000000},
  {   4,     XVPHY_PLL_TYPE_QPLL1,  XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CMN1,    XVPHY_CHANNEL_ID_CHA,
          2700,    XVPHY_DP_LINK_RATE_HZ_270GBPS,
          ONBOARD_REF_CLK,        ONBOARD_REF_CLK,     270000000,270000000},
  {   5,     XVPHY_PLL_TYPE_QPLL1,  XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CMN1,    XVPHY_CHANNEL_ID_CHA,
          5400,    XVPHY_DP_LINK_RATE_HZ_540GBPS,
          ONBOARD_REF_CLK,        ONBOARD_REF_CLK,     270000000,270000000},
  {   6,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
          1620,    XVPHY_DP_LINK_RATE_HZ_162GBPS,
          ONBOARD_REF_CLK,        ONBOARD_REF_CLK,         270000000,270000000},
  {   7,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
          2700,    XVPHY_DP_LINK_RATE_HZ_270GBPS,
          ONBOARD_REF_CLK,        ONBOARD_REF_CLK,         270000000,270000000},
  {   8,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
          XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
          5400,    XVPHY_DP_LINK_RATE_HZ_540GBPS,
          ONBOARD_REF_CLK,        ONBOARD_REF_CLK,         270000000,270000000},
  {   9,     XVPHY_PLL_TYPE_CPLL,   XVPHY_PLL_TYPE_CPLL,
		  XVPHY_CHANNEL_ID_CHA,     XVPHY_CHANNEL_ID_CHA,
		  8100,    XVPHY_DP_LINK_RATE_HZ_810GBPS,
		  ONBOARD_REF_CLK,        ONBOARD_REF_CLK,         270000000,270000000},
 {   10,     XVPHY_PLL_TYPE_QPLL1,  XVPHY_PLL_TYPE_CPLL,
		  XVPHY_CHANNEL_ID_CMN1,    XVPHY_CHANNEL_ID_CHA,
		  8100,    XVPHY_DP_LINK_RATE_HZ_810GBPS,
		  ONBOARD_REF_CLK,        ONBOARD_REF_CLK,     270000000,270000000},

};

struct xvphy_dev *register_dp_cb(void)
{
	return  vphydev_g;
}
EXPORT_SYMBOL(register_dp_cb);

/* given the (Linux) phy handle, return the xvphy */
XVphy *xvphy_get_xvphy(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	return &vphy_dev->xvphy;
}
EXPORT_SYMBOL_GPL(xvphy_get_xvphy);

/* given the (Linux) phy handle, enter critical section of xvphy baseline code
 * XVphy functions must be called with mutex acquired to prevent concurrent access
 * by XVphy and upper-layer video protocol drivers */
void xvphy_mutex_lock(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	hdmi_mutex_lock(&vphy_dev->xvphy_mutex);
}
EXPORT_SYMBOL_GPL(xvphy_mutex_lock);
/*****************************************************************************/
/**
*
* This function sets proper ref clk frequency and line rate
*
* @param    InstancePtr is a pointer to the Video PHY instance.
*
* @return    None.
*
* @note        None.
*
******************************************************************************/
void PLLRefClkSel (XVphy *InstancePtr, u32 link_rate) {

	switch (link_rate) {
	case 1620:
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
		ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
			//DP159_FORWARDED_CLK,
			ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				  XVPHY_DP_LINK_RATE_HZ_162GBPS);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CMN1,
				  XVPHY_DP_LINK_RATE_HZ_162GBPS);
		break;
	case 5400:
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
		ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
		//	DP159_FORWARDED_CLK,
		ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				  XVPHY_DP_LINK_RATE_HZ_540GBPS);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CMN1,
				  XVPHY_DP_LINK_RATE_HZ_540GBPS);
		break;
	case 8100:
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
		ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
	//	DP159_FORWARDED_CLK,
	ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				  XVPHY_DP_LINK_RATE_HZ_810GBPS);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CMN1,
				  XVPHY_DP_LINK_RATE_HZ_810GBPS);
		break;
	default:
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0, 
		ONBOARD_REF_CLK,
					XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgQuadRefClkFreq(InstancePtr, 0,
//					DP159_FORWARDED_CLK, XVPHY_DP_REF_CLK_FREQ_HZ_135);
					//DP159_FORWARDED_CLK, XVPHY_DP_REF_CLK_FREQ_HZ_270);
					ONBOARD_REF_CLK, XVPHY_DP_REF_CLK_FREQ_HZ_270);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				  XVPHY_DP_LINK_RATE_HZ_270GBPS);
		XVphy_CfgLineRate(InstancePtr, 0, XVPHY_CHANNEL_ID_CMN1,
				  XVPHY_DP_LINK_RATE_HZ_270GBPS);
		break;
	}
}
/*****************************************************************************/
/**
*
* This function is the callback function for when the link bandwidth change
* occurs.
*
* @param    InstancePtr is a pointer to the XDpRxSs instance.
*
* @return    None.
*
* @note        None.
*
******************************************************************************/
void DpRxSs_LinkBandwidthHandler(u32 linkrate)
{
//	dev_dbg(vphydev->dev,"  DpRxSs_LinkBandwidthHandler \n");
	/*Program Video PHY to requested line rate*/
	PLLRefClkSel (&vphydev->xvphy,linkrate);
	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			 XVPHY_DIR_RX,(TRUE));
	XVphy_PllInitialize(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			    ONBOARD_REF_CLK, ONBOARD_REF_CLK,
			    XVPHY_PLL_TYPE_QPLL1, XVPHY_PLL_TYPE_CPLL);
	XVphy_ClkInitialize(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			    XVPHY_DIR_RX);

}

/*****************************************************************************/
/**
*
* This function is the callback function for PLL reset request.
*
* @param    InstancePtr is a pointer to the XDpRxSs instance.
*
* @return    None.
*
* @note        None.
*
******************************************************************************/
void DpRxSs_PllResetHandler(void)
{
	/* Issue resets to Video PHY - This API
	 * called after line rate is programmed */
	XVphy_BufgGtReset(&vphydev->xvphy, XVPHY_DIR_RX,(TRUE));
	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			 XVPHY_DIR_RX,(TRUE));
	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			 XVPHY_DIR_RX, (FALSE));
	XVphy_BufgGtReset(&vphydev->xvphy, XVPHY_DIR_RX, (FALSE));
	hdmi_mutex_lock(&vphydev->xvphy_mutex);
	XVphy_WaitForResetDone(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			       XVPHY_DIR_RX);
	XVphy_WaitForPllLock(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA);
	hdmi_mutex_unlock(&vphydev->xvphy_mutex);
	
}
EXPORT_SYMBOL(DpRxSs_PllResetHandler);
/*****************************************************************************/
/**
 * *
 * * This function sets up PHY
 * *
 * * @param     pointer to VideoPHY
 * * @param     User Config table
 * *
 * * @return
 * *            - XST_SUCCESS if interrupt setup was successful.
 * *            - A specific error code defined in "xstatus.h" if an error
 * *            occurs.
 * *
 * * @note              None.
 * *
 * ******************************************************************************/

u32 PHY_Configuration_Tx(XVphy *InstancePtr, XVphy_User_Config PHY_User_Config_Table){

        XVphy_PllRefClkSelType QpllRefClkSel;
        XVphy_PllRefClkSelType CpllRefClkSel;
        XVphy_PllType TxPllSelect;
        XVphy_PllType RxPllSelect; // Required for VPHY setting
        XVphy_ChannelId TxChId;
        u8 QuadId = 0;
        u32 Status = XST_FAILURE;
        u32 retries = 0;

	QpllRefClkSel = PHY_User_Config_Table.QPLLRefClkSrc;
	CpllRefClkSel = PHY_User_Config_Table.CPLLRefClkSrc;

	TxPllSelect   = PHY_User_Config_Table.TxPLL;
	RxPllSelect   = PHY_User_Config_Table.RxPLL;
	TxChId        = PHY_User_Config_Table.TxChId;

        //Set the Ref Clock Frequency
	XVphy_CfgQuadRefClkFreq(InstancePtr, QuadId, QpllRefClkSel, PHY_User_Config_Table.QPLLRefClkFreqHz);
	XVphy_CfgQuadRefClkFreq(InstancePtr, QuadId, CpllRefClkSel, PHY_User_Config_Table.CPLLRefClkFreqHz);
	XVphy_CfgLineRate(InstancePtr, QuadId, TxChId, PHY_User_Config_Table.LineRateHz);

	XVphy_PllInitialize(InstancePtr, QuadId, TxChId, QpllRefClkSel, CpllRefClkSel, TxPllSelect, RxPllSelect);

        // Initialize GT with ref clock and PLL selects
        // GT DRPs may not get completed if GT is busy doing something else
        // hence this is run in loop and retried 100 times
        while (Status != XST_SUCCESS) {
                Status = XVphy_ClkInitialize(InstancePtr, QuadId, TxChId, XVPHY_DIR_TX);
                if (retries > 100) {
                        retries = 0;
                        printk("exhausted\r\n");
                        break;
                }
                retries++;
        }

	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_PLL_RESET_REG, 0x0);

	Status = XVphy_ResetGtPll(InstancePtr, QuadId,
				  XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_TX,(FALSE));
	hdmi_mutex_unlock(&vphydev->xvphy_mutex);
	Status += XVphy_WaitForPmaResetDone(InstancePtr, QuadId,
					    XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_TX);
	Status += XVphy_WaitForPllLock(InstancePtr, QuadId, TxChId);
	Status += XVphy_WaitForResetDone(InstancePtr, QuadId,
					 XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_TX);
	if (Status  != XST_SUCCESS) {
		printk ("++++TX GT config encountered error++++\r\n");
		printk("%d %s PLL Lock done failed: Status =%d\n",__LINE__,__func__,Status);
	}

	hdmi_mutex_unlock(&vphydev->xvphy_mutex);
	return (Status);
}
struct xvphy_dev *vphydev;

/*****************************************************************************/
/**
 * *
 * * This function sets VPHY based on the linerate
 * *
 * * @param     user_config_struct.
 * *
 * * @return    Status.
 * *
 * * @note              None.
 * *
 * ******************************************************************************/
u32 set_vphy(int LineRate_init_tx){


        u32 Status=0;

//	dev_dbg(vphydev->dev,"  set_vphy \n");
        switch(LineRate_init_tx){
                case 1620:
                        Status = PHY_Configuration_Tx(&vphydev->xvphy,
                                                PHY_User_Config_Table[(is_TX_CPLL)?0:3]);
                        break;

                case 2700:
                        Status = PHY_Configuration_Tx(&vphydev->xvphy,
                                                PHY_User_Config_Table[(is_TX_CPLL)?1:4]);
                        break;

                case 5400:
                        Status = PHY_Configuration_Tx(&vphydev->xvphy,
                                                PHY_User_Config_Table[(is_TX_CPLL)?2:5]);
                        break;

                case 8100:
                        Status = PHY_Configuration_Tx(&vphydev->xvphy,
                                                PHY_User_Config_Table[(is_TX_CPLL)?9:10]);
                        break;
        }

        if (Status != XST_SUCCESS) {
                printk ( "+++++++ vphy TX GT configuration encountered a failure +++++++ Sta     tus=%d\r\n",Status);
        }


        return Status;
}
EXPORT_SYMBOL_GPL(set_vphy);
unsigned int diff_swing[4][4] =
{
	{0x3, 0x6, 0x9, 0xf},
	{0x6, 0x9, 0xf, 0xf},
	{0x9, 0xf, 0xf, 0xf},
	{0xf, 0xf, 0xf, 0xf}
};
void xvphy_SetTxPreEmphasis(XVphy *InstancePtr, u32 chid, u8 pe)
{
	u32 regval;
	u32 maskval;
	u32 regoffset;

	if (chid == XVPHY_CHANNEL_ID_CH1 || chid == XVPHY_CHANNEL_ID_CH2)
		regoffset = XVPHY_TX_DRIVER_CH12_REG;
	else
		regoffset = XVPHY_TX_DRIVER_CH34_REG;

	regval = XVphy_ReadReg(InstancePtr->Config.BaseAddr, regoffset);
	maskval = XVPHY_TX_DRIVER_TXPRECURSOR_MASK(chid);
	regval &= ~maskval;
	regval |= (pe << XVPHY_TX_DRIVER_TXPRECURSOR_SHIFT(chid));
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, regoffset, regval);
}

/**
 * xlnx_dp_phy_set_tx_voltage_swing - Configure the vs values
 * @dp: DisplayPort IP core structure
 * @chid: channel index
 * @vs: vs value to be set
 *
 * This function sets the voltage swing value of the phy
 */

void xvphy_SetTxVoltageSwing (XVphy *InstancePtr, u32 chid, u8 vs)
{
	u32 regval;
	u32 maskval;
	u32 regoffset;

	if (chid == XVPHY_CHANNEL_ID_CH1 || chid == XVPHY_CHANNEL_ID_CH2)
		regoffset = XVPHY_TX_DRIVER_CH12_REG;
	else
		regoffset = XVPHY_TX_DRIVER_CH34_REG;

	regval = XVphy_ReadReg(InstancePtr->Config.BaseAddr, regoffset);
	maskval = XVPHY_TX_DRIVER_TXDIFFCTRL_MASK(chid);
	regval &= ~maskval;
	regval |= (vs << XVPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(chid));
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, regoffset, regval);
}


/**
 * xvphy__pe_vs_adjust_handler - Calculate and configure pe and vs values
 * @dp: DisplayPort IP core structure
 *
 * This function adjusts the pre emphasis and voltage swing values of phy.
 */

void xvphy_pe_vs_adjust_handler(XVphy *InstancePtr,
					struct phy_configure_opts_dp *dp)
{
	unsigned char preemp = 0, diff_swing = 0;

	switch (dp->pre[0]) {
	case 0:
		preemp = XVPHY_GTHE3_PREEMP_DP_L0; break;
	case 1:
		preemp = XVPHY_GTHE3_PREEMP_DP_L1; break;
	case 2:
		preemp = XVPHY_GTHE3_PREEMP_DP_L2; break;
	case 3:
		preemp = XVPHY_GTHE3_PREEMP_DP_L3; break;
	}

		xvphy_SetTxPreEmphasis(&vphydev->xvphy,XVPHY_CHANNEL_ID_CH1, preemp);
		xvphy_SetTxPreEmphasis(&vphydev->xvphy,XVPHY_CHANNEL_ID_CH2, preemp);
		xvphy_SetTxPreEmphasis(&vphydev->xvphy,XVPHY_CHANNEL_ID_CH3, preemp);
		xvphy_SetTxPreEmphasis(&vphydev->xvphy,XVPHY_CHANNEL_ID_CH4, preemp);

	switch (dp->voltage[0]) {
	case 0:
		switch (dp->pre[0]) {
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
		switch (dp->pre[0]) {
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
		switch (dp->pre[0]) {
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
		xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH1, diff_swing);
		xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH2, diff_swing);
		xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH3, diff_swing);
		xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH4, diff_swing);
}

void xvphy_mutex_unlock(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	hdmi_mutex_unlock(&vphy_dev->xvphy_mutex);
}
EXPORT_SYMBOL_GPL(xvphy_mutex_unlock);

/* XVphy functions must be called with mutex acquired to prevent concurrent access
 * by XVphy and upper-layer video protocol drivers */
EXPORT_SYMBOL_GPL(XVphy_GetPllType);
EXPORT_SYMBOL_GPL(XVphy_IBufDsEnable);

/* exclusively required by TX */
EXPORT_SYMBOL_GPL(XVphy_Clkout1OBufTdsEnable);
EXPORT_SYMBOL_GPL(XVphy_IsBonded);

static irqreturn_t xvphy_irq_handler(int irq, void *dev_id)
{
	struct xvphy_dev *vphydev;
	BUG_ON(!dev_id);
	vphydev = (struct xvphy_dev *)dev_id;
	BUG_ON(!vphydev);
	if (!vphydev)
		return IRQ_NONE;

	/* disable interrupts in the VPHY, they are re-enabled once serviced */
	XVphy_IntrDisable(&vphydev->xvphy, XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK |
			XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE |
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t xvphy_irq_thread(int irq, void *dev_id)
{
	struct xvphy_dev *vphydev;
	u32 IntrStatus;
	BUG_ON(!dev_id);
	vphydev = (struct xvphy_dev *)dev_id;
	BUG_ON(!vphydev);
	if (!vphydev)
		return IRQ_NONE;

	/* call baremetal interrupt handler with mutex locked */
	hdmi_mutex_lock(&vphydev->xvphy_mutex);

	IntrStatus = XVphy_ReadReg(vphydev->xvphy.Config.BaseAddr, XVPHY_INTR_STS_REG);
	dev_dbg(vphydev->dev,"XVphy IntrStatus = 0x%08x\n", IntrStatus);

	/* handle pending interrupts */
	XVphy_InterruptHandler(&vphydev->xvphy);
	hdmi_mutex_unlock(&vphydev->xvphy_mutex);

	/* enable interrupt requesting in the VPHY */
	XVphy_IntrEnable(&vphydev->xvphy, XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE |
		XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE |
		XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK |
		XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK |
		XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE |
		XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK |
		XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
		XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
		XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
		XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);

	return IRQ_HANDLED;
}

/**
 * xvphy_phy_init - initializes a lane
 * @phy: pointer to kernel PHY device
 *
 * Return: 0 on success or error on failure
 */
static int xvphy_phy_init(struct phy *phy)
{
	BUG_ON(!phy);
	return 0;
}
static int xvphy_phy_reset(struct phy *phy)
{
	BUG_ON(!phy);
	DpRxSs_PllResetHandler();

	return 0;
}
static int xvphy_phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	BUG_ON(!phy);
	if(opts->dp.set_rate && opts->dp.direction == PHY_RX_CFG) {
		DpRxSs_LinkBandwidthHandler(opts->dp.link_rate);
		opts->dp.set_rate = 0;
		opts->dp.direction = PHY_NONE;
	}
	if(opts->dp.set_rate && opts->dp.direction == PHY_TX_CFG) {
		set_vphy(opts->dp.link_rate);
		opts->dp.set_rate = 0;
		opts->dp.direction = PHY_NONE;
	}
	if(opts->dp.set_voltages) {
		xvphy_pe_vs_adjust_handler(&vphydev->xvphy, &opts->dp);
		opts->dp.set_voltages = 0;
	}

	return 0;
}
/**
 * xvphy_xlate - provides a PHY specific to a controller
 * @dev: pointer to device
 * @args: arguments from dts
 *
 * Return: pointer to kernel PHY device or error on failure
 *
 */
static struct phy *xvphy_xlate(struct device *dev,
				   struct of_phandle_args *args)
{
	struct xvphy_dev *vphydev = dev_get_drvdata(dev);
	struct xvphy_lane *vphy_lane = NULL;
	struct device_node *phynode = args->np;
	int index;
	u8 controller;
	u8 instance_num;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}
	if (!of_device_is_available(phynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}
	for (index = 0; index < of_get_child_count(dev->of_node); index++) {
		if (phynode == vphydev->lanes[index]->phy->dev.of_node) {
			vphy_lane = vphydev->lanes[index];
			break;
		}
	}
	if (!vphy_lane) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	/* get type of controller from lanes */
	controller = args->args[0];

	/* get controller instance number */
	instance_num = args->args[1];

	/* Check if lane sharing is required */
	vphy_lane->share_laneclk = args->args[2];

	/* get the direction for controller from lanes */
	vphy_lane->direction_tx = args->args[3];

	BUG_ON(!vphy_lane->phy);
	return vphy_lane->phy;

}

/* Local Global table for phy instance(s) configuration settings */
XVphy_Config XVphy_ConfigTable[XPAR_XVPHY_NUM_INSTANCES];

static struct phy_ops xvphy_phyops = {
	.configure	= xvphy_phy_configure,
	.reset		= xvphy_phy_reset,
	.init		= xvphy_phy_init,
	.owner		= THIS_MODULE,
};

static int instance = 0;
/* TX uses [1, 127], RX uses [128, 254] and VPHY uses [256, ...]. Note that 255 is used for not-present. */
#define VPHY_DEVICE_ID_BASE 256

static int vphy_parse_of(struct xvphy_dev *vphydev, XVphy_Config *c)
{
	struct device *dev = vphydev->dev;
	struct device_node *node = dev->of_node;
	int rc;
	u32 val;
	bool has_err_irq;

	rc = of_property_read_u32(node, "xlnx,transceiver-type", &val);
	if (rc < 0)
		goto error_dt;
	c->XcvrType = val;
	rc = of_property_read_u32(node, "xlnx,input-pixels-per-clock", &val);
	if (rc < 0)
		goto error_dt;
	c->Ppc = val;

	rc = of_property_read_u32(node, "xlnx,nidru", &val);
	if (rc < 0)
		goto error_dt;
	c->DruIsPresent = val;

	rc = of_property_read_u32(node, "xlnx,nidru-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	c->DruRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,rx-no-of-channels", &val);
	if (rc < 0)
		goto error_dt;
	c->RxChannels = val;

	rc = of_property_read_u32(node, "xlnx,tx-no-of-channels", &val);
	if (rc < 0)
		goto error_dt;
	c->TxChannels = val;

	rc = of_property_read_u32(node, "xlnx,rx-protocol", &val);
	if (rc < 0)
		goto error_dt;
	c->RxProtocol = val;

	rc = of_property_read_u32(node, "xlnx,tx-protocol", &val);
	if (rc < 0)
		goto error_dt;
	c->TxProtocol = val;

	rc = of_property_read_u32(node, "xlnx,rx-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	c->RxRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,tx-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	c->TxRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,rx-pll-selection", &val);
	if (rc < 0)
		goto error_dt;
	c->RxSysPllClkSel = val;

	rc = of_property_read_u32(node, "xlnx,tx-pll-selection", &val);
	if (rc < 0)
		goto error_dt;
	c->TxSysPllClkSel = val;

	rc = of_property_read_u32(node, "xlnx,hdmi-fast-switch", &val);
	if (rc < 0)
		goto error_dt;
	c->HdmiFastSwitch = val;

	has_err_irq = false;
	has_err_irq = of_property_read_bool(node, "xlnx,err-irq-en");
	c->ErrIrq = has_err_irq;

	c->xfmc_present =
		of_property_read_bool(node, "xlnx,xfmc-present");
	return 0;

error_dt:
	dev_err(vphydev->dev, "Error parsing device tree");
	return -EINVAL;
}


/*****************************************************************************/
/**
*
* This function sets GT in 16-bits (2-Byte) or 32-bits (4-Byte) mode.
*
* @param    InstancePtr is a pointer to the Video PHY instance.
*
* @return    None.
*
* @note        None.
*
******************************************************************************/
void PHY_Two_byte_set (XVphy *InstancePtr, u8 Rx_to_two_byte,
			u8 Tx_to_two_byte)
{
	u16 DrpVal;
	u16 WriteVal;

	if (Rx_to_two_byte == 1) {
		XVphy_DrpRd(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
				XVPHY_DRP_RX_DATA_WIDTH,&DrpVal);
		DrpVal &= ~0x1E0;
		WriteVal = 0x0;
		WriteVal = DrpVal | 0x60;
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
				XVPHY_DRP_RX_DATA_WIDTH, WriteVal);
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH2,
				XVPHY_DRP_RX_DATA_WIDTH, WriteVal);
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH3,
				XVPHY_DRP_RX_DATA_WIDTH, WriteVal);
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH4,
				XVPHY_DRP_RX_DATA_WIDTH, WriteVal);

		XVphy_DrpRd(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
				XVPHY_DRP_RX_INT_DATA_WIDTH,&DrpVal);
		DrpVal &= ~0x3;
		WriteVal = 0x0;
		WriteVal = DrpVal | 0x0;
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
				XVPHY_DRP_RX_INT_DATA_WIDTH, WriteVal);
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH2,
				XVPHY_DRP_RX_INT_DATA_WIDTH, WriteVal);
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH3,
				XVPHY_DRP_RX_INT_DATA_WIDTH, WriteVal);
		XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH4,
				XVPHY_DRP_RX_INT_DATA_WIDTH, WriteVal);
		xil_printf ("RX Channel configured for 2byte mode\r\n");
	}

	if (Tx_to_two_byte == 1) {
		u32 Status = XVphy_DrpRd(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
				TX_DATA_WIDTH_REG, &DrpVal);

		if (Status != XST_SUCCESS) {
			xil_printf("DRP access failed\r\n");
			return;
		}

		DrpVal &= ~0xF;
		WriteVal = 0x0;
		WriteVal = DrpVal | 0x3;
		Status  =XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
					TX_DATA_WIDTH_REG, WriteVal);
		Status +=XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH2,
					TX_DATA_WIDTH_REG, WriteVal);
		Status +=XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH3,
					TX_DATA_WIDTH_REG, WriteVal);
		Status +=XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH4,
					TX_DATA_WIDTH_REG, WriteVal);
		if(Status != XST_SUCCESS){
			xil_printf("DRP access failed\r\n");
			return;
		}

		Status = XVphy_DrpRd(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
					TX_INT_DATAWIDTH_REG, &DrpVal);
		if (Status != XST_SUCCESS) {
			xil_printf("DRP access failed\r\n");
			return;
		}

		DrpVal &= ~0xC00;
		WriteVal = 0x0;
		WriteVal = DrpVal | 0x0;
		Status  =XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1,
					TX_INT_DATAWIDTH_REG, WriteVal);
		Status +=XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH2,
					TX_INT_DATAWIDTH_REG, WriteVal);
		Status +=XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH3,
					TX_INT_DATAWIDTH_REG, WriteVal);
		Status +=XVphy_DrpWr(InstancePtr, 0, XVPHY_CHANNEL_ID_CH4,
					TX_INT_DATAWIDTH_REG, WriteVal);
		if (Status != XST_SUCCESS) {
			xil_printf("DRP access failed\r\n");
			return;
		}
		xil_printf ("TX Channel configured for 2byte mode\r\n");
	}
}

struct reg_8 {
        u16 addr;
        u8 val;
};

static const struct regmap_config fmc_regmap_config = {
        .reg_bits = 16,
        .val_bits = 8,
        .cache_type = REGCACHE_RBTREE,
};

//struct xvphy_dev *vphydev_g;
/**
 * xvphy_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xvphy_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct phy_provider *provider;
	struct device_node *fnode;
	struct platform_device *iface_pdev;
	struct phy *phy;
	unsigned long axi_lite_rate;
	unsigned long drp_clk_rate;
	unsigned int Status=1;
	struct resource *res;
	int port = 0, index = 0;
	void *ptr;
	int ret;

	dev_info(&pdev->dev, "xlnx-dp-vphy: probed\n");
	vphydev = devm_kzalloc(&pdev->dev, sizeof(*vphydev), GFP_KERNEL);
	if (!vphydev)
		return -ENOMEM;

	/* mutex that protects against concurrent access */
	mutex_init(&vphydev->xvphy_mutex);

	vphydev->dev = &pdev->dev;
	/* set a pointer to our driver data */
	platform_set_drvdata(pdev, vphydev);

	BUG_ON(!np);

	XVphy_ConfigTable[instance].DeviceId = VPHY_DEVICE_ID_BASE + instance;

	fnode = of_parse_phandle(np, "xlnx,xilinx-vfmc", 0);
	if (!fnode) {
		dev_err(&pdev->dev, "xilinx-vfmc not found in DT\n");
		of_node_put(fnode);
	} else {
		iface_pdev = of_find_device_by_node(fnode);
		if (!iface_pdev) {
			of_node_put(np);
			return -ENODEV;
		}
		ptr = dev_get_drvdata(&iface_pdev->dev);
		if (!ptr) {
			dev_info(&pdev->dev,
				 "xilinx-vfmc device not found -EPROBE_DEFER\n");
			of_node_put(fnode);
			return -EPROBE_DEFER;
		}
		of_node_put(fnode);
	}

	dev_dbg(vphydev->dev,"DT parse start\n");
	ret = vphy_parse_of(vphydev, &XVphy_ConfigTable[instance]);
	if (ret) return ret;
	dev_dbg(vphydev->dev,"DT parse done\n");


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vphydev->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vphydev->iomem))
		return PTR_ERR(vphydev->iomem);

	/* set address in configuration data */
	XVphy_ConfigTable[instance].BaseAddr = (uintptr_t)vphydev->iomem;

	vphydev->irq = platform_get_irq(pdev, 0);
	if (vphydev->irq <= 0) {
		dev_err(&pdev->dev, "platform_get_irq() failed\n");
		return vphydev->irq;
	}

	/* the AXI lite clock is used for the clock rate detector */
	vphydev->axi_lite_clk = devm_clk_get(&pdev->dev, "axi-lite");
	if (IS_ERR(vphydev->axi_lite_clk)) {
		ret = PTR_ERR(vphydev->axi_lite_clk);
		vphydev->axi_lite_clk = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(&pdev->dev, "axi-lite-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get the axi lite clk.\n");
		return ret;
	}
	ret = clk_prepare_enable(vphydev->axi_lite_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable axi-lite clk\n");
		return ret;
	}
	axi_lite_rate = clk_get_rate(vphydev->axi_lite_clk);
	/* set axi-lite clk in configuration data */
	XVphy_ConfigTable[instance].AxiLiteClkFreq = axi_lite_rate;
	
	vphydev->drp_clk = devm_clk_get(&pdev->dev, "drpclk");
	if (IS_ERR(vphydev->drp_clk)) {
		ret = PTR_ERR(vphydev->drp_clk);
		vphydev->drp_clk = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(&pdev->dev, "drp_clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get the drp clk.\n");
		return ret;
	}
	ret = clk_prepare_enable(vphydev->drp_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable drp clk\n");
		return ret;
	}

	drp_clk_rate = clk_get_rate(vphydev->drp_clk);
	
	XVphy_ConfigTable[instance].DrpClkFreq = drp_clk_rate;
	
	PLLRefClkSel (&vphydev->xvphy, PHY_User_Config_Table[9].LineRate);
	XVphy_DpInitialize(&vphydev->xvphy,&XVphy_ConfigTable[instance], 0,
			   PHY_User_Config_Table[9].CPLLRefClkSrc,
			   PHY_User_Config_Table[9].QPLLRefClkSrc,
			   PHY_User_Config_Table[9].TxPLL,
			   PHY_User_Config_Table[9].RxPLL,
			   PHY_User_Config_Table[9].LineRate);

	//setting vswing
	xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH1,
				XVPHY_GTHE4_DIFF_SWING_DP_V0P0);
	xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH2,
				XVPHY_GTHE4_DIFF_SWING_DP_V0P0);
	xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH3,
				XVPHY_GTHE4_DIFF_SWING_DP_V0P0);
	xvphy_SetTxVoltageSwing(&vphydev->xvphy, XVPHY_CHANNEL_ID_CH4,
				XVPHY_GTHE4_DIFF_SWING_DP_V0P0);
	PHY_Two_byte_set (&vphydev->xvphy, 1, 1);

	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_TX,(TRUE));
	XVphy_BufgGtReset(&vphydev->xvphy, XVPHY_DIR_TX,(TRUE));

	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			 XVPHY_DIR_TX,(FALSE));
	XVphy_BufgGtReset(&vphydev->xvphy, XVPHY_DIR_TX,(FALSE));

	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			 XVPHY_DIR_RX,(TRUE));
	XVphy_BufgGtReset(&vphydev->xvphy, XVPHY_DIR_RX,(TRUE));

	XVphy_ResetGtPll(&vphydev->xvphy, 0, XVPHY_CHANNEL_ID_CHA,
			 XVPHY_DIR_RX,(FALSE));
	XVphy_BufgGtReset(&vphydev->xvphy, XVPHY_DIR_RX,(FALSE));

	Status = PHY_Configuration_Tx(&vphydev->xvphy,
				PHY_User_Config_Table[(is_TX_CPLL) ? 2 : 5]);
	for_each_child_of_node(np, child) {
		struct xvphy_lane *vphy_lane;

		vphy_lane = devm_kzalloc(&pdev->dev, sizeof(*vphy_lane),
					 GFP_KERNEL);
		if (!vphy_lane)
			return -ENOMEM;
		/* Assign lane number to gtr_phy instance */
		vphy_lane->lane = index;

		/* Disable lane sharing as default */
		vphy_lane->share_laneclk = -1;

		BUG_ON(port >= 4);
		/* array of pointer to vphy_lane structs */
		vphydev->lanes[port] = vphy_lane;

		/* create phy device for each lane */
		phy = devm_phy_create(&pdev->dev, child, &xvphy_phyops);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret == -EPROBE_DEFER)
				dev_info(&pdev->dev, "xvphy probe deferred\n");
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to create PHY\n");
			return ret;
		}
		/* array of pointer to phy */
		vphydev->lanes[port]->phy = phy;
		/* where each phy device has vphy_lane as driver data */
		phy_set_drvdata(phy, vphydev->lanes[port]);
		/* and each vphy_lane points back to parent device */
		vphy_lane->data = vphydev;
		port++;
		index++;
	}
	provider = devm_of_phy_provider_register(&pdev->dev, xvphy_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
		return PTR_ERR(provider);
	}

	ret = devm_request_threaded_irq(&pdev->dev, vphydev->irq,
					xvphy_irq_handler, xvphy_irq_thread,
					IRQF_TRIGGER_HIGH /*IRQF_SHARED*/,
					"xilinx-vphy", vphydev/*dev_id*/);
	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ %d\n", vphydev->irq);
		return ret;
	}

	if (vphydev->xvphy.Config.DruIsPresent == (TRUE)) {
		printk("DRU reference clock frequency \n\r");
	}
	
	dev_info(&pdev->dev, "dp-vphy probe successful\n");
	vphydev_g = vphydev;

	/* probe has succeeded for this instance, increment instance index */
	instance++;
	/* Complete PHY dump */


	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id xvphy_of_match[] = {
	{ .compatible = "xlnx,vid-phy-controller-2.2" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xvphy_of_match);

static struct platform_driver xvphy_driver = {
	.probe = xvphy_probe,
	.driver = {
		.name = "xilinx-vphy",
		.of_match_table	= xvphy_of_match,
	},
};
module_platform_driver(xvphy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_DESCRIPTION("Xilinx Vphy driver");
