/*
 * ssi-if.c
 *
 * Part of the SSI character driver, implements the SSI interface.
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
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
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <asm/mach-types.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/bitmap.h>

#include <linux/ssi_driver_if.h>
#include <linux/ssi_char.h>

#include "ssi-char-debug.h"
#include "ssi-char.h"
#include "ssi-if.h"

#define SSI_CHANNEL_STATE_UNAVAIL	(1 << 0)
#define SSI_CHANNEL_STATE_READING	(1 << 1)
#define SSI_CHANNEL_STATE_WRITING	(1 << 2)

#define PORT1	0
#define PORT2	1

#define SSI_RX_PARAM(cfg, mod, fsize, n, tmo) \
	do { \
		(cfg)->mode = mod; \
		(cfg)->frame_size = fsize; \
		(cfg)->channels = n; \
		(cfg)->timeout = tmo; \
	} while (0)

#define SSI_TX_PARAM(cfg, mod, fsize, n, div, arb) \
	do { \
		(cfg)->mode = mod; \
		(cfg)->frame_size = fsize; \
		(cfg)->channels = n; \
		(cfg)->divisor = div; \
		(cfg)->arb_mode = arb; \
	} while (0)

#define RXCONV(dst, src) \
	do { \
		(dst)->mode = (src)->mode; \
		(dst)->frame_size = (src)->frame_size; \
		(dst)->channels = (src)->channels; \
		(dst)->timeout = (src)->timeout; \
	} while (0)

#define TXCONV(dst, src) \
	do { \
		(dst)->mode = (src)->mode; \
		(dst)->frame_size = (src)->frame_size; \
		(dst)->channels = (src)->channels; \
		(dst)->divisor = (src)->divisor; \
		(dst)->arb_mode = (src)->arb_mode; \
	} while (0)

struct if_ssi_channel {
	struct ssi_device *dev;
	unsigned int channel_id;
	u32 *tx_data;
	unsigned int tx_count;
	u32 *rx_data;
	unsigned int rx_count;
	unsigned int opened;
	unsigned int state;
	spinlock_t lock;
};

struct if_ssi_iface {
	struct if_ssi_channel channels[SSI_MAX_CHAR_DEVS];
	spinlock_t lock;
};

static void if_ssi_port_event(struct ssi_device *dev, unsigned int event,
				void *arg);
static int __devinit if_ssi_probe(struct ssi_device *dev);
static int __devexit if_ssi_remove(struct ssi_device *dev);

static struct ssi_device_driver if_ssi_char_driver = {
	.ctrl_mask = ANY_SSI_CONTROLLER,
	.probe = if_ssi_probe,
	.remove = __devexit_p(if_ssi_remove),
	.driver = {
		.name = "ssi_char"
	},
};

static struct if_ssi_iface ssi_iface;

static int if_ssi_read_on(int ch, u32 *data, unsigned int count)
{
	struct if_ssi_channel *channel;
	int ret;

	channel = &ssi_iface.channels[ch];

	spin_lock(&channel->lock);
	if (channel->state & SSI_CHANNEL_STATE_READING) {
		pr_err("Read still pending on channel %d\n", ch);
		spin_unlock(&channel->lock);
		return -EBUSY;
	}
	channel->state |= SSI_CHANNEL_STATE_READING;
	channel->rx_data = data;
	channel->rx_count = count;
	spin_unlock(&channel->lock);

	ret = ssi_read(channel->dev, data, count/4);

	return ret;
}

static void if_ssi_read_done(struct ssi_device *dev)
{
	struct if_ssi_channel *channel;
	struct ssi_event ev;

	channel = &ssi_iface.channels[dev->n_ch];
	spin_lock(&channel->lock);
	channel->state &= ~SSI_CHANNEL_STATE_READING;
	ev.event = SSI_EV_IN;
	ev.data = channel->rx_data;
	ev.count = channel->rx_count;
	spin_unlock(&channel->lock);
	if_notify(dev->n_ch, &ev);
}

int if_ssi_read(int ch, u32 *data, unsigned int count)
{
	int ret = 0;
	spin_lock_bh(&ssi_iface.lock);
	ret = if_ssi_read_on(ch, data, count);
	spin_unlock_bh(&ssi_iface.lock);
	return ret;
}

int if_ssi_poll(int ch)
{
	struct if_ssi_channel *channel;
	int ret = 0;
	channel = &ssi_iface.channels[ch];
	ret = ssi_poll(channel->dev);
	return ret;
}

static int if_ssi_write_on(int ch, u32 *address, unsigned int count)
{
	struct if_ssi_channel *channel;
	int ret;

	channel = &ssi_iface.channels[ch];

	spin_lock(&channel->lock);
	if (channel->state & SSI_CHANNEL_STATE_WRITING) {
		pr_err("Write still pending on channel %d\n", ch);
		spin_unlock(&channel->lock);
		return -EBUSY;
	}

	channel->tx_data = address;
	channel->tx_count = count;
	channel->state |= SSI_CHANNEL_STATE_WRITING;
	spin_unlock(&channel->lock);
	ret = ssi_write(channel->dev, address, count/4);
	return ret;
}

static void if_ssi_write_done(struct ssi_device *dev)
{
	struct if_ssi_channel *channel;
	struct ssi_event ev;

	channel = &ssi_iface.channels[dev->n_ch];

	spin_lock(&channel->lock);
	channel->state &= ~SSI_CHANNEL_STATE_WRITING;
	ev.event = SSI_EV_OUT;
	ev.data = channel->tx_data;
	ev.count = channel->tx_count;
	spin_unlock(&channel->lock);
	if_notify(dev->n_ch, &ev);
}

int if_ssi_write(int ch, u32 *data, unsigned int count)
{
	int ret = 0;

	spin_lock_bh(&ssi_iface.lock);
	ret = if_ssi_write_on(ch, data, count);
	spin_unlock_bh(&ssi_iface.lock);
	return ret;
}

void if_ssi_send_break(int ch)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, SSI_IOCTL_SEND_BREAK, NULL);
	spin_unlock_bh(&ssi_iface.lock);
}

void if_ssi_flush_rx(int ch)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, SSI_IOCTL_FLUSH_RX, NULL);
	spin_unlock_bh(&ssi_iface.lock);
}

void if_ssi_flush_ch(int ch)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock(&channel->lock);
	spin_unlock(&channel->lock);
}

void if_ssi_flush_tx(int ch)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, SSI_IOCTL_FLUSH_TX, NULL);
	spin_unlock_bh(&ssi_iface.lock);
}

void if_ssi_get_wakeline(int ch, unsigned int *state)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, SSI_IOCTL_WAKE, state);
	spin_unlock_bh(&ssi_iface.lock);
}

void if_ssi_set_wakeline(int ch, unsigned int state)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, state, NULL);
	spin_unlock_bh(&ssi_iface.lock);
}

int if_ssi_set_rx(int ch, struct ssi_rx_config *cfg)
{
	int ret;
	struct if_ssi_channel *channel;
	struct ssr_ctx ctx;

	RXCONV(&ctx, cfg);
	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ret = ssi_ioctl(channel->dev, SSI_IOCTL_SET_RX, &ctx);
	spin_unlock_bh(&ssi_iface.lock);
	return ret;
}

void if_ssi_get_rx(int ch, struct ssi_rx_config *cfg)
{
	struct if_ssi_channel *channel;
	struct ssr_ctx ctx;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, SSI_IOCTL_GET_RX, &ctx);
	RXCONV(cfg, &ctx);
	spin_unlock_bh(&ssi_iface.lock);
}

int if_ssi_set_tx(int ch, struct ssi_tx_config *cfg)
{
	int ret;
	struct if_ssi_channel *channel;
	struct sst_ctx ctx;

	TXCONV(&ctx, cfg);
	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ret = ssi_ioctl(channel->dev, SSI_IOCTL_SET_TX, &ctx);
	spin_unlock_bh(&ssi_iface.lock);
	return ret;
}

void if_ssi_get_tx(int ch, struct ssi_tx_config *cfg)
{
	struct if_ssi_channel *channel;
	struct sst_ctx ctx;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	ssi_ioctl(channel->dev, SSI_IOCTL_GET_TX, &ctx);
	TXCONV(cfg, &ctx);
	spin_unlock_bh(&ssi_iface.lock);
}

void if_ssi_cancel_read(int ch)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock(&channel->lock);
	if (channel->state & SSI_CHANNEL_STATE_READING)
		ssi_read_cancel(channel->dev);
	channel->state &= ~SSI_CHANNEL_STATE_READING;
	spin_unlock(&channel->lock);
}

void if_ssi_cancel_write(int ch)
{
	struct if_ssi_channel *channel;

	channel = &ssi_iface.channels[ch];
	spin_lock(&channel->lock);
	if (channel->state & SSI_CHANNEL_STATE_WRITING)
		ssi_write_cancel(channel->dev);
	channel->state &= ~SSI_CHANNEL_STATE_WRITING;
	spin_unlock(&channel->lock);
}

static int if_ssi_openchannel(struct if_ssi_channel *channel)
{
	int ret = 0;

	spin_lock(&channel->lock);

	if (channel->state == SSI_CHANNEL_STATE_UNAVAIL)
		return -ENODEV;

	if (channel->opened) {
		ret = -EBUSY;
		goto leave;
	}

	if (!channel->dev) {
		pr_err("Channel %d is not ready??\n",
				channel->channel_id);
		ret = -ENODEV;
		goto leave;
	}

	ret = ssi_open(channel->dev);
	if (ret < 0) {
		pr_err("Could not open channel %d\n",
				channel->channel_id);
		goto leave;
	}

	channel->opened = 1;

leave:
	spin_unlock(&channel->lock);
	return ret;
}


static int if_ssi_closechannel(struct if_ssi_channel *channel)
{
	int ret = 0;

	spin_lock(&channel->lock);

	if (!channel->opened)
		goto leave;

	if (!channel->dev) {
		pr_err("Channel %d is not ready??\n",
				channel->channel_id);
		ret = -ENODEV;
		goto leave;
	}

	/* Stop any pending read/write */
	if (channel->state & SSI_CHANNEL_STATE_READING) {
		ssi_read_cancel(channel->dev);
		channel->state &= ~SSI_CHANNEL_STATE_READING;
	}
	if (channel->state & SSI_CHANNEL_STATE_WRITING) {
		ssi_write_cancel(channel->dev);
		channel->state &= ~SSI_CHANNEL_STATE_WRITING;
	}

	ssi_close(channel->dev);

	channel->opened = 0;
