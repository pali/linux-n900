/*
 * ssi_driver_if.c
 *
 * Implements SSI hardware driver interfaces for the upper layers.
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
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

#include "ssi_driver.h"

#define NOT_SET		(-1)

int ssi_set_rx(struct ssi_port *sport, struct ssr_ctx *cfg)
{
	struct ssi_dev *ssi_ctrl = sport->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	int port = sport->port_number;

	if ((cfg->mode != SSI_MODE_STREAM) &&
		(cfg->mode != SSI_MODE_FRAME) &&
		(cfg->mode != SSI_MODE_SLEEP) &&
		(cfg->mode != NOT_SET))
		return -EINVAL;

	if ((cfg->frame_size > SSI_MAX_FRAME_SIZE) &&
		(cfg->frame_size != NOT_SET))
		return -EINVAL;

	if ((cfg->channels == 0) ||
		((cfg->channels > SSI_CHANNELS_DEFAULT) &&
			(cfg->channels != NOT_SET)))
		return -EINVAL;

	if ((cfg->channels & (-cfg->channels)) ^ cfg->channels)
		return -EINVAL;

	if ((cfg->timeout > SSI_MAX_RX_TIMEOUT) && (cfg->timeout != NOT_SET))
		return -EINVAL;

	if (cfg->mode != NOT_SET)
		ssi_outl(cfg->mode, base, SSI_SSR_MODE_REG(port));

	if (cfg->frame_size != NOT_SET)
		ssi_outl(cfg->frame_size, base, SSI_SSR_FRAMESIZE_REG(port));

	if (cfg->channels != NOT_SET) {
		ssi_outl(cfg->channels, base, SSI_SSR_CHANNELS_REG(port));
	}

	if (cfg->timeout != NOT_SET)
		ssi_outl(cfg->timeout, base, SSI_SSR_TIMEOUT_REG(port));

	return 0;
}

void ssi_get_rx(struct ssi_port *sport, struct ssr_ctx *cfg)
{
	struct ssi_dev *ssi_ctrl = sport->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	int port = sport->port_number;

	cfg->mode = ssi_inl(base, SSI_SSR_MODE_REG(port));
	cfg->frame_size = ssi_inl(base, SSI_SSR_FRAMESIZE_REG(port));
	cfg->channels = ssi_inl(base, SSI_SSR_CHANNELS_REG(port));
	cfg->timeout = ssi_inl(base, SSI_SSR_TIMEOUT_REG(port));
}

int ssi_set_tx(struct ssi_port *sport, struct sst_ctx *cfg)
{
	struct ssi_dev *ssi_ctrl = sport->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	int port = sport->port_number;

	if ((cfg->mode != SSI_MODE_STREAM) &&
		(cfg->mode != SSI_MODE_FRAME) &&
		(cfg->mode != NOT_SET))
		return -EINVAL;

	if ((cfg->frame_size > SSI_MAX_FRAME_SIZE) &&
		(cfg->frame_size != NOT_SET))
		return -EINVAL;

	if ((cfg->channels == 0) ||
		((cfg->channels > SSI_CHANNELS_DEFAULT) &&
			(cfg->channels != NOT_SET)))
		return -EINVAL;

	if ((cfg->channels & (-cfg->channels)) ^ cfg->channels)
		return -EINVAL;

	if ((cfg->divisor > SSI_MAX_TX_DIVISOR) && (cfg->divisor != NOT_SET))
		return -EINVAL;

	if ((cfg->arb_mode != SSI_ARBMODE_ROUNDROBIN) &&
		(cfg->arb_mode != SSI_ARBMODE_PRIORITY) &&
		(cfg->mode != NOT_SET))
		return -EINVAL;

	if (cfg->mode != NOT_SET)
		ssi_outl(cfg->mode, base, SSI_SST_MODE_REG(port));

	if (cfg->frame_size != NOT_SET)
		ssi_outl(cfg->frame_size, base, SSI_SST_FRAMESIZE_REG(port));

	if (cfg->channels != NOT_SET)
		ssi_outl(cfg->channels, base, SSI_SST_CHANNELS_REG(port));


	if (cfg->divisor != NOT_SET)
		ssi_outl(cfg->divisor, base, SSI_SST_DIVISOR_REG(port));

	if (cfg->arb_mode != NOT_SET)
		ssi_outl(cfg->arb_mode, base, SSI_SST_ARBMODE_REG(port));

	return 0;
}

void ssi_get_tx(struct ssi_port *sport, struct sst_ctx *cfg)
{
    struct ssi_dev *ssi_ctrl = sport->ssi_controller;
    void __iomem *base = ssi_ctrl->base;
    int port = sport->port_number;

    cfg->mode = ssi_inl(base, SSI_SST_MODE_REG(port));
    cfg->frame_size = ssi_inl(base, SSI_SST_FRAMESIZE_REG(port));
    cfg->channels = ssi_inl(base, SSI_SST_CHANNELS_REG(port));
    cfg->divisor = ssi_inl(base, SSI_SST_DIVISOR_REG(port));
    cfg->arb_mode = ssi_inl(base, SSI_SST_ARBMODE_REG(port));
}

/**
 * ssi_open - open a ssi device channel.
 * @dev - Reference to the ssi device channel to be openned.
 *
 * Returns 0 on success, -EINVAL on bad parameters, -EBUSY if is already opened.
 */
