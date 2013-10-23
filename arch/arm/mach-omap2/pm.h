/*
 * OMAP2/3 Power Management Routines
 *
 * Copyright (C) 2008 Nokia Corporation
 * Jouni Hogander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_PM_H
#define __ARCH_ARM_MACH_OMAP2_PM_H

#include <plat/powerdomain.h>
#include <plat/opp.h>

extern u32 enable_off_mode;
extern u32 sleep_while_idle;
extern u32 voltage_off_while_idle;
extern u32 disable_core_off;

extern void *omap3_secure_ram_storage;
extern void omap3_pm_off_mode_enable(int);
extern void omap_sram_idle(void);
extern int omap3_can_sleep(void);
extern int set_pwrdm_state(struct powerdomain *pwrdm, u32 state);
extern int omap3_idle_init(void);
extern void vfp_pm_save_context(void);
extern void pm_wdt_ping(unsigned timeout);
/*
 * ROM code has a bug that it is able to save secure ram only in
 * addresses in the range [0x8000:0000-0x8FFF:FFFF].
 * So, here are some macros.
 */
#define OMAP3_SECURE_RAM_LOW_MARK	0x80000000
#define OMAP3_SECURE_RAM_HIGH_MARK	0x8FFFFFFF
#define valid_secure_ram_address(a)					\
	(((a) >= OMAP3_SECURE_RAM_LOW_MARK) && 				\
	((a) < (OMAP3_SECURE_RAM_HIGH_MARK - OMAP_SECURE_COPY_AREA_SIZE)))
#ifdef CONFIG_PM
extern void pm_alloc_secure_ram(void);
#else
static inline void pm_alloc_secure_ram(void) { }
#endif

struct cpuidle_params {
	u8  valid;
	u32 sleep_latency;
	u32 wake_latency;
	u32 threshold;
};

#ifdef CONFIG_CPU_IDLE
extern void omap3_pm_init_cpuidle(struct cpuidle_params *cpuidle_board_params);
#else
static inline void omap3_pm_init_cpuidle(
			struct cpuidle_params *cpuidle_board_params)
{
}
#endif

extern int resource_set_opp_level(int res, u32 target_level, int flags);
extern int resource_access_opp_lock(int res, int delta);
#define resource_lock_opp(res) resource_access_opp_lock(res, 1)
#define resource_unlock_opp(res) resource_access_opp_lock(res, -1)
#define resource_get_opp_lock(res) resource_access_opp_lock(res, 0)

#define OPP_IGNORE_LOCK 0x1

extern int omap3_pm_get_suspend_state(struct powerdomain *pwrdm);
extern int omap3_pm_set_suspend_state(struct powerdomain *pwrdm, int state);
extern int omap3_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst);
extern int omap3_pwrdm_read_next_pwrst(struct powerdomain *pwrdm);
extern int program_vdd2_opp_dll_wa(struct omap_opp *min_opp, struct omap_opp *max_opp);
extern int reprogram_vdd2_opp_dll_wa(struct omap_opp *min_opp, struct omap_opp *max_opp);

extern u32 resume_action;

extern u32 wakeup_timer_seconds;
extern u32 wakeup_timer_milliseconds;
extern struct omap_dm_timer *gptimer_wakeup;

#ifdef CONFIG_PM_DEBUG
extern void omap2_pm_dump(int mode, int resume, unsigned int us);
extern int omap2_pm_debug;
#else
#define omap2_pm_dump(mode, resume, us)		do {} while (0);
#define omap2_pm_debug				0
#endif

#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
extern void pm_dbg_update_time(struct powerdomain *pwrdm, int prev);
extern int pm_dbg_regset_save(int reg_set);
extern int pm_dbg_regset_init(int reg_set);
#else
#define pm_dbg_update_time(pwrdm, prev) do {} while (0);
#define pm_dbg_regset_save(reg_set) do {} while (0);
#define pm_dbg_regset_init(reg_set) do {} while (0);
#endif /* CONFIG_PM_DEBUG */

extern void omap24xx_idle_loop_suspend(void);

extern void omap24xx_cpu_suspend(u32 dll_ctrl, void __iomem *sdrc_dlla_ctrl,
					void __iomem *sdrc_power);
extern void omap34xx_cpu_suspend(u32 *addr, int save_state);
extern void save_secure_ram_context(u32 *addr);
extern void omap3_save_scratchpad_contents(void);
extern int omap3_pm_force_cpuidle_state(int);
extern unsigned int *omap3_get_sdrc_counters(void);
extern int omap3_check_secure_ram_dirty(void);

extern unsigned int omap24xx_idle_loop_suspend_sz;
extern unsigned int omap34xx_suspend_sz;
extern unsigned int save_secure_ram_context_sz;
extern unsigned int omap24xx_cpu_suspend_sz;
extern unsigned int omap34xx_cpu_suspend_sz;

#endif
