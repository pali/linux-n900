/*
 * This file implements a driver for SPI data driven vibrator.
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Ilkka Koskinen <ilkka.koskinen@nokia.com>
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

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/spi/vibra.h>
#include <linux/io.h>
#include <linux/uaccess.h>

/* Number of effects handled with memoryless devices */
#define VIBRA_EFFECTS		36
#define MAX_EFFECT_SIZE		1024 /* In bytes */
#define WAIT_TIMEOUT		10 /* In ms */

#define FF_EFFECT_QUEUED	BIT(0)
#define FF_EFFECT_PLAYING	BIT(1)
#define FF_EFFECT_ABORTING	BIT(2)
#define FF_EFFECT_UPLOADING	BIT(3)

enum vibra_status {
	IDLE = 0,
	STARTED,
	PLAYING,
	CLOSING,
};

struct vibra_effect_info {
	char		*buf;
	unsigned int	buflen;
	unsigned long	flags;	/* effect state (STARTED, PLAYING, etc) */
	int		remaining_ms;
	unsigned int	len_ms;
};

struct vibra_data {
	struct device			*dev;
	struct input_dev		*input_dev;

	struct workqueue_struct 	*workqueue;
	struct work_struct		play_work;

	struct spi_device		*spi_dev;
	struct spi_transfer		t;
	struct spi_message		msg;
	u32				spi_max_speed_hz;

	enum vibra_status		status;

	struct vibra_effect_info	effects[VIBRA_EFFECTS];
	int				next_effect;
	int				current_effect;
	wait_queue_head_t		wq;

	void (*set_power)(bool enable);
};

static int vibra_spi_raw_write_effect(struct vibra_data *vibra)
{
	spi_message_init(&vibra->msg);
	memset(&vibra->t, 0, sizeof(vibra->t));

	vibra->t.tx_buf	= vibra->effects[vibra->current_effect].buf;
	vibra->t.len	= vibra->effects[vibra->current_effect].buflen;
	spi_message_add_tail(&vibra->t, &vibra->msg);

	return spi_sync(vibra->spi_dev, &vibra->msg);
}

static void vibra_play_work(struct work_struct *work)
{
	DECLARE_WAITQUEUE(wait, current);
	struct vibra_data *vibra = container_of(work,
						struct vibra_data, play_work);
	struct vibra_effect_info *curr;
	int ret;

	add_wait_queue(&vibra->wq, &wait);
	while (1) {
		long val;

		if (vibra->status == CLOSING)
			goto switch_off;

		vibra->current_effect = vibra->next_effect;
		curr = &vibra->effects[vibra->current_effect];

		if (curr->flags & FF_EFFECT_ABORTING)
			goto switch_off;

		if (curr->remaining_ms > 0) {
			spin_lock_bh(&vibra->input_dev->event_lock);
			curr->flags |= FF_EFFECT_PLAYING;
			curr->remaining_ms -= curr->len_ms;
			spin_unlock_bh(&vibra->input_dev->event_lock);

			dev_dbg(vibra->dev, "curr->remaining_ms: %d\n",
				curr->remaining_ms);

			if (vibra->status == STARTED) {
				if (vibra->set_power)
					vibra->set_power(true);

				vibra->status = PLAYING;
			}

			ret = vibra_spi_raw_write_effect(vibra);
			if (ret < 0) {
				dev_err(vibra->dev,
					"Error replaying an effect: %d", ret);
				goto switch_off;
			}

			spin_lock_bh(&vibra->input_dev->event_lock);
			curr->flags &= ~FF_EFFECT_PLAYING;
			spin_unlock_bh(&vibra->input_dev->event_lock);

			continue;
		} else {
			dev_dbg(vibra->dev, "curr->remaining_ms: %d\n",
				curr->remaining_ms);
		}

		/*
		 * Nothing to play, so switch off the power if no data
		 * appears before the timeout.
		 */
		val = wait_event_interruptible_timeout(
			vibra->wq,
			vibra->effects[vibra->next_effect].remaining_ms > 0,
			msecs_to_jiffies(WAIT_TIMEOUT));
		if (val > 0)
			continue;
switch_off:
		if (vibra->set_power)
			vibra->set_power(false);

		vibra->status = IDLE;
		remove_wait_queue(&vibra->wq, &wait);
		return;
	}
}

