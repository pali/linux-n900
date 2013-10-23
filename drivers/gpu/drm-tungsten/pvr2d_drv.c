/*
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 */


#include "drmP.h"
#include "drm_pciids.h"

#include "pvr2d_drm.h"
#include "pvr2d_drv.h"

#define PVR2D_SHMEM_HASH_ORDER 12

struct pvr2d_dev {
	rwlock_t hash_lock;
	struct drm_open_hash shmem_hash;
};

struct pvr2d_buf {
	struct pvr2d_dev *dev_priv;
	struct drm_hash_item hash;
	struct page **pages;
	struct kref kref;
	uint32_t num_pages;
};

/*
 * This pvr2d_ref object is needed strictly because
 * idr_for_each doesn't exist in 2.6.22. With kernels
 * supporting this function, we can use it to traverse
 * the file list of buffers at file release.
 */

struct pvr2d_ref{
	struct list_head head;
	struct pvr2d_buf *buf;
};

struct pvr2d_file {
	spinlock_t lock;
	struct list_head ref_list;
	struct idr buf_idr;
};

static inline struct pvr2d_dev *pvr2d_dp(struct drm_device *dev)
{
	return (struct pvr2d_dev *) dev->dev_private;
}

static inline struct pvr2d_file *pvr2d_fp(struct drm_file *file_priv)
{
	return (struct pvr2d_file *) file_priv->driver_priv;
}


static void
pvr2d_free_buf(struct pvr2d_buf *buf)
{
	uint32_t i;

	for (i=0; i<buf->num_pages; ++i) {
		struct page *page = buf->pages[i];

		if (!PageReserved(page))
			set_page_dirty_lock(page);

		put_page(page);
	}

	kfree(buf->pages);
	kfree(buf);
}

static void
pvr2d_release_buf(struct kref *kref)
{
	struct pvr2d_buf *buf =
		container_of(kref, struct pvr2d_buf, kref);

	struct pvr2d_dev *dev_priv = buf->dev_priv;

	drm_ht_remove_item(&dev_priv->shmem_hash, &buf->hash);
	write_unlock(&dev_priv->hash_lock);
	pvr2d_free_buf(buf);
	write_lock(&dev_priv->hash_lock);
}

static struct pvr2d_buf *
pvr2d_alloc_buf(struct pvr2d_dev *dev_priv, uint32_t num_pages)
{
	struct pvr2d_buf *buf = kmalloc(sizeof(*buf), GFP_KERNEL);

	if (unlikely(!buf))
		return NULL;

	buf->pages = kmalloc(num_pages * sizeof(*buf->pages), GFP_KERNEL);
	if (unlikely(!buf->pages))
		goto out_err0;

	buf->dev_priv = dev_priv;
	buf->num_pages = num_pages;


	DRM_DEBUG("pvr2d_alloc_buf successfully completed.\n");
	return buf;

out_err0:
	kfree(buf);

	return NULL;
}


static struct pvr2d_buf*
pvr2d_lookup_buf(struct pvr2d_dev *dev_priv, struct page *first_phys)
{
	struct drm_hash_item *hash;
	struct pvr2d_buf *buf = NULL;
	int ret;

	read_lock(&dev_priv->hash_lock);
	ret = drm_ht_find_item(&dev_priv->shmem_hash,
			       (unsigned long)first_phys,
			       &hash);

	if (likely(ret == 0)) {
		buf = drm_hash_entry(hash, struct pvr2d_buf, hash);
		kref_get(&buf->kref);
	}
	read_unlock(&dev_priv->hash_lock);

	if (buf != NULL) {
		DRM_DEBUG("pvr2d_lookup_buf found already used buffer.\n");
	}

	return buf;
}


