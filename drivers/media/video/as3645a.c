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
 * - Inductor peak current limit setting fixed to 1.75A
 * - VREF offset fixed to 0V
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
#define AS_INDICATOR_AND_TIMER_TIMEOUT_SHIFT	0
#define AS_INDICATOR_AND_TIMER_VREF_SHIFT	4
#define AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT 	6

/* Read / Write	(Current set register): Reset state: 0110 1001 */
#define AS_CURRENT_SET_REG			0x03
#define AS_CURRENT_ASSIST_LIGHT_SHIFT		0
#define AS_CURRENT_LED_DET_ON			(1 << 3)
#define AS_CURRENT_FLASH_CURRENT_SHIFT		4

/* Read / Write	(Control register): Reset state: 1011 0100 */
#define AS_CONTROL_REG				0x04
#define AS_CONTROL_MODE_SETTING_SHIFT		0
#define AS_CONTROL_STROBE_ON			(1 << 2)
#define AS_CONTROL_OUT_ON			(1 << 3)
#define AS_CONTROL_EXT_TORCH_ON			(1 << 4)
#define AS_CONTROL_STROBE_TYPE_EDGE		(0 << 5)
#define AS_CONTROL_STROBE_TYPE_LEVEL		(1 << 5)
#define AS_CONTROL_COIL_PEAK_SHIFT		6

/* Read only (D3 is read / write) (Fault and info): Reset state: 0000 x000 */
#define AS_FAULT_INFO_REG			0x05
#define AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT	(1 << 1)
#define AS_FAULT_INFO_INDICATOR_LED		(1 << 2)
#define AS_FAULT_INFO_LED_AMOUNT		(1 << 3)
#define AS_FAULT_INFO_TIMEOUT			(1 << 4)
#define AS_FAULT_INFO_OVER_TEMPERATURE		(1 << 5)
#define AS_FAULT_INFO_SHORT_CIRCUIT		(1 << 6)
#define AS_FAULT_INFO_OVER_VOLTAGE		(1 << 7)

/* Boost register */
#define AS_BOOST_REG				0x0d
#define AS_BOOST_CURRENT_DISABLE		(0 << 0)
#define AS_BOOST_CURRENT_ENABLE			(1 << 0)

/* Password register is used to unlock boost register writing */
#define AS_PASSWORD_REG				0x0f
#define AS_PASSWORD_UNLOCK_VALUE		0x55

/* AS_CONTROL_EXT_TORCH_ON - on, 0 - off */
#define TORCH_IN_STANDBY			0

#define AS3645A_FLASH_TIMEOUT_MIN		100000	/* us */
#define AS3645A_FLASH_TIMEOUT_MAX		850000
#define AS3645A_FLASH_TIMEOUT_STEP		50000

#define AS3645A_FLASH_INTENSITY_MIN		200	/* mA */
#define AS3645A_FLASH_INTENSITY_MAX_1LED	500
#define AS3645A_FLASH_INTENSITY_MAX_2LEDS	400
#define AS3645A_FLASH_INTENSITY_STEP		20

#define AS3645A_TORCH_INTENSITY_MIN		20	/* mA */
#define AS3645A_TORCH_INTENSITY_MAX		160
#define AS3645A_TORCH_INTENSITY_STEP		20

#define AS3645A_INDICATOR_INTENSITY_MIN		0	/* uA */
#define AS3645A_INDICATOR_INTENSITY_MAX		10000
#define AS3645A_INDICATOR_INTENSITY_STEP	2500

enum as_mode {
	AS_MODE_EXT_TORCH = 0 << AS_CONTROL_MODE_SETTING_SHIFT,
	AS_MODE_INDICATOR = 1 << AS_CONTROL_MODE_SETTING_SHIFT,
	AS_MODE_ASSIST = 2 << AS_CONTROL_MODE_SETTING_SHIFT,
	AS_MODE_FLASH = 3 << AS_CONTROL_MODE_SETTING_SHIFT,
};

enum as_state {
	AS_STATE_READY = V4L2_FLASH_READY,
	AS_STATE_COOLDOWN = V4L2_FLASH_COOLDOWN,
	AS_STATE_FLASH = 2,
};

