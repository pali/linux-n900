/*
 * This file is combines HW independent part of AV ECI (Enhancement Control
 * Interface) and ACI (Accessory Control Interface) drivers
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Tapio Vihuri <tapio.vihuri@nokia.com>
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

#define DEBUG
/* #define VERBOSE_DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/seq_file.h>
#include <linux/mfd/aci.h>
#include <linux/input.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <sound/jack.h>
#include <plat/dfl61-audio.h>
#include <linux/input/eci.h>

#define ECI_DRIVERNAME	"ECI_accessory"

#define ECI_WAIT_SEND_BUTTON		5	/* ms */
#define ECI_WAIT_BUS_SETTLE_LO		20000	/* us */
#define ECI_WAIT_BUS_SETTLE_HI		20100	/* us */
#define ECI_TRY_GET_MEMORY		2000	/* ms */
#define ECI_TRY_INIT_IO			200	/* ms */
#define ECI_TRY_SET_MIC			200	/* ms */
#define ECI_KEY_REPEAT_INTERVAL		400	/* ms */

#define ECI_EKEY_BLOCK_ID			0xb3
#define ECI_ENHANCEMENT_FEATURE_BLOCK_ID	0x02

/* ECI Inputs */
#define ECI_NIL_FEATURE			0x00 /* No feature */
#define ECI_IGNS			0x01 /* Ignition Sense */
#define ECI_CK_HANDSET_HOOK		0x02 /* Car-Kit Handset Hook */
#define ECI_POWER_SUPPLY		0x03 /* Power Supply/Car Battery Det  */
#define ECI_EXT_AUD_IN			0x06 /* External audio In */
#define ECI_SEND_END_VR			0x07 /* Send, End, and Voice Recogn */
#define ECI_HD_PLUG			0x08 /* Headphone plug */
#define ECI_DEV_POWER_REQ		0x0a /* Device Power Request */
#define ECI_VOL_UP			0x0b /* Volume Up */
#define ECI_VOL_DOWN			0x0c /* Volume Down */
#define ECI_PLAY_PAUSE_CTRL		0x0d /* Play / Pause */
#define ECI_STOP			0x0e /* Stop */
#define ECI_NEXT_FF_AUTOSRC_UP		0x0f /* Next/Fast Fward/Autosearch up */
#define ECI_PREV_REW_AUTOSEARCH_DOWN	0x10 /* Prev/Rewind/Autosearch down */
#define ECI_POC				0x11 /* Push to Talk over Cellular */
#define ECI_SYNC_BTN			0x14 /* Synchronization Button */
#define ECI_MUSIC_RADIO_OFF_SELECTOR	0x15 /* Music/Radio/Off Selector */
#define ECI_REDIAL			0x16 /* Redial */
#define ECI_LEFT_SOFT_KEY		0x17 /* Left Soft Key */
#define ECI_RIGHT_SOFT_KEY		0x18 /* Right Soft key */
#define ECI_SEND_KEY			0x19 /* Send key */
#define ECI_END_KEY			0x1a /* End key */
#define ECI_MIDDLE_SOFT_KEY		0x1b /* Middle Soft key */
#define ECI_UP				0x1c /* UP key/joystick direction */
#define ECI_DOWN			0x1d /* DOWN key/joystick direction */
#define ECI_RIGHT			0x1e /* RIGHT key/joystick direction */
#define ECI_LEFT			0x1f /* LEFT key/joystick direction */
#define ECI_SYMBIAN_NAVY_KEY		0x20 /* Symbian Application key */
#define ECI_TERMINAL_APP_CTRL_IN	0x21 /* Terminal Applicat Ctrl Input */
#define ECI_USB_CLASS_SWITCHING		0x23 /* USB Class Switching */
#define ECI_MUTE			0x24 /* Mute */
/* ECI Outputs */
#define ECI_CRM				0x82 /* Car Radio Mute */
#define ECI_PWR				0x83 /* Power */
#define ECI_AUD_AMP			0x85 /* Audio Amplifier */
#define ECI_EXT_AUD_SWITCH		0x86 /* External Audio Switch */
#define ECI_HANDSET_AUDIO		0x87 /* Handset Audio */
#define ECI_RING_INDICATOR		0x88 /* Ringing Indicator */
#define ECI_CALL_ACTIVE			0x89 /* Call Active */
#define ECI_ENHANCEMENT_DETECTED	0x8b /* Enhancement Detected */
#define ECI_AUDIO_BLOCK_IN_USE		0x8e /* Audio Block In Use */
#define ECI_STEREO_AUDIO_ACTIVE		0x8f /* stereo audio used in terminal */
#define ECI_MONO_AUDIO_ACTIVE		0x90 /* mono audio used in terminal */
#define ECI_TERMINAL_APP_CTRL_OUT	0x91 /* Terminal Applicat Ctrl Output */

