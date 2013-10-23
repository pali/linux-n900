#ifndef __ARCH_ARM_MACH_OMAP2_PM_H
#define __ARCH_ARM_MACH_OMAP2_PM_H
/*
 * linux/arch/arm/mach-omap2/pm.h
 *
 * OMAP Power Management Routines
 *
 * Copyright (C) 2008 Nokia Corporation
 * Jouni Hogander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <mach/powerdomain.h>

extern int omap2_pm_init(void);
extern int omap3_pm_init(void);

#ifdef CONFIG_CPU_IDLE
int omap3_idle_init(void);
#else
static inline int omap3_idle_init(void) { return 0; }
#endif

extern unsigned short enable_dyn_sleep;
extern unsigned short clocks_off_while_idle;
extern unsigned short enable_off_mode;
extern unsigned short voltage_off_while_idle;
extern atomic_t sleep_block;
extern void *omap3_secure_ram_storage;

extern void omap2_block_sleep(void);
extern void omap2_allow_sleep(void);
#ifdef CONFIG_ARCH_OMAP3
struct prm_setup_times {
	u16 clksetup;
	u16 voltsetup_time1;
	u16 voltsetup_time2;
	u16 voltoffset;
	u16 voltsetup2;
};

extern void omap3_pm_off_mode_enable(int);
extern int omap3_pm_get_suspend_state(struct powerdomain *pwrdm);
extern int omap3_pm_set_suspend_state(struct powerdomain *pwrdm, int state);
extern void omap3_set_prm_setup_times(struct prm_setup_times *setup_times);
#else
#define omap3_pm_off_mode_enable(int) do {} while (0);
#define omap3_pm_get_suspend_state(pwrdm) do {} while (0);
#define omap3_pm_set_suspend_state(pwrdm, state) do {} while (0);
#endif
extern int set_pwrdm_state(struct powerdomain *pwrdm, u32 state);
extern int resource_set_opp_level(int res, u32 target_level, int flags);
extern int resource_access_opp_lock(int res, int delta);
#define resource_lock_opp(res) resource_access_opp_lock(res, 1)
#define resource_unlock_opp(res) resource_access_opp_lock(res, -1)
#define resource_get_opp_lock(res) resource_access_opp_lock(res, 0)

#define OPP_IGNORE_LOCK 0x1

#ifdef CONFIG_PM_DEBUG
extern void omap2_pm_dump(int mode, int resume, unsigned int us);
extern int omap2_pm_debug;
extern void pm_dbg_update_time(struct powerdomain *pwrdm, int prev);
extern int pm_dbg_regset_save(int reg_set);
extern int pm_dbg_regset_init(int reg_set);
#else
#define omap2_pm_dump(mode, resume, us)		do {} while (0);
#define omap2_pm_debug				0
#define pm_dbg_update_time(pwrdm, prev) do {} while (0);
#define pm_dbg_regset_save(reg_set) do {} while (0);
#define pm_dbg_regset_init(reg_set) do {} while (0);
#endif /* CONFIG_PM_DEBUG */
#endif
