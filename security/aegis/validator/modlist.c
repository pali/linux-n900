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
 * This file contains an implementation of the kernel module whitelist.
 */

#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/hash.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/elf.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/hash.h>
#include "validator.h"
#include "verify.h"
#include "fs.h"
#include "modlist.h"

/* Hashtable size is power of 2 */
#define MOD_HASHTABLE_BITS 5

/* Hashtable size */
#define MOD_HASHTABLE_SIZE (1 << MOD_HASHTABLE_BITS)

/* Lock for modlist hashtable */
static DEFINE_RWLOCK(modlist_lock);

/* Module hashtable */
static struct hlist_head *modlist;

/**
 * struct modlist_entry - This is a module whitelist hashtable element
 * @list:     The list connecting items in the bucket
 * @hash:     SHA1 hash value of a kernel module
 *
 * SHA1 cryptographic hash value of authorized kernel modules is stored.
 */
struct modlist_entry {
	struct hlist_node list;
	unsigned char hash[SHA1_HASH_LENGTH];
};

/**
 * modlist_new() - Create new kernel module list structure.
 *
 * This function creates internal data structure for a list of kernel
 * modules.
 *
 * Return pointer to an internal module list structure or NULL.
 */
static struct hlist_head *modlist_new(void)
{
	int i;

	modlist = kmalloc(MOD_HASHTABLE_SIZE * sizeof(*modlist), GFP_KERNEL);
	if (!modlist) {
		pr_err("Aegis: Unable to allocate modlist data structure\n");
		return NULL;
	}

	for (i = 0; i < MOD_HASHTABLE_SIZE; i++)
		INIT_HLIST_HEAD(&modlist[i]);
	return modlist;
}

/**
 * modlist_add() - Add new hash entry to kernel module whitelist
 * @entry:     Modlist entry record
 *
 * New SHA1 hash value is added to kernel module hashlist. Return zero value
 * for success and negative value for an error.
 */
static int modlist_add(struct modlist_entry *entry)
{
	int i;
	struct hlist_node *pos;
	struct modlist_entry *tmp;

	BUG_ON(!entry);
	if (!modlist)
		modlist = modlist_new();
	if (!modlist) {
		pr_err("Aegis: Modlist creation failed\n");
		return -ENOMEM;
	}
	i = entry->hash[0] % MOD_HASHTABLE_SIZE;
	write_lock(&modlist_lock);
	hlist_for_each_entry(tmp, pos, &modlist[i], list)
		if (memcmp(tmp->hash, entry->hash, SHA1_HASH_LENGTH) == 0)
			goto out;
	hlist_add_head(&entry->list, &modlist[i]);
out:
	write_unlock(&modlist_lock);
	return 0;
}

/**
 * modlist_write() - Store reference hashlist value.
 * @f:    File pointer
 * @buf:  Where to get the data from
 * @size: Bytes sent
 * @pos:  Where to start
 *
 * Read new reference hashlist entry that is written to securityfs "hashlist"
 * entry.
 *
 * Return number of bytes written or negative error code.
 */
static ssize_t modlist_write(struct file *f, const char __user *buf,
			     size_t size, loff_t *pos)
{
	struct modlist_entry *entry;

	if (validator_fsaccess(AEGIS_FS_HASHLIST_WRITE))
		return -EPERM;
	if (*pos != 0)
		return -ESPIPE;
	if (size < SHA1_HASH_LENGTH) {
		pr_err("Aegis: Too short input (%d) for hash entry\n", size);
		return -EFAULT;
	}
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	if (copy_from_user(entry->hash, buf, SHA1_HASH_LENGTH)) {
		pr_err("Aegis: Failed to read new hashlist entries\n");
		goto err;
	}
	if (modlist_add(entry)) {
		pr_err("Aegis: Failed to authorize modules\n");
		goto err;
	}
	return size;
err:
	kfree(entry);
	return -EFAULT;
}

/* Seq_file read operations for /sys/kernel/security/digsig/modlist */
static void *modlist_fstart(struct seq_file *m, loff_t *pos)
{
	loff_t *spos;

	if (*pos >= MOD_HASHTABLE_SIZE)
		return NULL;
	spos = kmalloc(sizeof(*spos), GFP_KERNEL);
	if (!spos)
		return NULL;
	*spos = *pos;
	return spos;
}

