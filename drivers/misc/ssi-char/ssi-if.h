/*
 * ssi-if.h
 *
 * Part of the SSI character driver.
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
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


#ifndef _SSI_IF_H
#define _SSI_IF_H

#define SSI_EV_MASK		(0xffff << 0)
#define SSI_EV_TYPE_MASK	(0x0f << 16)
#define SSI_EV_IN		(0x01 << 16)
#define SSI_EV_OUT		(0x02 << 16)
#define SSI_EV_EXCEP		(0x03 << 16)
#define SSI_EV_AVAIL		(0x04 << 16)
#define SSI_EV_TYPE(event)	((event) & SSI_EV_TYPE_MASK)

#define SSI_HWBREAK		1
#define SSI_ERROR		2

struct ssi_event {
    unsigned int event;
    u32 *data;
    unsigned int count;
};

int if_ssi_init(unsigned int port, unsigned int *channels_map);
int if_ssi_exit(void);

int if_ssi_start(int ch);
void if_ssi_stop(int ch);

void if_ssi_send_break(int ch);
void if_ssi_flush_rx(int ch);
void if_ssi_flush_tx(int ch);
void if_ssi_set_wakeline(int ch, unsigned int state);
void if_ssi_get_wakeline(int ch, unsigned int *state);
int if_ssi_set_rx(int ch, struct ssi_rx_config *cfg);
void if_ssi_get_rx(int ch, struct ssi_rx_config *cfg);
int if_ssi_set_tx(int ch, struct ssi_tx_config *cfg);
void if_ssi_get_tx(int ch, struct ssi_tx_config *cfg);

int if_ssi_read(int ch, u32 *data, unsigned int count);
int if_ssi_poll(int ch);
int if_ssi_write(int ch, u32 *data, unsigned int count);

void if_ssi_cancel_read(int ch);
void if_ssi_cancel_write(int ch);

#endif /* _SSI_IF_H */
