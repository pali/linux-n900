/*
 * HACK driver for bad TWL WDT
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/* THIS DRIVER IS A HACK TO WORKAROUND BADLY CONFIGURED TWL5031 */

static int			bad_wdt;
static struct delayed_work	work;

#define TWL4030_WDT_KICK_PERIOD		(15 * HZ)
#define TWL4030_WDT_MAX			30

/*
 * Read / write functions declared also here to avoid unnecessary
 * modifications to real WD driver
 */
static u8 twl4030_wdt_read(void)
{
	u8 result;
	int ret;
	ret = twl4030_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, &result,
				TWL4030_WATCHDOG_CFG_REG_OFFS);
	if (ret < 0)
		printk(KERN_WARNING "TWL5031 WDT hack i2c failure\n");

	return result;
}

static int twl4030_wdt_write(unsigned char val);

static int twl4030_test_wd(void)
{
	u8 left;

	left = twl4030_wdt_read();
	if (left != 0) {
		/* WD is running. Next try to detect wrong eeprom cfg */
		twl4030_wdt_write(0);
		left = twl4030_wdt_read();

		twl4030_wdt_write(TWL4030_WDT_MAX);

		if (left != 0) {
			printk(KERN_INFO "HACK: TWL5031 WDT can't be disabled. Automatic WDT refresh started\n");
			bad_wdt = 1;
			return -1;
		}
	}
	return 0;
}

static void twl4030_kick_wdt(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);

	twl4030_wdt_write(TWL4030_WDT_MAX);
	schedule_delayed_work(dwork, TWL4030_WDT_KICK_PERIOD);
}

static int hack_twl4030_remove_bad_wd_handler(void)
{
	cancel_delayed_work_sync(&work);
	return 0;
}

static int hack_twl4030_init_bad_wd_handler(void)
{
	INIT_DELAYED_WORK(&work, twl4030_kick_wdt);
	if (twl4030_test_wd() < 0)
		schedule_delayed_work(&work, TWL4030_WDT_KICK_PERIOD);
	return 0;
}

static int hack_twl4030_bad_wd_suspend(void)
{
	if (bad_wdt) {
		/*
		 * In case of bad wdt there is no way to stop it.
		 * Kick it now and give some time for suspend mode
		 */
		cancel_delayed_work_sync(&work);
		twl4030_wdt_write(TWL4030_WDT_MAX);
		schedule_delayed_work(&work, TWL4030_WDT_KICK_PERIOD);
	}
	return 0;
}