leave:
	spin_unlock(&channel->lock);
	return ret;
}


int if_ssi_start(int ch)
{
	struct if_ssi_channel *channel;
	int ret = 0;

	channel = &ssi_iface.channels[ch];
	spin_lock_bh(&ssi_iface.lock);
	channel->state = 0;
	ret = if_ssi_openchannel(channel);
	if (ret < 0) {
		pr_err("Could not open channel %d\n", ch);
		spin_unlock_bh(&ssi_iface.lock);
		goto error;
	}
	if_ssi_poll(ch);
	spin_unlock_bh(&ssi_iface.lock);

error:
	return ret;
}

void if_ssi_stop(int ch)
{
	struct if_ssi_channel *channel;
	channel = &ssi_iface.channels[ch];
	if_ssi_set_wakeline(ch, 1);
	spin_lock_bh(&ssi_iface.lock);
	if_ssi_closechannel(channel);
	spin_unlock_bh(&ssi_iface.lock);
}

static int __devinit if_ssi_probe(struct ssi_device *dev)
{
	struct if_ssi_channel *channel;
	unsigned long *address;
	int ret = -ENXIO, port;

	for (port = 0; port < SSI_MAX_PORTS; port++) {
		if (if_ssi_char_driver.ch_mask[port])
			break;
	}

	if (port == SSI_MAX_PORTS)
		return -ENXIO;

	address = &if_ssi_char_driver.ch_mask[port];

	spin_lock_bh(&ssi_iface.lock);
	if (test_bit(dev->n_ch, address) && (dev->n_p == port)) {
		ssi_set_read_cb(dev, if_ssi_read_done);
		ssi_set_write_cb(dev, if_ssi_write_done);
		ssi_set_port_event_cb(dev, if_ssi_port_event);
		channel = &ssi_iface.channels[dev->n_ch];
		channel->dev = dev;
		channel->state = 0;
		ret = 0;
	}
	spin_unlock_bh(&ssi_iface.lock);

	return ret;
}

