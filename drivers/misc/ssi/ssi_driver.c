/*
 * ssi_driver.c
 *
 * Implements SSI module interface, initialization, and PM related functions.
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

#include <linux/err.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <mach/clock.h>
#include "ssi_driver.h"

#define	SSI_DRIVER_VERSION	"1.1-rc2"
#define SSI_RESETDONE_TIMEOUT	10	/* 10 ms */
#define SSI_RESETDONE_RETRIES	20	/* => max 200 ms waiting for reset */

/* NOTE: Function called in interrupt context */
int ssi_port_event_handler(struct ssi_port *p, unsigned int event, void *arg)
{
	int ch;

	for (ch = 0; ch < p->max_ch; ch++) {
		struct ssi_channel *ssi_channel = p->ssi_channel + ch;

		read_lock(&ssi_channel->rw_lock);
		if ((ssi_channel->dev) && (ssi_channel->port_event))
			ssi_channel->port_event(ssi_channel->dev, event, arg);
		read_unlock(&ssi_channel->rw_lock);
	}

	return 0;
}

static int ssi_clk_event(struct notifier_block *nb, unsigned long event,
								void *data)
{
	switch (event) {
	case CLK_PRE_RATE_CHANGE:
		break;
	case CLK_ABORT_RATE_CHANGE:
		break;
	case CLK_POST_RATE_CHANGE:
		break;
	default:
		break;
	}
	/*
	 * TODO: At this point we may emit a port event warning about the
	 * clk frequency change to the upper layers.
	 */
	return NOTIFY_DONE;
}

static void ssi_dev_release(struct device *dev)
{
}

static int __init reg_ssi_dev_ch(struct ssi_dev *ssi_ctrl, unsigned int p,
								unsigned int ch)
{
	struct ssi_device *dev;
	struct ssi_port *port = &ssi_ctrl->ssi_port[p];
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->n_ctrl = ssi_ctrl->id;
	dev->n_p = p;
	dev->n_ch = ch;
	dev->ch = &port->ssi_channel[ch];
	dev->device.bus = &ssi_bus_type;
	dev->device.parent = ssi_ctrl->dev;
	dev->device.release = ssi_dev_release;
	if (dev->n_ctrl < 0)
		snprintf(dev->device.bus_id, sizeof(dev->device.bus_id),
			"omap_ssi-p%u.c%u", p, ch);
	else
		snprintf(dev->device.bus_id, sizeof(dev->device.bus_id),
			"omap_ssi%d-p%u.c%u", dev->n_ctrl, p, ch);

	err = device_register(&dev->device);
	if (err >= 0) {
		write_lock_bh(&port->ssi_channel[ch].rw_lock);
		port->ssi_channel[ch].dev = dev;
		write_unlock_bh(&port->ssi_channel[ch].rw_lock);
	} else {
		kfree(dev);
	}
	return err;
}

static int __init register_ssi_devices(struct ssi_dev *ssi_ctrl)
{
	int port;
	int ch;
	int err;

	for (port = 0; port < ssi_ctrl->max_p; port++)
		for (ch = 0; ch < ssi_ctrl->ssi_port[port].max_ch; ch++) {
			err = reg_ssi_dev_ch(ssi_ctrl, port, ch);
			if (err < 0)
				return err;
		}

	return 0;
}

static int __init ssi_softreset(struct ssi_dev *ssi_ctrl)
{
	int ind = 0;
	void __iomem *base = ssi_ctrl->base;
	u32 status;

	ssi_outl_or(SSI_SOFTRESET, base, SSI_SYS_SYSCONFIG_REG);

	status = ssi_inl(base, SSI_SYS_SYSSTATUS_REG);
	while ((!(status & SSI_RESETDONE)) && (ind < SSI_RESETDONE_RETRIES)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(SSI_RESETDONE_TIMEOUT));
		status = ssi_inl(base, SSI_SYS_SYSSTATUS_REG);
		ind++;
	}

	if (ind >= SSI_RESETDONE_RETRIES)
		return -EIO;

	/* Reseting GDD */
	ssi_outl_or(SSI_SWRESET, base, SSI_GDD_GRST_REG);

	return 0;
}

static void __init set_ssi_ports_default(struct ssi_dev *ssi_ctrl,
						struct platform_device *pd)
{
	struct port_ctx *cfg;
	struct ssi_platform_data *pdata = pd->dev.platform_data;
	unsigned int port = 0;
	void __iomem *base = ssi_ctrl->base;

