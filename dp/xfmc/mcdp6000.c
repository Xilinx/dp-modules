// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DP Rx Subsystem
 *
 * Copyright (C) 2020 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define SWAP_BYTES(u32Value) ((u32Value & 0x000000FF) << 24)\
|((u32Value & 0x0000FF00) << 8) \
|((u32Value & 0x00FF0000) >> 8) \
|((u32Value & 0xFF000000) >> 24)

/**************************** Type Definitions *******************************/

struct reg_8 {
	u16 addr;
	u32 val;
};

static const struct regmap_config mcdp6000_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
};

/*
 * struct mcdp6000 - mcdp6000 device structure
 * @client: Pointer to I2C client
 * @ctrls: mcdp6000 control structure
 * @regmap: Pointer to regmap structure
 * @lock: Mutex structure
 * @mode_index: Resolution mode index
 */
struct mcdp6000 {
	struct i2c_client *client;
	struct regmap *regmap;

	/* mutex for serializing operations */
	struct mutex lock;
	u32 mode_index;
};

struct mcdp6000 *mcdp6000;
u32 mcdp6000_rev;
/*
 * Function declaration
 */
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

static inline int mcdp6000_read_reg(struct mcdp6000 *priv, u16 addr, u32 *val)
{
	int err, i;
	u32 value;

	err = regmap_read(priv->regmap, addr, &value);
	if (err < 0)
		dev_dbg(&priv->client->dev, "mcdp6000 :regmap_read failed\n");

	value = SWAP_BYTES(value);

	*val = value;

	return err;
}

static inline int mcdp6000_write_reg(struct mcdp6000 *priv, u16 addr, u32 val)
{
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err < 0)
		dev_dbg(&priv->client->dev, "mcdp6000 :regmap_write failed\n");

	return err;
}

static inline int mcdp6000_modify_reg(struct mcdp6000 *priv, u16 addr, u32 val,
				      u32 mask)
{
	u32 data, err;

	err = mcdp6000_read_reg(priv, addr, &data);
	/* clear masked bits */
	data &= ~mask;

	/* update */
	data |= (val & mask);
	data = SWAP_BYTES(data);

	err |= mcdp6000_write_reg(priv, addr, data);

	return err;
}