/*
 * most of these are keys
 * switch codes are put on top of keys (KEY_MAX ->)
 */
static int eci_codes[] = {
	KEY_UNKNOWN,		/* 0  ECI_NIL_FEATURE */
	KEY_UNKNOWN,		/* 1  ECI_IGNS */
	KEY_UNKNOWN,		/* 2  ECI_CK_HANDSET_HOOK */
	KEY_BATTERY,		/* 3  ECI_POWER_SUPPLY */
	KEY_RESERVED,		/* 4  ECI feature not defined */
	KEY_RESERVED,		/* 5  ECI feature not defined */
	KEY_AUDIO,		/* 6  ECI_EXT_AUD_IN */
	KEY_PHONE,		/* 7  ECI_SEND_END_VR */
	KEY_MAX + SW_HEADPHONE_INSERT,	/* 8  ECI_HD_PLUG, type switchs */
	KEY_RESERVED,		/* 9  ECI feature not defined */
	KEY_UNKNOWN,		/* 10 ECI_DEV_POWER_REQ */
	KEY_VOLUMEUP,		/* 11 ECI_VOL_UP */
	KEY_VOLUMEDOWN,		/* 12 ECI_VOL_DOWN */
	KEY_PLAYPAUSE,		/* 13 ECI_PLAY_PAUSE_CTRL */
	KEY_STOP,		/* 14 ECI_STOP */
	KEY_FORWARD,		/* 15 ECI_NEXT_FF_AUTOSRC_UP */
	KEY_REWIND,		/* 16 ECI_PREV_REW_AUTOSEARCH_DOWN */
	KEY_UNKNOWN,		/* 17 ECI_POC */
	KEY_RESERVED,		/* 18 ECI feature not defined */
	KEY_RESERVED,		/* 19 ECI feature not defined */
	KEY_UNKNOWN,		/* 20 ECI_SYNC_BTN */
	KEY_RADIO,		/* 21 ECI_MUSIC_RADIO_OFF_SELECTOR */
	KEY_UNKNOWN,		/* 22 ECI_REDIAL */
	KEY_UNKNOWN,		/* 23 ECI_LEFT_SOFT_KEY */
	KEY_UNKNOWN,		/* 24 ECI_RIGHT_SOFT_KEY */
	KEY_SEND,		/* 25 ECI_SEND_KEY */
	KEY_END,		/* 26 ECI_END_KEY */
	KEY_UNKNOWN,		/* 27 ECI_MIDDLE_SOFT_KEY */
	KEY_UP,			/* 28 ECI_UP */
	KEY_DOWN,		/* 29 ECI_DOWN */
	KEY_RIGHT,		/* 30 ECI_RIGHT */
	KEY_LEFT,		/* 31 ECI_LEFT */
	KEY_UNKNOWN,		/* 32 ECI_SYMBIAN_NAVY_KEY */
	KEY_UNKNOWN,		/* 33 ECI_TERMINAL_APP_CTRL_IN */
	KEY_RESERVED,		/* 34 ECI feature not defined */
	KEY_UNKNOWN,		/* 35 ECI_USB_CLASS_SWITCHING */
	KEY_MUTE,		/* 36 ECI_MUTE */
};

static struct aci_cb aci_callback;
static struct eci_cb eci_callback;
static struct dfl61audio_hsmic_event hsmic_event;

