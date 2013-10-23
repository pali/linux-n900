/*
 * hsi-char.c
 *
 * HSI character device driver, implements the character device
 * interface.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <asm/atomic.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/scatterlist.h>
#include <linux/hsi/hsi.h>
#include <linux/hsi/hsi_char.h>

#define HSI_CHAR_CHANNELS	8
#define HSI_CHAR_DEVS		8
#define HSI_CHAR_MSGS		4

#define HSI_CHST_UNAVAIL	0 /* SBZ! */
#define HSI_CHST_AVAIL		1

#define HSI_CHST_CLOSED		(0 << 4)
#define HSI_CHST_CLOSING	(1 << 4)
#define HSI_CHST_OPENING	(2 << 4)
#define HSI_CHST_OPENED		(3 << 4)

#define HSI_CHST_READOFF	(0 << 8)
#define HSI_CHST_READON		(1 << 8)
#define HSI_CHST_READING	(2 << 8)

#define HSI_CHST_WRITEOFF	(0 << 12)
#define HSI_CHST_WRITEON	(1 << 12)
#define HSI_CHST_WRITING	(2 << 12)

#define HSI_CHST_OC_MASK	0xf0
#define HSI_CHST_RD_MASK	0xf00
#define HSI_CHST_WR_MASK	0xf000

#define HSI_CHST_OC(c)		((c)->state & HSI_CHST_OC_MASK)
#define HSI_CHST_RD(c)		((c)->state & HSI_CHST_RD_MASK)
#define HSI_CHST_WR(c)		((c)->state & HSI_CHST_WR_MASK)

#define HSI_CHST_OC_SET(c, v) \
	do { \
		(c)->state &= ~HSI_CHST_OC_MASK; \
		(c)->state |= v; \
	} while (0);

#define HSI_CHST_RD_SET(c, v) \
	do { \
		(c)->state &= ~HSI_CHST_RD_MASK; \
		(c)->state |= v; \
	} while (0);

#define HSI_CHST_WR_SET(c, v) \
	do { \
		(c)->state &= ~HSI_CHST_WR_MASK; \
		(c)->state |= v; \
	} while (0);

#define HSI_CHAR_POLL_RST	(-1)
#define HSI_CHAR_POLL_OFF	0
#define HSI_CHAR_POLL_ON	1

#define HSI_CHAR_RX		0
#define HSI_CHAR_TX		1

struct hsi_char_channel {
	unsigned int		ch;
	unsigned int		state;
	int			wlrefcnt;
	int			rxpoll;
	struct hsi_client	*cl;
	struct list_head	free_msgs_list;
	struct list_head	rx_msgs_queue;
	struct list_head	tx_msgs_queue;
	int			poll_event;
	spinlock_t		lock;
	struct fasync_struct	*async_queue;
	wait_queue_head_t	rx_wait;
	wait_queue_head_t	tx_wait;
};

struct hsi_char_client_data {
	atomic_t		refcnt;
	int			attached;
	atomic_t		breq;
	struct hsi_char_channel	channels[HSI_CHAR_DEVS];
};

static unsigned int max_data_size = 0x1000;
module_param(max_data_size, uint, 0600);
MODULE_PARM_DESC(max_data_size, "max read/write data size [4,8..65536] (^2)");

static int channels_map[HSI_CHAR_DEVS] = {0, -1, -1 , -1, -1, -1, -1, -1};
module_param_array(channels_map, int, NULL, 0);
MODULE_PARM_DESC(channels_map, "Array of HSI channels ([0...7]) to be probed");

static dev_t hsi_char_dev;
static struct hsi_char_client_data hsi_char_cl_data;

static inline void hsi_char_msg_free(struct hsi_msg *msg)
{
	msg->complete = NULL;
	msg->destructor = NULL;
	kfree(sg_virt(msg->sgt.sgl));
	hsi_free_msg(msg);
}

static inline void hsi_char_msgs_free(struct hsi_char_channel *channel)
{
	struct hsi_msg *msg, *tmp;

	list_for_each_entry_safe(msg, tmp, &channel->free_msgs_list, link) {
		list_del(&msg->link);
		hsi_char_msg_free(msg);
	}
	list_for_each_entry_safe(msg, tmp, &channel->rx_msgs_queue, link) {
		list_del(&msg->link);
		hsi_char_msg_free(msg);
	}
	list_for_each_entry_safe(msg, tmp, &channel->tx_msgs_queue, link) {
		list_del(&msg->link);
		hsi_char_msg_free(msg);
	}
}

