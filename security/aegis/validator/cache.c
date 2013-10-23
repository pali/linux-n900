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
 * Authors: Serge Hallyn, 2003-2004
 *          Makan Pourzandi, 2004
 *          Chris Wright, 2004
 *          Markku Kylänpää, 2008-2010
 */

/*
 * This file contains integrity verification result caching code. The code also
 * includes securityfs entries to display cache content and a flush entry to
 * flush the cache.
 */

#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/hash.h>
#include "validator.h"
#include "cache.h"
#include "fs.h"

/* Each cache bucket can have 8 cache entries */
#define ENTRIES_PER_BUCKET 8

/*
 * Data structure struct hash_entry has src_id field that is used to store
 * Aegis source origin identifier. Aegis Runtime Policy Framework is using
 * source origin identifier to authorize library loading. Data type of the
 * field is 'long'. However, certain data values are unused so we are now
 * using the same field also as a flag to state whether the cache entry is
 * in use or not. Values zero and -1 are not valid source origin identifiers
 * so we are using value -1 as our flag value.
 */
#define UNUSED_SRC_ID_VALUE -1

/**
 * struct hash_entry - a single inode signature validation cache entry
 * @i_ino:  Inode number
 * @s_dev:  Device identifier
 * @src_id: Source identifier
 *
 * Cache positive verification results indexed by inode number and device
 * identifier. Source identifier is also stored in src_id field. Source
 * identifier is used by the Runtime Policy  Framework to authorize library
 * loading. UNUSED_SRC_ID_VALUE in src_id field is used to set this structure
 * to be in unused state.
 */
struct hash_entry {
	unsigned long i_ino;
	dev_t s_dev;
	long src_id;
};

/**
 * struct hash_line - a cache entry bucket.
 * @sequence:     Lock for the hash line
 * @entry:        Table of bucket entries
 * @next_evicted: Index of the next free bucket slot
 *
 * Hash function for the inode  will index to a hash bucket, which contains
 * ENTRIES_PER_BUCKET entries for collisions. In this way a lookup should be
 * write-less, and entirely contained within one cache line.  When a bucket is
 * full, we evict in a round-robin fashion (unless the next_evicted happened
 * to be the last allocated)
 */
struct hash_line {
	seqlock_t sequence;
	struct hash_entry entry[ENTRIES_PER_BUCKET];
	short next_evicted;
};

/* Number of verification cache buckets */
static int validator_cache_buckets = 512;

/* validator_cache_buckets = 2 ** hash_bits */
static int hash_bits;

/* Integrity verification result cache */
static struct hash_line *sig_cache;

/**
 * hash() - hash function for the inode
 * @inode: Inode structure
 *
 * Calculate hash index from the inode pointer value.
 *
 * Return hash index value
 */
static inline unsigned long hash(struct inode *inode)
{
	return inode ? hash_long(inode->i_ino, hash_bits) : 0;
}

/**
 * entry_is_used() - is this cache entry in use
 * @e: cache entry
 *
 * Return one if the entry is in use and otherwise zero
 */
static int entry_is_used(struct hash_entry *e)
{
	return e->src_id != UNUSED_SRC_ID_VALUE;
}

/**
 * set_entry_unused() - set cache entry into unused state
 * @e: cache entry
 */
static void set_entry_unused(struct hash_entry *e)
{
	e->src_id = UNUSED_SRC_ID_VALUE;
}

/**
 * is_same_inode() - Does the cache validation entry describe this inode?
 * @e:     Cached signature validation entry
 * @inode: An inode
 *
 * Check only from used entries. Compare values of i_ino and s_dev. This
 * should be called only when the caller has a seqlock for the hash line.
 *
 * Return 1 if the sig entry is for the given inode and 0 otherwise
 */
static int is_same_inode(struct hash_entry *e, struct inode *inode)
{
	return (!entry_is_used(e) || e->i_ino != inode->i_ino ||
		e->s_dev != inode->i_sb->s_dev) ? 0 : 1;
}

/**
 * inc_evicted() - Increment next bucket entry index counter.
 * @l: hash line entry
 *
 * This should be called only when the caller has a seqlock for the hash line.
 *
 * Return number of entries in the bucket.
 */
static short inc_evicted(struct hash_line *l)
{
	short ret = l->next_evicted++;
	if (l->next_evicted == ENTRIES_PER_BUCKET)
		l->next_evicted = 0;
	return ret;
}

