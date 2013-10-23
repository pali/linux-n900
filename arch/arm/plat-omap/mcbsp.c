/*
 * linux/arch/arm/plat-omap/mcbsp.c
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <mach/dma.h>
#include <mach/mcbsp.h>
#ifdef CONFIG_ARCH_OMAP34XX
#include "../mach-omap2/cm-regbits-34xx.h"
#endif

struct omap_mcbsp **mcbsp_ptr;
int omap_mcbsp_count;

void omap_mcbsp_write(void __iomem *io_base, u16 reg, u32 val)
{
	if (cpu_class_is_omap1() || cpu_is_omap2420())
		__raw_writew((u16)val, io_base + reg);
	else
		__raw_writel(val, io_base + reg);
}

int omap_mcbsp_read(void __iomem *io_base, u16 reg)
{
	if (cpu_class_is_omap1() || cpu_is_omap2420())
		return __raw_readw(io_base + reg);
	else
		return __raw_readl(io_base + reg);
}

#define OMAP_MCBSP_READ(base, reg) \
			omap_mcbsp_read(base, OMAP_MCBSP_REG_##reg)
#define OMAP_MCBSP_WRITE(base, reg, val) \
			omap_mcbsp_write(base, OMAP_MCBSP_REG_##reg, val)

#define omap_mcbsp_check_valid_id(id)	(id < omap_mcbsp_count)
#define id_to_mcbsp_ptr(id)		mcbsp_ptr[id];

#define OMAP_ST_READ(base, reg) \
			omap_mcbsp_read(base, OMAP_ST_REG_##reg)
#define OMAP_ST_WRITE(base, reg, val) \
			omap_mcbsp_write(base, OMAP_ST_REG_##reg, val)

#define omap_st_check_valid_id(id)	((id == 1 || id == 2) && \
						id < omap_mcbsp_count)

static void omap_mcbsp_dump_reg(u8 id)
{
	struct omap_mcbsp *mcbsp = id_to_mcbsp_ptr(id);

	dev_dbg(mcbsp->dev, "**** McBSP%d regs ****\n", mcbsp->id);
	dev_dbg(mcbsp->dev, "DRR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DRR2));
	dev_dbg(mcbsp->dev, "DRR1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DRR1));
	dev_dbg(mcbsp->dev, "DXR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DXR2));
	dev_dbg(mcbsp->dev, "DXR1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DXR1));
	dev_dbg(mcbsp->dev, "SPCR2: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SPCR2));
	dev_dbg(mcbsp->dev, "SPCR1: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SPCR1));
	dev_dbg(mcbsp->dev, "RCR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, RCR2));
	dev_dbg(mcbsp->dev, "RCR1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, RCR1));
	dev_dbg(mcbsp->dev, "XCR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, XCR2));
	dev_dbg(mcbsp->dev, "XCR1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, XCR1));
	dev_dbg(mcbsp->dev, "SRGR2: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SRGR2));
	dev_dbg(mcbsp->dev, "SRGR1: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SRGR1));
	dev_dbg(mcbsp->dev, "PCR0:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, PCR0));
	dev_dbg(mcbsp->dev, "***********************\n");
}

static irqreturn_t omap_mcbsp_tx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp_tx = dev_id;
	u16 irqst_spcr2;

	irqst_spcr2 = OMAP_MCBSP_READ(mcbsp_tx->io_base, SPCR2);
	dev_dbg(mcbsp_tx->dev, "TX IRQ callback : 0x%x\n", irqst_spcr2);

	if (irqst_spcr2 & XSYNC_ERR) {
		dev_err(mcbsp_tx->dev, "TX Frame Sync Error! : 0x%x\n",
			irqst_spcr2);
		/* Writing zero to XSYNC_ERR clears the IRQ */
		OMAP_MCBSP_WRITE(mcbsp_tx->io_base, SPCR2,
			irqst_spcr2 & ~(XSYNC_ERR));
	} else {
		complete(&mcbsp_tx->tx_irq_completion);
	}

	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_rx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp_rx = dev_id;
	u16 irqst_spcr1;

	irqst_spcr1 = OMAP_MCBSP_READ(mcbsp_rx->io_base, SPCR1);
	dev_dbg(mcbsp_rx->dev, "RX IRQ callback : 0x%x\n", irqst_spcr1);

	if (irqst_spcr1 & RSYNC_ERR) {
		dev_err(mcbsp_rx->dev, "RX Frame Sync Error! : 0x%x\n",
			irqst_spcr1);
		/* Writing zero to RSYNC_ERR clears the IRQ */
		OMAP_MCBSP_WRITE(mcbsp_rx->io_base, SPCR1,
			irqst_spcr1 & ~(RSYNC_ERR));
	} else {
		complete(&mcbsp_rx->tx_irq_completion);
	}

	return IRQ_HANDLED;
}

static void omap_mcbsp_tx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp *mcbsp_dma_tx = data;

	dev_dbg(mcbsp_dma_tx->dev, "TX DMA callback : 0x%x\n",
		OMAP_MCBSP_READ(mcbsp_dma_tx->io_base, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_tx->dma_tx_lch);
	mcbsp_dma_tx->dma_tx_lch = -1;

	complete(&mcbsp_dma_tx->tx_dma_completion);
}

static void omap_mcbsp_rx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp *mcbsp_dma_rx = data;

	dev_dbg(mcbsp_dma_rx->dev, "RX DMA callback : 0x%x\n",
		OMAP_MCBSP_READ(mcbsp_dma_rx->io_base, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_rx->dma_rx_lch);
	mcbsp_dma_rx->dma_rx_lch = -1;

	complete(&mcbsp_dma_rx->rx_dma_completion);
}

/*
 * omap_mcbsp_config simply write a config to the
 * appropriate McBSP.
 * You either call this function or set the McBSP registers
 * by yourself before calling omap_mcbsp_start().
 */
