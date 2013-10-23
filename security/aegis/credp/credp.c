/*
 * This file is part of AEGIS
 *
 * Copyright (C) 2009-2011 Nokia Corporation
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
 * Author: Markku Savela
 */

/* Note: This module does not currently work properly on mixed 64/32 bit
 * environments. */

#ifdef CONFIG_SECURITY_AEGIS_RESTOK
#define HAVE_RESTOK /* If restok is built in, it's available */
#endif /* CONFIG_SECURITY_AEGIS_RESTOK */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/err.h>
#include <linux/securebits.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/personality.h>
#include <linux/seq_file.h>
#include <linux/ptrace.h>
#include <linux/aegis/creds.h>
#include <linux/aegis/credp.h>
#include <linux/aegis/credpctl.h>

#ifdef HAVE_RESTOK
#include <linux/aegis/restok.h>
#define is_restok_range(id) \
	((id) >= RESTOK_GROUP_MIN && (id) <= RESTOK_GROUP_MAX)
#else
#define is_restok_range(id) 0
#endif

#define CREDP_NAME "credp"

#undef INFO
#undef ERR
#define INFO(args...) pr_info(CREDP_NAME ": " args)
#define ERR(args...) pr_err(CREDP_NAME ": " args)


#define NOBODY_UID 65534
#define NOBODY_GID 65534

/*
 * Temporary hack to avoid initial need of patching securebits.h, and
 * all other places where setuid/setgid executables are handled.
 *
 * SECURE_AEGIS_LOCKED is set when policy has been applied to current
 * task or some ancestor of the current. This activates Aegis
 * Inheritance Mode (overrides the legacy Linux way of handling
 * capabilities and root).
 *
 * When no policy applies to the execve target, then if SECURE_AEGIS
 * is set, the current capabilities and tokens are inherited, and if
 * SECURE_AEGIS is off, the capablities and tokes will not inherit.
 *
 * When the target has a policy, it defines the inheritance mode.
 *
 * REVISIT: This means that the bit selection here must be
 * verified against each new upstream kernel that is taken
 * into use.
 */
#define SECURE_AEGIS 6
#define SECURE_AEGIS_LOCKED 7

/* The set of securebits locked and managed by Aegis */
#define SECURE_AEGIS_BITS				\
	(issecure_mask(SECURE_NOROOT) |			\
	 issecure_mask(SECURE_AEGIS))
#define SECURE_AEGIS_LOCKS (SECURE_AEGIS_BITS << 1)
#define SECURE_AEGIS_MASK				\
	(issecure_mask(SECURE_AEGIS) | issecure_mask(SECURE_AEGIS_LOCKED))
#define SECURE_NOT_INHERITABLE (issecure_mask(SECURE_AEGIS_LOCKED))
#if (SECURE_AEGIS_MASK & (SECURE_ALL_BITS|SECURE_ALL_LOCKS))
/*
 * Fail compilation, if the bits chosen above overlap something
 * defined in securebits.h
 */
#error "Reassign SECURE_AEGIS bit position in securebits"
#endif

/*
 * Define the credential name that contains the origin
 * credential checking policy.
 */
#define ORIGIN_CHECK_ID "SRC::SRC"

/**
 * struct policy_creds - Subset of credential information currently used
 * @uid: User id
 * @gid: Group id
 * @caps: Capabilities
 * @groups: Supplementary groups
 */
struct policy_creds {
	uid_t uid;
	gid_t gid;
	kernel_cap_t caps;
	struct group_info *groups;
};

/**
 * struct policy - Mapping of credentials to an executable or identifier
 * @list: Linked either by name_hash or byid_hash
 * @pcreds: Actual credentials set by this policy
 * @flags: Type and modifiers for the policy processing
 * @id1: Used in path based policies (superblock pointer)
 * @id2: Used in path based policies and policies by identifier
 * @size: Length of the name
 * @name: Name (path) associated with the policy
 */
struct policy {
	struct list_head list;
	struct policy_creds pcreds;
	int flags;
	const void *id1;
	long id2;
	unsigned short size;
	char name[];
};

/* '-1' is used as magic in kernel (see kernel/sys.c) for unset
 * uid/gid values. There does not appear to be any definition for
 * it. */
static const struct policy_creds policy_creds_init = {
	.uid = (uid_t) -1,
	.gid = (gid_t) -1,
};

/*
 * Mutual exclusion for policy structures
 */
static DEFINE_MUTEX(mutex);

#define BYID_HASH 37
static struct list_head byid_hash[BYID_HASH];
#define NAME_HASH 127
static struct list_head name_hash[NAME_HASH];

static const struct policy_creds *enforce_origin_checking;
/*
 * A preallocated empty supplementary groups (always non-NULL after
 * succesful init, cannot be "const *", because of reference counting
 * get_group_info/put_group_info requirements)
 */
static struct group_info *empty_groups;

/**
 * authorized_p - Test if application is authorized
 *
 * Return 1, if the current task is authorized to update the policies,
 * and 0 zero otherwise.
 */
static int authorized_p(void)
{
#ifdef HAVE_RESTOK
	return restok_has_tcb();
#else
	return capable(CAP_MAC_ADMIN);
#endif
}

/**
 * credp_task_setgroups - Don't allow groups in restok range
 *
 * Deny operation, if any of the group numbers fall into restok range.
 *
 * This is a LSM hook.
 */
int credp_task_setgroups(struct group_info *group_info)
{
	int i;
	/*
	 * This LSM hook is called from set_groups, which is also used
	 * by this module to set the credentials. In such case setting
	 * the restok tokens should be allowed. The calls from this
	 * module are either in_execve, or include new tokens only
	 * if task has 'tcb'.
	 */
	if (current->in_execve || authorized_p())
		return 0;

	for (i = 0; i < group_info->ngroups; ++i) {
		const gid_t grp = GROUP_AT(group_info, i);
		/* Allow normal group ids set as before */
		if (!is_restok_range(grp))
			continue;
		/*
		 * Disallow restok tokens, unless they are already
		 * present in the current context. This allows
		 * getgroups - setgroups sequence to pass even with
		 * tokens, if no new ones have been added.
		 */
		if (!in_egroup_p(grp))
			return -EPERM;
	}
	return 0;
}

