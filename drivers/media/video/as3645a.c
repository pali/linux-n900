/*
 * drivers/media/video/as3645a.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Ivan T. Ivanov <iivanov@mm-sol.com>
 *
 * Based on adp1653.c by:
 *	    Sakari Ailus <sakari.ailus@nokia.com>
 *	    Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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
 * NOTES:
 * - Using only i2c strobing
 * - Inductor peak current limit setting fixed to 1.75A
 * - VREF offset fixed to 0V
 * - Torch and Indicator lights are enabled by just increasing
 *   intensity from zero
 * - Increasing Flash light intensity does nothing until it is
 *   strobed (strobe control set to 1)
 * - Strobing flash disables Torch and Indicator lights
 *
 * OVERHEATING PROTECTION:
 * It is not possible to follow Focus Assist, Torch and Flash requirements if
 * those 3 flash modes are used as single blocks. This is why overheting
 * protection was changed to cover flash sequence requiremts. Basically we
 * could say that there are three flash sequnces.
 *
 * 1) With RER (Red Eye Reduciton)- AF Assist, Pre-Flash, 3 RER pulses with
 *    Indicator light and Main Flash.
 *    Limitations:
 *    - time between Pre-flash and Main flash should be more than
 *    40ms;
 *    - time between Pre-flash and Main Flash should be less than
 *    600ms or flash sequence will be considered as finished;
 *    - time between AF Assist and Pre-Flash must be less than
 *    600ms or flash sequence will be considered as finished;
 *    - maximum 10 % duty cycle;
 *
 * 2) Without RER - AF Assist Pre-flash and Main FLash.
 *    Limitations: Same as above
 *
 * 3) Without AF Assist - Same as upper two just without the Assist requirements
 *    Limitations:
 *    - time between Pre-flash and Main flash should be more than
 *    40ms;
 *    - time between Pre-flash and Main flash should be less than
 *    600ms or flash sequence will be considered as finished;
 *    - maximum 10 % duty cycle;
 *
 * If Torch mode is used for over 2 seconds and imediatelly after that flash is
 * required, flash must first be cooled down for the same amount of time as if
 * full flash sequence was used. It is normal to use several AF Assists before
 * actully starting the Flash, and it that time is less than 2s Flash could be
 * fired immediatelly after AF Assist.
 *
 * If we have the following sequnce exact formulae for duty cycle calctlation
 * will be:
 *
 * Off ->  TORCH  (T1=200s) ->  COOL DOWN BEFORE USAGE  (2700ms)
 *
 * ASSIST (T2=1s) -> PRE-FLASH (F1=70ms) -> Off (D1=50ms) ->
 * -> MAIN-FLASH (F2=150ms) ->Off--(D2=1s) ->  COOL DOWN (C=?)
 *
 *
 * Active_time = F1 + F2 = 70+150 = 220 ms;
 *
 * Total_time = Active_time*10 = (70+150)*10 = 2200 ms;
 *
 * C > Total_time - Active = 1980 ms;
 *
 *
 * TODO:
 * - Check hardware FSTROBE control when sensor driver add support for this
 *
 */

#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/sysfs.h>
#include <linux/as3645a.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mutex.h>

#include <media/as3645a.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>

#define AS_TIMER_MS_TO_CODE(t)			(((t) - 100) / 50)
#define AS_TIMER_CODE_TO_MS(c)			(50 * (c) + 100)
#define AS_2_REG(a)				((a) ? ((a) - 1) : (a))

/* Register definitions */

/* Read-only Design info register: Reset state: xxxx 0001 - for Senna */
#define AS_DESIGN_INFO_REG			0x00
#define AS_DESIGN_INFO_FACTORY(x)		(((x) >> 4))
#define AS_DESIGN_INFO_MODEL(x)			((x) & 0x0f)

/* Read-only Version control register: Reset state: 0000 0000
 * for first engineering samples
 */
#define AS_VERSION_CONTROL_REG			0x01
#define AS_VERSION_CONTROL_RFU(x)		(((x) >> 4))
#define AS_VERSION_CONTROL_VERSION(x)		((x) & 0x0f)

/* Read / Write	(Indicator and timer register): Reset state: 0000 1111 */
#define AS_INDICATOR_AND_TIMER_REG		0x02
/* 0000=100ms, 0001=150ms, ... 1111=850ms */
#define AS_INDICATOR_AND_TIMER_TIMER_SHIFT	0
#define AS_INDICATOR_AND_TIMER_TIMER_MASK	0x0f

/* VREF offset (00=0V default, 01=+0.3V, 10=-0.3V, 11=+0.6V) */
#define AS_INDICATOR_AND_TIMER_VREF_SHIFT	4
#define AS_INDICATOR_AND_TIMER_VREF_MASK	0x30

/* Indicator LED current (00=2.5mA, 01=5mA, 10=7.5mA, 11=10mA) */
#define AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT 	6
#define AS_INDICATOR_AND_TIMER_INDICATOR_MASK  	0xc0

/* Read / Write	(Current set register): Reset state: 0110 1001 */
#define AS_CURRENT_SET_REG			0x03
/* Assist light current (000=20mA, 001=40mA, ... 111=160mA */
#define AS_CURRENT_ASSIST_LIGHT_SHIFT		0
#define AS_CURRENT_ASSIST_LIGHT_MASK		0x07

/* LED amount detection (0=disabled, 1=enabled). Default is enabled. */
#define AS_CURRENT_LED_DETECT_SHIFT		3
#define AS_CURRENT_LED_DETECT_MASK		0x08

/* Flash current (0000=200mA, 0001=220mA, ... 1111=500mA */
#define AS_CURRENT_FLASH_CURRENT_SHIFT		4
#define AS_CURRENT_FLASH_CURRENT_MASK		0xf0

/* Read / Write	(Control register): Reset state: 1011 0100 */
#define AS_CONTROL_REG				0x04
/* Output mode select, Default is external torch mode.
 * 00=external torch mode, 01=indicator mode
 * 10=assist light mode, 11=flash mode
 */
#define AS_CONTROL_OUTPUT_MODE_SELECT_SHIFT	0
#define AS_CONTROL_OUTPUT_MODE_SELECT_MASK	0x03

/* Strobe signal on/off (0=disabled, 1=enabled). Default is enabled. */
#define AS_CONTROL_STROBE_ON_SHIFT		2
#define AS_CONTROL_STROBE_ON_MASK		0x04

/* Turn on output: indicator, assist light, flash (0=off, 1=on) */
#define AS_CONTROL_TURN_ON_OUTPUT_SHIFT		3
#define AS_CONTROL_TURN_ON_OUTPUT_MASK		0x08

/* External torch mode allowed in standby mode (0=not allowed, 1=allowed)) */
#define AS_CONTROL_TORCH_IN_STANDBY_SHIFT	4
#define AS_CONTROL_TORCH_IN_STANDBY_MASK	0x10

