#
# Makefile for SSI drivers
#
EXTRA_CFLAGS := \
        -I$(src)/../../../arch/arm/plat-omap/include \
        -I$(src)/../../../include

omap_ssi-objs := 	ssi_driver.o ssi_driver_dma.o ssi_driver_int.o \
			ssi_driver_if.o ssi_driver_bus.o ssi_driver_gpio.o

ifeq ($(CONFIG_DEBUG_FS), y)
        omap_ssi-objs += ssi_driver_debugfs.o
endif

obj-$(CONFIG_OMAP_SSI)	+= omap_ssi.o