/*
 * struct as3645a
 *
 * @subdev:		V4L2 subdev
 * @platform_data:	Flash platform data
 * @power_lock:		Protects power_count
 * @power_count:	Power reference count
 * @vref:		VREF offset (0=0V, 1=+0.3V, 2=-0.3V, 3=+0.6V)
 * @peak:		Inductor peak current limit (0=1.25A, 1=1.5A, 2=1.75A,
 * 			3=2.0A)
 * @led_mode:		V4L2 flash LED mode
 * @timeout:		Flash timeout in microseconds
 * @flash_current:	Flash current (0=200mA ... 15=500mA). Maximum
 *			values are 400mA for two LEDs and 500mA for one LED.
 * @assist_current:	Torch/Assist light current (0=20mA, 1=40mA ... 7=160mA)
 * @indicator_current:	Indicator LED current (0=0mA, 1=2.5mA ... 4=10mA)
 * @strobe_source:	Flash strobe source (software or external)
 * @active:		Whether the torch/flash output is active. The state is
 *			updated in software when turning the output on or off,
 *			as well as when a timeout occurs.
 * @led_state:		State of the torch/flash output as controlled by
 *			software. This doesn't take timeouts into account.
 * @mode:		Output mode (external torch, flash, assist or indicator)
 * @timer:		Flash timeout and cooldown state machine timer
 * @state:		Flash state (ready, flash, cooldown)
 * @state_lock:		Protects the state
 * @action_start:	Time stamp of last initiated action (ON/OFF)
 * @flash_count:	Number of times the flash has been triggered
 * @flash_on_time:	Total flash mode on-time since ???
 * @flash_off_time:	Total off time since ???
 * @torch_on_time:	Total torch/assist mode on-time since last cooldown
 */
struct as3645a {
	struct v4l2_subdev subdev;
	struct as3645a_platform_data *platform_data;

	struct mutex power_lock;
	int power_count;

	/* Static parameters */
	u8 vref;
	u8 peak;

	/* Controls */
	struct v4l2_ctrl_handler ctrls;

	enum v4l2_flash_led_mode led_mode;
	unsigned int timeout;
	u8 flash_current;
	u8 assist_current;
	u8 indicator_current;
	enum v4l2_flash_strobe_source strobe_source;

	/* State */
	bool active;
	bool led_state;
	enum as_mode mode;

	struct timer_list timer;
	enum as_state state;
	spinlock_t state_lock;

	/* Overheat protection */
	ktime_t action_start;
	unsigned int flash_count;
	unsigned int flash_on_time;
	unsigned int flash_off_time;
	unsigned int torch_on_time;
};

#define to_as3645a(sd) container_of(sd, struct as3645a, subdev)

/* When sequencal flash are made duty cycle must be less than 10% from 1024 */
#define MAX_DUTY_CYCLE_PERCENTAGE	102

/* When preflash is used, flash must be turned off for at least 40 ms */
#define PRE_FLASH_OFF		40

/* Maximum sequent flashes without duty cycle calculation between them.
 * This number includes: Pre flash and Main Flash
 */
#define MAX_SEQ_FLASHES		2

/* Minimum cooling time after flash sequence. It is calculated for 10%
 * duty cucle and both Pre and Main flashes are 150ms.
 * Total time = (150 + 150) * 100 / 10
 * Time = Total time - (150 + 150) = 2700 ms
 * This number includes: Pre flash and Main Flash
 */
#define MIN_COOLING_TIME	2700

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

/* -----------------------------------------------------------------------------
 * Overheat protection
 *
 * Common flash usage use cases can't be supported if overheat protection
 * requirements are enforced on individual actions. The driver thus covers
 * complete flash sequences when enforcing overheat protection.
 *
 * - Flash duty cycle must be no higher than 10%. However, as pre-flash/flash
 *   sequences wouldn't be possible in that case, we allow higher duty cycles
 *   for sequences of at most MAX_SEQ_FLASHES consecutive flash strobes. A
 *   minimum PRE_FLASH_OFF off-time must be observed between flash strobes
 *   inside a sequence, but no duty cycle requirement is enforced.
 *
 *   If the off-time between flash strobes exceeds 600ms, the strobes are not
 *   considered part of a single sequence and the duty cycle requirement
 *   applies.
 *
 *   Duty cycles are defined as the ratio between total on-time and the sum of
 *   total on-time and last off-time. In the following flash sequence
 *
 *   Assist (T1 = 1s) -> Off (D1 = 100ms) ->
 *   Pre-Flash (F1 = 70ms) -> Off (D2 = 50ms) -> Main Flash (F2 = 150ms) ->
 *   Off (cooldown, C = ?)
 *
 *   the minimum cooldown time can be computed with
 *
 *   Duty cycle = (F1 + F2) / (F1 + F2 + C)
 *   C > (F1 + F2) / Duty cycle - (F1 + F2)
 *   C > (70 + 150) * 9
 *   C > 1980 ms
 *
 * - If the LED is used in torch/assist mode for more than
 *   MAX_ASSIST_LIGHT_TIME, a minimum cooldown of MIN_COOLING_TIME must be
 *   enforced before firing the flash. This cooldown period only applies when
 *   firing the flash, not when turning torch/assist mode on.
 *
 *   For the purpose of this requirement, the torch/assist mode on-time is
 *   computed as the total torch/assist mode on-time since the last off-time
 *   longer than MIN_COOLING_TIME.
 */