/**
 * credp_task_setgid - Don't allow gids in restok range
 *
 * Deny operation, if any id falls into restok range.
 *
 * This is a LSM hook.
 */
int credp_task_setgid(gid_t id0, gid_t id1, gid_t id2, int flags)
{
	if (is_restok_range(id0) ||
	    is_restok_range(id1) ||
	    is_restok_range(id2))
		return -EPERM;
	return 0;
}


/**
 * is_subset - Test if supplementary groups are included in other
 * @subset: Subset credentials
 * @set: The credentials to test against
 *
 * Test only the subset status of groups information. Other required
 * subset testing of credentials has already been done by the mainline
 * code.
 *
 * Return 0, if groups of 'subset' are subset of 'set', otherwise
 * return -EPERM.
 */
static int is_subset(const struct cred *subset, const struct cred *set)
{
	int i, j;

	/*
	 * The egid is not always included in the supplementary
	 * groups, and its subset status must be checked separately.
	 */
	if (subset->egid != set->egid &&
	    !groups_search(set->group_info, subset->egid))
		return -EPERM;
	/*
	 * Assume groups are sorted into ascending order. Also the
	 * same group number can occur multiple times in the lists.
	 */
	for (i = j = 0; i < subset->group_info->ngroups; ++i) {
		gid_t grp = GROUP_AT(subset->group_info, i);

		if (grp == set->egid)
			continue;

		for (;; ++j) {
			gid_t g;

			if (j == set->group_info->ngroups)
				return -EPERM;
			g = GROUP_AT(set->group_info, j);
			if (g > grp)
				return -EPERM;
			if (g == grp)
				break;
		}
	}
	return 0;
}

/**
 * credp_ptrace_access_check - Deny if child has more creds than current task
 *
 * Implements ptrace_access_check LSM hook
 */
int credp_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
	int rc = cap_ptrace_access_check(child, mode);
	if (rc)
		return rc;

	if (authorized_p())
		return 0; /* allow, if has 'tcb' */
	if (mode == PTRACE_MODE_READ && capable(CAP_SYS_PTRACE))
		return 0;

	rcu_read_lock();
	rc = is_subset(__task_cred(child), current_cred());
	rcu_read_unlock();
	return rc;
}

/**
 * credp_ptrace_traceme - Deny if current has more creds than parent task
 *
 * Implements ptrace_traceme LSM hook
 */
int credp_ptrace_traceme(struct task_struct *parent)
{
	int rc = cap_ptrace_traceme(parent);
	if (rc)
		return rc;
	rcu_read_lock();
	rc = is_subset(current_cred(), __task_cred(parent));
	rcu_read_unlock();
	return rc;
}

/**
 * policy_creds_clear - Release allocations from policy_creds
 * @pcreds: The structure to clear (must be non-NULL)
 */
static void policy_creds_clear(struct policy_creds *pcreds)
{
	if (pcreds->groups)
		put_group_info(pcreds->groups);
	*pcreds = policy_creds_init;
}

/**
 * find_policy_byname - Find policy by unique name string
 * @name: The name to search
 * @bucket: Return hash bucket index to name_hash
 */
static struct policy *find_policy_byname(const char *name, unsigned int *bucket)
{
	struct policy *p;
	const size_t size = strlen(name);

	*bucket = full_name_hash(name, size) % NAME_HASH;
	list_for_each_entry(p, &name_hash[*bucket], list)
		if (size == p->size && memcmp(name, p->name, size) == 0)
			return p;
	return NULL;
}

/**
 * find_policy_byid - Find policy by unique id
 * @id: Unique identifier
 * @bucket: Returns hash bucket index to byid_hash
 */
static struct policy *find_policy_byid(long id, unsigned int *bucket)
{
	struct policy *p;

	/* This simple hash should be enough: the id's are currently
	 * allocated sequentially starting from a base value in restok
	 * component. */
	*bucket = ((unsigned long)id) % BYID_HASH;
	list_for_each_entry(p, &byid_hash[*bucket], list)
		if (p->id2 == id)
			return p;
	return NULL;
}

/**
 * find_policy_bypath - Find policy by path
 * @path: The path
 *
 * If the path begins with '/', locate policy by the name (using the
 * path as a name). Otherwise, if restok defines the path into unique
 * identifier, locate policy by that id.
 *
 * Returns NULL, if policy cannot be found.
 */
static struct policy *find_policy_bypath(const char *path)
{
	unsigned int dummy;

	if (*path == '/')
		return find_policy_byname(path, &dummy);
#ifdef HAVE_RESTOK
	{
		long id = restok_locate(path);
		if (id > 0)
			return find_policy_byid(id, &dummy);
	}
#endif
	return NULL;
}

/**
 * credp_seq_position - Find the policy corresponding to pos
 * @pos: The position in file
 *
 * The 'pos' is treated as a sequence number in hash_byid/hash_name
 * when the policies are virtually numbered in the order they appear
 * in the hashes. First policy is number 0.
 */
static struct policy *credp_seq_position(loff_t pos)
{
	loff_t off = 0;
	int bucket;
	struct list_head *p;

	/* REVISIT: This could be speeded up by keeping a counter
	 * of entries in each hash bucket? */

	for (bucket = 0; bucket < BYID_HASH; ++bucket)
		list_for_each(p, &byid_hash[bucket]) {
			if (off == pos)
				return list_entry(p, struct policy, list);
			++off;
		}
	for (bucket = 0; bucket < NAME_HASH; ++bucket)
		list_for_each(p, &name_hash[bucket]) {
			if (off == pos)
				return list_entry(p, struct policy, list);
			++off;
		}
	return NULL;
}