int ssi_open(struct ssi_device *dev)
{
	struct ssi_channel *ch;
	struct ssi_port *port;
	struct ssi_dev *ssi_ctrl;

	if (!dev || !dev->ch) {
		pr_err(LOG_NAME "Wrong SSI device %p\n", dev);
		return -EINVAL;
	}

	ch = dev->ch;
	if (!ch->read_done || !ch->write_done) {
		dev_err(&dev->device, "Trying to open with no (read/write) "
						"callbacks registered\n");
		return -EINVAL;
	}
	port = ch->ssi_port;
	ssi_ctrl = port->ssi_controller;
	spin_lock_bh(&ssi_ctrl->lock);
	if (ch->flags & SSI_CH_OPEN) {
		dev_err(&dev->device, "Port %d Channel %d already OPENED\n",
							dev->n_p, dev->n_ch);
		spin_unlock_bh(&ssi_ctrl->lock);
		return -EBUSY;
	}
	clk_enable(ssi_ctrl->ssi_clk);
	ch->flags |= SSI_CH_OPEN;
	ssi_outl_or(SSI_ERROROCCURED | SSI_BREAKDETECTED, ssi_ctrl->base,
		SSI_SYS_MPU_ENABLE_REG(port->port_number, port->n_irq));
	clk_disable(ssi_ctrl->ssi_clk);
	spin_unlock_bh(&ssi_ctrl->lock);

	return 0;
}
EXPORT_SYMBOL(ssi_open);

/**
 * ssi_write - write data into the ssi device channel
 * @dev - reference to the ssi device channel  to write into.
 * @data - pointer to a 32-bit word data to be written.
 * @count - number of 32-bit word to be written.
 *
 * Return 0 on sucess, a negative value on failure.
 * A success values only indicates that the request has been accepted.
 * Transfer is only completed when the write_done callback is called.
 *
 */
int ssi_write(struct ssi_device *dev, u32 *data, unsigned int count)
{
	struct ssi_channel *ch;
	int err;

	if (unlikely(!dev || !dev->ch || !data || (count <= 0))) {
		dev_err(&dev->device, "Wrong paramenters "
			"ssi_device %p data %p count %d", dev, data, count);
		return -EINVAL;
	}
	if (unlikely(!(dev->ch->flags & SSI_CH_OPEN))) {
		dev_err(&dev->device, "SSI device NOT open\n");
		return -EINVAL;
	}

	ch = dev->ch;
	spin_lock_bh(&ch->ssi_port->ssi_controller->lock);
	ch->write_data.addr = data;
	ch->write_data.size = count;

	if (count == 1)
		err = ssi_driver_write_interrupt(ch, data);
	else
		err = ssi_driver_write_dma(ch, data, count);

	if (unlikely(err < 0)) {
		ch->write_data.addr = NULL;
		ch->write_data.size = 0;
	}
	spin_unlock_bh(&ch->ssi_port->ssi_controller->lock);

	return err;

}
EXPORT_SYMBOL(ssi_write);

