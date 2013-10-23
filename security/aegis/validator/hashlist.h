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
 * This file is a header file for the reference hashlist implementation that
 * is used to verify integrity of executable content as an alternative to RSA
 * signature.
 */

#ifndef AEGIS_VALIDATOR_HASHLIST_H
#define AEGIS_VALIDATOR_HASHLIST_H

/**
 * enum hashlist_node_type - node types in the hashlist
 * @EXECUTABLE_FILE:     application/script/library - executable content
 * @STATIC_DATA_FILE:    data file whose hash should be checked
 * @DYNAMIC_DATA_FILE:   configuration file that can change
 * @IMMUTABLE_DIRECTORY: directory contains protected content
 * @PROTECTED_DIRECTORY: directory cannot be renamed/deleted/hidden by mount
 *
 * FIXME: Support for PROTECTED_DIRECTORY requires modifications to LSM hooks
 */
enum hashlist_node_type {
	EXECUTABLE_FILE = 0,
	STATIC_DATA_FILE = 1,
	DYNAMIC_DATA_FILE = 2,
	IMMUTABLE_DIRECTORY = 3,
	PROTECTED_DIRECTORY = 4
};

/* Hashlist operations */
int validator_hashlist_entry(struct inode *node);
struct vprotection *validator_hashlist_get_wcreds(struct inode *node);
int validator_hashlist_get_data(struct inode *node, struct vmetadata *data);
void *validator_hashlist_new(void);
void validator_hashlist_delete(void *ptr);
int validator_hashlist_delete_item(struct inode *inode);

/* Securityfs entry operations for hashlist */
struct dentry *validator_hashlist_fsinit(struct dentry *top);
void validator_hashlist_fscleanup(struct dentry *f);

#endif /* AEGIS_VALIDATOR_HASHLIST_H */
