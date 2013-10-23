/*
 * This file is part of ECI (Enhancement Control Interface) driver
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Tapio Vihuri <tapio.vihuri@nokia.com>
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
 *
 */
#ifndef __ECI_H__
#define __ECI_H__

#include <plat/dfl61-audio.h>

#define ECI_MAX_MEM_SIZE	0x7c
#define ECI_BUTTON_BUF_SIZE	32
#define ECI_MAX_FEATURE_COUNT	31

struct eci_hw_ops {
	int (*acc_reset)(void);
	int (*acc_read_direct)(u8 addr, char *buf);
	int (*acc_read_reg)(u8 reg, u8 *buf, int count);
	int (*acc_write_reg)(u8 reg, u8 param);
	void (*aci_eci_buttons)(int enable);
};

struct aci_cb {
	void *priv;
	void (*button_event)(bool button_down, void *priv);
};

struct eci_cb {
	void *priv;
	void (*event)(int event, void *priv);
};

struct eci_platform_data {
	void (*register_hsmic_event_cb)(struct dfl61audio_hsmic_event *);
	void (*jack_report) (int status);
};

struct enchancement_features_fixed {
	u8	block_id;
	u8	length;
	u8	connector_conf;
	u8	number_of_features;
};

struct enchancement_features_variable {
	u8	*io_support;
	u8	*io_functionality;
	u8	*active_state;
};

struct eci_buttons_data {
	u32	buttons;
	int	windex;
	int	rindex;
	u32	buttons_up_mask;
	u32	buttons_buf[ECI_BUTTON_BUF_SIZE];
};

struct eci_data {
	struct device                           *dev;
	struct delayed_work                     eci_ws;
	wait_queue_head_t                       wait;
	struct input_dev                        *acc_input;
	int                                     event;
	bool                                    mem_ok;
	u16                                     mem_size;
	u8                                      memory[ECI_MAX_MEM_SIZE];
	struct enchancement_features_fixed      *e_features_fix;
	struct enchancement_features_variable   e_features_var;
	u8					port_reg_count;
	struct eci_buttons_data			buttons_data;
	struct eci_hw_ops                       *eci_hw_ops;
	u8					mic_state;

	void (*jack_report) (int status);
};

struct aci_cb *aci_register(void);
struct eci_cb *eci_register(struct eci_hw_ops *eci_ops);
#endif
