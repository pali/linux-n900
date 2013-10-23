/*
 * drivers/i2c/chips/twl4030-madc.c
 *
 * TWL4030 MADC module driver
 *
 * Copyright (C) 2009 Nokia Corporation
 * Tuukka Tikkanen <tuukka.tikkanen@nokia.com>
 * Mikko Ylinen <mikko.k.ylinen@nokia.com>
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
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/i2c/twl4030.h>
#include <linux/i2c/twl4030-madc.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/byteorder/generic.h>
#include <linux/workqueue.h>

#include <linux/uaccess.h>


#define TWL4030_MADC_CTRL1		0x00
#define TWL4030_MADC_MADCON		(1<<0)	/* MADC power on */

#define TWL4030_MADC_CTRL2		0x01
#define TWL4030_MADC_STARTADCRF		(1<<0)	/* HW trigger polarity */

#define TWL4030_MADC_RTSELECT_LSB	0x02
#define TWL4030_MADC_SW1SELECT_LSB	0x06
#define TWL4030_MADC_SW2SELECT_LSB	0x0A

#define TWL4030_MADC_RTAVERAGE_LSB	0x04
#define TWL4030_MADC_SW1AVERAGE_LSB	0x08
#define TWL4030_MADC_SW2AVERAGE_LSB	0x0C

#define TWL4030_MADC_CTRL_SW1		0x12
#define TWL4030_MADC_CTRL_SW2		0x13

#define TWL4030_MADC_RTCH0_LSB		0x17
#define TWL4030_MADC_GPCH0_LSB		0x37

#define TWL4030_MADC_ACQUISITION	0x0f

#define TWL4030_MADC_BUSY		(1<<0)	/* MADC busy */
#define TWL4030_MADC_EOC_SW		(1<<1)	/* MADC conversion completion */
#define TWL4030_MADC_SW_START		(1<<5)  /* MADC SWx start conversion */

/* TWL4030 and TWL5030 have BCICTL1 in MAIN_CHARGE module */
#define TWL4030_BCI_BCICTL1		0x23
#define	TWL4030_BCI_MESBAT		(1<<1)
#define	TWL4030_BCI_TYPEN		(1<<4)
#define	TWL4030_BCI_ITHEN		(1<<3)

/* TWL5031 has BCIA_CTRL in ACCESSORY module */
#define TWL5031_BCIA_CTRL		0x0E
#define TWL5031_BCIA_MESBAT_EN		1
#define TWL5031_BCIA_MESVAC_EN		2
#define TWL5031_BCIA_BTEMP_EN		4


struct twl4030_madc_conversion_mode {
	u8 sel;
	u8 avg;
	u8 rbase;
	u8 ctrl;
};

enum twl4030_madc_conversion_mode_ids {
	TWL4030_MADC_MODE_RT,
	TWL4030_MADC_MODE_SW1,
	TWL4030_MADC_MODE_SW2,
	TWL4030_MADC_NUM_MODES,
};

struct twl4030_madc_request {
	struct list_head	req_queue;
	u16			channel_mask;
	u16			average_mask;
	u16			polarity;
	u16			delay;
	u16			flags;
	u16			*rbuf;
	void			(*func_cb)(int, struct twl4030_madc_request *);
	void			*user_data;
	struct completion	comp;
	struct delayed_work	hybrid_work;
	int			status;
	int			not_in_queue;
	u16			ch0_event_count;
};

struct twl4030_madc_data {
	struct device		*dev;
	spinlock_t		misc_lock; /* miscdev and madc protection */
	struct mutex		lock; /* General madc mutex */
	struct mutex		isr_bits_lock; /* ISR access/caching only */
	struct mutex		imr_lock; /* IMR access only */
	struct workqueue_struct	*hybrid_work;
	struct list_head	sw_queue;
	struct list_head	rt_queue;
	struct twl4030_madc_request	*active[TWL4030_MADC_NUM_MODES];
	int			imr_addr;
	int			isr_addr;
	unsigned int		has_bci;
	int			ch0_unavailable;
	u16			ch0_event_count;
	u16			current_delay;
	u16			current_polarity;
	u16			current_mask[TWL4030_MADC_NUM_MODES][2];
	u8			isr_bits;
	u8			ctrl1_value;
	u8			ctrl2_value;
	u8			current_imr;
	u8			check_busy; /* Bitmask for channels */
};

struct twl4030_madc_filp_data {
	struct completion		complex_complete;
	struct mutex			data_lock; /* Result area mutex */
	struct twl4030_madc_request	*req;
	u16				*conversion_data;
	wait_queue_head_t		complex_wait;
};

static struct twl4030_madc_data *the_madc;

static int twl4030_madc_set_current_generator(struct twl4030_madc_data *madc,
					      int chan, int on);
static void twl4030_madc_process_queues(void);
static int twl4030_madc_set_power(struct twl4030_madc_data *madc, int on);
static void twl4030_madc_hybrid_finish(struct work_struct *work);
static void twl4030_madc_remove_request(struct twl4030_madc_request *req);

static const struct twl4030_madc_conversion_mode twl4030_conversion_modes[] = {
	[TWL4030_MADC_MODE_RT] = {
		.sel	= TWL4030_MADC_RTSELECT_LSB,
		.avg	= TWL4030_MADC_RTAVERAGE_LSB,
		.rbase	= TWL4030_MADC_RTCH0_LSB,
	},
	[TWL4030_MADC_MODE_SW1] = {
		.sel	= TWL4030_MADC_SW1SELECT_LSB,
		.avg	= TWL4030_MADC_SW1AVERAGE_LSB,
		.rbase	= TWL4030_MADC_GPCH0_LSB,
		.ctrl	= TWL4030_MADC_CTRL_SW1,
	},
	[TWL4030_MADC_MODE_SW2] = {
		.sel	= TWL4030_MADC_SW2SELECT_LSB,
		.avg	= TWL4030_MADC_SW2AVERAGE_LSB,
		.rbase	= TWL4030_MADC_GPCH0_LSB,
		.ctrl	= TWL4030_MADC_CTRL_SW2,
	},
};


static int twl4030_madc_read_byte(struct twl4030_madc_data *madc, u8 reg)
{
	int ret;
	u8 val;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_MADC, &val, reg);
	if (ret) {
		dev_dbg(madc->dev, "unable to read register 0x%X\n", reg);
		return ret;
	}

	return val;
}

