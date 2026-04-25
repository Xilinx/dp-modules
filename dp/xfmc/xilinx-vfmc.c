/*
 * GNU General Public License for more details.
 *
 */

/* if both both DEBUG and DEBUG_TRACE are defined, trace_printk() is used */
#define DEBUG
#define DEBUG_TRACE

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <dt-bindings/phy/phy.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include "fmc.h"
#include "xstatus.h"


static void xvfmc_retimer_prbs_mode(struct i2c_client *client, u8 enable)
{
	XDpRxSs_MCDP6000_EnableDisablePrbs7_Rx(client, enable);
	XDpRxSs_MCDP6000_ClearCounter(client);
}

static void xvfmc_retimer_access_laneset(struct i2c_client *client)
{
	mcdp6000_access_laneset_callback(client);
}

static void xvfmc_retimer_rst_dp_path(struct i2c_client *client)
{
	mcdp6000_rst_dp_path_callback(client);
}

static void xvfmc_retimer_rst_cr_path(struct i2c_client *client)
{
	mcdp6000_rst_cr_path_callback(client);
}

static struct i2c_client *resolve_phandle_to_client(struct device_node *parent,
						    const char *prop)
{
	struct device_node *node;
	struct i2c_client  *client = NULL;

	node = of_parse_phandle(parent, prop, 0);
	if (node) {
		client = of_find_i2c_device_by_node(node);
		of_node_put(node);
	}
	return client;
}

int xfmc_init(struct x_vfmc_dev *xfmcdev)
{
	int status = 0;

	if(!i2c_get_clientdata(xfmcdev->mcdp6000_client))
		return XST_DEVICE_NOT_FOUND;

	status |= fmc64_init(xfmcdev->fmc64_client);
	status |= fmc65_init(xfmcdev->fmc65_client);
	status |= IDT_8T49N24x_Init(xfmcdev->idt_client);
	status |= tipower_init(xfmcdev->tipower_client);
	status |= IDT_8T49N24x_SetClock(xfmcdev->idt_client);
	status |= IDT_8T49N24x_Configure(xfmcdev->idt_client);
	status |= mcdp6000_init(xfmcdev->mcdp6000_client);
	status |= dp141_init(xfmcdev->dp141_client);

	return status;
}
EXPORT_SYMBOL_GPL(xfmc_init);

/**
 * xvfmc_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xvfmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct x_vfmc_dev  *xfmcdev;
	struct x_vfmc_cfg  *cfg;
	int status;

	xfmcdev = devm_kzalloc(&pdev->dev, sizeof(*xfmcdev), GFP_KERNEL);
	if (!xfmcdev)
		return -ENOMEM;

	cfg = devm_kzalloc(&pdev->dev, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	xfmcdev->dev = &pdev->dev;

	xfmcdev->fmc64_client    = resolve_phandle_to_client(np, "xlnx,fmc64");
	xfmcdev->fmc65_client    = resolve_phandle_to_client(np, "xlnx,fmc65");
	xfmcdev->idt_client      = resolve_phandle_to_client(np, "xlnx,idt");
	xfmcdev->tipower_client  = resolve_phandle_to_client(np, "xlnx,tipower");
	xfmcdev->mcdp6000_client = resolve_phandle_to_client(np, "xlnx,mcdp6000");
	xfmcdev->dp141_client    = resolve_phandle_to_client(np, "xlnx,dp141");

	if (!xfmcdev->fmc64_client  ||
	    !xfmcdev->fmc65_client || !xfmcdev->idt_client     ||
	    !xfmcdev->tipower_client || !xfmcdev->mcdp6000_client ||
	    !xfmcdev->dp141_client) {
		dev_info(&pdev->dev,
			 "I2C clients not yet probed - deferring\n");
		return -EPROBE_DEFER;
	}

	cfg->mcdp6000_client        = xfmcdev->mcdp6000_client;
	cfg->retimer_rst_dp_path    = xvfmc_retimer_rst_dp_path;
	cfg->retimer_rst_cr_path    = xvfmc_retimer_rst_cr_path;
	cfg->retimer_access_laneset = xvfmc_retimer_access_laneset;
	cfg->retimer_set_prbs_mode  = xvfmc_retimer_prbs_mode;

	platform_set_drvdata(pdev, cfg);

	status = xfmc_init(xfmcdev);
	if (status == XST_DEVICE_NOT_FOUND) {
		dev_err(&pdev->dev, "FMC not found\n");
		dev_err(&pdev->dev, "xilinx-vfmc probe failed with error:%d\n",status);
		return status;

	}else if (status) {
		dev_err(&pdev->dev, "xilinx-vfmc probe failed with error:%d\n",status);
		return status;
	}

	dev_info(&pdev->dev, "xilinx-vfmc probed successfully\n");
	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id xvfmc_of_match[] = {
	{ .compatible = "xilinx-vfmc" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xvfmc_of_match);

static struct platform_driver xvfmc_driver = {
	.probe = xvfmc_probe,
	.driver = {
		.name = "xilinx-vfmc",
		.of_match_table	= xvfmc_of_match,
	},
};

static int __init xvfmc_module_init(void)
{
	int ret;

	ret = fmc64_entry();
	if (ret)
		goto err_fmc64;

	ret = fmc65_entry();
	if (ret)
		goto err_fmc65;

	ret = tipower_entry();
	if (ret)
		goto err_tipower;

	ret = dp141_entry();
	if (ret)
		goto err_dp141;

	ret = mcdp6000_entry();
	if (ret)
		goto err_mcdp6000;

	ret = idt_entry();
	if (ret)
		goto err_idt;

	ret = platform_driver_register(&xvfmc_driver);
	if (ret)
		goto err_platform;

	return 0;

err_platform:
	idt_exit();
err_idt:
	mcdp6000_exit();
err_mcdp6000:
	dp141_exit();
err_dp141:
	tipower_exit();
err_tipower:
	fmc65_exit();
err_fmc65:
	fmc64_exit();
err_fmc64:
	return ret;
}

static void __exit xvfmc_module_exit(void)
{
	platform_driver_unregister(&xvfmc_driver);
	idt_exit();
	mcdp6000_exit();
	dp141_exit();
	tipower_exit();
	fmc65_exit();
	fmc64_exit();
}

module_init(xvfmc_module_init);
module_exit(xvfmc_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_DESCRIPTION("Xilinx Vphy driver");
