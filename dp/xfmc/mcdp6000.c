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
#include "fmc.h"
#include "xstatus.h"

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
 * @rev: chip revision
 * @bs: Chip build status
 */
struct mcdp6000 {
	struct i2c_client *client;
	struct regmap *regmap;

	/* mutex for serializing operations */
	struct mutex lock;
	u32 mode_index;
	u32 rev;
	u32 bs;
};

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
		dev_err(&priv->client->dev, "mcdp6000 :regmap_read failed\n");

	value = SWAP_BYTES(value);

	*val = value;

	return err;
}

static inline int mcdp6000_write_reg(struct mcdp6000 *priv, u16 addr, u32 val)
{
	int err;

	int retry = 0;
	do {
		err = regmap_write(priv->regmap, addr, val);
		if (err) {
			retry++;
			dev_err(&priv->client->dev, "MCDP6000 I2C write failed, addr = %x val = %x Retry: %d\n", addr,val,retry);
			msleep_range(30);
		}
	}while (err && retry < I2C_RETRY_COUNT);

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

static int mcdp6000_get_revision(struct mcdp6000 *priv,
				 u32 *mcdp6000_rev, u32 *mcdp6000_bs)
{
	int ret = 0;
	u32 rev;

	ret = mcdp6000_read_reg(priv, 0x1005, &rev);
	if (ret == XST_SUCCESS) {
		*mcdp6000_rev = rev & 0xFF00;;
		*mcdp6000_bs = rev & 0x1c;
	} else {
		*mcdp6000_rev = 0;
		*mcdp6000_bs = 0;
	}
	return ret;
}

static int mcdp6000_reset_dp_path(struct mcdp6000 *priv)
{
	int ret = 0;

	ret |= mcdp6000_write_reg(priv, 0x0405, 0x5E710100);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	ret |= mcdp6000_write_reg(priv, 0x0405, 0x5E700100);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	return ret;
}

static int mcdp6000_reset_cr_path(struct mcdp6000 *priv)
{
	int ret = 0;

	ret |= mcdp6000_modify_reg(priv, 0x5001, 0x00008000, 0x00008000);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	ret |= mcdp6000_modify_reg(priv, 0x5001, 0x00000000, 0x00008000);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"mcdp6000 :regmap_modify failed\n");

	return ret;
}

static int mcdp6000_access_laneset(struct mcdp6000 *priv)
{
	int ret = 0;

	dev_dbg(&priv->client->dev, "%s: %d\n", __func__, __LINE__);
	ret = mcdp6000_write_reg(priv, 0x5001, 0x01000000);
	if (ret) {
		dev_err(&priv->client->dev,
			"mcdp6000 :regmap_write failed\n");
		return 1;
	}

	ret = mcdp6000_write_reg(priv, 0x5001, 0x00000000);
	if (ret) {
		dev_err(&priv->client->dev,
			"mcdp6000 :regmap_write failed\n");
		return 1;
	}
	return 0;
}