/* Seq_file read operations for /sys/kernel/security/digsig/cache */
static void *cache_start(struct seq_file *m, loff_t *pos)
{
	loff_t *spos = kmalloc(sizeof(loff_t), GFP_KERNEL);
	if (!spos)
		return NULL;
	*spos = *pos;
	if (*spos < validator_cache_buckets)
		return spos;
	kfree(spos);
	return NULL;
}

static void *cache_next(struct seq_file *m, void *v, loff_t *pos)
{
	loff_t *spos = v;
	*pos = ++(*spos);
	return (*spos < validator_cache_buckets) ? spos : NULL;
}

static void cache_stop(struct seq_file *m, void *v)
{
	kfree(v);
}

static int cache_show(struct seq_file *m, void *v)
{
	int i;
	unsigned int seq;
	struct hash_line *l;
	loff_t *spos = v;
	l = &sig_cache[*spos];
	seq_printf(m, "Line: %03Ld\t", *spos);
	do {
		seq = read_seqbegin(&l->sequence);
		for (i = 0; i < ENTRIES_PER_BUCKET; i++)
			if (entry_is_used(&l->entry[i]))
				seq_printf(m, "%ld ", l->entry[i].i_ino);
	} while (read_seqretry(&l->sequence, seq));
	seq_printf(m, "\n");
	return 0;
}

static const struct seq_operations cache_seqops = {
	.start = cache_start,
	.next = cache_next,
	.stop = cache_stop,
	.show = cache_show
};

/**
 * cache_open() - Create an iterator for the cache listing
 * @inode: inode structure representing file
 * @file:  "cache" file pointer
 *
 * Open cache iterator for /sys/kernel/security/digsig/cache.
 * Using cache_seqops functions to iterate cache entries.
 *
 * Return zero for success and negative value for an error.
 */
static int cache_open(struct inode *inode, struct file *file)
{
	if (validator_fsaccess(AEGIS_FS_CACHE_READ))
		return -EPERM;
	return seq_open(file, &cache_seqops);
}