static inline struct hsi_msg *hsi_char_msg_alloc(unsigned int alloc_size)
{
	struct hsi_msg *msg;
	void *buf;

	msg = hsi_alloc_msg(1, GFP_KERNEL);
	if (!msg)
		goto out;
	buf = kmalloc(alloc_size, GFP_KERNEL);
	if (!buf) {
		hsi_free_msg(msg);
		goto out;
	}
	sg_init_one(msg->sgt.sgl, buf, alloc_size);
	msg->context = buf;
	return msg;
out:
	return NULL;
}

static inline int hsi_char_msgs_alloc(struct hsi_char_channel *channel)
{
	struct hsi_msg *msg;
	int i;

	for (i = 0; i < HSI_CHAR_MSGS; i++) {
		msg = hsi_char_msg_alloc(max_data_size);
		if (!msg)
			goto out;
		msg->channel = channel->ch;
		list_add_tail(&msg->link, &channel->free_msgs_list);
	}
	return 0;
out:
	hsi_char_msgs_free(channel);

	return -ENOMEM;
}

static int _hsi_char_release(struct hsi_char_channel *channel, int remove)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(channel->cl);
	int ret = 0, refcnt;

	spin_lock_bh(&channel->lock);
	if (HSI_CHST_OC(channel) != HSI_CHST_OPENED)
		goto out;
	HSI_CHST_OC_SET(channel, HSI_CHST_CLOSING);
	spin_unlock_bh(&channel->lock);

	while (channel->wlrefcnt > 0) {
		hsi_stop_tx(channel->cl);
		channel->wlrefcnt--;
	}

	if (channel->rxpoll == HSI_CHAR_POLL_ON)
		channel->poll_event |= POLLERR;

	wake_up_interruptible(&channel->rx_wait);
	wake_up_interruptible(&channel->tx_wait);

	refcnt = atomic_dec_return(&cl_data->refcnt);
	if (!refcnt) {
		hsi_flush(channel->cl);
		hsi_release_port(channel->cl);
		cl_data->attached = 0;
	}
	hsi_char_msgs_free(channel);

	spin_lock_bh(&channel->lock);
	HSI_CHST_OC_SET(channel, HSI_CHST_CLOSED);
	HSI_CHST_RD_SET(channel, HSI_CHST_READOFF);
	HSI_CHST_WR_SET(channel, HSI_CHST_WRITEOFF);
out:
	if (remove)
		channel->cl = NULL;
	spin_unlock_bh(&channel->lock);

	return ret;
}

static int __devinit hsi_char_probe(struct device *dev)
{
	struct hsi_char_client_data *cl_data = &hsi_char_cl_data;
	struct hsi_char_channel *channel = cl_data->channels;
	struct hsi_client *cl = to_hsi_client(dev);
	int i;

	for (i = 0; i < HSI_CHAR_DEVS; i++, channel++) {
		if (channel->state == HSI_CHST_AVAIL)
			channel->cl = cl;
	}
	cl->hsi_start_rx = NULL;
	cl->hsi_stop_rx = NULL;
	atomic_set(&cl_data->refcnt, 0);
	atomic_set(&cl_data->breq, 1);
	cl_data->attached = 0;
	hsi_client_set_drvdata(cl, cl_data);

	return 0;
}

static int __devexit hsi_char_remove(struct device *dev)
{
	struct hsi_client *cl = to_hsi_client(dev);
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(cl);
	struct hsi_char_channel *channel = cl_data->channels;
	int i;

	for (i = 0; i < HSI_CHAR_DEVS; i++, channel++) {
		if (!(channel->state & HSI_CHST_AVAIL))
			continue;
		_hsi_char_release(channel, 1);
	}

	return 0;
}

static inline unsigned int hsi_char_msg_len_get(struct hsi_msg *msg)
{
	return msg->sgt.sgl->length;
}

static inline void hsi_char_msg_len_set(struct hsi_msg *msg, unsigned int len)
{
	msg->sgt.sgl->length = len;
}

static void hsi_char_data_available(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsi_char_channel *channel = cl_data->channels + msg->channel;
	int ret;

	if (msg->status == HSI_STATUS_ERROR) {
		ret = hsi_async_read(channel->cl, msg);
		if (ret < 0) {
			list_add_tail(&msg->link, &channel->free_msgs_list);
			spin_lock_bh(&channel->lock);
			list_add_tail(&msg->link, &channel->free_msgs_list);
			channel->rxpoll = HSI_CHAR_POLL_OFF;
			spin_unlock_bh(&channel->lock);
		}
	} else {
		spin_lock_bh(&channel->lock);
		channel->rxpoll = HSI_CHAR_POLL_OFF;
		channel->poll_event |= (POLLIN | POLLRDNORM);
		spin_unlock_bh(&channel->lock);
		spin_lock_bh(&channel->lock);
		list_add_tail(&msg->link, &channel->free_msgs_list);
		spin_unlock_bh(&channel->lock);
		wake_up_interruptible(&channel->rx_wait);
	}
}