void omap_mcbsp_config(unsigned int id, const struct omap_mcbsp_reg_cfg *config)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	io_base = mcbsp->io_base;
	dev_dbg(mcbsp->dev, "Configuring McBSP%d  phys_base: 0x%08lx\n",
			mcbsp->id, mcbsp->phys_base);

	/* We write the given config */
	OMAP_MCBSP_WRITE(io_base, SPCR2, config->spcr2);
	OMAP_MCBSP_WRITE(io_base, SPCR1, config->spcr1);
	OMAP_MCBSP_WRITE(io_base, RCR2, config->rcr2);
	OMAP_MCBSP_WRITE(io_base, RCR1, config->rcr1);
	OMAP_MCBSP_WRITE(io_base, XCR2, config->xcr2);
	OMAP_MCBSP_WRITE(io_base, XCR1, config->xcr1);
	OMAP_MCBSP_WRITE(io_base, SRGR2, config->srgr2);
	OMAP_MCBSP_WRITE(io_base, SRGR1, config->srgr1);
	OMAP_MCBSP_WRITE(io_base, MCR2, config->mcr2);
	OMAP_MCBSP_WRITE(io_base, MCR1, config->mcr1);
	OMAP_MCBSP_WRITE(io_base, PCR0, config->pcr0);
	if (cpu_is_omap2430() || cpu_is_omap34xx()) {
		OMAP_MCBSP_WRITE(io_base, XCCR, config->xccr);
		OMAP_MCBSP_WRITE(io_base, RCCR, config->rccr);
	}
}
EXPORT_SYMBOL(omap_mcbsp_config);

#ifdef CONFIG_ARCH_OMAP34XX
/*
 * omap_mcbsp_set_tx_threshold configures how to deal
 * with transmit threshold. the threshold value and handler can be
 * configure in here.
 */
void omap_mcbsp_set_tx_threshold(unsigned int id, u16 threshold)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;

	if (!cpu_is_omap34xx())
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	OMAP_MCBSP_WRITE(io_base, THRSH2, threshold);
}
EXPORT_SYMBOL(omap_mcbsp_set_tx_threshold);

/*
 * omap_mcbsp_set_rx_threshold configures how to deal
 * with receive threshold. the threshold value and handler can be
 * configure in here.
 */
void omap_mcbsp_set_rx_threshold(unsigned int id, u16 threshold)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;

	if (!cpu_is_omap34xx())
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	OMAP_MCBSP_WRITE(io_base, THRSH1, threshold);
}
EXPORT_SYMBOL(omap_mcbsp_set_rx_threshold);

/*
 * omap_mcbsp_get_max_tx_thres just return the current configured
 * maximum threshold for transmission
 */
u16 omap_mcbsp_get_max_tx_threshold(unsigned int id)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	return mcbsp->max_tx_thres;
}
EXPORT_SYMBOL(omap_mcbsp_get_max_tx_threshold);

/*
 * omap_mcbsp_get_max_rx_thres just return the current configured
 * maximum threshold for reception
 */
u16 omap_mcbsp_get_max_rx_threshold(unsigned int id)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	return mcbsp->max_rx_thres;
}
EXPORT_SYMBOL(omap_mcbsp_get_max_rx_threshold);

/*
 * omap_mcbsp_get_dma_op_mode just return the current configured
 * operating mode for the mcbsp channel
 */
int omap_mcbsp_get_dma_op_mode(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	int dma_op_mode;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%u)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	spin_lock_irq(&mcbsp->lock);
	dma_op_mode = mcbsp->dma_op_mode;
	spin_unlock_irq(&mcbsp->lock);

	return dma_op_mode;
}
EXPORT_SYMBOL(omap_mcbsp_get_dma_op_mode);
#endif

/*
 * We can choose between IRQ based or polled IO.
 * This needs to be called before omap_mcbsp_request().
 */
int omap_mcbsp_set_io_type(unsigned int id, omap_mcbsp_io_type_t io_type)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	spin_lock_irq(&mcbsp->lock);

	if (!mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d is currently in use\n",
			mcbsp->id);
		spin_unlock_irq(&mcbsp->lock);
		return -EINVAL;
	}

	mcbsp->io_type = io_type;

	spin_unlock_irq(&mcbsp->lock);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_set_io_type);

int omap_mcbsp_request(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	unsigned long flags;
	int i;
	int err;
	u16 syscon;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->request)
		mcbsp->pdata->ops->request(id);

	spin_lock_irqsave(&mcbsp->lock, flags);
	if (!mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d is currently in use\n",
			mcbsp->id);
		spin_unlock_irqrestore(&mcbsp->lock, flags);
		return -1;
	}

	mcbsp->free = 0;
	spin_unlock_irqrestore(&mcbsp->lock, flags);

	for (i = 0; i < mcbsp->num_clks; i++)
		clk_enable(mcbsp->clks[i]);

	/*
	 * Enable wakup behavior, smart idle and all wakeups
	 * REVISIT: some wakeups may be unnecessary
	 */
	if (cpu_is_omap34xx()) {
		syscon = OMAP_MCBSP_READ(mcbsp->io_base, SYSCON);
		syscon &= ~(ENAWAKEUP | SIDLEMODE(0x03) | CLOCKACTIVITY(0x03));

		spin_lock_irq(&mcbsp->lock);
		if (mcbsp->dma_op_mode == MCBSP_DMA_MODE_THRESHOLD) {
			syscon |= (ENAWAKEUP | SIDLEMODE(0x02) |
					CLOCKACTIVITY(0x02));
			OMAP_MCBSP_WRITE(mcbsp->io_base, WAKEUPEN,
					WAKEUPEN_ALL);
		} else {
			syscon |= SIDLEMODE(0x01);
		}
		spin_unlock_irq(&mcbsp->lock);

		OMAP_MCBSP_WRITE(mcbsp->io_base, SYSCON, syscon);
	}

	/*
	 * Make sure that transmitter, receiver and sample-rate generator are
	 * not running before activating IRQs.
	 */
	OMAP_MCBSP_WRITE(mcbsp->io_base, SPCR1, 0);
	OMAP_MCBSP_WRITE(mcbsp->io_base, SPCR2, 0);

	if (mcbsp->io_type == OMAP_MCBSP_IRQ_IO) {
		/* We need to get IRQs here */
		init_completion(&mcbsp->tx_irq_completion);
		err = request_irq(mcbsp->tx_irq, omap_mcbsp_tx_irq_handler,
					0, "McBSP", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request TX IRQ %d "
					"for McBSP%d\n", mcbsp->tx_irq,
					mcbsp->id);
			return err;
		}

		init_completion(&mcbsp->rx_irq_completion);
		err = request_irq(mcbsp->rx_irq, omap_mcbsp_rx_irq_handler,
					0, "McBSP", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request RX IRQ %d "
					"for McBSP%d\n", mcbsp->rx_irq,
					mcbsp->id);
			free_irq(mcbsp->tx_irq, (void *)mcbsp);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_request);

