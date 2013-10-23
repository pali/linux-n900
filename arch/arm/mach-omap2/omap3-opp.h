#ifndef __OMAP3_OPP_H_
#define __OMAP3_OPP_H_

#include <plat/omap-pm.h>

/**
 * omap3_pm_init_opp_table - OMAP opp table lookup called after cpu is detected.
 * Initialize the basic opp table here, board files could choose to modify opp
 * table after the basic initialization
 */
#ifdef CONFIG_CPU_FREQ
extern int omap3_pm_init_opp_table(void);
#else
static inline int omap3_pm_init_opp_table(void)
{
	return -EINVAL;
}
#endif

#endif
