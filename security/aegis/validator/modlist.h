/*
 * This file is part of Aegis Validator
 *
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
 * Authors: Markku Kylänpää, 2010
 */

/*
 * This file is a header file for the reference hashlist implementation that
 * is used to verify integrity of executable content as an alternative to RSA
 * signature.
 */

#ifndef AEGIS_VALIDATOR_MODLIST_H
#define AEGIS_VALIDATOR_MODLIST_H

/* Kernel module white list operations */
int validator_modlist_entry(unsigned char *hash);
void validator_modlist_delete(void *ptr);
int validator_kmod_check(const void *buf, unsigned long len);

/* Securityfs entry operations for hashlist */
struct dentry *validator_modlist_fsinit(struct dentry *top);

#endif /* AEGIS_VALIDATOR_MODLIST_H */
