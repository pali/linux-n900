#
# Makefile for the kernel multimedia device drivers.
#


media-objs	:= media-device.o media-devnode.o media-entity.o

obj-$(CONFIG_MEDIA_SUPPORT)	+= media.o

obj-y += common/ video/

obj-$(CONFIG_VIDEO_DEV)		+= radio/
obj-$(CONFIG_DVB_CORE)		+= dvb/
