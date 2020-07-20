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

/*
 * Function declaration
 */
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

static inline int mcdp6000_read_reg(struct mcdp6000 *priv, u16 addr, u32 *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_dbg(&priv->client->dev, "mcdp6000 :regmap_read failed\n");
	return err;
}

static inline int mcdp6000_write_reg(struct mcdp6000 *priv, u16 addr, u32 val)
{
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_dbg(&priv->client->dev, "mcdp6000 :regmap_write failed\n");
	return err;
}

int  mcdp6000_access_laneset(void)
{
	int ret = 0;

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
EXPORT_SYMBOL_GPL(mcdp6000_access_laneset);

int mcdp6000_init(void)
{
	int ret = 0;

	msleep_range(20);
	ret = mcdp6000_write_reg(mcdp6000, 0x5003, 0x1F0000);
	if (ret)
		return 1;

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

static int mcdp6000_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int ret;

	/* initialize mcdp6000 */
	mcdp6000 = devm_kzalloc(&client->dev, sizeof(*mcdp6000), GFP_KERNEL);
	if (!mcdp6000)
		return -ENOMEM;

	mutex_init(&mcdp6000->lock);

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

static int mcdp6000_remove(struct i2c_client *client)
{
	return 0;
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
