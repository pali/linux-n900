/*
 * twl5031-bcc.c - TWL5031 Battery Charge Control driver
 *
 * (C) 2010 Nokia Corporation
 *
 * Contact: Aliaksei Katovich <aliaksei.katovich@nokia.com>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/i2c/twl4030.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>
#include <linux/mutex.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi_regs.h>
#include <linux/notifier.h>

#define TWL5031_BCC_CTRL2		0x01
#define TWL5031_CHGMODE_SW		BIT(0)
#define TWL5031_SW_USBCHRG_EN		BIT(1)
#define TWL5031_SW_VACCHRG_EN		BIT(2)
#define TWL5031_SW_CHRG_DET		BIT(3)
#define TWL5031_SW_USB_DET_EN		BIT(4)

#define TWL5031_BCC_STS			0x02
#define TWL5031_USB_DET_STS_LO		BIT(0)
#define TWL5031_USB_DET_STS_HI		BIT(1)
#define TWL5031_USB_P_STS		BIT(2)
#define TWL5031_VAC_P_STS		BIT(3)
#define TWL5031_BATTERY_PRESENCE_STS	BIT(4)

#define TWL5031_USB_CHRG_CTRL1		0x03
#define TWL5031_USB_SW_CTRL_EN		BIT(0)
#define TWL5031_CHGD_IDX_SRC_EN_LOWV	BIT(1)
#define TWL5031_CHGD_VDX_SRC_EN_LOWV	BIT(2)
#define TWL5031_CHGD_SERX_EN_LOWV	BIT(3)
#define TWL5031_CHGD_VDX_LOWV		BIT(4)
#define TWL5031_CHGD_SERX_DP_LOWV	BIT(5)
#define TWL5031_CHGD_SERX_DM_LOWV	BIT(6)

#define TWL5031_USB_CHRG_CTRL2		0x04
#define TWL5031_USB_100			BIT(0)
#define TWL5031_USB_500			BIT(1)

#define TWL5031_USB_DET_STS_100MA	1
#define TWL5031_USB_DET_STS_500MA	2
#define TWL5031_USB_DET_STS_MASK	(TWL5031_USB_DET_STS_LO | \
					 TWL5031_USB_DET_STS_HI)

struct twl5031_bcc_data {
	/* device lock */
	struct mutex		mutex;
	struct device		*dev;
	struct workqueue_struct *wq;
	struct work_struct	work;
	struct otg_transceiver	*otg;
	struct notifier_block	nb;
	struct regulator	*usb3v1;
	struct power_supply	usb;
	u8			usb_present:1;
	u16			usb_current;	/* [mA] */
	struct power_supply	vac;
	u8			vac_present:1;
	u16			vac_current;	/* [mA] */
	unsigned long		event;
	int			mA;
	bool			dcd_online;	/* DCD status */
};

static inline int twl5031_bcc_read(u8 reg)
{
	int ret;
	u8 val;

	ret = twl4030_i2c_read_u8(TWL5031_MODULE_BCC, &val, reg);
	if (ret)
		return ret;

	return val;
}

static inline int twl5031_bcc_write(u8 reg, u8 val)
{
	return twl4030_i2c_write_u8(TWL5031_MODULE_BCC, val, reg);
}

static inline int twl5031_usb_read(u8 reg)
{
	int ret;
	u8 val;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_USB, &val, reg);
	if (ret)
		return ret;

	return val;
}

static inline int twl5031_usb_write(u8 reg, u8 val)
{
	return twl4030_i2c_write_u8(TWL4030_MODULE_USB, val, reg);
}

static inline void
twl5031_bcc_vac_update_present(struct twl5031_bcc_data *bcc, int val)
{
	if (bcc->vac_present != val) {
		bcc->vac_present = !!val;
		power_supply_changed(&bcc->vac);
	}
}

