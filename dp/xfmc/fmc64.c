/*
 * fmc64.c - FMC8T
 * Copyright (C) 2017, Leopard Imaging, Inc.
 *
 * Leon Luo <leonl@leopardimaging.com>
 * Edwin Zou <edwinz@leopardimaging.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>


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
	struct mutex lock; /* mutex lock for operations */
	u32 mode_index;
};
struct fmcs64 *fmc64 ;

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
		printk("fmc64: regmap read failed\n");
	return err;
}

static inline int fmc64_write_reg(struct fmcs64 *priv, u16 addr, u8 val)
{
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		printk("fmc64: regmap write failed \n");
	return err;
}

int fmc64_init(void)
{

	int ret =0;

	ret= fmc64_write_reg(fmc64, 0x0, 0x52);
	if(ret){
		printk("%s write failed\n");
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


static int fmc64_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	u8 reg_val[2];

	/* initialize fmc64 */
	fmc64 = devm_kzalloc(&client->dev, sizeof(*fmc64), GFP_KERNEL);
	if (!fmc64)
		return -ENOMEM;

	mutex_init(&fmc64->lock);

	/* initialize regmap */
	fmc64->regmap = devm_regmap_init_i2c(client, &fmc64_regmap_config);
	if (IS_ERR(fmc64->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(fmc64->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	dev_info(&client->dev, "fmc64 : probe success !\n");

	return 0;

err_regmap:
	mutex_destroy(&fmc64->lock);
	return ret;
}

static int fmc64_remove(struct i2c_client *client)
{
	return 0;
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

int fmc64_entry(void)
{

        return i2c_add_driver(&fmc64_i2c_driver);

}
EXPORT_SYMBOL_GPL(fmc64_entry);
EXPORT_SYMBOL_GPL(fmc64_exit);

MODULE_AUTHOR("Leon Luo <leonl@leopardimaging.com>");
MODULE_DESCRIPTION("FMC64 Expander driver");
MODULE_LICENSE("GPL v2");
