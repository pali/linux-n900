#
# Makefile for SSI protocol modules
#
obj-$(CONFIG_SSI_MCSAAB_IMP)	+= ssi_mcsaab_imp.o

ifeq ($(CONFIG_SSI_MCSAAB_DEBUG),y)
EXTRA_CFLAGS += -DSSI_DEBUG
endif
