/*
 * ssi_driver_dma.c
 *
 * Implements SSI low level interface driver functionality with DMA support.
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
#include <linux/dma-mapping.h>
#include "ssi_driver.h"
#include <mach/omap-pm.h>

#define SSI_SYNC_WRITE	0
#define SSI_SYNC_READ	1
#define SSI_L3_TPUT	13428 /* 13428 KiB/s => ~110 Mbit/s*/

static unsigned char ssi_sync_table[2][2][8] = {
	{
		{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
		{0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00}
	}, {
		{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17},
		{0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}
	}
};

/**
 * ssi_get_sync_port - Get the port number associate to a GDD sync.
 * @sync - The sync mask from where to deduce the port.
 *
 * There is not masking scheme that can retrieve easily the port number
 * from the sync value. TI spec made our live harder by for example
 * associating 0x18 and 0x08 values to different ports :(
 *
 * Return port number (1 or 2) associated to the sync mask.
 */
static inline unsigned int ssi_get_sync_port(unsigned int sync)
{
	return (((sync > 0x00) && (sync < 0x09)) ||
		((sync > 0x0f) && (sync < 0x18))) ? 1 : 2;
}

/**
 * ssi_get_free_lch - Get a free GDD(DMA)logical channel
 * @ssi_ctrl- SSI controller of the GDD.
 *
 * Needs to be called holding the ssi_controller lock
 *
 * Return a free logical channel number. If there is no free lch
 * then returns an out of range value
 */
static unsigned int ssi_get_free_lch(struct ssi_dev *ssi_ctrl)
{
	unsigned int enable_reg;
	unsigned int i;
	unsigned int lch = ssi_ctrl->last_gdd_lch;

	enable_reg = ssi_inl(ssi_ctrl->base, SSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	for (i = 1; i <= SSI_NUM_LCH; i++) {
		lch = (lch + i) & (SSI_NUM_LCH - 1);
		if (!(enable_reg & SSI_GDD_LCH(lch))) {
			ssi_ctrl->last_gdd_lch = lch;
			return lch;
		}
	}

	return lch;
}

/**
 * ssi_driver_write_dma - Program GDD [DMA] to write data from memory to
 * the ssi channel buffer.
 * @ssi_channel - pointer to the ssi_channel to write data to.
 * @data - 32-bit word pointer to the data.
 * @size - Number of 32bit words to be transfered.
 *
 * ssi_controller lock must be held before calling this function.
 *
 * Return 0 on success and < 0 on error.
 */
int ssi_driver_write_dma(struct ssi_channel *ssi_channel, u32 *data,
			 unsigned int size)
{
	struct ssi_dev *ssi_ctrl = ssi_channel->ssi_port->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	struct ssi_platform_data *pdata = ssi_ctrl->dev->platform_data;
	unsigned int port = ssi_channel->ssi_port->port_number;
	unsigned int channel = ssi_channel->channel_number;
	unsigned int sync;
	int lch;
	dma_addr_t dma_data;
	dma_addr_t s_addr;
	u16 tmp;

	if ((size < 1) || (data == NULL))
		return -EINVAL;

	clk_enable(ssi_ctrl->ssi_clk);

	lch = ssi_get_free_lch(ssi_ctrl);
	if (lch >= SSI_NUM_LCH) {
		dev_err(ssi_ctrl->dev, "No free GDD logical "
								"channels.\n");
		clk_disable(ssi_ctrl->ssi_clk);
		return -EBUSY;	/* No free GDD logical channels. */
	}

	if ((pdata->set_min_bus_tput) && (ssi_ctrl->gdd_usecount++ == 0))
		pdata->set_min_bus_tput(ssi_ctrl->dev, OCP_INITIATOR_AGENT,
					SSI_L3_TPUT);
	/* NOTE: Gettting a free gdd logical channel and
	 * reserve it must be done atomicaly. */
	ssi_channel->write_data.lch = lch;

	sync = ssi_sync_table[SSI_SYNC_WRITE][port - 1][channel];
	dma_data = dma_map_single(ssi_ctrl->dev, data, size * 4,
					DMA_TO_DEVICE);

	tmp = SSI_SRC_SINGLE_ACCESS0 |
		SSI_SRC_MEMORY_PORT |
		SSI_DST_SINGLE_ACCESS0 |
		SSI_DST_PERIPHERAL_PORT |
		SSI_DATA_TYPE_S32;
	ssi_outw(tmp, base, SSI_GDD_CSDP_REG(lch));

	tmp = SSI_SRC_AMODE_POSTINC | SSI_DST_AMODE_CONST | sync;
	ssi_outw(tmp, base, SSI_GDD_CCR_REG(lch));

	ssi_outw((SSI_BLOCK_IE | SSI_TOUT_IE), base, SSI_GDD_CICR_REG(lch));

	s_addr = (dma_addr_t)io_v2p(base +
					SSI_SST_BUFFER_CH_REG(port, channel));
	ssi_outl(s_addr, base, SSI_GDD_CDSA_REG(lch));

	ssi_outl(dma_data, base, SSI_GDD_CSSA_REG(lch));
	ssi_outw(size, base, SSI_GDD_CEN_REG(lch));

	ssi_outl_or(SSI_GDD_LCH(lch), base, SSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	ssi_outw_or(SSI_CCR_ENABLE, base, SSI_GDD_CCR_REG(lch));

	return 0;
}

/**
 * ssi_driver_read_dma - Program GDD [DMA] to write data to memory from
 * the ssi channel buffer.
 * @ssi_channel - pointer to the ssi_channel to read data from.
 * @data - 32-bit word pointer where to store the incoming data.
 * @size - Number of 32bit words to be transfered to the buffer.
 *
 * ssi_controller lock must be held before calling this function.
 *
 * Return 0 on success and < 0 on error.
 */
int ssi_driver_read_dma(struct ssi_channel *ssi_channel, u32 *data,
			unsigned int count)
{
	struct ssi_dev *ssi_ctrl = ssi_channel->ssi_port->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	struct ssi_platform_data *pdata = ssi_ctrl->dev->platform_data;
	unsigned int port = ssi_channel->ssi_port->port_number;
	unsigned int channel = ssi_channel->channel_number;
	unsigned int sync;
	unsigned int lch;
	dma_addr_t dma_data;
	dma_addr_t d_addr;
	u16 tmp;

	clk_enable(ssi_ctrl->ssi_clk);
	lch = ssi_get_free_lch(ssi_ctrl);
	if (lch >= SSI_NUM_LCH) {
		dev_err(ssi_ctrl->dev, "No free GDD logical channels.\n");
		clk_disable(ssi_ctrl->ssi_clk);
		return -EBUSY;	/* No free GDD logical channels. */
	}
	if ((pdata->set_min_bus_tput) && (ssi_ctrl->gdd_usecount++ == 0))
		pdata->set_min_bus_tput(ssi_ctrl->dev, OCP_INITIATOR_AGENT,
					SSI_L3_TPUT);
	/*
	 * NOTE: Gettting a free gdd logical channel and
	 * reserve it must be done atomicaly.
	 */
	ssi_channel->read_data.lch = lch;

	sync = ssi_sync_table[SSI_SYNC_READ][port - 1][channel];

	dma_data = dma_map_single(ssi_ctrl->dev, data, count * 4,
					DMA_FROM_DEVICE);

	tmp = SSI_DST_SINGLE_ACCESS0 |
		SSI_DST_MEMORY_PORT |
		SSI_SRC_SINGLE_ACCESS0 |
		SSI_SRC_PERIPHERAL_PORT |
		SSI_DATA_TYPE_S32;
	ssi_outw(tmp, base, SSI_GDD_CSDP_REG(lch));

	tmp = SSI_DST_AMODE_POSTINC | SSI_SRC_AMODE_CONST | sync;
	ssi_outw(tmp, base, SSI_GDD_CCR_REG(lch));

	ssi_outw((SSI_BLOCK_IE | SSI_TOUT_IE), base, SSI_GDD_CICR_REG(lch));

	d_addr = (dma_addr_t)io_v2p(base +
					SSI_SSR_BUFFER_CH_REG(port, channel));
	ssi_outl(d_addr, base, SSI_GDD_CSSA_REG(lch));

	ssi_outl(dma_data, base, SSI_GDD_CDSA_REG(lch));
	ssi_outw(count, base, SSI_GDD_CEN_REG(lch));

	ssi_outl_or(SSI_GDD_LCH(lch), base, SSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	ssi_outw_or(SSI_CCR_ENABLE, base, SSI_GDD_CCR_REG(lch));

	return 0;
}

void ssi_driver_cancel_write_dma(struct ssi_channel *ssi_ch)
{
	int lch = ssi_ch->write_data.lch;
	unsigned int port = ssi_ch->ssi_port->port_number;
	unsigned int channel = ssi_ch->channel_number;
	struct ssi_dev *ssi_ctrl = ssi_ch->ssi_port->ssi_controller;
	struct ssi_platform_data *pdata = ssi_ctrl->dev->platform_data;
	u32 ccr;

	if (lch < 0)
		return;

	clk_enable(ssi_ctrl->ssi_clk);
	ccr = ssi_inw(ssi_ctrl->base, SSI_GDD_CCR_REG(lch));
	if (!(ccr & SSI_CCR_ENABLE)) {
		dev_dbg(&ssi_ch->dev->device, LOG_NAME "Write cancel on not "
		"enabled logical channel %d CCR REG 0x%08X\n", lch, ccr);
		clk_disable(ssi_ctrl->ssi_clk);
		return;
	}

	if ((pdata->set_min_bus_tput) && (--ssi_ctrl->gdd_usecount == 0))
		pdata->set_min_bus_tput(ssi_ctrl->dev, OCP_INITIATOR_AGENT, 0);

	ssi_outw_and(~SSI_CCR_ENABLE, ssi_ctrl->base, SSI_GDD_CCR_REG(lch));
	ssi_outl_and(~SSI_GDD_LCH(lch), ssi_ctrl->base,
						SSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	ssi_outl(SSI_GDD_LCH(lch), ssi_ctrl->base,
						SSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	ssi_outl_and(~NOTFULL(channel), ssi_ctrl->base,
						SSI_SST_BUFSTATE_REG(port));

	ssi_reset_ch_write(ssi_ch);
	clk_disable(ssi_ctrl->ssi_clk);
	clk_disable(ssi_ctrl->ssi_clk);
}

void ssi_driver_cancel_read_dma(struct ssi_channel *ssi_ch)
{
	int lch = ssi_ch->read_data.lch;
	struct ssi_dev *ssi_ctrl = ssi_ch->ssi_port->ssi_controller;
	struct ssi_platform_data *pdata = ssi_ctrl->dev->platform_data;
	unsigned int port = ssi_ch->ssi_port->port_number;
	unsigned int channel = ssi_ch->channel_number;
	u32 reg;

	if (lch < 0)
		return;

	clk_enable(ssi_ctrl->ssi_clk);
	reg = ssi_inw(ssi_ctrl->base, SSI_GDD_CCR_REG(lch));
	if (!(reg & SSI_CCR_ENABLE)) {
		dev_dbg(&ssi_ch->dev->device, LOG_NAME "Read cancel on not "
		"enable logical channel %d CCR REG 0x%08X\n", lch, reg);
		clk_disable(ssi_ctrl->ssi_clk);
		return;
	}

	if ((pdata->set_min_bus_tput) && (--ssi_ctrl->gdd_usecount == 0))
		pdata->set_min_bus_tput(ssi_ctrl->dev, OCP_INITIATOR_AGENT, 0);

	ssi_outw_and(~SSI_CCR_ENABLE, ssi_ctrl->base, SSI_GDD_CCR_REG(lch));
	ssi_outl_and(~SSI_GDD_LCH(lch), ssi_ctrl->base,
						SSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	ssi_outl(SSI_GDD_LCH(lch), ssi_ctrl->base,
						SSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	ssi_outl_and(~NOTEMPTY(channel), ssi_ctrl->base,
						SSI_SSR_BUFSTATE_REG(port));

	ssi_reset_ch_read(ssi_ch);
	clk_disable(ssi_ctrl->ssi_clk);
	clk_disable(ssi_ctrl->ssi_clk);
}

static void do_ssi_gdd_lch(struct ssi_dev *ssi_ctrl, unsigned int gdd_lch)
{
	struct ssi_platform_data *pdata = ssi_ctrl->dev->platform_data;
	void __iomem *base = ssi_ctrl->base;
	struct ssi_channel *ch;
	unsigned int port;
	unsigned int channel;
	u32 sync;
	u32 gdd_csr;
	dma_addr_t dma_h;
	size_t size;

	sync = ssi_inw(base, SSI_GDD_CCR_REG(gdd_lch)) & SSI_CCR_SYNC_MASK;
	port = ssi_get_sync_port(sync);

	spin_lock(&ssi_ctrl->lock);

	ssi_outl_and(~SSI_GDD_LCH(gdd_lch), base,
						SSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	gdd_csr = ssi_inw(base, SSI_GDD_CSR_REG(gdd_lch));

	if (!(gdd_csr & SSI_CSR_TOUR)) {
		if (sync & 0x10) { /* Read path */
			channel = sync & 0x7;
			dma_h = ssi_inl(base, SSI_GDD_CDSA_REG(gdd_lch));
			size = ssi_inw(base, SSI_GDD_CEN_REG(gdd_lch)) * 4;
			dma_sync_single(ssi_ctrl->dev, dma_h, size,
					DMA_FROM_DEVICE);
			dma_unmap_single(ssi_ctrl->dev, dma_h, size,
						DMA_FROM_DEVICE);
			ch = ctrl_get_ch(ssi_ctrl, port, channel);
			ssi_reset_ch_read(ch);
			spin_unlock(&ssi_ctrl->lock);
			ch->read_done(ch->dev);
		} else {
			channel = (sync - 1) & 0x7;
			dma_h = ssi_inl(base, SSI_GDD_CSSA_REG(gdd_lch));
			size = ssi_inw(base, SSI_GDD_CEN_REG(gdd_lch)) * 4;
			dma_unmap_single(ssi_ctrl->dev, dma_h, size,
						DMA_TO_DEVICE);
			ch = ctrl_get_ch(ssi_ctrl, port, channel);
			ssi_reset_ch_write(ch);
			spin_unlock(&ssi_ctrl->lock);
			ch->write_done(ch->dev);
		}
	} else {
		dev_err(ssi_ctrl->dev, "Error  on GDD transfer "
				"on gdd channel %d sync %d\n", gdd_lch, sync);
		spin_unlock(&ssi_ctrl->lock);
		ssi_port_event_handler(&ssi_ctrl->ssi_port[port - 1],
							SSI_EVENT_ERROR, NULL);
	}

	if ((pdata->set_min_bus_tput) && (--ssi_ctrl->gdd_usecount == 0))
		pdata->set_min_bus_tput(ssi_ctrl->dev, OCP_INITIATOR_AGENT, 0);
	/* Decrease clk usecount which was increased in
	 * ssi_driver_{read,write}_dma() */
	clk_disable(ssi_ctrl->ssi_clk);
}

static void do_ssi_gdd_tasklet(unsigned long device)
{
	struct ssi_dev *ssi_ctrl = (struct ssi_dev *)device;
	void __iomem *base = ssi_ctrl->base;
	unsigned int gdd_lch = 0;
	u32 status_reg = 0;
	u32 lch_served = 0;

	clk_enable(ssi_ctrl->ssi_clk);

	status_reg = ssi_inl(base, SSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	for (gdd_lch = 0; gdd_lch < SSI_NUM_LCH; gdd_lch++) {
		if (status_reg & SSI_GDD_LCH(gdd_lch)) {
			do_ssi_gdd_lch(ssi_ctrl, gdd_lch);
			lch_served |= SSI_GDD_LCH(gdd_lch);
		}
	}

	ssi_outl(lch_served, base, SSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	status_reg = ssi_inl(base, SSI_SYS_GDD_MPU_IRQ_STATUS_REG);
	clk_disable(ssi_ctrl->ssi_clk);

	if (status_reg)
		tasklet_hi_schedule(&ssi_ctrl->ssi_gdd_tasklet);
	else
		enable_irq(ssi_ctrl->gdd_irq);
}

static irqreturn_t ssi_gdd_mpu_handler(int irq, void *ssi_controller)
{
	struct ssi_dev *ssi_ctrl = ssi_controller;

	tasklet_hi_schedule(&ssi_ctrl->ssi_gdd_tasklet);
	disable_irq_nosync(ssi_ctrl->gdd_irq);

	return IRQ_HANDLED;
}

int __init ssi_gdd_init(struct ssi_dev *ssi_ctrl, const char *irq_name)
{
	tasklet_init(&ssi_ctrl->ssi_gdd_tasklet, do_ssi_gdd_tasklet,
						(unsigned long)ssi_ctrl);
	if (request_irq(ssi_ctrl->gdd_irq, ssi_gdd_mpu_handler, IRQF_DISABLED,
						irq_name, ssi_ctrl) < 0) {
		dev_err(ssi_ctrl->dev, "FAILED to request GDD IRQ %d",
							ssi_ctrl->gdd_irq);
		return -EBUSY;
	}

	return 0;
}

void ssi_gdd_exit(struct ssi_dev *ssi_ctrl)
{
	tasklet_disable(&ssi_ctrl->ssi_gdd_tasklet);
	free_irq(ssi_ctrl->gdd_irq, ssi_ctrl);
}