	for (port = 1; port <= pdata->num_ports; port++) {
		cfg = &pdata->ctx.pctx[port - 1];
		ssi_outl(cfg->sst.mode, base, SSI_SST_MODE_REG(port));
		ssi_outl(cfg->sst.frame_size, base,
						SSI_SST_FRAMESIZE_REG(port));
		ssi_outl(cfg->sst.divisor, base, SSI_SST_DIVISOR_REG(port));
		ssi_outl(cfg->sst.channels, base, SSI_SST_CHANNELS_REG(port));
		ssi_outl(cfg->sst.arb_mode, base, SSI_SST_ARBMODE_REG(port));

		ssi_outl(cfg->ssr.mode, base, SSI_SSR_MODE_REG(port));
		ssi_outl(cfg->ssr.frame_size, base,
						SSI_SSR_FRAMESIZE_REG(port));
		ssi_outl(cfg->ssr.channels, base, SSI_SSR_CHANNELS_REG(port));
		ssi_outl(cfg->ssr.timeout, base, SSI_SSR_TIMEOUT_REG(port));
	}
}

static int __init ssi_port_channels_init(struct ssi_port *port)
{
	struct ssi_channel *ch;
	unsigned int ch_i;

	for (ch_i = 0; ch_i < port->max_ch; ch_i++) {
		ch = &port->ssi_channel[ch_i];
		ch->channel_number = ch_i;
		rwlock_init(&ch->rw_lock);
		ch->flags = 0;
		ch->ssi_port = port;
		ch->read_data.addr = NULL;
		ch->read_data.size = 0;
		ch->read_data.lch = -1;
		ch->write_data.addr = NULL;
		ch->write_data.size = 0;
		ch->write_data.lch = -1;
		ch->dev = NULL;
		ch->read_done = NULL;
		ch->write_done = NULL;
		ch->port_event = NULL;
	}

	return 0;
}

static void ssi_ports_exit(struct ssi_dev *ssi_ctrl, unsigned int max_ports)
{
	struct ssi_port *ssi_p;
	unsigned int port;

	for (port = 0; port < max_ports; port++) {
		ssi_p = &ssi_ctrl->ssi_port[port];
		ssi_mpu_exit(ssi_p);
		ssi_cawake_exit(ssi_p);
	}
}

static int __init ssi_request_mpu_irq(struct ssi_port *ssi_p)
{
	struct ssi_dev *ssi_ctrl = ssi_p->ssi_controller;
	struct platform_device *pd = to_platform_device(ssi_ctrl->dev);
	struct resource *mpu_irq;

	mpu_irq = platform_get_resource(pd, IORESOURCE_IRQ,
						(ssi_p->port_number - 1) * 2);
	if (!mpu_irq) {
		dev_err(ssi_ctrl->dev, "SSI misses info for MPU IRQ on"
					" port %d\n", ssi_p->port_number);
		return -ENXIO;
	}
	ssi_p->n_irq = 0; /* We only use one irq line */
	ssi_p->irq = mpu_irq->start;
	return ssi_mpu_init(ssi_p, mpu_irq->name);
}

static int __init ssi_request_cawake_irq(struct ssi_port *ssi_p)
{
	struct ssi_dev *ssi_ctrl = ssi_p->ssi_controller;
	struct platform_device *pd = to_platform_device(ssi_ctrl->dev);
	struct resource *cawake_irq;

	cawake_irq = platform_get_resource(pd, IORESOURCE_IRQ,
						4 + ssi_p->port_number);
	if (!cawake_irq) {
		dev_err(ssi_ctrl->dev, "SSI device misses info for CAWAKE"
				"IRQ on port %d\n", ssi_p->port_number);
		return -ENXIO;
	}
	if (cawake_irq->flags & IORESOURCE_UNSET) {
		dev_info(ssi_ctrl->dev, "No CAWAKE GPIO support\n");
		ssi_p->cawake_gpio = -1;
		return 0;
	}

	ssi_p->cawake_gpio_irq = cawake_irq->start;
	ssi_p->cawake_gpio = irq_to_gpio(cawake_irq->start);
	return ssi_cawake_init(ssi_p, cawake_irq->name);
}