static void hsi_char_rx_completed(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsi_char_channel *channel = cl_data->channels + msg->channel;

	spin_lock_bh(&channel->lock);
	list_add_tail(&msg->link, &channel->rx_msgs_queue);
	spin_unlock_bh(&channel->lock);
	wake_up_interruptible(&channel->rx_wait);
}

static void hsi_char_rx_msg_destructor(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsi_char_channel *channel = cl_data->channels + msg->channel;

	spin_lock_bh(&channel->lock);
	list_add_tail(&msg->link, &channel->free_msgs_list);
	HSI_CHST_RD_SET(channel, HSI_CHST_READOFF);
	spin_unlock_bh(&channel->lock);
}

static void hsi_char_rx_poll_destructor(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsi_char_channel *channel = cl_data->channels + msg->channel;

	spin_lock_bh(&channel->lock);
	list_add_tail(&msg->link, &channel->free_msgs_list);
	channel->rxpoll = HSI_CHAR_POLL_RST;
	spin_unlock_bh(&channel->lock);
}

static int hsi_char_rx_poll(struct hsi_char_channel *channel)
{
	struct hsi_msg *msg;
	int ret = 0;

	spin_lock_bh(&channel->lock);
	if (list_empty(&channel->free_msgs_list)) {
		ret = -ENOMEM;
		goto out;
	}
	if (channel->rxpoll == HSI_CHAR_POLL_ON)
		goto out;
	msg = list_first_entry(&channel->free_msgs_list, struct hsi_msg, link);
	list_del(&msg->link);
	channel->rxpoll = HSI_CHAR_POLL_ON;
	spin_unlock_bh(&channel->lock);
	hsi_char_msg_len_set(msg, 0);
	msg->complete = hsi_char_data_available;
	msg->destructor = hsi_char_rx_poll_destructor;
	/* don't touch msg->context! */
	ret = hsi_async_read(channel->cl, msg);
	spin_lock_bh(&channel->lock);
	if (ret < 0) {
		list_add_tail(&msg->link, &channel->free_msgs_list);
		channel->rxpoll = HSI_CHAR_POLL_OFF;
		goto out;
	}
out:
	spin_unlock_bh(&channel->lock);

	return ret;
}

static void hsi_char_tx_completed(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsi_char_channel *channel = cl_data->channels + msg->channel;

	spin_lock_bh(&channel->lock);
	list_add_tail(&msg->link, &channel->tx_msgs_queue);
	channel->poll_event |= (POLLOUT | POLLWRNORM);
	spin_unlock_bh(&channel->lock);
	wake_up_interruptible(&channel->tx_wait);
}

static void hsi_char_tx_msg_destructor(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsi_char_channel *channel = cl_data->channels + msg->channel;

	spin_lock_bh(&channel->lock);
	list_add_tail(&msg->link, &channel->free_msgs_list);
	HSI_CHST_WR_SET(channel, HSI_CHST_WRITEOFF);
	spin_unlock_bh(&channel->lock);
}

static void hsi_char_rx_poll_rst(struct hsi_client *cl)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(cl);
	struct hsi_char_channel *channel = cl_data->channels;
	int i;

	for (i = 0; i < HSI_CHAR_DEVS; i++, channel++) {
		if ((HSI_CHST_OC(channel) == HSI_CHST_OPENED) &&
			(channel->rxpoll == HSI_CHAR_POLL_RST))
			hsi_char_rx_poll(channel);
	}
}

static void hsi_char_reset(struct hsi_client *cl)
{
	hsi_flush(cl);
	hsi_char_rx_poll_rst(cl);
}

static void hsi_char_rx_cancel(struct hsi_char_channel *channel)
{
	hsi_flush(channel->cl);
	hsi_char_rx_poll_rst(channel->cl);
}

static void hsi_char_tx_cancel(struct hsi_char_channel *channel)
{
	hsi_flush(channel->cl);
	hsi_char_rx_poll_rst(channel->cl);
}

