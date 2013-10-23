/*
 * twl4030_keypad.c - driver for 8x8 keypad controller in twl4030 chips
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2008 Nokia Corporation
 *
 * Code re-written for 2430SDP by:
 * Syed Mohammed Khasim <x0khasim@ti.com>
 *
 * Initial Code:
 * Manjunatha G K <manjugk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl4030.h>
#include <linux/mutex.h>
#include <linux/pm_qos_params.h>


/*
 * The TWL4030 family chips include a keypad controller that supports
 * up to an 8x8 switch matrix.  The controller can issue system wakeup
 * events, since it uses only the always-on 32KiHz oscillator, and has
 * an internal state machine that decodes pressed keys, including
 * multi-key combinations.
 *
 * This driver lets boards define what keycodes they wish to report for
 * which scancodes, as part of the "struct twl4030_keypad_data" used in
 * the probe() routine.
 *
 * See the TPS65950 documentation; that's the general availability
 * version of the TWL5030 second generation part.
 */
#define TWL4030_MAX_ROWS	8	/* TWL4030 hard limit */
#define TWL4030_MAX_COLS	8
/*
 * Note that we add space for an extra column so that we can handle
 * row lines connected to the gnd (see twl4030_col_xlate()).
 */
#define TWL4030_ROW_SHIFT	4
#define TWL4030_KEYMAP_SIZE	(TWL4030_MAX_ROWS << TWL4030_ROW_SHIFT)

struct twl4030_keypad {
	unsigned short	keymap[TWL4030_KEYMAP_SIZE];
	u16		kp_state[TWL4030_MAX_ROWS];
	unsigned	n_rows;
	unsigned	n_cols;
	unsigned	irq;
	unsigned	user_disabled:1;
	unsigned	disable_depth;
	u8		isr;
	int		curr_debounce;

	struct device *dbg_dev;
	struct input_dev *input;
	struct mutex	mutex;
};

/*----------------------------------------------------------------------*/

/* arbitrary prescaler value 0..7 */
#define PTV_PRESCALER			4

/* Register Offsets */
#define KEYP_CTRL			0x00
#define KEYP_DEB			0x01
#define KEYP_LONG_KEY			0x02
#define KEYP_LK_PTV			0x03
#define KEYP_TIMEOUT_L			0x04
#define KEYP_TIMEOUT_H			0x05
#define KEYP_KBC			0x06
#define KEYP_KBR			0x07
#define KEYP_SMS			0x08
#define KEYP_FULL_CODE_7_0		0x09	/* row 0 column status */
#define KEYP_FULL_CODE_15_8		0x0a	/* ... row 1 ... */
#define KEYP_FULL_CODE_23_16		0x0b
#define KEYP_FULL_CODE_31_24		0x0c
#define KEYP_FULL_CODE_39_32		0x0d
#define KEYP_FULL_CODE_47_40		0x0e
#define KEYP_FULL_CODE_55_48		0x0f
#define KEYP_FULL_CODE_63_56		0x10
#define KEYP_ISR1			0x11
#define KEYP_IMR1			0x12
#define KEYP_ISR2			0x13
#define KEYP_IMR2			0x14
#define KEYP_SIR			0x15
#define KEYP_EDR			0x16	/* edge triggers */
#define KEYP_SIH_CTRL			0x17

/* KEYP_CTRL_REG Fields */
#define KEYP_CTRL_SOFT_NRST		BIT(0)
#define KEYP_CTRL_SOFTMODEN		BIT(1)
#define KEYP_CTRL_LK_EN			BIT(2)
#define KEYP_CTRL_TOE_EN		BIT(3)
#define KEYP_CTRL_TOLE_EN		BIT(4)
#define KEYP_CTRL_RP_EN			BIT(5)
#define KEYP_CTRL_KBD_ON		BIT(6)

/* KEYP_DEB, KEYP_LONG_KEY, KEYP_TIMEOUT_x*/
#define KEYP_PERIOD_US(t, prescale)	((t) / (31 << (prescale + 1)) - 1)

/* KEYP_LK_PTV_REG Fields */
#define KEYP_LK_PTV_PTV_SHIFT		5

/* KEYP_{IMR,ISR,SIR} Fields */
#define KEYP_IMR1_MIS			BIT(3)
#define KEYP_IMR1_TO			BIT(2)
#define KEYP_IMR1_LK			BIT(1)
#define KEYP_IMR1_KP			BIT(0)

