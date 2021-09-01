# mainlining: remove CFLAGS<...> += -I$(src)/include
# see below for further mainlining hints

# in same cases only used to check against presence, i.e. >0, actual number is not used
ccflags-y := -DXPAR_XVPHY_NUM_INSTANCES=1
ccflags-y += -DXPAR_XVTC_NUM_INSTANCES=1
ccflags-y += -DXPAR_VPHY_0_TRANSCEIVER=5
ccflags-y += -DXPAR_VPHY_0_RX_PROTOCOL=0
ccflags-y += -DXPAR_VPHY_0_TX_PROTOCOL=0

ccflags-y += -DXPAR_XDP_0_DEVICE_ID=0
ccflags-y += -DXPAR_XDP_0_NUM_INSTANCES=0
ccflags-y += -DXPAR_XDP_0_DEVICE_ID=XPAR_DP_RX_HIER_0_V_DP_RXSS1_0_DP_DEVICE_ID
ccflags-y += -DXPAR_VPHY_0_TX_NO_OF_CHANNELS=4
ccflags-y += -DXPAR_VPHY_0_RX_NO_OF_CHANNELS=4
ccflags-y += -DXPAR_VPHY_0_TX_REFCLK_SEL=0
ccflags-y += -DXPAR_VPHY_0_RX_REFCLK_SEL=1
ccflags-y += -DXPAR_VPHY_0_TX_PLL_SELECTION=2
ccflags-y += -DXPAR_VPHY_0_RX_PLL_SELECTION=0
ccflags-y += -DXPAR_VPHY_0_NIDRU=0
ccflags-y += -DXPAR_VPHY_0_NIDRU_REFCLK_SEL=0
ccflags-y += -DXPAR_VPHY_0_INPUT_PIXELS_PER_CLOCK=4
ccflags-y += -DXPAR_VPHY_0_TX_BUFFER_BYPASS=0
ccflags-y += -DXPAR_VPHY_0_HDMI_FAST_SWITCH=1
ccflags-y += -DXPAR_VPHY_0_TRANSCEIVER_WIDTH=2
ccflags-y += -DXPAR_VPHY_0_ERR_IRQ_EN=0
ccflags-y += -DXPAR_VPHY_0_AXI_LITE_FREQ_HZ=99990000
ccflags-y += -DXPAR_VPHY_0_DRPCLK_FREQ=40000000
ccflags-y += -DXPAR_VPHY_0_USE_GT_CH4_HDMI=0
ccflags-y += -DXPAR_XTMRCTR_NUM_INSTANCES=1

ccflags-y += -I${src}/phy-xilinx-vphy

obj-m += phy-xilinx-vphy.o
# top-level Linux driver source
phy-xilinx-vphy-objs += xfmc/fmc.o 
phy-xilinx-vphy-objs += xfmc/fmc64.o
phy-xilinx-vphy-objs += xfmc/fmc65.o
phy-xilinx-vphy-objs += xfmc/idt.o
phy-xilinx-vphy-objs += xfmc/tipower.o
phy-xilinx-vphy-objs += xfmc/mcdp6000.o
phy-xilinx-vphy-objs += xfmc/dp141.o
phy-xilinx-vphy-objs += phy-vphy.o

# the C API around the IP registers
CFLAGS_phy-vphy.o += -I$(src)/phy-xilinx-vphy/
CFLAGS_phy-vphy.o += -I$(src)/include/

phy-xilinx-vphy-objs += phy-xilinx-vphy/xdebug.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_i.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_gthe4.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_dp.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_intr.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_log.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_sinit.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvidc.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvidc_edid.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvidc_timings_table.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_mmcme2.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_mmcme3.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_mmcme4.o