static int mcdp6000_reset_dp_path(void)
{
	int ret = 0;

	ret |= mcdp6000_write_reg(mcdp6000, 0x0405, 0x5E710100);
	if (ret < 0)
		dev_dbg(&mcdp6000->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	ret |= mcdp6000_write_reg(mcdp6000, 0x0405, 0x5E700100);
	if (ret < 0)
		dev_dbg(&mcdp6000->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	return ret;
}

static int mcdp6000_reset_cr_path(void)
{
	int ret = 0;


	ret |= mcdp6000_modify_reg(mcdp6000, 0x5001, 0x00008000, 0x00008000);
	if (ret < 0)
		dev_dbg(&mcdp6000->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	ret |= mcdp6000_modify_reg(mcdp6000, 0x5001, 0x00000000, 0x00008000);
	if (ret < 0)
		dev_dbg(&mcdp6000->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	return ret;
}

static int  mcdp6000_access_laneset(void)
{
	int ret = 0;

	dev_dbg(&mcdp6000->client->dev,"%s: %d\n",__func__,__LINE__);
	ret = mcdp6000_write_reg(mcdp6000, 0x5001, 0x01000000);
	if (ret) {
		dev_dbg(&mcdp6000->client->dev,
			"mcdp6000 :regmap_write failed\n");
		return 1;
	}

	ret = mcdp6000_write_reg(mcdp6000, 0x5001, 0x00000000);
	if (ret) {
		dev_dbg(&mcdp6000->client->dev,
			"mcdp6000 :regmap_write failed\n");
		return 1;
	}
	return 0;
}

int mcdp6000_rst_cr_path_callback(void)
{
	int ret = 0;

	dev_dbg(&mcdp6000->client->dev,"mcdp_rev: %x\n",mcdp6000_rev);
	if (mcdp6000_rev == 0x3200) {
		dev_dbg(&mcdp6000->client->dev,"%s: 3200 %d\n",__func__,__LINE__);
		ret = mcdp6000_reset_cr_path();
		if (ret < 0)
			dev_dbg(&mcdp6000->client->dev,
				"mcdp6000 : reset_cr_path failed\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mcdp6000_rst_cr_path_callback);

int mcdp6000_access_laneset_callback(void)
{
	int ret = 0;

	if (mcdp6000_rev == 0x2100) {
		ret = mcdp6000_access_laneset();
		if (ret < 0)
			dev_dbg(&mcdp6000->client->dev,
				"mcdp6000 : mcdp6000_access_laneset failed\n");
	}

	return ret;

}
EXPORT_SYMBOL_GPL(mcdp6000_access_laneset_callback);

int mcdp6000_rst_dp_path_callback(void)
{
	u32 ret = 0;

	if (mcdp6000_rev == 0x2100) {
		ret = mcdp6000_reset_dp_path();
		if (ret < 0)
			dev_dbg(&mcdp6000->client->dev,
				"mcdp6000 : mcdp6000_reset_dp_path failed\n");
	}

	mcdp6000_modify_reg(mcdp6000, 0x000a, 0x55000000, 0x55000000);

	return ret;
}
EXPORT_SYMBOL_GPL(mcdp6000_rst_dp_path_callback);

int XDpRxSs_MCDP6000_EnableDisablePrbs7_Rx(u8 enabled)
{
	u32 readval, data, err;

	readval = mcdp6000_read_reg(mcdp6000, 0x0614, &data);

	if (mcdp6000_rev == 0x2100) {
		if (enabled == true) {
			/* Enable PRBS Mode */
			err |= mcdp6000_write_reg(mcdp6000, 0x0614, (readval | 0x800));
		} else {
			err |= mcdp6000_write_reg(mcdp6000, 0x0614, (readval & ~0xFFFFF7FF));
		}
	}

	return 0;
}

EXPORT_SYMBOL_GPL(XDpRxSs_MCDP6000_EnableDisablePrbs7_Rx);

int XDpRxSs_MCDP6000_ClearCounter(void)
{
	u32 read_val;

	/* Enable Symbol Counter Always*/
	mcdp6000_read_reg(mcdp6000, 0x061c, &read_val);
	mcdp6000_write_reg(mcdp6000, 0x061c, (read_val & 0xFFFFFFFE));

	return 0;
}
EXPORT_SYMBOL_GPL(XDpRxSs_MCDP6000_ClearCounter);

int mcdp6000_init(void)
{
	int ret = 0;
	u32 mcdp6000_bs, rev;

	mcdp6000_read_reg(mcdp6000, 0x1005, &rev);
	mcdp6000_rev = rev & 0xFF00;
	mcdp6000_bs = rev & 0x1c;

	dev_info(&mcdp6000->client->dev,
		 "mcdp6000 : revision no %x bs: %x\n",mcdp6000_rev, mcdp6000_bs);

	if (mcdp6000_rev == 0x2100) {
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x5003, 0x1F000000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0405, 0x5E700000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x8C27, 0x90010000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0C01, 0x242D0F0F);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0405, 0x5E710000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0405, 0x5E700000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x1426, 0x0F0F071A);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0xA001, 0x444488CC);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0xC001, 0x1EA8002C);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0xD001, 0x60C30000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x7801, 0x80144713);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0809, 0x000C0000);
		msleep(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x000B, 0x00000000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x040B, 0x00000000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0C09, 0x00000202);
		msleep_range(100);
	} else if (mcdp6000_rev == 0x3100) {
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x5003, 0x1f000000);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0405, 0x5e700100);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0xc001, 0x9e2c002c);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x2c09, 0xa5a55555);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x0009, 0x06050104);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x7801, 0x80144713);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0xa001, 0x444488cc);
		msleep_range(100);
		ret = mcdp6000_write_reg(mcdp6000, 0x1426, 0x0f0f8919);
		if (mcdp6000_bs == 0x18) {
			ret = mcdp6000_write_reg(mcdp6000, 0x4023, 0x00050000);
			msleep_range(100);
			ret = mcdp6000_write_reg(mcdp6000, 0x4025, 0x00050000);
		} else if (mcdp6000_bs == 0x8){
			ret = mcdp6000_write_reg(mcdp6000, 0x4022, 0x00050000);
			ret = mcdp6000_write_reg(mcdp6000, 0x4024, 0x00050000);
		}
		ret = mcdp6000_write_reg(mcdp6000, 0x0816, 0x04847400);
		ret = mcdp6000_write_reg(mcdp6000, 0x0826, 0x04847400);

	} else if (mcdp6000_rev == 0x3200) {
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x4c02, 0x501a2222);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x5003, 0x1f000000);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x0405, 0x5e700100);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x1426, 0x0f0f8919);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0xd801, 0x01060000);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x6006, 0x11500000);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x7c06, 0x01000000);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x0809, 0x66080000);
		msleep_range(20);
		ret = mcdp6000_write_reg(mcdp6000, 0x0c09, 0x00000204);
		msleep_range(20);
		if (mcdp6000_bs == 0x18) {
			ret = mcdp6000_write_reg(mcdp6000, 0x4023, 0x00050000);
			msleep_range(20);
			ret = mcdp6000_write_reg(mcdp6000, 0x4025, 0x00050000);
			msleep_range(20);
		} else if (mcdp6000_bs == 0x8) {
			ret = mcdp6000_write_reg(mcdp6000, 0x4022, 0x00050000);
			msleep_range(20);
			ret = mcdp6000_write_reg(mcdp6000, 0x4024, 0x00050000);
			msleep_range(20);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mcdp6000_init);