static int twl5031_bcc_usb_charger_type(struct twl5031_bcc_data *bcc)
{
	enum power_supply_type	type = POWER_SUPPLY_TYPE_USB;

	u8 reg;
	u8 func_ctrl, otg_ctrl;

	/* The transceiver must be resumed before this operation */
	otg_set_suspend(bcc->otg, 0);

	func_ctrl = twl5031_usb_read(ULPI_FUNC_CTRL);
	otg_ctrl = twl5031_usb_read(ULPI_OTG_CTRL);

	/* disable pulldowns */
	reg = ULPI_OTG_CTRL_DM_PULLDOWN | ULPI_OTG_CTRL_DP_PULLDOWN;
	twl5031_usb_write(ULPI_CLR(ULPI_OTG_CTRL), reg);

	/* full speed */
	twl5031_usb_write(ULPI_CLR(ULPI_FUNC_CTRL),
			ULPI_FUNC_CTRL_XCVRSEL_MASK);
	twl5031_usb_write(ULPI_SET(ULPI_FUNC_CTRL), ULPI_FUNC_CTRL_FULL_SPEED);

	/* enable DP pullup */
	twl5031_usb_write(ULPI_SET(ULPI_FUNC_CTRL), ULPI_FUNC_CTRL_TERMSELECT);
	udelay(100);

	reg = twl5031_usb_read(ULPI_DEBUG);

	twl5031_usb_write(ULPI_FUNC_CTRL, func_ctrl);
	twl5031_usb_write(ULPI_OTG_CTRL, otg_ctrl);

	if ((reg & 3) != 3) {
		type = POWER_SUPPLY_TYPE_USB_CDP;
	} else {
		type = POWER_SUPPLY_TYPE_USB_DCP;
		otg_set_suspend(bcc->otg, 1);
	}

	return type;
}

/**
 * Trigger software driven data contact detection (DCD) to enable more
 * robust dedicated charging port (DCP) detection
 */
static inline bool twl5031_bcc_psy_dcd_detect(struct twl5031_bcc_data *bcc)
{
	unsigned long timeout;
	int res;
	bool online = false;

	res = twl5031_bcc_read(TWL5031_USB_CHRG_CTRL1);
	if (res < 0)
		goto out;

	/* DCD init: enable DP source and DM pulldown */
	res |= (TWL5031_USB_SW_CTRL_EN |
		TWL5031_CHGD_IDX_SRC_EN_LOWV |
		TWL5031_CHGD_SERX_EN_LOWV);
	if (twl5031_bcc_write(TWL5031_USB_CHRG_CTRL1, res) < 0)
		goto out;

	/* DCD check: wait for DP line to be low
	 *
	 * twl5031 specification claims for typical value of 2.72 sec.
	 * Make it a bit worse by rounding to 3 sec.
	 */
	timeout = jiffies + msecs_to_jiffies(3000);
	do {
		/* read comparator output */
		res = twl5031_bcc_read(TWL5031_USB_CHRG_CTRL1);
		if (res < 0)
			goto out;

		if (!(res & TWL5031_CHGD_SERX_DP_LOWV)) {
			dev_dbg(bcc->dev, "DCD succeeded\n");
			online = true;
			break;
		}

		cpu_relax();
	} while (!time_after(jiffies, timeout));

	res = twl5031_bcc_read(TWL5031_USB_CHRG_CTRL1);
	if (res < 0)
		goto out;

	/* DCD de-init: disable DP source and DM pulldown */
	res &= ~(TWL5031_USB_SW_CTRL_EN |
		TWL5031_CHGD_IDX_SRC_EN_LOWV |
		TWL5031_CHGD_SERX_EN_LOWV);
	twl5031_bcc_write(TWL5031_USB_CHRG_CTRL1, res);
out:
	bcc->dcd_online = online;

	return online;
}

