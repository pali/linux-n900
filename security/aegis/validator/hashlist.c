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
 * This file contains an implementation of the reference hashlist that is
 * used to verify integrity of executable content as an alternative to RSA
 * signature used in original DigSig implementation.
 */

#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/hash.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "validator.h"
#include "fs.h"
#include "hashlist.h"
#include "sidcheck.h"

/* Hashtable size is power of 2 */
#define HASHTABLE_BITS 10

/* Hashtable size */
#define HASHTABLE_SIZE (1 << HASHTABLE_BITS)

/* Buffer size used in hashlist_fshow() */
#define LINEBUF_LENGTH 128

/* Minimum length for the hashlist store message (dynamic entry msg) */
#define SHORT_MESSAGE_MIN_LENGTH 14

/* Minimum length for the hashlist store message containing SHA1 hash */
#define HASH_MESSAGE_MIN_LENGTH (1 + SHA1_HASH_LENGTH + 5)

/**
 * struct hashlist_entry - This is a reference hashtable element
 * @list:     The list connecting items in the bucket
 * @nodetype: Executable/static data/dynamic data/directory
 * @ino:      Inode number
 * @sid:      Source identifier
 * @uid:      File owner
 * @gid:      File group
 * @mode:     File permission bits
 * @wcreds:   Optional record to control modification of the file object
 * @hash:     Hash value
 *
 * Inode number (ino) is used as a hashtable key. There is also a field
 * (sid) for the source identifier of the object and for file metadata
 * information (uid,gid,mode). Some entries can be marked as dynamic.
 * SHA1 cryptographic hash value is also stored. These elements are stored
 * in hashtable that has HASHTABLE_SIZE buckets. The entries that are
 * mapped to the same bucket are stored in a linked list that is addressed
 * by a field "list". Each filesystem volume should have its own reference
 * hashlist as this does not record superblock of the entry.
 */
struct hashlist_entry {
	struct hlist_node list;
	unsigned int nodetype;
	unsigned long ino;
	long sid;
	uid_t uid;
	gid_t gid;
	int mode;
	struct vprotection *wcreds;
	unsigned char hash[SHA1_HASH_LENGTH];
};

/**
 * struct hashlist_line - Hashlist bucket entry
 * @bucket_lock: Lock for the bucket
 * @entries:     A list of bucket entries
 *
 * Hashlist bucket entry. HASHTABLE_SIZE buckets are allocated.
 */
struct hashlist_line {
	rwlock_t bucket_lock;
	struct hlist_head entries;
};

/**
 * struct volume_list - List of volumes having reference hashlist
 * @vlist:   Volume list
 * @vhashes: Bucket list
 *
 * Each volume can have its own hashlist
 */
struct volume_list {
	struct list_head vlist;
	struct hashlist_line *vhashes;
};

/* List of volumes. Each can have own reference hashlist. */
static LIST_HEAD(volumes);

/**
 * parse_wcreds() - parse optional writer credentials list from the message
 * @n:   Number of credential type value pair entries
 * @str: String to be parsed
 *
 * Some objects that are integrity protected by reference hashes can only
 * be modified if the writer owns special credentials. This is typically
 * used to create protected directories but optionally this can be used
 * for other directories as well.
 *
 * FIXME: This allocates new entries for each node. However, most likely
 * there will only be small amount of different policy labels. It would
 * be possible to allocate policy labels only once and reuse these.
 *
 * Return write credentials data structure or NULL if there were no
 * credentials specified. Return error pointer if there was parsing
 * or memory allocation error.
 */
static struct vprotection *parse_wcreds(int n, char *str)
{
	struct vprotection *credlist;
	int r;
	int pos = 0;
	int i;