/* Cache information seq_file hooks */
static const struct file_operations cache_fops = {
	.open = cache_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

/* Cache flush file operations */
static ssize_t cache_flush(struct file *f, const char __user *buf, size_t size,
			   loff_t *pos)
{
	int j, i;

	if (validator_fsaccess(AEGIS_FS_FLUSH_WRITE))
		return -EPERM;

	for (j = 0; j < validator_cache_buckets; j++) {
		struct hash_line *l = &sig_cache[j];
		write_seqlock(&l->sequence);
		for (i = 0; i < ENTRIES_PER_BUCKET; i++)
			set_entry_unused(&l->entry[i]);
		l->next_evicted = 0;
		write_sequnlock(&l->sequence);
	}
	return size;
}

/* Cache flush operation seq_file hooks */
static const struct file_operations flush_fops = {
	.write = cache_flush
};

/**
 * validator_cache_contains() - is this inode in our cache?
 * @inode:  inode structure
 * @src_id: source identifier
 *
 * Check whether the inode is already cached. Return also source identifier
 * as a parameter.
 *
 * Return 1 if found, 0 otherwise
 */
int validator_cache_contains(struct inode *inode, long *src_id)
{
	unsigned int seq;
	int i;
	int found;
	int h = hash(inode);
	struct hash_line *l = &sig_cache[h];

	do {
		seq = read_seqbegin(&l->sequence);
		found = 0;
		for (i = 0; i < ENTRIES_PER_BUCKET && !found; i++)
			if (is_same_inode(&l->entry[i], inode)) {
				found = 1;
				*src_id = l->entry[i].src_id;
			}
	} while (read_seqretry(&l->sequence, seq));
	return found;
}

/**
 * validator_cache_remove() - Remove inode entry from the cache
 * @inode: inode to be removed
 *
 * Inode will be removed from the cache.
 */
void validator_cache_remove(struct inode *inode)
{
	int i;
	int h = hash(inode);
	struct hash_line *l = &sig_cache[h];

	write_seqlock(&l->sequence);
	for (i = 0; i < ENTRIES_PER_BUCKET; i++)
		if (is_same_inode(&l->entry[i], inode))
			set_entry_unused(&l->entry[i]);
	write_sequnlock(&l->sequence);
}

/**
 * validator_cache_add() - Cache inode entry
 * @inode:  Inode whose signature validation to cache
 * @src_id: Source identifier
 *
 * We have validated the signature on inode. Cache that decision. If the hash
 * bucket is full, we pick the next evicted entry in a round-robin fashion.
 * Otherwise, we make sure that the next evicted entry will not be the one we
 * just inserted.
 *
 * Return 0 for success and negative value for an error.
 */
int validator_cache_add(struct inode *inode, long src_id)
{
	struct hash_line *l;
	int i, h;

	if (!inode) {
		pr_err("Aegis: Request to cache null inode\n");
		return -EINVAL;
	}
	h = hash(inode);
	l = &sig_cache[h];
	write_seqlock(&l->sequence);
	for (i = 0; i < ENTRIES_PER_BUCKET && entry_is_used(&l->entry[i]); i++)
		;
	if (i == ENTRIES_PER_BUCKET)
		i = inc_evicted(l);
	else if (i == l->next_evicted)
		inc_evicted(l);
	l->entry[i].src_id = src_id;
	l->entry[i].i_ino = inode->i_ino;
	l->entry[i].s_dev = inode->i_sb->s_dev;
	write_sequnlock(&l->sequence);
	return 0;
}

/**
 * validator_cache_fsremove() - Remove all entries from certain filesystem
 * @dev: device identifier
 *
 * If filesystem is unmounted, all entries that belong to the umounted
 * filesystem should be removed from the verification cache.
 */
void validator_cache_fsremove(dev_t dev)
{
	int h;

	for (h = 0; h < validator_cache_buckets; h++) {
		int i;
		struct hash_line *l = &sig_cache[h];

		write_seqlock(&l->sequence);
		for (i = 0; i < ENTRIES_PER_BUCKET; i++)
			if (l->entry[i].s_dev == dev)
				set_entry_unused(&l->entry[i]);
		write_sequnlock(&l->sequence);
	}
}

/**
 * validator_cache_init() - Initialize caching
 *
 * Caching is initialized.
 *
 * Return 0 on success, -ENOMEM on failure.
 */
int validator_cache_init(void)
{
	int i, j;

	hash_bits = ilog2(validator_cache_buckets);
	if (validator_cache_buckets != (1 << hash_bits)) {
		hash_bits++;
		validator_cache_buckets = 1 << hash_bits;
	}
	sig_cache = kmalloc(validator_cache_buckets * sizeof(struct hash_line),
			    GFP_KERNEL);
	if (!sig_cache) {
		pr_err("Aegis: No memory to initialize cache.\n");
		return -ENOMEM;
	}
	for (i = 0; i < validator_cache_buckets; i++) {
		seqlock_init(&sig_cache[i].sequence);
		sig_cache[i].next_evicted = 0;
		for (j = 0; j < ENTRIES_PER_BUCKET; j++)
			set_entry_unused(&sig_cache[i].entry[j]);
	}
	return 0;
}

/**
 * validator_cache_cleanup() - Cleanup the cache
 *
 * Free cache data structures.
 */
void validator_cache_cleanup(void)
{
	kfree(sig_cache);
	sig_cache = NULL;
}

/**
 * validator_cache_fsinit() - Initialize securityfs entry to display cache
 * @top: securityfs parent directory
 *
 * Create a securityfs entry, which can be used to display cache content.
 *
 * Return file dentry for cache visualization (or null in case of an error)
 */
struct dentry *validator_cache_fsinit(struct dentry *top)
{
	return securityfs_create_file("cache", 0400, top, NULL, &cache_fops);
}

/**
 * validator_cache_fscleanup() - Remove securityfs entry of the cache.
 * @f: file dentry of the cache
 *
 * Remove cache content securityfs entry.
 */
void validator_cache_fscleanup(struct dentry *f)
{
	if (f)
		securityfs_remove(f);
}

/**
 * validator_cache_flush_fsinit() - Initialize securityfs entry for cache flush
 * @top: securityfs parent directory
 *
 * Create a securityfs entry, which can be used to flush cache content.
 *
 * Return file dentry for cache visualization (or null in case of an error)
 */
struct dentry *validator_cache_flush_fsinit(struct dentry *top)
{
	return securityfs_create_file("flush", 0200, top, NULL, &flush_fops);
}

/**
 * validator_cache_flush_fscleanup() - Remove securityfs entry of the cache.
 * @f: file dentry of the cache
 *
 * Remove cache flush securityfs entry.
 */
void validator_cache_flush_fscleanup(struct dentry *f)
{
	if (f)
		securityfs_remove(f);
}