int mcdp6000_rst_cr_path_callback(struct i2c_client *client)
{
	struct mcdp6000 *priv = i2c_get_clientdata(client);
	int ret = 0;

	if (!priv)
		return -ENODEV;

	dev_dbg(&priv->client->dev, "mcdp_rev: %x\n", priv->rev);
	if (priv->rev == 0x3200) {
		dev_dbg(&priv->client->dev, "%s: 3200 %d\n", __func__, __LINE__);
		ret = mcdp6000_reset_cr_path(priv);
		if (ret < 0)
			dev_err(&priv->client->dev,
				"mcdp6000 : reset_cr_path failed\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mcdp6000_rst_cr_path_callback);

int mcdp6000_access_laneset_callback(struct i2c_client *client)
{
	struct mcdp6000 *priv = i2c_get_clientdata(client);
	int ret = 0;

	if (!priv)
		return -ENODEV;

	if (priv->rev == 0x2100) {
		ret = mcdp6000_access_laneset(priv);
		if (ret < 0)
			dev_dbg(&priv->client->dev,
				"mcdp6000 : mcdp6000_access_laneset failed\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mcdp6000_access_laneset_callback);

int mcdp6000_rst_dp_path_callback(struct i2c_client *client)
{
	struct mcdp6000 *priv = i2c_get_clientdata(client);
	u32 ret = 0;

	if (!priv)
		return -ENODEV;

	if (priv->rev == 0x2100) {
		ret = mcdp6000_reset_dp_path(priv);
		if (ret < 0)
			dev_dbg(&priv->client->dev,
				"mcdp6000 : mcdp6000_reset_dp_path failed\n");
	}

	mcdp6000_modify_reg(priv, 0x000a, 0x55000000, 0x55000000);

	return ret;
}
EXPORT_SYMBOL_GPL(mcdp6000_rst_dp_path_callback);

int XDpRxSs_MCDP6000_EnableDisablePrbs7_Rx(struct i2c_client *client,
					    u8 enabled)
{
	struct mcdp6000 *priv = i2c_get_clientdata(client);
	u32 readval, data, err = 0;

	if (!priv)
		return -ENODEV;
	mcdp6000_read_reg(priv, 0x0614, &data);
	readval = data;

	if (priv->rev == 0x2100) {
		if (enabled == true) {
			/* Enable PRBS Mode */
			err |= mcdp6000_write_reg(priv, 0x0614, (readval | 0x800));
		} else {
			err |= mcdp6000_write_reg(priv, 0x0614, (readval & ~0xFFFFF7FF));
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(XDpRxSs_MCDP6000_EnableDisablePrbs7_Rx);

int XDpRxSs_MCDP6000_ClearCounter(struct i2c_client *client)
{
	struct mcdp6000 *priv = i2c_get_clientdata(client);
	u32 read_val;

	if (!priv)
		return -ENODEV;
	/* Enable Symbol Counter Always */
	mcdp6000_read_reg(priv, 0x061c, &read_val);
	mcdp6000_write_reg(priv, 0x061c, (read_val & 0xFFFFFFFE));

	return 0;
}
EXPORT_SYMBOL_GPL(XDpRxSs_MCDP6000_ClearCounter);

int mcdp6000_init(struct i2c_client *client)
{
	struct mcdp6000 *priv = i2c_get_clientdata(client);
	int ret = 0;

	if (!priv)
		return -ENODEV;

	if (priv->rev == 0x2100) {
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x5003, 0x1F000000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0405, 0x5E700000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x8C27, 0x90010000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0C01, 0x242D0F0F);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0405, 0x5E710000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0405, 0x5E700000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x1426, 0x0F0F071A);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0xA001, 0x444488CC);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0xC001, 0x1EA8002C);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0xD001, 0x60C30000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x7801, 0x80144713);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0809, 0x000C0000);
		msleep(100);
		ret |= mcdp6000_write_reg(priv, 0x000B, 0x00000000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x040B, 0x00000000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0C09, 0x00000202);
		msleep_range(100);
	} else if (priv->rev == 0x3100) {
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x5003, 0x1f000000);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0405, 0x5e700100);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0xc001, 0x9e2c002c);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x2c09, 0xa5a55555);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x0009, 0x06050104);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x7801, 0x80144713);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0xa001, 0x444488cc);
		msleep_range(100);
		ret |= mcdp6000_write_reg(priv, 0x1426, 0x0f0f8919);
		if (priv->bs == 0x18) {
			ret |= mcdp6000_write_reg(priv, 0x4023, 0x00050000);
			msleep_range(100);
			ret |= mcdp6000_write_reg(priv, 0x4025, 0x00050000);
		} else if (priv->bs == 0x8) {
			ret |= mcdp6000_write_reg(priv, 0x4022, 0x00050000);
			ret |= mcdp6000_write_reg(priv, 0x4024, 0x00050000);
		}
		ret |= mcdp6000_write_reg(priv, 0x0816, 0x04847400);
		ret |= mcdp6000_write_reg(priv, 0x0826, 0x04847400);

	} else if (priv->rev == 0x3200) {
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x4c02, 0x501a2222);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x5003, 0x1f000000);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x0405, 0x5e700100);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x1426, 0x0f0f8919);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0xd801, 0x01060000);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x6006, 0x11500000);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x7c06, 0x01000000);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x0809, 0x66080000);
		msleep_range(20);
		ret |= mcdp6000_write_reg(priv, 0x0c09, 0x00000204);
		msleep_range(20);
		if (priv->bs == 0x18) {
			ret |= mcdp6000_write_reg(priv, 0x4023, 0x00050000);
			msleep_range(20);
			ret |= mcdp6000_write_reg(priv, 0x4025, 0x00050000);
			msleep_range(20);
		} else if (priv->bs == 0x8) {
			ret |= mcdp6000_write_reg(priv, 0x4022, 0x00050000);
			msleep_range(20);
			ret |= mcdp6000_write_reg(priv, 0x4024, 0x00050000);
			msleep_range(20);
		}
	}

	if (ret) {
		dev_err(&priv->client->dev, "MCDP6000 init failed\n");
	}

	return ret;
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
	struct mcdp6000 *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	mutex_init(&priv->lock);

	priv->regmap = devm_regmap_init_i2c(client, &mcdp6000_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	/* Store per-instance priv */
	i2c_set_clientdata(client, priv);

	/* Read revision once and store it in instance pointer. This will be used by all runtime callbacks */
	if (mcdp6000_get_revision(priv, &priv->rev, &priv->bs) == XST_SUCCESS)
		dev_info(&client->dev,
			 "mcdp6000: revision 0x%x bs 0x%x\n",
			 priv->rev, priv->bs);
	else
		dev_warn(&client->dev, "mcdp6000_get_revision failed\n");

	dev_info(&client->dev, "mcdp6000 probed on adapter '%s'\n",
		 client->adapter->name);
	return 0;

err_regmap:
	mutex_destroy(&priv->lock);
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
