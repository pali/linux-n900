/*
 * drivers/media/video/smiapp-power.c
 *
 * The SMIA++ spec dictates that a there is a 10 ms delay between poweroff
 * and poweron. Because the sensor and lens are powered by the same power
 * rail but are driven by separate drivers, this cannot be ensured.
 *
 * This driver implements a set_power function that is used by those drivers
 * and delays poweron if necessary.
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Antti Koskipaa <antti.koskipaa@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <media/smiapp-power.h>

#define SMIAPP_POWER_DELAY 10  /* ms */

static struct {
	bool running; /* Is delay ongoing? */
	struct timespec ts; /* When was the last poweroff? */
	struct mutex lock;
} smiapp_power_timer;

int smiapp_power_set_power(struct regulator *reg, int on)
{
	struct timespec ts;
	int rval;

	mutex_lock(&smiapp_power_timer.lock);
	if (on) {
		/* Is it off while timer is running? */
		if (!regulator_is_enabled(reg) && smiapp_power_timer.running) {
			ktime_get_ts(&ts);
			ts = timespec_sub(ts, smiapp_power_timer.ts);
			/* Sleep the remainder of the delay if necessary */
			if (ts.tv_sec == 0 &&
			    ts.tv_nsec < SMIAPP_POWER_DELAY * NSEC_PER_MSEC)
				schedule_timeout(msecs_to_jiffies(
					SMIAPP_POWER_DELAY -
					ts.tv_nsec / NSEC_PER_MSEC));
			smiapp_power_timer.running = false;
		}
		rval = regulator_enable(reg);
	} else {
		rval = regulator_disable(reg);
		/* Was it the last one on? */
		if (!regulator_is_enabled(reg)) {
			/* Restart timer */
			ktime_get_ts(&smiapp_power_timer.ts);
			smiapp_power_timer.running = true;
		}
	}
	mutex_unlock(&smiapp_power_timer.lock);

	return rval;
}
EXPORT_SYMBOL_GPL(smiapp_power_set_power);

static int __init smiapp_power_init(void)
{
	mutex_init(&smiapp_power_timer.lock);
	smiapp_power_timer.running = false;
	return 0;
}

static void __exit smiapp_power_exit(void)
{
}

module_init(smiapp_power_init);
module_exit(smiapp_power_exit);

MODULE_AUTHOR("Antti Koskipaa <antti.koskipaa@nokia.com>");
MODULE_DESCRIPTION("SMIA++ power handler");
MODULE_LICENSE("GPL");