/**
 * ssi_read - read data from the ssi device channel
 * @dev - ssi device channel reference to read data from.
 * @data - pointer to a 32-bit word data to store the data.
 * @count - number of 32-bit word to be stored.
 *
 * Return 0 on sucess, a negative value on failure.
 * A success values only indicates that the request has been accepted.
 * Data is only available in the buffer when the read_done callback is called.
 *
 */
int ssi_read(struct ssi_device *dev, u32 *data, unsigned int count)
{
	struct ssi_channel *ch;
	int err;

	if (unlikely(!dev || !dev->ch || !data || (count <= 0))) {
		dev_err(&dev->device, "Wrong paramenters "
			"ssi_device %p data %p count %d", dev, data, count);
		return -EINVAL;
	}
	if (unlikely(!(dev->ch->flags & SSI_CH_OPEN))) {
		dev_err(&dev->device, "SSI device NOT open\n");
		return -EINVAL;
	}

	ch = dev->ch;
	spin_lock_bh(&ch->ssi_port->ssi_controller->lock);
	ch->read_data.addr = data;
	ch->read_data.size = count;

	if (count == 1)
		err = ssi_driver_read_interrupt(ch, data);
	else
		err = ssi_driver_read_dma(ch, data, count);

	if (unlikely(err < 0)) {
		ch->read_data.addr = NULL;
		ch->read_data.size = 0;
	}
	spin_unlock_bh(&ch->ssi_port->ssi_controller->lock);

	return err;
}
EXPORT_SYMBOL(ssi_read);

void __ssi_write_cancel(struct ssi_channel *ch)
{
	if (ch->write_data.size == 1)
		ssi_driver_cancel_write_interrupt(ch);
	else if (ch->write_data.size > 1)
		ssi_driver_cancel_write_dma(ch);

}
/**
 * ssi_write_cancel - Cancel pending write request.
 * @dev - ssi device channel where to cancel the pending write.
 *
 * write_done() callback will not be called after sucess of this function.
 */
void ssi_write_cancel(struct ssi_device *dev)
{
	if (unlikely(!dev || !dev->ch)) {
		pr_err(LOG_NAME "Wrong SSI device %p\n", dev);
		return;
	}
	if (unlikely(!(dev->ch->flags & SSI_CH_OPEN))) {
		dev_err(&dev->device, "SSI device NOT open\n");
		return;
	}

	spin_lock_bh(&dev->ch->ssi_port->ssi_controller->lock);
	__ssi_write_cancel(dev->ch);
	spin_unlock_bh(&dev->ch->ssi_port->ssi_controller->lock);
}
EXPORT_SYMBOL(ssi_write_cancel);

void __ssi_read_cancel(struct ssi_channel *ch)
{
	if (ch->read_data.size == 1)
		ssi_driver_cancel_read_interrupt(ch);
	else if (ch->read_data.size > 1)
		ssi_driver_cancel_read_dma(ch);
}

/**
 * ssi_read_cancel - Cancel pending read request.
 * @dev - ssi device channel where to cancel the pending read.
 *
 * read_done() callback will not be called after sucess of this function.
 */