static struct eci_data *the_eci;

/* returns size of accessory memory or error */
static int eci_get_ekey(struct eci_data *eci, int *key)
{
	u8 buf[4];
	struct block {
		u8 id;
		u8 len;
		u16 size;
	} *ekey = (void *)buf;
	int ret;

	/* read always four bytes */
	ret = eci->eci_hw_ops->acc_read_direct(0, buf);
	if (ret)
		return ret;

	if (ekey->id != ECI_EKEY_BLOCK_ID)
		return -ENODEV;

	*key = be16_to_cpu(ekey->size);

	return 0;
}

static ssize_t show_eci_memory(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!the_eci->mem_ok)
		return -ENXIO;

	memcpy(buf, the_eci->memory, the_eci->mem_size);

	return the_eci->mem_size;
}

static DEVICE_ATTR(memory, S_IRUGO, show_eci_memory, NULL);

static struct attribute *eci_attributes[] = {
	&dev_attr_memory.attr,
	NULL
};

static struct attribute_group eci_attr_group = {
	.attrs = eci_attributes
};

/* read ECI device memory into buffer */
static int eci_get_memory(struct eci_data *eci, int *restart)
{
	int i, ret;

	for (i = *restart; i < eci->mem_size; i += 4) {
		ret = eci->eci_hw_ops->acc_read_direct(i, eci->memory + i);
		*restart = i;
		if (ret)
			return ret;
	}

	return ret;
}

/*
 * this should be really init_features, but most oftens these are just buttons
 */
static int eci_init_buttons(struct eci_data *eci)
{
	struct enchancement_features_fixed *eff = eci->e_features_fix;
	u8 n, mireg;
	int ret;
	u8 buf[4];

	n = eff->number_of_features;

	if (n > ECI_MAX_FEATURE_COUNT)
		return -EINVAL;

	ret = eci->eci_hw_ops->acc_read_reg(ECICMD_MASTER_INT_REG, buf, 1);
	if (ret)
		return ret;

	mireg = buf[0];
	mireg &= ~ECI_INT_ENABLE;
	mireg |= ECI_INT_LEN_120US | ECI_INT_DELAY_ENABLE;

	ret = eci->eci_hw_ops->acc_write_reg(ECICMD_MASTER_INT_REG, mireg);
	if (ret) {
		dev_err(eci->dev, "Unable to set accessory interrupts\n");
		return ret;
	}

	usleep_range(ECI_WAIT_BUS_SETTLE_LO, ECI_WAIT_BUS_SETTLE_HI);
	mireg |= ECI_INT_ENABLE;
	ret = eci->eci_hw_ops->acc_write_reg(ECICMD_MASTER_INT_REG, mireg);
	if (ret) {
		dev_err(eci->dev, "Unable to set accessory interrupts\n");
		return ret;
	}

	usleep_range(ECI_WAIT_BUS_SETTLE_LO, ECI_WAIT_BUS_SETTLE_HI);

	return ret;
}

/* find "enchangement features" block from buffer */
static int eci_get_enchancement_features(struct eci_data *eci)
{
	u8 *mem = (void *)eci->memory;
	struct block {
		u8 id;
		u8 len;
	} *b = (void *)mem;
	struct block *mem_end = (void *)(eci->memory + eci->mem_size);

	if (b->id != ECI_EKEY_BLOCK_ID)
		return -ENODEV;

	do {
		dev_vdbg(eci->dev, "skip BLOCK 0x%02x, LEN 0x%02x\n",
				b->id, b->len);
		if (!b->len)
			return -EINVAL;

		mem += b->len;
		b = (void *)mem;
		eci->e_features_fix = (void *)b;
		dev_vdbg(eci->dev, "found BLOCK 0x%02x, LEN 0x%02x\n",
				b->id, b->len);
		if (b->id == ECI_ENHANCEMENT_FEATURE_BLOCK_ID)
			return 0;
	} while (b < mem_end);

	return -ENFILE;
}

