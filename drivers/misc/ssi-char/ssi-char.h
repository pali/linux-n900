/*
 * ssi-char.h
 *
 * Part of the SSI character device driver.
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


#ifndef _SSI_CHAR_H
#define _SSI_CHAR_H

#include "ssi-if.h"

/* how many char devices would be created at most */
#define SSI_MAX_CHAR_DEVS	8

void if_notify(int ch, struct ssi_event *ev);

#endif /* _SSI_CHAR_H */