	if ((n <= 0) || (str == NULL))
		return NULL;
	credlist = kmalloc(sizeof(*credlist), GFP_KERNEL);
	if (!credlist) {
		pr_err("Aegis: credlist memory allocation error\n");
		return ERR_PTR(-ENOMEM);
	}
	credlist->num = n;
	credlist->credtype = kmalloc(sizeof(long) * n, GFP_KERNEL);
	if (!credlist->credtype) {
		pr_err("Aegis: credtype memory allocation error\n");
		r = -ENOMEM;
		goto out1;
	}
	credlist->credvalue = kmalloc(sizeof(long) * n, GFP_KERNEL);
	if (!credlist->credvalue) {
		pr_err("Aegis: credvalue memory allocation error\n");
		r = -ENOMEM;
		goto out2;
	}
	for (i = 0; i < n; i++) {
		r = sscanf(str + pos, "%ld %ld%n", &credlist->credtype[i],
			   &credlist->credvalue[i], &pos);
		if (r < 2) {
			pr_err("Aegis: credential list parsing failure\n");
			r = -EINVAL;
			goto out3;
		}
	}
	return credlist;
out3:
	kfree(credlist->credtype);
out2:
	kfree(credlist->credvalue);
out1:
	kfree(credlist);
	return ERR_PTR(r);
}

/**
 * free_wcreds_data() - Free optional wcreds data field
 * @entry: Hashlist entry
 *
 * Directories that are protected by configuration data protection can have
 * an attribute (a list of resource tokens) that specify, which resource token
 * is required to allow modifications in the directory. When the hashlist
 * entry is removed this (optional) data structure should be removed as well
 * if it exists.
 */
static void free_wcreds_data(struct hashlist_entry *entry)
{
	if (entry->wcreds && entry->wcreds->num > 0) {
		kfree(entry->wcreds->credtype);
		kfree(entry->wcreds->credvalue);
		kfree(entry->wcreds);
		entry->wcreds = NULL;
	}
}

/**
 * hashlist_add() - Add new inode-hash pair to a reference hashlist
 * @device_id: Device identifier
 * @entry:     Hashlist entry record
 *
 * New inode number and SHA1 hash value is added to reference hashlist.
 * Will also setup source identifier. Return zero value for success and
 * negative value for an error.
 */
static int hashlist_add(unsigned int device_id, struct hashlist_entry *entry)
{
	int i;
	struct super_block *sb;
	struct hashlist_line *hashlist;
	struct hlist_node *pos;
	struct hlist_node *next;
	struct hashlist_entry *tmp;

	BUG_ON(!entry);
	sb = user_get_super(new_decode_dev(device_id));
	if (!sb) {
		pr_err("Aegis: Cannot find superblock %d\n", device_id);
		return -EFAULT;
	}
	if (sb->s_security == NULL)
		sb->s_security = validator_hashlist_new();
	hashlist = sb->s_security;
	drop_super(sb);
	if (!hashlist) {
		pr_err("Aegis: Hashlist creation failed\n");
		return -ENOMEM;
	}
	i = hash_long(entry->ino, HASHTABLE_BITS);
	write_lock(&hashlist[i].bucket_lock);
	hlist_for_each_safe(pos, next, &hashlist[i].entries) {
		tmp = hlist_entry(pos, struct hashlist_entry, list);
		if (tmp->ino == entry->ino) {
			hlist_del(pos);
			free_wcreds_data(tmp);
			kfree(tmp);
			break;
		}
	}
	hlist_add_head(&entry->list, &hashlist[i].entries);
	write_unlock(&hashlist[i].bucket_lock);
	return 0;
}

/**
 * parse_common_fields() - parse add hashlist entry message
 * @str:       Message to be parsed
 * @nodetype:  Message type (input parameter)
 * @device_id: Mount volume identifier (output parameter)
 *
 * Return new hashlist entry or NULL if there were parsing errors.
 */
static struct hashlist_entry *parse_common_fields(char *str,
						  unsigned int nodetype,
						  unsigned int *device_id)
{
	struct hashlist_entry *entry;
	int r;
	int pos;
	int num;

	if (str == NULL) {
		pr_err("Aegis: Hashlist input line was null\n");
		return NULL;
	}
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		pr_err("Aegis: Cannot allocate space for hashlist entry\n");
		return NULL;
	}
	entry->nodetype = nodetype;
	r = sscanf(str, "%u %lu %d %d %d %ld %d%n", device_id, &entry->ino,
		   &entry->uid, &entry->gid, &entry->mode, &entry->sid, &num,
		   &pos);
	if (r < 7)
		goto out;
	entry->wcreds = parse_wcreds(num, str + pos);
	if (IS_ERR(entry->wcreds))
		goto out;
	return entry;