/*
 * find out ECI features
 * all ECI memory block parsing is done here, be carefull as
 * pointers to memory tend to go wrong easily
 * ECI "Enhancement Features block is variable size, so we try to
 * catch pointers out of block due memory reading errors etc
 *
 * I/O support field is not implemented
 * data direction field is not implemented, nor writing to the ECI I/O
 */
static int eci_parse_enchancement_features(struct eci_data *eci)
{
	struct enchancement_features_fixed *eff = eci->e_features_fix;
	struct enchancement_features_variable *efv = &eci->e_features_var;
	int i;
	u8 n, k;
	void *mem_end = (void *)((u8 *)eff + eff->length);

	dev_vdbg(eci->dev, "block id 0x%02x length 0x%02x connector "
			"configuration 0x%02x\n", eff->block_id, eff->length,
			eff->connector_conf);
	n = eff->number_of_features;
	dev_vdbg(eci->dev, "number of features %d\n", n);

	if (n > ECI_MAX_FEATURE_COUNT)
		return -EINVAL;

	k = DIV_ROUND_UP(n, 8);
	dev_vdbg(eci->dev, "I/O support bytes count %d\n", k);

	efv->io_support = &eff->number_of_features + 1;
	/* efv->io_functionality[0] is not used! pins are in 1..31 range */
	efv->io_functionality = efv->io_support + k - 1;
	efv->active_state = efv->io_functionality + n + 1;

	if ((void *)&efv->active_state[k] > mem_end)
		return -EINVAL;

	/* last part of block */
	for (i = 0; i < k; i++)
		dev_vdbg(eci->dev, "active_state[%d] 0x%02x\n", i,
				efv->active_state[i]);

	eci->buttons_data.buttons_up_mask = ~be32_to_cpu(
			(*(u32 *)efv->active_state)) & (BIT_MASK(n + 1) - 1);
	dev_dbg(eci->dev, "buttons mask 0x%08x\n",
			eci->buttons_data.buttons_up_mask);

	/* ECI accessory responces as many bytes needed for used I/O pins
	 * up to four bytes, when lines 24..31 are used
	 * all tested ECI accessories how ever return two data bytes
	 * event though there are less than eight I/O pins
	 *
	 * so we get alway reading error if there are less than eight I/Os
	 * meanwhile just use this kludge, FIXME
	 */
	k = DIV_ROUND_UP(n + 1, 8);
	if (k == 1)
		k = 2;
	eci->port_reg_count = k;

	return 0;
}

static int eci_init_accessory(struct eci_data *eci)
{
	int ret, key = 0, restart = 0;
	unsigned long future;

	eci->mem_ok = false;
	usleep_range(ECI_WAIT_BUS_SETTLE_LO, ECI_WAIT_BUS_SETTLE_HI);

	ret = eci->eci_hw_ops->acc_reset();
	usleep_range(ECI_WAIT_BUS_SETTLE_LO, ECI_WAIT_BUS_SETTLE_HI);

	ret = eci->eci_hw_ops->acc_write_reg(ECICMD_MIC_CTRL, ECI_MIC_OFF);
	if (ret) {
		dev_err(eci->dev, "Unable to control headset microphone\n");
		return ret;
	}

	/* get ECI ekey block to determine memory size */
	future = jiffies + msecs_to_jiffies(ECI_TRY_GET_MEMORY);
	do {
		ret = eci_get_ekey(eci, &key);
		if (time_is_before_jiffies(future))
			break;
	} while (ret);

	if (ret)
		return ret;

	eci->mem_size = key;
	if (eci->mem_size > ECI_MAX_MEM_SIZE)
		return -EINVAL;

	/* get ECI memory */
	future = jiffies + msecs_to_jiffies(ECI_TRY_GET_MEMORY);
	do {
		ret = eci_get_memory(eci, &restart);
		if (time_is_before_jiffies(future))
			break;
	} while (ret);

	if (ret)
		return ret;

	if (eci_get_enchancement_features(eci))
		return -EIO;

	if (eci_parse_enchancement_features(eci))
		return -EIO;

	/*
	 * configure ECI buttons now when we know how after parsing
	 * enchancement features
	 */
	usleep_range(ECI_WAIT_BUS_SETTLE_LO, ECI_WAIT_BUS_SETTLE_HI);
	future = jiffies + msecs_to_jiffies(ECI_TRY_INIT_IO);
	do {
		ret = eci_init_buttons(eci);
		if (time_is_before_jiffies(future))
			break;
	} while (ret);

	if (ret) {
		dev_err(eci->dev, "Unable to init buttons\n");
		return ret;
	}

	eci->mem_ok = true;
	usleep_range(ECI_WAIT_BUS_SETTLE_LO, ECI_WAIT_BUS_SETTLE_HI);

	if (eci->eci_hw_ops->acc_write_reg(ECICMD_MIC_CTRL, eci->mic_state))
		dev_err(eci->dev, "Unable to control headset microphone\n");

	return 0;
}