/* Strobe signal type (0=edge sensitive, 1=level sensitive) */
#define AS_CONTROL_STROBE_TYPE_SHIFT		5
#define AS_CONTROL_STROBE_TYPE_MASK		0x20
/* Inductor peak current limit settings
 * (00=1.25A, 01=1.5A, 10=1.75A default, 11=maximum 2.0A)
 */
#define AS_CONTROL_INDUCTOR_PEAK_SELECT_SHIFT	6
#define AS_CONTROL_INDUCTOR_PEAK_SELECT_MASK	0xc0

/* Read only (D3 is read / write) (Fault and info): Reset state: 0000 x000 */
#define AS_FAULT_INFO_REG			0x05

#define AS_FAULT_INFO_RFU			0x01
/* Inductor peak current limit fault (1=fault, 0=no fault) */
#define AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT	0x02
/* Indicator LED fault (1=fault, 0=no fault);
 * fault is either short circuit or open loop
 */
#define AS_FAULT_INFO_INDICATOR_LED		0x04
/* Amount of LEDs (1=two LEDs, 0=only one LED) */
#define AS_FAULT_INFO_LED_AMOUNT		0x08
/* Timeout fault (1=fault, 0=no fault) */
#define AS_FAULT_INFO_TIMEOUT			0x10
/* Over Temperature Protection (OTP) fault (1=fault, 0=no fault) */
#define AS_FAULT_INFO_OVER_TEMPERATURE		0x20
/* Short circuit fault (1=fault, 0=no fault) */
#define AS_FAULT_INFO_SHORT_CIRCUIT		0x40
/* Over voltage protection (OVP) fault (1=fault, 0=no fault) */
#define AS_FAULT_INFO_OVER_VOLTAGE		0x80

/* Boost register */
#define AS_BOOST_REG				0x0D
/* Normal flash current (upto 500mA) */
#define AS_BOOST_CURRENT_DISABLE		0
/* Double flash current */
#define AS_BOOST_CURRENT_ENABLE			1

/* Password register is used to unlock boost register writing */
#define AS_PASSWORD_REG				0x0F
#define AS_PASSWORD_UNLOCK_VALUE		0x55

#define TORCH_IN_STANDBY			0	/* 1 - on, 0 - off */

#define AS3645A_MIN_FLASH_LEN			100000 /* us */
#define AS3645A_MAX_FLASH_LEN			850000

enum as_mode {
	AS_MODE_EXT_TORCH = 0,
	AS_MODE_INDICATOR,
	AS_MODE_ASSIST,
	AS_MODE_FLASH,
	AS_MODE_FLASH_READY
};

struct as3645a {
	struct v4l2_subdev subdev;
	struct as3645a_platform_data *platform_data;
	struct v4l2_ctrl_handler ctrls;

	struct mutex power_lock;
	int power_count;

	/* 1 - led is glowing, 0 - all leds are off */
	int active;
	/* enable/disable FSTROBE signal to be active
	 * controled through platform data
	 */
	int fstrobe;
	/* Fault and info register. Faults are saved here when user
	 * checks any fault (cleared when flash is triggered)
	 */
	u8 fault;
	/* Set to nonzero when the fault register is read
	 * and stored into `fault'. Cleared when flash is triggered.
	 */
	u8 fault_read;
	/* Software Flash Timer rval 0=100ms... 15=850ms */
	u8 timer;
	/* VREF offset (0=0V default, 1=+0.3V, 2=-0.3V, 3=+0.6V) */
	u8 vref;
	/* Flash current (0=0mA, 1=200mA ... 16=500mA)
	 * 320mA flash current is default for two LED case.
	 * 500mA is the default for one LED case.
	 * Maximum for two LEDs case is 400mA. The driver needs to ignore
	 * currents over 400mA in two LED case.
	 */
	u8 flash_current;
	/* Indicator LED current (0=0mA, 1=2.5mA, 2=mA, 3=7.5mA, 4=10mA) */
	u8 indicator_current;
	/* Torch/Assist light current (0=0mA, 1=20mA ... 8=160mA)
	 * 80mA Assist light current is default for one LED case
	 * and 40mA with two LEDs case
	 * external Torch uses the same current rval as the assist light
	 */
	u8 assist_current;
	/* Inductor peak current limit settings
	 * (0=1.25A, 1=1.5A, 2=1.75A default, 3=maximum 2.0A)
	 */
	u8 peak;

	/* Output mode select (0=external torch mode, 1=indicator mode,
	 * 2=assist light mode, 3=flash mode). Default is external
	 * torch mode.
	 */
	enum as_mode mode;

	/* Previous output mode select (0=external torch mode, 1=indicator
	 * mode, 2=assist light mode, 3=flash mode). Default is external
	 * torch mode.
	 */
	enum as_mode prev_mode;

	/* Flash mode (I2C or ext strobe) */
	enum v4l2_flash_mode flash_mode;

	/* Duration of LED in power on state */
	u32 active_time;
	/* Duration of LED in power off state */
	u32 inactive_time;
	/* Duration of last LED action */
	u32 act_duration;
	/* Time stamp of last initiated action (ON/OFF) */
	ktime_t action_start;
	/* Current number of sequencal flash*/
	u32 trigger_cnt;
	/* Current flash state */
	u8 led_state;
	/* Flag for duty cylce calculation.*/
	u8 duty_calc_flag;
	/* If Torch is used record its time in ON state.*/
	u32 torch_time_flag;
	/* If torch was used before use its time for duty cycle calculation */
	u32 last_torch_time;
	/* External strobe width in microseconds, if ext strobe is used */
	u32 ext_strobe_width;

	/* Used to reset active flag to zero */
	struct timer_list flash_end_timer;

	/* Used for status events notify */
	struct timer_list event_timer;
	/* Store flash status events state */
	u32 event_state;
	struct mutex events_lock;

	/* Pointer to Indicator control */
	struct v4l2_ctrl *indicator_ctrl;
	/* Pointer to Torch control */
	struct v4l2_ctrl *torch_ctrl;
	/* Pointer to Flash control */
	struct v4l2_ctrl *flash_ctrl;
};

#define to_as3645a(sd) container_of(sd, struct as3645a, subdev)

/* When sequencal flash are made duty cycle must be less than 10% from 1024 */
#define DUTY_CYCLE_PERCENTAGE	102

/* When preflash is used, flash must be turned off for at least 90 ms */
#define PRE_FLASH_OFF		40

/* Maximum sequent flashes without duty cycle calculation between them.
 * This number includes: Pre flash and Main Flash
 */
#define MAX_SEQ_FLASHES		2

/* Maximum cooling time after flash sequence. It is calculated for 10%
 * duty cucle and both Pre and Main flashes are 150ms.
 * Total time = (150 + 150) * 100 / 10
 * Time = Total time - (150 + 150) = 2700 ms
 * This number includes: Pre flash and Main Flash
 */
#define MAX_COOLING_TIME	2700

/* Maximum AF assist light duration.
 */
#define MAX_ASSIST_LIGHT_TIME	2500

