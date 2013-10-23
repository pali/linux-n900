#
# Makefile for the linux kernel.
#

# Common support
obj-y := common.o sram.o clock.o devices.o dma.o mux.o gpio.o \
	 usb.o fb.o io.o
obj-m :=
obj-n :=
obj-  :=

# OCPI interconnect support for 1710, 1610 and 5912
obj-$(CONFIG_ARCH_OMAP16XX) += ocpi.o

# OPP support in (OMAP3+ only at the moment)
# XXX The OPP TWL/TPS code should only be included when a TWL/TPS
# PMIC is selected.
ifdef CONFIG_CPU_FREQ
obj-$(CONFIG_ARCH_OMAP3) += opp.o opp_twl_tps.o
endif

# omap_device support (OMAP2+ only at the moment)
obj-$(CONFIG_ARCH_OMAP2) += omap_device.o
obj-$(CONFIG_ARCH_OMAP3) += omap_device_hack.o omap_device.o

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o
obj-$(CONFIG_OMAP_IOMMU) += iommu.o iovmm.o
obj-$(CONFIG_OMAP_IOMMU_DEBUG) += iommu-debug.o

obj-$(CONFIG_CPU_FREQ) += cpu-omap.o
obj-$(CONFIG_OMAP_DM_TIMER) += dmtimer.o
obj-$(CONFIG_OMAP_DEBUG_DEVICES) += debug-devices.o
obj-$(CONFIG_OMAP_DEBUG_LEDS) += debug-leds.o
i2c-omap-$(CONFIG_I2C_OMAP) := i2c.o
obj-y += $(i2c-omap-m) $(i2c-omap-y)

# OMAP mailbox framework
obj-$(CONFIG_OMAP_MBOX_FWK) += mailbox.o

obj-$(CONFIG_OMAP_PM_NOOP) += omap-pm-noop.o
obj-$(CONFIG_OMAP_PM_SRF) += omap-pm-srf.o \
				resource.o

# HS OMAP
obj-$(CONFIG_OMAP_SEC) += omap_sec.o
omap_sec-objs += pa.o sec.o