static int init_accessory_input(struct eci_data *eci)
{
	int err, i, code;

	eci->acc_input = input_allocate_device();
	if (!eci->acc_input) {
		dev_err(eci->dev, "Error allocating input device: %d\n",
				__LINE__);
		return -ENOMEM;
	}

	eci->acc_input->name = "ECI Accessory";

	/* codes on top of KEY_MAX are switch events */
	for (i = 0; i < ARRAY_SIZE(eci_codes); i++) {
		code = eci_codes[i];
		if (code >= KEY_MAX) {
			code -= KEY_MAX;
			set_bit(code, eci->acc_input->swbit);
		} else {
			set_bit(code, eci->acc_input->keybit);
		}
	}

	set_bit(EV_KEY, eci->acc_input->evbit);
	set_bit(EV_SW, eci->acc_input->evbit);
	set_bit(EV_REP, eci->acc_input->evbit);

	err = input_register_device(eci->acc_input);
	if (err) {
		dev_err(eci->dev, "Error registering input device: %d\n",
				__LINE__);
		goto err_free_dev;
	}

	/* must set after input_register_device() */
	eci->acc_input->rep[REP_PERIOD] = ECI_KEY_REPEAT_INTERVAL;

	return 0;

err_free_dev:
	input_free_device(eci->acc_input);
	return err;
}

static void remove_accessory_input(struct eci_data *eci)
{
	input_unregister_device(eci->acc_input);
}

/* press/release button */
static int eci_get_button(struct eci_data *eci)
{
	struct eci_buttons_data *b = &eci->buttons_data;

	if (b->windex < ECI_BUTTON_BUF_SIZE) {
		if (b->buttons_buf[b->windex] == 0)
			b->buttons_buf[b->windex] = b->buttons;
		else
			dev_err(eci->dev, "ECI button queue owerflow %d\n",
			       __LINE__);
	}
	b->windex++;
	if (b->windex == ECI_BUTTON_BUF_SIZE)
		b->windex = 0;

	return 0;
}

/* intended to use ONLY inside eci_parse_button() ! */
#define ACTIVE_STATE(x) (be32_to_cpu(*(u32 *)efv->active_state) & BIT(x-1))
#define BUTTON_STATE(x) ((buttons & BIT(x))>>1)

static void eci_parse_button(struct eci_data *eci, u32 buttons)
{
	int pin, code, state = 0;
	u8 n, io_fun;
	struct enchancement_features_variable *efv = &eci->e_features_var;
	struct enchancement_features_fixed *eff = eci->e_features_fix;

	n = eff->number_of_features;

	for (pin = 1; pin <= n; pin++)
		state += (BUTTON_STATE(pin) == ACTIVE_STATE(pin));

	if (state == n) {
		dev_err(eci->dev, "report all buttons down, reject\n");
		return;
	}

	for (pin = 1; pin <= n; pin++) {
		io_fun = efv->io_functionality[pin] & ~BIT(7);
		if (io_fun > ECI_MUTE)
			break;
		code = eci_codes[io_fun];
		state = (BUTTON_STATE(pin) == ACTIVE_STATE(pin));
		if (state)
			dev_dbg(eci->dev, "I/O functionality 0x%02x "
					"at pin %d\n", io_fun, pin);
		if (code >= KEY_MAX)
			input_report_switch(eci->acc_input, code - KEY_MAX,
					state);
		else
			input_report_key(eci->acc_input, code, state);
	}
	input_sync(eci->acc_input);
}

