/*
 * Xilinx VPHY header
 *
 * Copyright (C) 2016-2017 Xilinx, Inc.
 *
 * Author: Leon Woestenberg <leon@sidebranch.com>
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

#ifndef _PHY_VPHY_H_
#define _PHY_VPHY_H_

/* @TODO change directory name on production release */
#include "xvphy.h"

struct phy;
/**
 *  * struct xvphy_dev - representation of a Xilinx Video PHY
 *   * @dev: pointer to device
 *    * @iomem: serdes base address
 *     */
struct xvphy_dev {
        struct device *dev;
        /* virtual remapped I/O memory */
        void __iomem *iomem;
        int irq;
        /* protects the XVphy baseline against concurrent access */
        struct mutex xvphy_mutex;
        struct xvphy_lane *lanes[4];
        /* bookkeeping for the baseline subsystem driver instance */
        XVphy xvphy;
        /* AXI Lite clock drives the clock detector */
        struct clk *axi_lite_clk;
	struct clk *drp_clk;
	struct clk *vid_phy_rx_axi4s_aclk;
        /* NI-DRU clock input */
        struct clk *clkp;
        struct regmap *regmap;
};


struct xlnx_dp_clkwiz {
        struct device *dev;
        void __iomem *iomem;
        int irq;
};

#define is_TX_CPLL 		0
struct xvphy_dev *register_dp_cb(void);
//u32 set_vphy(XVphy *InstancePtr, int LineRate_init_tx); //gv
struct xlnx_dp_clkwiz *register_dp_clkwiz_cb(void);

/* VPHY is built (either as module or built-in) */
extern XVphy *xvphy_get_xvphy(struct phy *phy);
extern void xvphy_mutex_lock(struct phy *phy);
extern void xvphy_mutex_unlock(struct phy *phy);
extern int xvphy_do_something(struct phy *phy);

#endif /* _PHY_VPHY_H_ */
