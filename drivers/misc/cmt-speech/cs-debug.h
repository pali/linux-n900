/*
 *  cs-debug.h
 *
 * Part of the CMT speech driver. Debug.
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 * Original author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
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


#ifndef _CS_DEBUG_H
#define _CS_DEBUG_H

#ifdef CONFIG_SSI_CMT_SPEECH_DEBUG
#define DPRINTK(fmt, arg...) printk(KERN_DEBUG "%s(): " fmt, __func__, ##arg)
#define DENTER()	printk(KERN_DEBUG "ENTER %s()\n", __func__)
#define DLEAVE(a)	printk(KERN_DEBUG "LEAVE %s() %d\n", __func__, a)
#else
#define DPRINTK(fmt, arg...)	while (0)
#define DENTER()		while (0)
#define DLEAVE(a)		while (0)
#endif

#endif /* _CS_DEBUG_H */
