/*******************************************************************************
* Copyright (C) 2015 - 2020 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2023 - 2026 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: GPL-2.0
*******************************************************************************/
/******************************************************************************/
/**
 *
 * @file fmc.h
 *
 * The Xilinx FMC driver handles FMC configuration
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   pam  16/04/26 Initial release.
 * </pre>
 *
 * @addtogroup xvphy_v1_7
 * @{
*******************************************************************************/

#ifndef FMC_H_
/* Prevent circular inclusions by using protection macros. */
#define FMC_H_

/************************** Constant Definitions ******************************/
#define I2C_RETRY_COUNT          5

/*
 * x_vfmc_dev - per-instance device context for the VFMC platform device.
 * All i2c_client pointers are resolved at xvfmc_probe time via DT phandles.
 * The I2C mux framework selects the correct mux channel transparently before
 * every transaction - no manual channel selection is needed.
 */
struct x_vfmc_dev {
	struct device      *dev;
	bool               is_parretto;
	struct i2c_client  *fmc64_client;
	struct i2c_client  *fmc65_client;
	struct i2c_client  *idt_client;
	struct i2c_client  *tipower_client;
	struct i2c_client  *mcdp6000_client;
	struct i2c_client  *dp141_client;
	struct i2c_client  *tdp2004_client;
};

/*
 * x_vfmc_cfg - per-instance retimer operation table.
 * mcdp6000_client carries which HPC instance to use; the I2C mux framework
 * selects the channel automatically from client->adapter.
 * This struct is stored via platform_set_drvdata and retrieved by RXSS.
 */
struct x_vfmc_cfg {
	struct i2c_client *mcdp6000_client;
	void (*retimer_access_laneset)(struct i2c_client *client);
	void (*retimer_rst_cr_path)(struct i2c_client *client);
	void (*retimer_rst_dp_path)(struct i2c_client *client);
	void (*retimer_set_prbs_mode)(struct i2c_client *client, u8 enable);
};

/**************************** Function Prototypes *****************************/

/* All init functions take the per-instance i2c_client*.
 * Internally they call i2c_get_clientdata() to retrieve the priv struct.
 */
int fmc64_init(struct i2c_client *client);
int fmc65_init(struct i2c_client *client);
int IDT_8T49N24x_Init(struct i2c_client *client);
int IDT_8T49N24x_Configure(struct i2c_client *client);
int tipower_init(struct i2c_client *client);
int dp141_init(struct i2c_client *client);
int mcdp6000_init(struct i2c_client *client);

/* I2C driver registration helpers - called from xilinx-vfmc module_init */
int idt_entry(void);
void idt_exit(void);
int fmc_entry(void);
void fmc_exit(void);
int fmc64_entry(void);
void fmc64_exit(void);
int fmc65_entry(void);
void fmc65_exit(void);
int tipower_entry(void);
void tipower_exit(void);
int dp141_entry(void);
void dp141_exit(void);
int mcdp6000_entry(void);
void mcdp6000_exit(void);
int tdp2004_entry(void);
void tdp2004_exit(void);

int IDT_8T49N24x_SetClock(struct i2c_client *client);
int xfmc_init(struct x_vfmc_dev *xfmcdev);
int tdp2004_init(struct i2c_client *client);

/* MCDP6000 runtime callbacks - take explicit client for instance safety */
int XDpRxSs_MCDP6000_EnableDisablePrbs7_Rx(struct i2c_client *client,
					    u8 enabled);
int mcdp6000_access_laneset_callback(struct i2c_client *client);
int XDpRxSs_MCDP6000_ClearCounter(struct i2c_client *client);
int mcdp6000_rst_dp_path_callback(struct i2c_client *client);
int mcdp6000_rst_cr_path_callback(struct i2c_client *client);
/* mcdp6000_select_callback removed: I2C mux framework handles channel
 * selection transparently - no manual selection required. */
#endif /* FMC_H_ */
/** @} */