/*
 * as3645a_overheat_update_timing - Update flash timing parameters
 * @flash: The flash
 *
 * Update the flash on-time, flash off-time and torch/assist on-time based on
 * the last flash action.
 */
static void as3645a_overheat_update_timing(struct as3645a *flash)
{
	unsigned int duration;
	ktime_t time;

	time = ktime_get();
	duration = ktime_to_us(ktime_sub(time, flash->action_start));
	duration /= 1000;
	flash->action_start = time;

	/* Compute the on- and off-time since the last action. On-time is
	 * cumulative, while off-time accounts for the last off-time only.
	 *
	 * If a hardware timeout occurred, on-time is increased by the timeout
	 * value and off time is set to the action duration minus the timeout
	 * value.
	 */
	if (flash->led_state && !flash->active) {
		/* Hardware timeout */
		unsigned int on_time = flash->timeout / 1000;
		flash->flash_on_time += on_time;
		flash->flash_off_time = duration > on_time
				      ? duration - on_time : 0;
	} else if (flash->led_state) {
		if (flash->mode == AS_MODE_FLASH)
			flash->flash_on_time += duration;
		flash->flash_off_time = 0;
	} else {
		flash->flash_off_time += duration;
	}

	/* Update the total torch/assist mode on-time since the last off-time
	 * longer than MIN_COOLING_TIME. Torch/assist on-time is reset when
	 * the flash has been strobed.
	 */
	if (flash->mode == AS_MODE_ASSIST && flash->led_state)
		flash->torch_on_time += duration;
	else if ((flash->mode == AS_MODE_FLASH && flash->led_state) ||
		   (duration >= MIN_COOLING_TIME))
		flash->torch_on_time = 0;
}

/*
 * as3645a_overheat_check_duty_cycle - Check the flash duty cycle requirement
 * @flash: The flash
 *
 * Return 0 if the duty cycle requirement has been fullfilled, or a negative
 * error code otherwise.
 */
static int as3645a_overheat_check_duty_cycle(struct as3645a *flash)
{
	unsigned int duty_cycle;
	unsigned int total_time;

	total_time = flash->flash_on_time + flash->flash_off_time;
	if (!total_time)
		return -ETIME;

	duty_cycle = flash->flash_on_time * 1024 / total_time;
	if (duty_cycle > MAX_DUTY_CYCLE_PERCENTAGE)
		return -EBUSY;

	return 0;
}

/*
 * as3545a_overheat_torch_prepare - Prepare torch/assist mode
 * @flash: The flash
 *
 * Return 0 if torch/assist mode can be turned on now, or a negative error code
 * if the overheat protection requirements are not fullfilled.
 */
static int as3645a_overheat_torch_prepare(struct as3645a *flash)
{
	/* When switching from flash mode to torch/assist mode, make sure the
	 * flash duty cycle requirements are fullfilled.
	 */
	if (flash->mode != AS_MODE_FLASH)
		return 0;

	return as3645a_overheat_check_duty_cycle(flash);
}

/*
 * as3545a_overheat_flash_prepare - Prepare flash mode
 * @flash: The flash
 *
 * Return 0 if the flash can be strobed now, or a negative error code if the
 * overheat protection requirements are not fullfilled.
 */
static int as3645a_overheat_flash_prepare(struct as3645a *flash)
{
	int ret;

	/* If too many flash strobes have been recorded, or if the last flash
	 * strobe ended more than 600ms ago, consider the flash sequence as
	 * finished and enfore duty cycle requirements.
	 */
	if ((flash->flash_count && flash->flash_off_time > 600) ||
	    flash->flash_count == MAX_SEQ_FLASHES) {
		ret = as3645a_overheat_check_duty_cycle(flash);
		if (ret < 0)
			return ret;
	}

	if (flash->flash_count && flash->flash_off_time < PRE_FLASH_OFF)
		return -EBUSY;

	if (flash->torch_on_time > MAX_ASSIST_LIGHT_TIME)
		return -EBUSY;

	return 0;
}