/*
 * Input/Force feedback guarantees that playback() is called with spinlock held
 * and interrupts off.
 */
static int vibra_spi_playback(struct input_dev *input, int effect_id, int value)
{
	struct vibra_data *vibra = input_get_drvdata(input);
	struct vibra_effect_info *einfo = &vibra->effects[effect_id];
	struct ff_effect *ff_effect = &input->ff->effects[effect_id];

	if (!vibra->workqueue)
		return -ENODEV;

	if (einfo->flags & FF_EFFECT_UPLOADING)
		return -EBUSY;

	if (value == 0) {
		/* Abort the given effect */
		if (einfo->flags & FF_EFFECT_PLAYING)
			einfo->flags |= FF_EFFECT_ABORTING;

		einfo->flags &= ~FF_EFFECT_QUEUED;
	} else {
		/* Move the given effect as the next one */
		vibra->effects[vibra->next_effect].flags &= ~FF_EFFECT_QUEUED;

		vibra->next_effect = effect_id;
		einfo->flags |= FF_EFFECT_QUEUED;
		einfo->flags &= ~FF_EFFECT_ABORTING;
		einfo->remaining_ms = ff_effect->replay.length;

		if (vibra->status == IDLE) {
			vibra->status = STARTED;
			queue_work(vibra->workqueue, &vibra->play_work);
		}

		wake_up_interruptible(&vibra->wq);
	}

	return 0;
}

static int vibra_spi_upload(struct input_dev *input, struct ff_effect *effect,
			    struct ff_effect *old)
{
	struct vibra_data *vibra = input_get_drvdata(input);
	struct vibra_effect_info *einfo = &vibra->effects[effect->id];
	struct ff_periodic_effect *p = &effect->u.periodic;
	unsigned int datalen;
	int ret = 0;

	if (effect->type != FF_PERIODIC || p->waveform != FF_CUSTOM)
		return -EINVAL;

	spin_lock_bh(&vibra->input_dev->event_lock);
	if (einfo->flags &
	    (FF_EFFECT_QUEUED | FF_EFFECT_PLAYING | FF_EFFECT_UPLOADING)) {
		spin_unlock_bh(&vibra->input_dev->event_lock);
		return -EBUSY;
	}

	einfo->flags |= FF_EFFECT_UPLOADING;
	spin_unlock_bh(&vibra->input_dev->event_lock);

	datalen = p->custom_len * sizeof(p->custom_data[0]);
	if (datalen > MAX_EFFECT_SIZE) {
		pr_err("datalen: %d, MAX_EFFECT_SIZE: %d\n",
		       datalen, MAX_EFFECT_SIZE);

		ret = -ENOSPC;
		goto exit;
	}

	if (einfo->buf && einfo->buflen != datalen) {
		kfree(einfo->buf);
		einfo->buf = NULL;
	}

	if (!einfo->buf) {
		einfo->buf = kzalloc(datalen, GFP_KERNEL);
		if (!einfo->buf) {
			ret = -ENOMEM;
			goto exit;
		}
	}

	/*
	 * Input layer has performed copy_from_user() for the effect but not
	 * for custom_data. Moreover, custom_data has basically lost the
	 * information that it originates from user space.
	 */
	if (copy_from_user(einfo->buf, (void __user *) p->custom_data, datalen))
		return -EFAULT;

	einfo->buflen = datalen;

	/* Sample length in ms. */
	einfo->len_ms = 1000 * datalen * BITS_PER_BYTE /
		vibra->spi_max_speed_hz;
	if (einfo->len_ms < 1) {
		pr_err("Effect too short: %d\n", datalen);
		return -EINVAL;
	}
exit:
	einfo->flags &= ~FF_EFFECT_UPLOADING;
	return ret;
}

static int vibra_spi_open(struct input_dev *input)
{
	struct vibra_data *vibra = input_get_drvdata(input);

	vibra->workqueue = create_singlethread_workqueue("vibra");
	if (!vibra->workqueue) {
		dev_err(&input->dev, "couldn't create workqueue\n");
		return -ENOMEM;
	}

	return 0;
}

