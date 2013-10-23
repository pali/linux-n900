/*
 * ssi_driver_debugfs.c
 *
 * Implements SSI debugfs.
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
#include <linux/debugfs.h>
#include "ssi_driver.h"

#define SSI_DIR_NAME_SIZE	64

static struct dentry *ssi_dir;

static int ssi_debug_show(struct seq_file *m, void *p)
{
	struct ssi_dev *ssi_ctrl = m->private;

	clk_enable(ssi_ctrl->ssi_clk);

	seq_printf(m, "REVISION\t: 0x%08x\n",
				ssi_inl(ssi_ctrl->base, SSI_SYS_REVISION_REG));
	seq_printf(m, "SYSCONFIG\t: 0x%08x\n",
				ssi_inl(ssi_ctrl->base, SSI_SYS_SYSCONFIG_REG));
	seq_printf(m, "SYSSTATUS\t: 0x%08x\n",
				ssi_inl(ssi_ctrl->base, SSI_SYS_SYSSTATUS_REG));

	clk_disable(ssi_ctrl->ssi_clk);
	return 0;
}

static int ssi_debug_port_show(struct seq_file *m, void *p)
{
	struct ssi_port *ssi_port = m->private;
	struct ssi_dev *ssi_ctrl = ssi_port->ssi_controller;
	void __iomem *base = ssi_ctrl->base;
	unsigned int port = ssi_port->port_number;
	int ch;

	clk_enable(ssi_ctrl->ssi_clk);

	if (ssi_port->cawake_gpio >= 0)
		seq_printf(m, "CAWAKE\t\t: %d\n", ssi_cawake(ssi_port));

	seq_printf(m, "WAKE\t\t: 0x%08x\n",
					ssi_inl(base, SSI_SYS_WAKE_REG(port)));
	seq_printf(m, "MPU_ENABLE_IRQ%d\t: 0x%08x\n", ssi_port->n_irq,
		ssi_inl(base, SSI_SYS_MPU_ENABLE_REG(port, ssi_port->n_irq)));
	seq_printf(m, "MPU_STATUS_IRQ%d\t: 0x%08x\n", ssi_port->n_irq,
		ssi_inl(base, SSI_SYS_MPU_STATUS_REG(port, ssi_port->n_irq)));
	/* SST */
	seq_printf(m, "\nSST\n===\n");
	seq_printf(m, "MODE\t\t: 0x%08x\n",
					ssi_inl(base, SSI_SST_MODE_REG(port)));
	seq_printf(m, "FRAMESIZE\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_FRAMESIZE_REG(port)));
	seq_printf(m, "DIVISOR\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_DIVISOR_REG(port)));
	seq_printf(m, "CHANNELS\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_CHANNELS_REG(port)));
	seq_printf(m, "ARBMODE\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_ARBMODE_REG(port)));
	seq_printf(m, "TXSTATE\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_TXSTATE_REG(port)));
	seq_printf(m, "BUFSTATE\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_BUFSTATE_REG(port)));
	seq_printf(m, "BREAK\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SST_BREAK_REG(port)));
	for (ch = 0; ch < ssi_port->max_ch; ch++) {
		seq_printf(m, "BUFFER_CH%d\t: 0x%08x\n", ch,
				ssi_inl(base, SSI_SST_BUFFER_CH_REG(port, ch)));
	}
	/* SSR */
	seq_printf(m, "\nSSR\n===\n");
	seq_printf(m, "MODE\t\t: 0x%08x\n",
					ssi_inl(base, SSI_SSR_MODE_REG(port)));
	seq_printf(m, "FRAMESIZE\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_FRAMESIZE_REG(port)));
	seq_printf(m, "CHANNELS\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_CHANNELS_REG(port)));
	seq_printf(m, "TIMEOUT\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_TIMEOUT_REG(port)));
	seq_printf(m, "RXSTATE\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_RXSTATE_REG(port)));
	seq_printf(m, "BUFSTATE\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_BUFSTATE_REG(port)));
	seq_printf(m, "BREAK\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_BREAK_REG(port)));
	seq_printf(m, "ERROR\t\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_ERROR_REG(port)));
	seq_printf(m, "ERRORACK\t: 0x%08x\n",
				ssi_inl(base, SSI_SSR_ERRORACK_REG(port)));
	for (ch = 0; ch < ssi_port->max_ch; ch++) {
		seq_printf(m, "BUFFER_CH%d\t: 0x%08x\n", ch,
				ssi_inl(base, SSI_SSR_BUFFER_CH_REG(port, ch)));
	}
	clk_disable(ssi_ctrl->ssi_clk);
	return 0;
}

static int ssi_debug_gdd_show(struct seq_file *m, void *p)
{
	struct ssi_dev *ssi_ctrl = m->private;
	void __iomem *base = ssi_ctrl->base;
	int lch;

	clk_enable(ssi_ctrl->ssi_clk);

	seq_printf(m, "GDD_MPU_STATUS\t: 0x%08x\n",
				ssi_inl(base, SSI_SYS_GDD_MPU_IRQ_STATUS_REG));
	seq_printf(m, "GDD_MPU_ENABLE\t: 0x%08x\n\n",
				ssi_inl(base, SSI_SYS_GDD_MPU_IRQ_ENABLE_REG));

	seq_printf(m, "HW_ID\t\t: 0x%08x\n", ssi_inl(base, SSI_GDD_HW_ID_REG));
	seq_printf(m, "PPORT_ID\t: 0x%08x\n",
					ssi_inl(base, SSI_GDD_PPORT_ID_REG));
	seq_printf(m, "MPORT_ID\t: 0x%08x\n",
					ssi_inl(base, SSI_GDD_MPORT_ID_REG));
	seq_printf(m, "TEST\t\t: 0x%08x\n", ssi_inl(base, SSI_GDD_TEST_REG));
	seq_printf(m, "GCR\t\t: 0x%08x\n", ssi_inl(base, SSI_GDD_GCR_REG));

	for (lch = 0; lch < SSI_NUM_LCH; lch++) {
		seq_printf(m, "\nGDD LCH %d\n=========\n", lch);
		seq_printf(m, "CSDP\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CSDP_REG(lch)));
		seq_printf(m, "CCR\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CCR_REG(lch)));
		seq_printf(m, "CICR\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CICR_REG(lch)));
		seq_printf(m, "CSR\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CSR_REG(lch)));
		seq_printf(m, "CSSA\t\t: 0x%08x\n",
					ssi_inl(base, SSI_GDD_CSSA_REG(lch)));
		seq_printf(m, "CDSA\t\t: 0x%08x\n",
					ssi_inl(base, SSI_GDD_CDSA_REG(lch)));
		seq_printf(m, "CEN\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CEN_REG(lch)));
		seq_printf(m, "CSAC\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CSAC_REG(lch)));
		seq_printf(m, "CDAC\t\t: 0x%04x\n",
					ssi_inw(base, SSI_GDD_CDAC_REG(lch)));
		seq_printf(m, "CLNK_CTRL\t: 0x%04x\n",
				ssi_inw(base, SSI_GDD_CLNK_CTRL_REG(lch)));
	}

	clk_disable(ssi_ctrl->ssi_clk);
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

int __init ssi_debug_add_ctrl(struct ssi_dev *ssi_ctrl)
{
	struct platform_device *pdev = to_platform_device(ssi_ctrl->dev);
	unsigned char dir_name[SSI_DIR_NAME_SIZE];
	struct dentry *dir;
	unsigned int port;

	if (pdev->id < 0) {
		ssi_ctrl->dir = debugfs_create_dir(pdev->name, ssi_dir);
	} else {
		snprintf(dir_name, sizeof(dir_name), "%s%d", pdev->name,
								pdev->id);
		ssi_ctrl->dir = debugfs_create_dir(dir_name, ssi_dir);
	}
	if (IS_ERR(ssi_ctrl->dir))
		return PTR_ERR(ssi_ctrl->dir);

	debugfs_create_file("regs", S_IRUGO, ssi_ctrl->dir, ssi_ctrl,
								&ssi_regs_fops);

	for (port = 0; port < ssi_ctrl->max_p; port++) {
		snprintf(dir_name, sizeof(dir_name), "port%d", port + 1);
		dir = debugfs_create_dir(dir_name, ssi_ctrl->dir);
		if (IS_ERR(dir))
			goto rback;
		debugfs_create_file("regs", S_IRUGO, dir,
				&ssi_ctrl->ssi_port[port], &ssi_port_regs_fops);
	}

	dir = debugfs_create_dir("gdd", ssi_ctrl->dir);
	if (IS_ERR(dir))
		goto rback;
	debugfs_create_file("regs", S_IRUGO, dir, ssi_ctrl, &ssi_gdd_regs_fops);

	return 0;
rback:
	debugfs_remove_recursive(ssi_ctrl->dir);
	return PTR_ERR(dir);
}

void ssi_debug_remove_ctrl(struct ssi_dev *ssi_ctrl)
{
	debugfs_remove_recursive(ssi_ctrl->dir);
}

int __init ssi_debug_init(void)
{
	ssi_dir = debugfs_create_dir("ssi", NULL);
	if (IS_ERR(ssi_dir))
		return PTR_ERR(ssi_dir);

	return 0;
}

void ssi_debug_exit(void)
{
	debugfs_remove_recursive(ssi_dir);
}
