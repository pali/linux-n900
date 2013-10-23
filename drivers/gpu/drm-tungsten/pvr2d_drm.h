/* pvr2d_drm.h -- Public header for the PVR2D helper module -*- linux-c -*-
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 */

#ifndef __PVR2D_DRM_H__
#define __PVR2D_DRM_H__


/* This wouldn't work with 64 bit userland */
struct drm_pvr2d_buf_lock {
	uint32_t virt;
	uint32_t length;
	uint32_t phys_array;
	uint32_t handle;
};

struct drm_pvr2d_buf_release {
	uint32_t handle;
};

enum drm_pvr2d_cflush_type {
	DRM_PVR2D_CFLUSH_FROM_GPU = 1,
	DRM_PVR2D_CFLUSH_TO_GPU = 2
};

struct drm_pvr2d_cflush {
	enum drm_pvr2d_cflush_type type;
	uint32_t virt;
	uint32_t length;
};

#define DRM_PVR2D_BUF_LOCK    0x0
#define DRM_PVR2D_BUF_RELEASE 0x1
#define DRM_PVR2D_CFLUSH      0x2

#define DRM_IOCTL_PVR2D_BUF_LOCK DRM_IOWR(DRM_COMMAND_BASE + DRM_PVR2D_BUF_LOCK, \
					  struct drm_pvr2d_buf_lock)
#define DRM_IOCTL_PVR2D_BUF_RELEASE DRM_IOW(DRM_COMMAND_BASE + DRM_PVR2D_BUF_RELEASE, \
					  struct drm_pvr2d_buf_release)
#define DRM_IOCTL_PVR2D_CFLUSH DRM_IOW(DRM_COMMAND_BASE + DRM_PVR2D_CFLUSH, \
				       struct drm_pvr2d_cflush)


#endif /* __PVR2D_DRM_H__ */
