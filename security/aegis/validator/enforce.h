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
 * Authors: Markku Kylänpää, 2008-2010
 */

/*
 * This file contains functions that can be used to turn off/on Aegis
 * validator enforcement functionality. When enforcement is turned off the
 * system runs in permissive mode.
 */

#ifndef AEGIS_VALIDATOR_ENFORCE_H
#define AEGIS_VALIDATOR_ENFORCE_H

struct dentry *validator_func_enforce_fsinit(struct dentry *top);
struct dentry *validator_func_enable_fsinit(struct dentry *top);
struct dentry *validator_devorig_fsinit(struct dentry *top);
void validator_func_enforce_fscleanup(struct dentry *f);
void validator_func_enable_fscleanup(struct dentry *f);

#endif /* AEGIS_VALIDATOR_ENFORCE_H */
