/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2003 Ericsson, Inc
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
 * Authors: Axelle Apvrille, 2003
 *          David Gordon, 2003
 *          Makan Pourzandi, 2003
 *          Chris Wright, 2004
 *          Markku Kylänpää, 2008-2010
 */

/*
 * Verification functions.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/elf.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <crypto/hash.h>
#include "validator.h"
#include "verify.h"

/* Default buffer size before trying smaller ones. */
#define VERIFY_BUFFER_SIZE (8 * PAGE_SIZE)

/* Allocate and free functions for each kind of security blob. */
static DECLARE_MUTEX(digsig_sem);

/* Asynchronous SHA-1 calculation */
static struct crypto_ahash *ahash_tfm;

/**
 * struct ahash_result - calculation complete result structure
 * @completion: Completion structure
 * @err:        Error code
 *
 * When asynchronous SHA1 calculation request is sent there is also
 * callback function, complete variable and result structure to return
 * status. This structure contains completion variable and status code
 * to be used to check status of asynchronous calculation request.
 */
struct ahash_result {
	struct completion completion;
	int err;
};

/**
 * check_tfm() - Allocate ahash tfm structure
 *
 * Structure is allocated only once
 *
 * Return 0 if allocation was succesful and negative value for an error.
 */
static int check_tfm(void)
{
	int retval = 0;

	if (ahash_tfm)
		return 0;

	down(&digsig_sem);
	if (ahash_tfm == NULL) {
		ahash_tfm = crypto_alloc_ahash("sha1", 0, 0);
		if (IS_ERR(ahash_tfm)) {
			pr_err("Aegis: ahash_tfm allocation failed\n");
			retval = PTR_ERR(ahash_tfm);
			ahash_tfm = NULL;
		}
	}
	up(&digsig_sem);
	return retval;
}

/**
 * ahash_complete() - Calback function for asynch SHA1 calls
 * @req: Calculation request
 * @err: Error code
 *
 * This function is used as function parameter in async SHA1 calls.
 */
static void ahash_complete(struct crypto_async_request *req, int err)
{
	struct ahash_result *res = req->data;

	if (err == -EINPROGRESS)
		return;
	res->err = err;
	complete(&res->completion);
}

/**
 * ahash_wait() - Wait for completion of asynch SHA1 request
 * @ret: Return value from asynchronous request
 * @res: Calculation result
 *
 * This function is used as function parameter in async SHA1 calls.
 */
static int ahash_wait(int ret, struct ahash_result *res)
{
	switch (ret) {
	case 0:
		break;
	case -EBUSY:
	case -EINPROGRESS:
		wait_for_completion(&(res->completion));
		ret = res->err;
		if (!res->err) {
			INIT_COMPLETION(res->completion);
			break;
		}
	default:
		pr_err("Aegis: SHA1 hash calculation failed (%d %d)\n", ret,
			res->err);
		break;
	}
	return ret;
}

/* last page - first page + 1 */
#define PAGECOUNT(buf, buflen) \
    ((((unsigned long)(buf + buflen - 1) & PAGE_MASK) >> PAGE_SHIFT) - \
    (((unsigned long) buf               & PAGE_MASK) >> PAGE_SHIFT) + 1)

/* offset of buf in it's first page */
#define PAGEOFFSET(buf) ((unsigned long)buf & ~PAGE_MASK)

/**
 * vmalloc_to_sg() - Make scatterlist from vmallocated buffer
 * @virt: vmallocated buffer
 * @len:  buffer length
 *
 * Asynchronous SHA1 calculation is using scatterlist data. This
 * function can be used to create scatterlist from vmallocated
 * data buffer.
 *
 * Return pointer to scatterlist or NULL.
 */
static struct scatterlist *vmalloc_to_sg(const void *virt, unsigned long len)
{
	int nr_pages = PAGECOUNT(virt, len);
	struct scatterlist *sglist;
	struct page *pg;
	int i;
	int pglen;
	int pgoff;

	sglist = vmalloc(nr_pages * sizeof(*sglist));
	if (!sglist)
		return NULL;
	memset(sglist, 0, nr_pages * sizeof(*sglist));
	sg_init_table(sglist, nr_pages);
	for (i = 0; i < nr_pages; i++, virt += pglen, len -= pglen) {
		pg = vmalloc_to_page(virt);
		if (!pg)
			goto err;
		pgoff = PAGEOFFSET(virt);
		pglen = min((PAGE_SIZE - pgoff), len);
		sg_set_page(&sglist[i], pg, pglen, pgoff);
	}
	return sglist;
err:
	vfree(sglist);
	return NULL;
}

