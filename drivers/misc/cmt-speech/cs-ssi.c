/*
 *  cs-ssi.c
 *
 * Part of the CMT speech driver, implements the SSI interface.
 *
 * Copyright (C) 2008,2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 * Original author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
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
#include <linux/dma-mapping.h>
#include <linux/cache.h>

#include <linux/ssi_driver_if.h>
#include <linux/cs-protocol.h>

#include "cs-debug.h"
#include "cs-core.h"
#include "cs-ssi.h"

#define SSI_CHANNEL_STATE_READING	(1 << 0)
#define SSI_CHANNEL_STATE_WRITING	(1 << 1)

#define CONTROL_CH			0
#define DATA_CH				1

#define TARGET_MASK			(0x0f000000)
#define TARGET_CDSP			(0x1<<CS_DOMAIN_SHIFT)
#define TARGET_LOCAL			(0x0<<CS_DOMAIN_SHIFT)

#if INTERNODE_CACHE_SHIFT < 64
#define DMA_TRANSFER_ALIGN_BYTES        64
#else
#define DMA_TRANSFER_ALIGN_BYTES        INTERNODE_CACHE_SHIFT
#endif

struct cs_ssi_channel {
	struct ssi_device	*dev;

	unsigned int	channel_id;
	unsigned int	opened;
	unsigned int	state;

	spinlock_t 	lock;
};

struct cs_ssi_iface {

	struct cs_ssi_channel	channels[2];

	unsigned int	cdsp_state;
	unsigned int	wakeline_state;

	u32		control_tx;
	u32		control_rx;

	/* state exposed to application */
	struct cs_mmap_config_block *mmap_cfg;

	unsigned long	mmap_base;
	unsigned long	mmap_size;

	unsigned int	rx_slot;
	unsigned int	tx_slot;

	/* note: for security reasons, we do not trust the contents of
	 *       mmap_cfg, but instead duplicate the variables here */
	unsigned int    buf_size;
	unsigned int    rx_bufs;
	unsigned int    tx_bufs;
	unsigned int    rx_offsets[CS_MAX_BUFFERS];
	unsigned int    tx_offsets[CS_MAX_BUFFERS];

	unsigned int	slot_size; /* size of aligned memory blocks */
	unsigned int	flags;

	spinlock_t 	lock;
};

static struct cs_ssi_iface ssi_iface;

static void cs_ssi_read_on_control(void)
{
	struct cs_ssi_channel *channel;
	DENTER();

	channel = &ssi_iface.channels[CONTROL_CH];

	spin_lock(&channel->lock);

	if (channel->state & SSI_CHANNEL_STATE_READING) {
		DPRINTK("Read already pending.\n");
		spin_unlock(&channel->lock);
		DLEAVE(-1);
		return;
	}

	DPRINTK("Read issued\n");
	channel->state |= SSI_CHANNEL_STATE_READING;
	spin_unlock(&channel->lock);

	ssi_read(channel->dev, &ssi_iface.control_rx, 1);

	DLEAVE(0);
}

static void cs_ssi_read_control_done(struct ssi_device *dev)
{
	u32 msg;
	struct cs_ssi_channel *channel;
	DENTER();

	channel = &ssi_iface.channels[CONTROL_CH];
	spin_lock(&channel->lock);

	if (ssi_iface.flags & CS_FEAT_TSTAMP_RX_CTRL) {
		struct timespec *tstamp =
			&ssi_iface.mmap_cfg->tstamp_rx_ctrl;
		do_posix_clock_monotonic_gettime(tstamp);
	}

	channel->state &= ~SSI_CHANNEL_STATE_READING;

	msg = ssi_iface.control_rx;

	spin_unlock(&channel->lock);

	cs_notify(msg);

	/* Place read on control */
	cs_ssi_read_on_control();

	DLEAVE(0);
}

