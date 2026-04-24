// SPDX-License-Identifier: GPL-2.0
/*
 * FMC65 Expander driver
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

static const struct regmap_config fmc65_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

/*
 * struct fmc65 - fmc65 device structure
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

static inline int fmc65_read_reg(struct fmcs64 *priv, u16 addr, u8 *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_err(&priv->client->dev, "fmc65 :regmap_read failed\n");
	return err;
}

static inline int fmc65_write_reg(struct fmcs64 *priv, u16 addr, u8 val)
{
	int err;

	int retry = 0;
	do {
		err = regmap_write(priv->regmap, addr, val);
		if (err) {
			retry++;
			dev_err(&priv->client->dev, "FMC65 I2C write failed, addr = %x val = %x Retry: %d\n", addr,val,retry);
			msleep_range(30);
		}
	}while (err && retry < I2C_RETRY_COUNT);

	return err;
}

int fmc65_init(struct i2c_client *client)
{
	struct fmcs64 *priv = i2c_get_clientdata(client);
	int ret = 0;

	if (!priv)
		return -ENODEV;
	ret = fmc65_write_reg(priv, 0x0, 0x1E);
	if (ret) {
		dev_err(&priv->client->dev, "FMC65 Init failed\n");
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(fmc65_init);

static const struct of_device_id fmc65_of_id_table[] = {
	{ .compatible = "expander-fmc65" },
	{ }
};
MODULE_DEVICE_TABLE(of, fmc65_of_id_table);

static const struct i2c_device_id fmc65_id[] = {
	{ "FMC65", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fmc65_id);

static int fmc65_probe(struct i2c_client *client)
{
	int ret;

	struct fmcs64 *priv;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	mutex_init(&priv->lock);

	priv->regmap = devm_regmap_init_i2c(client, &fmc65_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "fmc65: regmap init failed: %ld\n",
			PTR_ERR(priv->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	i2c_set_clientdata(client, priv);
	dev_info(&client->dev, "fmc65 probed on adapter '%s'\n",
		 client->adapter->name);
	return 0;

err_regmap:
	mutex_destroy(&priv->lock);
	return ret;
}

static void fmc65_remove(struct i2c_client *client)
{
}

static struct i2c_driver fmc65_i2c_driver = {
	.driver = {
		.name	= "fmc65",
		.of_match_table	= fmc65_of_id_table,
	},
	.probe		= fmc65_probe,
	.remove		= fmc65_remove,
	.id_table	= fmc65_id,
};

void fmc65_exit(void)
{
	i2c_del_driver(&fmc65_i2c_driver);
}
EXPORT_SYMBOL_GPL(fmc65_exit);

int fmc65_entry(void)
{
	return i2c_add_driver(&fmc65_i2c_driver);
}
EXPORT_SYMBOL_GPL(fmc65_entry);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("FMC65 Expander driver");
MODULE_LICENSE("GPL v2");
