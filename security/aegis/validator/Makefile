
obj-$(CONFIG_SECURITY_AEGIS_VALIDATOR) += aegis_validator.o

aegis_validator-y := \
			fs.o \
			cache.o \
			enforce.o \
			hashlist.o \
			modlist.o \
			verify.o \
			validator.o

aegis_validator-$(CONFIG_SECURITY_AEGIS_RESTOK) += sidcheck.o

aegis_validator-$(CONFIG_OMAP_SEC) += platsec.o

EXTRA_CFLAGS += -Isecurity/aegis/validator/ -I.

