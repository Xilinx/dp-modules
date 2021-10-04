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

#define VERSAL_HPC0	0x4
#define ZYNQMP_HPC0	0x7

int fmc_init(u8 hpc_connector);
int fmc64_init(void);
int fmc65_init(void);
int IDT_8T49N24x_Init(void);
int IDT_8T49N24x_Configure(void);
int tipower_init(void);
int dp141_init(void);
int mcdp6000_init(void);
int idt_init(void);
void idt_exit(void);
int fmc_entry(void);
void fmc_exit(void);

int fmc64_entry(void);
int fmc64_exit(void);

int fmc65_entry(void);
int fmc65_exit(void);

int tipower_entry(void);
void tipower_exit(void);

int dp141_entry(void);
int dp141_exit(void);

int mcdp6000_entry(void);
void mcdp6000_exit(void);

int IDT_8T49N24x_SetClock(void);
int xfmc_init(u8 hpc_connector);

struct x_vfmc_dev {
	struct device *dev;
};

int xfmc_init(u8 hpc_connector)
{
	unsigned int Status=1;

	/* Platform Initialization */
	fmc_entry();
	fmc64_entry();
	fmc65_entry();
	tipower_entry();
	dp141_entry();
	mcdp6000_entry();
	Status = fmc_init(hpc_connector);
	if(Status)
		printk("vphy: @75 selection HPC FMC failed\n");
	Status = fmc64_init();
        if(Status)
                printk("vphy: @64 Configure VFMC IO Expander 0 failed\n");
	Status = fmc65_init();
	if(Status)
		printk("vphy: @65 Configure VFMC IO Expander 1 failed\n");

	idt_init();
	Status = IDT_8T49N24x_Init();
	if(Status)
		printk("vphy: @7C IDT init failed\n");

	Status = tipower_init();
	if(Status)
		printk("vphy: @50  TI POWER config failed\n");

	Status = IDT_8T49N24x_SetClock();
	if(Status)
		printk("vphy: @7C IDT set clock failed\n");

	Status = IDT_8T49N24x_Configure();
	if(Status)
	printk("vphy: @7C IDT configure failed\n");
			Status = mcdp6000_init();
        if(Status)
                printk("vphy: @14  MCDP6000 init failed\n");

	Status = dp141_init();
	if(Status)
		printk("vphy: @05  dp141 config failed\n");


	return 0;
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
	int status;
	struct x_vfmc_dev *xfmcdev;
	struct device_node *node = pdev->dev.of_node;
	
	u8 versal_present;

	xfmcdev = devm_kzalloc(&pdev->dev, sizeof(*xfmcdev), GFP_KERNEL);
	if (!xfmcdev)
		return -ENOMEM;	
	xfmcdev->dev = &pdev->dev;

	versal_present =
		of_property_read_bool(node, "xlnx,versal");
	
	if (versal_present)
		status = xfmc_init(VERSAL_HPC0);
	else
		status = xfmc_init(ZYNQMP_HPC0);
	
	if (status)
		dev_err(xfmcdev->dev,
			"Xilinx Video FMC initialization failed\n");
	
	platform_set_drvdata(pdev, xfmcdev);

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
module_platform_driver(xvfmc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_DESCRIPTION("Xilinx Vphy driver");