void ssi_read_cancel(struct ssi_device *dev)
{
	if (unlikely(!dev || !dev->ch)) {
		pr_err(LOG_NAME "Wrong SSI device %p\n", dev);
		return;
	}

	if (unlikely(!(dev->ch->flags & SSI_CH_OPEN))) {
		dev_err(&dev->device, "SSI device NOT open\n");
		return;
	}

	spin_lock_bh(&dev->ch->ssi_port->ssi_controller->lock);
	__ssi_read_cancel(dev->ch);
	spin_unlock_bh(&dev->ch->ssi_port->ssi_controller->lock);

}
EXPORT_SYMBOL(ssi_read_cancel);

/**
 * ssi_poll - SSI poll
 * @dev - ssi device channel reference to apply the I/O control
 * 						(or port associated to it)
 *
 * Return 0 on sucess, a negative value on failure.
 *
 */
int ssi_poll(struct ssi_device *dev)
{
	struct ssi_channel *ch;
	int err;

	if (unlikely(!dev || !dev->ch))
		return -EINVAL;

	if (unlikely(!(dev->ch->flags & SSI_CH_OPEN))) {
		dev_err(&dev->device, "SSI device NOT open\n");
		return -EINVAL;
	}

	ch = dev->ch;
	spin_lock_bh(&ch->ssi_port->ssi_controller->lock);
	ch->flags |= SSI_CH_RX_POLL;
	err = ssi_driver_read_interrupt(ch, NULL);
	spin_unlock_bh(&ch->ssi_port->ssi_controller->lock);

	return err;
}
EXPORT_SYMBOL(ssi_poll);


/**
 * ssi_ioctl - SSI I/O control
 * @dev - ssi device channel reference to apply the I/O control
 * 						(or port associated to it)
 * @command - SSI I/O control command
 * @arg - parameter associated to the control command. NULL, if no parameter.
 *
 * Return 0 on sucess, a negative value on failure.
 *
 */
int ssi_ioctl(struct ssi_device *dev, unsigned int command, void *arg)
{
	struct ssi_channel *ch;
	struct ssi_dev *ssi_ctrl;
	void __iomem *base;
	unsigned int port, channel;
	u32 wake;
	u32 v;
	int err = 0;

	if (unlikely((!dev) ||
		(!dev->ch) ||
		(!dev->ch->ssi_port) ||
		(!dev->ch->ssi_port->ssi_controller)) ||
		(!(dev->ch->flags & SSI_CH_OPEN))) {
		pr_err(LOG_NAME "SSI IOCTL Invalid parameter\n");
		return -EINVAL;
	}


	ch = dev->ch;
	ssi_ctrl = ch->ssi_port->ssi_controller;
	port = ch->ssi_port->port_number;
	channel = ch->channel_number;
	base = ssi_ctrl->base;
	clk_enable(ssi_ctrl->ssi_clk);

	switch (command) {
	case SSI_IOCTL_WAKE_UP:
		/* We only claim once the wake line per channel */
		wake = ssi_inl(base, SSI_SYS_WAKE_REG(port));
		if (!(wake & SSI_WAKE(channel))) {
			clk_enable(ssi_ctrl->ssi_clk);
			ssi_outl(SSI_WAKE(channel), base,
					SSI_SYS_SET_WAKE_REG(port));
		}
		break;
	case SSI_IOCTL_WAKE_DOWN:
		wake = ssi_inl(base, SSI_SYS_WAKE_REG(port));
		if ((wake & SSI_WAKE(channel))) {
			ssi_outl(SSI_WAKE(channel), base,
						SSI_SYS_CLEAR_WAKE_REG(port));
			clk_disable(ssi_ctrl->ssi_clk);
		}
		break;
	case SSI_IOCTL_SEND_BREAK:
		ssi_outl(1, base, SSI_SST_BREAK_REG(port));
		break;
	case SSI_IOCTL_WAKE:
		if (arg == NULL)
			err = -EINVAL;
		else
			*(u32 *)arg = ssi_inl(base, SSI_SYS_WAKE_REG(port));
		break;
	case SSI_IOCTL_FLUSH_RX:
		ssi_outl(0, base, SSI_SSR_RXSTATE_REG(port));
		break;
	case SSI_IOCTL_FLUSH_TX:
		ssi_outl(0, base, SSI_SST_TXSTATE_REG(port));
		break;
	case SSI_IOCTL_CAWAKE:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		if (dev->ch->ssi_port->cawake_gpio < 0) {
			err = -ENODEV;
			goto out;
		}
		*(unsigned int *)arg = ssi_cawake(dev->ch->ssi_port);
		break;
	case SSI_IOCTL_SET_RX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		err = ssi_set_rx(dev->ch->ssi_port, (struct ssr_ctx *)arg);
		break;
	case SSI_IOCTL_GET_RX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		ssi_get_rx(dev->ch->ssi_port, (struct ssr_ctx *)arg);
		break;
	case SSI_IOCTL_SET_TX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		err = ssi_set_tx(dev->ch->ssi_port, (struct sst_ctx *)arg);
		break;
	case SSI_IOCTL_GET_TX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		ssi_get_tx(dev->ch->ssi_port, (struct sst_ctx *)arg);
		break;
	case SSI_IOCTL_TX_CH_FULL:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		v = ssi_inl(base, SSI_SST_BUFSTATE_REG(port));
		*(unsigned int *)arg = v & (1 << channel);
		break;
	case SSI_IOCTL_CH_DATAACCEPT:
		ssi_driver_write_interrupt(dev->ch, NULL);
		break;
	default:
		err = -ENOIOCTLCMD;
		break;
	}
out:
	clk_disable(ssi_ctrl->ssi_clk);

	return err;
}
EXPORT_SYMBOL(ssi_ioctl);