/* KEYP_EDR Fields */
#define KEYP_EDR_KP_FALLING		0x01
#define KEYP_EDR_KP_RISING		0x02
#define KEYP_EDR_KP_BOTH		0x03
#define KEYP_EDR_LK_FALLING		0x04
#define KEYP_EDR_LK_RISING		0x08
#define KEYP_EDR_TO_FALLING		0x10
#define KEYP_EDR_TO_RISING		0x20
#define KEYP_EDR_MIS_FALLING		0x40
#define KEYP_EDR_MIS_RISING		0x80

/* microseconds */
#define KEYP_DEF_DEBOUNCE		20000
#define KEYP_LONG_DEBOUNCE		40000
#define KEYP_DEF_TIMEOUT                1000000 /* 1 second */

/*----------------------------------------------------------------------*/

static int twl4030_kpread(struct twl4030_keypad *kp,
		u8 *data, u32 reg, u8 num_bytes)
{
	int ret = twl4030_i2c_read(TWL4030_MODULE_KEYPAD, data, reg, num_bytes);

	if (ret < 0)
		dev_warn(kp->dbg_dev,
			"Couldn't read TWL4030: %X - ret %d[%x]\n",
			 reg, ret, ret);

	return ret;
}

static int twl4030_kpwrite_u8(struct twl4030_keypad *kp, u8 data, u32 reg)
{
	int ret = twl4030_i2c_write_u8(TWL4030_MODULE_KEYPAD, data, reg);

	if (ret < 0)
		dev_warn(kp->dbg_dev,
			"Could not write TWL4030: %X - ret %d[%x]\n",
			 reg, ret, ret);

	return ret;
}

static int twl4030_kp_enable_interrupts(struct twl4030_keypad *kp)
{
	u8 reg;
	int ret;
	/* Enable KP and TO interrupts now. */
	reg = (u8)~(KEYP_IMR1_KP | KEYP_IMR1_TO);
	ret = twl4030_kpwrite_u8(kp, reg, KEYP_IMR1);
	return ret;
}

static void twl4030_kp_disable_interrupts(struct twl4030_keypad *kp)
{
	u8 reg;
	/* mask all events - we don't care about the result */
	reg = KEYP_IMR1_MIS | KEYP_IMR1_TO | KEYP_IMR1_LK | KEYP_IMR1_KP;
	(void)twl4030_kpwrite_u8(kp, reg, KEYP_IMR1);
}

static inline u16 twl4030_col_xlate(struct twl4030_keypad *kp, u8 col)
{
	/* If all bits in a row are active for all coloumns then
	 * we have that row line connected to gnd. Mark this
	 * key on as if it was on matrix position n_cols (ie
	 * one higher than the size of the matrix).
	 */
	if (col == 0xFF)
		return 1 << kp->n_cols;
	else
		return col & ((1 << kp->n_cols) - 1);
}

static int twl4030_read_kp_matrix_state(struct twl4030_keypad *kp, u16 *state)
{
	u8 new_state[TWL4030_MAX_ROWS];
	int row;
	int ret = twl4030_kpread(kp, new_state,
				 KEYP_FULL_CODE_7_0, kp->n_rows);
	if (ret >= 0)
		for (row = 0; row < kp->n_rows; row++)
			state[row] = twl4030_col_xlate(kp, new_state[row]);

	return ret;
}

static bool twl4030_is_in_ghost_state(struct twl4030_keypad *kp, u16 *key_state)
{
	int i;
	u16 check = 0;

	for (i = 0; i < kp->n_rows; i++) {
		u16 col = key_state[i];

		if ((col & check) && hweight16(col) > 1)
			return true;

		check |= col;
	}

	return false;
}

static void twl4030_kp_report_changes(struct twl4030_keypad *kp, u16 *new_state)
{
	struct input_dev *input = kp->input;
	int col, row;

	/* check for changes and print those */
	for (row = 0; row < kp->n_rows; row++) {
		int changed = new_state[row] ^ kp->kp_state[row];

		if (!changed)
			continue;

		/* Extra column handles "all gnd" rows */
		for (col = 0; col < kp->n_cols + 1; col++) {
			int code;

			if (!(changed & (1 << col)))
				continue;

			dev_dbg(kp->dbg_dev, "key [%d:%d] %s\n", row, col,
				(new_state[row] & (1 << col)) ?
				"press" : "release");

			code = MATRIX_SCAN_CODE(row, col, TWL4030_ROW_SHIFT);
			input_event(input, EV_MSC, MSC_SCAN, code);
			input_report_key(input, kp->keymap[code],
					 new_state[row] & (1 << col));
		}
		kp->kp_state[row] = new_state[row];
	}
	input_sync(input);
}

