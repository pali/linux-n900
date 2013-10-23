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
 * Authors: Miroslaw Zakrzewski, 2002-2003
 *          David Gordon, 2002-2003
 *          Alain Patrick Medenou, 2002-2003
 *          Gabriel Ivascu, 2002-2003
 *          Makan Pourzandi, 2003-2004
 *          Markku Kylänpää, 2008-2010
 */

/*
 * The main header file for Aegis Validator
 */

#ifndef AEGIS_VALIDATOR_H
#define AEGIS_VALIDATOR_H

/* SHA1 hash length in bytes */
#define SHA1_HASH_LENGTH      20

/**
 * enum vreason - reason for validation failure
 * @R_OK:     validation ok
 * @R_SID:    source identifier check failed
 * @R_HLIST:  reference value not found
 * @R_ATTRIB: attribute check failed
 * @R_HASH:   sha1 hash mismatch
 * @R_LOAD:   no hashlist or hashlist loading failed
 * @R_CACHE:  internal error when caching verification
 * @R_EINTR:  interrupted system call
 */
enum vreason {
	R_OK = 0,
	R_SID = 1,
	R_HLIST = 2,
	R_ATTRIB = 3,
	R_HASH = 4,
	R_LOAD = 5,
	R_CACHE = 6,
	R_EINTR = 7,
};

/**
 * enum validator_hook - source of validation request
 * @PATH_CHECK: validator_path_check function added to fs/namei.c
 * @MMAP_CHECK: LSM hook .file_mmap
 * @BPRM_CHECK: LSM hook .bprm_check_security
 *
 * File validation request can come from one of these three hooks.
 */
enum validator_hook {
	PATH_CHECK = 1,
	MMAP_CHECK = 2,
	BPRM_CHECK = 3
};

/**
 * struct validator_info - Aegis Validator state information
 * @g_init:      This is set to one if integrity check is enabled
 * @s_init:      This is set to one if source id check is enabled
 * @mode:        This is zero in permissive mode and one in enforcing mode
 * @h_init:      Is the reference hashlist already initialized?
 * @sidmode:     Source identifier check either permissive or enforcing
 * @a_init:      Enable file attribute verification
 * @attribmode:  Enforce file attributes (uid,gid,perm) verified
 * @r_init:      Enable data file reading verification
 * @rootmode:    Enforce data file reading verification (for static files)
 * @listed_only: Set to 1 if only listed files should be checked
 * @hashreq:     If hash is not found invoke userspace helper
 * @secfs_init:  If set credential "tcb" is needed for hash loading
 * @seal:        Do not allow any mode changes after this is set
 * @kmod_init:   If set check kernel modules against whitelist
 * @v_init:      If this flag is set vhash command-line option is in use
 * @vcode:       SHA1 hash of hashlist laoding executable (valid if v_init == 1)
 * @devorig:     Source origin to be used for unknown source
 */
struct validator_info {
	int g_init;
	int s_init;
	int mode;
	int h_init;
	int sidmode;
	int a_init;
	int attribmode;
	int r_init;
	int rootmode;
	int listed_only;
	int hashreq;
	int secfs_init;
	int seal;
	int kmod_init;
	int v_init;
	char vcode[SHA1_HASH_LENGTH];
	long devorig;
};

/* Aegis Validator state information */
extern struct validator_info valinfo;

/**
 * struct vmetadata - reference context information used in validation
 * @sid:      source identifier
 * @uid:      uid file attribute
 * @gid:      gid file attribute
 * @mode:     file permission mode
 * @nodetype: true if file is marked to be dynamic file
 * @refhash:  SHA1 reference hash
 *
 * This structure is used as a parameter in Integrity Protection Policy check
 * functions.
 */
struct vmetadata {
	long sid;
	uid_t uid;
	gid_t gid;
	int mode;
	u8 nodetype;
	char refhash[SHA1_HASH_LENGTH];
};

/**
 * struct vprotection - modification protection record
 * @num:       Number of credentials to allow writing
 * @credtype:  Array of credential types
 * @credvalue: Array of credential values
 *
 * It is possible to set credentials (e.g. resource tokens or capabilities)
 * to specify who can modify protected files. This is one field in the
 * hashlist entry. If the field is NULL then normal DAC permissions control
 * writing. Because there most likely won't be many different protection
 * alternatives then this policy information can be shared.
 */
struct vprotection {
	int num;
	long *credtype;
	long *credvalue;
};

#endif /* AEGIS_VALIDATOR_H */