out:
	pr_err("Aegis: Conversion failed %s\n", str);
	kfree(entry);
	return NULL;
}

/**
 * parse_old_format_msg() - parse old format add hashlist entry message
 * @ptr:       Message fields after SHA1 hash to be parsed
 * @size:      Size of the message
 * @device_id: Mount volume identifier (output parameter)
 *
 * FIXME: This function will be removed when old format messages are
 *        deprecated.
 *
 * Return new hashlist entry or NULL if there were parsing errors.
 */
static struct hashlist_entry *parse_old_format_msg(char *ptr, size_t size,
						   unsigned int *device_id)
{
	struct hashlist_entry *entry;
	char *sid_string;
	int pos;
	int r;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		pr_err("Aegis: Cannot allocate hashlist entry\n");
		return NULL;
	}
	entry->nodetype = EXECUTABLE_FILE;
	r = sscanf(ptr, "%u %lu%n", device_id, &entry->ino, &pos);
	if (r < 2)
		goto out;
	sid_string = ptr + pos;
	if (size - 2 < pos)
		goto out;
	entry->sid = validator_sid_define(sid_string);
	entry->uid = 0;
	entry->gid = 0;
	entry->mode = 0;
	entry->wcreds = NULL;
	return entry;
out:
	pr_err("Aegis: message conversion failed\n");
	kfree(entry);
	return NULL;
}

/**
 * hashlist_store() - Store new reference hash values
 * @data: Buffer to contain a new hashlist entry
 * @size: Size of the buffer
 *
 * Add new entry to a reference hashlist data structure. Parse message from
 * securityfs entry. The message format is described in
 * Documenattion/aegis_validator.txt.
 *
 * FIXME: Odd message termination with "\0\n". The change requires changes
 *        to userspace tools as well.
 *
 * Return size of data stored or negative value for an error.
 */
static ssize_t hashlist_store(char *data, size_t size)
{
	struct hashlist_entry *entry;
	unsigned int device_id;
	unsigned int nodetype;
	char *attrib_addr;
	char *hash_addr;

	if (size < SHORT_MESSAGE_MIN_LENGTH) {
		pr_err("Aegis: Too short input (%d) for hash entry\n", size);
		return -EFAULT;
	}
	if (data[size - 1] != '\n') {
		pr_err("Aegis: Incorrect message without NL termination\n");
		return -EFAULT;
	}
	if (data[size - 2] != '\0') {
		pr_err("Aegis: Incorrect message without null termination\n");
		return -EFAULT;
	}
	if ((data[0] == 'a') || (data[0] == 't') || (data[0] == 's')) {
		if (size < HASH_MESSAGE_MIN_LENGTH) {
			pr_err("Aegis: Too short input (%d) for hash entry\n",
			       size);
			return -EFAULT;
		}
	}
	switch (data[0]) {
	case 'a':
	case 's':
		attrib_addr = data + 1 + SHA1_HASH_LENGTH;
		hash_addr = data + 1;
		nodetype = EXECUTABLE_FILE;
		break;
	case 't':
		attrib_addr = data + 1 + SHA1_HASH_LENGTH;
		hash_addr = data + 1;
		nodetype = STATIC_DATA_FILE;
		break;
	case 'x':
		attrib_addr = data + 1;
		hash_addr = NULL;
		nodetype = DYNAMIC_DATA_FILE;
		break;
	case 'd':
		attrib_addr = data + 1;
		hash_addr = NULL;
		nodetype = IMMUTABLE_DIRECTORY;
		break;
	case 'p':
		attrib_addr = data + 1;
		hash_addr = NULL;
		nodetype = PROTECTED_DIRECTORY;
		break;
	default:
		pr_err("Aegis: Bad hash load command %c\n", data[0]);
		return -EFAULT;
		break;
	}
	if (data[0] == 'a')
		entry = parse_old_format_msg(attrib_addr,
					     size - 1 - SHA1_HASH_LENGTH,
					     &device_id);
	else
		entry = parse_common_fields(attrib_addr, nodetype, &device_id);
	if (entry == NULL)
		return -EFAULT;
	if (hash_addr)
		memcpy(entry->hash, hash_addr, SHA1_HASH_LENGTH);
	else
		memset(entry->hash, 0, SHA1_HASH_LENGTH);
	if (hashlist_add(device_id, entry)) {
		kfree(entry);
		return -EFAULT;
	}
	return size;
}