static void vibra_spi_close(struct input_dev *input)
{
	struct vibra_data *vibra = input_get_drvdata(input);

	vibra->status = CLOSING;

	cancel_work_sync(&vibra->play_work);
	INIT_WORK(&vibra->play_work, vibra_play_work);
	destroy_workqueue(vibra->workqueue);
	vibra->workqueue = NULL;

	vibra->status = IDLE;
}

static int __devinit vibra_spi_probe(struct spi_device *spi)
{
	struct vibra_data *vibra;
	struct ff_device *ff;
	struct vibra_spi_platform_data *pdata;
	int ret = -ENOMEM;

	pdata = spi->dev.platform_data;
	if (!pdata)
		return -ENODEV;

	vibra = kzalloc(sizeof(*vibra), GFP_KERNEL);
	if (!vibra) {
		dev_err(&spi->dev, "Not enough memory");
		return -ENOMEM;
	}

	vibra->spi_max_speed_hz = spi->max_speed_hz;
	vibra->set_power = pdata->set_power;

	INIT_WORK(&vibra->play_work, vibra_play_work);
	init_waitqueue_head(&vibra->wq);

	vibra->dev = &spi->dev;
	spi_set_drvdata(spi, vibra);
	vibra->spi_dev = spi;

	spi->bits_per_word = 32;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed");
		goto err_spi_setup;
	}

	vibra->input_dev = input_allocate_device();
	if (!vibra->input_dev) {
		dev_err(vibra->dev, "couldn't allocate input device\n");
		ret = -ENOMEM;
		goto err_input_alloc;
	}

	input_set_drvdata(vibra->input_dev, vibra);

	vibra->input_dev->name		= "SPI vibrator";
	vibra->input_dev->id.version	= 1;
	vibra->input_dev->dev.parent	= spi->dev.parent;
	vibra->input_dev->open		= vibra_spi_open;
	vibra->input_dev->close		= vibra_spi_close;

	set_bit(FF_PERIODIC, vibra->input_dev->ffbit);
	set_bit(FF_CUSTOM, vibra->input_dev->ffbit);

	ret = input_ff_create(vibra->input_dev, VIBRA_EFFECTS);
	if (ret) {
		dev_err(&spi->dev, "Couldn't create input feedback device");
		goto err_input_ff_create;
	}

	ff		= vibra->input_dev->ff;
	ff->private	= vibra;
	ff->upload	= vibra_spi_upload;
	ff->playback	= vibra_spi_playback;

	ret = input_register_device(vibra->input_dev);
	if (ret < 0) {
		dev_dbg(&spi->dev, "couldn't register input device\n");
		goto err_input_register;
	}

	dev_dbg(&spi->dev, "SPI driven Vibra driver initialized\n");
	return 0;

err_input_register:
	input_ff_destroy(vibra->input_dev);
err_input_ff_create:
	input_free_device(vibra->input_dev);
err_input_alloc:
err_spi_setup:
	kfree(vibra);
	return ret;
}

static int __devexit vibra_spi_remove(struct spi_device *spi)
{
	struct vibra_data *vibra = dev_get_drvdata(&spi->dev);
	int i;

	for (i = 0; i < VIBRA_EFFECTS; i++)
		kfree(vibra->effects[i].buf);

	/*
	 * No need to do kfree(vibra) since the following calls
	 * input_ff_destroy, which does kfree(ff->private)
	 */
	input_unregister_device(vibra->input_dev);
	return 0;
}

static struct spi_driver vibra_spi_driver = {
	.driver = {
		.name		= "vibra_spi",
		.owner		= THIS_MODULE,
	},

	.probe		= vibra_spi_probe,
	.remove		= __devexit_p(vibra_spi_remove),
};

static int __init vibra_spi_init(void)
{
	return spi_register_driver(&vibra_spi_driver);
}
module_init(vibra_spi_init);

static void __exit vibra_spi_exit(void)
{
	spi_unregister_driver(&vibra_spi_driver);
}
module_exit(vibra_spi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilkka Koskinen <ilkka.koskinen@nokia.com>");
MODULE_ALIAS("spi:vibra_spi");
