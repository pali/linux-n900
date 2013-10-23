/*
 * leds-twl4030-vibra.c - TWL4030 Vibrator driver
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Written by Henrik Saari <henrik.saari@nokia.com>
 * Updates by Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c/twl4030.h>
#include <linux/leds.h>

/* MODULE ID1  */
#define CODEC_MODE		0x01
#	define CODECPDZ		(1 << 1)
#define VIBRA_CTL		0x45
#	define VIBRA_EN		(1 << 0)
#	define VIBRA_DIR	(1 << 1)
#define VIBRA_SET		0x46 /* PWM register */
#	define VIB_CFG		(1 << 3)
#	define VIB_PWM		(1 << 2)
#define APLL_CTL		0x3a
#	define APLL_EN		(1 << 4)
#	define APLL_FREQ_26	0x06

/* MODULE ID2 */
#define LEDEN                   0xee

/* MODULE ID3 */
#define VIBRA_CFG		0x60

#define MAX_SEQ_LEN		5

struct pulse_info {
	unsigned int		dir:1;
	unsigned int		pwm:31;
	unsigned long		duration;
};

struct vibra_info {
	struct mutex		lock;
	struct device		*dev;

	struct workqueue_struct *workqueue;
	struct delayed_work	work;
	struct work_struct	led_work;

	struct led_classdev	vibra;

	unsigned long		duration;
	int			enabled;
	int			speed;

	struct pulse_info	seq[MAX_SEQ_LEN];
	unsigned int		iseq;
	unsigned int		nseq;
};

/* Powers H-Bridge and enables audio clk */
static void vibra_enable(struct vibra_info *info)
{
	u8 reg;

	/* Disable LEDA & LEDB, cannot be used with vibra */
	twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, LEDEN);
	reg &= ~0x03;
	twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, LEDEN, reg);

	/* Turn codec on */
	twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			    &reg, CODEC_MODE);
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			     (reg |= CODECPDZ), CODEC_MODE);

	/* turn H-Bridge on */
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			     VIBRA_EN, VIBRA_CTL);

	/* set audio clock on  */
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			     (APLL_EN | APLL_FREQ_26), APLL_CTL);

	info->enabled = 1;
}

static void vibra_disable(struct vibra_info *info)
{
	u8 reg;

	/* Power down H-Bridge */
	twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			    &reg, VIBRA_CTL);
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			     (reg & ~VIBRA_EN), VIBRA_CTL);

	/* Turn codec OFF */
	twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			    &reg, CODEC_MODE);
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			     reg & ~CODECPDZ, CODEC_MODE);

	/* disable audio clk */
	twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			    &reg, APLL_CTL);
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			     (reg & ~APLL_EN), APLL_CTL);
	info->enabled = 0;
}

static int vibra_seq(struct vibra_info *info, const struct pulse_info *seq,
		unsigned int n)
{
	if (n == 0 || n > ARRAY_SIZE(info->seq))
		return -EINVAL;

	/* stop previous sequence, if any */
	cancel_delayed_work_sync(&info->work);

	mutex_lock(&info->lock);

	info->iseq = 0;
	info->nseq = n;
	memcpy(info->seq, seq, n * sizeof(*seq));

	queue_delayed_work(info->workqueue, &info->work, 0);

	mutex_unlock(&info->lock);

	return 0;
}

static void vibra_pwm(struct vibra_info *info, int dir, int pwm)
{
	struct pulse_info seq[2] = {
		{ .dir = dir, .pwm = pwm, .duration = info->duration },
	};

	vibra_seq(info, seq, ARRAY_SIZE(seq));
}

static void vibra_next_pulse(struct vibra_info *info)
{
	unsigned int pwm;
	unsigned int dir;
	unsigned long duration;

	mutex_lock(&info->lock);

	pwm = info->seq[info->iseq].pwm;
	dir = info->seq[info->iseq].dir;
	duration = info->seq[info->iseq].duration;

	if (pwm) {
		u8 reg;

		if (!info->enabled)
			vibra_enable(info);

		/* set vibra rotation direction */
		twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
				&reg, VIBRA_CTL);
		reg = (dir) ? (reg | VIBRA_DIR) : (reg & ~VIBRA_DIR);
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
				reg, VIBRA_CTL);

		/* set PWM, 1 = max, 255 = min */
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
				256 - pwm, VIBRA_SET);
	} else {
		vibra_disable(info);
	}

	info->iseq++;
	if (info->iseq < info->nseq && duration)
		queue_delayed_work(info->workqueue, &info->work,
				msecs_to_jiffies(duration));

	mutex_unlock(&info->lock);
}

static void vibra_work(struct work_struct *work)
{
	struct vibra_info *info = container_of(work,
			struct vibra_info, work.work);

	vibra_next_pulse(info);
}

static void vibra_led_work(struct work_struct *work)
{
	struct vibra_info *info = container_of(work,
			struct vibra_info, led_work);

	vibra_pwm(info, 1, info->speed);
}

static void vibra_led_set(struct led_classdev *led,
			  enum led_brightness value)
{
	struct vibra_info *info = container_of(led, struct vibra_info, vibra);

	info->speed = value;

	schedule_work(&info->led_work);
}

/*******************************************************************************
 * SYSFS                                                                       *
 ******************************************************************************/