/**
 * hashlist_write() - Store reference hashlist value.
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
static ssize_t hashlist_write(struct file *f, const char __user *buf,
			      size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	if (validator_fsaccess(AEGIS_FS_HASHLIST_WRITE))
		return -EPERM;
	if (*pos != 0)
		return -ESPIPE;
	/*
	 * The message can have a couple of variable length fields e.g.
	 * source identifier string, but test some sensible upper limit
	 * value to prevent unnecessary big allocations.
	 */
	if (size > PATH_MAX)
		return -EFAULT;
	data = kmalloc(size, GFP_KERNEL);
	if (!data) {
		pr_err("Aegis: allocation failed in hash writing\n");
		return -ENOMEM;
	}
	if (copy_from_user(data, buf, size)) {
		kfree(data);
		pr_err("Aegis: Failed to read new hashlist entries\n");
		return -EFAULT;
	}
	error = hashlist_store(data, size);
	kfree(data);
	return error;
}

/* Seq_file read operations for /sys/kernel/security/digsig/hashlist */
static void *hashlist_fstart(struct seq_file *m, loff_t *pos)
{
	loff_t *spos = kmalloc(sizeof(loff_t), GFP_KERNEL);
	if (!spos)
		return NULL;
	*spos = *pos;
	if (*spos < HASHTABLE_SIZE)
		return spos;
	kfree(spos);
	return NULL;
}

static void *hashlist_fnext(struct seq_file *m, void *v, loff_t *pos)
{
	loff_t *spos = v;
	*pos = ++(*spos);
	return (*spos < HASHTABLE_SIZE) ? spos : NULL;
}

static void hashlist_fstop(struct seq_file *m, void *v)
{
	kfree(v);
}

static int hashlist_fshow(struct seq_file *m, void *v)
{
	struct list_head *q;
	struct volume_list *vol = NULL;
	struct hashlist_line *l;
	loff_t *spos = v;
	struct hashlist_entry *my;
	struct hlist_node *pos;

	/* FIXME: This only works with one volume now. */
	list_for_each(q, &volumes) {
		vol = list_entry(q, struct volume_list, vlist);
	}
	if (!vol)
		return 0;
	l = vol->vhashes;
	seq_printf(m, "Line: %03Ld\n", *spos);
	read_lock(&l[*spos].bucket_lock);
	hlist_for_each_entry(my, pos, &l[*spos].entries, list) {
		int i;
		char c;
		switch (my->nodetype) {
		case EXECUTABLE_FILE:
			c = 'S';
			break;
		case STATIC_DATA_FILE:
			c = 'T';
			break;
		case DYNAMIC_DATA_FILE:
			c = 'X';
			break;
		case IMMUTABLE_DIRECTORY:
			c = 'D';
			break;
		case PROTECTED_DIRECTORY:
			c = 'P';
			break;
		default:
			c = '?';
			break;
		}
		seq_printf(m, "%ld\t%8ld\t", my->sid, my->ino);
		seq_printf(m, "(%d,%d,%d)%c %s\t", my->uid, my->gid,
			   my->mode, c, my->wcreds ? "creds " : "no    ");
		for (i = 0; i < SHA1_HASH_LENGTH; i++)
			seq_printf(m, "%02x", my->hash[i]);
		seq_printf(m, "\n");
	}
	read_unlock(&l[*spos].bucket_lock);
	return 0;
}

static const struct seq_operations hashlist_seqops = {
	.start = hashlist_fstart,
	.next = hashlist_fnext,
	.stop = hashlist_fstop,
	.show = hashlist_fshow
};

static int hashlist_open(struct inode *inode, struct file *file)
{
	if (validator_fsaccess(AEGIS_FS_HASHLIST_READ))
		return -EPERM;
	return seq_open(file, &hashlist_seqops);
}