static void *modlist_fnext(struct seq_file *m, void *v, loff_t *pos)
{
	loff_t *spos = v;
	*pos = ++(*spos);
	return (*spos < MOD_HASHTABLE_SIZE) ? spos : NULL;
}

static void modlist_fstop(struct seq_file *m, void *v)
{
	kfree(v);
}

static int modlist_fshow(struct seq_file *m, void *v)
{
	loff_t *spos = v;
	struct modlist_entry *my;
	struct hlist_node *pos;

	seq_printf(m, "Line: %03Ld\n", *spos);
	if (modlist == NULL)
		return 0;
	read_lock(&modlist_lock);
	hlist_for_each_entry(my, pos, &modlist[*spos], list) {
		int i;
		for (i = 0; i < SHA1_HASH_LENGTH; i++)
			seq_printf(m, "%02x", my->hash[i]);
		seq_printf(m, "\n");
	}
	read_unlock(&modlist_lock);
	return 0;
}

static const struct seq_operations modlist_seqops = {
	.start = modlist_fstart,
	.next = modlist_fnext,
	.stop = modlist_fstop,
	.show = modlist_fshow
};

static int modlist_open(struct inode *inode, struct file *file)
{
	if (validator_fsaccess(AEGIS_FS_HASHLIST_READ))
		return -EPERM;
	return seq_open(file, &modlist_seqops);
}

/* hashlist seq_file hooks */
static const struct file_operations modlist_fops = {
	.write = modlist_write,
	.open = modlist_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

/**
 * validator_modlist_entry() - Check an item from the kernel module list.
 * @hash: sha1 hash value (20 bytes)
 *
 * Test whether given sha1 hash value is in a modlist
 *
 * Return 0 if found and -ENOENT otherwise.
 */
int validator_modlist_entry(unsigned char *hash)
{
	int i;
	int r = -ENOENT;
	struct modlist_entry *tmp;
	struct hlist_node *pos;

	if ((modlist == NULL) || (hash == NULL))
		return r;
	i = hash[0] % MOD_HASHTABLE_SIZE;
	read_lock(&modlist_lock);
	hlist_for_each_entry(tmp, pos, &modlist[i], list) {
		if (memcmp(tmp->hash, hash, SHA1_HASH_LENGTH) == 0) {
			r = 0;
			break;
		}
	}
	read_unlock(&modlist_lock);
	return r;
}

/**
 * validator_modlist_fsinit() - Initialize securityfs entry for modlist data
 * @top: securityfs parent directory
 *
 * Create securityfs entry "modlist" to either add new entries or to
 * display a list of existing entries (for debugging) to kernel module
 * whitelist.
 *
 * Return file dentry for logging control (or ERR_PTR in case of an error)
 */
struct dentry *validator_modlist_fsinit(struct dentry *top)
{
	return securityfs_create_file("modlist", 0600, top, NULL,
				      &modlist_fops);
}

/**
 * validator_modlist_delete() - Deallocate module list structure.
 * @ptr: Kernel module white list data structure
 *
 * This function deallocates an internal data structure for a list of module
 * hashes. This data structure is allocated using modlist_new function.
 */
void validator_modlist_delete(void *ptr)
{
	struct hlist_head *rlist = ptr;
	int i;

	if (!ptr)
		return;
	for (i = 0; i < MOD_HASHTABLE_SIZE; i++) {
		struct modlist_entry *tmp;
		struct hlist_node *pos;
		struct hlist_node *q;
		write_lock(&modlist_lock);
		hlist_for_each_safe(pos, q, &rlist[i]) {
			tmp = hlist_entry(pos, struct modlist_entry, list);
			hlist_del(pos);
			kfree(tmp);
		}
		write_unlock(&modlist_lock);
	}
	kfree(ptr);
}

/**
 * validator_kmod_check() - check kernel module against white list
 * @vbuf:  kernel module buffer
 * @len:   buffer length
 *
 * Return 0 for success and negative value for an error
 */
int validator_kmod_check(const void *vbuf, unsigned long len)
{
	char digest[SHA1_HASH_LENGTH];
	int r;

	if (valinfo.kmod_init == 0)
		return 0;
	r = validator_sha1(vbuf, len, digest);
	if (r) {
		pr_err("Aegis: error in kernel module SHA1 calculation\n");
		return -EINVAL;
	}
	r = validator_modlist_entry(digest);
	if (r)
		pr_err("Aegis: module verification failed\n");
	return r;
}