static int eci_send_button(struct eci_data *eci)
{
	int i;
	struct enchancement_features_fixed *eff = eci->e_features_fix;
	struct eci_buttons_data *b = &eci->buttons_data;
	u8 n;

	n = eff->number_of_features;

	if (n > ECI_MAX_FEATURE_COUNT)
		return -EINVAL;
	/*
	 * codes on top of KEY_MAX are switch events
	 * let input system take care multiple key events
	 */

	for (i = 0; i < ECI_BUTTON_BUF_SIZE; i++) {
		if (b->buttons_buf[b->rindex] == 0)
			break;

		eci_parse_button(eci, b->buttons_buf[b->rindex]);

		b->buttons_buf[b->rindex] = 0;
		b->rindex++;
		if (b->rindex == ECI_BUTTON_BUF_SIZE)
			b->rindex = 0;
	}

	return 0;
}

static void aci_button_event(bool button_down, void *priv)
{
	struct eci_data *eci = priv;

	if (!eci)
		return;

	if (button_down)
		input_report_key(eci->acc_input, KEY_PHONE/* BTN_0 */, 1);
	else
		input_report_key(eci->acc_input, KEY_PHONE/* BTN_0 */, 0);

	input_sync(eci->acc_input);
}

/* ACI driver call this in ECI accessory event */
static void eci_accessory_event(int event, void *priv)
{
	struct eci_data *eci = priv;
	struct eci_buttons_data *b = &eci->buttons_data;
	int delay = 0;
	int ret = 0;

	eci->event = event;
	switch (event) {
	case ECI_EVENT_PLUG_IN:
		dev_dbg(eci->dev, "ECI_EVENT_PLUG_IN\n");
		ret = eci_init_accessory(eci);
		if (ret) {
			dev_err(eci->dev, "Accessory init %s%s%s%sat: %d\n",
					ret & ACI_COMMERR ? "COMMERR " : "",
					ret & ACI_FRAERR ? "FRAERR " : "",
					ret & ACI_RESERR ? "RESERR " : "",
					ret & ACI_COLL ? "COLLERR " : "",
					__LINE__);
			break;
		}
		if (eci->jack_report)
			eci->jack_report(SND_JACK_HEADSET);
		dev_dbg(eci->dev, "ECI jack event reported\n");
		break;
	case ECI_EVENT_PLUG_OUT:
		eci->mem_ok = false;
		break;
	case ECI_EVENT_BUTTON:
		dev_dbg(eci->dev, "buttons 0x%08x\n", b->buttons);

		ret = eci_get_button(eci);
		if (ret)
			dev_err(eci->dev, "error %d getting buttons\n", ret);

		delay = msecs_to_jiffies(ECI_WAIT_SEND_BUTTON);
		schedule_delayed_work(&eci->eci_ws, delay);
		break;
	default:
		dev_err(eci->dev, "unknown event %d: %d\n", event, __LINE__);
		break;
	}

	return;
}

static void eci_hsmic_event(void *priv, bool on)
{
	struct eci_data *eci = priv;
	unsigned long future;
	int ret;

	if (!eci || !eci->mem_ok)
		return;

	dev_vdbg(eci->dev, "ECI mic %s\n", on ? "auto" : "off");

	if (on)
		eci->mic_state = ECI_MIC_AUTO;
	else
		eci->mic_state = ECI_MIC_OFF;

	future = jiffies + msecs_to_jiffies(ECI_TRY_SET_MIC);
	do {
		ret = eci->eci_hw_ops->acc_write_reg(ECICMD_MIC_CTRL,
				eci->mic_state);
		if (time_is_before_jiffies(future))
			break;
	} while (ret);

	if (ret)
		dev_err(eci->dev, "Unable to control headset microphone\n");
}

/*
 * eci_ws
 * general work func for several tasks
 */
