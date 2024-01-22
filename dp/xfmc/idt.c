// SPDX-License-Identifier: GPL-2.0
/*
 * IDT Expander driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define IDT_8T49N24X_REVID 0x0    /**< Device Revision */
#define IDT_8T49N24X_DEVID 0x0607 /**< Device ID Code */

#define IDT_8T49N24X_XTAL_FREQ 40000000  //The frequency of the crystal in Hz

#define IDT_8T49N24X_FVCO_MAX 4000000000 //Maximum VCO Operating Frequency in Hz
#define IDT_8T49N24X_FVCO_MIN 3000000000 //Minimum VCO Operating Frequency in Hz

#define IDT_8T49N24X_FOUT_MAX 400000000  //Maximum Output Frequency in Hz
#define IDT_8T49N24X_FOUT_MIN      8000  //Minimum Output Frequency in Hz

#define IDT_8T49N24X_FIN_MAX 875000000  //!< Maximum Input Frequency in Hz
#define IDT_8T49N24X_FIN_MIN      8000  //!< Minimum Input Frequency in Hz

#define IDT_8T49N24X_FPD_MAX 8000000  //Maximum Phase Detector Frequency in Hz
#define IDT_8T49N24X_FPD_MIN   8000  //!< Minimum Phase Detector Frequency in Hz

#define IDT_8T49N24X_P_MAX pow(2, 22)  //!< Maximum P divider value
#define IDT_8T49N24X_M_MAX pow(2, 24)  //!< Maximum M multiplier value

#define TRUE 1
#define FALSE 0
#define XPAR_IIC_0_BASEADDR 0xA0080000
#define I2C_IDT8N49_ADDR 0x7C
/*
 *  This configuration was created with the IDT timing commander.
 *  IT configures the clock device in synthesizer mode.
 *  It produces a 148.5 MHz free running clock on outputs Q2 and Q3.
 *
 */
static const u8 idt_8T49n24x_config_syn[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xEF, 0x00, 0x03, 0x00, 0x31, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x07, 0x00, 0x00, 0x77,
	0x6D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
	0x3F, 0x00, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00,
	0x00, 0x44, 0x44, 0x01, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06,
	0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x0A, 0x2B, 0x20,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x27, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xB0
};

static const u8 idt_8T49n24x_config_ja[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xEF, 0x00, 0x03, 0x00,
	0x20, 0x00, 0x04, 0x89, 0x00, 0x00, 0x01, 0x00, 0x63, 0xC6,
	0x07, 0x00, 0x00, 0x77, 0x6D, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x3F, 0x00, 0x28, 0x00,
	0x1A, 0xCC, 0xCD, 0x00, 0x01, 0x00, 0x00, 0xD0, 0x08, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x0C, 0x00, 0x00,
	0x00, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0B, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x89, 0x02, 0x2B, 0x20, 0x00, 0x00,
	0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

static const u8 idt_8T49n24x_config_xx[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xEF, 0x00, 0x03, 0x00,
	0x20, 0x00, 0x02, 0x45, 0x00, 0x00, 0x01, 0x00, 0x5F, 0x52,
	0x07, 0x00, 0x00, 0x77, 0x6D, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x3F, 0x00, 0x26, 0x00,
	0x1F, 0x66, 0x66, 0x00, 0x01, 0x00, 0x00, 0xD0, 0x08, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00,
	0x00, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x89, 0x02, 0x2B, 0x20, 0x00, 0x00,
	0x00, 0x07, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

/**************************** Type Definitions *******************************/

struct reg_8 {
	u16 addr;
	u8 val;
};

static const struct regmap_config idt_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * struct idt - idt device structure
 * @client: Pointer to I2C client
 * @ctrls: idt control structure
 * @regmap: Pointer to regmap structure
 * @lock: Mutex structure
 * @mode_index: Resolution mode index
 */
struct idts {
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex lock; /* mutex lock for operations */
	u32 mode_index;
};

struct idts *idt;

/*
 * Function declaration
 */
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

static inline int idt_read_reg(struct idts *priv, u8 addr, u8 *val)
{
	int err = 0;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_dbg(&idt->client->dev,
			"i2c read failed, addr = %x\n", addr);
	return err;
}

static inline int idt_write_reg(struct idts *priv, u16 addr, u8 val)
{
	int err = 0;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_dbg(&idt->client->dev,
			"i2c write failed, addr = %x\n", addr);

	return err;
}

static int idt_enable(u8 enable)
{
	int ret = 0;

	if (enable) {
		ret = idt_write_reg(idt, 0x0070, 5);
		msleep_range(30);

	} else {
		ret = idt_write_reg(idt, 0x0070, 0);
		msleep_range(30);
	}
	return ret;
}

static int idt_8T49n24x_configure_ja(u32 i2c_base_addr, u8 i2c_slave_addr)
{
	int ret = 0;
	u32 index;

	for (index = 8; index < sizeof(idt_8T49n24x_config_ja); index++) {
		msleep_range(10);
		if (index != 0x070)
			ret = idt_write_reg(idt, index,
					    idt_8T49n24x_config_ja[index]);
	}
	return ret;
}

/***************************************************************************/
/**
 * *
 * * This function the IDT 8TN49N24x device with the data from the
 * * configuration table.
 * *
 * * @param i2c_base_addr is the baseaddress of the I2C core.
 * * @param i2c_slave_addr is the 7-bit I2C slave address.
 * *
 * * @return None
 * *
 * * @note None.
 * *
 * **************************************************************************/

int IDT_8T49N24x_Configure(void)
{
	int ret = 0;
	u32 index;

	for (index = 8; index < sizeof(idt_8T49n24x_config_syn); index++) {
		if (index != 0x070) {
			ret = idt_write_reg(idt, index,
					    idt_8T49n24x_config_syn[index]);
			msleep_range(10);
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(IDT_8T49N24x_Configure);

int IDT_8T49N24x_SetClock(void)
{
	int ret;

	ret = idt_write_reg(idt, 0x0070, 0x5);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n",  __LINE__,  __func__);
	msleep_range(20);

	ret = idt_write_reg(idt, 0x000a, 0x30);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x000a, 0x30);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret  = idt_write_reg(idt, 0x000a, 0x31);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0069, 0x0a);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x000b, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x000c, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x000d, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x000e, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x000f, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0010, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0014, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0015, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0016, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0011, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0012, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0013, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0025, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0026, 0x28);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0028, 0x10);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0029, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x002a, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0045, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);

	msleep_range(10);

	ret = idt_write_reg(idt, 0x0046, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0047, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0048, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0049, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x004a, 0x6);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x005b, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);

	msleep_range(10);

	ret = idt_write_reg(idt, 0x005c, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x005d, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x005e, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x005f, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0060, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0061, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0062, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	ret = idt_write_reg(idt, 0x0070, 0x0);
	if (ret)
		dev_dbg(&idt->client->dev,
			"%d %s i2c write failed\n", __LINE__, __func__);
	msleep_range(10);

	return ret;
}
EXPORT_SYMBOL_GPL(IDT_8T49N24x_SetClock);

