#ifndef __ARCH_ARM_MACH_OMAP3_TWL4030_SCRIPT_H
#define __ARCH_ARM_MACH_OMAP3_TWL4030_SCRIPT_H

#include <linux/i2c/twl.h>
#include "voltage.h"

#ifdef CONFIG_TWL4030_POWER
/* Info for enabling SR in T2/gaia. */
#define PHY_TO_OFF_PM_RECIEVER(p)	(p - 0x5b)
#define R_DCDC_GLOBAL_CFG	PHY_TO_OFF_PM_RECIEVER(0x61)
/* R_DCDC_GLOBAL_CFG register, SMARTREFLEX_ENABLE values */
#define DCDC_GLOBAL_CFG_ENABLE_SRFLX	0x08

extern void __init twl4030_get_scripts
			(struct twl4030_power_data *t2scripts_data);
extern void twl4030_get_vc_timings(struct prm_setup_vc *setup_vc);
#else
extern void __init twl4030_get_scripts
			(struct twl4030_power_data *t2scripts_data) {}
extern void twl4030_get_vc_timings(struct prm_setup_vc *setup_vc) {}
#endif

#endif