static int twl4030_madc_write_byte(struct twl4030_madc_data *madc,
				   u8 reg, u8 val)
{
	int ret;

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_MADC, val, reg);
	if (ret)
		dev_err(madc->dev, "unable to write register 0x%X\n", reg);

	return ret;
}

static int twl4030_madc_read_channels(struct twl4030_madc_data *madc,
		u8 reg_base, u16 ch_mask, u16 *buf)
{
	int ret;
	u8 first_ch, num_ch;
	u16 tmp_mask;
	int i;

	if (unlikely(!buf) || unlikely(!ch_mask))
		return 0;

	/*
	 * Channel values are stored in little-endian form in a array of 2*16
	 * 8-bit registers. Reading the part of array starting with first
	 * converted channel and ending the the last converted channel is
	 * enough and while keeping the number of separate i2c operations low.
	 */
	tmp_mask = ch_mask;
	for (first_ch = 0; !(tmp_mask & 1); first_ch++)
		tmp_mask >>= 1;
	for (num_ch = 0; tmp_mask; num_ch++)
		tmp_mask >>= 1;

	ret = twl4030_i2c_read(TWL4030_MODULE_MADC, (u8 *)&buf[first_ch],
			       reg_base + 2*first_ch, 2*num_ch);
	if (ret)
		return -EIO;

	/* Adjust bit positions and endianness if necessary */
	for (i = 0; i < TWL4030_MADC_MAX_CHANNELS; i++)
		if (ch_mask & (1<<i))
			buf[i] = le16_to_cpu(buf[i]) >> 6;

	return 0;
}

static u16 twl4030_madc_round_delay(u16 aq_delay)
{
	/* Acquisition delay is 5ns + register value. The 11 bit value is
	 * calculated from register value by skipping some powers of 2.
	 * In order to round up, we need to determine if some of the
	 * missing bits would be set to 1 in the requested delay.
	 */

	if (aq_delay >= 5)
		aq_delay -= 5;
	else
		aq_delay = 0;

	if (aq_delay >= 0x06af) /* 1024+512+128+32+8+4+2+1 */
		return 0x06af + 5;

	if (aq_delay & 0x0010)
		aq_delay = (aq_delay & 0xffe0) + 0x0020;

	if (aq_delay & 0x0040)
		aq_delay = (aq_delay & 0xff80) + 0x0080;

	if (aq_delay & 0x0100)
		aq_delay = (aq_delay & 0xfe00) + 0x0200;

	return aq_delay + 5;
}

static u8 twl4030_madc_encode_aq_delay(u16 aq_delay)
{
	static const u16 sparse_powers[] = {
		1024, 512, 128, 32, 8, 4, 2, 1, 0
	};
	const u16 *next_power = sparse_powers;
	u8 result = 0;

	aq_delay -= 5;
	while (*next_power) {
		result <<= 1;
		if (aq_delay & *next_power)
			result |= 1;
		++next_power;
	}

	return result;
}

static inline void twl4030_madc_set_acquisition(u16 aq_delay)
{
	twl4030_madc_write_byte(the_madc, TWL4030_MADC_ACQUISITION,
				twl4030_madc_encode_aq_delay(aq_delay));
	the_madc->current_delay = aq_delay;
}


static void twl4030_madc_set_polarity(u16 flags)
{
	/* Must not be called without holding the general madc mutex. */
	u8 regval;

	regval = the_madc->ctrl2_value;
	if (flags & TWL4030_MADC_RT_TRIG_RISING)
		regval |= TWL4030_MADC_STARTADCRF;
	else
		regval &= ~TWL4030_MADC_STARTADCRF;
	twl4030_madc_write_byte(the_madc, TWL4030_MADC_CTRL2, regval);
	the_madc->ctrl2_value = regval;
	the_madc->current_polarity = flags & TWL4030_MADC_RT_TRIG_MASK;
}


static int twl4030_madc_enable_irq(struct twl4030_madc_data *madc, int id)
{
	int ret = 0;

	mutex_lock(&madc->imr_lock);
	if (madc->current_imr & (1 << id)) {
		madc->current_imr &= ~(1 << id);
		ret = twl4030_madc_write_byte(madc, madc->imr_addr,
					      madc->current_imr);
	}
	mutex_unlock(&madc->imr_lock);

	return ret;
}

/* Visit the callback if one has been requested.
 * A callback means this function is responsible for
 * releasing the memory allocated for request structure.
 * Lack of callback means the initiator is synchronous and
 * completion should be performed instead.
 */
static void twl4030_madc_end_request(struct twl4030_madc_request *r, int status)
{
	/* Flags may be lost when the info is needed */
	u16 flags = r->flags;
	if (r->func_cb != NULL) {
		r->func_cb(status, r);
		if (!(flags & TWL4030_MADC_CB_RELEASES_MEM))
			kfree(r);
	} else {
		r->status = status;
		complete(&r->comp);
	}
}

