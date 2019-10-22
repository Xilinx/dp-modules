# mainlining: remove CFLAGS<...> += -I$(src)/include
# see below for further mainlining hints

# in same cases only used to check against presence, i.e. >0, actual number is not used
ccflags-y := -DXPAR_XVPHY_NUM_INSTANCES=1
ccflags-y += -DXPAR_XVTC_NUM_INSTANCES=1
ccflags-y += -DXPAR_VPHY_0_TRANSCEIVER=5
ccflags-y += -DXPAR_VPHY_0_RX_PROTOCOL=0
ccflags-y += -DXPAR_VPHY_0_TX_PROTOCOL=0

#ccflags-y += -DXPAR_XHDCP_NUM_INSTANCES=4
#ccflags-y += -DXPAR_XHDCP22_RX_NUM_INSTANCES=4
#ccflags-y += -DXPAR_XHDCP22_TX_NUM_INSTANCES=4
#ccflags-y += -DXPAR_XHDCP22_MMULT_NUM_INSTANCES=4
#ccflags-y += -DXPAR_XHDCP22_CIPHER_NUM_INSTANCES=8
#ccflags-y += -DXPAR_XHDCP22_RNG_NUM_INSTANCES=8
#ccflags-y += -DXPAR_XTMRCTR_NUM_INSTANCES=16

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
CFLAGS_xilinx-dprxss.o += -I$(src)/include/ -Idrivers/media/platform/xilinx/

obj-m += phy-xilinx-vphy.o xilinx_dp.o xilinx_dprxss.o
# top-level Linux driver source
#phy-xilinx-vphy-objs := fmc.o fmc64.o fmc65.o idt.o tipower.o mcdp6000.o phy-vphy.o
phy-xilinx-vphy-objs += xfmc/fmc.o 
phy-xilinx-vphy-objs += xfmc/fmc64.o
phy-xilinx-vphy-objs += xfmc/fmc65.o
phy-xilinx-vphy-objs += xfmc/idt.o
phy-xilinx-vphy-objs += xfmc/tipower.o
phy-xilinx-vphy-objs += xfmc/mcdp6000.o
phy-xilinx-vphy-objs += phy-vphy.o

xilinx_dp-objs :=xilinx_drm_dp.o 
xilinx_dprxss-objs :=xilinx-dprxss.o

# the C API around the IP registers
CFLAGS_phy-vphy.o += -I$(src)/phy-xilinx-vphy/
CFLAGS_phy-vphy.o += -I$(src)/include/

phy-xilinx-vphy-objs += phy-xilinx-vphy/xdebug.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_i.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_gthe4.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_dp.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_hdmi.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_hdmi_intr.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_intr.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_log.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_sinit.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvidc.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvidc_edid.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvidc_timings_table.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_mmcme2.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_mmcme3.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xvphy_mmcme4.o

#timer
phy-xilinx-vphy-objs += phy-xilinx-vphy/xtmrctr.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xtmrctr_l.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xtmrctr_sinit.o
phy-xilinx-vphy-objs += phy-xilinx-vphy/xtmrctr_options.o
