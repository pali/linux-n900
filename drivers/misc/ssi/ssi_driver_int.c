/*
 * ssi_driver_int.c
 *
 * Implements SSI interrupt functionality.
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

void ssi_reset_ch_read(struct ssi_channel *ch)
{
	struct ssi_port *p = ch->ssi_port;
	struct ssi_dev *ssi_ctrl = p->ssi_controller;
	unsigned int channel = ch->channel_number;
	void __iomem *base = ssi_ctrl->base;
	unsigned int port = p->port_number;
	unsigned int irq = p->n_irq;

	ch->read_data.addr = NULL;
	ch->read_data.size = 0;
	ch->read_data.lch = -1;

	ssi_outl(SSI_SSR_DATAAVAILABLE(channel), base,
			SSI_SYS_MPU_STATUS_REG(port, irq));
}

void ssi_reset_ch_write(struct ssi_channel *ch)
{
	ch->write_data.addr = NULL;
	ch->write_data.size = 0;
	ch->write_data.lch = -1;
}

int ssi_driver_write_interrupt(struct ssi_channel *ch, u32 *data)
{
	struct ssi_port *p = ch->ssi_port;
	unsigned int port = p->port_number;
	unsigned int channel = ch->channel_number;

	clk_enable(p->ssi_controller->ssi_clk);
	ssi_outl_or(SSI_SST_DATAACCEPT(channel), p->ssi_controller->base,
					SSI_SYS_MPU_ENABLE_REG(port, p->n_irq));

	return 0;
}

int ssi_driver_read_interrupt(struct ssi_channel *ch, u32 *data)
{
	struct ssi_port *p = ch->ssi_port;
	unsigned int port = p->port_number;
	unsigned int channel = ch->channel_number;

	clk_enable(p->ssi_controller->ssi_clk);

	ssi_outl_or(SSI_SSR_DATAAVAILABLE(channel), p->ssi_controller->base,
					SSI_SYS_MPU_ENABLE_REG(port, p->n_irq));

	clk_disable(p->ssi_controller->ssi_clk);

	return 0;
}

void ssi_driver_cancel_write_interrupt(struct ssi_channel *ch)
{
	struct ssi_port *p = ch->ssi_port;
	unsigned int port = p->port_number;
	unsigned int channel = ch->channel_number;
	void __iomem *base = p->ssi_controller->base;
	u32 enable;

	clk_enable(p->ssi_controller->ssi_clk);

	enable = ssi_inl(base, SSI_SYS_MPU_ENABLE_REG(port, p->n_irq));
	if (!(enable & SSI_SST_DATAACCEPT(channel))) {
		dev_dbg(&ch->dev->device, LOG_NAME "Write cancel on not "
		"enabled channel %d ENABLE REG 0x%08X", channel, enable);
		clk_disable(p->ssi_controller->ssi_clk);
		return;
	}
	ssi_outl_and(~SSI_SST_DATAACCEPT(channel), base,
				SSI_SYS_MPU_ENABLE_REG(port, p->n_irq));
	ssi_outl_and(~NOTFULL(channel), base, SSI_SST_BUFSTATE_REG(port));
	ssi_reset_ch_write(ch);

	clk_disable(p->ssi_controller->ssi_clk);
	clk_disable(p->ssi_controller->ssi_clk);
}

void ssi_driver_cancel_read_interrupt(struct ssi_channel *ch)
{
	struct ssi_port *p = ch->ssi_port;
	unsigned int port = p->port_number;
	unsigned int channel = ch->channel_number;
	void __iomem *base = p->ssi_controller->base;

	clk_enable(p->ssi_controller->ssi_clk);

	ssi_outl_and(~SSI_SSR_DATAAVAILABLE(channel), base,
					SSI_SYS_MPU_ENABLE_REG(port, p->n_irq));
	ssi_outl_and(~NOTEMPTY(channel), base, SSI_SSR_BUFSTATE_REG(port));
	ssi_reset_ch_read(ch);

	clk_disable(p->ssi_controller->ssi_clk);
}

static void do_channel_tx(struct ssi_channel *ch)
{
	struct ssi_dev *ssi_ctrl = ch->ssi_port->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	unsigned int n_ch;
	unsigned int n_p;
	unsigned int irq;

	n_ch = ch->channel_number;
	n_p = ch->ssi_port->port_number;
	irq = ch->ssi_port->n_irq;

	spin_lock(&ssi_ctrl->lock);

	if (ch->write_data.addr == NULL) {
		ssi_outl_and(~SSI_SST_DATAACCEPT(n_ch), base,
					SSI_SYS_MPU_ENABLE_REG(n_p, irq));
		ssi_reset_ch_write(ch);
		spin_unlock(&ssi_ctrl->lock);
		clk_disable(ssi_ctrl->ssi_clk);
		(*ch->write_done)(ch->dev);
	} else {
		ssi_outl(*(ch->write_data.addr), base,
					SSI_SST_BUFFER_CH_REG(n_p, n_ch));
		ch->write_data.addr = NULL;
		spin_unlock(&ssi_ctrl->lock);
	}
}

static void do_channel_rx(struct ssi_channel *ch)
{
	struct ssi_dev *ssi_ctrl = ch->ssi_port->ssi_controller;
	void __iomem *base = ch->ssi_port->ssi_controller->base;
	unsigned int n_ch;
	unsigned int n_p;
	unsigned int irq;
	int rx_poll = 0;
	int data_read = 0;

	n_ch = ch->channel_number;
	n_p = ch->ssi_port->port_number;
	irq = ch->ssi_port->n_irq;

	spin_lock(&ssi_ctrl->lock);

	if (ch->flags & SSI_CH_RX_POLL)
		rx_poll = 1;

	if (ch->read_data.addr) {
		data_read = 1;
		*(ch->read_data.addr) = ssi_inl(base,
						SSI_SSR_BUFFER_CH_REG(n_p, n_ch));
	}

	ssi_outl_and(~SSI_SSR_DATAAVAILABLE(n_ch), base,
					SSI_SYS_MPU_ENABLE_REG(n_p, irq));
	ssi_reset_ch_read(ch);

	spin_unlock(&ssi_ctrl->lock);

	if (rx_poll)
		ssi_port_event_handler(ch->ssi_port,
					SSI_EVENT_SSR_DATAAVAILABLE,
					(void *)n_ch);

	if (data_read)
		(*ch->read_done)(ch->dev);
}

static void do_ssi_tasklet(unsigned long ssi_port)
{
	struct ssi_port *pport = (struct ssi_port *)ssi_port;
	struct ssi_dev *ssi_ctrl = pport->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	unsigned int port = pport->port_number;
	unsigned int channel;
	unsigned int irq = pport->n_irq;
	u32 channels_served = 0;
	u32 status_reg;
	u32 ssr_err_reg;

	clk_enable(ssi_ctrl->ssi_clk);

	status_reg = ssi_inl(base, SSI_SYS_MPU_STATUS_REG(port, irq));
	status_reg &= ssi_inl(base, SSI_SYS_MPU_ENABLE_REG(port, irq));

	for (channel = 0; channel < pport->max_ch; channel++) {
		if (status_reg & SSI_SST_DATAACCEPT(channel)) {
			do_channel_tx(&pport->ssi_channel[channel]);
			channels_served |= SSI_SST_DATAACCEPT(channel);
		}

		if (status_reg & SSI_SSR_DATAAVAILABLE(channel)) {
			do_channel_rx(&pport->ssi_channel[channel]);
			channels_served |= SSI_SSR_DATAAVAILABLE(channel);
		}
	}

	if (status_reg & SSI_BREAKDETECTED) {
		dev_info(ssi_ctrl->dev, "Hardware BREAK on port %d\n", port);
		ssi_outl(0, base, SSI_SSR_BREAK_REG(port));
		ssi_port_event_handler(pport, SSI_EVENT_BREAK_DETECTED, NULL);
		channels_served |= SSI_BREAKDETECTED;
	}

	if (status_reg & SSI_ERROROCCURED) {
		ssr_err_reg = ssi_inl(base, SSI_SSR_ERROR_REG(port));
		dev_err(ssi_ctrl->dev, "SSI ERROR Port %d: 0x%02x\n",
							port, ssr_err_reg);
		ssi_outl(ssr_err_reg, base, SSI_SSR_ERRORACK_REG(port));
		if (ssr_err_reg) /* Ignore spurios errors */
			ssi_port_event_handler(pport, SSI_EVENT_ERROR, NULL);
		else
			dev_dbg(ssi_ctrl->dev, "spurious SSI error!\n");

		channels_served |= SSI_ERROROCCURED;
	}

	ssi_outl(channels_served, base, SSI_SYS_MPU_STATUS_REG(port, irq));

	status_reg = ssi_inl(base, SSI_SYS_MPU_STATUS_REG(port, irq));
	status_reg &= ssi_inl(base, SSI_SYS_MPU_ENABLE_REG(port, irq));

	clk_disable(ssi_ctrl->ssi_clk);

	if (status_reg)
		tasklet_hi_schedule(&pport->ssi_tasklet);
	else
		enable_irq(pport->irq);
}

static irqreturn_t ssi_mpu_handler(int irq, void *ssi_port)
{
	struct ssi_port *p = ssi_port;

	tasklet_hi_schedule(&p->ssi_tasklet);
	disable_irq_nosync(p->irq);

	return IRQ_HANDLED;
}

int __init ssi_mpu_init(struct ssi_port *ssi_p, const char *irq_name)
{
	int err;

	tasklet_init(&ssi_p->ssi_tasklet, do_ssi_tasklet,
							(unsigned long)ssi_p);
	err = request_irq(ssi_p->irq, ssi_mpu_handler, IRQF_DISABLED,
							irq_name, ssi_p);
	if (err < 0) {
		dev_err(ssi_p->ssi_controller->dev, "FAILED to MPU request"
			" IRQ (%d) on port %d", ssi_p->irq, ssi_p->port_number);
		return -EBUSY;
	}

	return 0;
}

void ssi_mpu_exit(struct ssi_port *ssi_p)
{
	tasklet_disable(&ssi_p->ssi_tasklet);
	free_irq(ssi_p->irq, ssi_p);
}