static irqreturn_t twl4030_madc_irq_handler(int irq, void *_madc)
{
	struct twl4030_madc_data *madc = _madc;
	u8 isr_val;

#ifdef CONFIG_LOCKDEP
	/*
	 * This is not a real hard irq context and lockdep mistakenly left
	 * hardirq disabled.
	 */
	local_irq_enable();
#endif

	/* Use COR to ack interrupts since we have no shared IRQs in ISRx.
	 * Disable interrupt generation for RT channel if conversion complete.
	 * This code is not executed in real interrupt, so we can and should
	 * use mutexes in order to yeild to the competing thread as fast as
	 * possible if we are blocked. The test to see if an interrupt needs
	 * to be disabled can be done without locking as no competing thread
	 * can exist to enable bit for already active channel.
	 */
	isr_val = twl4030_madc_read_byte(madc, madc->isr_addr);
	if (~madc->current_imr & isr_val & (1 << TWL4030_MADC_MODE_RT)) {
		mutex_lock(&madc->imr_lock);
		madc->current_imr |= 1 << TWL4030_MADC_MODE_RT;
		twl4030_madc_write_byte(madc, madc->imr_addr,
					madc->current_imr);
		mutex_unlock(&madc->imr_lock);
	}

	mutex_lock(&madc->isr_bits_lock);
	madc->isr_bits |= isr_val;
	mutex_unlock(&madc->isr_bits_lock);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t twl4030_madc_irq_thread(int irq, void *_madc)
{
	struct twl4030_madc_data *madc = _madc;
	const struct twl4030_madc_conversion_mode *mode;
	struct twl4030_madc_request *r;
	u8 isr_val;
	u8 disable_buf[3];
	int ret, i, skip_read, skip_complete;

	mutex_lock(&madc->isr_bits_lock);
	isr_val = madc->isr_bits;
	madc->isr_bits = 0;
	mutex_unlock(&madc->isr_bits_lock);

	for (i = 0; i < TWL4030_MADC_NUM_MODES; i++) {

		if (!(isr_val & (1<<i)))
			continue;

		skip_read = 0;
		skip_complete = 0;

		/*
		 * Flag this channel ready to accept next conversion without
		 * need to check the busy bit in hardware.
		 */
		madc->check_busy &= ~(1<<i);

		mode = &twl4030_conversion_modes[i];

		if (i == TWL4030_MADC_MODE_RT) {
			/*
			 * Only way to disable RT conversions is to clear
			 * the channel mask.
			 */
			disable_buf[1] = 0;
			disable_buf[2] = 0;
			twl4030_i2c_write(TWL4030_MODULE_MADC, disable_buf,
					  mode->sel, 2);
		}

		mutex_lock(&madc->lock);
		r = madc->active[i];

		/* The request may have been terminated */
		if (!r) {
			mutex_unlock(&madc->lock);
			continue;
		}

		/*
		 * Did the conversion ask for ch0 and did some other driver
		 * mess with it? If so, requeue.
		 */
		if ((r->channel_mask & TWL4030_MADC_ADCIN0) &&
		    r->ch0_event_count != madc->ch0_event_count) {
			if ((r->flags & TWL4030_MADC_TRIG_MODE_MASK) ==
			    TWL4030_MADC_TRIG_MODE_HYBRID) {
				/*
				 * This is a complex situation. Potentially
				 * there is another pipeline with the same
				 * request, which may or may not be complete.
				 * We can not simply put this back to queue.
				 * Just flag the data as tainted and let the
				 * hybrid completion code check for this.
				 */
				r->flags |= TWL4030_MADC_DATA_TAINTED;
				skip_read = 1;
			} else {
				/* Easier case, simply requeue */
				madc->active[i] = NULL;
				r->not_in_queue = 0;
				if (i != TWL4030_MADC_MODE_RT)
					list_add_tail(&r->req_queue,
						      &madc->sw_queue);
				else
					list_add_tail(&r->req_queue,
						      &madc->rt_queue);
				mutex_unlock(&madc->lock);
				continue;
			}
		}

		if ((r->flags & TWL4030_MADC_TRIG_MODE_MASK) ==
		    TWL4030_MADC_TRIG_MODE_HYBRID) {
			if (i != TWL4030_MADC_MODE_RT) {
				/* SW conversion of a hybrid req */
				r->flags |= TWL4030_MADC_HYBRID_SW_COMPLETED;
				skip_complete = 1;
				/*
				 * If we have RT data, that is preferential
				 * and SW results are tossed.
				 */
				if (r->flags & TWL4030_MADC_HYBRID_RT_COMPLETED)
					skip_read = 1;
			} else {
				/* RT conversion of a hybrid req */
				r->flags |= TWL4030_MADC_HYBRID_RT_COMPLETED;
				skip_complete = 1;
			}
		}

		/* Read results */
		if (skip_read)
			ret = 0;
		else
			ret = twl4030_madc_read_channels(madc, mode->rbase,
							 r->channel_mask,
							 r->rbuf);
		madc->active[i] = NULL;

		if (!skip_complete)
			twl4030_madc_end_request(r, ret);
		mutex_unlock(&madc->lock);
	}

	twl4030_madc_process_queues();

	return IRQ_HANDLED;
}

static int twl4030_madc_init_conversion(struct twl4030_madc_request *req,
					u16 mode_id, int force_setup)
{
	const struct twl4030_madc_conversion_mode *mode;
	u8 setup_buf[5];
	int setup_length = 0;
	int ret = 0;

	mode = &twl4030_conversion_modes[mode_id];

	the_madc->active[mode_id] = req;
	req->not_in_queue = 1;

	/* Keep the number and size of i2c accesses to minimum */
	if (the_madc->current_mask[mode_id][1] != req->average_mask ||
	    unlikely(force_setup) || unlikely(mode_id == TWL4030_MADC_MODE_RT))
		setup_length = 4;
	else if (the_madc->current_mask[mode_id][0] != req->channel_mask)
		setup_length = 2;

	if (setup_length) {
		/* Update channel and averaging mask if necessary */
		the_madc->current_mask[mode_id][0] = req->channel_mask;
		the_madc->current_mask[mode_id][1] = req->average_mask;
		setup_buf[1] = req->channel_mask & 0xff;
		setup_buf[2] = (req->channel_mask >> 8) & 0xff;
		setup_buf[3] = req->average_mask & 0xff;
		setup_buf[4] = (req->average_mask >> 8) & 0xff;

		ret = twl4030_i2c_write(TWL4030_MODULE_MADC, setup_buf,
					mode->sel, setup_length);
	}

	if (!ret)
		ret = twl4030_madc_enable_irq(the_madc, mode_id);

	if (!ret && (mode_id != TWL4030_MADC_MODE_RT)) {
		the_madc->check_busy |= 1<<mode_id;
		ret = twl4030_madc_write_byte(the_madc, mode->ctrl,
					      TWL4030_MADC_SW_START);
	}

	if (ret)
		the_madc->active[mode_id] = NULL;

	return ret;
}

static void twl4030_madc_activate_queue_head(struct list_head  *queue,
					     u16  mode_id)
{
	const struct twl4030_madc_conversion_mode *mode;
	struct twl4030_madc_request *req;
	struct twl4030_madc_request *firstseen;
	int ret;
	int force_setup = 0;

	if (list_empty(queue))
		return;

	mode = &twl4030_conversion_modes[mode_id];

	/* Some registers are read-only while existing (canceled) conversion
	 * is in progress, in that case wait for interrupt and try again.
	 */
	if (the_madc->check_busy & (1<<mode_id)) {
		ret = twl4030_madc_read_byte(the_madc, mode->ctrl);
		if (ret < 0 || !(ret & TWL4030_MADC_EOC_SW))
			return;
		/* Known to be idle now, but force setup of all registers */
		the_madc->check_busy &= ~(1<<mode_id);
		force_setup = 1;
	}

	firstseen = NULL;
	while (!list_empty(queue)) {
		req = list_first_entry(queue,
				       struct twl4030_madc_request, req_queue);
		if (firstseen == req)
			return;
		list_del(&req->req_queue);

		/* Requeue ch0 requests if that channel is not available */
		if (the_madc->ch0_unavailable &&
		    (req->channel_mask & TWL4030_MADC_ADCIN0)) {
			if (firstseen == NULL)
				firstseen = req;
			list_add_tail(&req->req_queue, queue);
			continue;
		}
		req->ch0_event_count = the_madc->ch0_event_count;

		ret = twl4030_madc_init_conversion(req, mode_id, force_setup);
		if (!ret && ((req->flags & TWL4030_MADC_TRIG_MODE_MASK) ==
			     TWL4030_MADC_TRIG_MODE_HYBRID)) {
			ret = twl4030_madc_init_conversion(req,
				TWL4030_MADC_MODE_SW1, 1);
			if (ret) {
				req->flags |= TWL4030_MADC_HYBRID_SW_FAILED;
				ret = 0;
			}
			queue_delayed_work(the_madc->hybrid_work,
					   &req->hybrid_work, HZ/100+1);
		}
		if (ret)
			twl4030_madc_end_request(req, ret);
		else
			return;
	}
}

static void twl4030_madc_process_queues(void)
{
	int i;
	struct twl4030_madc_request *req;
	u16 modes_busy;
	u16 use_delay;
	u16 use_polarity;
	u16 need_restart;

	if (the_madc == NULL)
		return;

	/* Strategy for handling queues:
	 * 1) If a hybrid request with any settings or RT request with delay
	 *    or polarity different from current settings is next in queue:
	 *    finish all current activity and power down the subchip and
	 *    reprogram new values, then proceed.
	 * 2) If no RT request is in queue or active, current delay is
	 *    more than the minimum and SW requests are queued: finish all
	 *    current activity and power down the subchip to reprogram minimum
	 *    delay, then proceed.
	 * 3) If there are no active conversions and queues are empty,
	 *    power down if necessary and exit.
	 * 4) Power up if necessary.
	 * 5) Activate as many conversions as possible.
	 */


	mutex_lock(&the_madc->lock);
	modes_busy = 0;
	need_restart = 0;

	for (i = 0; i < TWL4030_MADC_NUM_MODES; ++i)
		if (the_madc->active[i])
			modes_busy |= 1<<i;

	if (!list_empty(&the_madc->rt_queue)) {
		req = list_first_entry(&the_madc->rt_queue,
				       struct twl4030_madc_request, req_queue);
		if (((req->flags & TWL4030_MADC_TRIG_MODE_MASK) ==
		     TWL4030_MADC_TRIG_MODE_HYBRID) ||
		    req->delay != the_madc->current_delay ||
		    req->polarity != the_madc->current_polarity) {
			/* 1 */
			if (modes_busy) {
				mutex_unlock(&the_madc->lock);
				return;
			}
			need_restart = 1;
			use_delay = req->delay;
			use_polarity = req->polarity;
		}
	} else if (!list_empty(&the_madc->sw_queue) &&
		   !(modes_busy & (1<<TWL4030_MADC_MODE_RT)) &&
		   (the_madc->current_delay > 5)) {
		/* 2 */
		if (modes_busy) {
			mutex_unlock(&the_madc->lock);
			return;
		}
		need_restart = 1;
		use_delay = 5;
		use_polarity = the_madc->current_polarity;
	}

	if (need_restart) {
		if ((the_madc->ctrl1_value & TWL4030_MADC_MADCON))
			twl4030_madc_set_power(the_madc, 0);
		if (use_delay != the_madc->current_delay)
			twl4030_madc_set_acquisition(use_delay);
		if (use_polarity != the_madc->current_polarity)
			twl4030_madc_set_polarity(use_polarity);
	}

	if (list_empty(&the_madc->sw_queue) &&
	    list_empty(&the_madc->rt_queue)) {
		/* 3 */
		if ((the_madc->ctrl1_value & TWL4030_MADC_MADCON))
			twl4030_madc_set_power(the_madc, 0);
		mutex_unlock(&the_madc->lock);
		return;
	}

	/* 4 */
	if (!(the_madc->ctrl1_value & TWL4030_MADC_MADCON))
		twl4030_madc_set_power(the_madc, 1);

	/* 5 */
	if (!(modes_busy & (1<<TWL4030_MADC_MODE_RT)))
		twl4030_madc_activate_queue_head(&the_madc->rt_queue,
						 TWL4030_MADC_MODE_RT);
	/* SW pipeline might have been reserved for hybrid mode */
	for (i = 0; i < TWL4030_MADC_NUM_MODES; ++i)
		if (the_madc->active[i])
			modes_busy |= 1<<i;
	if (!(modes_busy & (1<<TWL4030_MADC_MODE_SW1)))
		twl4030_madc_activate_queue_head(&the_madc->sw_queue,
						 TWL4030_MADC_MODE_SW1);
	if (!(modes_busy & (1<<TWL4030_MADC_MODE_SW2)))
		twl4030_madc_activate_queue_head(&the_madc->sw_queue,
						 TWL4030_MADC_MODE_SW2);

	mutex_unlock(&the_madc->lock);
}

static void twl4030_madc_hybrid_finish(struct work_struct *work)
{
	u8 disable_buf[3];
	int ret;
	struct twl4030_madc_request *req =
		container_of(work, struct twl4030_madc_request,
			     hybrid_work.work);
	int status = 0;

	/*
	 * Under normal conditions the SW conversion should be complete now
	 * and RT conversion may or may not have occurred. If not, we must
	 * disable it as the first thing.
	 */
	mutex_lock(&the_madc->lock);
	if (the_madc->active[TWL4030_MADC_MODE_RT] == req) {
		disable_buf[1] = 0;
		disable_buf[2] = 0;
		twl4030_i2c_write(TWL4030_MODULE_MADC, disable_buf,
			twl4030_conversion_modes[TWL4030_MADC_MODE_RT].sel, 2);
		/* HW may have blocked this (conversion busy!) - verify */
		ret = twl4030_i2c_read(TWL4030_MODULE_MADC, disable_buf,
			twl4030_conversion_modes[TWL4030_MADC_MODE_RT].sel, 2);
		if (ret || disable_buf[0] || disable_buf[1]) {
			/*
			 * Since there is work in progress, we should be
			 * seeing a completion interrupt in near future. Give
			 * it another 5 ms to complete as we prefer RT
			 * results over SW results.
			 * In case of some really funky hardware failure,
			 * we can keep trying this until someone else decides
			 * to cancel this request, which is easy to see
			 * from ->active[RT].
			 */
			queue_delayed_work(the_madc->hybrid_work,
					   &req->hybrid_work, HZ/200+1);
			mutex_unlock(&the_madc->lock);
			return;
		}
		/* Success! */
		the_madc->active[TWL4030_MADC_MODE_RT] = NULL;
	}
	mutex_unlock(&the_madc->lock);

	/* Now worry about the SW part for a while */
	if (!(req->flags & TWL4030_MADC_HYBRID_SW_FAILED)) {
		/* Failed to start; ok if RT results are available */
		if (req->flags & TWL4030_MADC_HYBRID_RT_COMPLETED)
			req->flags |= TWL4030_MADC_HYBRID_SW_COMPLETED;
	}
	if (!(req->flags & TWL4030_MADC_HYBRID_SW_COMPLETED)) {
		/*
		 * This should never happen. The only non-catastropic
		 * way this could happen is RT conversions happening so
		 * frequently the SW conversion can never finish before
		 * being restarted. If we have RT data, just assume that is
		 * the case and give some more time, otherwise assume the
		 * sky is falling.
		 */
		if (req->flags & TWL4030_MADC_HYBRID_RT_COMPLETED) {
			queue_delayed_work(the_madc->hybrid_work,
					   &req->hybrid_work, HZ/200+1);
			return;
		}

		/* Remove pointers to this request and report error. */
		mutex_lock(&the_madc->lock);
		twl4030_madc_remove_request(req);
		mutex_unlock(&the_madc->lock);
		status = -EIO;
	} else if (req->flags & TWL4030_MADC_DATA_TAINTED) {
		req->not_in_queue = 0;
		req->flags &= ~TWL4030_MADC_DATA_TAINTED;
		mutex_lock(&the_madc->lock);
		list_add_tail(&req->req_queue, &the_madc->rt_queue);
		mutex_unlock(&the_madc->lock);
		return;
	}

	twl4030_madc_end_request(req, status);
}

/**
 * twl4030_madc_remove_request: - remove driver-level pointers to a request
 * @req:	madc conversion request to remove from queues
 *
 * This function removes driver-level pointers to a madc conversion request
 * structure so the memory may be freed. It can not stop the hardware in
 * case the request is already being processed.
 *
 * Caller must hold the madc mutex.
 */
static void twl4030_madc_remove_request(struct twl4030_madc_request *req)
{
	int i;

	for (i = 0; i < TWL4030_MADC_NUM_MODES; ++i)
		if (the_madc->active[i] == req)
			the_madc->active[i] = NULL;

	if (!req->not_in_queue) {
		/* The request is still in either queue */
		list_del(&req->req_queue);
	}
}

void twl4030_madc_cancel_request(struct twl4030_madc_request *req)
{
	mutex_lock(&the_madc->lock);
	twl4030_madc_remove_request(req);
	mutex_unlock(&the_madc->lock);
	if ((req->flags & TWL4030_MADC_TRIG_MODE_MASK) ==
	    TWL4030_MADC_TRIG_MODE_HYBRID)
		if (!cancel_delayed_work(&req->hybrid_work))
			flush_workqueue(the_madc->hybrid_work);
	kfree(req);
}
EXPORT_SYMBOL(twl4030_madc_cancel_request);

int twl4030_madc_single_conversion(u16 channel, u16 average)
{
	u16 rbuf[TWL4030_MADC_MAX_CHANNELS];
	u16 channel_mask, average_mask;
	int ret;

	if (channel >= TWL4030_MADC_MAX_CHANNELS)
		return -EINVAL;

	channel_mask = 1 << channel;
	if (average)
		average_mask = channel_mask;
	else
		average_mask = 0;

	ret = twl4030_madc_conversion(channel_mask, average_mask, rbuf);

	if (ret < 0)
		return ret;

	return rbuf[channel];
}
EXPORT_SYMBOL(twl4030_madc_single_conversion);

int twl4030_madc_conversion(u16 channel_mask, u16 average_mask, u16 *rbuf)
{
	struct twl4030_madc_request *req;
	int ret;

	if (the_madc == NULL)
		return -ENODEV;

	if (rbuf == NULL || channel_mask == 0)
		return -EINVAL;

	req = kzalloc(sizeof(struct twl4030_madc_request), GFP_KERNEL);
	if (req == NULL)
		return -ENOMEM;

	req->channel_mask = channel_mask;
	req->average_mask = average_mask & channel_mask;
	req->rbuf = rbuf;
	init_completion(&req->comp);
	req->status = -EIO;

	mutex_lock(&the_madc->lock);
	list_add_tail(&req->req_queue, &the_madc->sw_queue);
	mutex_unlock(&the_madc->lock);
	twl4030_madc_process_queues();
	wait_for_completion_killable(&req->comp);

	/* Unnecessary in case of success, but does no harm */
	mutex_lock(&the_madc->lock);
	twl4030_madc_remove_request(req);
	mutex_unlock(&the_madc->lock);

	ret = req->status;
	kfree(req);
	return ret;
}
EXPORT_SYMBOL(twl4030_madc_conversion);

struct twl4030_madc_request *
    twl4030_madc_start_conversion(u16 channel_mask, u16 average_mask,
				  u16 *rbuf,
				  void (*func_cb)(int,
					    struct twl4030_madc_request *),
				  void *user_data)
{
	struct twl4030_madc_request *req;

	if (the_madc == NULL)
		return ERR_PTR(-ENODEV);

	if (rbuf == NULL || channel_mask == 0 || func_cb == NULL)
		return ERR_PTR(-EINVAL);

	req = kzalloc(sizeof(struct twl4030_madc_request), GFP_KERNEL);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	req->channel_mask = channel_mask;
	req->average_mask = average_mask & channel_mask;
	req->rbuf = rbuf;
	req->flags = TWL4030_MADC_TRIG_MODE_SW;
	req->func_cb = func_cb;
	req->user_data = user_data;

	mutex_lock(&the_madc->lock);
	list_add_tail(&req->req_queue, &the_madc->sw_queue);
	mutex_unlock(&the_madc->lock);
	twl4030_madc_process_queues();

	return req;
}
EXPORT_SYMBOL(twl4030_madc_start_conversion);

struct twl4030_madc_request *
    twl4030_madc_start_conversion_ex(u16 channel_mask, u16 average_mask,
				     u16 flags, u16 delay,
				     u16 *rbuf,
				     void (*func_cb)(int,
					    struct twl4030_madc_request *),
				     void *user_data)
{
	struct twl4030_madc_request *req;
	u16 polarity = flags & TWL4030_MADC_RT_TRIG_MASK;
	u16 trig_mode = flags & TWL4030_MADC_TRIG_MODE_MASK;

	if (the_madc == NULL)
		return ERR_PTR(-ENODEV);

	if (rbuf == NULL || channel_mask == 0 ||
	    func_cb == NULL || trig_mode == 0 ||
	    (flags & TWL4030_MADC_INTERNAL_FLAGS))
		return ERR_PTR(-EINVAL);

	req = kzalloc(sizeof(struct twl4030_madc_request), GFP_KERNEL);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	req->channel_mask = channel_mask;
	req->average_mask = average_mask & channel_mask;
	req->polarity = polarity;
	req->delay = twl4030_madc_round_delay(delay);
	req->flags = flags;
	req->rbuf = rbuf;
	req->func_cb = func_cb;
	req->user_data = user_data;

	if (trig_mode == TWL4030_MADC_TRIG_MODE_HYBRID)
		INIT_DELAYED_WORK(&req->hybrid_work,
				  twl4030_madc_hybrid_finish);

	mutex_lock(&the_madc->lock);
	if (trig_mode == TWL4030_MADC_TRIG_MODE_SW)
		list_add_tail(&req->req_queue, &the_madc->sw_queue);
	else
		list_add_tail(&req->req_queue, &the_madc->rt_queue);
	mutex_unlock(&the_madc->lock);
	twl4030_madc_process_queues();

	return req;
}
EXPORT_SYMBOL(twl4030_madc_start_conversion_ex);

void twl4030_madc_halt_ch0(void)
{
	mutex_lock(&the_madc->lock);
	++the_madc->ch0_unavailable;
	++the_madc->ch0_event_count;
	mutex_unlock(&the_madc->lock);
}
EXPORT_SYMBOL(twl4030_madc_halt_ch0);

void twl4030_madc_resume_ch0(void)
{
	mutex_lock(&the_madc->lock);
	--the_madc->ch0_unavailable;
	++the_madc->ch0_event_count;
	mutex_unlock(&the_madc->lock);
	twl4030_madc_process_queues();
}
EXPORT_SYMBOL(twl4030_madc_resume_ch0);

void *twl4030_madc_get_user_data(struct twl4030_madc_request *req)
{
	return req->user_data;
}
EXPORT_SYMBOL(twl4030_madc_get_user_data);

/**
 * Current generator control for certain channels in TWL4030 / TWL5030.
 */
static int twl4030_madc_set_current_generator(struct twl4030_madc_data *madc,
		int chan, int on)
{
	int ret;
	u8 regval;

	/* First, check if we have MAIN_CHARGE module */
	if (!madc->has_bci)
		return EINVAL;

	/* Current generator is only available for ADCIN0 and ADCIN1.
	 * NB: ADCIN1 current generator only works when AC or VBUS is present */
	if (chan > 1)
		return EINVAL;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_MAIN_CHARGE,
				  &regval, TWL4030_BCI_BCICTL1);
	if (on) {
		regval |= (chan) ? TWL4030_BCI_ITHEN : TWL4030_BCI_TYPEN;
		regval |= TWL4030_BCI_MESBAT;
	} else {
		regval &= (chan) ? ~TWL4030_BCI_ITHEN : ~TWL4030_BCI_TYPEN;
		regval &= ~TWL4030_BCI_MESBAT;
	}

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_MAIN_CHARGE,
				   regval, TWL4030_BCI_BCICTL1);

	return ret;
}

