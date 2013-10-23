/*
 * ssi_driver.h
 *
 * Header file for the SSI driver low level interface.
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

#ifndef __SSI_DRIVER_H__
#define __SSI_DRIVER_H__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/notifier.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <mach/ssi.h>
#include <linux/ssi_driver_if.h>

/* Channel states */
#define	SSI_CH_OPEN		0x01
#define SSI_CH_RX_POLL		0x10

/*
 * The number of channels to use by the driver in the ports, or the highest
 * port channel number (+1) used. (MAX:8)
 */
#define SSI_PORT_MAX_CH		4
/* Number of logical channel in GDD */
#define SSI_NUM_LCH		8

#define LOG_NAME		"OMAP SSI: "

/**
 * struct ssi_data - SSI buffer descriptor
 * @addr: pointer to the buffer where to send or receive data
 * @size: size in words (32 bits) of the buffer
 * @lch: associated GDD (DMA) logical channel number, if any
 */
struct ssi_data {
	u32 *addr;
	unsigned int size;
	int lch;
};

/**
 * struct ssi_channel - SSI channel data
 * @read_data: Incoming SSI buffer descriptor
 * @write_data: Outgoing SSI buffer descriptor
 * @ssi_port: Reference to port where the channel belongs to
 * @flags: Tracks if channel has been open FIXME
 * @channel_number: SSI channel number
 * @rw_lock: Read/Write lock to serialize access to callback and ssi_device
 * @dev: Reference to the associated ssi_device channel
 * @write_done: Callback to signal TX completed.
 * @read_done: Callback to signal RX completed.
 * @port_event: Callback to signal port events (RX Error, HWBREAK, CAWAKE ...)
 */
struct ssi_channel {
	struct ssi_data read_data;
	struct ssi_data write_data;
	struct ssi_port *ssi_port;
	u8 flags;
	u8 channel_number;
	rwlock_t rw_lock;
	struct ssi_device *dev;
	void (*write_done) (struct ssi_device *dev);
	void (*read_done) (struct ssi_device *dev);
	void (*port_event)(struct ssi_device *dev, unsigned int event,
								void *arg);
};

/**
 * struct ssi_port - ssi port driver data
 * @ssi_channel: Array of channels in the port
 * @ssi_controller: Reference to the SSI controller
 * @port_number: port number
 * @max_ch: maximum number of channels enabled in the port
 * @n_irq: SSI irq line use to handle interrupts (0 or 1)
 * @irq: IRQ number
 * @cawake_gpio: GPIO number for cawake line (-1 if none)
 * @cawake_gpio_irq: IRQ number for cawake gpio events
 * @lock: Serialize access to the port registers and internal data
 * @ssi_tasklet: Bottom half for interrupts
 * @cawake_tasklet: Bottom half for cawake events
 */
struct ssi_port {
	struct ssi_channel ssi_channel[SSI_PORT_MAX_CH];
	struct ssi_dev *ssi_controller;
	u8 flags;
	u8 port_number;
	u8 max_ch;
	u8 n_irq;
	int irq;
	int cawake_gpio;
	int cawake_gpio_irq;
	spinlock_t lock;
	struct tasklet_struct ssi_tasklet;
	struct tasklet_struct cawake_tasklet;
};

/**
 * struct ssi_dev - ssi controller driver data
 * @ssi_port: Array of ssi ports enabled in the controller
 * @id: SSI controller platform id number
 * @max_p: Number of ports enabled in the controller
 * @ssi_clk: Reference to the SSI custom clock
 * @base: SSI registers base virtual address
 * @lock: Serializes access to internal data and regs
 * @cawake_clk_enable: Tracks if a cawake event has enable the clocks
 * @gdd_irq: GDD (DMA) irq number
 * @gdd_usecount: Holds the number of ongoning DMA transfers
 * @last_gdd_lch: Last used GDD logical channel
 * @set_min_bus_tput: (PM) callback to set minimun bus throuput
 * @clk_notifier_register: (PM) callabck for DVFS support
 * @clk_notifier_unregister: (PM) callabck for DVFS support
 * @ssi_nb: (PM) Notification block for DVFS notification chain
 * @ssi_gdd_tasklet: Bottom half for DMA transfers
 * @dir: debugfs base directory
 * @dev: Reference to the SSI platform device
 */
