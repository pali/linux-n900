#
# Makefile for the linux kernel.
#

# Common support
obj-y := id.o io.o control.o mux.o devices.o serial.o gpmc.o timer-gp.o

omap-2-3-common				= irq.o sdrc.o
omap-3-4-common				= dpll.o

hwmod-common                            = omap_hwmod.o \
					  omap_hwmod_common_data.o

prcm-common				= prcm.o powerdomain.o
clock-common				= clock.o clock_common_data.o clockdomain.o

obj-$(CONFIG_ARCH_OMAP2) += $(omap-2-3-common) $(prcm-common) $(clock-common) \
			    $(hwmod-common)
obj-$(CONFIG_ARCH_OMAP3) += $(omap-2-3-common) $(prcm-common) $(clock-common) \
			    $(omap-3-4-common) $(hwmod-common)
obj-$(CONFIG_ARCH_OMAP4) += $(omap-3-4-common) prcm.o clock.o

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o

# SMP support ONLY available for OMAP4
obj-$(CONFIG_SMP)			+= omap-smp.o omap-headsmp.o
obj-$(CONFIG_LOCAL_TIMERS)		+= timer-mpu.o

# Functions loaded to SRAM
obj-$(CONFIG_ARCH_OMAP2420)		+= sram242x.o
obj-$(CONFIG_ARCH_OMAP2430)		+= sram243x.o
obj-$(CONFIG_ARCH_OMAP3)		+= sram34xx.o

# SMS/SDRC
obj-$(CONFIG_ARCH_OMAP2)		+= sdrc2xxx.o
# obj-$(CONFIG_ARCH_OMAP3)		+= sdrc3xxx.o

# Power Management
ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_ARCH_OMAP2)		+= pm24xx.o
obj-$(CONFIG_ARCH_OMAP24XX)		+= sleep24xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= pm.o pm34xx.o sleep34xx.o cpuidle34xx.o \
					   voltage.o
obj-$(CONFIG_PM_DEBUG)			+= pm-debug.o
obj-$(CONFIG_OMAP_SMARTREFLEX)  	+= sr_device.o smartreflex.o
obj-$(CONFIG_OMAP_SMARTREFLEX_CLASS3)   += smartreflex-class3.o
obj-$(CONFIG_OMAP_SMARTREFLEX_CLASS1P5)	+= smartreflex-class1p5.o
endif

# CPU Frequency
ifeq ($(CONFIG_CPU_FREQ),y)
obj-$(CONFIG_ARCH_OMAP3)		+= cpufreq34xx.o
endif

# PRCM
obj-$(CONFIG_ARCH_OMAP2)		+= cm.o
obj-$(CONFIG_ARCH_OMAP3)		+= cm.o
obj-$(CONFIG_ARCH_OMAP4)		+= cm4xxx.o

# Clock framework
obj-$(CONFIG_ARCH_OMAP2)		+= clock2xxx.o clock2xxx_data.o
obj-$(CONFIG_ARCH_OMAP2420)		+= opp2420_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock34xx.o clock34xx_data.o \
					   clk-tracer.o pm-optimizer.o
obj-$(CONFIG_ARCH_OMAP2430)		+= opp2430_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= clock44xx.o clock44xx_data.o
obj-$(CONFIG_OMAP_PM_SRF)		+=  resource34xx.o

obj-$(CONFIG_OMAP_MBOX_FWK)		+= mailbox_mach.o
mailbox_mach-objs			:= mailbox.o

# hwmod data
obj-$(CONFIG_ARCH_OMAP2420)             += omap_hwmod_2420_data.o
obj-$(CONFIG_ARCH_OMAP2430)             += omap_hwmod_2430_data.o
obj-$(CONFIG_ARCH_OMAP3)                += omap_hwmod_3xxx_data.o

# EMU peripherals
obj-$(CONFIG_OMAP3_EMU)			+= emu.o

iommu-y					+= iommu2.o
iommu-$(CONFIG_ARCH_OMAP3)		+= omap3-iommu.o

obj-$(CONFIG_OMAP_IOMMU)		+= $(iommu-y)

ifneq ($(CONFIG_MPU_BRIDGE),)
obj-y					+= dspbridge.o
endif