static int
pvr2d_buf_lock(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_pvr2d_buf_lock *bl = data;
	uint32_t i;
	unsigned nr_pages = ((bl->virt & ~PAGE_MASK) + bl->length + PAGE_SIZE -
			     1) / PAGE_SIZE;
	struct page *first_page;
	struct pvr2d_buf *buf = NULL;
	struct pvr2d_dev *dev_priv = pvr2d_dp(dev);
	struct pvr2d_file *pvr2d_fpriv = pvr2d_fp(file_priv);
	struct pvr2d_ref *ref;
	int ret;


	/*
	 * Obtain a global hash key for the pvr2d buffer structure.
	 * We use the address of the struct page of the first
	 * page.
	 */

	down_read(&current->mm->mmap_sem);
        ret = get_user_pages(current, current->mm, bl->virt & PAGE_MASK,
                             1, WRITE, 0, &first_page, NULL);
        up_read(&current->mm->mmap_sem);

	if (unlikely(ret < 1)) {
		DRM_ERROR("Failed getting first page: %d\n", ret);
		return -ENOMEM;
	}

	/*
	 * Look up buffer already in the hash table, or create
	 * and insert a new one.
	 */

	while(buf == NULL) {
		buf = pvr2d_lookup_buf(dev_priv, first_page);

		if (likely(buf != NULL))
			break;

		/*
		if (!capable(CAP_SYS_ADMIN)) {
			ret = -EPERM;
			goto out_put;
		}
		*/

		buf = pvr2d_alloc_buf(dev_priv, nr_pages);
		if (unlikely(buf == NULL)) {
			DRM_ERROR("Failed allocating pvr2d buffer.\n");
			ret = -ENOMEM;
			goto out_put;
		}

		down_read(&current->mm->mmap_sem);
		ret = get_user_pages(current, current->mm, bl->virt & PAGE_MASK,
				     nr_pages, WRITE, 0, buf->pages, NULL);
		up_read(&current->mm->mmap_sem);

		if (unlikely(ret < nr_pages)) {
			DRM_ERROR("Failed getting user pages.\n");
			buf->num_pages = ret;
			ret = -ENOMEM;
			pvr2d_free_buf(buf);
			goto out_put;
		}

		kref_init(&buf->kref);
		buf->hash.key = (unsigned long) first_page;

		write_lock(&dev_priv->hash_lock);
		ret = drm_ht_insert_item(&dev_priv->shmem_hash, &buf->hash);
		write_unlock(&dev_priv->hash_lock);

		if (unlikely(ret == -EINVAL)) {

			/*
			 * Somebody raced us and already
			 * inserted this buffer.
			 * Very unlikely, but retry anyway.
			 */

			pvr2d_free_buf(buf);
			buf = NULL;
		}
	}

	/*
	 * Create a reference object that is used for unreferencing
	 * either by user action or when the drm file is closed.
	 */

	ref = kmalloc(sizeof(*ref), GFP_KERNEL);
	if (unlikely(ref == NULL))
		goto out_err0;

	ref->buf = buf;
	do {
		if (idr_pre_get(&pvr2d_fpriv->buf_idr, GFP_KERNEL) == 0) {
			ret = -ENOMEM;
			DRM_ERROR("Failed idr_pre_get\n");
			goto out_err1;
		}

		spin_lock( &pvr2d_fpriv->lock );
		ret = idr_get_new( &pvr2d_fpriv->buf_idr, ref, &bl->handle);

		if (likely(ret == 0))
			list_add_tail(&ref->head, &pvr2d_fpriv->ref_list);

		spin_unlock( &pvr2d_fpriv->lock );

	} while (unlikely(ret == -EAGAIN));

	if (unlikely(ret != 0))
		goto out_err1;


	/*
	 * Copy info to user-space.
	 */

	DRM_DEBUG("Locking range of %u bytes at virtual 0x%08x, physical array at 0x%08x\n",
		 bl->length, bl->virt, bl->phys_array);

	for (i = 0; i < nr_pages; i++) {
	        uint32_t physical = (uint32_t)page_to_pfn(buf->pages[i]) << PAGE_SHIFT;
		DRM_DEBUG("Virtual 0x%08lx => Physical 0x%08x\n",
			 bl->virt + i * PAGE_SIZE, physical);

		if (DRM_COPY_TO_USER((void*)(bl->phys_array +
					     i * sizeof(uint32_t)),
				     &physical, sizeof(uint32_t))) {
			ret = -EFAULT;
			goto out_err2;
		}

	}

#ifdef CONFIG_X86
	/* XXX: Quick'n'dirty hack to avoid corruption on Poulsbo, remove when
	 * there's a better solution
	 */
	wbinvd();
#endif

	DRM_DEBUG("pvr2d_buf_lock returning handle 0x%08x\n",
		 bl->handle);

out_put:
	put_page(first_page);
	return ret;

out_err2:
	spin_lock( &pvr2d_fpriv->lock );
	list_del(&ref->head);
	idr_remove( &pvr2d_fpriv->buf_idr, bl->handle);
	spin_unlock( &pvr2d_fpriv->lock );
out_err1:
	kfree(ref);
out_err0:
	write_lock(&dev_priv->hash_lock);
	kref_put(&buf->kref, &pvr2d_release_buf);
	write_unlock(&dev_priv->hash_lock);
	put_page(first_page);
	return ret;
}


