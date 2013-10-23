/*
 * ssi_driver_if.h
 *
 * Header for the SSI driver low level interface.
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
#ifndef __SSI_H__
#define __SSI_H__

#include <linux/device.h>
#include <linux/clk.h>
#include <linux/notifier.h>

/* The number of ports handled by the driver. (MAX:2) */
#define SSI_MAX_PORTS		1

#define SSI_MAX_FRAME_SIZE	0x1f
#define SSI_MAX_RX_TIMEOUT	0x1ff
#define SSI_MAX_TX_DIVISOR	0x7f

#define ANY_SSI_CONTROLLER	-1
#define ANY_CHANNEL		-1
#define CHANNEL(channel)	(1 << (channel))

enum {
	SSI_EVENT_BREAK_DETECTED = 0,
	SSI_EVENT_ERROR,
	SSI_EVENT_PRE_SPEED_CHANGE,
	SSI_EVENT_POST_SPEED_CHANGE,
	SSI_EVENT_CAWAKE_UP,
	SSI_EVENT_CAWAKE_DOWN,
	SSI_EVENT_SSR_DATAAVAILABLE,
};

enum {
	SSI_IOCTL_WAKE_UP,
	SSI_IOCTL_WAKE_DOWN,
	SSI_IOCTL_SEND_BREAK,
	SSI_IOCTL_WAKE,
	SSI_IOCTL_FLUSH_RX,
	SSI_IOCTL_FLUSH_TX,
	SSI_IOCTL_CAWAKE,
	SSI_IOCTL_SET_RX,
	SSI_IOCTL_GET_RX,
	SSI_IOCTL_SET_TX,
	SSI_IOCTL_GET_TX,
	SSI_IOCTL_TX_CH_FULL,
	SSI_IOCTL_CH_DATAACCEPT,
};

/* Forward references */
struct ssi_device;
struct ssi_channel;

/* DPS */
struct sst_ctx {
	u32 mode;
	u32 frame_size;
	u32 divisor;
	u32 arb_mode;
	u32 channels;
};

struct ssr_ctx {
	u32 mode;
	u32 frame_size;
	u32 timeout;
	u32 channels;
};

struct port_ctx {
	u32 sys_mpu_enable[2];
	struct sst_ctx sst;
	struct ssr_ctx ssr;
};

/**
 *	struct ctrl_ctx - ssi controller regs context
 *	@loss_count: ssi last loss count
 *	@sysconfig: keeps sysconfig reg state
 *	@pctx: array of port context
 */
struct ctrl_ctx {
	int loss_count;
	u32 sysconfig;
	u32 gdd_gcr;
	struct port_ctx *pctx;
};
/* END DPS */

struct ssi_platform_data {
	void (*set_min_bus_tput)(struct device *dev, u8 agent_id,
							unsigned long r);
	int (*clk_notifier_register)(struct clk *clk,
						struct notifier_block *nb);
	int (*clk_notifier_unregister)(struct clk *clk,
						struct notifier_block *nb);
	u8 num_ports;
	struct ctrl_ctx ctx; /* FIXME */
};

struct ssi_device {
	int n_ctrl;
	unsigned int n_p;
	unsigned int n_ch;
	char modalias[BUS_ID_SIZE];
	struct ssi_channel *ch;
	struct device device;
};

#define to_ssi_device(dev)	container_of(dev, struct ssi_device, device)

struct ssi_device_driver {
	unsigned long		ctrl_mask;
	unsigned long		ch_mask[SSI_MAX_PORTS];
	void			(*port_event) (struct ssi_device *dev,
						unsigned int event, void *arg);
	int			(*probe)(struct ssi_device *dev);
	int			(*remove)(struct ssi_device *dev);
	int			(*suspend)(struct ssi_device *dev,
						pm_message_t mesg);
	int			(*resume)(struct ssi_device *dev);
	struct device_driver	driver;
};

#define to_ssi_device_driver(drv) container_of(drv, \
						struct ssi_device_driver, \
						driver)

int register_ssi_driver(struct ssi_device_driver *driver);
void unregister_ssi_driver(struct ssi_device_driver *driver);
int ssi_open(struct ssi_device *dev);
int ssi_write(struct ssi_device *dev, u32 *data, unsigned int count);
void ssi_write_cancel(struct ssi_device *dev);
int ssi_read(struct ssi_device *dev, u32 *data, unsigned int w_count);
void ssi_read_cancel(struct ssi_device *dev);
int ssi_poll(struct ssi_device *dev);
int ssi_ioctl(struct ssi_device *dev, unsigned int command, void *arg);
void ssi_close(struct ssi_device *dev);
void ssi_set_read_cb(struct ssi_device *dev,
				void (*read_cb)(struct ssi_device *dev));
void ssi_set_write_cb(struct ssi_device *dev,
				void (*write_cb)(struct ssi_device *dev));
void ssi_set_port_event_cb(struct ssi_device *dev,
				void (*port_event_cb)(struct ssi_device *dev,
						unsigned int event, void *arg));
#endif /* __SSI_H__ */