static int __init ssi_ports_init(struct ssi_dev *ssi_ctrl)
{
	struct platform_device *pd = to_platform_device(ssi_ctrl->dev);
	struct ssi_platform_data *pdata = pd->dev.platform_data;
	struct ssi_port *ssi_p;
	unsigned int port;
	int err;

	for (port = 0; port < ssi_ctrl->max_p; port++) {
		ssi_p = &ssi_ctrl->ssi_port[port];
		ssi_p->port_number = port + 1;
		ssi_p->ssi_controller = ssi_ctrl;
		ssi_p->max_ch = max(pdata->ctx.pctx[port].sst.channels,
					pdata->ctx.pctx[port].ssr.channels);
		ssi_p->irq = 0;
		spin_lock_init(&ssi_p->lock);
		err = ssi_port_channels_init(&ssi_ctrl->ssi_port[port]);
		if (err < 0)
			goto rback1;
		err = ssi_request_mpu_irq(ssi_p);
		if (err < 0)
			goto rback2;
		err = ssi_request_cawake_irq(ssi_p);
		if (err < 0)
			goto rback3;
	}
	return 0;
rback3:
	ssi_mpu_exit(ssi_p);
rback2:
	ssi_ports_exit(ssi_ctrl, port + 1);
rback1:
	return err;
}

static int __init ssi_request_gdd_irq(struct ssi_dev *ssi_ctrl)
{
	struct platform_device *pd  = to_platform_device(ssi_ctrl->dev);
	struct resource *gdd_irq;

	gdd_irq = platform_get_resource(pd, IORESOURCE_IRQ, 4);
	if (!gdd_irq) {
		dev_err(ssi_ctrl->dev, "SSI has no GDD IRQ resource\n");
		return -ENXIO;
	}

	ssi_ctrl->gdd_irq = gdd_irq->start;
	return ssi_gdd_init(ssi_ctrl, gdd_irq->name);
}

static int __init ssi_controller_init(struct ssi_dev *ssi_ctrl,
						struct platform_device *pd)
{
	struct ssi_platform_data *pdata = pd->dev.platform_data;
	struct resource *mem, *ioarea;
	int err;

	mem = platform_get_resource(pd, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pd->dev, "SSI device does not have "
					"SSI IO memory region information\n");
		return -ENXIO;
	}

	ioarea = devm_request_mem_region(&pd->dev, mem->start,
				(mem->end - mem->start) + 1, pd->dev.bus_id);
	if (!ioarea) {
		dev_err(&pd->dev, "Unable to request SSI IO mem region\n");
		return -EBUSY;
	}

	ssi_ctrl->base = devm_ioremap(&pd->dev, mem->start,
						(mem->end - mem->start) + 1);
	if (!ssi_ctrl->base) {
		dev_err(&pd->dev, "Unable to ioremap SSI base IO address\n");
		return -ENXIO;
	}

	ssi_ctrl->id = pd->id;
	ssi_ctrl->max_p = pdata->num_ports;
	ssi_ctrl->dev = &pd->dev;
	spin_lock_init(&ssi_ctrl->lock);
	ssi_ctrl->ssi_clk = clk_get(&pd->dev, "ssi_clk");

	if (IS_ERR(ssi_ctrl->ssi_clk)) {
		dev_err(ssi_ctrl->dev, "Unable to get SSI clocks\n");
		return PTR_ERR(ssi_ctrl->ssi_clk);
	}

	if (pdata->clk_notifier_register) {
		ssi_ctrl->ssi_nb.notifier_call = ssi_clk_event;
		ssi_ctrl->ssi_nb.priority = INT_MAX; /* Let's try to be first */
		err = pdata->clk_notifier_register(ssi_ctrl->ssi_clk,
							&ssi_ctrl->ssi_nb);
		if (err < 0)
			goto rback1;
	}

	err = ssi_ports_init(ssi_ctrl);
	if (err < 0)
		goto rback2;

	err = ssi_request_gdd_irq(ssi_ctrl);
	if (err < 0)
		goto rback3;

	return 0;
rback3:
	ssi_ports_exit(ssi_ctrl, ssi_ctrl->max_p);
rback2:
	if (pdata->clk_notifier_unregister)
		pdata->clk_notifier_unregister(ssi_ctrl->ssi_clk,
							&ssi_ctrl->ssi_nb);
rback1:
	clk_put(ssi_ctrl->ssi_clk);
	dev_err(&pd->dev, "Error on ssi_controller initialization\n");
	return err;
}

static void ssi_controller_exit(struct ssi_dev *ssi_ctrl)
{
	struct ssi_platform_data *pdata = ssi_ctrl->dev->platform_data;

	ssi_gdd_exit(ssi_ctrl);
	ssi_ports_exit(ssi_ctrl, ssi_ctrl->max_p);
	if (pdata->clk_notifier_unregister)
		pdata->clk_notifier_unregister(ssi_ctrl->ssi_clk,
							&ssi_ctrl->ssi_nb);
	clk_put(ssi_ctrl->ssi_clk);
}