static void eci_work(struct work_struct *ws)
{
	struct eci_data *eci = container_of(ws, struct eci_data, eci_ws.work);
	int ret;

	ret = eci_send_button(eci);
	if (ret)
		dev_err(eci->dev, "Error sending event: %d\n", __LINE__);
}

static struct miscdevice eci_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = ECI_DRIVERNAME,
};

struct aci_cb *aci_register(void)
{
	if (!the_eci)
		return ERR_PTR(-EBUSY);

	return &aci_callback;
}
EXPORT_SYMBOL(aci_register);

struct eci_cb *eci_register(struct eci_hw_ops *eci_ops)
{
	if (!the_eci)
		return ERR_PTR(-EBUSY);

	if (!eci_ops || !eci_ops->acc_read_direct ||
			!eci_ops->acc_read_reg || !eci_ops->acc_write_reg ||
			!eci_ops->acc_reset || !eci_ops->aci_eci_buttons)
		return ERR_PTR(-EINVAL);

	the_eci->eci_hw_ops = eci_ops;
	return &eci_callback;
}
EXPORT_SYMBOL(eci_register);

static int __init eci_probe(struct platform_device *pdev)
{
	struct eci_data *eci;
	struct eci_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	eci = kzalloc(sizeof(*eci), GFP_KERNEL);
	if (!eci)
		return -ENOMEM;

	platform_set_drvdata(pdev, eci);
	eci->dev = &pdev->dev;

	ret = misc_register(&eci_device);
	if (ret) {
		dev_err(&pdev->dev, "could not register misc_device: %d\n",
				__LINE__);
		goto err_misc;
	}

	the_eci = eci;

	aci_callback.button_event       = aci_button_event;
	aci_callback.priv               = eci;

	eci_callback.event              = eci_accessory_event;
	eci_callback.priv               = eci;

	ret = sysfs_create_group(&pdev->dev.kobj, &eci_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "could not create sysfs entries: %d\n",
				__LINE__);
		goto err_sysfs;
	}

	ret = init_accessory_input(eci);
	if (ret) {
		dev_err(&pdev->dev, "ERROR initializing accessory input: %d\n",
				__LINE__);
		goto err_input;
	}

	if (pdata) {
		if (pdata->register_hsmic_event_cb) {
			hsmic_event.private	= eci;
			hsmic_event.event	= eci_hsmic_event;
			pdata->register_hsmic_event_cb(&hsmic_event);
		}

		eci->jack_report = pdata->jack_report;
	}

	init_waitqueue_head(&eci->wait);
	INIT_DELAYED_WORK(&eci->eci_ws, eci_work);

	eci->mem_ok = false;
	eci->mic_state = ECI_MIC_OFF;

	/* init buttons_data indexes and buffer */
	memset(&eci->buttons_data, 0, sizeof(struct eci_buttons_data));
	eci->buttons_data.buttons = 0xffffffff;

	return 0;

err_input:
	sysfs_remove_group(&pdev->dev.kobj, &eci_attr_group);

err_sysfs:
	misc_deregister(&eci_device);

err_misc:
	kfree(eci);

	return ret;
}

static int __exit eci_remove(struct platform_device *pdev)
{
	struct eci_data *eci = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&eci->eci_ws);
	sysfs_remove_group(&pdev->dev.kobj, &eci_attr_group);
	remove_accessory_input(eci);
	kfree(eci);
	misc_deregister(&eci_device);

	return 0;
}

static struct platform_driver eci_driver = {
	.probe		= eci_probe,
	.remove		= __exit_p(eci_remove),
	.driver		= {
		.name	= ECI_DRIVERNAME,
		.owner	= THIS_MODULE,
	},
};

static int __init eci_init(void)
{
	return platform_driver_register(&eci_driver);
}
device_initcall(eci_init);

static void __exit eci_exit(void)
{
	platform_driver_unregister(&eci_driver);
}
module_exit(eci_exit);

MODULE_ALIAS("platform:" ECI_DRIVERNAME);
MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("ECI accessory driver");
MODULE_LICENSE("GPL");