/**
 * ssi_close - close given ssi device channel
 * @dev - reference to ssi device channel.
 */
void ssi_close(struct ssi_device *dev)
{
	if (!dev || !dev->ch) {
		pr_err(LOG_NAME "Trying to close wrong SSI device %p\n", dev);
		return;
	}

	spin_lock_bh(&dev->ch->ssi_port->ssi_controller->lock);
	if (dev->ch->flags & SSI_CH_OPEN) {
		dev->ch->flags &= ~SSI_CH_OPEN;
		__ssi_write_cancel(dev->ch);
		__ssi_read_cancel(dev->ch);
	}
	spin_unlock_bh(&dev->ch->ssi_port->ssi_controller->lock);
}
EXPORT_SYMBOL(ssi_close);

/**
 * ssi_set_read_cb - register read_done() callback.
 * @dev - reference to ssi device channel where the callback is associated to.
 * @read_cb - callback to signal read transfer completed.
 *
 * NOTE: Write callback must be only set when channel is not open !
 */
void ssi_set_read_cb(struct ssi_device *dev,
					void (*read_cb)(struct ssi_device *dev))
{
	dev->ch->read_done = read_cb;
}
EXPORT_SYMBOL(ssi_set_read_cb);

/**
 * ssi_set_read_cb - register write_done() callback.
 * @dev - reference to ssi device channel where the callback is associated to.
 * @write_cb - callback to signal read transfer completed.
 *
 * NOTE: Read callback must be only set when channel is not open !
 */
void ssi_set_write_cb(struct ssi_device *dev,
				void (*write_cb)(struct ssi_device *dev))
{
	dev->ch->write_done = write_cb;
}
EXPORT_SYMBOL(ssi_set_write_cb);

/**
 * ssi_set_port_event_cb - register port_event callback.
 * @dev - reference to ssi device channel where the callback is associated to.
 * @port_event_cb - callback to signal events from the channel port.
 */
void ssi_set_port_event_cb(struct ssi_device *dev,
				void (*port_event_cb)(struct ssi_device *dev,
						unsigned int event, void *arg))
{
	write_lock_bh(&dev->ch->rw_lock);
	dev->ch->port_event = port_event_cb;
	write_unlock_bh(&dev->ch->rw_lock);
}
EXPORT_SYMBOL(ssi_set_port_event_cb);