/**
 * ahash_init() - Initialize SHA-1 hash calculation.
 * @req: SHA-1 context
 *
 * Initialize asynchronous SHA1 calculation.
 *
 * Return 0 for successful allocation,
 */
static int ahash_init(struct ahash_request *req)
{
	int ret;

	ret = crypto_ahash_init(req);
	return ahash_wait(ret, req->base.data);
}

/**
 * validator_alloc_pages() - Allocated contiguous pages.
 * @max_size:       Maximum amount of memory to allocate.
 * @min_size:       Minimum amount of memory to allocate.
 * @allocated_size: Returned size of actual allocation.
 * @last_warn:      Should the min_size allocation warn or not.
 *
 * Tries to do opportunistic allocation for memory first trying to allocate
 * max_size amount of memory and then splitting that until min_size limit is
 * reached. Allocation is tried without generating allocation warnings unless
 * last_warn is set. Last_warn set affects only last allocation of min_size.
 * If max_size is smaller that min_size, min_size amount of memory is
 * allocated.
 *
 * Return pointer to allocated memory, or NULL on failure.
 */
static void *validator_alloc_pages(size_t max_size, size_t min_size,
	size_t *allocated_size, int last_warn)
{
	size_t curr_size = max_size;
	gfp_t gfp_mask;
	void *ptr;

	while (curr_size > min_size) {
		ptr = alloc_pages_exact(curr_size, __GFP_NOWARN | __GFP_WAIT |
					__GFP_NORETRY);
		if (ptr) {
			*allocated_size = curr_size;
			return ptr;
		}
		curr_size >>= 1;
	}

	gfp_mask = GFP_KERNEL;

	if (!last_warn)
		gfp_mask |= __GFP_NOWARN;

	ptr = alloc_pages_exact(min_size, gfp_mask);
	if (ptr) {
		*allocated_size = min_size;
		return ptr;
	}

	*allocated_size = 0;
	return NULL;
}

/**
 * validator_free_pages() - Free pages allocated by validator_alloc_pages().
 * @ptr:  Pointer to allocated pages.
 * @size: Size of allocated buffer.
 */
static void validator_free_pages(void *ptr, size_t size)
{
	if (!ptr)
		return;

	free_pages_exact(ptr, size);
}

/**
 * validator_verify_refhash() - Verify a file using reference hash value
 * @file:    File to be verified
 * @refhash: Reference SHA1 hash value
 *
 * Calculate SHA1 hash of the content of the file and compare to the given
 * value.
 *
 * Return 0 if verification was successful and negative value for errors.
 */
