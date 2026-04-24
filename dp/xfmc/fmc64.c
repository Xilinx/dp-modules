// SPDX-License-Identifier: GPL-2.0
/*
 * FMC64 Expander driver
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
/**************************** Type Definitions *******************************/
static const struct regmap_config fmc64_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

/*
 * struct fmc64 - fmc64 device structure
 * @client: Pointer to I2C client
 * @regmap: Pointer to regmap structure
 * @lock: Mutex structure
 * @mode_index: Resolution mode index
 */
struct fmcs64 {
	struct i2c_client *client;
	struct regmap *regmap;

	/* mutex for serializing operations */
	struct mutex lock;
	u32 mode_index;
};


/*
 * Function declaration
 */
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

static inline int fmc64_read_reg(struct fmcs64 *priv, u16 addr, u8 *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_err(&priv->client->dev, "fmc64 :regmap_read failed\n");
	return err;
}

static inline int fmc64_write_reg(struct fmcs64 *priv, u16 addr, u8 val)
{
	int err;

	int retry = 0;
	do {
		err = regmap_write(priv->regmap, addr, val);
		if (err) {
			retry++;
			dev_err(&priv->client->dev, "FMC64 I2C write failed, addr = %x val = %x Retry: %d\n", addr,val,retry);
			msleep_range(30);
		}
	}while (err && retry < I2C_RETRY_COUNT);

	return err;
}

int fmc64_init(struct i2c_client *client)
{
	struct fmcs64 *priv = i2c_get_clientdata(client);
	int ret = 0;

	if (!priv)
		return -ENODEV;
	ret = fmc64_write_reg(priv, 0x0, 0x52);
	if (ret) {
		dev_err(&priv->client->dev, "FMC64 Init failed\n");
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(fmc64_init);

static const struct of_device_id fmc64_of_id_table[] = {
	{ .compatible = "expander-fmc64" },
	{ }
};
MODULE_DEVICE_TABLE(of, fmc64_of_id_table);

static const struct i2c_device_id fmc64_id[] = {
	{ "FMC64", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fmc64_id);

static int fmc64_probe(struct i2c_client *client)
{
	int ret;

	struct fmcs64 *priv;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	mutex_init(&priv->lock);

	priv->regmap = devm_regmap_init_i2c(client, &fmc64_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "fmc64: regmap init failed: %ld\n",
			PTR_ERR(priv->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	i2c_set_clientdata(client, priv);
	dev_info(&client->dev, "fmc64 probed on adapter '%s'\n",
		 client->adapter->name);
	return 0;

err_regmap:
	mutex_destroy(&priv->lock);
	return ret;
}

static void fmc64_remove(struct i2c_client *client)
{
}

static struct i2c_driver fmc64_i2c_driver = {
	.driver = {
		.name	= "fmc64",
		.of_match_table	= fmc64_of_id_table,
	},
	.probe		= fmc64_probe,
	.remove		= fmc64_remove,
	.id_table	= fmc64_id,
};

void fmc64_exit(void)
{
	i2c_del_driver(&fmc64_i2c_driver);
}
EXPORT_SYMBOL_GPL(fmc64_exit);

int fmc64_entry(void)
{
	return i2c_add_driver(&fmc64_i2c_driver);
}
EXPORT_SYMBOL_GPL(fmc64_entry);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("FMC64 Expander driver");
MODULE_LICENSE("GPL v2");
