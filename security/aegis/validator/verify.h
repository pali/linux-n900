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
 * Authors: Axelle Apvrille, 2002-2003
 *          David Gordon, 2002-2003
 *          Markku Kylänpää, 2008-2010
 */

/*
 * This file is a header file for verification functions.
 */

#ifndef AEGIS_VALIDATOR_VERIFY_H
#define AEGIS_VALIDATOR_VERIFY_H

int validator_verify_refhash(struct file *file, char *refhash);
int validator_sha1(const void *vbuf, unsigned long len, unsigned char *digest);

#endif /* AEGIS_VALIDATOR_VERIFY_H */
