/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2002-2003 Ericsson, Inc
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
 * This file contains the header file for securityfs functions
 */

#ifndef AEGIS_VALIDATOR_FS_H
#define AEGIS_VALIDATOR_FS_H

enum aegis_fs_access_t {
	AEGIS_FS_ENFORCE_READ,
	AEGIS_FS_ENFORCE_WRITE,
	AEGIS_FS_ENABLE_READ,
	AEGIS_FS_ENABLE_WRITE,
	AEGIS_FS_FLUSH_WRITE,
	AEGIS_FS_CACHE_READ,
	AEGIS_FS_CONFIG_READ,
	AEGIS_FS_HASHLIST_READ,
	AEGIS_FS_HASHLIST_WRITE,
	AEGIS_FS_DEVORIG_READ,
	AEGIS_FS_DEVORIG_WRITE,
};

/* Securityfs entry operations */
void validator_fscleanup(void);
int validator_fsaccess(int op);

#endif /* AEGIS_VALIDATOR_FS_H */