static const struct of_device_id mcdp6000_of_id_table[] = {
	{ .compatible = "expander-mcdp6000" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcdp6000_of_id_table);

static const struct i2c_device_id mcdp6000_id[] = {
	{ "mcdp6000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcdp6000_id);

static int mcdp6000_probe(struct i2c_client *client)
{
	int ret;

	/* initialize mcdp6000 */
	mcdp6000 = devm_kzalloc(&client->dev, sizeof(*mcdp6000), GFP_KERNEL);
	if (!mcdp6000)
		return -ENOMEM;

	mutex_init(&mcdp6000->lock);
	mcdp6000->client = client;

	/* initialize regmap */
	mcdp6000->regmap = devm_regmap_init_i2c(client,
						&mcdp6000_regmap_config);
	if (IS_ERR(mcdp6000->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(mcdp6000->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}
	dev_info(&client->dev, "mcdp6000 : probe success !\n");

	return 0;

err_regmap:
	mutex_destroy(&mcdp6000->lock);
	return ret;
}

static void mcdp6000_remove(struct i2c_client *client)
{
}

static struct i2c_driver mcdp6000_i2c_driver = {
	.driver = {
		.name	= "mcdp6000",
		.of_match_table	= mcdp6000_of_id_table,
	},
	.probe		= mcdp6000_probe,
	.remove		= mcdp6000_remove,
	.id_table	= mcdp6000_id,
};

void mcdp6000_exit(void)
{
	i2c_del_driver(&mcdp6000_i2c_driver);
}
EXPORT_SYMBOL_GPL(mcdp6000_exit);

int mcdp6000_entry(void)
{
	return i2c_add_driver(&mcdp6000_i2c_driver);
}
EXPORT_SYMBOL_GPL(mcdp6000_entry);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("mcdp6000 Expander driver");
MODULE_LICENSE("GPL v2");