# Debobs
obj-$(CONFIG_OMAP3_DEBOBS)	+= debobs.o

omap-ssi-$(CONFIG_OMAP_SSI)		:= ssi.o
obj-y					+= $(omap-ssi-m) $(omap-ssi-y)

# DSS
omap-dss-$(CONFIG_OMAP2_DSS)		:= dss.o
obj-y					+= $(omap-dss-m) $(omap-dss-y)

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OMAP_LDP)		+= board-ldp.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OVERO)		+= board-overo.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OMAP3EVM)		+= board-omap3evm.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OMAP3_PANDORA)	+= board-omap3pandora.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OMAP_3430SDP)		+= board-3430sdp.o \
					   hsmmc.o \
					   twl4030.o
obj-$(CONFIG_MACH_NOKIA_N8X0)		+= board-n8x0.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51.o \
					   sdram-nokia.o \
					   board-rx51-peripherals.o \
					   nokia-bootparams.o \
					   board-gpio-export.o \
					   hsmmc.o
obj-$(CONFIG_MACH_NOKIA_RX71)		+= board-rx71.o \
					   sdram-nokia.o \
					   board-rx71-peripherals.o \
					   board-omap-bt.o \
					   board-rx71-video.o \
					   nokia-bootparams.o \
					   board-gpio-export.o \
					   board-rx71-audio.o \
					   hsmmc.o \
					   nokia-twl4030.o \
					   twl4030.o \
					   atmel_mxt_config.o \
					   board-rx71-camera.o
obj-$(CONFIG_MACH_NOKIA_RM680)		+= board-rm680.o \
					   sdram-nokia.o \
					   board-rm680-peripherals.o \
					   board-omap-bt.o \
					   board-rm680-video.o \
					   nokia-bootparams.o \
					   board-gpio-export.o \
					   board-rm680-audio.o \
					   hsmmc.o \
					   nokia-twl4030.o \
					   twl4030.o \
					   atmel_mxt_config.o \
					   board-rm680-camera.o
obj-$(CONFIG_MACH_NOKIA_RM696)		+= board-rm696.o \
					   sdram-nokia.o \
					   board-rm696-peripherals.o \
					   board-omap-bt.o \
					   board-rm696-video.o \
					   nokia-bootparams.o \
					   board-gpio-export.o \
					   board-rm696-audio.o \
					   hsmmc.o \
					   twl4030.o \
					   board-rm696-camera.o
obj-$(CONFIG_MACH_OMAP_ZOOM2)		+= board-zoom2.o \
					   board-zoom-peripherals.o \
					   hsmmc.o \
					   board-zoom-debugboard.o
obj-$(CONFIG_MACH_OMAP_ZOOM3)		+= board-zoom3.o \
					   board-zoom-peripherals.o \
					   hsmmc.o \
					   board-zoom-debugboard.o
obj-$(CONFIG_MACH_OMAP_3630SDP)		+= board-3630sdp.o \
					   board-zoom-peripherals.o \
					   hsmmc.o \
					   twl4030.o
obj-$(CONFIG_MACH_CM_T35)		+= board-cm-t35.o \
					   hsmmc.o
obj-$(CONFIG_MACH_IGEP0020)		+= board-igep0020.o \
					   hsmmc.o

obj-$(CONFIG_MACH_OMAP_4430SDP)		+= board-4430sdp.o

obj-$(CONFIG_MACH_OMAP3517EVM)     += board-am3517evm.o

# Platform specific device init code
obj-y					+= usb-musb.o
obj-$(CONFIG_MACH_OMAP2_TUSB6010)	+= usb-tusb6010.o
obj-y					+= usb-ehci.o

onenand-$(CONFIG_MTD_ONENAND_OMAP2)	:= gpmc-onenand.o
obj-y					+= $(onenand-m) $(onenand-y)

smc91x-$(CONFIG_SMC91X)			:= gpmc-smc91x.o
obj-y					+= $(smc91x-m) $(smc91x-y)

# HS OMAP
obj-$(CONFIG_ARCH_OMAP3_HS)		+= omap_hs.o
omap_hs-objs				+= hs.o sec_hal.o sec_hal_entry.o
