/*
 * omap_ssi.c
 *
 * Implements the OMAP SSI driver.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
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
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hsi/hsi.h>
#include <linux/debugfs.h>
#include <plat/omap-pm.h>
#include <plat/clock.h>
#include <plat/ssi.h>

#define SSI_MAX_CHANNELS	8
#define SSI_MAX_GDD_LCH		8
#define SSI_BYTES_TO_FRAMES(x) ((((x) - 1) >> 2) + 1)

/**
 * struct ssi_clk_res - Device resource data for the SSI clocks
 * @clk: Pointer to the clock
 * @nb: Pointer to the clock notifier for clk, if any
 */
struct ssi_clk_res {
	struct clk *clk;
	struct notifier_block *nb;
};

/**
 * struct gdd_trn - GDD transaction data
 * @msg: Pointer to the HSI message being served
 * @sg: Pointer to the current sg entry being served
 */
struct gdd_trn {
	struct hsi_msg		*msg;
	struct scatterlist	*sg;
};

/**
 * struct omap_ssm_ctx - OMAP synchronous serial module (TX/RX) context
 * @mode: Bit transmission mode
 * @channels: Number of channels
 * @framesize: Frame size in bits
 * @timeout: RX frame timeout
 * @divisor: TX divider
 * @arb_mode: Arbitration mode for TX frame (Round robin, priority)
 */
struct omap_ssm_ctx {
	u32	mode;
	u32	channels;
	u32	frame_size;
	union	{
			u32	timeout; /* Rx Only */
			struct	{
					u32	arb_mode;
					u32	divisor;
			}; /* Tx only */
	};
};

/**
 * struct omap_ssi_port - OMAP SSI port data
 * @dev: device associated to the port (HSI port)
 * @sst_dma: SSI transmitter physical base address
 * @ssr_dma: SSI receiver physical base address
 * @sst_base: SSI transmitter base address
 * @ssr_base: SSI receiver base address
 * @wk_lock: spin lock to serialize access to the wake lines
 * @lock: Spin lock to serialize access to the SSI port
 * @channels: Current number of channels configured (1,2,4 or 8)
 * @txqueue: TX message queues
 * @rxqueue: RX message queues
 * @brkqueue: Queue of incoming HWBREAK requests (FRAME mode)
 * @irq: IRQ number
 * @wake_irq: IRQ number for incoming wake line (-1 if none)
 * @pio_tasklet: Bottom half for PIO transfers and events
 * @wake_tasklet: Bottom half for incoming wake events
 * @wkin_cken: Keep track of clock references due to the incoming wake line
 * @wk_refcount: Reference count for output wake line
 * @sys_mpu_enable: Context for the interrupt enable register for irq 0
 * @sst: Context for the synchronous serial transmitter
 * @ssr: Context for the synchronous serial receiver
 */
struct omap_ssi_port {
	struct device		*dev;
	dma_addr_t		sst_dma;
	dma_addr_t		ssr_dma;
	void __iomem		*sst_base;
	void __iomem		*ssr_base;
	spinlock_t		wk_lock;
	spinlock_t		lock;
	unsigned int		channels;
	struct list_head	txqueue[SSI_MAX_CHANNELS];
	struct list_head	rxqueue[SSI_MAX_CHANNELS];
	struct list_head	brkqueue;
	unsigned int		irq;
	int			wake_irq;
	struct tasklet_struct	pio_tasklet;
	struct tasklet_struct	wake_tasklet;
	unsigned int		wkin_cken:1; /* Workaround */
	int			wk_refcount;
	/* OMAP SSI port context */
	u32			sys_mpu_enable; /* We use only one irq */
	struct omap_ssm_ctx	sst;
	struct omap_ssm_ctx	ssr;
};

/**
 * struct omap_ssi_controller - OMAP SSI controller data
 * @dev: device associated to the controller (HSI controller)
 * @sys: SSI I/O base address
 * @gdd: GDD I/O base address
 * @ick: SSI interconnect clock
 * @fck: SSI functional clock
 * @ck_refcount: References count for clocks
 * @gdd_irq: IRQ line for GDD
 * @gdd_tasklet: bottom half for DMA transfers
 * @gdd_trn: Array of GDD transaction data for ongoing GDD transfers
 * @lock: lock to serialize access to GDD
 * @ck_lock: lock to serialize access to the clocks
 * @loss_count: To follow if we need to restore context or not
 * @max_speed: Maximum TX speed (Kb/s) set by the clients.
 * @sysconfig: SSI controller saved context
 * @gdd_gcr: SSI GDD saved context
 * @get_loss: Pointer to omap_pm_get_dev_context_loss_count, if any
 * @port: Array of pointers of the ports of the controller
 * @dir: Debugfs SSI root directory
 */
struct omap_ssi_controller {
	struct device		*dev;
	void __iomem		*sys;
	void __iomem		*gdd;
	struct clk		*ick;
	struct clk		*fck;
	int			ck_refcount;
	unsigned int		gdd_irq;
	struct tasklet_struct	gdd_tasklet;
	struct gdd_trn		gdd_trn[SSI_MAX_GDD_LCH];
	spinlock_t		lock;
	spinlock_t		ck_lock;
	unsigned long		fck_rate;
	u32			loss_count;
	u32			max_speed;
	/* OMAP SSI Controller context */
	u32			sysconfig;
	u32			gdd_gcr;
	u32			(*get_loss)(struct device *dev);
	struct omap_ssi_port	**port;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dir;
#endif
};

static inline unsigned int ssi_wakein(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	return gpio_get_value(irq_to_gpio(omap_port->wake_irq));
}

static int ssi_for_each_port(struct hsi_controller *ssi, void *data,
			int (*fn)(struct omap_ssi_port *p, void *data))
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	unsigned int i = 0;
	int err = 0;

	for (i = 0; ((i < ssi->num_ports) && !err); i++)
		err = (*fn)(omap_ssi->port[i], data);

	return err;
}

static int ssi_set_port_mode(struct omap_ssi_port *omap_port, void *data)
{
	u32 *mode = data;

	__raw_writel(*mode, omap_port->sst_base + SSI_SST_MODE_REG);
	__raw_writel(*mode, omap_port->ssr_base + SSI_SSR_MODE_REG);
	/* OCP barrier */
	*mode = __raw_readl(omap_port->ssr_base + SSI_SSR_MODE_REG);

	return 0;
}

static inline void ssi_set_mode(struct hsi_controller *ssi, u32 mode)
{
	ssi_for_each_port(ssi, &mode, ssi_set_port_mode);
}

static int ssi_restore_port_mode(struct omap_ssi_port *omap_port,
						void *data __maybe_unused)
{
	u32 mode;

	__raw_writel(omap_port->sst.mode,
				omap_port->sst_base + SSI_SST_MODE_REG);
	__raw_writel(omap_port->ssr.mode,
				omap_port->ssr_base + SSI_SSR_MODE_REG);
	/* OCP barrier */
	mode =  __raw_readl(omap_port->ssr_base + SSI_SSR_MODE_REG);

	return 0;
}

static int ssi_restore_divisor(struct omap_ssi_port *omap_port,
						void *data __maybe_unused)
{
	__raw_writel(omap_port->sst.divisor,
				omap_port->sst_base + SSI_SST_DIVISOR_REG);

	return 0;
}