static void twl4030_update_debounce(struct twl4030_keypad *kp, int time)
{
	int i;

	if (time != kp->curr_debounce) {
		/* Update both debounce time and pm qos requirement */
		kp->curr_debounce = time;
		i = KEYP_PERIOD_US(time, PTV_PRESCALER);
		twl4030_kpwrite_u8(kp, i, KEYP_DEB);
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY, "twlkey",
					time);
	}
}

static void twl4030_invalidate_debounce(struct twl4030_keypad *kp)
{
	/* Set invalid debounce time and release CPU latency requirement */
	kp->curr_debounce = -1;
	pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY, "twlkey",
				PM_QOS_DEFAULT_VALUE);
}

static inline int twl4030_kp_disabled(struct twl4030_keypad *kp)
{
	return kp->disable_depth != 0;
}

static void twl4030_kp_enable(struct twl4030_keypad *kp)
{
	BUG_ON(!twl4030_kp_disabled(kp));
	if (--kp->disable_depth == 0) {
		twl4030_update_debounce(kp, KEYP_LONG_DEBOUNCE);
		enable_irq(kp->irq);
		twl4030_kp_enable_interrupts(kp);
	}
}

static int twl4030_kp_scan(struct twl4030_keypad *kp, u16 *new_state)
{
	/* check for any changes */
	int ret = twl4030_read_kp_matrix_state(kp, new_state);
	if (ret < 0)    /* panic ... */
		return ret;

	return twl4030_is_in_ghost_state(kp, new_state);
}

static void twl4030_kp_disable(struct twl4030_keypad *kp)
{
	u16 new_state[TWL4030_MAX_ROWS];

	if (kp->disable_depth++ == 0) {
		twl4030_invalidate_debounce(kp);
		memset(new_state, 0, sizeof(new_state));
		twl4030_kp_report_changes(kp, new_state);
		twl4030_kp_disable_interrupts(kp);
		disable_irq(kp->irq);
	}
}

static ssize_t twl4030_kp_disable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct twl4030_keypad *kp = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", twl4030_kp_disabled(kp));
}

static ssize_t twl4030_kp_disable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct twl4030_keypad *kp = dev_get_drvdata(dev);
	long i = 0;
	int ret;

	ret = strict_strtoul(buf, 10, &i);
	if (ret)
		return -EINVAL;
	i = !!i;

	mutex_lock(&kp->mutex);
	if (i == kp->user_disabled) {
		mutex_unlock(&kp->mutex);
		return count;
	}
	kp->user_disabled = i;

	if (i)
		twl4030_kp_disable(kp);
	else
		twl4030_kp_enable(kp);

	mutex_unlock(&kp->mutex);
	return count;
}

/*
 * Keypad interrupt handler
 */
static irqreturn_t do_kp_irq(int irq, void *_kp)
{
	struct twl4030_keypad *kp = _kp;
	int ret;
	u8 reg;

#ifdef CONFIG_LOCKDEP
	/*
	 * This is not a real hard irq context and lockdep mistakenly left
	 * hardirq disabled.
	 */
	local_irq_enable();
#endif

	/* Read & Clear TWL4030 pending interrupt */
	ret = twl4030_kpread(kp, &reg, KEYP_ISR1, 1);
	if (ret < 0)
		reg = 0;

	mutex_lock(&kp->mutex);
	if (twl4030_kp_disabled(kp)) {
		kp->isr = 0;
		ret = IRQ_HANDLED;
	} else {
		kp->isr |= reg;
		ret = IRQ_WAKE_THREAD;
	}
	mutex_unlock(&kp->mutex);

	return ret;
}
static irqreturn_t do_kp_irq_thread(int irq, void *_kp)
{
	struct twl4030_keypad *kp = _kp;
	u16 new_state[TWL4030_MAX_ROWS];
	u8 reg;

	mutex_lock(&kp->mutex);
	reg = kp->isr;
	kp->isr = 0;

	/* Release all keys when the KEYP has gone to idle state */
	if (reg & KEYP_IMR1_KP) {
		twl4030_update_debounce(kp, KEYP_DEF_DEBOUNCE);
		twl4030_kp_scan(kp, new_state);
	} else {
		memset(new_state, 0, sizeof(new_state));
		twl4030_update_debounce(kp, KEYP_LONG_DEBOUNCE);
	}

	twl4030_kp_report_changes(kp, new_state);

	mutex_unlock(&kp->mutex);

	return IRQ_HANDLED;
}