void omap_mcbsp_free(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	unsigned long flags;
	int i;
	u16 syscon;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(id);

	/*
	 * Disable wakup behavior, smart idle and all wakeups
	 */
	if (cpu_is_omap34xx()) {
		syscon = OMAP_MCBSP_READ(mcbsp->io_base, SYSCON);
		syscon &= ~(ENAWAKEUP | SIDLEMODE(0x03) | CLOCKACTIVITY(0x03));
		/*
		 * HW bug workaround - If no_idle mode is taken, we need to
		 * go to smart_idle before going to always_idle, or the
		 * device will not hit retention anymore.
		 */
		syscon |= SIDLEMODE(0x02);
		OMAP_MCBSP_WRITE(mcbsp->io_base, SYSCON, syscon);

		syscon &= ~(SIDLEMODE(0x03));
		OMAP_MCBSP_WRITE(mcbsp->io_base, SYSCON, syscon);

		OMAP_MCBSP_WRITE(mcbsp->io_base, WAKEUPEN, 0);
	}

	spin_lock_irqsave(&mcbsp->lock, flags);
	if (mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d was not reserved\n",
			mcbsp->id);
		spin_unlock_irqrestore(&mcbsp->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&mcbsp->lock, flags);

	for (i = mcbsp->num_clks - 1; i >= 0; i--)
		clk_disable(mcbsp->clks[i]);

	spin_lock_irqsave(&mcbsp->lock, flags);
	mcbsp->free = 1;
	spin_unlock_irqrestore(&mcbsp->lock, flags);

	if (mcbsp->io_type == OMAP_MCBSP_IRQ_IO) {
		/* Free IRQs */
		free_irq(mcbsp->rx_irq, (void *)mcbsp);
		free_irq(mcbsp->tx_irq, (void *)mcbsp);
	}
}
EXPORT_SYMBOL(omap_mcbsp_free);

#ifdef CONFIG_ARCH_OMAP34XX
static void omap_st_enable(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data;
	void __iomem *io_base_mcbsp;
	void __iomem *io_base_st;
	unsigned int w;

	io_base_mcbsp = mcbsp->io_base;
	st_data = mcbsp->st_data;
	io_base_st = st_data->io_base_st;

	/*
	 * Sidetone uses McBSP ICLK - which must not idle when sidetones
	 * are enabled or sidetones start sounding ugly.
	 */
	w = cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	w &= ~(mcbsp->id - 1);
	cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);

	/* Enable McBSP Sidetone */
	w = OMAP_MCBSP_READ(io_base_mcbsp, SSELCR);
	OMAP_MCBSP_WRITE(io_base_mcbsp, SSELCR, w | SIDETONEEN);

	w = OMAP_ST_READ(io_base_st, SYSCONFIG);
	OMAP_ST_WRITE(io_base_st, SYSCONFIG, w & ~(ST_AUTOIDLE));

	/* Enable Sidetone from Sidetone Core */
	w = OMAP_ST_READ(io_base_st, SSELCR);
	OMAP_ST_WRITE(io_base_st, SSELCR, w | ST_SIDETONEEN);
}

static void omap_st_disable(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data;
	void __iomem *io_base_mcbsp;
	void __iomem *io_base_st;
	unsigned int w;

	io_base_mcbsp = mcbsp->io_base;
	st_data = mcbsp->st_data;
	io_base_st = st_data->io_base_st;

	w = OMAP_ST_READ(io_base_st, SSELCR);
	OMAP_ST_WRITE(io_base_st, SSELCR, w & ~(ST_SIDETONEEN));

	w = OMAP_ST_READ(io_base_st, SYSCONFIG);
	OMAP_ST_WRITE(io_base_st, SYSCONFIG, w | ST_AUTOIDLE);

	w = OMAP_MCBSP_READ(io_base_mcbsp, SSELCR);
	OMAP_MCBSP_WRITE(io_base_mcbsp, SSELCR, w & ~(SIDETONEEN));

	w = cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	w |= (mcbsp->id - 1);
	cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);
}

static void omap_st_enable_autoidle(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data;
	void __iomem *io_base_st;
	unsigned int w;

	st_data = mcbsp->st_data;
	io_base_st = st_data->io_base_st;

	w = OMAP_ST_READ(io_base_st, SYSCONFIG);
	OMAP_ST_WRITE(io_base_st, SYSCONFIG, w | ST_AUTOIDLE);
}

static void omap_st_fir_write(struct omap_mcbsp *mcbsp, s16 *fir)
{
	struct omap_mcbsp_st_data *st_data;
	void __iomem *io_base;
	u16 w, i;

	st_data = mcbsp->st_data;
	io_base = st_data->io_base_st;

	w = OMAP_ST_READ(io_base, SYSCONFIG);
	OMAP_ST_WRITE(io_base, SYSCONFIG, w & ~(ST_AUTOIDLE));

	w = OMAP_ST_READ(io_base, SSELCR);

	if (w & ST_COEFFWREN)
		OMAP_ST_WRITE(io_base, SSELCR, w & ~(ST_COEFFWREN));

	OMAP_ST_WRITE(io_base, SSELCR, w | ST_COEFFWREN);

	for (i = 0; i < 128; i++)
		OMAP_ST_WRITE(io_base, SFIRCR, fir[i]);

	i = 0;

	w = OMAP_ST_READ(io_base, SSELCR);
	while (!(w & ST_COEFFWRDONE) && (++i < 1000))
		w = OMAP_ST_READ(io_base, SSELCR);

	OMAP_ST_WRITE(io_base, SSELCR, w & ~(ST_COEFFWREN));

	if (i == 1000)
		dev_err(mcbsp->dev, "McBSP FIR load error!\n");
}

