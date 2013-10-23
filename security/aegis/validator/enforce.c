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
 * Validator and also to set Aegis Validator into enforcing or permissive
 * mode. It is also possible to independently enable Runtime Policy
 * Framework source identity check.
 */

#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/aegis/creds.h>
#include "validator.h"
#include "enforce.h"
#include "fs.h"
#include "verify.h"
#include "sidcheck.h"

/*
 * Treat input number that is read from securityfs entry as bit fields to
 * independently configure integrity verification checks. Description of
 * bit fields:
 *
 * 0: SHA1 hash calculation
 * 1: Source identity check
 * 2: Data file open integrity check
 * 3: File attribute check
 * 4: Try to load new hashes if hash entry is not found
 * 5: Check only entries that are listed in the reference hash list
 * 6: Use pre-defined resource token to protect hash loading
 * 7: If set then only allow loading of new hashes but no mode changes
 * 8: If set then allow only whitelisted kernel modules
 */
#define HASH_CHECK_BIT    BIT(0)
#define SID_CHECK_BIT     BIT(1)
#define DATA_CHECK_BIT    BIT(2)
#define ATTRIB_CHECK_BIT  BIT(3)
#define HASH_REQ_BIT      BIT(4)
#define LISTED_ONLY_BIT   BIT(5)
#define SECFS_BIT         BIT(6)
#define SEAL_BIT          BIT(7)
#define KMOD_BIT          BIT(8)

/* Maximum values for user input */
#define VALIDATOR_ENABLE_ALL   0x1ff
#define VALIDATOR_ENFORCE_ALL  0xf

/* Buffer sizes to read/write securityfs messages */
#define RBUF_SIZE         8
#define WBUF_SIZE         8
#define DEVORIG_BUF_SIZE  24

/* "enforce" file operations */
static ssize_t read_enforce(struct file *file, char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	char buf[RBUF_SIZE];
	int val = 0;
	int r;

	if (validator_fsaccess(AEGIS_FS_ENFORCE_READ))
		return -EPERM;
	if (valinfo.mode)
		val |= HASH_CHECK_BIT;
	if (valinfo.sidmode)
		val |= SID_CHECK_BIT;
	if (valinfo.rootmode)
		val |= DATA_CHECK_BIT;
	if (valinfo.attribmode)
		val |= ATTRIB_CHECK_BIT;
	r = snprintf(buf, sizeof(buf), "0x%x\n", val);
	return simple_read_from_buffer(user_buf, count, ppos, buf, r + 1);
}

static ssize_t write_enforce(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[WBUF_SIZE];
	unsigned long val;
	int r;

	if (validator_fsaccess(AEGIS_FS_ENFORCE_WRITE))
		return -EPERM;
	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, user_buf, sizeof(buf)))
		return -EFAULT;
	buf[count] = 0;
	r = strict_strtoul(buf, 16, &val);
	if (r || (val > VALIDATOR_ENFORCE_ALL)) {
		pr_err("Aegis: bad enforce input: %s\n", buf);
		return -EINVAL;
	}
	valinfo.mode       = (val & HASH_CHECK_BIT)   ? 1 : 0;
	valinfo.sidmode    = (val & SID_CHECK_BIT)    ? 1 : 0;
	valinfo.rootmode   = (val & DATA_CHECK_BIT)   ? 1 : 0;
	valinfo.attribmode = (val & ATTRIB_CHECK_BIT) ? 1 : 0;
	return count;
}

/* "enforce" seq_file hooks. */
static const struct file_operations enforce_fops = {
	.read = read_enforce,
	.write = write_enforce
};

/* "enable" file operations */
static ssize_t read_enable(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	char buf[RBUF_SIZE];
	int val = 0;
	int r;

	if (validator_fsaccess(AEGIS_FS_ENABLE_READ))
		return -EPERM;
	if (valinfo.g_init)
		val |= HASH_CHECK_BIT;
	if (valinfo.s_init)
		val |= SID_CHECK_BIT;
	if (valinfo.r_init)
		val |= DATA_CHECK_BIT;
	if (valinfo.a_init)
		val |= ATTRIB_CHECK_BIT;
	if (valinfo.hashreq)
		val |= HASH_REQ_BIT;
	if (valinfo.listed_only)
		val |= LISTED_ONLY_BIT;
	if (valinfo.secfs_init)
		val |= SECFS_BIT;
	if (valinfo.seal)
		val |= SEAL_BIT;
	if (valinfo.kmod_init)
		val |= KMOD_BIT;
	r = snprintf(buf, sizeof(buf), "0x%x\n", val);
	return simple_read_from_buffer(user_buf, count, ppos, buf, r + 1);
}