static int ssi_restore_port_ctx(struct omap_ssi_port *omap_port,
						void *data __maybe_unused)
{
	struct hsi_port *port = to_hsi_port(omap_port->dev);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem	*base = omap_port->sst_base;

	__raw_writel(omap_port->sys_mpu_enable,
			omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	/* SST context */
	__raw_writel(omap_port->sst.frame_size, base + SSI_SST_FRAMESIZE_REG);
	__raw_writel(omap_port->sst.channels, base + SSI_SST_CHANNELS_REG);
	__raw_writel(omap_port->sst.arb_mode, base + SSI_SST_ARBMODE_REG);
	/* SSR context */
	base = omap_port->ssr_base;
	__raw_writel(omap_port->ssr.frame_size, base + SSI_SSR_FRAMESIZE_REG);
	__raw_writel(omap_port->ssr.channels, base + SSI_SSR_CHANNELS_REG);
	__raw_writel(omap_port->ssr.timeout, base + SSI_SSR_TIMEOUT_REG);

	return 0;
}

static int ssi_save_port_ctx(struct omap_ssi_port *omap_port,
						void *data __maybe_unused)
{
	struct hsi_port *port = to_hsi_port(omap_port->dev);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	omap_port->sys_mpu_enable = __raw_readl(omap_ssi->sys +
					SSI_MPU_ENABLE_REG(port->num, 0));

	return 0;
}

static int ssi_clk_enable(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int err = 0;

	spin_lock_bh(&omap_ssi->ck_lock);
	if (omap_ssi->ck_refcount++)
		goto out;
	err = clk_enable(omap_ssi->fck);
	if (unlikely(err < 0))
		goto out;
	err = clk_enable(omap_ssi->ick);
	if (unlikely(err < 0)) {
		clk_disable(omap_ssi->fck);
		goto out;
	}
	if ((omap_ssi->get_loss) && (omap_ssi->loss_count ==
				(*omap_ssi->get_loss)(ssi->device.parent)))
		goto mode; /* We always need to restore the mode & TX divisor */

	__raw_writel(omap_ssi->sysconfig, omap_ssi->sys + SSI_SYSCONFIG_REG);
	__raw_writel(omap_ssi->gdd_gcr, omap_ssi->gdd + SSI_GDD_GCR_REG);

	ssi_for_each_port(ssi, NULL, ssi_restore_port_ctx);
mode:
	ssi_for_each_port(ssi, NULL, ssi_restore_divisor);
	ssi_for_each_port(ssi, NULL, ssi_restore_port_mode);
out:
	spin_unlock_bh(&omap_ssi->ck_lock);

	return err;
}

static void ssi_clk_disable(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	spin_lock_bh(&omap_ssi->ck_lock);
	WARN_ON(omap_ssi->ck_refcount <= 0);
	if (--omap_ssi->ck_refcount)
		goto out;

	ssi_set_mode(ssi, SSI_MODE_SLEEP);

	if (omap_ssi->get_loss)
		omap_ssi->loss_count =
				(*omap_ssi->get_loss)(ssi->device.parent);

	ssi_for_each_port(ssi, NULL, ssi_save_port_ctx);
	clk_disable(omap_ssi->ick);
	clk_disable(omap_ssi->fck);
out:
	spin_unlock_bh(&omap_ssi->ck_lock);
}

#ifdef CONFIG_DEBUG_FS
static int ssi_debug_show(struct seq_file *m, void *p __maybe_unused)
{
	struct hsi_controller *ssi = m->private;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem	*sys = omap_ssi->sys;

	ssi_clk_enable(ssi);
	seq_printf(m, "REVISION\t: 0x%08x\n",
					__raw_readl(sys + SSI_REVISION_REG));
	seq_printf(m, "SYSCONFIG\t: 0x%08x\n",
					__raw_readl(sys + SSI_SYSCONFIG_REG));
	seq_printf(m, "SYSSTATUS\t: 0x%08x\n",
					__raw_readl(sys + SSI_SYSSTATUS_REG));
	ssi_clk_disable(ssi);

	return 0;
}

static int ssi_debug_port_show(struct seq_file *m, void *p __maybe_unused)
{
	struct hsi_port *port = m->private;
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem	*base = omap_ssi->sys;
	unsigned int ch;

	ssi_clk_enable(ssi);
	if (omap_port->wake_irq > 0)
		seq_printf(m, "CAWAKE\t\t: %d\n", ssi_wakein(port));
	seq_printf(m, "WAKE\t\t: 0x%08x\n",
				__raw_readl(base + SSI_WAKE_REG(port->num)));
	seq_printf(m, "MPU_ENABLE_IRQ%d\t: 0x%08x\n", 0,
			__raw_readl(base + SSI_MPU_ENABLE_REG(port->num, 0)));
	seq_printf(m, "MPU_STATUS_IRQ%d\t: 0x%08x\n", 0,
			__raw_readl(base + SSI_MPU_STATUS_REG(port->num, 0)));
	/* SST */
	base = omap_port->sst_base;
	seq_printf(m, "\nSST\n===\n");
	seq_printf(m, "ID SST\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_ID_REG));
	seq_printf(m, "MODE\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_MODE_REG));
	seq_printf(m, "FRAMESIZE\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_FRAMESIZE_REG));
	seq_printf(m, "DIVISOR\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_DIVISOR_REG));
	seq_printf(m, "CHANNELS\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_CHANNELS_REG));
	seq_printf(m, "ARBMODE\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_ARBMODE_REG));
	seq_printf(m, "TXSTATE\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_TXSTATE_REG));
	seq_printf(m, "BUFSTATE\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_BUFSTATE_REG));
	seq_printf(m, "BREAK\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SST_BREAK_REG));
	for (ch = 0; ch < omap_port->channels; ch++) {
		seq_printf(m, "BUFFER_CH%d\t: 0x%08x\n", ch,
				__raw_readl(base + SSI_SST_BUFFER_CH_REG(ch)));
	}
	/* SSR */
	base = omap_port->ssr_base;
	seq_printf(m, "\nSSR\n===\n");
	seq_printf(m, "ID SSR\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_ID_REG));
	seq_printf(m, "MODE\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_MODE_REG));
	seq_printf(m, "FRAMESIZE\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_FRAMESIZE_REG));
	seq_printf(m, "CHANNELS\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_CHANNELS_REG));
	seq_printf(m, "TIMEOUT\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_TIMEOUT_REG));
	seq_printf(m, "RXSTATE\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_RXSTATE_REG));
	seq_printf(m, "BUFSTATE\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_BUFSTATE_REG));
	seq_printf(m, "BREAK\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_BREAK_REG));
	seq_printf(m, "ERROR\t\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_ERROR_REG));
	seq_printf(m, "ERRORACK\t: 0x%08x\n",
				__raw_readl(base + SSI_SSR_ERRORACK_REG));
	for (ch = 0; ch < omap_port->channels; ch++) {
		seq_printf(m, "BUFFER_CH%d\t: 0x%08x\n", ch,
				__raw_readl(base + SSI_SSR_BUFFER_CH_REG(ch)));
	}
	ssi_clk_disable(ssi);

	return 0;
}

static int ssi_debug_gdd_show(struct seq_file *m, void *p __maybe_unused)
{
	struct hsi_controller *ssi = m->private;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem	*gdd = omap_ssi->gdd;
	int lch;

	ssi_clk_enable(ssi);
	seq_printf(m, "GDD_MPU_STATUS\t: 0x%08x\n",
		__raw_readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_STATUS_REG));
	seq_printf(m, "GDD_MPU_ENABLE\t: 0x%08x\n\n",
		__raw_readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG));
	seq_printf(m, "HW_ID\t\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_HW_ID_REG));
	seq_printf(m, "PPORT_ID\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_PPORT_ID_REG));
	seq_printf(m, "MPORT_ID\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_MPORT_ID_REG));
	seq_printf(m, "TEST\t\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_TEST_REG));
	seq_printf(m, "GCR\t\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_GCR_REG));

	for (lch = 0; lch < SSI_MAX_GDD_LCH; lch++) {
		seq_printf(m, "\nGDD LCH %d\n=========\n", lch);
		seq_printf(m, "CSDP\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CSDP_REG(lch)));
		seq_printf(m, "CCR\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CCR_REG(lch)));
		seq_printf(m, "CICR\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CICR_REG(lch)));
		seq_printf(m, "CSR\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CSR_REG(lch)));
		seq_printf(m, "CSSA\t\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_CSSA_REG(lch)));
		seq_printf(m, "CDSA\t\t: 0x%08x\n",
				__raw_readl(gdd + SSI_GDD_CDSA_REG(lch)));
		seq_printf(m, "CEN\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CEN_REG(lch)));
		seq_printf(m, "CSAC\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CSAC_REG(lch)));
		seq_printf(m, "CDAC\t\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CDAC_REG(lch)));
		seq_printf(m, "CLNK_CTRL\t: 0x%04x\n",
				__raw_readw(gdd + SSI_GDD_CLNK_CTRL_REG(lch)));
	}
	ssi_clk_disable(ssi);

	return 0;
}

static int ssi_div_get(void *data, u64 *val)
{
	struct hsi_port *port = data;
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);

	ssi_clk_enable(ssi);
	*val = __raw_readl(omap_port->sst_base + SSI_SST_DIVISOR_REG);
	ssi_clk_disable(ssi);

	return 0;
}

static int ssi_div_set(void *data, u64 val)
{
	struct hsi_port *port = data;
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);

	if (val > 127)
		return -EINVAL;

	ssi_clk_enable(ssi);
	__raw_writel(val, omap_port->sst_base + SSI_SST_DIVISOR_REG);
	omap_port->sst.divisor = val;
	ssi_clk_disable(ssi);

	return 0;
}

static int ssi_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, ssi_debug_show, inode->i_private);
}

static int ssi_port_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, ssi_debug_port_show, inode->i_private);
}

static int ssi_gdd_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, ssi_debug_gdd_show, inode->i_private);
}

static const struct file_operations ssi_regs_fops = {
	.open		= ssi_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations ssi_port_regs_fops = {
	.open		= ssi_port_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations ssi_gdd_regs_fops = {
	.open		= ssi_gdd_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

DEFINE_SIMPLE_ATTRIBUTE(ssi_sst_div_fops, ssi_div_get, ssi_div_set, "%llu\n");

static int __init ssi_debug_add_port(struct omap_ssi_port *omap_port,
								void *data)
{
	struct hsi_port *port = to_hsi_port(omap_port->dev);
	struct dentry *dir = data;

	dir = debugfs_create_dir(dev_name(omap_port->dev), dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	debugfs_create_file("regs", S_IRUGO, dir, port, &ssi_port_regs_fops);
	dir = debugfs_create_dir("sst", dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	debugfs_create_file("divisor", S_IRUGO | S_IWUSR, dir, port,
							&ssi_sst_div_fops);

	return 0;
}

static int __init ssi_debug_add_ctrl(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct dentry *dir;
	int err;

	/* SSI controller */
	omap_ssi->dir = debugfs_create_dir(dev_name(&ssi->device), NULL);
	if (IS_ERR(omap_ssi->dir))
		return PTR_ERR(omap_ssi->dir);

	debugfs_create_file("regs", S_IRUGO, omap_ssi->dir, ssi,
								&ssi_regs_fops);
	/* SSI GDD (DMA) */
	dir = debugfs_create_dir("gdd", omap_ssi->dir);
	if (IS_ERR(dir))
		goto rback;
	debugfs_create_file("regs", S_IRUGO, dir, ssi, &ssi_gdd_regs_fops);
	/* SSI ports */
	err = ssi_for_each_port(ssi, omap_ssi->dir, ssi_debug_add_port);
	if (err < 0)
		goto rback;

	return 0;
rback:
	debugfs_remove_recursive(omap_ssi->dir);

	return PTR_ERR(dir);
}

static void ssi_debug_remove_ctrl(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	debugfs_remove_recursive(omap_ssi->dir);
}
#endif /* CONFIG_DEBUG_FS */

static int ssi_claim_lch(struct hsi_msg *msg)
{

	struct hsi_port *port = hsi_get_port(msg->cl);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int lch;

	for (lch = 0; lch < SSI_MAX_GDD_LCH; lch++)
		if (!omap_ssi->gdd_trn[lch].msg) {
			omap_ssi->gdd_trn[lch].msg = msg;
			omap_ssi->gdd_trn[lch].sg = msg->sgt.sgl;
			return lch;
		}

	return -EBUSY;
}

static int ssi_start_pio(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	u32 val;

	ssi_clk_enable(ssi);
	if (msg->ttype == HSI_MSG_WRITE) {
		val = SSI_DATAACCEPT(msg->channel);
		ssi_clk_enable(ssi); /* Hold clocks for pio writes */
	} else {
		val = SSI_DATAAVAILABLE(msg->channel) | SSI_ERROROCCURED;
	}
	dev_dbg(&port->device, "Single %s transfer\n",
						msg->ttype ? "write" : "read");
	val |= __raw_readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	__raw_writel(val, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	ssi_clk_disable(ssi);
	msg->actual_len = 0;
	msg->status = HSI_STATUS_PROCEEDING;

	return 0;
}

static int ssi_start_dma(struct hsi_msg *msg, int lch)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *gdd = omap_ssi->gdd;
	int err;
	u16 csdp;
	u16 ccr;
	u32 s_addr;
	u32 d_addr;
	u32 tmp;

	if (msg->ttype == HSI_MSG_READ) {
		err = dma_map_sg(&ssi->device, msg->sgt.sgl, msg->sgt.nents,
							DMA_FROM_DEVICE);
		if (err < 0) {
			dev_dbg(&ssi->device, "DMA map SG failed !\n");
			return err;
		}
		csdp = SSI_DST_BURST_4x32_BIT | SSI_DST_MEMORY_PORT |
			SSI_SRC_SINGLE_ACCESS0 | SSI_SRC_PERIPHERAL_PORT |
			SSI_DATA_TYPE_S32;
		ccr = msg->channel + 0x10 + (port->num * 8); /* Sync */
		ccr |= SSI_DST_AMODE_POSTINC | SSI_SRC_AMODE_CONST |
			SSI_CCR_ENABLE;
		s_addr = omap_port->ssr_dma +
					SSI_SSR_BUFFER_CH_REG(msg->channel);
		d_addr = sg_dma_address(msg->sgt.sgl);
	} else {
		err = dma_map_sg(&ssi->device, msg->sgt.sgl, msg->sgt.nents,
							DMA_TO_DEVICE);
		if (err < 0) {
			dev_dbg(&ssi->device, "DMA map SG failed !\n");
			return err;
		}
		csdp = SSI_SRC_BURST_4x32_BIT | SSI_SRC_MEMORY_PORT |
			SSI_DST_SINGLE_ACCESS0 | SSI_DST_PERIPHERAL_PORT |
			SSI_DATA_TYPE_S32;
		ccr = (msg->channel + 1 + (port->num * 8)) & 0xf; /* Sync */
		ccr |= SSI_SRC_AMODE_POSTINC | SSI_DST_AMODE_CONST |
			SSI_CCR_ENABLE;
		s_addr = sg_dma_address(msg->sgt.sgl);
		d_addr = omap_port->sst_dma +
					SSI_SST_BUFFER_CH_REG(msg->channel);
	}
	dev_dbg(&ssi->device, "lch %d cdsp %08x ccr %04x s_addr %08x"
			" d_addr %08x\n", lch, csdp, ccr, s_addr, d_addr);
	ssi_clk_enable(ssi); /* Hold clocks during the transfer */
	__raw_writew(csdp, gdd + SSI_GDD_CSDP_REG(lch));
	__raw_writew(SSI_BLOCK_IE | SSI_TOUT_IE, gdd + SSI_GDD_CICR_REG(lch));
	__raw_writel(d_addr, gdd + SSI_GDD_CDSA_REG(lch));
	__raw_writel(s_addr, gdd + SSI_GDD_CSSA_REG(lch));
	__raw_writew(SSI_BYTES_TO_FRAMES(msg->sgt.sgl->length),
						gdd + SSI_GDD_CEN_REG(lch));

	spin_lock_bh(&omap_ssi->lock);
	tmp = __raw_readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	tmp |= SSI_GDD_LCH(lch);
	__raw_writel(tmp, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	spin_unlock_bh(&omap_ssi->lock);
	__raw_writew(ccr, gdd + SSI_GDD_CCR_REG(lch));
	msg->status = HSI_STATUS_PROCEEDING;

	return 0;
}

static int ssi_start_transfer(struct list_head *queue)
{
	struct hsi_msg *msg;
	int lch = -1;

	if (list_empty(queue))
		return 0;
	msg = list_first_entry(queue, struct hsi_msg, link);
	if (msg->status != HSI_STATUS_QUEUED)
		return 0;
	if ((msg->sgt.nents) && (msg->sgt.sgl->length > sizeof(u32)))
		lch = ssi_claim_lch(msg);
	if (lch >= 0)
		return ssi_start_dma(msg, lch);
	else
		return ssi_start_pio(msg);
}

static void ssi_transfer(struct omap_ssi_port *omap_port,
							struct list_head *queue)
{
	struct hsi_msg *msg;
	int err = -1;

	spin_lock_bh(&omap_port->lock);
	while (err < 0) {
		err = ssi_start_transfer(queue);
		if (err < 0) {
			msg = list_first_entry(queue, struct hsi_msg, link);
			msg->status = HSI_STATUS_ERROR;
			msg->actual_len = 0;
			list_del(&msg->link);
			spin_unlock_bh(&omap_port->lock);
			msg->complete(msg);
			spin_lock_bh(&omap_port->lock);
		}
	}
	spin_unlock_bh(&omap_port->lock);
}

static u32 ssi_calculate_div(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	u32 tx_fckrate = (u32) omap_ssi->fck_rate;

	/* / 2 : SSI TX clock is always half of the SSI functional clock */
	tx_fckrate >>= 1;
	/* Round down when tx_fckrate % omap_ssi->max_speed == 0 */
	tx_fckrate--;
	dev_dbg(&ssi->device, "TX div %d for fck_rate %lu Khz speed %d Kb/s\n",
			tx_fckrate / omap_ssi->max_speed, omap_ssi->fck_rate,
							omap_ssi->max_speed);

	return tx_fckrate / omap_ssi->max_speed;
}

static void ssi_error(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	unsigned int i;
	u32 err;
	u32 val;
	u32 tmp;

	/* ACK error */
	err = __raw_readl(omap_port->ssr_base + SSI_SSR_ERROR_REG);
	dev_err(&port->device, "SSI error: 0x%02x\n", err);
	if (!err) {
		dev_dbg(&port->device, "spurious SSI error ignored!\n");
		return;
	}
	spin_lock(&omap_ssi->lock);
	/* Cancel all GDD read transfers */
	for (i = 0, val = 0; i < SSI_MAX_GDD_LCH; i++) {
		msg = omap_ssi->gdd_trn[i].msg;
		if ((msg) && (msg->ttype == HSI_MSG_READ)) {
			__raw_writew(0, omap_ssi->gdd + SSI_GDD_CCR_REG(i));
			val |= (1 << i);
			omap_ssi->gdd_trn[i].msg = NULL;
		}
	}
	tmp = __raw_readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	tmp &= ~val;
	__raw_writel(tmp, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	spin_unlock(&omap_ssi->lock);
	/* Cancel all PIO read transfers */
	spin_lock(&omap_port->lock);
	tmp = __raw_readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	tmp &= 0xfeff00ff; /* Disable error & all dataavailable interrupts */
	__raw_writel(tmp, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	/* ACK error */
	__raw_writel(err, omap_port->ssr_base + SSI_SSR_ERRORACK_REG);
	__raw_writel(SSI_ERROROCCURED,
			omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	/* Signal the error all current pending read requests */
	for (i = 0; i < omap_port->channels; i++) {
		if (list_empty(&omap_port->rxqueue[i]))
			continue;
		msg = list_first_entry(&omap_port->rxqueue[i], struct hsi_msg,
									link);
		list_del(&msg->link);
		msg->status = HSI_STATUS_ERROR;
		spin_unlock(&omap_port->lock);
		msg->complete(msg);
		/* Now restart queued reads if any */
		ssi_transfer(omap_port, &omap_port->rxqueue[i]);
		spin_lock(&omap_port->lock);
	}
	spin_unlock(&omap_port->lock);
}

static void ssi_break_complete(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	struct hsi_msg *tmp;
	u32 val;

	dev_dbg(&port->device, "HWBREAK received\n");

	spin_lock(&omap_port->lock);
	val = __raw_readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	val &= ~SSI_BREAKDETECTED;
	__raw_writel(val, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	__raw_writel(0, omap_port->ssr_base + SSI_SSR_BREAK_REG);
	__raw_writel(SSI_BREAKDETECTED,
			omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	spin_unlock(&omap_port->lock);

	list_for_each_entry_safe(msg, tmp, &omap_port->brkqueue, link) {
		msg->status = HSI_STATUS_COMPLETED;
		spin_lock(&omap_port->lock);
		list_del(&msg->link);
		spin_unlock(&omap_port->lock);
		msg->complete(msg);
	}

}

static int ssi_async_break(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int err = 0;
	u32 tmp;

	ssi_clk_enable(ssi);
	if (msg->ttype == HSI_MSG_WRITE) {
		if (omap_port->sst.mode != SSI_MODE_FRAME) {
			err = -EINVAL;
			goto out;
		}
		__raw_writel(1, omap_port->sst_base + SSI_SST_BREAK_REG);
		msg->status = HSI_STATUS_COMPLETED;
		msg->complete(msg);
	} else {
		if (omap_port->ssr.mode != SSI_MODE_FRAME) {
			err = -EINVAL;
			goto out;
		}
		spin_lock_bh(&omap_port->lock);
		tmp = __raw_readl(omap_ssi->sys +
					SSI_MPU_ENABLE_REG(port->num, 0));
		__raw_writel(tmp | SSI_BREAKDETECTED,
			omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
		msg->status = HSI_STATUS_PROCEEDING;
		list_add_tail(&msg->link, &omap_port->brkqueue);
		spin_unlock_bh(&omap_port->lock);
	}
out:
	ssi_clk_disable(ssi);

	return err;
}

static int ssi_async(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct list_head *queue;
	int err = 0;

	BUG_ON(!msg);

	if (msg->sgt.nents > 1)
		return -ENOSYS; /* TODO: Add sg support */

	if (msg->break_frame)
		return ssi_async_break(msg);

	if (msg->ttype) {
		BUG_ON(msg->channel >= omap_port->sst.channels);
		queue = &omap_port->txqueue[msg->channel];
	} else {
		BUG_ON(msg->channel >= omap_port->ssr.channels);
		queue = &omap_port->rxqueue[msg->channel];
	}
	msg->status = HSI_STATUS_QUEUED;
	spin_lock_bh(&omap_port->lock);
	list_add_tail(&msg->link, queue);
	err = ssi_start_transfer(queue);
	if (err < 0) {
		list_del(&msg->link);
		msg->status = HSI_STATUS_ERROR;
	}
	spin_unlock_bh(&omap_port->lock);
	dev_dbg(&port->device, "msg status %d ttype %d ch %d\n",
				msg->status, msg->ttype, msg->channel);

	return err;
}

static void ssi_flush_queue(struct list_head *queue, struct hsi_client *cl)
{
	struct list_head *node, *tmp;
	struct hsi_msg *msg;

	list_for_each_safe(node, tmp, queue) {
		msg = list_entry(node, struct hsi_msg, link);
		if ((cl) && (cl != msg->cl))
			continue;
		list_del(node);
		pr_debug("flush queue: ch %d, msg %p len %d type %d ctxt %p\n",
			msg->channel, msg, msg->sgt.sgl->length,
					msg->ttype, msg->context);
		if (msg->destructor)
			msg->destructor(msg);
		else
			hsi_free_msg(msg);
	}
}

static int ssi_setup(struct hsi_client *cl)
{
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sst = omap_port->sst_base;
	void __iomem *ssr = omap_port->ssr_base;
	u32 div;
	u32 val;
	int err = 0;

	ssi_clk_enable(ssi);
	spin_lock_bh(&omap_port->lock);
	if (cl->tx_cfg.speed)
		omap_ssi->max_speed = cl->tx_cfg.speed;
	div = ssi_calculate_div(ssi);
	if (div > SSI_MAX_DIVISOR) {
		dev_err(&cl->device, "Invalid TX speed %d Mb/s (div %d)\n",
						cl->tx_cfg.speed, div);
		err = -EINVAL;
		goto out;
	}
	/* Set TX/RX module to sleep to stop TX/RX during cfg update */
	__raw_writel(SSI_MODE_SLEEP, sst + SSI_SST_MODE_REG);
	__raw_writel(SSI_MODE_SLEEP, ssr + SSI_SSR_MODE_REG);
	/* Flush posted write */
	val = __raw_readl(ssr + SSI_SSR_MODE_REG);
	/* TX */
	__raw_writel(31, sst + SSI_SST_FRAMESIZE_REG);
	__raw_writel(div, sst + SSI_SST_DIVISOR_REG);
	__raw_writel(cl->tx_cfg.channels, sst + SSI_SST_CHANNELS_REG);
	__raw_writel(cl->tx_cfg.arb_mode, sst + SSI_SST_ARBMODE_REG);
	__raw_writel(cl->tx_cfg.mode, sst + SSI_SST_MODE_REG);
	/* RX */
	__raw_writel(31, ssr + SSI_SSR_FRAMESIZE_REG);
	__raw_writel(cl->rx_cfg.channels, ssr + SSI_SSR_CHANNELS_REG);
	__raw_writel(0, ssr + SSI_SSR_TIMEOUT_REG);
	/* Cleanup the break queue if we leave FRAME mode */
	if ((omap_port->ssr.mode == SSI_MODE_FRAME) &&
		(cl->rx_cfg.mode != SSI_MODE_FRAME))
		ssi_flush_queue(&omap_port->brkqueue, cl);
	__raw_writel(cl->rx_cfg.mode, ssr + SSI_SSR_MODE_REG);
	omap_port->channels = max(cl->rx_cfg.channels, cl->tx_cfg.channels);
	/* Shadow registering for OFF mode */
	/* SST */
	omap_port->sst.divisor = div;
	omap_port->sst.frame_size = 31;
	omap_port->sst.channels = cl->tx_cfg.channels;
	omap_port->sst.arb_mode = cl->tx_cfg.arb_mode;
	omap_port->sst.mode = cl->tx_cfg.mode;
	/* SSR */
	omap_port->ssr.frame_size = 31;
	omap_port->ssr.timeout = 0;
	omap_port->ssr.channels = cl->rx_cfg.channels;
	omap_port->ssr.mode = cl->rx_cfg.mode;
out:
	spin_unlock_bh(&omap_port->lock);
	ssi_clk_disable(ssi);

	return err;
}

static void ssi_cleanup_queues(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	unsigned int i;
	u32 rxbufstate = 0;
	u32 txbufstate = 0;
	u32 status = SSI_ERROROCCURED;
	u32 tmp;

	ssi_flush_queue(&omap_port->brkqueue, cl);
	if (list_empty(&omap_port->brkqueue))
		status |= SSI_BREAKDETECTED;

	for (i = 0; i < omap_port->channels; i++) {
		if (list_empty(&omap_port->txqueue[i]))
			continue;
		msg = list_first_entry(&omap_port->txqueue[i], struct hsi_msg,
									link);
		if ((msg->cl == cl) && (msg->status == HSI_STATUS_PROCEEDING)) {
			txbufstate |= (1 << i);
			status |= SSI_DATAACCEPT(i);
			/* Release the clocks writes, also GDD ones */
			ssi_clk_disable(ssi);
		}
		ssi_flush_queue(&omap_port->txqueue[i], cl);
	}
	for (i = 0; i < omap_port->channels; i++) {
		if (list_empty(&omap_port->rxqueue[i]))
			continue;
		msg = list_first_entry(&omap_port->rxqueue[i], struct hsi_msg,
									link);
		if ((msg->cl == cl) && (msg->status == HSI_STATUS_PROCEEDING)) {
			rxbufstate |= (1 << i);
			status |= SSI_DATAAVAILABLE(i);
		}
		ssi_flush_queue(&omap_port->rxqueue[i], cl);
		/* Check if we keep the error detection interrupt armed */
		if (!list_empty(&omap_port->rxqueue[i]))
			status &= ~SSI_ERROROCCURED;
	}
	/* Cleanup write buffers */
	tmp = __raw_readl(omap_port->sst_base + SSI_SST_BUFSTATE_REG);
	tmp &= ~txbufstate;
	__raw_writel(tmp, omap_port->sst_base + SSI_SST_BUFSTATE_REG);
	/* Cleanup read buffers */
	tmp = __raw_readl(omap_port->ssr_base + SSI_SSR_BUFSTATE_REG);
	tmp &= ~rxbufstate;
	__raw_writel(tmp, omap_port->ssr_base + SSI_SSR_BUFSTATE_REG);
	/* Disarm and ack pending interrupts */
	tmp = __raw_readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	tmp &= ~status;
	__raw_writel(tmp, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	__raw_writel(status, omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
}

static void ssi_cleanup_gdd(struct hsi_controller *ssi, struct hsi_client *cl)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	unsigned int i;
	u32 val = 0;
	u32 tmp;

	for (i = 0; i < SSI_MAX_GDD_LCH; i++) {
		msg = omap_ssi->gdd_trn[i].msg;
		if ((!msg) || (msg->cl != cl))
			continue;
		__raw_writew(0, omap_ssi->gdd + SSI_GDD_CCR_REG(i));
		val |= (1 << i);
		/*
		 * Clock references for write will be handled in
		 * ssi_cleanup_queues
		 */
		if (msg->ttype == HSI_MSG_READ)
			ssi_clk_disable(ssi);
		omap_ssi->gdd_trn[i].msg = NULL;
	}
	tmp = __raw_readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	tmp &= ~val;
	__raw_writel(tmp, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	__raw_writel(val, omap_ssi->sys + SSI_GDD_MPU_IRQ_STATUS_REG);
}

static int ssi_release(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	spin_lock_bh(&omap_port->lock);
	ssi_clk_enable(ssi);
	/* Stop all the pending DMA requests for that client */
	ssi_cleanup_gdd(ssi, cl);
	/* Now cleanup all the queues */
	ssi_cleanup_queues(cl);
	ssi_clk_disable(ssi);
	/* If it is the last client of the port, do extra checks and cleanup */
	if (port->claimed <= 1) {
		/*
		 * Drop the clock reference for the incoming wake line
		 * if it is still kept high by the other side.
		 */
		if (omap_port->wkin_cken) {
			ssi_clk_disable(ssi);
			omap_port->wkin_cken = 0;
		}
		ssi_clk_enable(ssi);
		/* Stop any SSI TX/RX without a client */
		ssi_set_mode(ssi, SSI_MODE_SLEEP);
		omap_port->sst.mode = SSI_MODE_SLEEP;
		omap_port->ssr.mode = SSI_MODE_SLEEP;
		ssi_clk_disable(ssi);
		WARN_ON(omap_port->wk_refcount != 0);
		WARN_ON(omap_ssi->ck_refcount != 0);
	}
	spin_unlock_bh(&omap_port->lock);

	return 0;
}

static int ssi_flush(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	void __iomem *sst = omap_port->sst_base;
	void __iomem *ssr = omap_port->ssr_base;
	unsigned int i;
	u32 err;

	ssi_clk_enable(ssi);
	spin_lock_bh(&omap_port->lock);
	/* Stop all DMA transfers */
	for (i = 0; i < SSI_MAX_GDD_LCH; i++) {
		msg = omap_ssi->gdd_trn[i].msg;
		if (!msg || (port != hsi_get_port(msg->cl)))
			continue;
		__raw_writew(0, omap_ssi->gdd + SSI_GDD_CCR_REG(i));
		if (msg->ttype == HSI_MSG_READ)
			ssi_clk_disable(ssi);
		omap_ssi->gdd_trn[i].msg = NULL;
	}
	/* Flush all SST buffers */
	__raw_writel(0, sst + SSI_SST_BUFSTATE_REG);
	__raw_writel(0, sst + SSI_SST_TXSTATE_REG);
	/* Flush all SSR buffers */
	__raw_writel(0, ssr + SSI_SSR_RXSTATE_REG);
	__raw_writel(0, ssr + SSI_SSR_BUFSTATE_REG);
	/* Flush all errors */
	err = __raw_readl(ssr + SSI_SSR_ERROR_REG);
	__raw_writel(err, ssr + SSI_SSR_ERRORACK_REG);
	/* Flush break */
	__raw_writel(0, ssr + SSI_SSR_BREAK_REG);
	/* Clear interrupts */
	__raw_writel(0, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	__raw_writel(0xffffff00,
			omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	__raw_writel(0, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	__raw_writel(0xff, omap_ssi->sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	/* Dequeue all pending requests */
	for (i = 0; i < omap_port->channels; i++) {
		/* Release write clocks */
		if (!list_empty(&omap_port->txqueue[i]))
			ssi_clk_disable(ssi);
		ssi_flush_queue(&omap_port->txqueue[i], NULL);
		ssi_flush_queue(&omap_port->rxqueue[i], NULL);
	}
	ssi_flush_queue(&omap_port->brkqueue, NULL);
	spin_unlock_bh(&omap_port->lock);
	ssi_clk_disable(ssi);

	return 0;
}

static int ssi_start_tx(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(&port->device, "Wake out high %d\n", omap_port->wk_refcount);

	spin_lock_bh(&omap_port->wk_lock);
	if (omap_port->wk_refcount++) {
		spin_unlock_bh(&omap_port->wk_lock);
		return 0;
	}
	ssi_clk_enable(ssi); /* Grab clocks */
	__raw_writel(SSI_WAKE(0), omap_ssi->sys + SSI_SET_WAKE_REG(port->num));
	spin_unlock_bh(&omap_port->wk_lock);

	return 0;
}

static int ssi_stop_tx(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(&port->device, "Wake out low %d\n", omap_port->wk_refcount);

	spin_lock_bh(&omap_port->wk_lock);
	BUG_ON(!omap_port->wk_refcount);
	if (--omap_port->wk_refcount) {
		spin_unlock_bh(&omap_port->wk_lock);
		return 0;
	}
	__raw_writel(SSI_WAKE(0),
				omap_ssi->sys + SSI_CLEAR_WAKE_REG(port->num));
	ssi_clk_disable(ssi); /* Release clocks */
	spin_unlock_bh(&omap_port->wk_lock);

	return 0;
}

static void ssi_pio_complete(struct hsi_port *port, struct list_head *queue)
{
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_msg *msg;
	u32 *buf;
	u32 reg;
	u32 val;

	spin_lock(&omap_port->lock);
	msg = list_first_entry(queue, struct hsi_msg, link);
	if ((!msg->sgt.nents) || (!msg->sgt.sgl->length)) {
		msg->actual_len = 0;
		msg->status = HSI_STATUS_PENDING;
	}
	if (msg->ttype == HSI_MSG_WRITE)
		val = SSI_DATAACCEPT(msg->channel);
	else
		val = SSI_DATAAVAILABLE(msg->channel);
	if (msg->status == HSI_STATUS_PROCEEDING) {
		buf = sg_virt(msg->sgt.sgl) + msg->actual_len;
		if (msg->ttype == HSI_MSG_WRITE)
			__raw_writel(*buf, omap_port->sst_base +
					SSI_SST_BUFFER_CH_REG(msg->channel));
		 else
			*buf = __raw_readl(omap_port->ssr_base +
					SSI_SSR_BUFFER_CH_REG(msg->channel));
		dev_dbg(&port->device, "ch %d ttype %d 0x%08x\n", msg->channel,
							msg->ttype, *buf);
		msg->actual_len += sizeof(*buf);
		if (msg->actual_len >= msg->sgt.sgl->length)
			msg->status = HSI_STATUS_COMPLETED;
		/*
		 * Wait for the last written frame to be really sent before
		 * we call the complete callback
		 */
		if ((msg->status == HSI_STATUS_PROCEEDING) ||
				((msg->status == HSI_STATUS_COMPLETED) &&
					(msg->ttype == HSI_MSG_WRITE))) {
			__raw_writel(val, omap_ssi->sys +
					SSI_MPU_STATUS_REG(port->num, 0));
			spin_unlock(&omap_port->lock);

			return;
		}

	}
	/* Transfer completed at this point */
	reg = __raw_readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	if (msg->ttype == HSI_MSG_WRITE)
		ssi_clk_disable(ssi); /* Release clocks for write transfer */
	reg &= ~val;
	__raw_writel(reg, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	__raw_writel(val, omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	list_del(&msg->link);
	spin_unlock(&omap_port->lock);
	msg->complete(msg);
	ssi_transfer(omap_port, queue);
}

static void ssi_gdd_complete(struct hsi_controller *ssi, unsigned int lch)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg = omap_ssi->gdd_trn[lch].msg;
	struct hsi_port *port = to_hsi_port(msg->cl->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	unsigned int dir;
	u32 csr;
	u32 val;

	spin_lock(&omap_ssi->lock);

	val = __raw_readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	val &= ~SSI_GDD_LCH(lch);
	__raw_writel(val, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);

	if (msg->ttype == HSI_MSG_READ) {
		dir = DMA_FROM_DEVICE;
		val = SSI_DATAAVAILABLE(msg->channel);
		ssi_clk_disable(ssi);
	} else {
		dir = DMA_TO_DEVICE;
		val = SSI_DATAACCEPT(msg->channel);
		/* Keep clocks reference for write pio event */
	}
	dma_unmap_sg(&ssi->device, msg->sgt.sgl, msg->sgt.nents, dir);
	csr = __raw_readw(omap_ssi->gdd + SSI_GDD_CSR_REG(lch));
	omap_ssi->gdd_trn[lch].msg = NULL; /* release GDD lch */
	dev_dbg(&port->device, "DMA completed ch %d ttype %d\n",
				msg->channel, msg->ttype);
	spin_unlock(&omap_ssi->lock);
	if (csr & SSI_CSR_TOUR) { /* Timeout error */
		msg->status = HSI_STATUS_ERROR;
		msg->actual_len = 0;
		spin_lock(&omap_port->lock);
		list_del(&msg->link); /* Dequeue msg */
		spin_unlock(&omap_port->lock);
		msg->complete(msg);
		return;
	}
	spin_lock(&omap_port->lock);
	val |= __raw_readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	__raw_writel(val, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	spin_unlock(&omap_port->lock);

	msg->status = HSI_STATUS_COMPLETED;
	msg->actual_len = sg_dma_len(msg->sgt.sgl);
}

static void ssi_gdd_tasklet(unsigned long dev)
{
	struct hsi_controller *ssi = (struct hsi_controller *)dev;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sys = omap_ssi->sys;
	unsigned int lch;
	u32 status_reg;

	ssi_clk_enable(ssi);

	status_reg = __raw_readl(sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	for (lch = 0; lch < SSI_MAX_GDD_LCH; lch++) {
		if (status_reg & SSI_GDD_LCH(lch))
			ssi_gdd_complete(ssi, lch);
	}
	__raw_writel(status_reg, sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	status_reg = __raw_readl(sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	ssi_clk_disable(ssi);
	if (status_reg)
		tasklet_hi_schedule(&omap_ssi->gdd_tasklet);
	else
		enable_irq(omap_ssi->gdd_irq);

}

static irqreturn_t ssi_gdd_isr(int irq, void *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	tasklet_hi_schedule(&omap_ssi->gdd_tasklet);
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}

static void ssi_pio_tasklet(unsigned long ssi_port)
{
	struct hsi_port *port = (struct hsi_port *)ssi_port;
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sys = omap_ssi->sys;
	unsigned int ch;
	u32 status_reg;

	ssi_clk_enable(ssi);
	status_reg = __raw_readl(sys + SSI_MPU_STATUS_REG(port->num, 0));
	status_reg &= __raw_readl(sys + SSI_MPU_ENABLE_REG(port->num, 0));

	for (ch = 0; ch < omap_port->channels; ch++) {
		if (status_reg & SSI_DATAACCEPT(ch))
			ssi_pio_complete(port, &omap_port->txqueue[ch]);
		if (status_reg & SSI_DATAAVAILABLE(ch))
			ssi_pio_complete(port, &omap_port->rxqueue[ch]);
	}
	if (status_reg & SSI_BREAKDETECTED)
		ssi_break_complete(port);
	if (status_reg & SSI_ERROROCCURED)
		ssi_error(port);

	status_reg = __raw_readl(sys + SSI_MPU_STATUS_REG(port->num, 0));
	status_reg &= __raw_readl(sys + SSI_MPU_ENABLE_REG(port->num, 0));
	ssi_clk_disable(ssi);

	if (status_reg)
		tasklet_hi_schedule(&omap_port->pio_tasklet);
	else
		enable_irq(omap_port->irq);
}

static irqreturn_t ssi_pio_isr(int irq, void *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	tasklet_hi_schedule(&omap_port->pio_tasklet);
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}

static void ssi_wake_tasklet(unsigned long ssi_port)
{
	struct hsi_port *port = (struct hsi_port *)ssi_port;
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	if (ssi_wakein(port)) {
		/**
		 * We can have a quick High-Low-High transition in the line.
		 * In such a case if we have long interrupt latencies,
		 * we can miss the low event or get twice a high event.
		 * This workaround will avoid breaking the clock reference
		 * count when such a situation ocurrs.
		 */
		spin_lock(&omap_port->lock);
		if (!omap_port->wkin_cken) {
			omap_port->wkin_cken = 1;
			ssi_clk_enable(ssi);
		}
		spin_unlock(&omap_port->lock);
		dev_dbg(&ssi->device, "Wake in high\n");
		hsi_event(port, HSI_EVENT_START_RX);
	} else {
		dev_dbg(&ssi->device, "Wake in low\n");
		hsi_event(port, HSI_EVENT_STOP_RX);
		spin_lock(&omap_port->lock);
		if (omap_port->wkin_cken) {
			ssi_clk_disable(ssi);
			omap_port->wkin_cken = 0;
		}
		spin_unlock(&omap_port->lock);
	}
}

static irqreturn_t ssi_wake_isr(int irq __maybe_unused, void *ssi_port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(ssi_port);

	tasklet_hi_schedule(&omap_port->wake_tasklet);

	return IRQ_HANDLED;
}

static int __init ssi_port_irq(struct hsi_port *port,
						struct platform_device *pd)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct resource *irq;
	int err;

	irq = platform_get_resource(pd, IORESOURCE_IRQ, (port->num * 3) + 1);
	if (!irq) {
		dev_err(&port->device, "Port IRQ resource missing\n");
		return -ENXIO;
	}
	omap_port->irq = irq->start;
	tasklet_init(&omap_port->pio_tasklet, ssi_pio_tasklet,
							(unsigned long)port);
	err = devm_request_irq(&pd->dev, omap_port->irq, ssi_pio_isr,
						IRQF_DISABLED, irq->name, port);
	if (err < 0)
		dev_err(&port->device, "Request IRQ %d failed (%d)\n",
							omap_port->irq, err);
	return err;
}

static int __init ssi_wake_irq(struct hsi_port *port,
						struct platform_device *pd)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct resource *irq;
	int err;

	irq = platform_get_resource(pd, IORESOURCE_IRQ, (port->num * 3) + 3);
	if (!irq) {
		dev_err(&port->device, "Wake in IRQ resource missing");
		return -ENXIO;
	}
	if (irq->flags & IORESOURCE_UNSET) {
		dev_info(&port->device, "No Wake in support\n");
		omap_port->wake_irq = -1;
		return 0;
	}
	omap_port->wake_irq = irq->start;
	tasklet_init(&omap_port->wake_tasklet, ssi_wake_tasklet,
							(unsigned long)port);
	err = devm_request_irq(&pd->dev, omap_port->wake_irq, ssi_wake_isr,
		IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
							irq->name, port);
	if (err < 0)
		dev_err(&port->device, "Request Wake in IRQ %d failed %d\n",
						omap_port->wake_irq, err);
	err = enable_irq_wake(omap_port->wake_irq);
	if (err < 0)
		dev_err(&port->device, "Enable wake on the wakeline in irq %d"
				" failed %d\n", omap_port->wake_irq, err);

	return err;
}

static void __init ssi_queues_init(struct omap_ssi_port *omap_port)
{
	unsigned int ch;

	for (ch = 0; ch < SSI_MAX_CHANNELS; ch++) {
		INIT_LIST_HEAD(&omap_port->txqueue[ch]);
		INIT_LIST_HEAD(&omap_port->rxqueue[ch]);
	}
	INIT_LIST_HEAD(&omap_port->brkqueue);
}

static int __init ssi_get_iomem(struct platform_device *pd,
		unsigned int num, void __iomem **pbase, dma_addr_t *phy)
{
	struct resource *mem;
	struct resource *ioarea;
	void __iomem *base;

	mem = platform_get_resource(pd, IORESOURCE_MEM, num);
	if (!mem) {
		dev_err(&pd->dev, "IO memory region missing (%d)\n", num);
		return -ENXIO;
	}
	ioarea = devm_request_mem_region(&pd->dev, mem->start,
					resource_size(mem), dev_name(&pd->dev));
	if (!ioarea) {
		dev_err(&pd->dev, "%s IO memory region request failed\n",
								mem->name);
		return -ENXIO;
	}
	base = devm_ioremap(&pd->dev, mem->start, resource_size(mem));
	if (!base) {
		dev_err(&pd->dev, "%s IO remap failed\n", mem->name);
		return -ENXIO;
	}
	*pbase = base;

	if (phy)
		*phy = mem->start;

	return 0;
}

static int __init ssi_ports_init(struct hsi_controller *ssi,
					struct platform_device *pd)
{
	struct hsi_port *port;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct omap_ssi_port *omap_port;
	unsigned int i;
	int err;

	omap_ssi->port = devm_kzalloc(&pd->dev,
				sizeof(omap_port) * ssi->num_ports, GFP_KERNEL);
	if (!omap_ssi->port)
		return -ENOMEM;

	for (i = 0; i < ssi->num_ports; i++) {
		port = &ssi->port[i];
		omap_port = devm_kzalloc(&pd->dev, sizeof(*omap_port),
								GFP_KERNEL);
		if (!omap_port)
			return -ENOMEM;
		port->async = ssi_async;
		port->setup = ssi_setup;
		port->flush = ssi_flush;
		port->start_tx = ssi_start_tx;
		port->stop_tx = ssi_stop_tx;
		port->release = ssi_release;
		hsi_port_set_drvdata(port, omap_port);
		/* Get SST base addresses*/
		err = ssi_get_iomem(pd, ((i * 2) + 2), &omap_port->sst_base,
							&omap_port->sst_dma);
		if (err < 0)
			return err;
		/* Get SSR base addresses */
		err = ssi_get_iomem(pd, ((i * 2) + 3), &omap_port->ssr_base,
							&omap_port->ssr_dma);
		if (err < 0)
			return err;
		err = ssi_port_irq(port, pd);
		if (err < 0)
			return err;
		err = ssi_wake_irq(port, pd);
		if (err < 0)
			return err;
		ssi_queues_init(omap_port);
		spin_lock_init(&omap_port->lock);
		spin_lock_init(&omap_port->wk_lock);
		omap_port->dev = &port->device;
		omap_ssi->port[i] = omap_port;
	}

	return 0;
}

static void ssi_ports_exit(struct hsi_controller *ssi)
{
	struct omap_ssi_port *omap_port;
	unsigned int i;

	for (i = 0; i < ssi->num_ports; i++) {
		omap_port = hsi_port_drvdata(&ssi->port[i]);
		tasklet_kill(&omap_port->wake_tasklet);
		tasklet_kill(&omap_port->pio_tasklet);
	}
}

static void ssi_clk_release(struct device *dev __maybe_unused, void *res)
{
	struct ssi_clk_res *r = res;

	clk_put(r->clk);
}

static struct clk *__init ssi_devm_clk_get(struct device *dev, const char *id)
{
	struct ssi_clk_res *pclk;
	struct clk *clk;

	pclk = devres_alloc(ssi_clk_release, sizeof(*pclk), GFP_KERNEL);
	if (!pclk) {
		dev_err(dev, "Could not allocate the device resource entry\n");
		return ERR_PTR(-ENOMEM);
	}
	clk = clk_get(dev, id);
	if (IS_ERR(clk)) {
		dev_err(dev, "clock get %s failed %li\n", id, PTR_ERR(clk));
		devres_free(pclk);
	} else {
		pclk->clk = clk;
		devres_add(dev, pclk);
	}

	return clk;
}

static int __init ssi_add_controller(struct hsi_controller *ssi,
						struct platform_device *pd)
{
	struct omap_ssi_platform_data *omap_ssi_pdata = pd->dev.platform_data;
	struct omap_ssi_controller *omap_ssi;
	struct resource *irq;
	int err;

	omap_ssi = devm_kzalloc(&pd->dev, sizeof(*omap_ssi), GFP_KERNEL);
	if (!omap_ssi) {
		dev_err(&pd->dev, "not enough memory for omap ssi\n");
		return -ENOMEM;
	}
	ssi->id = pd->id;
	ssi->owner = THIS_MODULE;
	ssi->device.parent = &pd->dev;
	dev_set_name(&ssi->device, "ssi%d", ssi->id);
	hsi_controller_set_drvdata(ssi, omap_ssi);
	omap_ssi->dev = &ssi->device;
	err = ssi_get_iomem(pd, 0, &omap_ssi->sys, NULL);
	if (err < 0)
		return err;
	err = ssi_get_iomem(pd, 1, &omap_ssi->gdd, NULL);
	if (err < 0)
		return err;
	irq = platform_get_resource(pd, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pd->dev, "GDD IRQ resource missing\n");
		return -ENXIO;
	}
	omap_ssi->gdd_irq = irq->start;
	tasklet_init(&omap_ssi->gdd_tasklet, ssi_gdd_tasklet,
							(unsigned long)ssi);
	err = devm_request_irq(&pd->dev, omap_ssi->gdd_irq, ssi_gdd_isr,
						IRQF_DISABLED, irq->name, ssi);
	if (err < 0) {
		dev_err(&ssi->device, "Request GDD IRQ %d failed (%d)",
							omap_ssi->gdd_irq, err);
		return err;
	}
	err = ssi_ports_init(ssi, pd);
	if (err < 0)
		return err;
	omap_ssi->get_loss = omap_ssi_pdata->get_dev_context_loss_count;
	omap_ssi->max_speed = UINT_MAX;
	spin_lock_init(&omap_ssi->lock);
	spin_lock_init(&omap_ssi->ck_lock);
	omap_ssi->ick = ssi_devm_clk_get(&pd->dev, "ssi_ick");
	if (IS_ERR(omap_ssi->ick))
		return PTR_ERR(omap_ssi->ick);
	omap_ssi->fck = ssi_devm_clk_get(&pd->dev, "ssi_ssr_fck");
	if (IS_ERR(omap_ssi->fck))
		return PTR_ERR(omap_ssi->fck);
	err = hsi_register_controller(ssi);

	return err;
}

static int __init ssi_hw_init(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	unsigned int i;
	u32 val;
	int err;

	err = ssi_clk_enable(ssi);
	if (err < 0) {
		dev_err(&ssi->device, "Failed to enable the clocks %d\n", err);
		return err;
	}
	/* Reseting SSI controller */
	__raw_writel(SSI_SOFTRESET, omap_ssi->sys + SSI_SYSCONFIG_REG);
	val = __raw_readl(omap_ssi->sys + SSI_SYSSTATUS_REG);
	for (i = 0; ((i < 20) && !(val & SSI_RESETDONE)); i++) {
		msleep(20);
		val = __raw_readl(omap_ssi->sys + SSI_SYSSTATUS_REG);
	}
	if (!(val & SSI_RESETDONE)) {
		dev_err(&ssi->device, "SSI HW reset failed\n");
		ssi_clk_disable(ssi);
		return -EIO;
	}
	/* Reseting GDD */
	__raw_writel(SSI_SWRESET, omap_ssi->gdd + SSI_GDD_GRST_REG);
	/* Get FCK rate */
	omap_ssi->fck_rate = clk_get_rate(omap_ssi->fck) / 1000; /* KHz */
	dev_dbg(&ssi->device, "SSI fck rate %lu KHz\n", omap_ssi->fck_rate);
	/* Set default PM settings */
	val = SSI_AUTOIDLE | SSI_SIDLEMODE_SMART | SSI_MIDLEMODE_SMART;
	__raw_writel(val, omap_ssi->sys + SSI_SYSCONFIG_REG);
	omap_ssi->sysconfig = val;
	__raw_writel(SSI_CLK_AUTOGATING_ON, omap_ssi->sys + SSI_GDD_GCR_REG);
	omap_ssi->gdd_gcr = SSI_CLK_AUTOGATING_ON;
	ssi_clk_disable(ssi);

	return 0;
}

static void ssi_remove_controller(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	ssi_ports_exit(ssi);
	tasklet_kill(&omap_ssi->gdd_tasklet);
	hsi_unregister_controller(ssi);
}

static int __init ssi_probe(struct platform_device *pd)
{
	struct omap_ssi_platform_data *omap_ssi_pdata = pd->dev.platform_data;
	struct hsi_controller *ssi;
	int err;

	if (!omap_ssi_pdata) {
		dev_err(&pd->dev, "No OMAP SSI platform data\n");
		return -EINVAL;
	}
	ssi = hsi_alloc_controller(omap_ssi_pdata->num_ports, GFP_KERNEL);
	if (!ssi) {
		dev_err(&pd->dev, "No memory for controller\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pd, ssi);
	err = ssi_add_controller(ssi, pd);
	if (err < 0)
		goto out1;
	err = ssi_hw_init(ssi);
	if (err < 0)
		goto out2;
#ifdef CONFIG_DEBUG_FS
	err = ssi_debug_add_ctrl(ssi);
	if (err < 0)
		goto out2;
#endif
	return err;
out2:
	ssi_remove_controller(ssi);
out1:
	platform_set_drvdata(pd, NULL);
	hsi_free_controller(ssi);

	return err;
}

static int __exit ssi_remove(struct platform_device *pd)
{
	struct hsi_controller *ssi = platform_get_drvdata(pd);

#ifdef CONFIG_DEBUG_FS
	ssi_debug_remove_ctrl(ssi);
#endif
	ssi_remove_controller(ssi);
	platform_set_drvdata(pd, NULL);
	hsi_free_controller(ssi);

	return 0;
}

static struct platform_driver ssi_pdriver = {
	.remove	= __exit_p(ssi_remove),
	.driver	= {
			.name	= "omap_ssi",
			.owner	= THIS_MODULE,
	},
};

static int __init omap_ssi_init(void)
{
	pr_info("OMAP SSI hw driver loaded\n");
	return platform_driver_probe(&ssi_pdriver, ssi_probe);
}
module_init(omap_ssi_init);

static void __exit omap_ssi_exit(void)
{
	platform_driver_unregister(&ssi_pdriver);
	pr_info("OMAP SSI driver removed\n");
}
module_exit(omap_ssi_exit);

MODULE_ALIAS("platform:omap_ssi");
MODULE_AUTHOR("Carlos Chinea <carlos.chinea@nokia.com>");
MODULE_DESCRIPTION("Synchronous Serial Interface Driver");
MODULE_LICENSE("GPL v2");