static void *credp_seq_start(struct seq_file *m, loff_t *pos)
{
	struct policy *policy;

	mutex_lock(&mutex);
	policy = credp_seq_position(*pos);
	if (policy)
		return policy;
	mutex_unlock(&mutex);
	return NULL;
}

static void credp_seq_stop(struct seq_file *m, void *v)
{
	if (v)
		mutex_unlock(&mutex);
}

static void *credp_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct policy *policy = credp_seq_position(++*pos);
	if (policy == NULL)
		mutex_unlock(&mutex);
	return policy;
}

static int credp_seq_show(struct seq_file *m, void *v)
{
	unsigned i;
	const struct policy *policy = v;

	if (!policy)
		return -EFAULT;
	if (policy->id1)
		seq_printf(m, "%x.%ld", (int)policy->id1, policy->id2);
	else
		seq_printf(m, "GRP::%ld", policy->id2);

	i = policy->flags & CREDP_TYPE_MASK;
	seq_printf(m, " %s [%s", policy->name,
		   (i == CREDP_TYPE_NONE) ? "" :
		   (i == CREDP_TYPE_ADD) ? "add" :
		   (i == CREDP_TYPE_SET) ? "set" :
		   (i == CREDP_TYPE_INHERIT) ? "inherit" : "unknown");
	if (i != CREDP_TYPE_NONE) {
		if ((policy->flags & CREDP_TYPE_INHERITABLE) == 0)
			seq_puts(m, " not-inheritable");
		if ((policy->flags & CREDP_TYPE_SETUID) == 0)
			seq_puts(m, " setxid");
	}
	seq_puts(m, "]\n");

	if (policy->pcreds.uid != (uid_t) -1)
		seq_printf(m, "\tUID::%d\n", policy->pcreds.uid);
	if (policy->pcreds.gid != (gid_t) -1)
		seq_printf(m, "\tGID::%d\n", policy->pcreds.gid);
	if (policy->pcreds.groups && policy->pcreds.groups->ngroups > 0) {
		for (i = 0; i < policy->pcreds.groups->ngroups; ++i) {
			const gid_t g = GROUP_AT(policy->pcreds.groups, i);
			seq_printf(m, "\tGRP::%d\n", g);
		}
	}
	CAP_FOR_EACH_U32(i) {
		const unsigned long caps = policy->pcreds.caps.cap[i];
		unsigned b;
		for_each_bit(b, &caps, 32)
			seq_printf(m, "\tCAP::%d\n", 32*i+b);
	}
	return 0;
}

static const struct seq_operations credp_seq_ops = {
	.start = credp_seq_start,
	.next = credp_seq_next,
	.stop = credp_seq_stop,
	.show = credp_seq_show
};

/**
 * purge_tokens - Remove all tokens from a groups
 * @groups: The groups structure
 * @keep_old: Non-zero, if currently owned tokens should remain
 *
 * Return possibly modified new groups structure which must be
 * released by put_group_info.
 */
static struct group_info *purge_tokens(struct group_info *groups, int keep_old)
{
	const struct group_info *now = current_cred()->group_info;
	struct group_info *new;
	int count = 0;
	int i;

	if (!groups)
		goto empty;

	/* Count the final size of the new groups */
	for (i = 0; i < groups->ngroups; ++i) {
		const gid_t g = GROUP_AT(groups, i);

		if (!is_restok_range(g) ||
		    (keep_old && groups_search(now, g)))
			++count;
	}
	if (groups->ngroups == count)
		/* No changes required */
		return get_group_info(groups);

	/* Construct a new version of groups */
	if (count == 0)
		goto empty;
	new = groups_alloc(count);
	if (!new)
		goto empty;

	/* Fill in new groups without restok tokens */
	count = 0;
	for (i = 0; i < groups->ngroups; ++i) {
		const gid_t g = GROUP_AT(groups, i);
		if (!is_restok_range(g) ||
		    (keep_old && groups_search(now, g))) {
			GROUP_AT(new, count) = g;
			++count;
		}
	}
	return new;
empty:
	return get_group_info(empty_groups);
}


/**
 * groups_intersect - Intersect groups information with current
 * @groups: The groups to intersect with current groups
 *
 * Returns NULL only if a groups could not be allocated.  Otherwise,
 * returns non-NULL group_info, which must be released by a call to
 * put_group_info.
 */
static struct group_info *groups_intersect(struct group_info *groups)
{
	const struct group_info *now = current_cred()->group_info;
	struct group_info *new;
	int common = 0, missed = 0;
	int i;

	/* If the current supplementary groups is empty, then the
	 * intersection is also empty */
	if (now->ngroups == 0)
		return get_group_info(empty_groups);

	/* If the policy defines empty supplementary set, but as the
	 *  current set is not empty, we need to replace the current
	 *  one with an empty set. */
	if (!groups || groups->ngroups == 0)
		return get_group_info(empty_groups);

	/* The now->groups is sorted, but groups in policy is not.  A
	 * somewhat brute force approach below... */
	for (i = 0; i < groups->ngroups; ++i)
		if (groups_search(now, GROUP_AT(groups, i)))
			++common;/* The group exists in both */
		else
			++missed;/* The group does not exist in current */
	/* If all groups are present in current groups, just use the
	 * groups as is */
	if (missed == 0)
		return get_group_info(groups);

	/* Could still check if now is a true subset of new groups,
	 * and just leave current to be used, but, for now just give
	 * up and allocate a new groups. */
	new = groups_alloc(common);
	if (!new)
		return NULL;

	common = 0;
	for (i = 0; i < groups->ngroups; ++i) {
		const gid_t g = GROUP_AT(groups, i);
		if (groups_search(now, g)) {
			GROUP_AT(new, common) = g;
			++common;
		}
	}
	return new;
}

/**
 * groups_combine - Combine groups information with current
 * @groups: The groups to combine with current groups
 *
 * Returns NULL only if a groups could not be allocated.  Otherwise,
 * returns non-NULL group_info, which must be released by a call to
 * put_group_info.
 */