/* Number of supported v4l2 events */
#define AS3645A_FLASH_NEVENTS	1

/* Return negative errno else zero on success */
static int as3645a_write(struct as3645a *flash, u8 addr, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;

	rval = i2c_smbus_write_byte_data(client, addr, val);

	dev_dbg(&client->dev, "Write Addr:%02X Val:%02X %s\n", addr, val,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/* Return negative errno else a data byte received from the device. */
static int as3645a_read(struct as3645a *flash, u8 addr)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;

	rval = i2c_smbus_read_byte_data(client, addr);

	dev_dbg(&client->dev, "Read Addr:%02X Val:%02X %s\n", addr, rval,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/*
 * Calculate duty cycle
 */
static int as3645a_duty_cycle(struct as3645a *flash)
{
	u32 total_time;

	total_time = flash->active_time + flash->inactive_time;

	if (!total_time)
		return -1;
	else
		return flash->active_time * 1024 / total_time;
}

/*
 * Calculate flash timing paramets
 */
static int as3645a_flash_timing(struct as3645a *flash, enum as_mode mode,
				int on)
{
	ktime_t curr_time;
	u32 pulse_time = 0;
	u32 hw_timeout = 0;
	int rval = 0;

	curr_time = ktime_get();
	pulse_time = ktime_to_us(ktime_sub(curr_time, flash->action_start));
	pulse_time = pulse_time / 1000;

	/* Flash was stopped by HW timeout */
	if (flash->active != flash->led_state) {
		hw_timeout = AS_TIMER_CODE_TO_MS(flash->timer);
		hw_timeout = (hw_timeout > pulse_time) ? 0 : hw_timeout;
		if (mode != AS_MODE_FLASH_READY)
			flash->led_state = flash->active;
	}

	/* If flash has been used and HW timeout has occured calculate
	 * flash timing parameters.
	 */
	if (hw_timeout) {
		flash->active_time += hw_timeout;
		pulse_time = pulse_time - hw_timeout;
		flash->inactive_time = pulse_time;
	} else {
		/* If HW timeout has not occured calculate flash timing
		 * parameters.
		 */
		if (on) {
			flash->inactive_time = pulse_time;
		} else if (mode == AS_MODE_FLASH) {
			flash->active_time += pulse_time;
		}

		if (!(flash->active_time + flash->inactive_time) && on)
			rval = -ETIME;
	}

	flash->act_duration = pulse_time;

	return rval;
}

/*
 * Prepare Torch/AF assist paramets
 */
static int as3645a_torch_prepare(struct as3645a *flash, int on)
{
	int duty_cycle = 0;

	/* Mode is changed from Flash to Assist */
	if (flash->prev_mode == AS_MODE_FLASH && on) {
		duty_cycle = as3645a_duty_cycle(flash);
		if (duty_cycle > DUTY_CYCLE_PERCENTAGE)
			return -EBUSY;

		if (duty_cycle < 0)
			return -ETIME;

		flash->prev_mode = AS_MODE_ASSIST;
		flash->trigger_cnt = 0;
		flash->torch_time_flag = 0;
		flash->duty_calc_flag = 0;
		flash->active_time = 0;
		flash->inactive_time = 0;
	}

	/* Assist or Torch has been used for more than 2s. Overtime will be
	 * used for duty cycle calculation for next capture.
	 */
	if (!on && flash->act_duration > MAX_ASSIST_LIGHT_TIME)
		flash->torch_time_flag = 1;

	return 0;
}

/*
 * Prepare Flash parameters
 */
static int as3645a_flash_prepare(struct as3645a *flash, enum as_mode mode,
				 int on)
{
	int duty_cycle = 0;

	/* 5th flash has been made or flash sequnce has been broken */
	if ((flash->trigger_cnt && on && flash->inactive_time > 600) ||
	    flash->trigger_cnt == MAX_SEQ_FLASHES) {
		flash->duty_calc_flag = 1;
	}

	/* Calculate duty cycle and if possible start flash sequence */
	if (on && flash->duty_calc_flag) {
		duty_cycle = as3645a_duty_cycle(flash);
		if (duty_cycle > DUTY_CYCLE_PERCENTAGE)
			return -EBUSY;

		if (duty_cycle < 0)
			return -ETIME;

		/* Clear all timing parameter */
		if (mode == AS_MODE_FLASH) {
			flash->trigger_cnt = 0;
			flash->torch_time_flag = 0;
			flash->duty_calc_flag = 0;
			flash->active_time = 0;
			flash->inactive_time = 0;
		}
	}

	if (on && !flash->trigger_cnt && flash->torch_time_flag &&
	    flash->inactive_time < MAX_COOLING_TIME)
		return -EBUSY;

	if (on && flash->trigger_cnt &&
	    flash->inactive_time < PRE_FLASH_OFF)
		return -EBUSY;

	flash->prev_mode = AS_MODE_FLASH;
	return 0;
}

static int
as3645a_overheat_protect(struct as3645a *flash, enum as_mode mode, int on)
{
	int rval = 0;
	u32 hw_timeout = 0;

	if ((mode != AS_MODE_ASSIST) && (mode != AS_MODE_FLASH) &&
	    (mode != AS_MODE_FLASH_READY))
		goto out;

	if (!on && !flash->led_state)
		goto out;

	if (as3645a_flash_timing(flash, mode, on))
		goto out;

	if ((on == flash->led_state) && (AS_MODE_ASSIST == mode))
		goto out;

	if (mode == AS_MODE_ASSIST) {
		rval = as3645a_torch_prepare(flash, on);
		if (rval)
			goto out;
	}

	if (mode == AS_MODE_FLASH || mode == AS_MODE_FLASH_READY) {
		rval = as3645a_flash_prepare(flash, mode, on);
		if (!on && rval && mode == AS_MODE_FLASH) {
			flash->active_time -= flash->act_duration;
			goto out;
		}
	}

out:
	if (!rval && on && mode == AS_MODE_FLASH)
		flash->trigger_cnt++;

	if (mode == AS_MODE_FLASH_READY) {
		flash->inactive_time -= flash->act_duration;
		if (flash->active != flash->led_state) {
			hw_timeout = AS_TIMER_CODE_TO_MS(flash->timer);
			flash->active_time -= hw_timeout;
		}
	}

	return rval;
}

static int as3645a_is_active(struct as3645a *flash)
{
	int act;

	act = as3645a_read(flash, AS_CONTROL_REG);
	act &= AS_CONTROL_TURN_ON_OUTPUT_MASK;

	return !!act;
}

static void as3645a_event_set_state(struct as3645a *flash, u32 new)
{
	mutex_lock(&flash->events_lock);
	flash->event_state = new;
	mutex_unlock(&flash->events_lock);
}

static u32 as3645a_event_get_state(struct as3645a *flash)
{
	u32 state;

	mutex_lock(&flash->events_lock);
	state = flash->event_state;
	mutex_unlock(&flash->events_lock);
	return state;
}

static int as3645a_is_ready(struct as3645a *flash)
{
	int ready;

	mutex_lock(&flash->events_lock);
	ready = flash->event_state == V4L2_FLASH_READY;
	mutex_unlock(&flash->events_lock);
	return ready;
}

static void as3645a_event_queue(struct as3645a *flash, u32 status)
{
	struct video_device *vdev = &flash->subdev.devnode;
	struct v4l2_event event;

	memset(&event, 0, sizeof(event));
	event.type = V4L2_EVENT_STATUS_CHANGED;
	memcpy(event.u.data, &status, sizeof(status));
	v4l2_event_queue(vdev, &event);
}

static void as3645a_event_setup_timer(struct as3645a *flash)
{
	u32 pulse;

	if (flash->flash_mode == V4L2_FLASH_MODE_SOFT)
		pulse = AS_TIMER_CODE_TO_MS(flash->timer);
	else
		pulse = flash->ext_strobe_width / 1000;

	/*
	 * If this is first flash (lash->trigger_cnt == 1) and duration
	 * is less than 80ms (flash->ext_strobe_width < 80)
	 * queue COOLDOWN for 40ms and then change it to READY.
	 * If duration is more set the status for 90ms.
	 *
	 * If this is second flash duty cycle calculalation must be used to get
	 * the appropriate timings.
	 * TTIME = flash->active_time + flash->ext_strobe_width
	 * flash->active_time will contain only flash time for first flash
	 * cooling_time = (TTIME * 1024 / DUTY_CYCLE_PERCENTAGE) - TTIME
	 */
	if (flash->trigger_cnt == 1) {
		if (pulse < 80)
			/* wait 40ms and send READY */
			mod_timer(&flash->event_timer, jiffies +
				  msecs_to_jiffies(40));
		else
			/* wait 90ms and send READY */
			mod_timer(&flash->event_timer, jiffies +
				  msecs_to_jiffies(90));
	} else if (flash->trigger_cnt == 2) {
		u32 ttime, cooling_time;

		ttime = flash->active_time + pulse;
		cooling_time = (ttime * 1024 / DUTY_CYCLE_PERCENTAGE) - ttime;

		mod_timer(&flash->event_timer, jiffies +
			  msecs_to_jiffies(cooling_time));
	}
}

static void as3645_events_timer_func(unsigned long flash_priv)
{
	struct as3645a *flash = (struct as3645a *)flash_priv;

	as3645a_event_queue(flash, V4L2_FLASH_READY);
	as3645a_event_set_state(flash, V4L2_FLASH_READY);
}

/* This software timer resets flash->active when the flash ends */
static void as3645_flash_end(unsigned long flash_priv)
{
	struct as3645a *flash = (struct as3645a *)flash_priv;

	if (flash->mode == AS_MODE_FLASH &&
	    as3645a_event_get_state(flash) != V4L2_FLASH_COOLDOWN) {
		as3645a_event_queue(flash, V4L2_FLASH_COOLDOWN);
		as3645a_event_set_state(flash, V4L2_FLASH_COOLDOWN);
		as3645a_event_setup_timer(flash);
	}

	flash->active = 0;
}

static int as3645a_trigger(struct as3645a *flash, enum as_mode mode, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	u8 reg;
	int rval = 0;

	/* Force real reading of faults from chip next time */
	flash->fault = 0;
	flash->fault_read = 0;

	/* Before enabling flash, clear the status register */
	as3645a_read(flash, AS_FAULT_INFO_REG);

	/* Fill indicator and timeout registers. */
	if (flash->flash_mode != V4L2_FLASH_MODE_EXT_STROBE)
		reg = flash->timer << AS_INDICATOR_AND_TIMER_TIMER_SHIFT;
	else {
		u32 timeout = 100;
		if (flash->ext_strobe_width > AS3645A_MAX_FLASH_LEN)
			return -EINVAL;
		/* Use timeout to protect the flash in case the external
		 * strobe gets stuck. Minimum value 100 ms, maximum 850 ms.
		 */
		if (flash->ext_strobe_width >= AS3645A_MIN_FLASH_LEN &&
		    flash->ext_strobe_width <= AS3645A_MAX_FLASH_LEN)
			timeout = (flash->ext_strobe_width + 999) / 1000;
			/* Round up to multiple of 50 and not less than 100 */
			timeout = (timeout + 49) / 50 * 50;
			if (timeout < 100)
				timeout = 100;
		reg = AS_TIMER_MS_TO_CODE(timeout);
	}
	reg |= flash->vref << AS_INDICATOR_AND_TIMER_VREF_SHIFT;
	reg |= AS_2_REG(flash->indicator_current) <<
	       AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT;

	rval = as3645a_write(flash, AS_INDICATOR_AND_TIMER_REG, reg);
	if (rval) {
		dev_err(&client->dev,
			"Writing to timeout and indicator register failed\n");
		return rval;
	}

	/* fill current set register */
	reg = AS_2_REG(flash->flash_current) << AS_CURRENT_FLASH_CURRENT_SHIFT;
	reg |= AS_2_REG(flash->assist_current) << AS_CURRENT_ASSIST_LIGHT_SHIFT;
	/* always enabled */
	reg |= 1 << AS_CURRENT_LED_DETECT_SHIFT;

	rval = as3645a_write(flash, AS_CURRENT_SET_REG, reg);
	if (rval) {
		dev_err(&client->dev, "Writing to current register failed\n");
		return rval;
	}

	rval = as3645a_overheat_protect(flash, mode, on);
	if (rval) {
		dev_dbg(&client->dev, "Flash Overheating Protection occured\n");
		return rval;
	}

	if (mode != flash->mode || !on) {
		/* turn-off lights first */
		reg = AS_MODE_FLASH << AS_CONTROL_OUTPUT_MODE_SELECT_SHIFT;
		reg |= 0 << AS_CONTROL_TURN_ON_OUTPUT_SHIFT;
		reg |= TORCH_IN_STANDBY << AS_CONTROL_TORCH_IN_STANDBY_SHIFT;
		reg |= 0 << AS_CONTROL_STROBE_ON_SHIFT;
		reg |= flash->peak << AS_CONTROL_INDUCTOR_PEAK_SELECT_SHIFT;

		rval = as3645a_write(flash, AS_CONTROL_REG, reg);

		if (!rval && on != flash->led_state && (mode == AS_MODE_FLASH ||
		    mode == AS_MODE_FLASH_READY || mode == AS_MODE_ASSIST)) {
			flash->action_start = ktime_get();
			flash->led_state = on;
		}

		if (rval)
			dev_err(&client->dev, "Can't turn off output %d\n",
				flash->mode);
		else
			flash->active = 0;
	}

	if (!on)
		goto out;

	if (on && flash->flash_mode == V4L2_FLASH_MODE_EXT_STROBE &&
	    flash->fstrobe && mode == AS_MODE_FLASH) {
		if (flash->platform_data->setup_ext_strobe)
			flash->platform_data->setup_ext_strobe(1);
		/* Hardware-specific strobe using I/O pin,
		 * it is a self cleared action
		 */
		reg = mode << AS_CONTROL_OUTPUT_MODE_SELECT_SHIFT;
		reg |= 1 << AS_CONTROL_TURN_ON_OUTPUT_SHIFT;
		reg |= TORCH_IN_STANDBY << AS_CONTROL_TORCH_IN_STANDBY_SHIFT;
		reg |= 1 << AS_CONTROL_STROBE_TYPE_SHIFT;
		reg |= 1 << AS_CONTROL_STROBE_ON_SHIFT;
		reg |= flash->peak << AS_CONTROL_INDUCTOR_PEAK_SELECT_SHIFT;

		rval = as3645a_write(flash, AS_CONTROL_REG, reg);

		if (!rval && (on != flash->led_state)) {
			flash->action_start = ktime_get();
			flash->led_state = on;
		}

		if (!rval) {
			flash->mode = mode;
			mod_timer(&flash->flash_end_timer,
				  jiffies +
				  msecs_to_jiffies((flash->ext_strobe_width +
						   999) / 1000));
			flash->active = 1;
		}

		dev_dbg(&client->dev, "%s: ext hw strobe\n", __func__);

	} else if (on) {
		/* Software strobe using i2c */
		reg = mode << AS_CONTROL_OUTPUT_MODE_SELECT_SHIFT;
		reg |= 1 << AS_CONTROL_TURN_ON_OUTPUT_SHIFT;
		reg |= TORCH_IN_STANDBY << AS_CONTROL_TORCH_IN_STANDBY_SHIFT;
		reg |= 0 << AS_CONTROL_STROBE_ON_SHIFT;
		reg |= flash->peak << AS_CONTROL_INDUCTOR_PEAK_SELECT_SHIFT;

		rval = as3645a_write(flash, AS_CONTROL_REG, reg);

		if (!rval && on != flash->led_state && (mode == AS_MODE_FLASH ||
		    mode == AS_MODE_FLASH_READY || mode == AS_MODE_ASSIST)) {
			flash->action_start = ktime_get();
			flash->led_state = on;
		}

		if (!rval) {
			flash->mode = mode;
			mod_timer(&flash->flash_end_timer,
				  jiffies +
				  msecs_to_jiffies(
				  AS_TIMER_CODE_TO_MS(flash->timer)));
			flash->active = 1;
		}

		dev_dbg(&client->dev, "%s: i2c strobe\n", __func__);
	}

out:
	dev_dbg(&client->dev, "Mode %d strobe %s %s\n", mode,
		on ? "on" : "off", rval < 0 ? "fail" : "succeed");

	return rval;
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int as3645a_read_fault(struct as3645a *flash)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;

	/* NOTE: reading register clear fault status */
	rval = as3645a_read(flash, AS_FAULT_INFO_REG);
	if (rval < 0)
		return rval;

	if (rval & AS_FAULT_INFO_RFU)
		dev_err(&client->dev, "RFU fault\n");

	if (rval & AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT)
		dev_err(&client->dev, "Inductor Peak limit fault\n");

	if (rval & AS_FAULT_INFO_INDICATOR_LED)
		dev_err(&client->dev, "Indicator LED fault: "
			"Short circuit or open loop\n");

	dev_dbg(&client->dev, "%u connected LEDs\n",
		rval & AS_FAULT_INFO_LED_AMOUNT ? 2 : 1);

	if (rval & AS_FAULT_INFO_TIMEOUT)
		dev_err(&client->dev, "Timeout fault\n");

	if (rval & AS_FAULT_INFO_OVER_TEMPERATURE)
		dev_err(&client->dev, "Over temperature fault\n");

	if (rval & AS_FAULT_INFO_SHORT_CIRCUIT)
		dev_err(&client->dev, "Short circuit fault\n");

	if (rval & AS_FAULT_INFO_OVER_VOLTAGE)
		dev_err(&client->dev, "Over voltage fault: "
			"Indicates missing capacitor or open connection\n");

	if (rval & ~AS_FAULT_INFO_INDICATOR_LED)
		dev_dbg(&client->dev, "Senna: No faults, nice\n");

	return rval;
}

static int as3645a_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct as3645a *flash =
		container_of(ctrl->handler, struct as3645a, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval, shift;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT_RFU:
	case V4L2_CID_FLASH_FAULT_INDUCTOR_PEAK_LIMIT:
	case V4L2_CID_FLASH_FAULT_INDICATOR_LED:
	case V4L2_CID_FLASH_FAULT_LED_AMOUNT:
	case V4L2_CID_FLASH_FAULT_TIMEOUT:
	case V4L2_CID_FLASH_FAULT_OVER_TEMPERATURE:
	case V4L2_CID_FLASH_FAULT_SHORT_CIRCUIT:
	case V4L2_CID_FLASH_FAULT_OVER_VOLTAGE:
		if (!flash->fault_read) {
			rval = as3645a_read_fault(flash);
			if (rval < 0)
				return rval;

			flash->fault = (u8)rval;
			flash->fault_read = 1;
		}

		/* FIXME This relies on the CID values to match the order of
		 * the bits in the register. Don't do that.
		 */
		shift = ctrl->id - V4L2_CID_FLASH_FAULT_RFU;
		ctrl->cur.val = (flash->fault >> shift) & 1;
		if (ctrl->id == V4L2_CID_FLASH_FAULT_LED_AMOUNT)
			ctrl->cur.val += 1;
		break;

	case V4L2_CID_FLASH_READY:
		ctrl->cur.val = 0;
		if (!as3645a_is_active(flash) && as3645a_is_ready(flash))
			ctrl->cur.val = 1;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&client->dev, "G_CTRL %08x:%d\n", ctrl->id, ctrl->cur.val);

	return 0;
}

static int as3645a_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct as3645a *flash =
		container_of(ctrl->handler, struct as3645a, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval = 0;
	u8 reg;

	dev_dbg(&client->dev, "S_CTRL %08x:%d\n", ctrl->id, ctrl->val);

	flash->active = as3645a_is_active(flash);

	switch (ctrl->id) {
	case V4L2_CID_FLASH_MODE:
		flash->flash_mode = ctrl->val;
		break;

	case V4L2_CID_FLASH_STROBE:
	case V4L2_CID_FLASH_TRIGGER:
		if (flash->active && flash->mode != AS_MODE_FLASH)
			return -EBUSY;

		if (flash->flash_current)
			rval = as3645a_trigger(flash, AS_MODE_FLASH, 1);
		break;

	case V4L2_CID_FLASH_TIMEOUT:
	case V4L2_CID_FLASH_DURATION:
		/* This is ugly. The step value is different depending on strobe
		 * mode, so only round the value when using I2C strobing.
		 */
		if (flash->flash_mode == V4L2_FLASH_MODE_EXT_STROBE &&
		    flash->platform_data->set_strobe_width != NULL) {
			flash->ext_strobe_width = ctrl->val;
			flash->platform_data->set_strobe_width(ctrl->val);
		} else {
			if (ctrl->val < AS3645A_MIN_FLASH_LEN)
				ctrl->val = AS3645A_MIN_FLASH_LEN;
			ctrl->val = ctrl->val / 50000 * 50000;
			flash->timer = AS_TIMER_MS_TO_CODE(ctrl->val / 1000);
		}
		break;

	case V4L2_CID_FLASH_INTENSITY:
		if (flash->active && flash->mode != AS_MODE_FLASH)
			return -EBUSY;

		/* fill current set register */
		reg = AS_2_REG(flash->assist_current) <<
			       AS_CURRENT_ASSIST_LIGHT_SHIFT;
		reg |= AS_2_REG(ctrl->val) << AS_CURRENT_FLASH_CURRENT_SHIFT;
		/* always enabled */
		reg |= 1 << AS_CURRENT_LED_DETECT_SHIFT;

		rval = as3645a_write(flash, AS_CURRENT_SET_REG, reg);
		if (rval < 0)
			return rval;

		flash->flash_current = ctrl->val;
		flash->mode = AS_MODE_FLASH;
		if (!ctrl->val) {
			rval = as3645a_trigger(flash, AS_MODE_FLASH, 0);
			if (rval)
				break;

			as3645a_event_queue(flash, V4L2_FLASH_COOLDOWN);
			as3645a_event_set_state(flash, V4L2_FLASH_COOLDOWN);
			as3645a_event_setup_timer(flash);
		}
		break;

	case V4L2_CID_TORCH_INTENSITY:
		if (flash->active && flash->mode != AS_MODE_ASSIST)
			return -EBUSY;

		/* fill current set register */
		reg = AS_2_REG(flash->flash_current) <<
			       AS_CURRENT_FLASH_CURRENT_SHIFT;
		reg |= AS_2_REG(ctrl->val) << AS_CURRENT_ASSIST_LIGHT_SHIFT;
		/* always enabled */
		reg |= 1 << AS_CURRENT_LED_DETECT_SHIFT;

		rval = as3645a_write(flash, AS_CURRENT_SET_REG, reg);
		if (rval < 0)
			return rval;

		flash->assist_current = ctrl->val;

		rval = as3645a_trigger(flash, AS_MODE_ASSIST, !!ctrl->val);
		if (rval)
			break;

		if (ctrl->val)
			break;

		if (flash->act_duration > MAX_ASSIST_LIGHT_TIME) {
			/*
			 * queue event COOLDOWN for MAX_COOLING_TIME,
			 * and then change it to READY
			 */
			as3645a_event_queue(flash, V4L2_FLASH_COOLDOWN);
			as3645a_event_set_state(flash,
						V4L2_FLASH_COOLDOWN);
			mod_timer(&flash->event_timer, jiffies +
				  msecs_to_jiffies(MAX_COOLING_TIME));
		} else {
			as3645a_event_queue(flash, V4L2_FLASH_READY);
			as3645a_event_set_state(flash, V4L2_FLASH_READY);
		}
		break;

	case V4L2_CID_INDICATOR_INTENSITY:
		if (flash->active && flash->mode != AS_MODE_INDICATOR)
			return -EBUSY;

		/* fill indicator and timer register */
		reg = AS_2_REG(ctrl->val) <<
		      AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT;
		reg |= flash->timer << AS_INDICATOR_AND_TIMER_TIMER_SHIFT;
		reg |= flash->vref << AS_INDICATOR_AND_TIMER_VREF_SHIFT;

		rval = as3645a_write(flash, AS_INDICATOR_AND_TIMER_REG, reg);
		if (rval < 0)
			return rval;

		flash->indicator_current = ctrl->val;

		return as3645a_trigger(flash, AS_MODE_INDICATOR, !!ctrl->val);

	default:
		return -EINVAL;
	}

	return rval;
}

static const struct v4l2_ctrl_ops as3645a_ctrl_ops = {
	.g_volatile_ctrl = as3645a_get_ctrl,
	.s_ctrl = as3645a_set_ctrl,
};

static const char *as3645a_flash_mode_menu[] = {
	"I2C strobe",
	"External strobe",
};

static const struct v4l2_ctrl_config as3645a_ctrls[] = {
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_MODE,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Flash strobe mode",
		.min		= V4L2_FLASH_MODE_SOFT,
		.max		= V4L2_FLASH_MODE_EXT_STROBE,
		.step		= 0,
		.def		= V4L2_FLASH_MODE_SOFT,
		.flags		= 0,
		.qmenu		= as3645a_flash_mode_menu,
	},
	{
		/* This is deprecated - use TRIGGER instead */
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_STROBE,
		.type		= V4L2_CTRL_TYPE_BUTTON,
		.name		= "Flash strobe",
		.min		= 0,
		.max		= 1,
		.step		= 0,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_UPDATE,
	},
	{
		/* Does the same as STROBE */
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_TRIGGER,
		.type		= V4L2_CTRL_TYPE_BUTTON,
		.name		= "Flash trigger",
		.min		= 0,
		.max		= 1,
		.step		= 0,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_UPDATE,
	},
	{
		/* This is deprecated - use DURATION instead */
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_TIMEOUT,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Flash timeout [us]",
		.min		= 1,
		.max		= 850000,
		.step		= 1,
		.def		= 150000,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		/* Does the same as above */
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_DURATION,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Flash duration [us]",
		.min		= 1, /* It can be this short with ext strobe */
		.max		= AS3645A_MAX_FLASH_LEN,
		.step		= 1,
		.def		= AS3645A_MIN_FLASH_LEN,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_INTENSITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Flash intensity",
		.min		= 0,
		.max		= 16,
		.step		= 1,
		.def		= 16,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{	/* same as Focus Assist light */
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_TORCH_INTENSITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Torch/Assist intensity",
		.min		= 0,
		.max		= 8,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_INDICATOR_INTENSITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Indicator intensity",
		.min		= 0,
		.max		= 4,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_RFU,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "RFU",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_INDUCTOR_PEAK_LIMIT,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Inductor peak limit fault",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_INDICATOR_LED,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Indicator LED fault",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_LED_AMOUNT,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Number of LEDs",
		.min		= 1,
		.max		= 2,
		.step		= 1,
		.def		= 1,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_TIMEOUT,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Timeout fault",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_OVER_TEMPERATURE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Over temperature fault",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_SHORT_CIRCUIT,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Short circuit fault",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_FAULT_OVER_VOLTAGE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Over voltage fault",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	},
	{
		.ops		= &as3645a_ctrl_ops,
		.id		= V4L2_CID_FLASH_READY,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flash ready status",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 1,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
		.is_volatile	= 1,
	}
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

/* Put device into know state. */
static int as3645a_setup(struct as3645a *flash)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;
	u8 reg;

	/* clear errors */
	rval = as3645a_read(flash, AS_FAULT_INFO_REG);
	if (rval < 0)
		return rval;

	flash->fault = (u8) (rval & ~AS_CURRENT_LED_DETECT_MASK);

	/*
	 * Clear the fault read variable, fault register does not return
	 * reliable information until after the flash is triggered.
	 */
	flash->fault_read = 0;

	dev_dbg(&client->dev, "Fault info: %02X\n", flash->fault);

	rval = as3645a_read(flash, AS_CURRENT_SET_REG);
	if (rval < 0)
		return rval;

	reg = rval;
	/* ensure that number of led's detection is active */
	reg |= AS_CURRENT_LED_DETECT_MASK;

	rval = as3645a_write(flash, AS_CURRENT_SET_REG, reg);
	if (rval < 0)
		return rval;

	rval = as3645a_trigger(flash, flash->mode, flash->active);
	if (rval < 0)
		return rval;

	/* read status */
	rval = as3645a_read_fault(flash);
	if (rval < 0)
		return rval;

	flash->fault = rval & ~AS_FAULT_INFO_LED_AMOUNT;

	dev_dbg(&client->dev, "AS_INDICATOR_AND_TIMER_REG: %02X\n",
		as3645a_read(flash, AS_INDICATOR_AND_TIMER_REG));
	dev_dbg(&client->dev, "AS_CURRENT_SET_REG: %02X\n",
		as3645a_read(flash, AS_CURRENT_SET_REG));
	dev_dbg(&client->dev, "AS_CONTROL_REG: %02X\n",
		as3645a_read(flash, AS_CONTROL_REG));
	dev_dbg(&client->dev, "AS_FAULT_INFO_REG: %02X\n",
		as3645a_read(flash, AS_FAULT_INFO_REG));

	return flash->fault;
}

static int __as3645a_set_power(struct as3645a *flash, int on)
{
	return (flash->platform_data && flash->platform_data->set_power) ?
		flash->platform_data->set_power(&flash->subdev, on) : 0;
}

static int as3645a_set_power(struct v4l2_subdev *sd, int on)
{
	struct as3645a *flash = to_as3645a(sd);
	int ret = 0;

	mutex_lock(&flash->power_lock);

	if (flash->power_count == !on) {
		ret = __as3645a_set_power(flash, !!on);
		if (ret < 0)
			goto done;
	}

	flash->active = (as3645a_read(flash, AS_CONTROL_REG) &
					AS_CONTROL_TURN_ON_OUTPUT_MASK)
					>> AS_CONTROL_TURN_ON_OUTPUT_SHIFT;

	if (!on && flash->active) {
		ret = as3645a_trigger(flash, flash->mode, 0);
		if (ret < 0)
			goto done;

		v4l2_ctrl_lock(flash->flash_ctrl);
		flash->flash_ctrl->cur.val = flash->flash_current = 0;
		v4l2_ctrl_unlock(flash->flash_ctrl);

		v4l2_ctrl_lock(flash->torch_ctrl);
		flash->torch_ctrl->cur.val = flash->assist_current = 0;
		v4l2_ctrl_unlock(flash->torch_ctrl);

		v4l2_ctrl_lock(flash->indicator_ctrl);
		flash->indicator_ctrl->cur.val = flash->indicator_current = 0;
		v4l2_ctrl_unlock(flash->indicator_ctrl);
	}

	flash->power_count += on ? 1 : -1;
	WARN_ON(flash->power_count < 0);

done:
	mutex_unlock(&flash->power_lock);
	return ret;
}

static int as3645a_registered(struct v4l2_subdev *sd)
{
	struct as3645a_flash_torch_parms *flash_params = NULL;
	struct as3645a *flash = to_as3645a(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int rval, man, model, rfu, version;
	struct v4l2_ctrl *ctrl;
	unsigned int leds = 2;
	const char *factory;

	if (flash->platform_data) {
		if (flash->platform_data->num_leds)
			leds = flash->platform_data->num_leds;

		flash_params = flash->platform_data->flash_torch_limits;
	}

	/* Power up the flash driver and read manufacturer ID, model ID, RFU
	 * and version.
	 */
	as3645a_set_power(&flash->subdev, 1);

	rval = as3645a_read(flash, AS_DESIGN_INFO_REG);
	if (rval < 0)
		goto power_off;

	man = AS_DESIGN_INFO_FACTORY(rval);
	model = AS_DESIGN_INFO_MODEL(rval);

	rval = as3645a_read(flash, AS_VERSION_CONTROL_REG);
	if (rval < 0)
		goto power_off;

	rfu = AS_VERSION_CONTROL_RFU(rval);
	version = AS_VERSION_CONTROL_VERSION(rval);

	if (model != 0x0001 || rfu != 0x0000) {		/* Check for Senna */
		dev_err(&client->dev, "Senna not detected (model:%d rfu:%d)\n",
			model, rfu);
		rval = -ENODEV;
		goto power_off;
	}

	switch (man) {
	case 1:
		factory = "AMS, Austria Micro Systems";
		break;
	case 2:
		factory = "ADI, Analog Devices Inc.";
		break;
	case 3:
		factory = "NSC, National Semiconductor";
		break;
	case 4:
		factory = "NXP";
		break;
	case 5:
		factory = "TI, Texas Instrument";
		break;
	default:
		factory = "Unknown";
	}

	dev_dbg(&client->dev, "Factory: %s(%d) Version: %d\n", factory, man,
		version);

	rval = as3645a_write(flash, AS_PASSWORD_REG, AS_PASSWORD_UNLOCK_VALUE);
	if (rval < 0)
		goto power_off;

	rval = as3645a_write(flash, AS_BOOST_REG, AS_BOOST_CURRENT_DISABLE);
	if (rval < 0)
		goto power_off;

	/* Update the control limits. The number of LEDs reported in platform
	 * data is first used to update the default value, and the parameters
	 * passed to platform data can then override that.
	 */

	/* V4L2_CID_FLASH_INTENSITY */
	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_FLASH_INTENSITY);
	flash->flash_ctrl = ctrl;
	if (leds == 1)
		ctrl->default_value = 16;
	else
		ctrl->default_value = 11;

	if (flash_params) {
		ctrl->minimum = flash_params->flash_min_current;
		ctrl->maximum = flash_params->flash_max_current;
		ctrl->step = flash_params->flash_step;
		ctrl->default_value = ctrl->maximum;
	}

	ctrl->cur.val = ctrl->default_value;
	ctrl->val = ctrl->default_value;

	/* V4L2_CID_TORCH_INTENSITY */
	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_TORCH_INTENSITY);
	flash->torch_ctrl = ctrl;

	if (flash_params) {
		ctrl->minimum = flash_params->torch_min_current;
		ctrl->maximum = flash_params->torch_max_current;
		ctrl->step = flash_params->torch_step;
	}

	ctrl->cur.val = ctrl->default_value;
	ctrl->val = ctrl->default_value;

	/* V4L2_CID_FLASH_TIMEOUT */
	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_FLASH_TIMEOUT);

	if (flash_params) {
		ctrl->minimum = flash_params->timeout_min_us;
		ctrl->maximum = flash_params->timeout_max_us;
		ctrl->step = flash_params->timeout_step;
		ctrl->default_value = ctrl->maximum;
	}

	ctrl->cur.val = ctrl->default_value;
	ctrl->val = ctrl->default_value;

	/* V4L2_CID_FLASH_FAULT_LED_AMOUNT */
	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_FLASH_FAULT_LED_AMOUNT);
	ctrl->default_value = leds;

	ctrl->cur.val = ctrl->default_value;
	ctrl->val = ctrl->default_value;

	/* Initialize the parameters stored in the as3645a structure with
	 * default values.
	 *
	 * TODO Handle this by calling v4l2_ctrl_handler_setup(). Current
	 * control values need to be modified first to make sure the defaults
	 * won't turn the flash on.
	 */
	flash->flash_current = flash->flash_ctrl->default_value;

	flash->assist_current = flash->torch_ctrl->default_value;

	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_FLASH_TIMEOUT);
	flash->timer = AS_TIMER_MS_TO_CODE(ctrl->default_value/1000);

	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_INDICATOR_INTENSITY);
	flash->indicator_current = ctrl->default_value;
	flash->indicator_ctrl = ctrl;

	flash->mode = AS_MODE_EXT_TORCH;
	flash->prev_mode = AS_MODE_FLASH;
	flash->active = 0;
	flash->trigger_cnt = 0;
	flash->active_time = 0;
	flash->inactive_time = 0;
	flash->duty_calc_flag = 0;
	flash->torch_time_flag = 0;
	flash->last_torch_time = 0;
	flash->event_state = V4L2_FLASH_READY;

	ctrl = v4l2_ctrl_find(&flash->ctrls, V4L2_CID_FLASH_MODE);
	flash->flash_mode = ctrl->default_value;

	/* FIXME: These are hard coded for now */
	flash->vref = 0;	/* 0V */
	flash->peak = 2;	/* 1.75A */

	flash->fstrobe = 0;
	if (flash->platform_data)
		flash->fstrobe = !!flash->platform_data->use_ext_flash_strobe;

	rval = as3645a_setup(flash); /* setup default values */