/*
 * as3645a_overheat_protect - Enforce overheat protection
 * @flash: The flash
 * @mode: Desired output mode
 * @on: Desired output state
 */
static int
as3645a_overheat_protect(struct as3645a *flash, enum as_mode mode, int on)
{
	int ret;

	if (!on && !flash->led_state)
		return 0;

	as3645a_overheat_update_timing(flash);

	/* Overheat protection is only needed in torch/assist and flash modes,
	 * and only when turning the output on.
	 */
	if ((mode != AS_MODE_ASSIST && mode != AS_MODE_FLASH) || !on)
		return 0;

	if (mode == AS_MODE_FLASH)
		ret = as3645a_overheat_flash_prepare(flash);
	else
		ret = as3645a_overheat_torch_prepare(flash);

	if (ret < 0)
		return ret;

	if (mode != AS_MODE_FLASH || flash->flash_count == MAX_SEQ_FLASHES ||
	    (flash->flash_count && flash->flash_off_time > 600)) {
		flash->flash_count = 0;
		flash->flash_on_time = 0;
		flash->flash_off_time = 0;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * State machine
 */

static void as3645a_state_event_queue(struct as3645a *flash, u32 status)
{
	struct video_device *vdev = &flash->subdev.devnode;
	struct v4l2_event event;

	memset(&event, 0, sizeof(event));
	event.type = V4L2_EVENT_STATUS_CHANGED;
	memcpy(event.u.data, &status, sizeof(status));
	v4l2_event_queue(vdev, &event);
}

static void as3645a_state_setup_timer(struct as3645a *flash, enum as_state new)
{
	if (new == AS_STATE_FLASH) {
		mod_timer(&flash->timer, jiffies +
			  msecs_to_jiffies(DIV_ROUND_UP(flash->timeout, 1000)));
		return;
	}

	if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH) {
		/* In torch mode, just wait for MIN_COOLING_TIME */
		mod_timer(&flash->timer, jiffies +
			  msecs_to_jiffies(MIN_COOLING_TIME));
		return;
	}

	/* When cooling down after flash, wait for PRE_FLASH_OFF if we haven't
	 * exceeded the maximum number of sequential flashes, or for a duration
	 * based on the maximum duty cycle requirement otherwise.
	 */
	if (flash->flash_count < MAX_SEQ_FLASHES) {
		mod_timer(&flash->timer, jiffies +
			  msecs_to_jiffies(PRE_FLASH_OFF));
	} else {
		u32 ttime, cooling_time;

		ttime = flash->flash_on_time + flash->timeout / 1000;
		cooling_time = (ttime * 1024 / MAX_DUTY_CYCLE_PERCENTAGE)
			     - ttime;

		mod_timer(&flash->timer, jiffies +
			  msecs_to_jiffies(cooling_time));
	}
}

static void __as3645a_set_state(struct as3645a *flash, enum as_state new)
{
	if (new == flash->state)
		return;

	if (new == AS_STATE_READY || new == AS_STATE_COOLDOWN)
		as3645a_state_event_queue(flash, new);

	flash->state = new;

	if (new == AS_STATE_FLASH || new == AS_STATE_COOLDOWN)
		as3645a_state_setup_timer(flash, new);
}

static void as3645a_set_state(struct as3645a *flash, enum as_state new)
{
	unsigned long flags;

	spin_lock_irqsave(&flash->state_lock, flags);
	__as3645a_set_state(flash, new);
	spin_unlock_irqrestore(&flash->state_lock, flags);
}

static enum as_state as3645a_get_state(struct as3645a *flash)
{
	enum as_state state;
	unsigned long flags;

	spin_lock_irqsave(&flash->state_lock, flags);
	state = flash->state;
	spin_unlock_irqrestore(&flash->state_lock, flags);

	return state;
}

/* This software timer updates the flash state machine */
static void as3645_state_timer_func(unsigned long flash_priv)
{
	struct as3645a *flash = (struct as3645a *)flash_priv;
	unsigned long flags;

	spin_lock_irqsave(&flash->state_lock, flags);

	switch (flash->state) {
	case AS_STATE_FLASH:
		__as3645a_set_state(flash, AS_STATE_COOLDOWN);
		flash->active = false;
		break;
	case AS_STATE_COOLDOWN:
		__as3645a_set_state(flash, AS_STATE_READY);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&flash->state_lock, flags);
}

static bool as3645a_is_active(struct as3645a *flash)
{
	return as3645a_read(flash, AS_CONTROL_REG) & AS_CONTROL_OUT_ON;
}

static bool as3645a_is_ready(struct as3645a *flash)
{
	return as3645a_get_state(flash) == AS_STATE_READY;
}

/* -----------------------------------------------------------------------------
 * Hardware configuration and trigger
 */

/*
 * as3645a_set_config - Set flash configuration registers
 * @flash: The flash
 *
 * Configure the hardware with flash, assist and indicator currents, as well as
 * flash timeout.
 *
 * Return 0 on success, or a negative error code if an I2C communication error
 * occurred.
 */
static int as3645a_set_config(struct as3645a *flash)
{
	int ret;
	u8 val;

	val = (flash->flash_current << AS_CURRENT_FLASH_CURRENT_SHIFT)
	    | (flash->assist_current << AS_CURRENT_ASSIST_LIGHT_SHIFT)
	    | AS_CURRENT_LED_DET_ON;

	ret = as3645a_write(flash, AS_CURRENT_SET_REG, val);
	if (ret < 0)
		return ret;

	if (flash->strobe_source == V4L2_FLASH_STROBE_SOURCE_EXTERNAL) {
		/* Use timeout to protect the flash in case the external
		 * strobe gets stuck. Minimum value 100 ms, maximum 850 ms.
		 */
		u32 timeout = DIV_ROUND_UP(flash->timeout, 1000);
		timeout = max_t(u32, DIV_ROUND_UP(timeout, 50) * 50, 100);
		val = AS_TIMER_MS_TO_CODE(timeout)
		    << AS_INDICATOR_AND_TIMER_TIMEOUT_SHIFT;
	} else {
		val = AS_TIMER_MS_TO_CODE(flash->timeout / 1000)
		    << AS_INDICATOR_AND_TIMER_TIMEOUT_SHIFT;
	}

	val |= (flash->vref << AS_INDICATOR_AND_TIMER_VREF_SHIFT)
	    |  ((flash->indicator_current ? flash->indicator_current - 1 : 0)
		 << AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT);

	return as3645a_write(flash, AS_INDICATOR_AND_TIMER_REG, val);
}

/*
 * as3645a_set_control - Set flash control register
 * @flash: The flash
 * @mode: Desired output mode
 * @on: Desired output state
 *
 * Configure the hardware with output mode and state.
 *
 * Return 0 on success, or a negative error code if an I2C communication error
 * occurred.
 */
static int
as3645a_set_control(struct as3645a *flash, enum as_mode mode, bool on)
{
	u8 reg;

	/* Configure output parameters and operation mode. */
	reg = (flash->peak << AS_CONTROL_COIL_PEAK_SHIFT)
	    | TORCH_IN_STANDBY
	    | (on ? AS_CONTROL_OUT_ON : 0)
	    | mode;

	if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH &&
	    flash->strobe_source == V4L2_FLASH_STROBE_SOURCE_EXTERNAL) {
		if (flash->platform_data->setup_ext_strobe)
			flash->platform_data->setup_ext_strobe(1);
		reg |= AS_CONTROL_STROBE_TYPE_LEVEL
		    |  AS_CONTROL_STROBE_ON;
	}

	return as3645a_write(flash, AS_CONTROL_REG, reg);
}