static int
pvr2d_buf_release(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct pvr2d_dev *dev_priv = pvr2d_dp(dev);
	struct drm_pvr2d_buf_release *br = data;
	struct pvr2d_file *pvr2d_fpriv = pvr2d_fp(file_priv);
	struct pvr2d_buf *buf;
	struct pvr2d_ref *ref;

	DRM_DEBUG("pvr2d_buf_release releasing 0x%08x\n",
		  br->handle);

	spin_lock( &pvr2d_fpriv->lock );
	ref = idr_find( &pvr2d_fpriv->buf_idr, br->handle);

	if (unlikely(ref == NULL)) {
		spin_unlock( &pvr2d_fpriv->lock );
		DRM_ERROR("Could not find pvr2d buf to unref.\n");
		return -EINVAL;
	}
	(void) idr_remove( &pvr2d_fpriv->buf_idr, br->handle);
	list_del(&ref->head);
	spin_unlock( &pvr2d_fpriv->lock );

	buf = ref->buf;
	kfree(ref);

	write_lock(&dev_priv->hash_lock);
	kref_put(&buf->kref, &pvr2d_release_buf);
	write_unlock(&dev_priv->hash_lock);

	return 0;
}

static int
pvr2d_cflush(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_pvr2d_cflush *cf = data;

	switch (cf->type) {
	case DRM_PVR2D_CFLUSH_FROM_GPU:
		DRM_DEBUG("DRM_PVR2D_CFLUSH_FROM_GPU 0x%08x, length 0x%08x\n",
			  cf->virt, cf->length);
#ifdef CONFIG_ARM
		dmac_inv_range((const void*)cf->virt,
			       (const void*)(cf->virt + cf->length));
#endif
		return 0;
	case DRM_PVR2D_CFLUSH_TO_GPU:
		DRM_DEBUG("DRM_PVR2D_CFLUSH_TO_GPU 0x%08x, length 0x%08x\n",
			  cf->virt, cf->length);
#ifdef CONFIG_ARM
		dmac_clean_range((const void*)cf->virt,
				 (const void*)(cf->virt + cf->length));
#endif
		return 0;
	default:
		DRM_ERROR("Invalid cflush type 0x%x\n", cf->type);
		return -EINVAL;
	}
}

static int
pvr2d_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct pvr2d_file *pvr2d_fpriv;
	struct drm_file *file_priv;

	pvr2d_fpriv = kmalloc(sizeof(*pvr2d_fpriv), GFP_KERNEL);
	if (unlikely(pvr2d_fpriv == NULL))
		return -ENOMEM;

	spin_lock_init(&pvr2d_fpriv->lock);
	INIT_LIST_HEAD(&pvr2d_fpriv->ref_list);
	idr_init(&pvr2d_fpriv->buf_idr);

	ret = drm_open(inode, filp);

	if (unlikely(ret != 0)) {
		idr_destroy(&pvr2d_fpriv->buf_idr);
		kfree(pvr2d_fpriv);
		return ret;
	}

	file_priv = filp->private_data;
	file_priv->driver_priv = pvr2d_fpriv;

	DRM_DEBUG("pvr2d_open completed successfully.\n");
	return 0;
};


