ifeq ($(CONFIG_CRASH_DUMP),y)
zreladdr-y		:= 0x84008000
params_phys-y		:= 0x84000100
else
zreladdr-y		:= 0x80008000
params_phys-y		:= 0x80000100
endif
initrd_phys-y		:= 0x80800000