/*
 * as3645a_set_output - Configure output and operation mode
 * @flash: Flash controller
 * @strobe: Strobe the flash (only valid in flash mode)
 *
 * Turn the LEDs output on/off and set the operation mode based on the current
 * parameters.
 *
 * The AS3645A can't control the indicator LED independently of the flash/torch
 * LED. If the flash controller is in V4L2_FLASH_LED_MODE_NONE mode, set the
 * chip to indicator mode. Otherwise set it to assist light (torch) or flash
 * mode.
 *
 * In indicator and assist modes, turn the output on/off based on the indicator
 * and torch currents. In software strobe flash mode, turn the output on/off
 * based on the strobe parameter.
 */
static int as3645a_set_output(struct as3645a *flash, bool strobe)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	enum as_mode mode;
	bool on;
	int ret;

	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		on = flash->indicator_current != 0;
		mode = AS_MODE_INDICATOR;
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		on = true;
		mode = AS_MODE_ASSIST;
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		on = strobe;
		mode = AS_MODE_FLASH;
		break;
	default:
		return 0;
	}

	/* Make sure we won't overheat if we perform the requested action. */
	ret = as3645a_overheat_protect(flash, mode, on);
	if (ret) {
		dev_dbg(&client->dev, "Flash overheat protection triggered\n");
		return ret;
	}

	/* Configure output parameters and operation mode. */
	ret = as3645a_set_control(flash, mode, on);
	if (ret < 0)
		return ret;

	/* Update the flash state. Move to
	 * - AS_STATE_FLASH when the flash is strobed
	 * - AS_STATE_COOLDOWN when turning the output off after a flash strobe
	 *   or after a long torch time
	 * - AS_STATE_READY when turning the output off otherwise
	 */
	if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH && strobe) {
		as3645a_set_state(flash, AS_STATE_FLASH);
		flash->flash_count++;
	} else if (flash->active && !on) {
		if (flash->torch_on_time > MAX_ASSIST_LIGHT_TIME ||
		    flash->mode == AS_MODE_FLASH)
			as3645a_set_state(flash, AS_STATE_COOLDOWN);
		else
			as3645a_set_state(flash, AS_STATE_READY);
	}

	flash->mode = mode;
	if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH ||
	    flash->led_mode == V4L2_FLASH_LED_MODE_TORCH) {
		flash->led_state = on;
		flash->active = on;
	} else {
		flash->led_state = false;
		flash->active = false;
	}

	return 0;
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
	int fault;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT:
		fault = as3645a_read_fault(flash);
		if (fault < 0)
			return fault;

		if (fault & AS_FAULT_INFO_SHORT_CIRCUIT)
			ctrl->cur.val |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (fault & AS_FAULT_INFO_OVER_TEMPERATURE)
			ctrl->cur.val |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (fault & AS_FAULT_INFO_TIMEOUT)
			ctrl->cur.val |= V4L2_FLASH_FAULT_TIMEOUT;
		if (fault & AS_FAULT_INFO_OVER_VOLTAGE)
			ctrl->cur.val |= V4L2_FLASH_FAULT_OVER_VOLTAGE;
		if (fault & AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT)
			ctrl->cur.val |= V4L2_FLASH_FAULT_OVER_CURRENT;
		if (fault & AS_FAULT_INFO_INDICATOR_LED)
			ctrl->cur.val |= V4L2_FLASH_FAULT_INDICATOR;
		break;

	case V4L2_CID_FLASH_READY:
		ctrl->cur.val = 0;
		if (!as3645a_is_active(flash) && as3645a_is_ready(flash))
			ctrl->cur.val = 1;
		break;

	case V4L2_CID_FLASH_STROBE_STATUS:
		ctrl->cur.val = (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH)
			      && flash->active;
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
	int ret;

	dev_dbg(&client->dev, "S_CTRL %08x:%d\n", ctrl->id, ctrl->val);

	/* If a control that doesn't apply to the current mode is modified,
	 * we store the value and return immediately. The setting will be
	 * applied when the LED mode is changed. Otherwise we apply the setting
	 * immediately.
	 */

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		if (flash->indicator_current)
			return -EBUSY;

		ret = as3645a_set_config(flash);
		if (ret < 0)
			return ret;

		flash->led_mode = ctrl->val;
		return as3645a_set_output(flash, false);

	case V4L2_CID_FLASH_STROBE_SOURCE:
		flash->strobe_source = ctrl->val;

		/* Applies to flash mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		return as3645a_set_output(flash, false);

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			return -EBUSY;

		return as3645a_set_output(flash, true);

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			return -EBUSY;

		return as3645a_set_output(flash, false);

	case V4L2_CID_FLASH_TIMEOUT:
		/* This is ugly. The step value is different depending on strobe
		 * mode, so only round the value when using I2C strobing.
		 */
		if (flash->strobe_source == V4L2_FLASH_STROBE_SOURCE_EXTERNAL &&
		    flash->platform_data->set_strobe_width != NULL) {
			flash->platform_data->set_strobe_width(ctrl->val);
		} else {
			if (ctrl->val < AS3645A_FLASH_TIMEOUT_MIN)
				ctrl->val = AS3645A_FLASH_TIMEOUT_MIN;
			ctrl->val = ctrl->val / AS3645A_FLASH_TIMEOUT_STEP
				  * AS3645A_FLASH_TIMEOUT_STEP;
		}
		flash->timeout = ctrl->val;

		/* Applies to flash mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		return as3645a_set_config(flash);

	case V4L2_CID_FLASH_INTENSITY:
		flash->flash_current = (ctrl->val - AS3645A_FLASH_INTENSITY_MIN)
				     / AS3645A_FLASH_INTENSITY_STEP;

		/* Applies to flash mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		return as3645a_set_config(flash);

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		flash->assist_current =
			(ctrl->val - AS3645A_TORCH_INTENSITY_MIN)
			/ AS3645A_TORCH_INTENSITY_STEP;

		/* Applies to torch mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_TORCH)
			break;

		return as3645a_set_config(flash);

	case V4L2_CID_FLASH_INDICATOR_INTENSITY:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_NONE)
			return -EBUSY;

		flash->indicator_current =
			(ctrl->val - AS3645A_INDICATOR_INTENSITY_MIN)
			/ AS3645A_INDICATOR_INTENSITY_STEP;

		ret = as3645a_set_config(flash);
		if (ret < 0)
			return ret;

		if ((ctrl->val == 0) == (ctrl->cur.val == 0))
			break;

		return as3645a_set_output(flash, false);

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops as3645a_ctrl_ops = {
	.g_volatile_ctrl = as3645a_get_ctrl,
	.s_ctrl = as3645a_set_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

/* Put device into know state. */
static int as3645a_setup(struct as3645a *flash)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int ret;

	/* clear errors */
	ret = as3645a_read(flash, AS_FAULT_INFO_REG);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "Fault info: %02x\n", ret);

	ret = as3645a_set_config(flash);
	if (ret < 0)
		return ret;

	ret = as3645a_set_output(flash, false);
	if (ret < 0)
		return ret;

	/* read status */
	ret = as3645a_read_fault(flash);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "AS_INDICATOR_AND_TIMER_REG: %02x\n",
		as3645a_read(flash, AS_INDICATOR_AND_TIMER_REG));
	dev_dbg(&client->dev, "AS_CURRENT_SET_REG: %02x\n",
		as3645a_read(flash, AS_CURRENT_SET_REG));
	dev_dbg(&client->dev, "AS_CONTROL_REG: %02x\n",
		as3645a_read(flash, AS_CONTROL_REG));

	return ret & ~AS_FAULT_INFO_LED_AMOUNT ? -EIO : 0;
}