static int __devexit if_ssi_remove(struct ssi_device *dev)
{
	struct if_ssi_channel *channel;
	unsigned long *address;
	int ret = -ENXIO, port;

	for (port = 0; port < SSI_MAX_PORTS; port++) {
		if (if_ssi_char_driver.ch_mask[port])
			break;
	}

	if (port == SSI_MAX_PORTS)
		return -ENXIO;

	address = &if_ssi_char_driver.ch_mask[port];

	spin_lock_bh(&ssi_iface.lock);
	if (test_bit(dev->n_ch, address) && (dev->n_p == port)) {
		ssi_set_read_cb(dev, NULL);
		ssi_set_write_cb(dev, NULL);
		channel = &ssi_iface.channels[dev->n_ch];
		channel->dev = NULL;
		channel->state = SSI_CHANNEL_STATE_UNAVAIL;
		ret = 0;
	}
	spin_unlock_bh(&ssi_iface.lock);

	return ret;
}

static void if_ssi_port_event(struct ssi_device *dev, unsigned int event,
				void *arg)
{
	struct ssi_event ev;
	int i;

	ev.event = SSI_EV_EXCEP;
	ev.data = (u32 *)0;
	ev.count = 0;

	switch (event) {
	case SSI_EVENT_BREAK_DETECTED:
		ev.data = (u32 *)SSI_HWBREAK;
		spin_lock_bh(&ssi_iface.lock);
		for (i = 0; i < SSI_MAX_CHAR_DEVS; i++) {
			if (ssi_iface.channels[i].opened)
				if_notify(i, &ev);
		}
		spin_unlock_bh(&ssi_iface.lock);
		break;
	case SSI_EVENT_SSR_DATAAVAILABLE:
		i = (int)arg;
		ev.event = SSI_EV_AVAIL;
		spin_lock_bh(&ssi_iface.lock);
		if (ssi_iface.channels[i].opened)
			if_notify(i, &ev);
		spin_unlock_bh(&ssi_iface.lock);
		break;
	case SSI_EVENT_CAWAKE_UP:
		break;
	case SSI_EVENT_CAWAKE_DOWN:
		break;
	case SSI_EVENT_ERROR:
		break;
	default:
		printk(KERN_DEBUG "%s, Unknown event(%d)\n", __func__, event);
		break;
	}
}

