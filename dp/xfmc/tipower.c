// SPDX-License-Identifier: GPL-2.0
/*
 * TIPOWER Expander driver
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

static const struct regmap_config tipower_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/*
 * struct tipower - tipower device structure
 * @client: Pointer to I2C client
 * @ctrls: tipower control structure
 * @regmap: Pointer to regmap structure
 * @lock: Mutex structure
 * @mode_index: Resolution mode index
 */
struct tipowers {
	struct i2c_client *client;
	struct regmap *regmap;

	/* mutex for serializing operations */
	struct mutex lock;
	u32 mode_index;
};

struct tipowers *tipower;

/*
 * Function declaration
 */
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

static inline int tipower_read_reg(struct tipowers *priv, u16 addr, u8 *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_dbg(&priv->client->dev, "tipower :regmap_read failed\n");
	return err;
}

static inline int tipower_write_reg(struct tipowers *priv, u16 addr, u8 val)
{
	int err;
	u32 rdval;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_dbg(&priv->client->dev, "tipower :regmap_write failed\n");

	err = regmap_read(priv->regmap, addr, &rdval);
	if (err) {
		dev_dbg(&priv->client->dev, "tipower :regmap_write failed\n");
	}

	dev_dbg(&priv->client->dev,
		"reg_addr = 0x%x, wrval =0x%x, rdval = 0x%x\n",addr, val, rdval);

	return err;
}

int tipower_init(void)
{
	int ret = 0;

	msleep_range(20);
	ret = tipower_write_reg(tipower, 29, 0x3);
	if (ret)
		return 1;

	msleep_range(10);
	ret = tipower_write_reg(tipower, 31, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 32, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 34, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 35, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 37, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 39, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 41, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 43, 0x0);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 50, 0xf6);
	msleep_range(10);
	ret = tipower_write_reg(tipower, 56, 0x1);
	msleep_range(10);

	return 0;
}
EXPORT_SYMBOL_GPL(tipower_init);

static const struct of_device_id tipower_of_id_table[] = {
	{ .compatible = "expander-tipower" },
	{ }
};
MODULE_DEVICE_TABLE(of, tipower_of_id_table);

static const struct i2c_device_id tipower_id[] = {
	{ "TIPOWER", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tipower_id);

static int tipower_probe(struct i2c_client *client)
{
	int ret;

	/* initialize tipower */
	tipower = devm_kzalloc(&client->dev, sizeof(*tipower), GFP_KERNEL);
	if (!tipower)
		return -ENOMEM;

	mutex_init(&tipower->lock);

	/* initialize regmap */
	tipower->regmap = devm_regmap_init_i2c(client, &tipower_regmap_config);
	if (IS_ERR(tipower->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(tipower->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	tipower->client = client;

	dev_info(&client->dev, "tipower : probe success !\n");

	return 0;

err_regmap:
	mutex_destroy(&tipower->lock);
	return ret;
}

static void tipower_remove(struct i2c_client *client)
{
}

static struct i2c_driver tipower_i2c_driver = {
	.driver = {
		.name	= "tipower",
		.of_match_table	= tipower_of_id_table,
	},
	.probe		= tipower_probe,
	.remove		= tipower_remove,
	.id_table	= tipower_id,
};

void tipower_exit(void)
{
	i2c_del_driver(&tipower_i2c_driver);
}
EXPORT_SYMBOL_GPL(tipower_exit);

int tipower_entry(void)
{
	return i2c_add_driver(&tipower_i2c_driver);
}
EXPORT_SYMBOL_GPL(tipower_entry);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("TIPOWER Expander driver");
MODULE_LICENSE("GPL v2");