/* hashlist seq_file hooks */
static const struct file_operations hashlist_fops = {
	.write = hashlist_write,
	.open = hashlist_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

/**
 * validator_hashlist_entry() - Check an item from the hashlist.
 * @inode: inode
 *
 * Test whether given inode is in a hashlist
 *
 * Return 0 if found and -ENOENT otherwise.
 */
int validator_hashlist_entry(struct inode *node)
{
	int i;
	unsigned char *bufptr = NULL;
	struct hashlist_entry *tmp;
	struct hashlist_line *hashlist;
	struct hlist_node *pos;

	if (node->i_sb->s_security == NULL)
		return -ENOENT;
	i = hash_long(node->i_ino, HASHTABLE_BITS);
	hashlist = node->i_sb->s_security;
	read_lock(&hashlist[i].bucket_lock);
	hlist_for_each_entry(tmp, pos, &hashlist[i].entries, list) {
		if (tmp->ino == node->i_ino) {
			bufptr = tmp->hash;
			break;
		}
	}
	read_unlock(&hashlist[i].bucket_lock);
	return bufptr ? 0 : -ENOENT;
}

/**
 * validator_hashlist_get_wcreds() - Get credentials from the hashlist.
 * @inode: inode
 *
 * Return const credentials data structure if found and otherwise NULL
 */
struct vprotection *validator_hashlist_get_wcreds(struct inode *node)
{
	int i;
	struct hashlist_entry *tmp;
	struct hashlist_line *hashlist;
	struct vprotection *vprot = NULL;
	struct hlist_node *pos;

	if (node->i_sb->s_security == NULL)
		return NULL;
	i = hash_long(node->i_ino, HASHTABLE_BITS);
	hashlist = node->i_sb->s_security;
	read_lock(&hashlist[i].bucket_lock);
	hlist_for_each_entry(tmp, pos, &hashlist[i].entries, list) {
		if (tmp->ino == node->i_ino) {
			vprot = tmp->wcreds;
			break;
		}
	}
	read_unlock(&hashlist[i].bucket_lock);
	return vprot;
}

/**
 * validator_hashlist_get_data() - Get an item from the hashlist.
 * @inode: inode
 * @data:  data structure to store reference hashlist data
 *
 * Get SHA1 reference hash that is assigned to inode if there is an entry.
 * The function also returns the source identifier as a parameter.
 *
 * Return 0 if found and otherwise -ENOENT. Fill data structure if found
 */
int validator_hashlist_get_data(struct inode *node, struct vmetadata *data)
{
	int i;
	unsigned char *bufptr = NULL;
	struct hashlist_entry *tmp;
	struct hashlist_line *hashlist;
	struct hlist_node *pos;

	if (node->i_sb->s_security == NULL)
		return -ENOENT;
	i = hash_long(node->i_ino, HASHTABLE_BITS);
	hashlist = node->i_sb->s_security;
	read_lock(&hashlist[i].bucket_lock);
	hlist_for_each_entry(tmp, pos, &hashlist[i].entries, list) {
		if (tmp->ino == node->i_ino) {
			bufptr = tmp->hash;
			memcpy(data->refhash, bufptr, SHA1_HASH_LENGTH);
			data->sid = tmp->sid;
			data->uid = tmp->uid;
			data->gid = tmp->gid;
			data->mode = tmp->mode;
			data->nodetype = tmp->nodetype;
			break;
		}
	}
	read_unlock(&hashlist[i].bucket_lock);
	return bufptr ? 0 : -ENOENT;
}

/**
 * validator_hashlist_fsinit() - Initialize securityfs entry for hashlist data
 * @top: securityfs parent directory
 *
 * Create securityfs entry "hashlist" to either add new hashlist entries or to
 * display a list of existing reference hashes (for debugging).
 *
 * Return file dentry for logging control (or null in case of an error)
 */
struct dentry *validator_hashlist_fsinit(struct dentry *top)
{
	return securityfs_create_file("hashlist", 0600, top, NULL,
				      &hashlist_fops);
}

/**
 * validator_hashlist_fscleanup() - Remove securityfs entry of hashlist display
 * @f: file dentry of the logging securityfs entry
 *
 * Remove "hashlist" securityfs entry.
 */
void validator_hashlist_fscleanup(struct dentry *f)
{
	if (f)
		securityfs_remove(f);
}

/**
 * validator_hashlist_new() - Create new reference hashlist structure.
 *
 * This function creates internal data structure for a list of reference
 * hashes. The pointer that is returned should be deallocated using
 * validator_hashlist_delete function. We are using inode numbers as an index
 * in our reference hash requests. If multiple mounted volumes are supported
 * then separate list is used for each volume. A pointer to that list is stored
 * in the superblock of the filesystem.
 *
 * Return pointer to an internal reference hashlist structure or NULL.
 */
void *validator_hashlist_new(void)
{
	int i;
	struct hashlist_line *newlist;
	struct volume_list *myvolume;

	newlist = kmalloc(HASHTABLE_SIZE * sizeof(*newlist), GFP_KERNEL);
	if (!newlist) {
		pr_err("Aegis: Unable to allocate hashlist data structure\n");
		return NULL;
	}
	myvolume = kmalloc(sizeof(*myvolume), GFP_KERNEL);
	if (!myvolume) {
		pr_err("Aegis: Unable to allocate volume list data struct\n");
		kfree(newlist);
		return NULL;
	}
	myvolume->vhashes = newlist;
	pr_info("Aegis: Creating new mount point hashlist %p\n", newlist);
	for (i = 0; i < HASHTABLE_SIZE; i++) {
		INIT_HLIST_HEAD(&newlist[i].entries);
		rwlock_init(&newlist[i].bucket_lock);
	}
	list_add_tail(&myvolume->vlist, &volumes);
	return newlist;
}

/**
 * validator_hashlist_delete() - Deallocate reference hashlist structure.
 * @ptr: Reference hashlist data structure
 *
 * This function deallocates an internal data structure for a list of reference
 * hashes. This data structure is allocated using validator_hashlist_new
 * function.
 */
void validator_hashlist_delete(void *ptr)
{
	struct hashlist_line *rlist = ptr;
	struct volume_list *p;
	struct list_head *h = NULL;
	int i;

	if (!ptr)
		return;
	list_for_each_entry(p, &volumes, vlist) {
		if (p->vhashes == ptr)
			h = &p->vlist;
	}
	if (h)
		list_del(h);
	for (i = 0; i < HASHTABLE_SIZE; i++) {
		struct hashlist_entry *tmp;
		struct hlist_node *pos;
		struct hlist_node *q;
		write_lock(&rlist[i].bucket_lock);
		hlist_for_each_safe(pos, q, &rlist[i].entries) {
			tmp = hlist_entry(pos, struct hashlist_entry, list);
			hlist_del(pos);
			free_wcreds_data(tmp);
			kfree(tmp);
		}
		write_unlock(&rlist[i].bucket_lock);
	}
	kfree(ptr);
}

/**
 * validator_hashlist_delete_item() - Remove an item from reference hashlist
 * @inode: inode entry to be removed
 *
 * When files are deleted corresponding hashlist reference entries should be
 * removed too. Otherwise when inode entries are reused those entries could
 * still be found in the reference hashlist, but most likely hash values
 * do not match.
 *
 * Return zero for success and negative value for an error.
 */
int validator_hashlist_delete_item(struct inode *inode)
{
	int i;
	int r = -ENOENT;
	struct hashlist_entry *tmp;
	struct hashlist_line *hashlist;
	struct hlist_node *pos;
	struct hlist_node *next;

	if (inode->i_sb->s_security == NULL)
		return -ENOENT;
	i = hash_long(inode->i_ino, HASHTABLE_BITS);
	hashlist = inode->i_sb->s_security;
	write_lock(&hashlist[i].bucket_lock);
	hlist_for_each_safe(pos, next, &hashlist[i].entries) {
		tmp = hlist_entry(pos, struct hashlist_entry, list);
		if (tmp->ino == inode->i_ino) {
			hlist_del(pos);
			free_wcreds_data(tmp);
			kfree(tmp);
			r = 0;
			break;
		}
	}
	write_unlock(&hashlist[i].bucket_lock);
	return r;
}
