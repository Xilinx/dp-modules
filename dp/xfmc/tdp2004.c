// SPDX-License-Identifier: GPL-2.0
/*
 * TDP2004 linear redriver driver (Parretto FMC)
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Performs the two-register read diagnostic and two-register write
 * initialisation sequence required by the Parretto FMC board at probe time.
 * No runtime (link-training) callbacks are required.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include "fmc.h"

/**
 * struct tdp2004 - per-instance device context
 * @client: Pointer to the I2C client
 * @regmap: Pointer to the regmap instance
 */
struct tdp2004 {
	struct i2c_client *client;
	struct regmap *regmap;
};

static const struct regmap_config tdp2004_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xF1,
};

static int tdp2004_read_reg(struct tdp2004 *priv, u8 addr, unsigned int *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, val);
	if (err)
		dev_err(&priv->client->dev,
			"TDP2004: regmap_read failed addr=0x%02x err=%d\n",
			addr, err);
	return err;
}

static int tdp2004_write_reg(struct tdp2004 *priv, u8 addr, u8 val)
{
	int err;
	int retry = 0;

	do {
		err = regmap_write(priv->regmap, addr, val);
		if (err) {
			retry++;
			dev_err(&priv->client->dev,
				"TDP2004: write failed addr=0x%02x val=0x%02x retry=%d\n",
				addr, val, retry);
			usleep_range(1000, 2000);
		}
	} while (err && retry < I2C_RETRY_COUNT);

	return err;
}

/**
 * tdp2004_init - Execute the Parretto FMC initialisation sequence.
 * @client: I2C client for the TDP2004 device
 *
 * Reads registers 0xF0 and 0xF1 for diagnostic purposes, then writes
 * the two configuration registers 0x83 and 0x84 required for TXSS
 * operation on the Parretto FMC.
 *
 * Return: 0 on success, negative error code on failure.
 */
int tdp2004_init(struct i2c_client *client)
{
	struct tdp2004 *priv = i2c_get_clientdata(client);
	unsigned int val;
	int ret;

	if (!priv)
		return -ENODEV;

	/* Presence probe: reads must clock data back from a real device */
	ret = tdp2004_read_reg(priv, 0xF0, &val);
	if (ret) {
		dev_err(&client->dev,
			"TDP2004: presence probe read reg 0xF0 failed\n");
		return ret;
	}
	dev_dbg(&client->dev, "TDP2004: reg 0xF0 = 0x%02x\n", val);

	ret = tdp2004_read_reg(priv, 0xF1, &val);
	if (ret) {
		dev_err(&client->dev,
			"TDP2004: presence probe read reg 0xF1 failed\n");
		return ret;
	}
	dev_dbg(&client->dev, "TDP2004: reg 0xF1 = 0x%02x\n", val);

	/* Configuration writes */
	ret = tdp2004_write_reg(priv, 0x83, 0x07);
	if (ret) {
		dev_err(&client->dev,
			"TDP2004: failed to write reg 0x83\n");
		return ret;
	}

	ret = tdp2004_write_reg(priv, 0x84, 0x04);
	if (ret) {
		dev_err(&client->dev,
			"TDP2004: failed to write reg 0x84\n");
		return ret;
	}

	dev_info(&client->dev, "TDP2004: initialised successfully\n");
	return 0;
}

static int tdp2004_probe(struct i2c_client *client)
{
	struct tdp2004 *priv;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->regmap = devm_regmap_init_i2c(client, &tdp2004_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "TDP2004: failed to init regmap\n");
		return PTR_ERR(priv->regmap);
	}

	i2c_set_clientdata(client, priv);
	dev_info(&client->dev, "TDP2004: probed at 0x%02x\n", client->addr);
	return 0;
}

static const struct of_device_id tdp2004_of_match[] = {
	{ .compatible = "xlnx,tdp2004" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, tdp2004_of_match);

static const struct i2c_device_id tdp2004_id[] = {
	{ "tdp2004", 0 },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(i2c, tdp2004_id);

static struct i2c_driver tdp2004_i2c_driver = {
	.driver = {
		.name           = "tdp2004",
		.of_match_table = tdp2004_of_match,
	},
	.probe    = tdp2004_probe,
	.id_table = tdp2004_id,
};

int tdp2004_entry(void)
{
	return i2c_add_driver(&tdp2004_i2c_driver);
}

void tdp2004_exit(void)
{
	i2c_del_driver(&tdp2004_i2c_driver);
}
