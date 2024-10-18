// SPDX-License-Identifier: GPL-2.0
/*
 * dp141 redriver driver
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
	u8 val;
};

static const struct regmap_config dp141_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/*
 * struct dp141 - dp141 device structure
 * @client: Pointer to I2C client
 * @ctrls: dp141 control structure
 * @regmap: Pointer to regmap structure
 * @lock: Mutex structure
 * @mode_index: Resolution mode index
 */
struct dp141 {
	struct i2c_client *client;
	struct regmap *regmap;

	/* mutex for serializing operations */
	struct mutex lock;
	u32 mode_index;
};

struct dp141 *dp141;

/*
 * Function declaration
 */
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

static inline int dp141_read_reg(struct dp141 *priv, u8 addr, u8 *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_dbg(&priv->client->dev, "dp141 :regmap_read failed\n");
	return err;
}

static inline int dp141_write_reg(struct dp141 *priv, u8 addr, u8 val)
{
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_dbg(&priv->client->dev,
			"dp141 :regmap_write failed @0x%x\n",addr);

	return err;
}

int dp141_init(void)
{
	int ret = 0;

	msleep_range(20);
	ret = dp141_write_reg(dp141, 0x2, 0x3c);
	msleep_range(10);
	ret = dp141_write_reg(dp141, 0x5, 0x3c);
	msleep_range(10);
	ret = dp141_write_reg(dp141, 0x8, 0x3c);
	msleep_range(10);
	ret = dp141_write_reg(dp141, 0xb, 0x3c);

	return ret;
}
EXPORT_SYMBOL_GPL(dp141_init);

static const struct of_device_id dp141_of_id_table[] = {
	{ .compatible = "dp141" },
	{ }
};
MODULE_DEVICE_TABLE(of, dp141_of_id_table);

static const struct i2c_device_id dp141_id[] = {
	{ "dp141", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dp141_id);

static int dp141_probe(struct i2c_client *client)
{
	int ret;

	/* initialize dp141 */
	dp141 = devm_kzalloc(&client->dev, sizeof(*dp141), GFP_KERNEL);
	if (!dp141)
		return -ENOMEM;

	mutex_init(&dp141->lock);

	/* initialize regmap */
	dp141->regmap = devm_regmap_init_i2c(client, &dp141_regmap_config);
	if (IS_ERR(dp141->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(dp141->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	dev_info(&client->dev, "dp141 : probe success !\n");

	return 0;

err_regmap:
	mutex_destroy(&dp141->lock);
	return ret;
}

static int dp141_remove(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver dp141_i2c_driver = {
	.driver = {
		.name	= "dp141",
		.of_match_table	= dp141_of_id_table,
	},
	.probe		= dp141_probe,
	.remove		= dp141_remove,
	.id_table	= dp141_id,
};

void dp141_exit(void)
{
	i2c_del_driver(&dp141_i2c_driver);
}
EXPORT_SYMBOL_GPL(dp141_exit);

int dp141_entry(void)
{
	return i2c_add_driver(&dp141_i2c_driver);
}
EXPORT_SYMBOL_GPL(dp141_entry);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("dp141 driver");
MODULE_LICENSE("GPL v2");