static int twl4030_madc_set_power(struct twl4030_madc_data *madc, int on)
{
	int ret = 0;
	/*
	 * This function is called only with the general madc mutex
	 * held, so we can update hardware and software cached values
	 * without any local locking.
	 */

	if (on) {
		madc->ctrl1_value |= TWL4030_MADC_MADCON;
		twl4030_madc_write_byte(madc, TWL4030_MADC_CTRL1,
					madc->ctrl1_value);

		/* REVISIT: this seems to make this function fail if
		 * there is no MAIN_CHARGE module */
		/* REVISIT: These should be enabled only if ch0 or ch1
		 * are to be read */
		ret = twl4030_madc_set_current_generator(madc, 0, 1);

	} else {
		/* REVISIT: this seems to make this function fail if
		 * there is no MAIN_CHARGE module */
		ret = twl4030_madc_set_current_generator(madc, 0, 0);

		madc->ctrl1_value &= ~TWL4030_MADC_MADCON;
		twl4030_madc_write_byte(madc, TWL4030_MADC_CTRL1,
					madc->ctrl1_value);
	}
	return ret;
}

static void twl4030_madc_finish_complex_filereq(int status,
					     struct twl4030_madc_request *req)
{
	struct twl4030_madc_filp_data *local_data;

	spin_lock(&the_madc->misc_lock);
	local_data = req->user_data;
	if ((local_data == NULL) || (local_data->req == NULL)) {
		/*
		 * File closed or request aborted during the measurement.
		 * The request is released in twl4030_madc_cancel_request
		 * called by ABORT IOCTL or misc device close.
		 */
		spin_unlock(&the_madc->misc_lock);
		return;
	}
	/*
	 * After this point don't event think about touching
	 * the request elsewhere than here.
	 * TWL4030_MADC_CB_RELEASES_MEM flag is allready set for this req.
	 */
	local_data->req = NULL;
	complete(&local_data->complex_complete);
	wake_up_all(&local_data->complex_wait); /* Select/poll */
	spin_unlock(&the_madc->misc_lock);
	kfree(req);
	return;
}

