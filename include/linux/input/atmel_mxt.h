/*
 * Platform configuration for Atmel mXT touchscreen driver.
 */

#ifndef __ATMEL_MXT_H
#define __ATMEL_MXT_H

struct mxt_obj_config {
	u8 type;
	const u8 *obj_data;
	u8 obj_len;
};

struct mxt_config {
	const struct mxt_obj_config *obj;
	u8 num_objs;

};

struct mxt_platform_data {
	int	reset_gpio;	/* zero == use sw reset */
	int	int_gpio;	/* zero == use msg_id of 255 for end of msg */
	int	wakeup_interval_ms;	/* wait for gesture interval */

	int	rlimit_min_interval_us;	/* movement report rate limiter */
	int	rlimit_bypass_time_us;	/* overrides above if fresh contact */

	const struct mxt_config *config;
};

#endif