static struct group_info *groups_combine(struct group_info *groups)
{
	struct group_info *now = current_cred()->group_info;
	struct group_info *new;
	int missed = 0;
	int i;

	/* If the to be combine set is empty, then the current set is
	 * not changed */
	if (!groups || groups->ngroups == 0)
		return get_group_info(now);
	/* If the current groups is empty, then the combined set is
	 * the to be combined set */
	if (now->ngroups == 0)
		return get_group_info(groups);

	/* The now->groups is sorted, but groups in policy is not.  A
	 * somewhat brute force approach below... */
	for (i = 0; i < groups->ngroups; ++i)
		if (!groups_search(now, GROUP_AT(groups, i)))
			++missed;/* The group does not exist in current */

	/* If all groups are present in current groups, just use the
	 * current as is */
	if (missed == 0)
		return get_group_info(now);

	missed += now->ngroups;
	new = groups_alloc(missed);
	if (!new)
		return NULL;
	/* First, just copy the current groups */
	for (i = 0; i < now->ngroups; ++i)
		GROUP_AT(new, i) = GROUP_AT(now, i);
	/* Now add the missing groups */
	for (missed = 0; missed < groups->ngroups; ++missed) {
		const gid_t g = GROUP_AT(groups, missed);
		if (!groups_search(now, g)) {
			GROUP_AT(new, i) = g;
			++i;
		}
	}
	return new;
}

#define APPLY_PCREDS_ID_CHANGED 1
#define APPLY_PCREDS_SETGID 2
#define APPLY_PCREDS_SETUID 4

/**
 * apply_pcreds - Apply pcreds to a new creds
 * @flags: The policy type and modifiers
 * @pcreds: The credentials to be applied
 * @new: The new credentials being built
 * @tcb: Non-zero, if everything is allowed
 *
 * Return negative error code on failure,
 * On success, return non-negative, with possible flags set
 * APPLY_PCREDS_ID_CHANGED is set, if effective UID or GID changed
 * APPLY_PCREDS_SETGID is set, if real GID should be set from effective GID
 * APPLY_PCREDS_SETUID is set, if real UID should be set from effective UID
 *
 * REVISIT: The above special returns (instead of simple success = 0)
 * are needed, because when called from 'apply', the following
 * commoncap.c code paths check for changed uid/gid from the new
 * credentials. If the checks were made between current and new
 * credentials, this function could do GID/UID settings already
 * here. [Pending on fix into commoncap.c]
 *
 * NOTE: If efficiency becomes an issue, the 'tcb' enabled path is the
 * critical path (used by apply triggered by execve).
 */
static int apply_pcreds(int flags, const struct policy_creds *pcreds,
			struct cred *new, int tcb)
{
	const struct cred *old = current_cred();
	struct group_info *groups = NULL;
	gid_t gid = pcreds->gid;
	uid_t uid = pcreds->uid;
	int ret = 0;
	int ret_flags = 0;
	int inheritable = (SECURE_AEGIS_MASK & old->securebits) !=
		SECURE_NOT_INHERITABLE;
	int action;

	/* Handle the securebits */
	new->securebits |= SECURE_AEGIS_BITS | SECURE_AEGIS_LOCKS;
	if ((flags & CREDP_TYPE_INHERITABLE) == 0)
		new->securebits &= ~issecure_mask(SECURE_AEGIS);

	/* Handle UID change, if any */
	if (uid != (uid_t) -1) {
		if (!(tcb || uid == old->uid || capable(CAP_SETUID)))
			uid = NOBODY_UID;
		new->suid = new->euid = new->fsuid = uid;
		ret_flags |= APPLY_PCREDS_ID_CHANGED;
		if (flags & CREDP_TYPE_SETUID)
			ret_flags |= APPLY_PCREDS_SETUID;
	}

	/* Handle GID change, if any */
	if (gid != (gid_t) -1) {
		if (is_restok_range(gid)) /* Cannot use token as GID */
			gid = NOBODY_GID;
		else if (!(tcb || gid == old->gid || capable(CAP_SETGID)))
			gid = NOBODY_GID;
		new->sgid = new->egid = new->fsgid = gid;
		ret_flags |= APPLY_PCREDS_ID_CHANGED;
		if (flags & CREDP_TYPE_SETUID)
			ret_flags |= APPLY_PCREDS_SETGID;
	}

	/*
	 * If current credentials are not inheritable, then
	 * the ADD mode must be handled as SET
	 */
	action = flags & CREDP_TYPE_MASK;
	if (action == CREDP_TYPE_ADD && !inheritable)
		action = CREDP_TYPE_SET;