static long twl4030_madc_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct twl4030_madc_request *req;
	struct twl4030_madc_filp_data *local_data;
	struct twl4030_madc_user_parms sw_par;
	struct twl4030_madc_setup_parms complex_par;
	int val, ret;
	u16 *databuf;

	local_data = (struct twl4030_madc_filp_data *) filp->private_data;

	switch (cmd) {
	case TWL4030_MADC_IOCX_ADC_RAW_READ:
		ret = copy_from_user(&sw_par, (void __user *) arg,
				     sizeof(sw_par));
		if (ret) {
			dev_dbg(the_madc->dev, "copy_from_user: %d\n", ret);
			return -EACCES;
		}

		if (sw_par.channel >= TWL4030_MADC_MAX_CHANNELS ||
		    sw_par.channel < 0)
			return -EINVAL;

		val = twl4030_madc_single_conversion(sw_par.channel,
						     sw_par.average);
		if (likely(val >= 0)) {
			sw_par.status = 0;
			sw_par.result = val;
		} else {
			sw_par.status = val;
			sw_par.result = 0;
		}

		ret = copy_to_user((void __user *) arg, &sw_par,
				   sizeof(sw_par));
		if (ret) {
			dev_dbg(the_madc->dev, "copy_to_user: %d\n", ret);
			return -EACCES;
		}

		return ret;

	case TWL4030_MADC_IOCX_ADC_SETUP_CONVERSION:
		ret = copy_from_user(&complex_par, (void __user *) arg,
				     sizeof(complex_par));
		if (ret) {
			dev_dbg(the_madc->dev, "copy_from_user: %d\n", ret);
			return -EACCES;
		}

		mutex_lock(&local_data->data_lock);
		if (local_data->conversion_data) {
			mutex_unlock(&local_data->data_lock);
			return -EBUSY;
		}

		databuf = kzalloc(sizeof(u16)*TWL4030_MADC_MAX_CHANNELS,
				  GFP_KERNEL);
		if (databuf == NULL) {
			mutex_unlock(&local_data->data_lock);
			return -ENOMEM;
		}

		init_completion(&local_data->complex_complete);
		local_data->conversion_data = databuf;

		req = twl4030_madc_start_conversion_ex(
			complex_par.channel_mask,
			complex_par.average_mask,
			TWL4030_MADC_CB_RELEASES_MEM |
			(complex_par.flags & (TWL4030_MADC_RT_TRIG_MASK |
					      TWL4030_MADC_TRIG_MODE_MASK)),
			complex_par.delay,
			databuf,
			twl4030_madc_finish_complex_filereq,
			local_data);


		if (IS_ERR(req)) {
			local_data->conversion_data = NULL;
			kfree(databuf);
			ret = PTR_ERR(req);
		} else {
			local_data->req = req;
		}
		mutex_unlock(&local_data->data_lock);

		return ret;

	case TWL4030_MADC_IOCX_ADC_ABORT_CONVERSION:
		mutex_lock(&local_data->data_lock);

		spin_lock(&the_madc->misc_lock);
		req = local_data->req;
		local_data->req = NULL;
		spin_unlock(&the_madc->misc_lock);

		if (req)
			twl4030_madc_cancel_request(req);

		databuf = local_data->conversion_data;
		local_data->conversion_data = NULL;
		kfree(databuf);
		if (databuf)
			complete_all(&local_data->complex_complete);

		mutex_unlock(&local_data->data_lock);
		return 0;

	default:
		break;
	}

	return -EINVAL;
}