struct ssi_dev {
	struct ssi_port ssi_port[SSI_MAX_PORTS];
	int id;
	u8 max_p;
	struct clk *ssi_clk;
	void __iomem *base;
	spinlock_t lock;
	unsigned int cawake_clk_enable:1;
	int gdd_irq;
	unsigned int gdd_usecount;
	unsigned int last_gdd_lch;
	void (*set_min_bus_tput)(struct device *dev, u8 agent_id,
						unsigned long r);
	int (*clk_notifier_register)(struct clk *clk,
					struct notifier_block *nb);
	int (*clk_notifier_unregister)(struct clk *clk,
					struct notifier_block *nb);
	struct notifier_block ssi_nb;
	struct tasklet_struct ssi_gdd_tasklet;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dir;
#endif
	struct device *dev;
};

/* SSI Bus */
extern struct bus_type ssi_bus_type;

int ssi_port_event_handler(struct ssi_port *p, unsigned int event, void *arg);
int ssi_bus_init(void);
void ssi_bus_exit(void);
/* End SSI Bus */

void ssi_reset_ch_read(struct ssi_channel *ch);
void ssi_reset_ch_write(struct ssi_channel *ch);

int ssi_driver_read_interrupt(struct ssi_channel *ssi_channel, u32 *data);
int ssi_driver_write_interrupt(struct ssi_channel *ssi_channel, u32 *data);
int ssi_driver_read_dma(struct ssi_channel *ssi_channel, u32 *data,
			unsigned int count);
int ssi_driver_write_dma(struct ssi_channel *ssi_channel, u32 *data,
			unsigned int count);

void ssi_driver_cancel_write_interrupt(struct ssi_channel *ch);
void ssi_driver_cancel_read_interrupt(struct ssi_channel *ch);
void ssi_driver_cancel_write_dma(struct ssi_channel *ch);
void ssi_driver_cancel_read_dma(struct ssi_channel *ch);

int ssi_mpu_init(struct ssi_port *ssi_p, const char *irq_name);
void ssi_mpu_exit(struct ssi_port *ssi_p);

int ssi_gdd_init(struct ssi_dev *ssi_ctrl, const char *irq_name);
void ssi_gdd_exit(struct ssi_dev *ssi_ctrl);

int ssi_cawake_init(struct ssi_port *port, const char *irq_name);
void ssi_cawake_exit(struct ssi_port *port);


#ifdef CONFIG_DEBUG_FS
int ssi_debug_init(void);
void ssi_debug_exit(void);
int ssi_debug_add_ctrl(struct ssi_dev *ssi_ctrl);
void ssi_debug_remove_ctrl(struct ssi_dev *ssi_ctrl);
#else
#define	ssi_debug_add_ctrl(ssi_ctrl)	0
#define	ssi_debug_remove_ctrl(ssi_ctrl)
#define	ssi_debug_init()		0
#define	ssi_debug_exit()
#endif /* CONFIG_DEBUG_FS */

static inline unsigned int ssi_cawake(struct ssi_port *port)
{
	return gpio_get_value(port->cawake_gpio);
}

static inline struct ssi_channel *ctrl_get_ch(struct ssi_dev *ssi_ctrl,
					unsigned int port, unsigned int channel)
{
	return &ssi_ctrl->ssi_port[port - 1].ssi_channel[channel];
}

/* SSI IO access */
static inline u32 ssi_inl(void __iomem *base, u32 offset)
{
	return inl(base + offset);
}

static inline void ssi_outl(u32 data, void __iomem *base, u32 offset)
{
	outl(data, base + offset);
}

static inline void ssi_outl_or(u32 data, void __iomem *base, u32 offset)
{
	u32 tmp = ssi_inl(base, offset);
	ssi_outl((tmp | data), base, offset);
}

static inline void ssi_outl_and(u32 data, void __iomem *base, u32 offset)
{
	u32 tmp = ssi_inl(base, offset);
	ssi_outl((tmp & data), base, offset);
}

static inline u16 ssi_inw(void __iomem *base, u32 offset)
{
	return inw(base + offset);
}

static inline void ssi_outw(u16 data, void __iomem *base, u32 offset)
{
	outw(data, base + offset);
}

static inline void ssi_outw_or(u16 data, void __iomem *base, u32 offset)
{
	u16 tmp = ssi_inw(base, offset);
	ssi_outw((tmp | data), base, offset);
}

static inline void ssi_outw_and(u16 data, void __iomem *base, u32 offset)
{
	u16 tmp = ssi_inw(base, offset);
	ssi_outw((tmp & data), base, offset);
}

#endif /* __SSI_DRIVER_H__ */
