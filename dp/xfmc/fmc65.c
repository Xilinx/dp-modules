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

struct fmcs64 *fmc65;

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
		dev_dbg(&priv->client->dev, "fmc65 :regmap_read failed\n");
	return err;
}

static inline int fmc65_write_reg(struct fmcs64 *priv, u16 addr, u8 val)
{
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_dbg(&priv->client->dev, "fmc65 :regmap_write failed\n");
	return err;
}

int fmc65_init(void)
{
	int ret = 0;

	ret = fmc65_write_reg(fmc65, 0x0, 0x1E);
	if (ret)
		return 1;
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

	/* initialize fmc65 */
	fmc65 = devm_kzalloc(&client->dev, sizeof(*fmc65), GFP_KERNEL);
	if (!fmc65)
		return -ENOMEM;

	mutex_init(&fmc65->lock);

	/* initialize regmap */
	fmc65->regmap = devm_regmap_init_i2c(client, &fmc65_regmap_config);
	if (IS_ERR(fmc65->regmap)) {
		dev_err(&client->dev, "fmc65: regmap init failed: %ld\n",
			PTR_ERR(fmc65->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	dev_info(&client->dev, "fmc65 : probe success !\n");

	return 0;

err_regmap:
	mutex_destroy(&fmc65->lock);
	return ret;
}

static int fmc65_remove(struct i2c_client *client)
{
	return 0;
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