static ssize_t twl4030_madc_file_read(struct file *filp, char __user *buf,
				 size_t len, loff_t *ppos)
{
	struct twl4030_madc_filp_data *local_data;
	u16 *results;

	if (len > (sizeof(u16)*TWL4030_MADC_MAX_CHANNELS))
		len = sizeof(u16)*TWL4030_MADC_MAX_CHANNELS;

	local_data = (struct twl4030_madc_filp_data *) filp->private_data;

	/* Conversion requested? */
	if (!local_data->conversion_data)
		return 0;

	if (wait_for_completion_interruptible(&local_data->complex_complete) ==
	    -ERESTARTSYS)
		return -ERESTARTSYS;

	mutex_lock(&local_data->data_lock);
	results = local_data->conversion_data;
	/* Conversion aborted? */
	if (!results) {
		mutex_unlock(&local_data->data_lock);
		return 0;
	}


	if (copy_to_user(buf, results, len))
		return -EACCES;

	local_data->conversion_data = NULL;
	kfree(results);
	mutex_unlock(&local_data->data_lock);

	return len;
}

static unsigned int twl4030_madc_poll(struct file *filp,
				      struct poll_table_struct *wait)
{
	struct twl4030_madc_filp_data *local_data =
		(struct twl4030_madc_filp_data *) filp->private_data;