static void hsi_char_bcast_break(struct hsi_client *cl)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(cl);
	struct hsi_char_channel *channel = cl_data->channels;
	int i;

	for (i = 0; i < HSI_CHAR_DEVS; i++, channel++) {
		if (HSI_CHST_OC(channel) != HSI_CHST_OPENED)
			continue;
		channel->poll_event |= POLLPRI;
		wake_up_interruptible(&channel->rx_wait);
		wake_up_interruptible(&channel->tx_wait);
	}
}

static void hsi_char_break_received(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);
	int ret;

	hsi_char_bcast_break(msg->cl);
	ret = hsi_async_read(msg->cl, msg);
	if (ret < 0) {
		hsi_free_msg(msg);
		atomic_inc(&cl_data->breq);
	}
}

static void hsi_char_break_req_destructor(struct hsi_msg *msg)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(msg->cl);

	hsi_free_msg(msg);
	atomic_inc(&cl_data->breq);
}

static int hsi_char_break_request(struct hsi_client *cl)
{
	struct hsi_char_client_data *cl_data = hsi_client_drvdata(cl);
	struct hsi_msg *msg;
	int ret = 0;

	if (!atomic_dec_and_test(&cl_data->breq)) {
		atomic_inc(&cl_data->breq);
		return -EBUSY;
	}
	msg = hsi_alloc_msg(0, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->break_frame = 1;
	msg->complete = hsi_char_break_received;
	msg->destructor = hsi_char_break_req_destructor;
	ret = hsi_async_read(cl, msg);
	if (ret < 0)
		hsi_free_msg(msg);

	return ret;
}

static int hsi_char_break_send(struct hsi_client *cl)
{
	struct hsi_msg *msg;
	int ret = 0;

	msg = hsi_alloc_msg(0, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;
	msg->break_frame = 1;
	msg->complete = hsi_free_msg;
	msg->destructor = hsi_free_msg;
	ret = hsi_async_write(cl, msg);
	if (ret < 0)
		hsi_free_msg(msg);

	return ret;
}

static inline int ssi_check_common_cfg(struct hsi_config *cfg)
{
	if ((cfg->mode != HSI_MODE_STREAM) && (cfg->mode != HSI_MODE_FRAME))
		return -EINVAL;
	if ((cfg->channels == 0) || (cfg->channels > HSI_CHAR_CHANNELS))
		return -EINVAL;
	if (cfg->channels & (cfg->channels - 1))
		return -EINVAL;
	if ((cfg->flow != HSI_FLOW_SYNC) && (cfg->flow != HSI_FLOW_PIPE))
		return -EINVAL;

	return 0;
}

static inline int ssi_check_rx_cfg(struct hsi_config *cfg)
{
	return ssi_check_common_cfg(cfg);
}

static inline int ssi_check_tx_cfg(struct hsi_config *cfg)
{
	int ret = ssi_check_common_cfg(cfg);

	if (ret < 0)
		return ret;
	if ((cfg->arb_mode != HSI_ARB_RR) && (cfg->arb_mode != HSI_ARB_PRIO))
		return -EINVAL;

	return 0;
}

static inline int hsi_char_cfg_set(struct hsi_client *cl,
						struct hsi_config *cfg, int dir)
{
	struct hsi_config *rxtx_cfg;
	int ret = 0;

	if (dir == HSI_CHAR_RX) {
		rxtx_cfg = &cl->rx_cfg;
		ret = ssi_check_rx_cfg(cfg);
	} else {
		rxtx_cfg = &cl->tx_cfg;
		ret = ssi_check_tx_cfg(cfg);
	}
	if (ret < 0)
		return ret;

	*rxtx_cfg = *cfg;
	ret = hsi_setup(cl);
	if (ret < 0)
		return ret;

	if ((dir == HSI_CHAR_RX) && (cfg->mode == HSI_MODE_FRAME))
		hsi_char_break_request(cl);

	return ret;
}

static inline void hsi_char_cfg_get(struct hsi_client *cl,
						struct hsi_config *cfg, int dir)
{
	struct hsi_config *rxtx_cfg;

	if (dir == HSI_CHAR_RX)
		rxtx_cfg = &cl->rx_cfg;
	else
		rxtx_cfg = &cl->tx_cfg;
	*cfg = *rxtx_cfg;
}

static inline void hsi_char_rx2icfg(struct hsi_config *cfg,
						struct hsc_rx_config *rx_cfg)
{
	cfg->mode = rx_cfg->mode;
	cfg->flow = rx_cfg->flow;
	cfg->channels = rx_cfg->channels;
	cfg->speed = 0;
	cfg->arb_mode = 0;
}

static inline void hsi_char_tx2icfg(struct hsi_config *cfg,
						struct hsc_tx_config *tx_cfg)
{
	cfg->mode = tx_cfg->mode;
	cfg->flow = tx_cfg->flow;
	cfg->channels = tx_cfg->channels;
	cfg->speed = tx_cfg->speed;
	cfg->arb_mode = tx_cfg->arb_mode;
}

static inline void hsi_char_rx2ecfg(struct hsc_rx_config *rx_cfg,
							struct hsi_config *cfg)
{
	rx_cfg->mode = cfg->mode;
	rx_cfg->flow = cfg->flow;
	rx_cfg->channels = cfg->channels;
}

static inline void hsi_char_tx2ecfg(struct hsc_tx_config *tx_cfg,
							struct hsi_config *cfg)
{
	tx_cfg->mode = cfg->mode;
	tx_cfg->flow = cfg->flow;
	tx_cfg->channels = cfg->channels;
	tx_cfg->speed = cfg->speed;
	tx_cfg->arb_mode = cfg->arb_mode;
}

static ssize_t hsi_char_read(struct file *file, char __user *buf,
						size_t len, loff_t *ppos)
{
	struct hsi_char_channel *channel = file->private_data;
	struct hsi_msg *msg = NULL;
	ssize_t ret;

	if (len == 0) {
		channel->poll_event &= ~POLLPRI;
		return 0;
	}
	channel->poll_event &= ~POLLPRI;

	if (!IS_ALIGNED(len, sizeof(u32)))
		return -EINVAL;

	if (len > max_data_size)
		len = max_data_size;

	spin_lock_bh(&channel->lock);
	if (HSI_CHST_OC(channel) != HSI_CHST_OPENED) {
		ret = -ENODEV;
		goto out;
	}
	if (HSI_CHST_RD(channel) != HSI_CHST_READOFF) {
		ret = -EBUSY;
		goto out;
	}
	if (channel->ch >= channel->cl->rx_cfg.channels) {
		ret = -ENODEV;
		goto out;
	}
	if (list_empty(&channel->free_msgs_list)) {
		ret = -ENOMEM;
		goto out;
	}
	msg = list_first_entry(&channel->free_msgs_list, struct hsi_msg, link);
	list_del(&msg->link);
	spin_unlock_bh(&channel->lock);
	hsi_char_msg_len_set(msg, len);
	msg->complete = hsi_char_rx_completed;
	msg->destructor = hsi_char_rx_msg_destructor;
	ret = hsi_async_read(channel->cl, msg);
	spin_lock_bh(&channel->lock);
	if (ret < 0)
		goto out;
	HSI_CHST_RD_SET(channel, HSI_CHST_READING);
	msg = NULL;

	for ( ; ; ) {
		DEFINE_WAIT(wait);

		if (!list_empty(&channel->rx_msgs_queue)) {
			msg = list_first_entry(&channel->rx_msgs_queue,
					struct hsi_msg, link);
			HSI_CHST_RD_SET(channel, HSI_CHST_READOFF);
			channel->poll_event &= ~(POLLIN | POLLRDNORM);
			list_del(&msg->link);
			spin_unlock_bh(&channel->lock);
			if (msg->status == HSI_STATUS_ERROR) {
				ret = -EIO;
			} else {
				ret = copy_to_user((void __user *)buf,
						msg->context,
						hsi_char_msg_len_get(msg));
				if (ret)
					ret = -EFAULT;
				else
					ret = hsi_char_msg_len_get(msg);
			}
			spin_lock_bh(&channel->lock);
			break;
		} else if (signal_pending(current)) {
			spin_unlock_bh(&channel->lock);
			hsi_char_rx_cancel(channel);
			spin_lock_bh(&channel->lock);
			HSI_CHST_RD_SET(channel, HSI_CHST_READOFF);
			ret = -EINTR;
			break;
		} else if ((HSI_CHST_OC(channel) == HSI_CHST_CLOSING) ||
				(HSI_CHST_OC(channel) == HSI_CHST_CLOSING)) {
			ret = -EIO;
			break;
		}
		prepare_to_wait(&channel->rx_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_bh(&channel->lock);

		schedule();

		spin_lock_bh(&channel->lock);
		finish_wait(&channel->rx_wait, &wait);
	}
out:
	if (msg)
		list_add_tail(&msg->link, &channel->free_msgs_list);
	spin_unlock_bh(&channel->lock);

	return ret;
}

static ssize_t hsi_char_write(struct file *file, const char __user *buf,
						size_t len, loff_t *ppos)
{
	struct hsi_char_channel *channel = file->private_data;
	struct hsi_msg *msg = NULL;
	ssize_t ret;

	if ((len == 0) || !IS_ALIGNED(len, sizeof(u32)))
		return -EINVAL;

	if (len > max_data_size)
		len = max_data_size;

	spin_lock_bh(&channel->lock);
	if (HSI_CHST_OC(channel) != HSI_CHST_OPENED) {
		ret = -ENODEV;
		goto out;
	}
	if (HSI_CHST_WR(channel) != HSI_CHST_WRITEOFF) {
		ret = -EBUSY;
		goto out;
	}
	if (channel->ch >= channel->cl->tx_cfg.channels) {
		ret = -ENODEV;
		goto out;
	}
	if (list_empty(&channel->free_msgs_list)) {
		ret = -ENOMEM;
		goto out;
	}
	msg = list_first_entry(&channel->free_msgs_list, struct hsi_msg, link);
	list_del(&msg->link);
	HSI_CHST_WR_SET(channel, HSI_CHST_WRITEON);
	spin_unlock_bh(&channel->lock);

	if (copy_from_user(msg->context, (void __user *)buf, len)) {
		spin_lock_bh(&channel->lock);
		HSI_CHST_WR_SET(channel, HSI_CHST_WRITEOFF);
		ret = -EFAULT;
		goto out;
	}

	hsi_char_msg_len_set(msg, len);
	msg->complete = hsi_char_tx_completed;
	msg->destructor = hsi_char_tx_msg_destructor;
	channel->poll_event &= ~(POLLOUT | POLLWRNORM);
	ret = hsi_async_write(channel->cl, msg);
	spin_lock_bh(&channel->lock);
	if (ret < 0) {
		channel->poll_event |= (POLLOUT | POLLWRNORM);
		HSI_CHST_WR_SET(channel, HSI_CHST_WRITEOFF);
		goto out;
	}
	HSI_CHST_WR_SET(channel, HSI_CHST_WRITING);
	msg = NULL;

	for ( ; ; ) {
		DEFINE_WAIT(wait);

		if (!list_empty(&channel->tx_msgs_queue)) {
			msg = list_first_entry(&channel->tx_msgs_queue,
					struct hsi_msg, link);
			list_del(&msg->link);
			HSI_CHST_WR_SET(channel, HSI_CHST_WRITEOFF);
			if (msg->status == HSI_STATUS_ERROR)
				ret = -EIO;
			else
				ret = hsi_char_msg_len_get(msg);
			break;
		} else if (signal_pending(current)) {
			spin_unlock_bh(&channel->lock);
			hsi_char_tx_cancel(channel);
			spin_lock_bh(&channel->lock);
			HSI_CHST_WR_SET(channel, HSI_CHST_WRITEOFF);
			ret = -EINTR;
			break;
		} else if ((HSI_CHST_OC(channel) == HSI_CHST_CLOSING) ||
				(HSI_CHST_OC(channel) == HSI_CHST_CLOSING)) {
			ret = -EIO;
			break;
		}
		prepare_to_wait(&channel->tx_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_bh(&channel->lock);

		schedule();

		spin_lock_bh(&channel->lock);
		finish_wait(&channel->tx_wait, &wait);
	}
out:
	if (msg)
		list_add_tail(&msg->link, &channel->free_msgs_list);

	spin_unlock_bh(&channel->lock);

	return ret;
}

static unsigned int hsi_char_poll(struct file *file, poll_table *wait)
{
	struct hsi_char_channel *channel = file->private_data;
	unsigned int ret;

	spin_lock_bh(&channel->lock);
	if ((HSI_CHST_OC(channel) != HSI_CHST_OPENED) ||
		(channel->ch >= channel->cl->rx_cfg.channels)) {
		spin_unlock_bh(&channel->lock);
		return -ENODEV;
	}
	poll_wait(file, &channel->rx_wait, wait);
	poll_wait(file, &channel->tx_wait, wait);
	ret = channel->poll_event;
	spin_unlock_bh(&channel->lock);
	hsi_char_rx_poll(channel);

	return ret;
}

static int hsi_char_ioctl(struct inode *inode, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct hsi_char_channel *channel = file->private_data;
	unsigned int state;
	struct hsi_config cfg;
	struct hsc_rx_config rx_cfg;
	struct hsc_tx_config tx_cfg;
	int ret = 0;

	if (HSI_CHST_OC(channel) != HSI_CHST_OPENED)
		return -ENODEV;

	switch (cmd) {
	case HSC_RESET:
		hsi_char_reset(channel->cl);
		break;
	case HSC_SET_PM:
		if (copy_from_user(&state, (void __user *)arg, sizeof(state)))
			return -EFAULT;
		if (state == HSC_PM_DISABLE) {
			ret = hsi_start_tx(channel->cl);
			if (!ret)
				channel->wlrefcnt++;
		} else if ((state == HSC_PM_ENABLE)
				&& (channel->wlrefcnt > 0)) {
			ret = hsi_stop_tx(channel->cl);
			if (!ret)
				channel->wlrefcnt--;
		} else {
			ret = -EINVAL;
		}
		break;
	case HSC_SEND_BREAK:
		return hsi_char_break_send(channel->cl);
	case HSC_SET_RX:
		if (copy_from_user(&rx_cfg, (void __user *)arg, sizeof(rx_cfg)))
			return -EFAULT;
		hsi_char_rx2icfg(&cfg, &rx_cfg);
		return hsi_char_cfg_set(channel->cl, &cfg, HSI_CHAR_RX);
	case HSC_GET_RX:
		hsi_char_cfg_get(channel->cl, &cfg, HSI_CHAR_RX);
		hsi_char_rx2ecfg(&rx_cfg, &cfg);
		if (copy_to_user((void __user *)arg, &rx_cfg, sizeof(rx_cfg)))
			return -EFAULT;
		break;
	case HSC_SET_TX:
		if (copy_from_user(&tx_cfg, (void __user *)arg, sizeof(tx_cfg)))
			return -EFAULT;
		hsi_char_tx2icfg(&cfg, &tx_cfg);
		return hsi_char_cfg_set(channel->cl, &cfg, HSI_CHAR_TX);
	case HSC_GET_TX:
		hsi_char_cfg_get(channel->cl, &cfg, HSI_CHAR_TX);
		hsi_char_tx2ecfg(&tx_cfg, &cfg);
		if (copy_to_user((void __user *)arg, &tx_cfg, sizeof(tx_cfg)))
			return -EFAULT;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static int hsi_char_open(struct inode *inode, struct file *file)
{
	struct hsi_char_client_data *cl_data = &hsi_char_cl_data;
	struct hsi_char_channel *channel = cl_data->channels + iminor(inode);
	int ret = 0, refcnt;

	spin_lock_bh(&channel->lock);
	if ((channel->state == HSI_CHST_UNAVAIL) || (!channel->cl)) {
		ret = -ENODEV;
		goto out;
	}
	if (HSI_CHST_OC(channel) != HSI_CHST_CLOSED) {
		ret = -EBUSY;
		goto out;
	}
	HSI_CHST_OC_SET(channel, HSI_CHST_OPENING);
	spin_unlock_bh(&channel->lock);

	refcnt = atomic_inc_return(&cl_data->refcnt);
	if (refcnt == 1) {
		if (cl_data->attached) {
			atomic_dec(&cl_data->refcnt);
			spin_lock_bh(&channel->lock);
			HSI_CHST_OC_SET(channel, HSI_CHST_CLOSED);
			ret = -EBUSY;
			goto out;
		}
		ret = hsi_claim_port(channel->cl, 0);
		if (ret < 0) {
			atomic_dec(&cl_data->refcnt);
			spin_lock_bh(&channel->lock);
			HSI_CHST_OC_SET(channel, HSI_CHST_CLOSED);
			goto out;
		}
		hsi_setup(channel->cl);
	} else if (!cl_data->attached) {
		atomic_dec(&cl_data->refcnt);
		spin_lock_bh(&channel->lock);
		HSI_CHST_OC_SET(channel, HSI_CHST_CLOSED);
		ret = -ENODEV;
		goto out;
	}
	ret = hsi_char_msgs_alloc(channel);

	if (ret < 0) {
		refcnt = atomic_dec_return(&cl_data->refcnt);
		if (!refcnt)
			hsi_release_port(channel->cl);
		spin_lock_bh(&channel->lock);
		HSI_CHST_OC_SET(channel, HSI_CHST_CLOSED);
		goto out;
	}
	if (refcnt == 1)
		cl_data->attached = 1;
	channel->wlrefcnt = 0;
	channel->rxpoll = HSI_CHAR_POLL_OFF;
	channel->poll_event = (POLLOUT | POLLWRNORM);
	file->private_data = channel;
	spin_lock_bh(&channel->lock);
	HSI_CHST_OC_SET(channel, HSI_CHST_OPENED);
out:
	spin_unlock_bh(&channel->lock);

	return ret;
}

static int hsi_char_release(struct inode *inode, struct file *file)
{
	struct hsi_char_channel *channel = file->private_data;
	return _hsi_char_release(channel, 0);
}

static int hsi_char_fasync(int fd, struct file *file, int on)
{
	struct hsi_char_channel *channel = file->private_data;

	if (fasync_helper(fd, file, on, &channel->async_queue) < 0)
		return -EIO;

	return 0;
}

static const struct file_operations hsi_char_fops = {
	.owner		= THIS_MODULE,
	.read		= hsi_char_read,
	.write		= hsi_char_write,
	.poll		= hsi_char_poll,
	.ioctl		= hsi_char_ioctl,
	.open		= hsi_char_open,
	.release	= hsi_char_release,
	.fasync		= hsi_char_fasync,
};

static struct hsi_client_driver hsi_char_driver = {
	.driver = {
		.name	= "hsi_char",
		.owner	= THIS_MODULE,
		.probe	= hsi_char_probe,
		.remove	= hsi_char_remove,
	},
};

static inline void hsi_char_channel_init(struct hsi_char_channel *channel)
{
	channel->state = HSI_CHST_AVAIL;
	INIT_LIST_HEAD(&channel->free_msgs_list);
	init_waitqueue_head(&channel->rx_wait);
	init_waitqueue_head(&channel->tx_wait);
	spin_lock_init(&channel->lock);
	INIT_LIST_HEAD(&channel->rx_msgs_queue);
	INIT_LIST_HEAD(&channel->tx_msgs_queue);
}

static struct cdev hsi_char_cdev;

static int __init hsi_char_init(void)
{
	char devname[] = "hsi_char";
	struct hsi_char_client_data *cl_data = &hsi_char_cl_data;
	struct hsi_char_channel *channel = cl_data->channels;
	unsigned long ch_mask = 0;
	unsigned int i;
	int ret;

	if ((max_data_size < 4) || (max_data_size > 0x10000) ||
		(max_data_size & (max_data_size - 1))) {
		pr_err("Invalid max read/write data size");
		return -EINVAL;
	}

	for (i = 0; i < HSI_CHAR_DEVS && channels_map[i] >= 0; i++) {
		if (channels_map[i] >= HSI_CHAR_DEVS) {
			pr_err("Invalid HSI/SSI channel specified");
			return -EINVAL;
		}
		set_bit(channels_map[i], &ch_mask);
	}

	if (i == 0) {
		pr_err("No HSI channels available");
		return -EINVAL;
	}

	memset(cl_data->channels, 0, sizeof(cl_data->channels));
	for (i = 0; i < HSI_CHAR_DEVS; i++, channel++) {
		channel->ch = i;
		channel->state = HSI_CHST_UNAVAIL;
		if (test_bit(i, &ch_mask))
			hsi_char_channel_init(channel);
	}

	ret = hsi_register_client_driver(&hsi_char_driver);
	if (ret) {
		pr_err("Error while registering HSI/SSI driver %d", ret);
		return ret;
	}

	ret = alloc_chrdev_region(&hsi_char_dev, 0, HSI_CHAR_DEVS, devname);
	if (ret < 0) {
		hsi_unregister_client_driver(&hsi_char_driver);
		return ret;
	}

	cdev_init(&hsi_char_cdev, &hsi_char_fops);
	ret = cdev_add(&hsi_char_cdev, hsi_char_dev, HSI_CHAR_DEVS);
	if (ret) {
		unregister_chrdev_region(hsi_char_dev, HSI_CHAR_DEVS);
		hsi_unregister_client_driver(&hsi_char_driver);
		return ret;
	}

	pr_info("HSI/SSI char device loaded\n");

	return 0;
}
module_init(hsi_char_init);

static void __exit hsi_char_exit(void)
{
	cdev_del(&hsi_char_cdev);
	unregister_chrdev_region(hsi_char_dev, HSI_CHAR_DEVS);
	hsi_unregister_client_driver(&hsi_char_driver);
	pr_info("HSI char device removed\n");
}
module_exit(hsi_char_exit);

MODULE_AUTHOR("Andras Domokos <andras.domokos@nokia.com>");
MODULE_ALIAS("hsi:hsi_char");
MODULE_DESCRIPTION("HSI character device");
MODULE_LICENSE("GPL v2");