int __init if_ssi_init(unsigned int port, unsigned int *channels_map)
{
	struct if_ssi_channel *channel;
	int	i, ret = 0;

	port -= 1;
	if (port >= SSI_MAX_PORTS)
		return -EINVAL;

	spin_lock_init(&ssi_iface.lock);

	for (i = 0; i < SSI_MAX_PORTS; i++)
		if_ssi_char_driver.ch_mask[i] = 0;

	for (i = 0; i < SSI_MAX_CHAR_DEVS; i++) {
		channel = &ssi_iface.channels[i];
		channel->dev = NULL;
		channel->opened = 0;
		channel->state = 0;
		channel->channel_id = i;
		spin_lock_init(&channel->lock);
		channel->state = SSI_CHANNEL_STATE_UNAVAIL;
	}

	for (i = 0; (i < SSI_MAX_CHAR_DEVS) && channels_map[i]; i++) {
		if ((channels_map[i] - 1) < SSI_MAX_CHAR_DEVS)
			if_ssi_char_driver.ch_mask[port] |= (1 << ((channels_map[i] - 1)));
	}

	ret = register_ssi_driver(&if_ssi_char_driver);
	if (ret)
		pr_err("Error while registering SSI driver %d", ret);

	return ret;
}

int __exit if_ssi_exit(void)
{
	struct if_ssi_channel *channel;
	unsigned long *address;
	int i, port;

	for (port = 0; port < SSI_MAX_PORTS; port++) {
		if (if_ssi_char_driver.ch_mask[port])
			break;
	}

	if (port == SSI_MAX_PORTS)
		return -ENXIO;

	address = &if_ssi_char_driver.ch_mask[port];

	for (i = 0; i < SSI_MAX_CHAR_DEVS; i++) {
		channel = &ssi_iface.channels[i];
		if (channel->opened) {
			if_ssi_set_wakeline(i, 1);
			if_ssi_closechannel(channel);
		}
	}
	unregister_ssi_driver(&if_ssi_char_driver);
	return 0;
}
