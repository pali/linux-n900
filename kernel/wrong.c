/*
 * linux/kernel/wrong.c
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Author: Alexander Shishkin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/sched.h> /* for current */
#include <linux/seq_file.h>
#include <linux/module.h>

struct wrongdoer {
	char		name[TASK_COMM_LEN];
	const char	*felony;
	unsigned int	times;
};

#define WRONGDOERS_MAX 1024
static struct wrongdoer *wrongdoers[WRONGDOERS_MAX];
static int nwrongdoers;

void wrongdoer_log(const char *what)
{
	int i;

	if (nwrongdoers == WRONGDOERS_MAX) {
		printk(KERN_INFO "Too many wrongdoers here.\n");
		return;
	}

	for (i = 0; i < nwrongdoers; i++)
		if (!strcmp(current->comm, wrongdoers[i]->name) &&
				wrongdoers[i]->felony == what)
			break;

	if (i == nwrongdoers) {
		wrongdoers[i] = vmalloc(sizeof(*wrongdoers[i]));
		if (!wrongdoers[i])
			return;

		strcpy(wrongdoers[i]->name, current->comm);
		wrongdoers[i]->felony = what;
		wrongdoers[i]->times = 1;

		nwrongdoers++;

		printk(KERN_INFO "Wrongdoer pid %d, \"%s\" caught %s\n",
				current->pid, current->comm, what);
	} else
		wrongdoers[i]->times++;
}

static void *wrong_start(struct seq_file *s, loff_t *pos)
{
	if (*pos < 0 || *pos >= nwrongdoers)
		return NULL;

	return &wrongdoers[*pos];
}

static void *wrong_next(struct seq_file *s, void *p, loff_t *pos)
{
	struct wrongdoer **w = p;
	loff_t n = ((unsigned long)w - (unsigned long)wrongdoers) / sizeof(*w);

	if (++n >= nwrongdoers)
		return NULL;

	*pos = n;

	return &wrongdoers[n];
}

static void wrong_stop(struct seq_file *s, void *v)
{
}

static int wrong_show(struct seq_file *s, void *p)
{
	struct wrongdoer **w = p;

	seq_printf(s, "%-15s: %s [%d]\n", (*w)->name, (*w)->felony,
			(*w)->times);

	return 0;
}

static const struct seq_operations wrong_op = {
	.start = wrong_start,
	.next = wrong_next,
	.stop = wrong_stop,
	.show = wrong_show,
};

static int wrong_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &wrong_op);
}

static const struct file_operations wrong_fops = {
	.owner   = THIS_MODULE,
	.open    = wrong_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static int __init wrongdoer_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("wrongdoers", 0, NULL);
	if (!entry)
		return -ENOMEM;

	entry->proc_fops = &wrong_fops;

	return 0;
}

core_initcall(wrongdoer_init);