int cs_ssi_write_on_control(u32 message)
{
	struct cs_ssi_channel *channel;
	int	err;
	DENTER();

	channel = &ssi_iface.channels[CONTROL_CH];
	spin_lock(&channel->lock);

	if (channel->state & SSI_CHANNEL_STATE_WRITING) {
		pr_err("CS_SSI: Write still pending on control channel.\n");
		spin_unlock(&channel->lock);
		return -EBUSY;
	}

	ssi_iface.control_tx = message;
	channel->state  |= SSI_CHANNEL_STATE_WRITING;
	spin_unlock(&channel->lock);

	err = ssi_write(channel->dev, &ssi_iface.control_tx, 1);

	DLEAVE(err);
	return err;
}

static void cs_ssi_write_control_done(struct ssi_device *dev)
{
	struct cs_ssi_channel *channel;
	DENTER();

	channel = &ssi_iface.channels[CONTROL_CH];
	spin_lock(&channel->lock);

	channel->state &= ~SSI_CHANNEL_STATE_WRITING;
	ssi_iface.control_tx = 0;

	spin_unlock(&channel->lock);

	DLEAVE(0);
}

static void cs_ssi_read_on_data(void)
{
	u32	*address;
	struct cs_ssi_channel *channel;
	DENTER();

	channel = &ssi_iface.channels[DATA_CH];
	spin_lock(&channel->lock);

	if (channel->state & SSI_CHANNEL_STATE_READING) {
		DPRINTK("Read already pending.\n");
		spin_unlock(&channel->lock);
		DLEAVE(-1);
		return;
	}

	DPRINTK("Read issued\n");
	channel->state |= SSI_CHANNEL_STATE_READING;

	address = (u32 *) (ssi_iface.mmap_base +
			   ssi_iface.rx_offsets[ssi_iface.rx_slot]);

	spin_unlock(&channel->lock);

	ssi_read(channel->dev, address, ssi_iface.buf_size/4);

	DLEAVE(0);
}

static void cs_ssi_read_data_done(struct ssi_device *dev)
{
	u32 msg;
	struct cs_ssi_channel *channel;
	DENTER();

	channel = &ssi_iface.channels[DATA_CH];
	spin_lock(&channel->lock);

	channel->state &= ~SSI_CHANNEL_STATE_READING;

	msg = CS_RX_DATA_RECEIVED;
	msg |= ssi_iface.rx_slot;

	ssi_iface.rx_slot++;
	ssi_iface.rx_slot %= ssi_iface.rx_bufs;

	spin_unlock(&channel->lock);

	cs_notify(msg);

	cs_ssi_read_on_data();

	DLEAVE(0);
}

int cs_ssi_write_on_data(unsigned int slot)
{
	u32	*address;
	struct cs_ssi_channel *channel;
	int	err;
	DENTER();

	channel = &ssi_iface.channels[DATA_CH];
	spin_lock(&channel->lock);

	if (ssi_iface.cdsp_state != CS_STATE_CONFIGURED) {
		DPRINTK("Not configured, aborting\n");
		spin_unlock(&channel->lock);
		return -EINVAL;
	}

	if (channel->state & SSI_CHANNEL_STATE_WRITING) {
		pr_err("CS_SSI: Write still pending on data channel.\n");
		spin_unlock(&channel->lock);
		return -EBUSY;
	}

	ssi_iface.tx_slot = slot;
	address = (u32 *) (ssi_iface.mmap_base +
			   ssi_iface.tx_offsets[ssi_iface.tx_slot]);

	channel->state |= SSI_CHANNEL_STATE_WRITING;

	spin_unlock(&channel->lock);

	err = ssi_write(channel->dev, (u32 *)address, ssi_iface.buf_size/4);

	DLEAVE(err);
	return err;
}

static void cs_ssi_write_data_done(struct ssi_device *dev)
{
	struct cs_ssi_channel *channel;
	DENTER();

	channel = &ssi_iface.channels[DATA_CH];
	spin_lock(&channel->lock);

	channel->state &= ~SSI_CHANNEL_STATE_WRITING;

	spin_unlock(&channel->lock);

	DLEAVE(0);
}