/** Trigger hardware controlled FSM to detect dedicated USB charger */
static void twl5031_bcc_psy_fsm_detect(struct twl5031_bcc_data *bcc)
{
	unsigned long timeout;
	int res = 0;
	int ctl;

	/* this particular regulator is required for DCD */
	if (!regulator_is_enabled(bcc->usb3v1))
		regulator_enable(bcc->usb3v1);

	/** perform DCD to secure following charger detection */
	if (!twl5031_bcc_psy_dcd_detect(bcc)) {
		dev_notice(bcc->dev, "dcd failed\n");
		/* ignore error and force charger detection */
	}

	/* Let USB dedicated FSM to control current sources */
	res = twl5031_bcc_read(TWL5031_USB_CHRG_CTRL1);
	if (res < 0)
		return;

	res &= ~TWL5031_USB_SW_CTRL_EN;
	res = twl5031_bcc_write(TWL5031_USB_CHRG_CTRL1, res);
	if (res < 0)
		return;

	/* Enable USB FSM (STM mode) */
	ctl = twl5031_bcc_read(TWL5031_BCC_CTRL2);
	if (ctl < 0)
		return;

	ctl |= (TWL5031_CHGMODE_SW | TWL5031_SW_USB_DET_EN);
	if (twl5031_bcc_write(TWL5031_BCC_CTRL2, ctl) < 0)
		return;

	/* Read status */
	timeout = jiffies + msecs_to_jiffies(300);
	do {
		res = twl5031_bcc_read(TWL5031_BCC_STS);
		if (res == 0xaa) /* no VBUS, no charger, BCC in reset state */
			continue;
		if (res < 0)
			continue;

		if (!(res & TWL5031_BATTERY_PRESENCE_STS)) {
			dev_err(bcc->dev, "no battery present\n");
			continue;
		}

		/* USB charger and battery presence bits should be high */
		if (!(res & TWL5031_USB_P_STS)) {
			dev_dbg(bcc->dev, "no usb charger present\n");
			continue;
		}

		res &= TWL5031_USB_DET_STS_MASK;
		if (res == TWL5031_USB_DET_STS_500MA)
			break;

	} while (!time_after(jiffies, timeout) && bcc->dcd_online);

	switch (res) {
	case TWL5031_USB_DET_STS_500MA:
		ctl |= TWL5031_SW_CHRG_DET | TWL5031_SW_USBCHRG_EN;
		bcc->usb_present = true;
		bcc->usb_current = 500;
		bcc->usb.type = twl5031_bcc_usb_charger_type(bcc);
		dev_dbg(bcc->dev, "500mA detected, STS %02x\n", res);
		break;
	case TWL5031_USB_DET_STS_100MA:
		ctl |= TWL5031_SW_USBCHRG_EN;
		bcc->usb.type = POWER_SUPPLY_TYPE_USB;
		bcc->usb_present = true;
		bcc->usb_current = 100;
		dev_dbg(bcc->dev, "100mA detected, STS %02x\n", res);
		break;
	default:
		ctl &= ~TWL5031_SW_USBCHRG_EN;
		dev_dbg(bcc->dev, "No charger detected, STS %02x\n", res);
		break;
	}

	if (regulator_is_enabled(bcc->usb3v1))
		regulator_disable(bcc->usb3v1);

	ctl &= ~TWL5031_SW_USB_DET_EN; /* save result, stop FSM */
	if (twl5031_bcc_write(TWL5031_BCC_CTRL2, ctl) < 0)
		return;
}

static void twl5031_bcc_psy_usb_detect(struct twl5031_bcc_data *bcc)
{
	/* disable all regulators + put the transceiver into nondriving mode */
	otg_set_suspend(bcc->otg, 1);

	twl5031_bcc_psy_fsm_detect(bcc); /* detect charger */

	/* verify dedicated charger */
	if (bcc->dcd_online) {
		switch (bcc->usb.type) {
		case POWER_SUPPLY_TYPE_USB:
			dev_dbg(bcc->dev, "Standard Downstream Port\n");
			otg_notify_event(bcc->otg, USB_EVENT_TRY_CONN, NULL);
			break;
		case POWER_SUPPLY_TYPE_USB_CDP:
			dev_dbg(bcc->dev, "Charging Downstream Port\n");
			otg_notify_event(bcc->otg, USB_EVENT_TRY_CONN, NULL);
			break;
		case POWER_SUPPLY_TYPE_USB_ACA:
			dev_dbg(bcc->dev, "Accessory Charger Adapter\n");
			break;
		case POWER_SUPPLY_TYPE_USB_DCP:	/* Dedicated charger */
			dev_dbg(bcc->dev, "Dedicated Charging Port\n");
			break;
		default:
			dev_dbg(bcc->dev, "UNKOWN port type, trying to connect\n");
			otg_notify_event(bcc->otg, USB_EVENT_TRY_CONN, NULL);
		}
	}
}