static ssize_t vibra_set_seq(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	const char *p;
	unsigned int i;
	struct vibra_info *info = dev_get_drvdata(dev);
	struct pulse_info seq[ARRAY_SIZE(info->seq)];

	for (p = buf, i = 0;
	     *p != '\0' && *p != '\n' && i < ARRAY_SIZE(seq); i++) {
		long val;
		char *endp;

		/* speed and direction */
		val = simple_strtol(p, &endp, 0);
		if (p == endp || *endp != ' ')
			return -EINVAL;
		for (p = endp; *p == ' '; p++)
			;

		seq[i].dir = val < 0 ? 1 : 0;
		seq[i].pwm = min(abs(val), 255);

		/* duration */
		val = simple_strtol(p, &endp, 0);
		if (p == endp ||
			(*endp != ' ' && *endp != '\0' && *endp != '\n'))
			return -EINVAL;
		for (p = endp; *p == ' '; p++)
			;

		if (val < 0)
			return -EINVAL;

		seq[i].duration = val;
	}

	/* no room for end of sequence */
	if (i == ARRAY_SIZE(seq))
		return -EINVAL;

	/* end of sequence */
	seq[i].pwm = 0;
	seq[i].dir = 0;
	seq[i++].duration = 0;

	vibra_seq(info, seq, i);

	return len;
}

static ssize_t vibra_set_pwm(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	long pwm;
	int  ret;
	int dir = 0;

	struct vibra_info *info = dev_get_drvdata(dev);

	ret = strict_strtol(buf, 0, &pwm);
	if (ret < 0)
		return -EINVAL;
	if (pwm < 0)
		dir = 1;
	pwm = abs(pwm);
	if (pwm > 255)
		pwm = 255;
	vibra_pwm(info, dir, pwm);

	return len;
}

static ssize_t vibra_show_pwm(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct vibra_info *info = dev_get_drvdata(dev);

	u8 reg;
	mutex_lock(&info->lock);
	twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &reg, VIBRA_SET);
	mutex_unlock(&info->lock);

	return sprintf(buf, "%d\n", 256 - reg);
}

static ssize_t vibra_set_duration(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	unsigned long duration;
	int ret;
	struct vibra_info *info = dev_get_drvdata(dev);

	ret = strict_strtoul(buf, 0, &duration);
	if (ret < 0)
		return -EINVAL;

	mutex_lock(&info->lock);
	info->duration = duration;
	mutex_unlock(&info->lock);

	return len;
}

static ssize_t vibra_show_duration(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct vibra_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%ld\n", info->duration);
}

static struct device_attribute vibra_attrs[] = {
	__ATTR(speed, S_IRUGO | S_IWUSR,
	       vibra_show_pwm, vibra_set_pwm),
	__ATTR(duration, S_IRUGO | S_IWUSR,
	       vibra_show_duration, vibra_set_duration),
	__ATTR(pulse, S_IWUSR,
	       NULL, vibra_set_seq),
};

static int vibra_register_sysfs(struct vibra_info *info)
{
	int r, i;

	for (i = 0; i < ARRAY_SIZE(vibra_attrs); i++) {
		r = device_create_file(info->dev, &vibra_attrs[i]);
		if (r)
			goto fail;
	}
	return 0;
fail:
	while (i--)
		device_remove_file(info->dev, &vibra_attrs[i]);

	return r;
}

static void vibra_unregister_sysfs(struct vibra_info *info)
{
	int i;

	for (i = ARRAY_SIZE(vibra_attrs) - 1; i >= 0; i--)
		device_remove_file(info->dev, &vibra_attrs[i]);
}

static int __init twl4030_vibra_probe(struct platform_device *pdev)
{
	struct vibra_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->enabled = 0;
	info->duration = 0;
	info->iseq = 0;
	info->nseq = 0;

	platform_set_drvdata(pdev, info);

	info->workqueue = create_singlethread_workqueue("vibra");
	if (info->workqueue == NULL) {
		dev_err(&pdev->dev, "couldn't create workqueue\n");
		kfree(info);
		return -ENOMEM;
	}

	mutex_init(&info->lock);
	INIT_DELAYED_WORK(&info->work, vibra_work);
	INIT_WORK(&info->led_work, vibra_led_work);

	info->vibra.name = "twl4030:vibrator";
	info->vibra.default_trigger = NULL;
	info->vibra.brightness = 0;
	info->vibra.brightness_set = vibra_led_set;
	info->vibra.brightness_get = NULL;

	if (led_classdev_register(&pdev->dev, &info->vibra) < 0)
		dev_dbg(&pdev->dev, "could not register vibrator to LED FW\n");

	if (vibra_register_sysfs(info) < 0)
		dev_dbg(&pdev->dev, "could not register sysfs files\n");

	return 0;
}

static int __exit twl4030_vibra_remove(struct platform_device *pdev)
{
	struct vibra_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->work);
	destroy_workqueue(info->workqueue);

	vibra_unregister_sysfs(info);
	led_classdev_unregister(&info->vibra);
	kfree(info);

	return 0;
}

MODULE_DESCRIPTION("Triton2 Vibra driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nokia Corporation");

static struct platform_driver twl4030_vibra_driver = {
	.probe		= twl4030_vibra_probe,
	.remove		= __exit_p(twl4030_vibra_remove),
	.driver		= {
		.name	= "twl4030_vibra",
	},
};

static int __init twl4030_vibra_init(void)
{
	return platform_driver_register(&twl4030_vibra_driver);
}
late_initcall(twl4030_vibra_init);

static void __exit twl4030_vibra_exit(void)
{
	platform_driver_unregister(&twl4030_vibra_driver);
}
module_exit(twl4030_vibra_exit);

MODULE_ALIAS("platform:twl4030-vibra");