static ssize_t write_enable(struct file *file, const char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	char buf[WBUF_SIZE];
	unsigned long val;
	int r;

	if (validator_fsaccess(AEGIS_FS_ENABLE_WRITE))
		return -EPERM;
	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, user_buf, sizeof(buf)))
		return -EFAULT;
	buf[count] = 0;
	r = strict_strtoul(buf, 16, &val);
	if (r || (val > VALIDATOR_ENABLE_ALL)) {
		pr_err("Aegis: bad enable input: %s\n", buf);
		return -EINVAL;
	}

	valinfo.g_init      = (val & HASH_CHECK_BIT)   ? 1 : 0;
	valinfo.s_init      = (val & SID_CHECK_BIT)    ? 1 : 0;
	valinfo.r_init      = (val & DATA_CHECK_BIT)   ? 1 : 0;
	valinfo.a_init      = (val & ATTRIB_CHECK_BIT) ? 1 : 0;
	valinfo.hashreq     = (val & HASH_REQ_BIT)     ? 1 : 0;
	valinfo.listed_only = (val & LISTED_ONLY_BIT)  ? 1 : 0;
	valinfo.secfs_init  = (val & SECFS_BIT)        ? 1 : 0;
	valinfo.seal        = (val & SEAL_BIT)         ? 1 : 0;
	valinfo.kmod_init   = (val & KMOD_BIT)         ? 1 : 0;

	return count;
}

/* "enable" seq_file hooks. */
static const struct file_operations enable_fops = {
	.read = read_enable,
	.write = write_enable
};

/* "devorig" file operations */
static ssize_t read_devorig(struct file *file, char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	char buf[DEVORIG_BUF_SIZE];
	int r;

	if (validator_fsaccess(AEGIS_FS_DEVORIG_READ))
		return -EPERM;
	r = snprintf(buf, sizeof(buf), "%ld\n", valinfo.devorig);
	if (r + 1 > sizeof(buf))
		return -EINVAL;
	return simple_read_from_buffer(user_buf, count, ppos, buf, r + 1);
}

static ssize_t write_devorig(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[DEVORIG_BUF_SIZE];
	long val;
	int r;

	if (validator_fsaccess(AEGIS_FS_DEVORIG_WRITE))
		return -EPERM;
	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, user_buf, sizeof(buf)))
		return -EFAULT;
	buf[count] = 0;
	r = strict_strtol(buf, 10, &val);
	if (r) {
		pr_err("Aegis: bad devorig input: %s\n", buf);
		return -EINVAL;
	}
	valinfo.devorig = val;
	return count;
}

/* "devorig" seq_file hooks. */
static const struct file_operations devorig_fops = {
	.read = read_devorig,
	.write = write_devorig
};

/**
 * validator_func_enforce_fsinit() - Initialize securityfs entry for enforcement
 * @top: securityfs parent directory
 *
 * Creates new securityfs entry called "enforce", which can be used to set
 * Aegis Validator to either enforcing or permissive mode.
 *
 * Return file dentry for enforcement control (or null in case of an error)
 */
struct dentry *validator_func_enforce_fsinit(struct dentry *top)
{
	return securityfs_create_file("enforce", 0600, top, NULL,
				      &enforce_fops);
}

/**
 * validator_func_enforce_fscleanup() - Remove securityfs entry of enforcement.
 * @f: file dentry of the logging securityfs entry
 *
 * Removes "enforce" securityfs entry.
 */
void validator_func_enforce_fscleanup(struct dentry *f)
{
	if (f)
		securityfs_remove(f);
}

/**
 * validator_func_enable_fsinit() - Initialize securityfs entry for enable
 * @top: securityfs parent directory
 *
 * Creates new securityfs entry called "enabled", which can be used to either
 * enable or disable Aegis Validator functionality.
 *
 * Return file dentry for enable/disable control.
 */
struct dentry *validator_func_enable_fsinit(struct dentry *top)
{
	return securityfs_create_file("enabled", 0600, top, NULL,
				      &enable_fops);
}

/**
 * validator_func_enable_fscleanup() - Remove securityfs entry of enable.
 * @f: file dentry of the logging securityfs entry
 *
 * Removes "enabled" securityfs entry.
 */
void validator_func_enable_fscleanup(struct dentry *f)
{
	if (f)
		securityfs_remove(f);
}

/**
 * validator_devorig_fsinit() - Initialize securityfs entry for devorig
 * @top: securityfs parent directory
 *
 * Creates new securityfs entry called "devorig", which can be used to set
 * source origin string for developer mode.
 *
 * Return file dentry for devorig control.
 */
struct dentry *validator_devorig_fsinit(struct dentry *top)
{
	return securityfs_create_file("devorig", 0600, top, NULL,
				      &devorig_fops);
}