static void twl5031_bcc_event_work(struct work_struct *work)
{
	struct twl5031_bcc_data	*bcc;
	unsigned long event;
	int mA;

	bcc = container_of(work, struct twl5031_bcc_data, work);

	mutex_lock(&bcc->mutex);

	event = bcc->event;
	mA = bcc->mA;

	switch (event) {
	case USB_EVENT_NONE:
		bcc->usb_present = 0;
		bcc->usb_current = 0;
		bcc->dcd_online = false;
		bcc->usb.type = POWER_SUPPLY_TYPE_USB;
		break;
	case USB_EVENT_VBUS:
		twl5031_bcc_psy_usb_detect(bcc);
		break;
	case USB_EVENT_ENUMERATED:
		if (mA <= 0)
			return;

		bcc->usb_present = 1;

		switch (bcc->usb.type) {
		case POWER_SUPPLY_TYPE_USB_CDP:
			/*
			 * Theoretical maximum, this should
			 * indicate BME to increase input
			 * current in small steps until
			 * real maximum is achieved
			 */
			bcc->usb_current = 900;
			break;
		case POWER_SUPPLY_TYPE_USB: /* FALLTHROUGH */
		default:
			bcc->usb_current = mA;
			break;
		}

		break;
	case USB_EVENT_SUSPENDED:
		bcc->usb_present = 1;

		switch (bcc->usb.type) {
		case POWER_SUPPLY_TYPE_USB_CDP:
			bcc->usb_current = 500;
			break;
		case POWER_SUPPLY_TYPE_USB: /* FALLTHROUGH */
		default:
			bcc->usb_current = 2;
			break;
		}
		break;
	case USB_EVENT_RESUMED:
		bcc->usb_present = 1;

		switch (bcc->usb.type) {
		case POWER_SUPPLY_TYPE_USB_CDP:
			bcc->usb_current = 500;
			break;
		case POWER_SUPPLY_TYPE_USB: /* FALLTHROUGH */
		default:
			bcc->usb_current = mA;
			break;
		}
		break;
	default:
		dev_dbg(bcc->dev, "unsupported event %lu\n", event);
	}

	power_supply_changed(&bcc->usb);
	mutex_unlock(&bcc->mutex);
}

/* atomic notifier support */
static int bcc_notifier_call(struct notifier_block *nb,
		unsigned long event, void *power)
{
	struct twl5031_bcc_data	*bcc;

	bcc = container_of(nb, struct twl5031_bcc_data, nb);
	bcc->event = event;

	dev_dbg(bcc->dev, "received event=%lu\n", event);

	if (power)
		bcc->mA = *((int *) power);

	queue_work(bcc->wq, &bcc->work);

	return NOTIFY_OK;
}

/*
 * Updates charger presense information immediately. This function
 * should not be called on the spot when status change interrupt
 * occurs, as the status bit in hardware will be updated after
 * a delay.
 */
static void twl5031_bcc_psy_vac_detect(struct twl5031_bcc_data *bcc)
{
	int val;

	val = twl5031_bcc_read(TWL5031_BCC_STS);
	if (val < 0 || val == 0xaa) {
		/* reading failed or charger is not present */
		twl5031_bcc_vac_update_present(bcc, 0);
	} else {
		val = (val & TWL5031_VAC_P_STS) ? 1 : 0;
		twl5031_bcc_vac_update_present(bcc, val);
	}
}

static irqreturn_t twl5031_bcc_pm_irq_thread(int irq, void *_bcc)
{
	struct twl5031_bcc_data *bcc = _bcc;

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((HZ / 10) + 1);
	twl5031_bcc_psy_vac_detect(bcc);

	return IRQ_HANDLED;
}