static int __as3645a_set_power(struct as3645a *flash, int on)
{
	int ret;

	if (!on)
		as3645a_set_control(flash, AS_MODE_EXT_TORCH, false);

	if (flash->platform_data && flash->platform_data->set_power) {
		ret = flash->platform_data->set_power(&flash->subdev, on);
		if (ret < 0)
			return ret;
	}

	return on ? as3645a_setup(flash) : 0;
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

	flash->power_count += on ? 1 : -1;
	WARN_ON(flash->power_count < 0);

done:
	mutex_unlock(&flash->power_lock);
	return ret;
}

static int as3645a_registered(struct v4l2_subdev *sd)
{
	struct as3645a *flash = to_as3645a(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int rval, man, model, rfu, version;
	const char *factory;

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

	/* Setup default values. This makes sure that the chip is in a known
	 * state, in case the power rail can't be controlled.
	 */
	rval = as3645a_setup(flash);

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

static const struct v4l2_subdev_ops as3645a_ops = {
	.core = &as3645a_core_ops,
};

static const struct v4l2_subdev_internal_ops as3645a_internal_ops = {
	.registered = as3645a_registered,
	.open = as3645a_open,
	.close = as3645a_close,
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

	dev_dbg(&client->dev, "Resume %s\n", rval < 0 ? "fail" : "ok");

	return rval;
}

#else

#define as3645a_suspend	NULL
#define as3645a_resume	NULL

#endif /* CONFIG_PM */

/*
 * as3645a_init_controls - Create controls
 * @flash: The flash
 *
 * The number of LEDs reported in platform data is used to compute default
 * limits. Parameters passed through platform data can override those limits.
 */
static int as3645a_init_controls(struct as3645a *flash)
{
	struct as3645a_flash_torch_parms *flash_params = NULL;
	bool use_ext_strobe = false;
	unsigned int leds = 2;
	struct v4l2_ctrl *ctrl;
	int minimum;
	int maximum;

	if (flash->platform_data) {
		if (flash->platform_data->num_leds)
			leds = flash->platform_data->num_leds;

		flash_params = flash->platform_data->flash_torch_limits;
		use_ext_strobe = flash->platform_data->use_ext_flash_strobe;
	}

	v4l2_ctrl_handler_init(&flash->ctrls, 11);

	/* V4L2_CID_FLASH_LED_MODE */
	v4l2_ctrl_new_std_menu(&flash->ctrls, &as3645a_ctrl_ops,
			       V4L2_CID_FLASH_LED_MODE, 2, ~7,
			       V4L2_FLASH_LED_MODE_NONE);

	/* V4L2_CID_FLASH_STROBE_SOURCE */
	v4l2_ctrl_new_std_menu(&flash->ctrls, &as3645a_ctrl_ops,
			       V4L2_CID_FLASH_STROBE_SOURCE,
			       use_ext_strobe ? 1 : 0, use_ext_strobe ? ~3 : ~1,
			       V4L2_FLASH_STROBE_SOURCE_SOFTWARE);

	flash->strobe_source = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;

	/* V4L2_CID_FLASH_STROBE */
	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);

	/* V4L2_CID_FLASH_STROBE_STOP */
	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);

	/* V4L2_CID_FLASH_STROBE_STATUS */
	ctrl = v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
				 V4L2_CID_FLASH_STROBE_STATUS, 0, 1, 1, 1);
	if (ctrl != NULL)
		ctrl->is_volatile = 1;

	/* V4L2_CID_FLASH_TIMEOUT */
	if (flash_params) {
		minimum = flash_params->timeout_min;
		maximum = flash_params->timeout_max;
	} else {
		minimum = 1;
		maximum = AS3645A_FLASH_TIMEOUT_MAX;
	}

	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_TIMEOUT, minimum, maximum,
			  1, maximum);

	flash->timeout = maximum;

	/* V4L2_CID_FLASH_INTENSITY */
	if (flash_params) {
		minimum = flash_params->flash_min_current;
		maximum = flash_params->flash_max_current;
	} else {
		minimum = AS3645A_FLASH_INTENSITY_MIN;
		maximum = leds == 1 ? AS3645A_FLASH_INTENSITY_MAX_1LED
				    : AS3645A_FLASH_INTENSITY_MAX_2LEDS;
	}

	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_INTENSITY, minimum, maximum,
			  AS3645A_FLASH_INTENSITY_STEP, maximum);

	flash->flash_current = (maximum - AS3645A_FLASH_INTENSITY_MIN)
			     / AS3645A_FLASH_INTENSITY_STEP;

	/* V4L2_CID_FLASH_TORCH_INTENSITY */
	if (flash_params) {
		minimum = flash_params->torch_min_current;
		maximum = flash_params->torch_max_current;
	} else {
		minimum = AS3645A_TORCH_INTENSITY_MIN;
		maximum = AS3645A_TORCH_INTENSITY_MAX;
	}

	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_TORCH_INTENSITY, minimum, maximum,
			  AS3645A_TORCH_INTENSITY_STEP, minimum);

	flash->assist_current = (minimum - AS3645A_TORCH_INTENSITY_MIN)
			      / AS3645A_TORCH_INTENSITY_STEP;

	/* V4L2_CID_FLASH_INDICATOR_INTENSITY */
	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_INDICATOR_INTENSITY,
			  AS3645A_INDICATOR_INTENSITY_MIN,
			  AS3645A_INDICATOR_INTENSITY_MAX,
			  AS3645A_INDICATOR_INTENSITY_STEP,
			  AS3645A_INDICATOR_INTENSITY_MIN);

	flash->indicator_current = 0;

	/* V4L2_CID_FLASH_FAULT */
	ctrl = v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
				 V4L2_CID_FLASH_FAULT, 0,
				 V4L2_FLASH_FAULT_OVER_VOLTAGE |
				 V4L2_FLASH_FAULT_TIMEOUT |
				 V4L2_FLASH_FAULT_OVER_TEMPERATURE |
				 V4L2_FLASH_FAULT_SHORT_CIRCUIT, 0, 0);
	if (ctrl != NULL)
		ctrl->is_volatile = 1;

	/* V4L2_CID_FLASH_READY */
	ctrl = v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
				 V4L2_CID_FLASH_READY, 0, 1, 1, 1);
	if (ctrl != NULL)
		ctrl->is_volatile = 1;

	flash->subdev.ctrl_handler = &flash->ctrls;

	return flash->ctrls.error;
}

static int as3645a_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct as3645a *flash;
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

	mutex_init(&flash->power_lock);

	init_timer(&flash->timer);
	flash->timer.function = as3645_state_timer_func;
	flash->timer.data = (unsigned long)flash;
	spin_lock_init(&flash->state_lock);

	/* FIXME: These are hard coded for now */
	flash->vref = 0;	/* 0V */
	flash->peak = 2;	/* 1.75A */

	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	flash->mode = AS_MODE_EXT_TORCH;
	flash->active = false;
	flash->torch_on_time = 0;
	flash->flash_count = 0;
	flash->flash_on_time = 0;
	flash->flash_off_time = 0;
	flash->state = AS_STATE_READY;

	ret = as3645a_init_controls(flash);
	if (ret < 0) {
		kfree(flash);
		return ret;
	}

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