unsigned int cs_ssi_get_state()
{
	return ssi_iface.cdsp_state;
}

int cs_ssi_command(u32 cmd)
{
	int ret = 0;
	DENTER();

	spin_lock_bh(&ssi_iface.lock);

	switch (cmd & TARGET_MASK) {
	case TARGET_CDSP:
		ret = cs_ssi_write_on_control(cmd);
		break;
	case TARGET_LOCAL:
		if ((cmd & CS_CMD_MASK) == CS_TX_DATA_READY)
			ret = cs_ssi_write_on_data(cmd & CS_PARAM_MASK);
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_bh(&ssi_iface.lock);

	DLEAVE(ret);
	return ret;
}

void cs_ssi_set_wakeline(unsigned int new_state)
{
	DENTER();

	spin_lock_bh(&ssi_iface.lock);

	if (ssi_iface.wakeline_state != new_state) {
		ssi_iface.wakeline_state = new_state;
		ssi_ioctl(ssi_iface.channels[CONTROL_CH].dev,
			  new_state ? SSI_IOCTL_WAKE_UP : SSI_IOCTL_WAKE_DOWN,
			  NULL);
	}

	spin_unlock_bh(&ssi_iface.lock);

	DLEAVE(0);
}

static int cs_ssi_openchannel(struct cs_ssi_channel *channel)
{
	int	ret = 0;

	spin_lock(&channel->lock);

	if (channel->opened)
		goto leave;

	if (!channel->dev) {
		pr_err("CS_SSI: %s channel is not ready??\n",
			channel->channel_id ? "DATA" : "CONTROL");
		ret = -ENODEV;
		goto leave;
	}
	ret = ssi_open(channel->dev);
	if (ret < 0) {
		pr_err("CS_SSI: Could not open %s channel\n",
			channel->channel_id ? "DATA" : "CONTROL");
		goto leave;
	}

	channel->opened = 1;
leave:
	spin_unlock(&channel->lock);
	return ret;
}

static int cs_ssi_closechannel(struct cs_ssi_channel *channel)
{
	int	ret = 0;

	spin_lock(&channel->lock);

	if (!channel->opened)
		goto leave;

	if (!channel->dev) {
		pr_err("CS_SSI: %s channel is not ready??\n",
			channel->channel_id ? "DATA" : "CONTROL");
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

int cs_ssi_buf_config(struct cs_buffer_config *buf_cfg)
{
	struct cs_ssi_channel *channel;
	const unsigned cache_align = DMA_TRANSFER_ALIGN_BYTES;
	int r = 0;

	DENTER();

	spin_lock_bh(&ssi_iface.lock);

	if (ssi_iface.mmap_size <=
	    (ALIGN(buf_cfg->buf_size, cache_align) *
	     (buf_cfg->rx_bufs + buf_cfg->tx_bufs)) +
	    ALIGN(sizeof(*ssi_iface.mmap_cfg), cache_align)) {

		pr_err("CS_SSI: no space for the requested buffer configuration\n");
		spin_unlock_bh(&ssi_iface.lock);
		r = -ENOBUFS;
		goto error;
	}

	ssi_iface.cdsp_state = CS_STATE_OPENED;

	channel = &ssi_iface.channels[DATA_CH];

	cs_ssi_closechannel(channel);

	ssi_iface.buf_size =
		ssi_iface.mmap_cfg->buf_size = buf_cfg->buf_size;
	if (buf_cfg->buf_size > 0) {
		ssi_iface.rx_bufs =
			ssi_iface.mmap_cfg->rx_bufs = buf_cfg->rx_bufs;
		ssi_iface.tx_bufs =
			ssi_iface.mmap_cfg->tx_bufs = buf_cfg->tx_bufs;
	}

	ssi_iface.rx_slot = 0;
	ssi_iface.tx_slot = 0;
	ssi_iface.slot_size = 0;
	ssi_iface.flags = buf_cfg->flags;
	ssi_iface.channels[DATA_CH].state = 0;

	if (ssi_iface.mmap_cfg->buf_size) {
		unsigned data_start;
		int i;

		if (cs_ssi_openchannel(channel)) {
			pr_err("CS_SSI: Could not open DATA channel\n");
			spin_unlock_bh(&ssi_iface.lock);
			r = -EINVAL;
			goto error;
		}

		ssi_iface.slot_size = ALIGN(ssi_iface.mmap_cfg->buf_size, cache_align);
		DPRINTK("setting slot size to %u, buf size %u, align %u\n",
			ssi_iface.slot_size, ssi_iface.mmap_cfg->buf_size, cache_align);

		data_start = ALIGN(sizeof(*ssi_iface.mmap_cfg), cache_align);
		DPRINTK("setting data start at %u, cfg block %u, align %u\n",
			data_start, sizeof(*ssi_iface.mmap_cfg), cache_align);


		for (i = 0; i < ssi_iface.mmap_cfg->rx_bufs; i++) {
			ssi_iface.rx_offsets[i] =
				ssi_iface.mmap_cfg->rx_offsets[i] = data_start + i * ssi_iface.slot_size;
			DPRINTK("DL buf #%u at %u\n",
				i, ssi_iface.mmap_cfg->rx_offsets[i]);
		}

		for (i = 0; i < ssi_iface.mmap_cfg->tx_bufs; i++) {
			ssi_iface.tx_offsets[i] =
				ssi_iface.mmap_cfg->tx_offsets[i] =
				data_start + (i + ssi_iface.mmap_cfg->rx_bufs) * ssi_iface.slot_size;
			DPRINTK("UL buf #%u at %u\n",
				i, ssi_iface.mmap_cfg->rx_offsets[i]);
		}

		ssi_iface.cdsp_state = CS_STATE_CONFIGURED;
	}

	spin_unlock_bh(&ssi_iface.lock);

	if (ssi_iface.buf_size) {
		local_bh_disable();
		cs_ssi_read_on_data();
		local_bh_enable();
	}

error:
	DLEAVE(r);
	return r;
}

int cs_ssi_start(unsigned long mmap_base, unsigned long mmap_size)
{
	int	err = 0;
	DENTER();

	spin_lock_bh(&ssi_iface.lock);

	ssi_iface.rx_slot = 0;
	ssi_iface.tx_slot = 0;
	ssi_iface.slot_size = 0;

	ssi_iface.channels[CONTROL_CH].state = 0;
	ssi_iface.channels[DATA_CH].state = 0;

	ssi_iface.mmap_cfg = (struct cs_mmap_config_block *)mmap_base;
	ssi_iface.mmap_base = mmap_base;
	ssi_iface.mmap_size = mmap_size;

	memset(ssi_iface.mmap_cfg, 0, sizeof(*ssi_iface.mmap_cfg));
	ssi_iface.mmap_cfg->version = CS_VER_MAJOR << 8 | CS_VER_MINOR;

	err = cs_ssi_openchannel(&ssi_iface.channels[CONTROL_CH]);
	if (err < 0) {
		pr_err("CS_SSI: Could not open CONTROL channel\n");
		spin_unlock_bh(&ssi_iface.lock);
		goto error;
	}

	ssi_iface.cdsp_state = CS_STATE_OPENED;

	spin_unlock_bh(&ssi_iface.lock);

	local_bh_disable();
	cs_ssi_read_on_control();
	local_bh_enable();

error:
	DLEAVE(err);
	return err;
}

void cs_ssi_stop(void)
{
	DENTER();

	cs_ssi_set_wakeline(0);

	spin_lock_bh(&ssi_iface.lock);

	cs_ssi_closechannel(&ssi_iface.channels[CONTROL_CH]);
	cs_ssi_closechannel(&ssi_iface.channels[DATA_CH]);

	ssi_iface.cdsp_state = CS_STATE_CLOSED;

	spin_unlock_bh(&ssi_iface.lock);
}

static int __devinit cs_ssi_probe(struct ssi_device *dev)
{
	int	err = 0;
	struct cs_ssi_channel *channel;
	DENTER();

	spin_lock_bh(&ssi_iface.lock);

	if ((dev->n_ch == 1) && (dev->n_p == 0)) {
		ssi_set_read_cb(dev, cs_ssi_read_control_done);
		ssi_set_write_cb(dev, cs_ssi_write_control_done);
		channel = &ssi_iface.channels[CONTROL_CH];
	} else if ((dev->n_ch == 2) && (dev->n_p == 0)) {
		ssi_set_read_cb(dev, cs_ssi_read_data_done);
		ssi_set_write_cb(dev, cs_ssi_write_data_done);
		channel = &ssi_iface.channels[DATA_CH];
	} else {
		err = -ENXIO;
		goto leave;
	}

	channel->dev = dev;
	channel->state = 0;
leave:
	spin_unlock_bh(&ssi_iface.lock);

	DLEAVE(err);
	return err;
}

static int __devexit cs_ssi_remove(struct ssi_device *dev)
{
	int	err = 0;
	struct cs_ssi_channel *channel;
	DENTER();

	spin_lock_bh(&ssi_iface.lock);

	if ((dev->n_ch == 1) && (dev->n_p == 0))
		channel = &ssi_iface.channels[CONTROL_CH];
	else if ((dev->n_ch == 2) && (dev->n_p == 0))
		channel = &ssi_iface.channels[DATA_CH];
	else {
		err = -ENXIO;
		goto leave;
	}

	ssi_set_read_cb(dev, NULL);
	ssi_set_write_cb(dev, NULL);
	channel->dev = NULL;
	channel->state = 0;
leave:
	spin_unlock_bh(&ssi_iface.lock);

	DLEAVE(err);
	return err;
}

static struct ssi_device_driver cs_ssi_speech_driver = {
	.ctrl_mask = ANY_SSI_CONTROLLER,
	.ch_mask[0] = CHANNEL(1) | CHANNEL(2),
	.probe = cs_ssi_probe,
	.remove = __devexit_p(cs_ssi_remove),
	.driver = {
		.name = "cmt_speech",
	},
};

int __init cs_ssi_init(void)
{
	int	err = 0;
	DENTER();

	spin_lock_init(&ssi_iface.lock);

	ssi_iface.channels[CONTROL_CH].dev = NULL;
	ssi_iface.channels[CONTROL_CH].opened = 0;
	ssi_iface.channels[CONTROL_CH].state = 0;
	ssi_iface.channels[CONTROL_CH].channel_id = CONTROL_CH;
	spin_lock_init(&ssi_iface.channels[CONTROL_CH].lock);

	ssi_iface.channels[DATA_CH].dev = NULL;
	ssi_iface.channels[DATA_CH].opened = 0;
	ssi_iface.channels[DATA_CH].state = 0;
	ssi_iface.channels[DATA_CH].channel_id = DATA_CH;
	spin_lock_init(&ssi_iface.channels[DATA_CH].lock);

	ssi_iface.cdsp_state = CS_STATE_CLOSED;
	ssi_iface.wakeline_state = 0;

	err = register_ssi_driver(&cs_ssi_speech_driver);
	if (err)
		pr_err("Error when registering ssi driver %d", err);

	DLEAVE(err);
	return err;
}

int __exit cs_ssi_exit(void)
{
	DENTER();

	cs_ssi_set_wakeline(0);

	cs_ssi_closechannel(&ssi_iface.channels[CONTROL_CH]);
	cs_ssi_closechannel(&ssi_iface.channels[DATA_CH]);

	unregister_ssi_driver(&cs_ssi_speech_driver);

	DLEAVE(0);
	return 0;
}