int IDT_8T49N24x_Init(void)
{
	int ret = 0;

	msleep_range(30);
	ret = idt_enable(FALSE);
	if (ret)
		dev_dbg(&idt->client->dev,
			"IDT_8T49N24x_enable 0  I2C progmming failed\n");
	msleep_range(30);
	/* Configure device. */
	ret = idt_8T49n24x_configure_ja(XPAR_IIC_0_BASEADDR, I2C_IDT8N49_ADDR);
	if (ret)
		dev_dbg(&idt->client->dev,
			"idt_8T49n24x_configure_ja I2C progmming failed\n");

	msleep_range(30);
	/* enable DPLL and APLL calibration. */
	ret = idt_enable(TRUE);
	if (ret)
		dev_dbg(&idt->client->dev,
			"IDT_8T49N24x_enable 1 I2C progmming failed\n");
	msleep_range(30);
	return 0;
}
EXPORT_SYMBOL_GPL(IDT_8T49N24x_Init);

static const struct of_device_id idt_of_id_table[] = {
	{ .compatible = "expander-idt" },
	{ }
};
MODULE_DEVICE_TABLE(of, idt_of_id_table);

static const struct i2c_device_id idt_id[] = {
	{ "IDT", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, idt_id);

static int idt_probe(struct i2c_client *client)
{
	int ret;

	/* initialize idt */
	idt = devm_kzalloc(&client->dev, sizeof(*idt), GFP_KERNEL);
	if (!idt)
		return -ENOMEM;

	mutex_init(&idt->lock);

	/* initialize regmap */
	idt->regmap = devm_regmap_init_i2c(client, &idt_regmap_config);
	if (IS_ERR(idt->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(idt->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	dev_info(&client->dev, "idt : probe success !\n");
	return 0;

err_regmap:
	mutex_destroy(&idt->lock);
	return ret;
}

static void idt_remove(struct i2c_client *client)
{
}

static struct i2c_driver idt_i2c_driver = {
	.driver = {
		.name	= "idt",
		.of_match_table	= idt_of_id_table,
	},
	.probe		= idt_probe,
	.remove		= idt_remove,
	.id_table	= idt_id,
};

void idt_exit(void)
{
	i2c_del_driver(&idt_i2c_driver);
}
EXPORT_SYMBOL_GPL(idt_exit);

int idt_init(void)
{
	return i2c_add_driver(&idt_i2c_driver);
}
EXPORT_SYMBOL_GPL(idt_init);

MODULE_AUTHOR("Rajesh Gugulothu <gugulot@xilinx.com>");
MODULE_DESCRIPTION("IDT Expander driver");
MODULE_LICENSE("GPL v2");