int validator_verify_refhash(struct file *file, char *refhash)
{
	char *digest;
	loff_t i;
	loff_t i_size;
	struct ahash_request *req;
	struct scatterlist sg[1];
	char *buffer[2];
	size_t buffer_size[2];
	size_t tmpsize;
	int buflen, active = 0;
	int retval, ret = -EFAULT, try = 2;
	struct ahash_result res;

	retval = check_tfm();
	if (retval)
		return retval;

try_again:

	retval = -ENOMEM;

	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		pr_err("Aegis: ahash descriptor allocation failed\n");
		goto out1;
	}

	init_completion(&res.completion);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
					CRYPTO_TFM_REQ_USE_FINUP,
				   ahash_complete, &res);

	i_size = i_size_read(file->f_dentry->d_inode);

	/*
	 * Try to allocate maximum size of memory, fail if not even single
	 * page cannot be allocated.
	 */
	tmpsize = min_t(size_t, i_size, VERIFY_BUFFER_SIZE);
	buffer[0] = validator_alloc_pages(tmpsize, PAGE_SIZE, &buffer_size[0],
					  1);
	if (!buffer[0]) {
		pr_err("Aegis: cannot get free page\n");
		goto out2;
	}

	/* Only allocate one buffer if that is enough. */
	if (i_size <= buffer_size[0]) {
		buffer_size[1] = 0;
		buffer[1] = NULL;
	} else {
		/*
		 * Try to allocate secondary buffer if that fails fallback to
		 * using single buffering. Use previous memory allocation size
		 * as baseline for possible allocation size.
		 */
		tmpsize = min_t(size_t, i_size - buffer_size[0],
				buffer_size[0]);
		buffer[1] = validator_alloc_pages(tmpsize, PAGE_SIZE,
						  &buffer_size[1], 0);
	}
	digest = kmalloc(SHA1_HASH_LENGTH, GFP_KERNEL);
	if (!digest) {
		pr_err("Aegis: digest allocation failed\n");
		goto out3;
	}

	retval = ahash_init(req);
	if (retval)
		goto out4;

	buflen = min_t(loff_t, i_size, buffer_size[active]);
	retval = kernel_read(file, 0, buffer[active], buflen);
	if (retval != buflen) {
		pr_err("Aegis: read error during measurement (%d %s)\n",
			retval, current->comm);
		goto out5;
	}

	for (i = 0; i < i_size; ) {
		i += buflen;
		sg_init_one(&sg[0], buffer[active], buflen);
		ahash_request_set_crypt(req, sg, digest, buflen);
		if (i == i_size)
			ret = crypto_ahash_finup(req); /* last block */
		else
			ret = crypto_ahash_update(req);

		/*
		 * If there are two buffers allocated, load more data in
		 * background.
		 */
		if (i < i_size && buffer[1]) {
			buflen = min_t(loff_t, i_size - i,
				       buffer_size[!active]);
			retval = kernel_read(file, i, buffer[!active], buflen);
			if (retval != buflen) {
				pr_err("Aegis: read error during measurement " \
					"(%d %s)\n", retval, current->comm);
				break;
			}
		}

		retval = ahash_wait(ret, req->base.data);
		if (retval)
			break;

		/*
		 * If only one buffer is in use, load more data after hash
		 * calculation when single buffer can be modified.
		 */
		if (!buffer[1]) {
			buflen = min_t(loff_t, i_size - i, buffer_size[0]);
			retval = kernel_read(file, i, buffer[0], buflen);
			if (retval != buflen) {
				pr_err("Aegis: read error during measurement "
					"(%d %s)\n", retval, current->comm);
				retval = -EFAULT;
				break;
			}
		} else
			/* Double buffering in use, swap buffers. */
			active = !active;

	}
out5:
	if (!retval)
		ret = memcmp(digest, refhash, SHA1_HASH_LENGTH);
out4:
	kfree(digest);
out3:
	validator_free_pages(buffer[0], buffer_size[0]);
	validator_free_pages(buffer[1], buffer_size[1]);
out2:
	ahash_request_free(req);
out1:
	if (retval && --try)
		goto try_again;

	return retval ? retval : (ret ? -EFAULT : 0);
}

/**
 * validator_sha1() - calculate SHA1 hash for vmallocated buffer
 * @vbuf:   Buffer
 * @len:    Buffer length
 * @digest: SHA1 result (20 bytes)
 *
 * Use crypto_ahash to calculate SHA1 hash for the buffer.
 *
 * Return 0 for success and negative value for an errot
 */
int validator_sha1(const void *vbuf, unsigned long len, unsigned char *digest)
{
	struct ahash_request *req;
	struct scatterlist *sg;
	int retval;
	int ret = -EFAULT;
	int try = 2;
	struct ahash_result res;

	sg = vmalloc_to_sg(vbuf, len);
	if (!sg)
		return -ENOMEM;
	retval = check_tfm();
	if (retval)
		goto out0;
try_again:
	retval = -ENOMEM;
	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		pr_err("Aegis: ahash descriptor allocation failed\n");
		goto out1;
	}
	init_completion(&res.completion);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				   CRYPTO_TFM_REQ_USE_FINUP,
				   ahash_complete, &res);
	retval = ahash_init(req);
	if (retval)
		goto out2;
	ahash_request_set_crypt(req, sg, digest, len);
	ret = crypto_ahash_digest(req);
	retval = ahash_wait(ret, req->base.data);
	if (!retval)
		ret = 0;
out2:
	ahash_request_free(req);
out1:
	if (retval && --try)
		goto try_again;
out0:
	vfree(sg);
	return retval ? retval : (ret ? -EFAULT : 0);
}