static int twl5031_bcc_usb_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct twl5031_bcc_data *bcc;
	int ret = 0;

	bcc = container_of(psy, struct twl5031_bcc_data, usb);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bcc->usb_present;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bcc->usb_current;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int twl5031_bcc_vac_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct twl5031_bcc_data *bcc;
	int ret = 0;

	bcc = container_of(psy, struct twl5031_bcc_data, vac);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bcc->vac_present;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bcc->vac_current;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static enum power_supply_property power_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static int __devinit twl5031_bcc_probe(struct platform_device *pdev)
{
	struct twl5031_bcc_data *bcc;
	int ret = -ENODEV;

	bcc = kzalloc(sizeof *bcc, GFP_KERNEL);
	if (!bcc)
		return -ENOMEM;

	bcc->wq = create_singlethread_workqueue("bcc");
	if (!bcc->wq)
		return -ENOMEM;

	INIT_WORK(&bcc->work, twl5031_bcc_event_work);
	mutex_init(&bcc->mutex);

	bcc->dev = &pdev->dev;
	platform_set_drvdata(pdev, bcc);

	/* register usb notifier */
	bcc->otg = otg_get_transceiver();
	if (!bcc->otg)
		goto err;

	bcc->usb3v1 = regulator_get(bcc->dev, "usb3v1");
	if (IS_ERR(bcc->usb3v1))
		goto get_regulator_failed;

	/* initialize power supplies */
	bcc->usb_present = 0;
	bcc->usb_current = 0;
	bcc->usb.name = "usb";
	bcc->usb.type = POWER_SUPPLY_TYPE_USB;
	bcc->usb.properties = power_props;
	bcc->usb.num_properties = ARRAY_SIZE(power_props);
	bcc->usb.get_property = twl5031_bcc_usb_get_property;

	ret = power_supply_register(bcc->dev, &bcc->usb);
	if (ret)
		goto psy_usb_register_failed;

	bcc->vac_present = 0;
	bcc->vac_current = 0;
	bcc->vac.name = "vac";
	bcc->vac.type = POWER_SUPPLY_TYPE_MAINS;
	bcc->vac.properties = power_props;
	bcc->vac.num_properties = ARRAY_SIZE(power_props);
	bcc->vac.get_property = twl5031_bcc_vac_get_property;

	ret = power_supply_register(bcc->dev, &bcc->vac);
	if (ret)
		goto psy_vac_register_failed;

	twl5031_bcc_psy_vac_detect(bcc);	/* probe vac charger */

	/* request charger detection interrupt */
	ret = request_threaded_irq(platform_get_irq(pdev, 0), NULL,
				   twl5031_bcc_pm_irq_thread,
				   0, "twl5031_chgdet", bcc);
	if (ret) {
		dev_dbg(bcc->dev, "could not request irq (chg det)\n");
		goto request_irq_failed;
	}

	bcc->nb.priority = OTG_NB_PRIORITY_CHARGER;
	bcc->nb.notifier_call = bcc_notifier_call;
	ret = otg_register_notifier(bcc->otg, &bcc->nb);
	if (ret)
		goto register_notifier_failed;

	ret = otg_get_last_event(bcc->otg);
	if (ret < 0)
		goto get_last_event_failed;

	return 0;

get_last_event_failed:
	otg_unregister_notifier(bcc->otg, &bcc->nb);
register_notifier_failed:
	free_irq(platform_get_irq(pdev, 0), bcc);
request_irq_failed:
	power_supply_unregister(&bcc->vac);
psy_vac_register_failed:
	power_supply_unregister(&bcc->usb);
psy_usb_register_failed:
	regulator_put(bcc->usb3v1);
get_regulator_failed:
	/* FIXME: theoretically we should put transceiver here but
	 *	  in practice it does not work. The reason for this
	 *	  is reference counter that will be set to 0 if we
	 *	  are the only users of otg by this time. This will
	 *	  in turn result into unwanted transceiver destruction.
	 *
	 *otg_put_transceiver(bcc->otg);
	 */
err:
	destroy_workqueue(bcc->wq);
	kfree(bcc);
	return ret;
}

static int __devexit twl5031_bcc_remove(struct platform_device *pdev)
{
	struct twl5031_bcc_data *bcc = platform_get_drvdata(pdev);

	free_irq(platform_get_irq(pdev, 0), bcc);
	otg_unregister_notifier(bcc->otg, &bcc->nb);
	regulator_put(bcc->usb3v1);
	otg_put_transceiver(bcc->otg);
	power_supply_unregister(&bcc->usb);
	power_supply_unregister(&bcc->vac);
	destroy_workqueue(bcc->wq);

	kfree(bcc);

	return 0;
}

static struct platform_driver twl5031_bcc_driver = {
	.probe		= twl5031_bcc_probe,
	.remove		= __devexit_p(twl5031_bcc_remove),
	.driver		= {
		.name	= "twl5031_bcc",
		.owner	= THIS_MODULE,
	},
};

static int __init twl5031_bcc_init(void)
{
	return platform_driver_register(&twl5031_bcc_driver);
}
module_init(twl5031_bcc_init);

static void __exit twl5031_bcc_exit(void)
{
	platform_driver_unregister(&twl5031_bcc_driver);
}
module_exit(twl5031_bcc_exit);

MODULE_ALIAS("platform:twl5031-bcc");
MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("twl5031 BCC driver");
MODULE_LICENSE("GPL");