	poll_wait(filp, &local_data->complex_wait, wait);

	/* Conversion requested? */
	if (!local_data->conversion_data)
		return 0;

	if (completion_done(&local_data->complex_complete))
		return POLLIN | POLLRDNORM;

	return 0;
}

static int twl4030_madc_open(struct inode *inode, struct file *filp)
{
	struct twl4030_madc_filp_data *local_data;

	if (!the_madc)
		return -EIO;

	local_data = kzalloc(sizeof(struct twl4030_madc_filp_data), GFP_KERNEL);
	if (!local_data)
		return -ENOMEM;

	init_waitqueue_head(&local_data->complex_wait);
	mutex_init(&local_data->data_lock);
	filp->private_data = local_data;

	try_module_get(THIS_MODULE);

	return 0;
}

static int twl4030_madc_release(struct inode *inode, struct file *filp)
{
	struct twl4030_madc_filp_data *local_data;
	struct twl4030_madc_request *req;

	if (!filp->private_data) {
		module_put(THIS_MODULE);
		return 0;
	}

	local_data = (struct twl4030_madc_filp_data *) filp->private_data;

	if (local_data) {
		spin_lock(&the_madc->misc_lock);
		req = local_data->req;
		if (req)
			req->user_data = NULL;
		local_data->req = NULL;
		spin_unlock(&the_madc->misc_lock);

		/* now we can safely cancel the request */
		if (req)
			twl4030_madc_cancel_request(req);


		kfree(local_data->conversion_data);
	}

	kfree(filp->private_data);
	filp->private_data = NULL;

	module_put(THIS_MODULE);

	return 0;
}


