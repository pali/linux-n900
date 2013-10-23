/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2002-2003 Ericsson, Inc
 * Copyright (c) 2003-2004 International Business Machines <serue@us.ibm.com>
 * Copyright (C) 2008-2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Markku Kylänpää <ext-markku.kylanpaa@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Authors: Serge Hallyn, 2004
 *          Markku Kylänpää, 2008-2010
 */

/*
 * This file is a header file for verification caching.
 */

#ifndef AEGIS_VALIDATOR_CACHE_H
#define AEGIS_VALIDATOR_CACHE_H

/* Cache operations */
int validator_cache_init(void);
int validator_cache_contains(struct inode *inode, long *src_id);
int validator_cache_add(struct inode *inode, long src_id);
void validator_cache_remove(struct inode *inode);
void validator_cache_fsremove(dev_t dev);
void validator_cache_cleanup(void);

/* Securityfs entry operations for cache */
struct dentry *validator_cache_fsinit(struct dentry *top);
struct dentry *validator_cache_flush_fsinit(struct dentry *top);
void validator_cache_fscleanup(struct dentry *f);
void validator_cache_flush_fscleanup(struct dentry *f);

#endif /* AEGIS_VALIDATOR_CACHE_H */