static int __devinit twl4030_kp_program(struct twl4030_keypad *kp)
{
	u16 new_state[TWL4030_MAX_ROWS];
	u8 reg;
	int i;

	/* Enable controller, with hardware decoding but not autorepeat */
	reg = KEYP_CTRL_SOFT_NRST | KEYP_CTRL_SOFTMODEN
		| KEYP_CTRL_TOE_EN | KEYP_CTRL_KBD_ON;
	if (twl4030_kpwrite_u8(kp, reg, KEYP_CTRL) < 0)
		return -EIO;

	/* NOTE:  we could use sih_setup() here to package keypad
	 * event sources as four different IRQs ... but we don't.
	 */

	/* Enable TO rising and KP rising and falling edge detection */
	reg = KEYP_EDR_KP_BOTH | KEYP_EDR_TO_RISING;
	if (twl4030_kpwrite_u8(kp, reg, KEYP_EDR) < 0)
		return -EIO;

	/* Set PTV prescaler Field */
	reg = (PTV_PRESCALER << KEYP_LK_PTV_PTV_SHIFT);
	if (twl4030_kpwrite_u8(kp, reg, KEYP_LK_PTV) < 0)
		return -EIO;

	/* Set key debounce time to LONG period */
	kp->curr_debounce = KEYP_LONG_DEBOUNCE;
	i = KEYP_PERIOD_US(KEYP_LONG_DEBOUNCE, PTV_PRESCALER);
	if (twl4030_kpwrite_u8(kp, i, KEYP_DEB) < 0)
		return -EIO;

	/* Set timeout period to 100 ms */
	i = KEYP_PERIOD_US(KEYP_DEF_TIMEOUT, PTV_PRESCALER);
	if (twl4030_kpwrite_u8(kp, (i & 0xFF), KEYP_TIMEOUT_L) < 0)
		return -EIO;

	if (twl4030_kpwrite_u8(kp, (i >> 8), KEYP_TIMEOUT_H) < 0)
		return -EIO;

	/*
	 * Enable Clear-on-Read; disable remembering events that fire
	 * after the IRQ but before our handler acks (reads) them,
	 */
	reg = TWL4030_SIH_CTRL_COR_MASK | TWL4030_SIH_CTRL_PENDDIS_MASK;
	if (twl4030_kpwrite_u8(kp, reg, KEYP_SIH_CTRL) < 0)
		return -EIO;

	/* initialize key state; irqs update it from here on */
	memset(kp->kp_state, 0, sizeof(kp->kp_state));
	twl4030_kp_scan(kp, new_state);
	twl4030_kp_report_changes(kp, new_state);

	return 0;
}

static DEVICE_ATTR(disable_kp, 0664, twl4030_kp_disable_show,
		  twl4030_kp_disable_store);

/*
 * Registers keypad device with input subsystem
 * and configures TWL4030 keypad registers
 */