	switch (action) {
	case CREDP_TYPE_SET:
		if (tcb || capable(CAP_SETPCAP))
			new->cap_permitted = cap_intersect(
				old->cap_bset,
				pcreds->caps);
		else if (inheritable)
			new->cap_permitted = cap_intersect(
				old->cap_effective,
				pcreds->caps);
		else
			cap_clear(new->cap_permitted);
		if (!pcreds->groups)
			groups = get_group_info(empty_groups);
		else if (tcb)
			/* allow everything */
			groups = get_group_info(pcreds->groups);
		else if (capable(CAP_SETGID))
			/* allow all groups, and old tokens if inheritable */
			groups = purge_tokens(pcreds->groups, inheritable);
		else if (inheritable)
			/* allow only what we have or less */
			groups = groups_intersect(pcreds->groups);
		else
			/*
			 * not inheritable, and no privs to set
			 * anything new -- allow nothing
			 */
			groups = get_group_info(empty_groups);
		if (!groups) {
			/* Remove groups, even on fail! */
			groups = get_group_info(empty_groups);
			ret = -ENOMEM;
		}
		break;
	case CREDP_TYPE_INHERIT:
		/*
		 * For capabilities, intersect with the current
		 * effective set
		 */
		new->cap_permitted = cap_intersect(
			old->cap_effective,
			pcreds->caps);
		/*
		 * Set only common groups from the current
		 * supplementary groups
		 */
		groups = groups_intersect(pcreds->groups);
		if (!groups) {
			/* Remove groups, even on fail! */
			groups = get_group_info(empty_groups);
			ret = -ENOMEM;
		}
		/*
		 * If the old policy was NOT-INHERITABLE, a policy
		 * with INHERIT must not change the NOT-INHERITABLE
		 * state -- the new policy is also NOT-INHERITABLE
		 */
		if (!inheritable)
			new->securebits &= ~issecure_mask(SECURE_AEGIS);
		break;
	case CREDP_TYPE_ADD:
		/* Adds to current capabilities */

		if (tcb || capable(CAP_SETPCAP)) {
			/* Don't add anything not in cap_bset' */
			new->cap_permitted = cap_intersect(
				old->cap_bset, pcreds->caps);
			/* Combine the result of above with old caps */
			new->cap_permitted = cap_combine(
				old->cap_effective, new->cap_permitted);
		}
		if (tcb) {
			groups = groups_combine(pcreds->groups);
		} else if (capable(CAP_SETGID)) {
			groups = purge_tokens(pcreds->groups, 1);
			if (!groups)
				ret = -ENOMEM;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (groups) {
		/*
		 * set the groups even if we are about to return an
		 * error, and possible error from set_groups is
		 * returned only if we were ok before this.
		 */
		int tmp = set_groups(new, groups);
		ret = ret ? ret : tmp;
		put_group_info(groups);
	}
	if (ret == 0)
		ret |= ret_flags;
	return ret;
}

/**
 * credp_apply - Apply policy to an executable.
 * @bprm: The linux binprm structure
 * @effective: Set TRUE, if capabilities should be set from permitted
 *
 * Return 0, on success (whether policy applied or not)
 * Return negative error, on fatal errors
 */
int credp_apply(struct linux_binprm *bprm, bool *effective)
{
	struct policy *policy;
	struct inode *inode;
	unsigned int i;
	int ret = 0;
	const struct cred *old = current_cred();
	struct cred *new = bprm->cred;

	/* REVISIT: The following NULL tests are most likely
	 * unnecessary, because the "bprm->file" should have been
	 * successfully opened by open_exec. Leave them in for now and
	 * return 0 -- let caller handle it */
	if (!bprm->file || !bprm->file->f_dentry)
		return 0;
	inode = bprm->file->f_dentry->d_inode;
	if (!inode)
		return 0;

	if ((issecure_mask(SECURE_AEGIS_LOCKED) & new->securebits) == 0) {
		/*
		 * Unless the policy matches, the caller should apply
		 * legacy handling, as if the apply was not called at
		 * all: file 'setuid/setgid' and capabilities are
		 * honoured. However, never allow group numbers in
		 * token range to be used as a GID.
		 */
		if (is_restok_range(new->egid))
			new->egid = old->egid;
	} else {
		/* By default, inherit the capabilities, gid and uid.
		 * This cancels file 'setuid/setgid' processing, and
		 * file capabilities. */
		new->euid = old->euid;
		new->egid = old->egid;
		new->cap_permitted = old->cap_permitted;
		*effective = !cap_isclear(new->cap_permitted);
	}

	mutex_lock(&mutex);
	/*
	 * If being traced/debugged, just fall back to plain
	 * inheritance, no policy applied, no elevated credentials,
	 * unless task has 'tcb'.
	 */
	if ((bprm->unsafe & (LSM_UNSAFE_PTRACE | LSM_UNSAFE_PTRACE_CAP)) &&
	    !authorized_p())
		goto no_policy;

	/* HACK: There is a problem with interpeters: the mainline
	 * code clears capabilities (in commoncap.c:get_file_caps) and
	 * euid/egid (in exec.c:prepare_binprm). If a policy has been
	 * set on the script, we want it to apply when the final
	 * interpreter is run. To achieve this, we add a (HACK) bit
	 * policy_path into linux_binprm. When this is bit is set, we
	 * accept the policy based on the path only (no id
	 * checks). This assumes that the bprm->filename still
	 * contains the original path.  The bit is set when policy
	 * matches the original script path with id's checked.
	 *
	 * REVISIT: This solution may have some unexplored holes that
	 * need to be examined later and possible more secure solution
	 * generated.
	 */
	policy = find_policy_byname(bprm->filename, &i);
	if (!policy)
		goto no_policy;
	/* Skip id checks if policy_path is set */
	if (!bprm->policy_path && (policy->id1 != inode->i_sb ||
				   policy->id2 != inode->i_ino))
		goto no_policy;
	bprm->policy_path = 1;
	ret = apply_pcreds(policy->flags, &policy->pcreds, new, 1);
	if (ret > 0) {
		bprm->per_clear |= PER_CLEAR_ON_SETID;
		if (ret & APPLY_PCREDS_SETUID)
			bprm->hard_setuid = 1;
		if (ret & APPLY_PCREDS_SETGID)
			bprm->hard_setgid = 1;
		ret = 0;
	}
	*effective = !cap_isclear(new->cap_permitted);
	goto out;

no_policy:
	/* New executable has no policy defined */

	if ((SECURE_AEGIS_MASK & new->securebits) == SECURE_NOT_INHERITABLE) {

		/* Not inheritable mode -- remove caps and tokens */

		struct group_info *groups;

		cap_clear(new->cap_permitted);
		*effective = false;
		groups = purge_tokens(new->group_info, 0);
		ret = set_groups(new, groups);
		put_group_info(groups);
	}
out:
	mutex_unlock(&mutex);
	return ret;
}

/**
 * pcreds_from_user - Initialize policy_creds structure from user space
 * @pcreds: The policy_creds to be initialized/loaded
 * @list: Start of the credentials TLV array in user space
 * @list_length: Number of the entries in the the list
 *
 * Return 0, when policy_creds successfully initialized. And must be
 * cleared by policy_creds_clear, when the content is thrown away.
 *
 * Return negative error on any failures. The policy_creds is set to
 * initialized cleared state. Thus, on error return, no
 * policy_creds_clear is needed.
 */
static int pcreds_from_user(struct policy_creds *pcreds,
			    const __u32  __user *list, size_t list_length)
{
	int index = 0;
	int retval;
	*pcreds = policy_creds_init;

	if (!access_ok(VERIFY_READ, list, sizeof(*list) * list_length))
		return -EINVAL;

	/* Retrieve pcreds items */
	while (index < list_length) {
		u32 tl, v;
		int j;

		retval = __get_user(tl, &list[index]);
		if (retval)
			goto out;
		retval = -EINVAL;
		++index;
		if (index + CREDS_TLV_L(tl) > list_length)
			goto out;

		/* Prepare processing the value of one TLV */
		switch (CREDS_TLV_T(tl)) {
		case CREDS_CAP:
			if (CREDS_TLV_L(tl) > _KERNEL_CAPABILITY_U32S)
				goto out;
			break;
		case CREDS_UID:
		case CREDS_GID:
			if (CREDS_TLV_L(tl) != 1)
				goto out;
			break;
		case CREDS_GRP:
			if (pcreds->groups)
				goto out;
			pcreds->groups = groups_alloc(CREDS_TLV_L(tl));
			if (!pcreds->groups) {
				retval = -ENOMEM;
				goto out;
			}
			break;
		default:
			goto out;
		}
		/* Retrieve and process one TLV value */
		for (j = 0; j < CREDS_TLV_L(tl); ++j) {
			retval = __get_user(v, &list[index + j]);
			if (retval)
				goto out;
			switch (CREDS_TLV_T(tl)) {
			case CREDS_CAP:
				while (v) {
					const int cap = j * 32 + __ffs(v);
					if (cap > CAP_LAST_CAP)
						goto tlv_done;
					cap_raise(pcreds->caps, cap);
					v &= v - 1; /* turn bit off  */
				}
				break;
			case CREDS_UID:
				pcreds->uid = v;
				break;
			case CREDS_GID:
				pcreds->gid = v;
				break;
			case CREDS_GRP:
				GROUP_AT(pcreds->groups, j) = v;
				break;
			default:
				break;
			}
		}
tlv_done:
		index += CREDS_TLV_L(tl);
	}
	return 0;
out:
	policy_creds_clear(pcreds);
	return retval;
}

/**
 * credp_kload - Define a policy for a path
 * @flags: For now, contains the policy type.
 * @path: The path to an executable
 * @list: The list of TLV formatted credentials for the path
 * @list_length: The total length of the list in units of __u32
 *
 * If the path does not resolve (e.g. if the file cannot be
 * opened), then the policy is just stored and it will not
 * affect any execve. It can only be activate via
 * explicit call to exec_policy_activate.
 *
 * Return zero if operation successfull.
 */
long credp_kload(int flags, const char *path,
		 const __u32 __user *list, size_t list_length)
{
	long retval;
	unsigned int bucket;
	struct policy_creds pcreds;
	struct policy *policy;
	void *id1;
	long id2;
	size_t size;
	struct list_head *hashlist;

	if (!authorized_p())
		return -EPERM;
	if (!path)
		return -EINVAL;

	retval =  pcreds_from_user(&pcreds, list, list_length);
	if (retval)
		return retval;

	if (*path == '/') {
		/* When a policy is bound to an executable path, it is
		 * matched in apply as is. We must somehow guarantee
		 * that if a chroot is in effect at apply time, the
		 * policy will not match, even if the path is same.
		 *
		 * The current remedy is to store the superblock
		 * pointer and inode number into policy. These must
		 * also match the executable in apply, before the
		 * policy applies.
		 *
		 * This is a hack that works for our purposes
		 * now. Some other solutions in future, if needed..
		 */
		struct file *file;

		file = filp_open(path, O_RDONLY, 0);
		if (IS_ERR(file)) {
			retval = PTR_ERR(file);
			goto out1;
		}
		id1 = file->f_dentry->d_inode->i_sb;
		id2 = file->f_dentry->d_inode->i_ino;
		fput(file);

		mutex_lock(&mutex);
		policy = find_policy_byname(path, &bucket);
		size = strlen(path);
		hashlist = &name_hash[bucket];
	} else {
		retval = -EINVAL;
#ifdef HAVE_RESTOK
		id1 = NULL;
		id2 = restok_locate(path);
		if (id2 <= 0)
			goto out1;
		mutex_lock(&mutex);
		policy = find_policy_byid(id2, &bucket);
		size = 0;
		hashlist = &byid_hash[bucket];
#else
		goto out1;
#endif
	}
	if (!policy) {
		/* A new policy */
		policy = kmalloc(offsetof(struct policy, name) +
			       size + 1, GFP_KERNEL);
		if (policy == NULL) {
			retval = -ENOMEM;
			goto out2;
		}
		list_add_tail(&policy->list, hashlist);
		policy->pcreds = policy_creds_init;
		policy->size = size;
		if (size)
			memcpy(policy->name, path, size);
		policy->name[size] = 0;
	} else {
		/* Update an existing policy */
		policy_creds_clear(&policy->pcreds);
	}
	policy->flags = flags;
	policy->id1 = id1;
	policy->id2 = id2;
	policy->pcreds = pcreds;
	pcreds = policy_creds_init;

	/* A hack that enables origin checking when
	 * a specific policy for origin id is loaded.
	 */
	if (strcmp(path, ORIGIN_CHECK_ID) == 0)
		enforce_origin_checking = &policy->pcreds;
	retval = 0;
out2:
	mutex_unlock(&mutex);
out1:
	policy_creds_clear(&pcreds);
	return retval;
}

/**
 * credp_kunload: Remove the policy for the path
 * @path: The path that select the policy
 */
long credp_kunload(const char *path)
{
	struct policy *policy;
	long ret = -ENOENT;

	if (!authorized_p())
		return -EPERM;

	mutex_lock(&mutex);
	policy = find_policy_bypath(path);
	if (policy) {
		list_del(&policy->list);
		/* Disable origin checking, if it was enabled
		 * by this policy (see 'credp_kload'). */
		if (enforce_origin_checking == &policy->pcreds)
			enforce_origin_checking = NULL;
		policy_creds_clear(&policy->pcreds);
		kfree(policy);
		ret = 0;
	}
	mutex_unlock(&mutex);
	return ret;
}


/**
 * set_current_from_pcreds - Set credentials from a policy
 * @flags: The modifiers
 * @pcreds: The credentials to apply
 *
 * Aside from unexpected error conditions, this operation should
 * always succeed. However, this can only *DECREASE* the privileges of
 * the current process, unless it has the required capabilities:
 *
 * - if the policy defines UID change, it is only used if process has
 *   CAP_SETUID, otherwise "NOBODY_UID" is used.
 *
 * - if the policy defines GID change, it is only used if process has
 *   CAP_SETGID, otherwise "NOBODY_GID" is used.
 *
 * - the supplementary groups is intersection of current groups and
 *   policy defined groups, unless task has CAP_SETGID
 *
 * - capabilities is intersection of the current capabilities and
 *   policy capabilities, unless task has CAP_SETPCAP.
 */
static int set_current_from_pcreds(int flags, const struct policy_creds *pcreds)
{
	int ret;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	ret = apply_pcreds(flags, pcreds, new, authorized_p());
	if (ret < 0)
		goto out;
	if (ret & APPLY_PCREDS_SETGID)
		new->gid = new->egid;
	if (ret & APPLY_PCREDS_SETUID) {
		ret = kernel_setuid(new, new->euid);
		if (ret < 0)
			goto out;
	}
	new->cap_effective = new->cap_permitted;
	commit_creds(new);
	return 0;
out:
	abort_creds(new);
	return ret;
}

/**
 * drop_credentials: removes credentials for the current task.
 *
 * clears capabilities and resource tokens, but
 * uid/gid and supplementary group list remains unchanged
 */
static int drop_credentials(void)
{
	int ret;
	struct group_info *groups;
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	cap_clear(new->cap_permitted);
	new->cap_effective = new->cap_permitted;
	groups = purge_tokens(new->group_info, 0);
	ret = set_groups(new, groups);
	put_group_info(groups);
	if (ret < 0)
		goto out;
	commit_creds(new);
	return 0;
out:
	abort_creds(new);
	return ret;
}

/**
 * credp_kconfine: Confine credentials for the current task.
 * @path: The path that selects the policy (not opened)
 *
 * Locate a policy that has been bound to the specified name and
 * activate it for the current process.
 *
 * Unless the task has specific capabilities, this can only decrease
 * the credentials.
 */
long credp_kconfine(const char *path)
{
	struct policy *policy;
	long ret;

	mutex_lock(&mutex);
	policy = find_policy_bypath(path);
	if (policy)
		ret = set_current_from_pcreds(CREDP_TYPE_SET, &policy->pcreds);
	else
		ret = drop_credentials();
	mutex_unlock(&mutex);
	return ret;
}

/**
 * credp_kconfine2: Confine credentials for the current task.
 * @path: The path that selects the policy (not opened)
 * @flags: Allow variations for operation
 * @list: The credentials buffer
 * @list_length: The length of the buffer
 *
 * Locate a policy that has been bound to the specified name and
 * activate it for the current process.
 *
 * If policy cannot be found based on path, use flags and credential
 * list as fallback mechanism.
 *
 * Unless the task has specific capabilities, this can only decrease
 * the credentials.
 */
long credp_kconfine2(const char *path, int flags,
		     const __u32 __user *list, size_t list_length)
{
	struct policy *policy;
	long ret;

	mutex_lock(&mutex);
	policy = find_policy_bypath(path);

	if (policy)
		ret = set_current_from_pcreds(flags, &policy->pcreds);
	else {
		struct policy_creds pcreds;

		ret = pcreds_from_user(&pcreds, list, list_length);

		if (ret == 0)
			ret = set_current_from_pcreds(flags, &pcreds);

		/* If any of previous calls failed, drop all extra
		 * privileges. */
		if (ret)
			drop_credentials();

		policy_creds_clear(&pcreds);
	}

	mutex_unlock(&mutex);
	return ret;
}

/**
 * credp_kset: Set credentials of the current process.
 * @flags: Allow variations for operation
 * @list: The credentials buffer
 * @list_length: The length of the buffer
 *
 * Return 0 on success, and negative error otherwise.
 */
long credp_kset(int flags, const __u32 __user *list, size_t list_length)
{
	int ret = -EINVAL;
	struct policy_creds pcreds;

	ret =  pcreds_from_user(&pcreds, list, list_length);
	if (!ret)
		ret = set_current_from_pcreds(flags, &pcreds);
	policy_creds_clear(&pcreds);
	return ret;
}

/**
 * group_in_p - Test the presence of group id in group_info
 * @g: The group to test
 * @groups: The groups info
 *
 * This function does not assume that groups in group_info are sorted.
 * (brute force search).
 *
 * Return 1 if present, and 0 if not
 */
static int group_in_p(gid_t g, const struct group_info *groups)
{
	int i;

	if (!groups)
		return 0;
	for (i = 0; i < groups->ngroups; ++i)
		if (g == GROUP_AT(groups, i))
			return 1;
	return 0;
}

static void audit_message(int type, int id, long srcid)
{
	INFO("%s: credential %d::%d not present in source SRC::%ld\n",
	     current->comm, type, id, srcid);
}

/**
 * audit_group: Test a group against current task and source groups
 * @g: The group to test
 * @groups: The groups allowed by the source
 * @srcid: The source identifier
 * @cred: The credentials to check
 *
 * If a group 'g' is in current task context, it must be also in
 * the allowed groups of the source.
 *
 * Return 1, if access denied (log message generated), and 0
 * otherwise.
 */
static int audit_group_p(gid_t g, const struct group_info *groups, long srcid,
	const struct cred *cred)
{
	/* Check if group g is in credentials */
	if (cred->gid == g || cred->egid == g || cred->sgid == g ||
	    cred->fsgid == g || groups_search(cred->group_info, g)) {
		/*
		 * The group g is included in credentials, now need to
		 * check whether it is included in the allowed groups
		 * by the source.
		 */
		if (group_in_p(g, groups))
			return 0;
		/*
		 * The group is in credentials, but not allowed for
		 * the source
		 */
		audit_message(CREDS_GRP, g, srcid);
		return 1;
	}
	/* The g is not included in credentials */
	return 0;
}

/**
 * credp_check: Verify that task does not have too many credentials
 * @srcid: The source identifier
 * @cred: The credentials to check against (if NULL, uses current_cred())
 *
 * In case of conflicts, each of them is logged.
 *
 * Return 0, if check passes, and -EACCESS otherwise.
 */
int credp_check(long srcid, const struct cred *cred)
{
	int ret = 0;
	struct policy *byid;
	const struct policy_creds *pcreds = &policy_creds_init;
	unsigned int i;

	mutex_lock(&mutex);
	if (!enforce_origin_checking)
		goto out;

	if (!cred) {
		/*
		 * When in execve, only check origin when cred
		 * from linux_binprm has been supplied, and pass
		 * check otherwise.
		 */
		if (current->in_execve)
			goto out;
		cred = current_cred();
	}

	byid = find_policy_byid(srcid, &i);
	if (byid)
		pcreds = &byid->pcreds;
	/* All capabilities in current context must be allowed by the
	 * source policy. */
	CAP_FOR_EACH_U32(i) {
		const u32 check = enforce_origin_checking->caps.cap[i];
		const u32 mask = ~pcreds->caps.cap[i] & check;
		const unsigned long denied =
			(mask & (cred->cap_effective.cap[i] |
				 cred->cap_permitted.cap[i]));
		unsigned j;
		for_each_bit(j, &denied, 32) {
			/* Current task has more than allowed capabilies.
			 * Generate audit_message from each conflict */
			audit_message(CREDS_CAP, i*32 + j, srcid);
			ret = -EACCES;
		}
	}
	if (!enforce_origin_checking->groups)
		goto out;
	for (i = 0; i < enforce_origin_checking->groups->ngroups; ++i)
		if (audit_group_p(GROUP_AT(enforce_origin_checking->groups, i),
				  pcreds->groups, srcid, cred))
			ret = -EACCES;
out:
	mutex_unlock(&mutex);
	return ret;
}

/**
 * credp_ioctl - Transfer operation from user space to kernel
 */
static long credp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct credp_ioc_arg par;
	long retval = 0;
	char *path = NULL;

	if (copy_from_user(&par, (const void __user *)arg, sizeof(par)))
		return -EFAULT;

	if (cmd != SIOCCREDP_SET) {
		if (par.path.length > CREDP_PATH_MAX)
			return -EFAULT;
		path = kmalloc(par.path.length + 1, GFP_KERNEL);
		if (!path)
			return -ENOMEM;
		if (copy_from_user(path, par.path.name, par.path.length)) {
			retval = -EFAULT;
			goto out;
		}
		path[par.path.length] = 0;
	}
	switch (cmd) {
	case SIOCCREDP_LOAD:
		retval = credp_kload(par.flags, path, par.list.items,
				     par.list.length);
		break;
	case SIOCCREDP_UNLOAD:
		retval = credp_kunload(path);
		break;
	case SIOCCREDP_CONFINE:
		retval = credp_kconfine(path);
		break;
	case SIOCCREDP_CONFINE2:
		retval = credp_kconfine2(path, par.flags,
					 par.list.items, par.list.length);
		break;
	case SIOCCREDP_SET:
		retval = credp_kset(par.flags, par.list.items, par.list.length);
		break;
	default:
		retval = -EINVAL;
		break;
	}
out:
	kfree(path);
	return retval;
}

static int credp_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &credp_seq_ops);
}