power_off:
	as3645a_set_power(&flash->subdev, 0);

	return rval;
}

static int as3645a_subscribe_event(struct v4l2_subdev *subdev,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	return v4l2_event_subscribe(fh, sub);
}

static int as3645a_unsubscribe_event(struct v4l2_subdev *subdev,
				     struct v4l2_fh *fh,
				     struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

static int as3645a_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return as3645a_set_power(sd, 1);
}

static int as3645a_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return as3645a_set_power(sd, 0);
}

static const struct v4l2_subdev_core_ops as3645a_core_ops = {
	.s_power		= as3645a_set_power,
	.subscribe_event	= as3645a_subscribe_event,
	.unsubscribe_event	= as3645a_unsubscribe_event,
};

static const struct v4l2_subdev_file_ops as3645a_file_ops = {
	.open		= as3645a_open,
	.close		= as3645a_close,
};

static const struct v4l2_subdev_ops as3645a_ops = {
	.core = &as3645a_core_ops,
	.file = &as3645a_file_ops,
};

static const struct v4l2_subdev_internal_ops as3645a_internal_ops = {
	.registered = as3645a_registered,
};

/* -----------------------------------------------------------------------------
 *  I2C driver
 */
#ifdef CONFIG_PM

static int as3645a_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct as3645a *flash = to_as3645a(subdev);
	int rval;

	if (flash->power_count == 0)
		return 0;

	rval = __as3645a_set_power(flash, 0);

	dev_dbg(&client->dev, "Suspend %s\n", rval < 0 ? "failed" : "ok");

	return rval;
}