static const struct file_operations twl4030_madc_fileops = {
	.owner = THIS_MODULE,
	.open = twl4030_madc_open,
	.release = twl4030_madc_release,
	.read = twl4030_madc_file_read,
	.poll = twl4030_madc_poll,
	.unlocked_ioctl = twl4030_madc_ioctl
};

static struct miscdevice twl4030_madc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "twl4030-adc",
	.fops = &twl4030_madc_fileops
};

static int __init twl4030_madc_probe(struct platform_device *pdev)
{
	struct twl4030_madc_data *madc;
	struct twl4030_madc_platform_data *pdata = pdev->dev.platform_data;
	struct device *parent_dev = pdev->dev.parent;
	int ret;
	u8 regval;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &regval,
				  TWL4030_INTBR_GPBR1);
	if (ret) {
		dev_dbg(&pdev->dev, "unable to read GPBR1 for clock control\n");
		return -ENODEV;
	}

	/* Use HFCLK and enable MADC 1Mhz clock divider */
	regval |= (1<<7);
	regval &= ~(1<<4);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, regval,
				   TWL4030_INTBR_GPBR1);
	if (ret) {
		dev_dbg(&pdev->dev,
				"unable to write GPBR1 for clock control\n");
		return -ENODEV;
	}

	madc = kzalloc(sizeof *madc, GFP_KERNEL);
	if (!madc)
		return -ENOMEM;
	madc->dev = &pdev->dev;

	spin_lock_init(&madc->misc_lock);
	mutex_init(&madc->lock);
	mutex_init(&madc->imr_lock);
	mutex_init(&madc->isr_bits_lock);
	madc->hybrid_work = create_singlethread_workqueue("TWL4030-madc-h");
	INIT_LIST_HEAD(&madc->sw_queue);
	INIT_LIST_HEAD(&madc->rt_queue);
	madc->check_busy = ~(1<<TWL4030_MADC_MODE_RT);

	/* Current polarity is unknown, force update on first request */
	madc->current_polarity = ~TWL4030_MADC_RT_TRIG_MASK;

	the_madc = madc;

	if (!pdata) {
		dev_dbg(&pdev->dev, "platform_data not available\n");
		ret = -EINVAL;
		goto err_pdata;
	}

	madc->imr_addr = (pdata->irq_line == 1) ? TWL4030_MADC_IMR1 :
						TWL4030_MADC_IMR2;
	madc->isr_addr = (pdata->irq_line == 1) ? TWL4030_MADC_ISR1 :
						TWL4030_MADC_ISR2;

	ret = misc_register(&twl4030_madc_device);
	if (ret) {
		dev_dbg(&pdev->dev, "could not register misc_device\n");
		goto err_misc;
	}

	twl4030_madc_set_power(madc, 1);

	/*
	 * If the chip has MAIN_CHARGE module (TWL5030 and older), enable the
	 * current generator and VBAT prescaler via BCICTL1 register.
	 */
	if (parent_dev->platform_data &&
	    ((struct twl4030_platform_data *)parent_dev->platform_data)->bci)
		madc->has_bci = 1;

	if (pdata->bcia_control != 0) {
		/*
		 * Enable MESBAT_EN, MESVAC_EN and  BTEMP_EN in BCIA_CTRL if
		 * corresponding bits are set in bcia_control. The value is
		 * zeroed by parent device if we do not have a TWL5031.
		 */
		ret = twl4030_i2c_read_u8(TWL5031_MODULE_ACCESSORY,
					  &regval, TWL5031_BCIA_CTRL);

		regval |= pdata->bcia_control;
		ret = twl4030_i2c_write_u8(TWL5031_MODULE_ACCESSORY,
					   regval, TWL5031_BCIA_CTRL);
	}

	madc->current_imr = 0xff;
	ret = twl4030_madc_write_byte(madc, madc->imr_addr, 0xff);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "could not mask interrupts\n");
		goto err_irq;
	}

	ret = request_threaded_irq(platform_get_irq(pdev, 0),
				   twl4030_madc_irq_handler,
				   twl4030_madc_irq_thread,
				   0, "twl4030_madc", madc);
	if (ret) {
		dev_dbg(&pdev->dev, "could not request irq\n");
		goto err_irq;
	}

	platform_set_drvdata(pdev, madc);

	return 0;

err_irq:
	misc_deregister(&twl4030_madc_device);

err_misc:
err_pdata:
	the_madc = NULL;
	kfree(madc);

	return ret;
}

static int __exit twl4030_madc_remove(struct platform_device *pdev)
{
	struct twl4030_madc_data *madc = platform_get_drvdata(pdev);
	int ret;

	flush_workqueue(madc->hybrid_work);
	destroy_workqueue(madc->hybrid_work);
	ret = twl4030_madc_write_byte(madc, madc->imr_addr, 0xff);
	if (ret < 0)
		return ret;
	free_irq(platform_get_irq(pdev, 0), madc);
	misc_deregister(&twl4030_madc_device);

	return 0;
}

static struct platform_driver twl4030_madc_driver = {
	.probe		= twl4030_madc_probe,
	.remove		= __exit_p(twl4030_madc_remove),
	.driver		= {
		.name	= "twl4030_madc",
		.owner	= THIS_MODULE,
	},
};

static int __init twl4030_madc_init(void)
{
	return platform_driver_register(&twl4030_madc_driver);
}
module_init(twl4030_madc_init);

static void __exit twl4030_madc_exit(void)
{
	platform_driver_unregister(&twl4030_madc_driver);
}
module_exit(twl4030_madc_exit);

MODULE_ALIAS("platform:twl4030-madc");
MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("twl4030 ADC driver");
MODULE_LICENSE("GPL");