static int
pvr2d_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct pvr2d_dev *dev_priv = pvr2d_dp(dev);
	struct pvr2d_file *pvr2d_fpriv = pvr2d_fp(file_priv);
	struct pvr2d_buf *buf;
	struct pvr2d_ref *ref, *next;

	/*
	 * At this point we're the only user of the list, so
	 * it should be safe to release the file lock whenever we want to.
	 */

	spin_lock(&pvr2d_fpriv->lock);

	list_for_each_entry_safe(ref, next, &pvr2d_fpriv->ref_list,
				 head) {
		list_del(&ref->head);
		buf = ref->buf;
		kfree(ref);
		spin_unlock(&pvr2d_fpriv->lock);
		write_lock(&dev_priv->hash_lock);
		kref_put(&buf->kref, &pvr2d_release_buf);
		write_unlock(&dev_priv->hash_lock);
		spin_lock(&pvr2d_fpriv->lock);
	}

	idr_remove_all(&pvr2d_fpriv->buf_idr);
	idr_destroy(&pvr2d_fpriv->buf_idr);
	spin_unlock(&pvr2d_fpriv->lock);

	kfree(pvr2d_fpriv);

	DRM_DEBUG("pvr2d_release calling drm_release.\n");
	return drm_release(inode, filp);
}

static int pvr2d_load(struct drm_device *dev, unsigned long chipset)
{
	struct pvr2d_dev *dev_priv;
	int ret;

	dev_priv = kmalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (unlikely(dev_priv == NULL))
		return -ENOMEM;

	rwlock_init(&dev_priv->hash_lock);
	ret = drm_ht_create(&dev_priv->shmem_hash,
			   PVR2D_SHMEM_HASH_ORDER);

	if (unlikely(ret != 0))
		goto out_err0;

	dev->dev_private = dev_priv;

	DRM_DEBUG("pvr2d_load completed successfully.\n");
	return 0;
out_err0:
	kfree(dev_priv);
	return ret;
}


static int pvr2d_unload(struct drm_device *dev)
{
	struct pvr2d_dev *dev_priv = pvr2d_dp(dev);

	drm_ht_remove(&dev_priv->shmem_hash);
	kfree(dev_priv);
	DRM_DEBUG("pvr2d_unload completed successfully.\n");
	return 0;
}

static struct pci_device_id pciidlist[] = {
	pvr2d_PCI_IDS
};

struct drm_ioctl_desc pvr2d_ioctls[] = {
	DRM_IOCTL_DEF(DRM_PVR2D_BUF_LOCK, pvr2d_buf_lock, 0),
	DRM_IOCTL_DEF(DRM_PVR2D_BUF_RELEASE, pvr2d_buf_release, 0),
	DRM_IOCTL_DEF(DRM_PVR2D_CFLUSH, pvr2d_cflush, 0)
};

static int probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static struct drm_driver driver = {
	.driver_features = DRIVER_USE_MTRR,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = pvr2d_ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(pvr2d_ioctls),
	.load = pvr2d_load,
	.unload = pvr2d_unload,
	.fops = {
		.owner = THIS_MODULE,
		.open = pvr2d_open,
		.release = pvr2d_release,
		.unlocked_ioctl = drm_unlocked_ioctl,
		.mmap = drm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
		},
	.pci_driver = {
		.name = DRIVER_NAME,
		.id_table = pciidlist,
		.probe = probe,
		.remove = __devexit_p(drm_cleanup_pci),
	},

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_dev(pdev, ent, &driver);
}


static int __init pvr2d_init(void)
{
#ifdef CONFIG_PCI
	return drm_init(&driver, pciidlist);
#else
	return drm_get_dev(NULL, NULL, &driver);
#endif
}

static void __exit pvr2d_exit(void)
{
	drm_exit(&driver);
}

module_init(pvr2d_init);
module_exit(pvr2d_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