static int as3645a_resume(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct as3645a *flash = to_as3645a(subdev);
	int rval;

	if (flash->power_count == 0)
		return 0;

	rval = __as3645a_set_power(flash, 1);
	if (!rval)
		rval = as3645a_setup(flash);

	dev_dbg(&client->dev, "Resume %s\n", rval < 0 ? "fail" : "ok");

	return rval;
}

#else

#define as3645a_suspend	NULL
#define as3645a_resume	NULL

#endif /* CONFIG_PM */

static int as3645a_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct as3645a *flash;
	unsigned int i;
	int ret;

	flash = kzalloc(sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->platform_data = client->dev.platform_data;

	v4l2_i2c_subdev_init(&flash->subdev, client, &as3645a_ops);
	flash->subdev.internal_ops = &as3645a_internal_ops;
	flash->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			       V4L2_SUBDEV_FL_HAS_EVENTS;
	flash->subdev.nevents = AS3645A_FLASH_NEVENTS;

	ret = media_entity_init(&flash->subdev.entity, 0, NULL, 0);
	if (ret < 0) {
		kfree(flash);
		return ret;
	}

	v4l2_ctrl_handler_init(&flash->ctrls, ARRAY_SIZE(as3645a_ctrls));
	for (i = 0; i < ARRAY_SIZE(as3645a_ctrls); ++i)
		v4l2_ctrl_new_custom(&flash->ctrls, &as3645a_ctrls[i], NULL);
	flash->subdev.ctrl_handler = &flash->ctrls;

	mutex_init(&flash->power_lock);

	init_timer(&flash->flash_end_timer);
	flash->flash_end_timer.function = as3645_flash_end;
	flash->flash_end_timer.data = (unsigned long)flash;

	init_timer(&flash->event_timer);
	flash->event_timer.function = as3645_events_timer_func;
	flash->event_timer.data = (unsigned long)flash;
	mutex_init(&flash->events_lock);

	return 0;
}

static int __exit as3645a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct as3645a *flash = to_as3645a(subdev);

	v4l2_device_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&flash->ctrls);

	kfree(flash);

	return 0;
}

static const struct i2c_device_id as3645a_id_table[] = {
	{ AS3645A_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, as3645a_id_table);

static struct i2c_driver as3645a_i2c_driver = {
	.driver	= {
		.name = AS3645A_NAME,
	},
	.probe	= as3645a_probe,
	.remove	= __exit_p(as3645a_remove),
	.suspend = as3645a_suspend,
	.resume = as3645a_resume,
	.id_table = as3645a_id_table,
};

static int __init as3645a_init(void)
{
	int rval;

	rval = i2c_add_driver(&as3645a_i2c_driver);
	if (rval)
		printk(KERN_ERR "Failed registering driver" AS3645A_NAME"\n");

	return rval;
}

static void __exit as3645a_exit(void)
{
	i2c_del_driver(&as3645a_i2c_driver);
}

module_init(as3645a_init);
module_exit(as3645a_exit);

MODULE_AUTHOR("Ivan T. Ivanov <iivanov@mm-sol.com>");
MODULE_DESCRIPTION("AS3645A Flash driver");
MODULE_LICENSE("GPL");