static void omap_st_chgain(struct omap_mcbsp *mcbsp, s16 ch0gain, s16 ch1gain)
{
	struct omap_mcbsp_st_data *st_data;
	void __iomem *io_base;
	u16 w;

	st_data = mcbsp->st_data;
	io_base = st_data->io_base_st;

	w = OMAP_ST_READ(io_base, SYSCONFIG);
	OMAP_ST_WRITE(io_base, SYSCONFIG, w & ~(ST_AUTOIDLE));

	w = OMAP_ST_READ(io_base, SSELCR);

	OMAP_ST_WRITE(io_base, SGAINCR, ST_CH0GAIN(ch0gain) | \
			ST_CH1GAIN(ch1gain));
}

static void omap_st_start(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	unsigned long flags;

	spin_lock_irqsave(&mcbsp->lock, flags);
	if (st_data) {
		omap_st_fir_write(mcbsp, mcbsp->st_data->taps);
		omap_st_chgain(mcbsp,
			       mcbsp->st_data->ch0gain,
			       mcbsp->st_data->ch1gain);
		if (st_data->enabled)
			omap_st_enable(mcbsp);
		else
			omap_st_enable_autoidle(mcbsp);
		st_data->running = 1;
	}
	spin_unlock_irqrestore(&mcbsp->lock, flags);
}

static void omap_st_stop(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	unsigned long flags;

	spin_lock_irqsave(&mcbsp->lock, flags);
	if (st_data && st_data->running) {
		omap_st_disable(mcbsp);
		st_data->running = 0;
	}
	spin_unlock_irqrestore(&mcbsp->lock, flags);
}
#else
static inline void omap_st_start(struct omap_mcbsp *mcbsp) {}
static inline void omap_st_stop(struct omap_mcbsp *mcbsp) {}
#endif /* CONFIG_ARCH_OMAP34XX */

/*
 * Here we start the McBSP, by enabling the sample
 * generator, both transmitter and receivers,
 * and the frame sync.
 */
void omap_mcbsp_start(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	u16 w;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	if (cpu_is_omap34xx())
		omap_st_start(mcbsp);

	mcbsp->rx_word_length = (OMAP_MCBSP_READ(io_base, RCR1) >> 5) & 0x7;
	mcbsp->tx_word_length = (OMAP_MCBSP_READ(io_base, XCR1) >> 5) & 0x7;

	/* Start the sample generator */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | (1 << 6));

	/* Enable transmitter and receiver */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | 1);

	w = OMAP_MCBSP_READ(io_base, SPCR1);
	OMAP_MCBSP_WRITE(io_base, SPCR1, w | 1);

	/*  Worst case: CLKSRG*2 = 8000khz: (1/8000) * 2 * 2 usec */
	udelay(500);

	/* Start frame sync */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | (1 << 7));

	/* Dump McBSP Regs */
	omap_mcbsp_dump_reg(id);
}
EXPORT_SYMBOL(omap_mcbsp_start);

void omap_mcbsp_stop(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	u16 w;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	/* Reset transmitter */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w & ~(1));

	/* Reset receiver */
	w = OMAP_MCBSP_READ(io_base, SPCR1);
	OMAP_MCBSP_WRITE(io_base, SPCR1, w & ~(1));

	/* Reset the sample rate generator */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w & ~(1 << 6));

	if (cpu_is_omap34xx())
		omap_st_stop(mcbsp);
}
EXPORT_SYMBOL(omap_mcbsp_stop);

void omap_mcbsp_xmit_enable(unsigned int id, u8 enable)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	u16 w;

	if (!(cpu_is_omap2430() || cpu_is_omap34xx()))
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	w = OMAP_MCBSP_READ(io_base, XCCR);

	if (enable)
		OMAP_MCBSP_WRITE(io_base, XCCR, w & ~(XDISABLE));
	else
		OMAP_MCBSP_WRITE(io_base, XCCR, w | XDISABLE);
}
EXPORT_SYMBOL(omap_mcbsp_xmit_enable);

void omap_mcbsp_recv_enable(unsigned int id, u8 enable)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	u16 w;

	if (!(cpu_is_omap2430() || cpu_is_omap34xx()))
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	w = OMAP_MCBSP_READ(io_base, RCCR);

	if (enable)
		OMAP_MCBSP_WRITE(io_base, RCCR, w & ~(RDISABLE));
	else
		OMAP_MCBSP_WRITE(io_base, RCCR, w | RDISABLE);
}
EXPORT_SYMBOL(omap_mcbsp_recv_enable);

