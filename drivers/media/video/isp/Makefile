# Makefile for OMAP3 ISP driver

ifdef CONFIG_ARCH_OMAP3410
isp-mod-objs += \
	isp.o ispccdc.o
else
isp-mod-objs += \
	isp.o ispccdc.o \
	isppreview.o ispresizer.o isph3a.o isphist.o isp_af.o ispcsi2.o \
	ispstat.o
endif

obj-$(CONFIG_VIDEO_OMAP3) += isp-mod.o
obj-$(CONFIG_VIDEO_OMAP3) += omap_previewer_hack.o