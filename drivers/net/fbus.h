/*
 * fbus.h -- Phonet over Fast Bus line discipline
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Author: Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#define PN_MEDIA_FBUS	0x1E
#define FBUS_MAX_FRAG	120

struct net_device;
struct sk_buff;
struct tty_struct;

/* Defragmentation interface to the line discipline */
struct net_device *fbus_create(struct tty_struct *);
void fbus_destroy(struct net_device *);
void fbus_rx(struct sk_buff *, struct net_device *);

/* Line discipline interface to the fragmentation layer */
int fbus_xmit(struct sk_buff *, struct tty_struct *);