/* polled mcbsp i/o operations */
int omap_mcbsp_pollwrite(unsigned int id, u16 buf)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *base;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	base = mcbsp->io_base;

	writew(buf, base + OMAP_MCBSP_REG_DXR1);
	/* if frame sync error - clear the error */
	if (readw(base + OMAP_MCBSP_REG_SPCR2) & XSYNC_ERR) {
		/* clear error */
		writew(readw(base + OMAP_MCBSP_REG_SPCR2) & (~XSYNC_ERR),
		       base + OMAP_MCBSP_REG_SPCR2);
		/* resend */
		return -1;
	} else {
		/* wait for transmit confirmation */
		int attemps = 0;
		while (!(readw(base + OMAP_MCBSP_REG_SPCR2) & XRDY)) {
			if (attemps++ > 1000) {
				writew(readw(base + OMAP_MCBSP_REG_SPCR2) &
				       (~XRST),
				       base + OMAP_MCBSP_REG_SPCR2);
				udelay(10);
				writew(readw(base + OMAP_MCBSP_REG_SPCR2) |
				       (XRST),
				       base + OMAP_MCBSP_REG_SPCR2);
				udelay(10);
				dev_err(mcbsp->dev, "Could not write to"
					" McBSP%d Register\n", mcbsp->id);
				return -2;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_pollwrite);

int omap_mcbsp_pollread(unsigned int id, u16 *buf)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *base;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	base = mcbsp->io_base;
	/* if frame sync error - clear the error */
	if (readw(base + OMAP_MCBSP_REG_SPCR1) & RSYNC_ERR) {
		/* clear error */
		writew(readw(base + OMAP_MCBSP_REG_SPCR1) & (~RSYNC_ERR),
		       base + OMAP_MCBSP_REG_SPCR1);
		/* resend */
		return -1;
	} else {
		/* wait for recieve confirmation */
		int attemps = 0;
		while (!(readw(base + OMAP_MCBSP_REG_SPCR1) & RRDY)) {
			if (attemps++ > 1000) {
				writew(readw(base + OMAP_MCBSP_REG_SPCR1) &
				       (~RRST),
				       base + OMAP_MCBSP_REG_SPCR1);
				udelay(10);
				writew(readw(base + OMAP_MCBSP_REG_SPCR1) |
				       (RRST),
				       base + OMAP_MCBSP_REG_SPCR1);
				udelay(10);
				dev_err(mcbsp->dev, "Could not read from"
					" McBSP%d Register\n", mcbsp->id);
				return -2;
			}
		}
	}
	*buf = readw(base + OMAP_MCBSP_REG_DRR1);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_pollread);

/*
 * IRQ based word transmission.
 */
void omap_mcbsp_xmit_word(unsigned int id, u32 word)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	omap_mcbsp_word_length word_length;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;
	word_length = mcbsp->tx_word_length;

	wait_for_completion(&mcbsp->tx_irq_completion);

	if (word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, word & 0xffff);
}
EXPORT_SYMBOL(omap_mcbsp_xmit_word);