static int __init ssi_probe(struct platform_device *pd)
{
	struct ssi_platform_data *pdata = pd->dev.platform_data;
	struct ssi_dev *ssi_ctrl;
	u32 revision;
	int err;

	if (!pdata) {
		pr_err(LOG_NAME "No platform_data found on ssi device\n");
		return -ENXIO;
	}

	ssi_ctrl = kzalloc(sizeof(*ssi_ctrl), GFP_KERNEL);
	if (ssi_ctrl == NULL) {
		dev_err(&pd->dev, "Could not allocate memory for"
					" struct ssi_dev\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pd, ssi_ctrl);
	err = ssi_controller_init(ssi_ctrl, pd);
	if (err < 0) {
		dev_err(&pd->dev, "Could not initialize ssi controller:"
						" %d\n", err);
		goto rollback1;
	}

	clk_enable(ssi_ctrl->ssi_clk);

	err = ssi_softreset(ssi_ctrl);
	if (err < 0)
		goto rollback2;

	/* Set default PM settings */
	ssi_outl((SSI_AUTOIDLE | SSI_SIDLEMODE_SMART | SSI_MIDLEMODE_SMART),
			ssi_ctrl->base,  SSI_SYS_SYSCONFIG_REG);
	ssi_outl(SSI_CLK_AUTOGATING_ON, ssi_ctrl->base, SSI_GDD_GCR_REG);

	/* Configure SSI ports */
	set_ssi_ports_default(ssi_ctrl, pd);

	/* Gather info from registers for the driver.(REVISION) */
	revision = ssi_inl(ssi_ctrl->base, SSI_SYS_REVISION_REG);
	dev_info(ssi_ctrl->dev, "SSI Hardware REVISION %d.%d\n",
		(revision & SSI_REV_MAJOR) >> 4, (revision & SSI_REV_MINOR));

	err = ssi_debug_add_ctrl(ssi_ctrl);
	if (err < 0)
		goto rollback2;

	err = register_ssi_devices(ssi_ctrl);
	if (err < 0)
		goto rollback3;

	clk_disable(ssi_ctrl->ssi_clk);

	return err;

rollback3:
	ssi_debug_remove_ctrl(ssi_ctrl);
rollback2:
	clk_disable(ssi_ctrl->ssi_clk);
	ssi_controller_exit(ssi_ctrl);
rollback1:
	kfree(ssi_ctrl);
	return err;
}

static void __exit unregister_ssi_devices(struct ssi_dev *ssi_ctrl)
{
	struct ssi_port *ssi_p;
	struct ssi_device *device;
	unsigned int port;
	unsigned int ch;

	for (port = 0; port < ssi_ctrl->max_p; port++) {
		ssi_p = &ssi_ctrl->ssi_port[port];
		for (ch = 0; ch < ssi_p->max_ch; ch++) {
			device = ssi_p->ssi_channel[ch].dev;
			ssi_close(device);
			device_unregister(&device->device);
			kfree(device);
		}
	}
}

static int __exit ssi_remove(struct platform_device *pd)
{
	struct ssi_dev *ssi_ctrl = platform_get_drvdata(pd);

	if (!ssi_ctrl)
		return 0;

	unregister_ssi_devices(ssi_ctrl);
	ssi_debug_remove_ctrl(ssi_ctrl);
	ssi_controller_exit(ssi_ctrl);
	kfree(ssi_ctrl);

	return 0;
}

static struct platform_driver ssi_pdriver = {
	.probe = ssi_probe,
	.remove = __exit_p(ssi_remove),
	.driver = {
		.name = "omap_ssi",
		.owner = THIS_MODULE,
	}
};

static int __init ssi_driver_init(void)
{
	int err = 0;

	pr_info("SSI DRIVER Version " SSI_DRIVER_VERSION "\n");

	ssi_bus_init();
	err = ssi_debug_init();
	if (err < 0) {
		pr_err(LOG_NAME "SSI Debugfs failed %d\n", err);
		goto rback1;
	}
	err = platform_driver_probe(&ssi_pdriver, ssi_probe);
	if (err < 0) {
		pr_err(LOG_NAME "Platform DRIVER register FAILED: %d\n", err);
		goto rback2;
	}

	return 0;
rback2:
	ssi_debug_exit();
rback1:
	ssi_bus_exit();
	return err;
}

static void __exit ssi_driver_exit(void)
{
	platform_driver_unregister(&ssi_pdriver);
	ssi_debug_exit();
	ssi_bus_exit();

	pr_info("SSI DRIVER removed\n");
}

module_init(ssi_driver_init);
module_exit(ssi_driver_exit);

MODULE_ALIAS("platform:omap_ssi");
MODULE_AUTHOR("Carlos Chinea / Nokia");
MODULE_DESCRIPTION("Synchronous Serial Interface Driver");
MODULE_LICENSE("GPL");