static int __devinit twl4030_kp_probe(struct platform_device *pdev)
{
	struct twl4030_keypad_data *pdata = pdev->dev.platform_data;
	const struct matrix_keymap_data *keymap_data = pdata->keymap_data;
	struct twl4030_keypad *kp;
	struct input_dev *input;
	int error;

	if (!pdata || !pdata->rows || !pdata->cols ||
	    pdata->rows > TWL4030_MAX_ROWS || pdata->cols > TWL4030_MAX_COLS) {
		dev_err(&pdev->dev, "Invalid platform_data\n");
		return -EINVAL;
	}

	kp = kzalloc(sizeof(*kp), GFP_KERNEL);
	if (!kp)
		return -ENOMEM;

	input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err1;
	}

	mutex_init(&kp->mutex);

	/* Get the debug Device */
	kp->dbg_dev = &pdev->dev;
	kp->input = input;

	kp->n_rows = pdata->rows;
	kp->n_cols = pdata->cols;
	kp->irq = platform_get_irq(pdev, 0);

	/* setup input device */
	__set_bit(EV_KEY, input->evbit);

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	input_set_capability(input, EV_MSC, MSC_SCAN);

	input->name		= "TWL4030 Keypad";
	input->phys		= "twl4030_keypad/input0";
	input->dev.parent	= &pdev->dev;

	input->id.bustype	= BUS_HOST;
	input->id.vendor	= 0x0001;
	input->id.product	= 0x0001;
	input->id.version	= 0x0003;

	input->keycode		= kp->keymap;
	input->keycodesize	= sizeof(kp->keymap[0]);
	input->keycodemax	= ARRAY_SIZE(kp->keymap);

	matrix_keypad_build_keymap(keymap_data, TWL4030_ROW_SHIFT,
				   input->keycode, input->keybit);

	error = input_register_device(input);
	if (error) {
		dev_err(kp->dbg_dev,
			"Unable to register twl4030 keypad device\n");
		goto err2;
	}

	error = twl4030_kp_program(kp);
	if (error)
		goto err3;

	/*
	 * This ISR will always execute in kernel thread context because of
	 * the need to access the TWL4030 over the I2C bus.
	 *
	 * NOTE:  we assume this host is wired to TWL4040 INT1, not INT2 ...
	 */
	error = request_threaded_irq(kp->irq, do_kp_irq, do_kp_irq_thread,
				0, pdev->name, kp);
	if (error) {
		dev_info(kp->dbg_dev, "request_irq failed for irq no=%d\n",
			kp->irq);
		goto err3;
	}

	/* Enable KP and TO interrupts now. */
	error = twl4030_kp_enable_interrupts(kp);
	if (error < 0)
		goto err4;

	error = pm_qos_add_requirement(PM_QOS_CPU_DMA_LATENCY, "twlkey",
				KEYP_LONG_DEBOUNCE);
	if (error < 0)
		goto err5;

	error = device_create_file(&pdev->dev, &dev_attr_disable_kp);
	if (error < 0)
		goto err6;

	platform_set_drvdata(pdev, kp);
	return 0;

err6:
	pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY, "twlkey");
err5:
	/* mask all events - we don't care about the result */
	twl4030_kp_disable_interrupts(kp);
err4:
	free_irq(kp->irq, NULL);
err3:
	input_unregister_device(input);
	input = NULL;
err2:
	input_free_device(input);
err1:
	kfree(kp);
	return error;
}

static int __devexit twl4030_kp_remove(struct platform_device *pdev)
{
	struct twl4030_keypad *kp = platform_get_drvdata(pdev);

	twl4030_kp_disable_interrupts(kp);
	device_remove_file(&pdev->dev, &dev_attr_disable_kp);
	pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY, "twlkey");
	free_irq(kp->irq, kp);
	input_unregister_device(kp->input);
	platform_set_drvdata(pdev, NULL);
	kfree(kp);

	return 0;
}

#ifdef CONFIG_PM
static int twl4030_kp_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct twl4030_keypad *kp = dev_get_drvdata(&pdev->dev);
	mutex_lock(&kp->mutex);
	twl4030_kp_disable(kp);
	mutex_unlock(&kp->mutex);
	return 0;
}

static int twl4030_kp_resume(struct platform_device *pdev)
{
	struct twl4030_keypad *kp = dev_get_drvdata(&pdev->dev);
	mutex_lock(&kp->mutex);
	twl4030_kp_enable(kp);
	mutex_unlock(&kp->mutex);
	return 0;
}

static void twl4030_kp_shutdown(struct platform_device *pdev)
{
	struct twl4030_keypad *kp = dev_get_drvdata(&pdev->dev);
	/* Disable controller */
	twl4030_kpwrite_u8(kp, 0, KEYP_CTRL);
}
#else

#define twl4030_kp_suspend	NULL
#define twl4030_kp_resume	NULL
#define twl4030_kp_shutdown	NULL

#endif /* CONFIG_PM */

/*
 * NOTE: twl4030 are multi-function devices connected via I2C.
 * So this device is a child of an I2C parent, thus it needs to
 * support unplug/replug (which most platform devices don't).
 */

static struct platform_driver twl4030_kp_driver = {
	.probe		= twl4030_kp_probe,
	.remove		= __devexit_p(twl4030_kp_remove),
	.suspend	= twl4030_kp_suspend,
	.resume		= twl4030_kp_resume,
	.shutdown	= twl4030_kp_shutdown,
	.driver		= {
		.name	= "twl4030_keypad",
		.owner	= THIS_MODULE,
	},
};

static int __init twl4030_kp_init(void)
{
	return platform_driver_register(&twl4030_kp_driver);
}
module_init(twl4030_kp_init);

static void __exit twl4030_kp_exit(void)
{
	platform_driver_unregister(&twl4030_kp_driver);
}
module_exit(twl4030_kp_exit);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("TWL4030 Keypad Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030_keypad");