u32 omap_mcbsp_recv_word(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	u16 word_lsb, word_msb = 0;
	omap_mcbsp_word_length word_length;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	word_length = mcbsp->rx_word_length;
	io_base = mcbsp->io_base;

	wait_for_completion(&mcbsp->rx_irq_completion);

	if (word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	return (word_lsb | (word_msb << 16));
}
EXPORT_SYMBOL(omap_mcbsp_recv_word);

int omap_mcbsp_spi_master_xmit_word_poll(unsigned int id, u32 word)
{
	struct omap_mcbsp *mcbsp;
	void __iomem *io_base;
	omap_mcbsp_word_length tx_word_length;
	omap_mcbsp_word_length rx_word_length;
	u16 spcr2, spcr1, attempts = 0, word_lsb, word_msb = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;
	tx_word_length = mcbsp->tx_word_length;
	rx_word_length = mcbsp->rx_word_length;

	if (tx_word_length != rx_word_length)
		return -EINVAL;

	/* First we wait for the transmitter to be ready */
	spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
	while (!(spcr2 & XRDY)) {
		spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
		if (attempts++ > 1000) {
			/* We must reset the transmitter */
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 & (~XRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 | XRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d transmitter not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* Now we can push the data */
	if (tx_word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, word & 0xffff);

	/* We wait for the receiver to be ready */
	spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
	while (!(spcr1 & RRDY)) {
		spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
		if (attempts++ > 1000) {
			/* We must reset the receiver */
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 & (~RRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 | RRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d receiver not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* Receiver is ready, let's read the dummy data */
	if (rx_word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_spi_master_xmit_word_poll);

int omap_mcbsp_spi_master_recv_word_poll(unsigned int id, u32 *word)
{
	struct omap_mcbsp *mcbsp;
	u32 clock_word = 0;
	void __iomem *io_base;
	omap_mcbsp_word_length tx_word_length;
	omap_mcbsp_word_length rx_word_length;
	u16 spcr2, spcr1, attempts = 0, word_lsb, word_msb = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	io_base = mcbsp->io_base;

	tx_word_length = mcbsp->tx_word_length;
	rx_word_length = mcbsp->rx_word_length;

	if (tx_word_length != rx_word_length)
		return -EINVAL;

	/* First we wait for the transmitter to be ready */
	spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
	while (!(spcr2 & XRDY)) {
		spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
		if (attempts++ > 1000) {
			/* We must reset the transmitter */
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 & (~XRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 | XRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d transmitter not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* We first need to enable the bus clock */
	if (tx_word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, clock_word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, clock_word & 0xffff);

	/* We wait for the receiver to be ready */
	spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
	while (!(spcr1 & RRDY)) {
		spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
		if (attempts++ > 1000) {
			/* We must reset the receiver */
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 & (~RRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 | RRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d receiver not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* Receiver is ready, there is something for us */
	if (rx_word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	word[0] = (word_lsb | (word_msb << 16));

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_spi_master_recv_word_poll);

/*
 * Simple DMA based buffer rx/tx routines.
 * Nothing fancy, just a single buffer tx/rx through DMA.
 * The DMA resources are released once the transfer is done.
 * For anything fancier, you should use your own customized DMA
 * routines and callbacks.
 */
int omap_mcbsp_xmit_buffer(unsigned int id, dma_addr_t buffer,
				unsigned int length)
{
	struct omap_mcbsp *mcbsp;
	int dma_tx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (omap_request_dma(mcbsp->dma_tx_sync, "McBSP TX",
				omap_mcbsp_tx_dma_callback,
				mcbsp,
				&dma_tx_ch)) {
		dev_err(mcbsp->dev, " Unable to request DMA channel for "
				"McBSP%d TX. Trying IRQ based TX\n",
				mcbsp->id);
		return -EAGAIN;
	}
	mcbsp->dma_tx_lch = dma_tx_ch;

	dev_err(mcbsp->dev, "McBSP%d TX DMA on channel %d\n", mcbsp->id,
		dma_tx_ch);

	init_completion(&mcbsp->tx_dma_completion);

	if (cpu_class_is_omap1()) {
		src_port = OMAP_DMA_PORT_TIPB;
		dest_port = OMAP_DMA_PORT_EMIFF;
	}
	if (cpu_class_is_omap2())
		sync_dev = mcbsp->dma_tx_sync;

	omap_set_dma_transfer_params(mcbsp->dma_tx_lch,
				     OMAP_DMA_DATA_TYPE_S16,
				     length >> 1, 1,
				     OMAP_DMA_SYNC_ELEMENT,
	 sync_dev, 0);

	omap_set_dma_dest_params(mcbsp->dma_tx_lch,
				 src_port,
				 OMAP_DMA_AMODE_CONSTANT,
				 mcbsp->phys_base + OMAP_MCBSP_REG_DXR1,
				 0, 0);

	omap_set_dma_src_params(mcbsp->dma_tx_lch,
				dest_port,
				OMAP_DMA_AMODE_POST_INC,
				buffer,
				0, 0);

	omap_start_dma(mcbsp->dma_tx_lch);
	wait_for_completion(&mcbsp->tx_dma_completion);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_xmit_buffer);

int omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer,
				unsigned int length)
{
	struct omap_mcbsp *mcbsp;
	int dma_rx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (omap_request_dma(mcbsp->dma_rx_sync, "McBSP RX",
				omap_mcbsp_rx_dma_callback,
				mcbsp,
				&dma_rx_ch)) {
		dev_err(mcbsp->dev, "Unable to request DMA channel for "
				"McBSP%d RX. Trying IRQ based RX\n",
				mcbsp->id);
		return -EAGAIN;
	}
	mcbsp->dma_rx_lch = dma_rx_ch;

	dev_err(mcbsp->dev, "McBSP%d RX DMA on channel %d\n", mcbsp->id,
		dma_rx_ch);

	init_completion(&mcbsp->rx_dma_completion);

	if (cpu_class_is_omap1()) {
		src_port = OMAP_DMA_PORT_TIPB;
		dest_port = OMAP_DMA_PORT_EMIFF;
	}
	if (cpu_class_is_omap2())
		sync_dev = mcbsp->dma_rx_sync;

	omap_set_dma_transfer_params(mcbsp->dma_rx_lch,
					OMAP_DMA_DATA_TYPE_S16,
					length >> 1, 1,
					OMAP_DMA_SYNC_ELEMENT,
					sync_dev, 0);

	omap_set_dma_src_params(mcbsp->dma_rx_lch,
				src_port,
				OMAP_DMA_AMODE_CONSTANT,
				mcbsp->phys_base + OMAP_MCBSP_REG_DRR1,
				0, 0);

	omap_set_dma_dest_params(mcbsp->dma_rx_lch,
					dest_port,
					OMAP_DMA_AMODE_POST_INC,
					buffer,
					0, 0);

	omap_start_dma(mcbsp->dma_rx_lch);
	wait_for_completion(&mcbsp->rx_dma_completion);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_recv_buffer);

/*
 * SPI wrapper.
 * Since SPI setup is much simpler than the generic McBSP one,
 * this wrapper just need an omap_mcbsp_spi_cfg structure as an input.
 * Once this is done, you can call omap_mcbsp_start().
 */
void omap_mcbsp_set_spi_mode(unsigned int id,
				const struct omap_mcbsp_spi_cfg *spi_cfg)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_reg_cfg mcbsp_cfg;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	memset(&mcbsp_cfg, 0, sizeof(struct omap_mcbsp_reg_cfg));

	/* SPI has only one frame */
	mcbsp_cfg.rcr1 |= (RWDLEN1(spi_cfg->word_length) | RFRLEN1(0));
	mcbsp_cfg.xcr1 |= (XWDLEN1(spi_cfg->word_length) | XFRLEN1(0));

	/* Clock stop mode */
	if (spi_cfg->clk_stp_mode == OMAP_MCBSP_CLK_STP_MODE_NO_DELAY)
		mcbsp_cfg.spcr1 |= (1 << 12);
	else
		mcbsp_cfg.spcr1 |= (3 << 11);

	/* Set clock parities */
	if (spi_cfg->rx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 |= CLKRP;
	else
		mcbsp_cfg.pcr0 &= ~CLKRP;

	if (spi_cfg->tx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 &= ~CLKXP;
	else
		mcbsp_cfg.pcr0 |= CLKXP;

	/* Set SCLKME to 0 and CLKSM to 1 */
	mcbsp_cfg.pcr0 &= ~SCLKME;
	mcbsp_cfg.srgr2 |= CLKSM;

	/* Set FSXP */
	if (spi_cfg->fsx_polarity == OMAP_MCBSP_FS_ACTIVE_HIGH)
		mcbsp_cfg.pcr0 &= ~FSXP;
	else
		mcbsp_cfg.pcr0 |= FSXP;

	if (spi_cfg->spi_mode == OMAP_MCBSP_SPI_MASTER) {
		mcbsp_cfg.pcr0 |= CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(spi_cfg->clk_div - 1);
		mcbsp_cfg.pcr0 |= FSXM;
		mcbsp_cfg.srgr2 &= ~FSGM;
		mcbsp_cfg.xcr2 |= XDATDLY(1);
		mcbsp_cfg.rcr2 |= RDATDLY(1);
	} else {
		mcbsp_cfg.pcr0 &= ~CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(1);
		mcbsp_cfg.pcr0 &= ~FSXM;
		mcbsp_cfg.xcr2 &= ~XDATDLY(3);
		mcbsp_cfg.rcr2 &= ~RDATDLY(3);
	}

	mcbsp_cfg.xcr2 &= ~XPHASE;
	mcbsp_cfg.rcr2 &= ~RPHASE;

	omap_mcbsp_config(id, &mcbsp_cfg);
}
EXPORT_SYMBOL(omap_mcbsp_set_spi_mode);

#ifdef CONFIG_ARCH_OMAP34XX
#define max_thres(m)			(mcbsp->pdata->buffer_size)
#define valid_threshold(m, val)		((val) <= max_thres(m))
#define THRESHOLD_PROP_BUILDER(prop)					\
static ssize_t prop##_show(struct device *dev,				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
									\
	return sprintf(buf, "%u\n", mcbsp->prop);			\
}									\
									\
static ssize_t prop##_store(struct device *dev,				\
				struct device_attribute *attr,		\
				const char *buf, size_t size)		\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
	unsigned long val;						\
	int status;							\
									\
	status = strict_strtoul(buf, 0, &val);				\
	if (status)							\
		return status;						\
									\
	if (!valid_threshold(mcbsp, val))				\
		return -EDOM;						\
									\
	mcbsp->prop = val;						\
	return size;							\
}									\
									\
static DEVICE_ATTR(prop, 0644, prop##_show, prop##_store);

THRESHOLD_PROP_BUILDER(max_tx_thres);
THRESHOLD_PROP_BUILDER(max_rx_thres);

static ssize_t dma_op_mode_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	int dma_op_mode;

	spin_lock_irq(&mcbsp->lock);
	dma_op_mode = mcbsp->dma_op_mode;
	spin_unlock_irq(&mcbsp->lock);

	return sprintf(buf, "%d\n", dma_op_mode);
}

static ssize_t dma_op_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	unsigned long val;
	int status;

	status = strict_strtoul(buf, 0, &val);
	if (status)
		return status;

	spin_lock_irq(&mcbsp->lock);

	if (!mcbsp->free) {
		size = -EBUSY;
		goto unlock;
	}

	if (val > MCBSP_DMA_MODE_FRAME || val < MCBSP_DMA_MODE_ELEMENT) {
		size = -EINVAL;
		goto unlock;
	}

	mcbsp->dma_op_mode = val;

unlock:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(dma_op_mode, 0644, dma_op_mode_show, dma_op_mode_store);

static ssize_t st_enable_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	return sprintf(buf, "%d\n", st_data->enabled);
}

static ssize_t st_enable_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	unsigned long val;
	int status;

	status = strict_strtoul(buf, 0, &val);
	if (status)
		return status;

	spin_lock_irq(&mcbsp->lock);
	st_data->enabled = !!val;

	if (st_data->running) {
		if (st_data->enabled)
			omap_st_enable(mcbsp);
		else
			omap_st_disable(mcbsp);
	}
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(st_enable, 0644, st_enable_show, st_enable_store);

static ssize_t st_taps_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	ssize_t status = 0;
	int i;

	spin_lock_irq(&mcbsp->lock);
	for (i = 0; i < st_data->nr_taps; i++)
		status += sprintf(&buf[status], (i ? ", %d" : "%d"),
				  st_data->taps[i]);
	if (i)
		status += sprintf(&buf[status], "\n");
	spin_unlock_irq(&mcbsp->lock);

	return status;
}

static ssize_t st_taps_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int val, tmp, status, i = 0;

	spin_lock_irq(&mcbsp->lock);
	memset(st_data->taps, 0, sizeof(st_data->taps));
	st_data->nr_taps = 0;

	do {
		status = sscanf(buf, "%d%n", &val, &tmp);
		if (status < 0 || status == 0) {
			size = -EINVAL;
			goto out;
		}
		if (val < -32768 || val > 32767) {
			size = -EINVAL;
			goto out;
		}
		st_data->taps[i++] = val;
		buf += tmp;
		if (*buf != ',')
			break;
		buf++;
	} while (1);

	st_data->nr_taps = i;

out:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(st_taps, 0644, st_taps_show, st_taps_store);

static ssize_t st_chgain_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (strcmp("st_ch0gain", attr->attr.name) == 0)
		return sprintf(buf, "%d\n", st_data->ch0gain);
	else
		return sprintf(buf, "%d\n", st_data->ch1gain);
}

static ssize_t st_chgain_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	long val;
	int status;

	status = strict_strtol(buf, 0, &val);
	if (status)
		return status;
	if (val < -32768 || val > 32767)
		return -EINVAL;

	spin_lock_irq(&mcbsp->lock);
	if (strcmp("st_ch0gain", attr->attr.name) == 0)
		st_data->ch0gain = val;
	else
		st_data->ch1gain = val;

	if (st_data->running)
		omap_st_chgain(mcbsp,
			       mcbsp->st_data->ch0gain,
			       mcbsp->st_data->ch1gain);
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(st_ch0gain, 0644, st_chgain_show, st_chgain_store);
static DEVICE_ATTR(st_ch1gain, 0644, st_chgain_show, st_chgain_store);

static const struct attribute *additional_attrs[] = {
	&dev_attr_max_tx_thres.attr,
	&dev_attr_max_rx_thres.attr,
	&dev_attr_dma_op_mode.attr,
	NULL,
};

static const struct attribute_group additional_attr_group = {
	.attrs = (struct attribute **)additional_attrs,
};

static inline int __devinit omap_additional_add(struct platform_device *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &additional_attr_group);
}

static inline void __devexit omap_additional_rem(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &additional_attr_group);
}

static const struct attribute *sidetone_attrs[] = {
	&dev_attr_st_enable.attr,
	&dev_attr_st_taps.attr,
	&dev_attr_st_ch0gain.attr,
	&dev_attr_st_ch1gain.attr,
	NULL,
};

static const struct attribute_group sidetone_attr_group = {
	.attrs = (struct attribute **)sidetone_attrs,
};

int __devinit omap_st_add(struct platform_device *pdev)
{
	struct omap_mcbsp_platform_data *pdata = pdev->dev.platform_data;
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);
	struct omap_mcbsp_st_data *st_data;
	int err;

	st_data = kzalloc(sizeof(*mcbsp->st_data), GFP_KERNEL);
	if (!st_data) {
		err = -ENOMEM;
		goto err1;
	}

	st_data->io_base_st = ioremap(pdata->phys_base_st, SZ_4K);
	if (!st_data->io_base_st) {
		err = -ENOMEM;
		goto err2;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &sidetone_attr_group);
	if (err)
		goto err3;

	mcbsp->st_data = st_data;
	return 0;

err3:
	iounmap(st_data->io_base_st);
err2:
	kfree(st_data);
err1:
	return err;

}

static void __devexit omap_st_remove(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data) {
		sysfs_remove_group(&pdev->dev.kobj, &sidetone_attr_group);
		iounmap(st_data->io_base_st);
		kfree(st_data);
	}
}
#else
static inline int __devinit omap_st_add(struct platform_device *pdev)
{
	return 0;
}
static inline void __devexit omap_st_remove(struct platform_device *pdev) {}
static inline int __devinit omap_additional_add(struct platform_device *pdev)
{
	return 0;
}
static inline void __devexit omap_additional_rem(struct platform_device *pdev)
{ }
#endif /* CONFIG_ARCH_OMAP34XX */

/*
 * McBSP1 and McBSP3 are directly mapped on 1610 and 1510.
 * 730 has only 2 McBSP, and both of them are MPU peripherals.
 */
static int __devinit omap_mcbsp_probe(struct platform_device *pdev)
{
	struct omap_mcbsp_platform_data *pdata = pdev->dev.platform_data;
	struct omap_mcbsp *mcbsp;
	int id = pdev->id - 1;
	int i;
	int ret = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "McBSP device initialized without"
				"platform data\n");
		ret = -EINVAL;
		goto exit;
	}

	dev_dbg(&pdev->dev, "Initializing OMAP McBSP (%d).\n", pdev->id);

	if (id >= omap_mcbsp_count) {
		dev_err(&pdev->dev, "Invalid McBSP device id (%d)\n", id);
		ret = -EINVAL;
		goto exit;
	}

	mcbsp = kzalloc(sizeof(struct omap_mcbsp), GFP_KERNEL);
	if (!mcbsp) {
		ret = -ENOMEM;
		goto exit;
	}
	mcbsp_ptr[id] = mcbsp;

	spin_lock_init(&mcbsp->lock);
	mcbsp->id = id + 1;
	mcbsp->free = 1;
	mcbsp->dma_tx_lch = -1;
	mcbsp->dma_rx_lch = -1;

	mcbsp->phys_base = pdata->phys_base;
	mcbsp->io_base = ioremap(pdata->phys_base, SZ_4K);
	if (!mcbsp->io_base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	/* Default I/O is IRQ based */
	mcbsp->io_type = OMAP_MCBSP_IRQ_IO;
	mcbsp->tx_irq = pdata->tx_irq;
	mcbsp->rx_irq = pdata->rx_irq;
	mcbsp->dma_rx_sync = pdata->dma_rx_sync;
	mcbsp->dma_tx_sync = pdata->dma_tx_sync;

	if (pdata->num_clks) {
		mcbsp->num_clks = pdata->num_clks;
		mcbsp->clks = kzalloc(mcbsp->num_clks * sizeof(struct clk *),
					GFP_KERNEL);
		if (!mcbsp->clks) {
			ret = -ENOMEM;
			goto exit;
		}
		for (i = 0; i < mcbsp->num_clks; i++) {
			mcbsp->clks[i] = clk_get(&pdev->dev, pdata->clk_names[i]);
			if (IS_ERR(mcbsp->clks[i])) {
				dev_err(&pdev->dev,
					"Invalid %s configuration for McBSP%d.\n",
					pdata->clk_names[i], mcbsp->id);
				ret = PTR_ERR(mcbsp->clks[i]);
				goto err_clk;
			}
		}

	}

	mcbsp->pdata = pdata;
	mcbsp->dev = &pdev->dev;
	platform_set_drvdata(pdev, mcbsp);
#ifdef CONFIG_ARCH_OMAP34XX
	if (cpu_is_omap34xx()) {
		mcbsp->max_tx_thres = max_thres(mcbsp);
		mcbsp->max_rx_thres = max_thres(mcbsp);
		mcbsp->dma_op_mode = MCBSP_DMA_MODE_THRESHOLD;
	} else {
		mcbsp->max_tx_thres = -EINVAL;
		mcbsp->max_rx_thres = -EINVAL;
		mcbsp->dma_op_mode = MCBSP_DMA_MODE_ELEMENT;
	}
#endif

	if (cpu_is_omap34xx()) {
		if (mcbsp->id == 2 || mcbsp->id == 3)
			if (omap_st_add(pdev))
				dev_warn(&pdev->dev,
				 "Unable to create sidetone controls\n");

		if (omap_additional_add(pdev))
			dev_warn(&pdev->dev,
				"Unable to create threshold controls\n");
	}

	return 0;

err_clk:
	while (i--)
		clk_put(mcbsp->clks[i]);
	kfree(mcbsp->clks);
	iounmap(mcbsp->io_base);
err_ioremap:
	mcbsp->free = 0;
exit:
	return ret;
}

static int __devexit omap_mcbsp_remove(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);
	int i;

	platform_set_drvdata(pdev, NULL);
	if (mcbsp) {

		if (mcbsp->pdata && mcbsp->pdata->ops &&
				mcbsp->pdata->ops->free)
			mcbsp->pdata->ops->free(mcbsp->id);

		if (cpu_is_omap34xx()) {
			if (mcbsp->id == 2 || mcbsp->id == 3)
				omap_st_remove(pdev);

			omap_additional_rem(pdev);
		}

		for (i = mcbsp->num_clks - 1; i >= 0; i--) {
			clk_disable(mcbsp->clks[i]);
			clk_put(mcbsp->clks[i]);
		}

		iounmap(mcbsp->io_base);

		if (mcbsp->num_clks) {
			kfree(mcbsp->clks);
			mcbsp->clks = NULL;
			mcbsp->num_clks = 0;
		}
		mcbsp->free = 0;
		mcbsp->dev = NULL;
	}

	return 0;
}

static struct platform_driver omap_mcbsp_driver = {
	.probe		= omap_mcbsp_probe,
	.remove		= __devexit_p(omap_mcbsp_remove),
	.driver		= {
		.name	= "omap-mcbsp",
	},
};

int __init omap_mcbsp_init(void)
{
	/* Register the McBSP driver */
	return platform_driver_register(&omap_mcbsp_driver);
}

