#ifndef __ARCH_ARM_MACH_OMAP2_NOKIA_TWL4030_SCRIPT_H
#define __ARCH_ARM_MACH_OMAP2_NOKIA_TWL4030_SCRIPT_H

#include <linux/i2c/twl.h>
#ifdef CONFIG_TWL4030_POWER
extern void __init twl4030_get_nokia_powerdata
		(struct twl4030_power_data *powerdata);
#else
extern void __init twl4030_get_nokia_powerdata
		(struct twl4030_power_data *powerdata) {}
#endif

#endif