static const struct file_operations credp_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = credp_ioctl,
	.open		= credp_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release
};

static struct dentry *credp_fs;
static struct dentry *credp_file;

static void cleanup_hash(struct list_head *hash, size_t num)
{
	int i;
	for (i = 0; i < num; ++i) {
		struct policy *p, *n;
		list_for_each_entry_safe(p, n, &hash[i], list) {
			list_del(&p->list);
			policy_creds_clear(&p->pcreds);
			kfree(p);
		}
	}
}

static void __init cleanup(void)
{
	if (credp_file && !IS_ERR(credp_file))
		securityfs_remove(credp_file);
	if (credp_fs && !IS_ERR(credp_fs))
		securityfs_remove(credp_fs);
	if (empty_groups)
		put_group_info(empty_groups);
	cleanup_hash(name_hash, NAME_HASH);
	cleanup_hash(byid_hash, BYID_HASH);
}

static int __init credp_init(void)
{
	int res = -ENOMEM;
	int i;

	for (i = 0; i < BYID_HASH; ++i)
		INIT_LIST_HEAD(&byid_hash[i]);
	for (i = 0; i < NAME_HASH; ++i)
		INIT_LIST_HEAD(&name_hash[i]);
	empty_groups = groups_alloc(0);
	if (!empty_groups)
		goto out;
	credp_fs = securityfs_create_dir(CREDP_SECURITY_DIR, NULL);
	if (IS_ERR(credp_fs)) {
		ERR("Failed creating '" CREDP_SECURITY_DIR "'\n");
		res = PTR_ERR(credp_fs);
		goto out;
		}
	credp_file = securityfs_create_file(CREDP_SECURITY_FILE,
					    S_IFREG | 0644, credp_fs,
					    NULL, &credp_fops);
	if (IS_ERR(credp_file)) {
		ERR("Failed creating '"
		    CREDP_SECURITY_DIR "/" CREDP_SECURITY_FILE "'\n");
		res = PTR_ERR(credp_file);
		goto out;
		}

	INFO("active\n");
	return 0;
out:
	cleanup();
	return res;
}

module_init(credp_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markku Savela");
MODULE_DESCRIPTION("Credentials assigner module for execve");


