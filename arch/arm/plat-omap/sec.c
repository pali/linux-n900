/*
 * linux/arch/arm/plat-omap/sec.c
 *
 * Copyright (C) 2007 - 2010 Nokia Corporation
 * Author: Sami Tolvanen
 *	   Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP HS secure mode driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/stat.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <plat/sec.h>
#include <plat/pa.h>

#define SEC_NAME			"omap_sec"

#define SEC_MAX_DATA_SIZE		(PAGE_SIZE * 5)
#define SEC_MAX_OBUF_SIZE		16384
#define OBUF_SLOTS			16

/* Give 64 KiB breathing room for PA's. */
#define SEC_MIN_PA_TRANSFER_BUF_SIZE	(PAGE_SIZE * 16)

#define SEC_PA_OFFSET			0x00000018
#define SEC_PABIN_DEFAULT		"omap3_pa"
#define SEC_PABIN_PATH			"/lib/firmware/"

#undef SEC_ALLOW_UNSIGNED_PAFMT

struct sec_firmware_data {
	void	*data;
	size_t	size;
	int	vmem;
};

struct sec_firmware {
	char name[SEC_PABIN_NAME_SIZE]; /*more efficient than kmalloc*/
	struct sec_firmware_data	pa_bin;
	struct sec_firmware_data	pafmt_bin;
	struct sec_firmware_data	papub_bin;
	struct pa_format	format;
};

struct sec_status {
	struct mutex lock;	/* Mutex used to protect structure data. */
	u32 cmd;		/* Command identifier */
	struct sec_cmd_header param;
	u8 *ip;			/* Input pointer */
	u8 *data;		/* Parameter data */
	struct sec_obuf *obuf;		/* Output buffer */
	wait_queue_head_t writeq;
	wait_queue_head_t readq;
	char	pabin[SEC_PABIN_NAME_SIZE]; /*more efficient than kmalloc*/
};

struct sec_device {
	struct miscdevice misc_dev;
	int	obuf_cnt;
	struct sec_obuf *bufs[OBUF_SLOTS];
	struct mutex buf_lock; /* Mutex used to protect global buffer data. */
	struct mutex lock; /* Mutex to protect structure data, must be locked
			      before buf_lock if needed. */
	struct sec_operations *ops;
	struct sec_obuf *secs;

	u8 *pa_transfer_buffer;
	size_t pa_transfer_buffer_size;

	int			fwid;
	struct sec_firmware	fw[2]; /* 0: default, 1: alternative */
};

static struct sec_device sdev;

static int minor = MISC_DYNAMIC_MINOR;
module_param(minor, uint, S_IRUGO);
MODULE_PARM_DESC(minor, "Set minor value to use.");

static unsigned int kci;
module_param(kci, uint, S_IRUGO);
MODULE_PARM_DESC(kci, "Set KCI value to use.");

/**
 * free_firmware() - Free firmware data.
 * @fw:		Pointer to firmware data which to release.
 *
 * Releases both vmem and linear physical memory data.
 */
static void free_firmware(struct sec_firmware_data *fw)
{
	if (fw->vmem)
		vfree(fw->data);
	else
		kfree(fw->data);

	fw->data = NULL;
}

/**
 * load_firmware() - loads binary from specific offset
 * @fmt:	Formatting syntax used to form what filename to load.
 * @name:	Name of the binary to load. Given as 1st argument for
 * 		filename formatting.
 * @fw:		Pointer to firmware data where to store loaded firmware.
 * @offset:	Offset in filename from where to start loading.
 * @fw_kci:	KCI for the firmware. When non-zero Given as 2nd argument for
 * 		filename formatting.
 * @vmem:	If non-zero use virtual memory for loading firmware data. If
 * 		zero use linear physical memory for firmware data.
 *
 * Loads firmware data to either virtual memory or to linear physical memory
 * depending on vmem parameter. Firmware filename is being constructed using
 * special formatting argument fmt that is passed to snprintf. Call to snprintf
 * is feed with firmware name and if fw_kci is non-zero then it is also
 * provided to snprintf.
 *
 * Physical linear memory is needed for secure environment communication.
 * Virtual memory can be used to store other data that is only needed inside
 * the kernel.
 *
 * All firmware data is loaded from directory SEC_PABIN_PATH.
 *
 * Return 0 for success and negative value for an error.
 */
static int load_firmware(const char *fmt, const char *name,
			 struct sec_firmware_data *fw, size_t offset,
			 int fw_kci, int vmem)
{
	struct file *f;

	char filename[sizeof(SEC_PABIN_PATH) + SEC_PABIN_PATH_SIZE];

	int len;
	int rv;

	len = strlcpy(filename, SEC_PABIN_PATH, sizeof(filename));

	if (len >= sizeof(filename))
		return -EINVAL;

	if (fw_kci)
		len += snprintf(&filename[len], sizeof(filename) - len, fmt,
				name, fw_kci);
	else
		len += snprintf(&filename[len], sizeof(filename) - len, fmt,
				name);

	if (len >= sizeof(filename))
		return -EINVAL;

	f = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);

	len = f->f_dentry->d_inode->i_size;

	if (len <= offset) {
		rv = -EINVAL;
		goto done;
	}

	fw->size = len - offset;

	fw->vmem = vmem;

	if (vmem)
		fw->data = vmalloc(fw->size);
	else
		fw->data = kmalloc(fw->size, GFP_KERNEL);

	if (!fw->data) {
		rv = -ENOMEM;
		goto done;
	}

	rv = kernel_read(f, offset, (char *)fw->data, fw->size);

	if (rv == fw->size) {
		rv = 0;
		pr_debug("%s loaded with kernel_read() ok\n", filename);
	} else
		pr_err("%s load with kernel_read() failed: %d\n", filename, rv);

done:
	filp_close(f, NULL);

	if (rv) {
		free_firmware(fw);
		printk(KERN_ERR SEC_NAME ": Error loading %s\n", filename);
	}

	return rv;
}

/**
 * firmware_cleanup() - Release all resources allocated for firmware.
 * @fw:		Pointer to firmware which resources to release.
 */
static void firmware_cleanup(struct sec_firmware *fw)
{
	free_firmware(&fw->pa_bin);
	free_firmware(&fw->pafmt_bin);
	free_firmware(&fw->papub_bin);
	kfree(fw->format.cmd);
	memset(fw, 0, sizeof(*fw));
}

static inline int fw_loaded(struct sec_firmware *fw)
{
	return fw->pa_bin.data != NULL;
}

/*
 * firmware_init() - loads OMAP secure environment binaries
 *
 * Loads all necessary binaries to the driver
 * 1. Protected Applications (PA) binaries (e.g. omap3_pa_767.bin)
 * 2. PA public keys (e.g. omap3_papub_767.bin)
 * 3. format parsing binary (omap3_pafmt.bin)
 *
 * This function assumes that mutex sdev.lock has been locked before entry.
 */
static int firmware_init(const char *pabin)
{
	int rv = 0, oldfw = sdev.fwid;
	struct sec_firmware *fw;
	size_t offset = 0;
	size_t buf_size = 0;
	size_t i;

	pr_debug("fwid: %d, KCI=%u\n", sdev.fwid, kci);

	/* first entry is always default binaries */
	if (!pabin || pabin[0] == '\0') {
		sdev.fwid = 0;
		if (sdev.fw[0].name[0] == '\0')
			strcpy(sdev.fw[0].name, SEC_PABIN_DEFAULT);
	} else {
		if (sdev.fwid == 0 &&
				  strcmp(sdev.fw[0].name, pabin) != 0)
			sdev.fwid = 1;
		if (strcmp(sdev.fw[sdev.fwid].name, pabin) != 0) {
			/* clean it up */
			firmware_cleanup(&sdev.fw[sdev.fwid]);
			strlcpy(sdev.fw[sdev.fwid].name, pabin,
				sizeof(sdev.fw[sdev.fwid].name));
		}
	}

	fw = &sdev.fw[sdev.fwid];

	if (fw_loaded(fw)) {
		pr_debug("pabin already loaded\n");
		if (oldfw != sdev.fwid)
			sdev.ops->flags |= SEC_FLAGS_NEW_PAPUB;
		return 0;
	}

	/* need kmalloc for secure side access */
	rv = load_firmware("%sfmt_%d.bin", fw->name, &fw->pafmt_bin, 0, kci, 0);
	if (rv) {
#ifdef SEC_ALLOW_UNSIGNED_PAFMT
		/* try now old pafmt without kci - it will be removed soon */
		rv = load_firmware("%sfmt.bin", fw->name, &fw->pafmt_bin,
				   0, 0, 1);
#endif
		if (rv)
			goto err;
	} else {
		rv = sdev.ops->rom_verify(fw->pafmt_bin.data,
					  fw->pafmt_bin.size, &offset);
		pr_debug("verify: %d, offset: %u\n", rv, offset);
		if (rv) {
			rv = -EACCES;
			goto err;
		}
	}

	rv = pa_format_parse(fw->pafmt_bin.data, fw->pafmt_bin.size,
			       &fw->format, offset);
	if (rv)
		goto err;

	rv = load_firmware("%s_%d.bin", fw->name, &fw->pa_bin, SEC_PA_OFFSET,
			    kci, 1);
	if (rv)
		goto err;

	/* papub is always about 1.5k - use kmalloc */
	rv = load_firmware("%spub_%d.bin", fw->name, &fw->papub_bin, 0, kci, 0);
	if (rv == -ENOENT)
		rv = 0; /* papub may not be provided */

err:
	if (rv) {
		firmware_cleanup(fw);
		sdev.fwid = 0; /* failed: set to default */
		return rv;
	}

	/* Notify secure environment calls to reload new PAPUB's. */
	sdev.ops->flags |= SEC_FLAGS_NEW_PAPUB;

	/*
	 * Calculate maximum size of PA from PA firmware packages to figure
	 * out PA firmware transfer buffer size.
	 */
	for (i = 0; i < ARRAY_SIZE(sdev.fw); i++) {
		struct pa_image_toc_entry *data = sdev.fw[i].pa_bin.data;

		if (!data)
			continue;

		buf_size = max(buf_size, pa_find_max_pa_size(data));
	}

	pr_debug("PA's require %d bytes for PA transfer buffer\n", buf_size);

	/*
	 * Provide some breathing room for PA's just in case later on there is
	 * call to load of secondary PA set which allocation could fail.
	 */
	if (buf_size < SEC_MIN_PA_TRANSFER_BUF_SIZE)
		buf_size = SEC_MIN_PA_TRANSFER_BUF_SIZE;

	/*
	 * Allocate new transfer buffer only if there is need to have more
	 * space.
	 */
	if (buf_size > sdev.pa_transfer_buffer_size) {
		u8 *new_buffer;

		new_buffer = kmalloc(buf_size, GFP_KERNEL);
		if (!new_buffer) {
			firmware_cleanup(fw);
			sdev.fwid = 0;
			return -ENOMEM;
		}

		kfree(sdev.pa_transfer_buffer);

		sdev.pa_transfer_buffer = new_buffer;
		sdev.pa_transfer_buffer_size = buf_size;

		pr_debug("Allocated %d bytes for PA transfer buffer\n",
			 buf_size);
	}

	return rv;
}

static void firmware_cleanup_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sdev.fw); i++)
		firmware_cleanup(&sdev.fw[i]);

	kfree(sdev.pa_transfer_buffer);
	sdev.pa_transfer_buffer = NULL;
	sdev.pa_transfer_buffer_size = 0;
}

/*
 * sec_buf_alloc() - allocate output buffer (obuf)
 *
 * obuf is used as dynamic output buffer to output data for character misc
 * device. Allocated linear physical memory blocks to allow it to be used to
 * store data for secure environment communication. It is used to load and
 * store in example certificates at the runtime.
 *
 * Note: Mutex sdev.buf_lock must be locked before calling this function.
 */
static void *sec_obuf_alloc(size_t size)
{
	struct sec_obuf *obuf;

	obuf = kzalloc(sizeof(*obuf), GFP_KERNEL);
	if (!obuf)
		return NULL;

	obuf->data = kmalloc(size, GFP_KERNEL);
	if (!obuf->data) {
		kfree(obuf);
		return NULL;
	}
	obuf->rp  = obuf->data;
	obuf->wp  = obuf->data;
	obuf->end = obuf->data + size;
	atomic_set(&obuf->refcnt, 1);
	sdev.obuf_cnt++;
	return obuf;
}

/*
 * sec_obuf_put() - release buffer
 *
 * used from pa.c code
 *
 * Note: Mutex sdev.buf_lock must be locked before calling this function.
 */
void sec_obuf_put(struct sec_obuf *obuf)
{
	if (obuf && atomic_dec_and_test(&obuf->refcnt)) {
		kfree(obuf->data);
		if (obuf->id)
			sdev.bufs[obuf->id - 1] = NULL;
		kfree(obuf);
		sdev.obuf_cnt--;
	}
}

/**
 * Note: Mutex sdev.buf_lock must be locked when calling this function.
 */
static void *sec_obuf_set(struct sec_status *ss, size_t size)
{
	sec_obuf_put(ss->obuf);
	ss->obuf = sec_obuf_alloc(sizeof(struct sec_result) + size);
	if (ss->obuf) {
		sec_obuf_add(ss->obuf, sizeof(struct sec_result));
		memset(ss->obuf->data, 0, sizeof(struct sec_result));
		return ss->obuf->data;
	}
	return NULL;
}

static void sec_obuf_copy(struct sec_obuf *obuf, const void *src, size_t size)
{
	memcpy(obuf->data, src, size);
	obuf->wp = obuf->data + size;
}

static void sec_obuf_append(struct sec_obuf *obuf, const void *src, size_t size)
{
	int count;

	count = min(size, (size_t)(obuf->end - obuf->wp));
	if (count <= 0)
		return;
	memcpy(obuf->wp, src, count);
	obuf->wp += count;
}

/*
 * sec_obuf_get_id() - finds obuf by id
 *
 * used from pa.c code
 *
 * Note: Mutex sdev.buf_lock must be locked when calling this function.
 */
struct sec_obuf *sec_obuf_get_id(int id)
{
	if (id > 0 && id <= ARRAY_SIZE(sdev.bufs))
		return sec_obuf_get(sdev.bufs[id - 1]);
	return NULL;
}

/**
 * Note: Mutex sdev.buf_lock must be locked when calling this function.
 */
static struct sec_obuf *sec_obuf_get_name(char *name)
{
	int i;
	struct sec_obuf *obuf;

	for (i = 0; i < ARRAY_SIZE(sdev.bufs); i++) {
		obuf = sdev.bufs[i];
		if (obuf && strcmp(obuf->name, name) == 0)
			return sec_obuf_get(obuf);
	}
	return NULL;
}

/**
 * Note: Mutex sdev.buf_lock must be locked when calling this function.
 */
static void sec_obuf_release_all(void)
{
	int i;
	struct sec_obuf *obuf;

	for (i = 0; i < ARRAY_SIZE(sdev.bufs); i++) {
		obuf = sdev.bufs[i];
		if (obuf)
			sec_obuf_put(obuf);
	}
}

/**
 * Note: Mutex sdev.buf_lock must be locked when calling this function.
 */
static struct sec_obuf *sec_obuf_create_named(char *name, int len,
							const char *data)
{
	struct sec_obuf *obuf;
	int	i;

	obuf = sec_obuf_get_name(name);
	if (obuf) {
		sec_obuf_put(obuf);
		sec_obuf_put(obuf);
	}
	for (i = 0; i < ARRAY_SIZE(sdev.bufs); i++) {
		if (!sdev.bufs[i]) {
			obuf = sec_obuf_alloc(len);
			if (!obuf)
				return NULL;
			obuf->id = i + 1;
			strcpy(obuf->name, name);
			sdev.bufs[i] = obuf;
			if (data)
				sec_obuf_copy(obuf, data, len);
			else {
				memset(obuf->data, 0x00, len);
				sec_obuf_add(obuf, len);
			}
			return obuf;
		}
	}
	return NULL;
}

/**
 * Note: Mutex sdev.buf_lock must be locked when calling this function.
 */
static void reset_status(struct sec_status *ss)
{
	kfree(ss->data);
	ss->data = NULL;

	sec_obuf_put(ss->obuf);
	ss->obuf = NULL;

	ss->cmd = SEC_CMD_NONE;
	ss->ip = (u8 *)&ss->param;
}

/**
 * sec_read() - Callback function to read results from secure environment.
 *
 * Reads command-specific results from ss->obuf, blocks if nothing to read
 * Treats ss->obuf as a circular buffer, copies available contents to user
 * space and blocks if nothing is available.
 */
static ssize_t sec_read(struct file *filp, char __user *buf, size_t count,
			loff_t *f_pos)
{
	struct sec_status *ss = (struct sec_status *) filp->private_data;
	struct sec_obuf *obuf;
	ssize_t rv = 0;

	if (mutex_lock_interruptible(&ss->lock))
		return -ERESTARTSYS;

	/*
	 * There is always something to read when we have a command, or the
	 * command-specific read function does the blocking.
	 */
	while (ss->cmd == SEC_CMD_NONE) {
		/*FIXME: make sense?*/
		mutex_unlock(&ss->lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(ss->readq,
		    (ss->cmd != SEC_CMD_NONE)))
			return -ERESTARTSYS;

		if (mutex_lock_interruptible(&ss->lock))
			return -ERESTARTSYS;
	}

	obuf = ss->obuf;

	if (obuf->rp >= obuf->wp)
		goto done;
	rv = min(count, (size_t)(obuf->wp - obuf->rp));
	if (copy_to_user(buf, obuf->rp, rv)) {
		rv = -EFAULT;
		goto done;
	}
	obuf->rp += rv;
	if (obuf->rp == obuf->wp) {
done:
		mutex_lock(&sdev.buf_lock);
		reset_status(ss);
		mutex_unlock(&sdev.buf_lock);
		wake_up_interruptible(&ss->writeq);
	}
	mutex_unlock(&ss->lock);
	pr_debug("rv: %d\n", rv);
	return rv;
}

static int check_cmd_arg_length(size_t got, size_t shouldhave)
{
	if (got > shouldhave)
		return -E2BIG;

	if (got < shouldhave)
		return -EINVAL;

	return 0;
}

/*
 * call_init_helper - calls bb5-init helper
 *
 * used by the driver to load/store secure storage on demand.
 * secure storage might be needed by request from kernel code
 */
static int call_init_helper(int event)
{
	char *argv[3], *envp[4], action[32];
	int rv, i;

	i = 0;
	argv[i++] = "/usr/sbin/bb5-init";
	argv[i++] = event ? "load" : "save";
	argv[i] = NULL;

	sprintf(action, "ACTION=%s", argv[1]);

	i = 0;
	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[i++] = action;
	envp[i] = NULL;

	/*
	 * Allow user space helper (and other user space applications) to
	 * access buffers. Other access to security device is denied to make
	 * sure that other needed data structures are not changed in-between.
	 */
	mutex_unlock(&sdev.buf_lock);
	rv = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	mutex_lock(&sdev.buf_lock);

	pr_debug("rv: %d\n", rv);

	return rv;
}

static int process_cmd_init_secs(struct sec_status *ss)
{
	struct sec_result *output;

	output = sec_obuf_set(ss, 0);
	if (!output)
		return -ENOMEM;

	pr_debug("secs len: %d\n", ss->param.length);
	if (!sdev.secs) {
		/* allocate initial storage */
		sdev.secs = sec_obuf_create_named("SECS",
				SEC_STORAGE_SIZE, NULL);
		if (!sdev.secs)
			return -ENOMEM;
	}

	pr_debug("secs: %p, data: %p\n", sdev.secs, sdev.secs->data);

	if (ss->param.length) {
		if (ss->param.length <= sec_obuf_space(sdev.secs))
			sec_obuf_copy(sdev.secs, ss->data, ss->param.length);
	} else
		/* cleanup storage */
		memset(sdev.secs->data, 0, sec_obuf_space(sdev.secs));

	sdev.ops->flags |= SEC_FLAGS_NEW_SECS;

	return 0;
}

static int process_cmd_setpabin(struct sec_status *ss)
{
	struct sec_result *output;

	output = sec_obuf_set(ss, 0);
	if (!output)
		return -ENOMEM;
	ss->data[ss->param.length - 1] = '\0';
	strlcpy(ss->pabin, ss->data, sizeof(ss->pabin));
	pr_debug("pabin: %s\n", ss->pabin);
	return 0;
}

static int process_cmd_version(struct sec_status *ss)
{
	struct sec_result *output;
	int rv;

	rv = check_cmd_arg_length(ss->param.length, 0);
	if (rv)
		return rv;

	output = sec_obuf_set(ss, 2 * sizeof(u32));
	if (!output)
		return -ENOMEM;

	rv = pa_image_version_get(sdev.fw[sdev.fwid].pa_bin.data,
				  &sdev.fw[sdev.fwid].format,
					(u32 *)output->data,
					 (u32 *)output->data + 1);
	if (rv)
		return rv;

	sec_obuf_add(ss->obuf, 2 * sizeof(u32));

	return rv;
}

static int process_cmd_random(struct sec_status *ss)
{
	int rv;
	struct sec_result *output;
	struct sec_cmd_random_args *args = (struct sec_cmd_random_args *)
					   ss->data;

	rv = check_cmd_arg_length(ss->param.length, sizeof(*args));
	if (rv)
		return rv;
	output = sec_obuf_set(ss, args->len);
	if (!output)
		return -ENOMEM;
	rv = sdev.ops->random_get(output->data, args->len, output);
	if (!rv && output->rom_rv == SEC_OK)
		sec_obuf_add(ss->obuf, args->len);
	return rv;
}

static int process_cmd_create(struct sec_status *ss)
{
	struct sec_result *output;
	struct sec_cmd_load_args *args = (struct sec_cmd_load_args *) ss->data;
	struct sec_obuf *obuf;
	void	*data = NULL;

	if (ss->param.length == args->len + sizeof(*args))
		data = args->data;

	args->name[sizeof(args->name) - 1] = '\0';

	obuf = sec_obuf_create_named(args->name, args->len, data);
	if (!obuf)
		return -ENOMEM;

	pr_debug("cmd length %d, data length: %d, name: %s, id: %d\n",
		  ss->param.length, args->len, args->name, obuf->id);

	output = sec_obuf_set(ss, sizeof(int));
	if (!output) {
		sec_obuf_put(obuf);
		return -ENOMEM;
	}
	sec_obuf_append(ss->obuf, &obuf->id, sizeof(obuf->id));
	return 0;
}

static int process_cmd_read(struct sec_status *ss)
{
	struct sec_result *output;
	struct sec_obuf	*obuf;
	int	rv, len;
	struct sec_cmd_save_args *args = (struct sec_cmd_save_args *)
					   ss->data;

	args->name[sizeof(args->name) - 1] = '\0';

	rv = check_cmd_arg_length(ss->param.length, sizeof(*args));
	if (rv)
		return rv;

	if (args->id > 0)
		obuf = sec_obuf_get_id(args->id);
	else
		obuf = sec_obuf_get_name(args->name);

	if (!obuf)
		return -ENODEV;

	len = sec_obuf_len(obuf);

	output = sec_obuf_set(ss, sizeof(int) + len);
	if (!output) {
		rv = -ENOMEM;
		goto err;
	}
	sec_obuf_append(ss->obuf, &len, sizeof(int));
	sec_obuf_append(ss->obuf, obuf->data, len);
	pr_debug("cmd length %d, name: %s, id: %d, len: %d\n",
		  ss->param.length, args->name, args->id, len);
err:
	sec_obuf_put(obuf);
	return rv;
}

static int process_cmd_free(struct sec_status *ss)
{
	struct sec_result *output;
	struct sec_obuf	*obuf;
	int	rv;
	struct sec_cmd_save_args *args = (struct sec_cmd_save_args *)
					   ss->data;

	args->name[sizeof(args->name) - 1] = '\0';

	rv = check_cmd_arg_length(ss->param.length, sizeof(*args));
	if (rv)
		return rv;

	if (args->id > 0)
		obuf = sec_obuf_get_id(args->id);
	else
		obuf = sec_obuf_get_name(args->name);

	if (!obuf)
		return -ENODEV;

	output = sec_obuf_set(ss, 0);
	if (!output)
		return -ENOMEM;
	sec_obuf_put(obuf);
	sec_obuf_put(obuf);
	return 0;
}

static int secenv_init(struct sec_status *ss, int flags,
		       struct sec_result *output)
{
	int	rv = 0;

	output->rom_rv = 0;

	pr_debug("calling secenv_init()...\n");

	if ((sdev.ops->flags & SEC_FLAGS_NEW_PAPUB) &&
		    (flags & PA_FLAGS_NEED_PAPUB)) {
		rv = sdev.ops->import_papub(sdev.fw[sdev.fwid].papub_bin.data,
					   output);
		sdev.ops->flags &= ~SEC_FLAGS_NEW_PAPUB;
		if (rv < 0)
			return rv;
	}

	if (!sdev.secs && (flags & PA_FLAGS_NEED_SECS)) {
		call_init_helper(1);
		if (!sdev.secs) {
			pr_err("Secure storage uninitialized\n");
			return -ENODEV;
		}
	}

	if ((sdev.ops->flags & SEC_FLAGS_NEW_SECS) &&
		    (flags & PA_FLAGS_NEED_SECS)) {
		rv = sdev.ops->init_secs(sdev.secs->data,
					sec_obuf_len(sdev.secs), output);
		sdev.ops->flags &= ~SEC_FLAGS_NEW_SECS;
	}

	pr_debug("rv: %d, rom_rv: %d\n", rv, output->rom_rv);

	return rv;
}

/**
 * call_pa_service() - Transfer PA to secure environment and calls service on it.
 *
 * @pa_name:	Name of PA to call.
 * @pa_sub:	PA sub service ID.
 * @params:	Params for PA call.
 * @results:	Results from PA call.
 * @output:	Return value from PA call.
 *
 * This function assumes that mutex sdev.lock and sdev.buf_lock has been
 * locked before entry.
 *
 * Returns zero on successful invocation, negative on failure.
 */
static int call_pa_service(const char *pa_name, int pa_sub,
		   const void *params, void *results, struct sec_result *output)
{
	int rv;
	const void *pa_addr;
	size_t pa_size;

	pr_debug("calling pa_service()...\n");

	pa_addr = pa_image_address(sdev.fw[sdev.fwid].pa_bin.data, pa_name,
				   &pa_size);
	if (!pa_addr) {
		pr_debug("PA not found %s !\n", pa_name);
		return -ENXIO;
	}

	pr_debug("pa_addr: %p, pa_size: %d\n", pa_addr, pa_size);

	/* Make sure we are not overflowing PA transfer buffer */
	if (unlikely(pa_size > sdev.pa_transfer_buffer_size)) {
		pr_err("PA was too large to be used: %s!\n", pa_name);
		return -ENOMEM;
	}

	if (unlikely(!sdev.pa_transfer_buffer)) {
		pr_err("PA transfer buffer was not found! (pa: %s)\n", pa_name);
		return -ENOMEM;
	}

	/* Copy PA to transfer buffer */
	memcpy(sdev.pa_transfer_buffer, pa_addr, pa_size);

	rv = sdev.ops->pa_service(sdev.pa_transfer_buffer, pa_sub, params,
				  results, output);

	output->pa_rv = ((struct pa_res_common *)results)->pa_rv;

	pr_debug("rv: %d, rom_rv: %d, pa_rv: %d\n", rv,
		  output->rom_rv, output->pa_rv);

	if (output->rom_rv == SEC_OK && output->pa_rv == SEC_OK) {
		if (sdev.ops->flags & SEC_FLAGS_SECS_CHANGED) {
			call_init_helper(0);
			sdev.ops->flags &= ~SEC_FLAGS_SECS_CHANGED;
		}
	}

	return 0;
}

/**
 * sec_pa_service() - kernel interface to access PA services
 *
 * This function is used by kernel code to call Protected Application
 * services. Currently is used by Aegis Validator.
 */
int sec_pa_service(const char *pa_name, int pa_sub, int flags,
		   const void *params, void *results, struct sec_result *output)
{
	int	rv = ENODEV;

	mutex_lock(&sdev.lock);
	mutex_lock(&sdev.buf_lock);

	if (!sdev.ops)
		goto out;

	rv = firmware_init(NULL);
	if (rv < 0)
		goto out;

	rv = secenv_init(NULL, flags, output);
	if (rv || output->rom_rv)
		/* initialization failed */
		goto out;

	rv = call_pa_service(pa_name, pa_sub, params, results, output);

out:
	mutex_unlock(&sdev.buf_lock);
	mutex_unlock(&sdev.lock);
	return rv;
}
EXPORT_SYMBOL_GPL(sec_pa_service);

int sec_rom_service(int cmd, const void *params,
			    struct sec_result *output)
{
	int rv;
	pr_debug("calling rom_service()...\n");
	rv = sdev.ops->rom_service(cmd, params, output);
	pr_debug("rv: %d, rom_rv: %d\n", rv, output->rom_rv);
	return rv;
}
EXPORT_SYMBOL_GPL(sec_rom_service);

/*
 * call_cmd_pa() - main function to call PA services
 *
 * prepares parameters, initializes sec env, calls pa service
 * extract results
 */
static int call_cmd_pa(struct sec_status *ss, size_t coup,
		       struct sec_result *output)
{
	struct pa_command_data p;
	int	rv, rv1, flags = 0;

	if (!output)
		return -EINVAL;

	p.resource = 0;

	p.format = &sdev.fw[sdev.fwid].format;
	p.vtp    = sdev.ops->vtp;
	p.ptv    = sdev.ops->ptv;
	p.cinp   = ss->param.length;
	p.coup   = coup;
	p.input  = ss->data;
	p.output = output->data;
	p.papub = sdev.fw[sdev.fwid].papub_bin.data;

	p.hal = (ss->cmd < SEC_CMD_PA_FIRST) ? 1 : 0;

	rv = pa_command_prepare(ss->cmd, &p);
	if (rv < 0)
		goto err;

	flags = p.c->cmd->flags;

	rv = secenv_init(ss, flags, output);
	if (rv || output->rom_rv)
		/* initialization failed */
		goto err1;

	if (p.hal)
		rv = sec_rom_service(p.c->cmd->index, p.par, output);
	else
		rv = call_pa_service(p.c->cmd->filename, p.c->cmd->sa,
				     p.par, p.res, output);

err1:
	rv1 = pa_command_finish(&p);
err:
	if (WARN(p.resource != 0, "resource counter is not 0 (was %ld)\n",
		 p.resource)) {
		pr_info("Calling process: %s (%d)\n", current->comm,
			(int)current->pid);
		pr_info("cmd=0x%x, index=0x%x, sa=0x%x\n", p.c->cmd->cmd,
			p.c->cmd->index, p.c->cmd->sa);
	}
	return rv ? rv : rv1;
}

static int process_cmd_pa(struct sec_status *ss)
{
	struct sec_result *output;
	size_t cpar, cres;
	int rv;

	pr_debug("started: %d\n", ss->cmd);

	output = sec_obuf_set(ss, 0);
	if (!output)
		return -ENOMEM;

	pr_debug("param.length: %d\n", ss->param.length);

	rv = firmware_init(ss->pabin);
	if (rv < 0)
		return rv;

	rv = pa_command_query(ss->cmd, ss->data, ss->param.length, &cpar, &cres,
				 &sdev.fw[sdev.fwid].format);
	if (rv) {
		pr_debug("PA query failed!\n");
		return rv;
	}
	pr_debug("cpar: %d, cres: %d\n", cpar, cres);

	rv = check_cmd_arg_length(ss->param.length, cpar);
	if (rv)
		return rv;

	output = sec_obuf_set(ss, cres);
	if (!output)
		return -ENOMEM;

	rv = call_cmd_pa(ss, cres, output);

	if (!rv && output->rom_rv == SEC_OK && output->pa_rv == SEC_OK)
		sec_obuf_add(ss->obuf, cres);

	return rv;
}

static inline int process_cmd(struct sec_status *ss)
{
	int rv = -EACCES;
	int sdevlock = 0;

	ss->cmd = ss->param.cmd;

	/* Lock global security driver lock for all calls that need it. */
	if ((ss->cmd >= SEC_CMD_ROM_FIRST) || (ss->cmd == SEC_CMD_RANDOM1)
	    || (ss->cmd == SEC_CMD_VERSIONS_GET)) {
		mutex_lock(&sdev.lock);
		sdevlock = 1;
	}

	/* All calls need buffer lock. */
	mutex_lock(&sdev.buf_lock);

	switch (ss->cmd) {
	case SEC_CMD_INIT_SECS:
		rv = process_cmd_init_secs(ss);
		break;
	case SEC_CMD_SETPANAME:
		rv = process_cmd_setpabin(ss);
		break;
	case SEC_CMD_CREATE:
		rv = process_cmd_create(ss);
		break;
	case SEC_CMD_READ:
		rv = process_cmd_read(ss);
		break;
	case SEC_CMD_FREE:
		rv = process_cmd_free(ss);
		break;
	case SEC_CMD_RANDOM1:
		rv = process_cmd_random(ss);
		break;
	case SEC_CMD_VERSIONS_GET:
		rv = process_cmd_version(ss);
		break;
	default:
		if (ss->cmd >= SEC_CMD_ROM_FIRST)
			rv = process_cmd_pa(ss);
		break;
	}

	if (!rv)
		wake_up_interruptible(&ss->readq);

	mutex_unlock(&sdev.buf_lock);

	if (sdevlock)
		mutex_unlock(&sdev.lock);

	return rv;
}

static long sec_ioctl(struct file *filp, unsigned int cmd,
		      unsigned long arg)
{
	ssize_t rv = 0;
	struct sec_status *ss = (struct sec_status *)filp->private_data;

	pr_debug("ioctl, cmd = 0x%08X, arg = 0x%08lX\n", cmd, arg);

	mutex_lock(&ss->lock);

	switch (cmd) {
	case IOCTL_SET_CMD:
		mutex_lock(&sdev.buf_lock);
		reset_status(ss);
		mutex_unlock(&sdev.buf_lock);
		ss->param.cmd = (u32)arg;
		ss->ip += sizeof(u32);
		break;
	default:
		rv = -EINVAL;
		break;
	}

	mutex_unlock(&ss->lock);

	pr_debug("rv: %d\n", rv);

	return rv;
}

static ssize_t sec_write(struct file *filp, const char __user *buf,
			size_t count, loff_t *f_pos)
{
	size_t size, c;
	ssize_t rv = 0;
	struct sec_status *ss = (struct sec_status *) filp->private_data;
	const char __user *ptr = buf;

	pr_debug("count: %d, cmd: %d\n", count, ss->cmd);

	if (mutex_lock_interruptible(&ss->lock))
		return -ERESTARTSYS;

	while (ss->cmd != SEC_CMD_NONE) {

		mutex_unlock(&ss->lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(ss->writeq,
		    (ss->cmd == SEC_CMD_NONE)))
			return -ERESTARTSYS;

		if (mutex_lock_interruptible(&ss->lock))
			return -ERESTARTSYS;
	}

	while (count) {
		/*
		* The amount of data we are still expecting to receive before a
		* command can be processed.
		*/
		if (ss->data)
			size = ss->param.length  - (size_t)(ss->ip - ss->data);
		else
			size = sizeof(ss->param) -
					(size_t)(ss->ip - (u8 *)&ss->param);

		c = min(count, size);

		if (copy_from_user(ss->ip, ptr, c)) {
			rv = -EFAULT;
			break;
		}

		ss->ip += c;
		ptr += c;
		count -= c;
		size -= c;

		if (size == 0) {
			if (!ss->data) {
				size = ss->param.length;

				if (size < 0 || size > SEC_MAX_DATA_SIZE) {
					rv = -EFBIG;
					break;
				}

				if (size) {
					/*allocate the buffer*/
					ss->data = kmalloc(size, GFP_KERNEL);
					if (!ss->data) {
						rv = -ENOMEM;
						break;
					}
					/* Continue with the parameter data */
					ss->ip = ss->data;
					continue;
				}
			}
			/* all command data received, process... */
			rv = process_cmd(ss);
			break;
		}
	}

	if (rv < 0) {
		/* Command failed, wake up any pending writers */
		mutex_lock(&sdev.buf_lock);
		reset_status(ss);
		mutex_unlock(&sdev.buf_lock);
		wake_up_interruptible(&ss->writeq);
	} else
		rv = ptr - buf;

	mutex_unlock(&ss->lock);
	pr_debug("rv: %d\n", rv);
	return rv;
}

static unsigned int sec_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct sec_status *ss = (struct sec_status *) filp->private_data;
	unsigned int mask = 0;

	mutex_lock(&ss->lock);

	poll_wait(filp, &ss->writeq,  wait);
	poll_wait(filp, &ss->readq, wait);

	if (ss->cmd == SEC_CMD_NONE)
		mask |= POLLOUT | POLLWRNORM;
	else if (ss->obuf && ss->obuf->rp != ss->obuf->wp)
		mask |= POLLIN | POLLRDNORM;

	mutex_unlock(&ss->lock);
	return mask;
}

static int sec_open(struct inode *inode, struct file *filp)
{
	int	rv;
	struct sec_status *ss;

	pr_debug("enter\n");

	if (!sdev.ops || !try_module_get(sdev.ops->owner)) {
		request_module("omap_hs");
		if (!sdev.ops || !try_module_get(sdev.ops->owner))
			return -ENODEV;
	}

	ss = kzalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss) {
		module_put(sdev.ops->owner);
		return -ENOMEM;
	}

	mutex_init(&ss->lock);
	init_waitqueue_head(&ss->writeq);
	init_waitqueue_head(&ss->readq);
	mutex_lock(&sdev.buf_lock);
	reset_status(ss);
	mutex_unlock(&sdev.buf_lock);

	filp->private_data = ss;
	rv = nonseekable_open(inode, filp);
	if (rv)
		module_put(sdev.ops->owner);
	return rv;
}

static int sec_release(struct inode *inode, struct file *filp)
{
	struct sec_status *ss = (struct sec_status *) filp->private_data;

	pr_debug("enter\n");
	mutex_lock(&sdev.buf_lock);
	reset_status(ss);
	mutex_unlock(&sdev.buf_lock);
	kfree(ss);
	/* should not be NULL here */
	module_put(sdev.ops->owner);
	return 0;
}

/**
 * sec_register() - register security operations
 *
 * OMAP3 HS security operations register itself with this call.
 * omap_sec is a generic code and access OMAP3 secure environment via
 * omap_hs driver.
 */
int sec_register(struct sec_operations *ops)
{
	int rv = -EBUSY;

	/* mutex_lock() was here, but omap_hs is under try_module_get() */
	if (ops && !sdev.ops) {
		rv = 0;
		sdev.ops = ops;
	}
	return rv;
}
EXPORT_SYMBOL_GPL(sec_register);

/**
 * sec_unregister() - register security operations
 *
 * OMAP3 HS security operations unregister itself with this call
 */
int sec_unregister(struct sec_operations *ops)
{
	int rv = -ENODEV;

	if (ops && sdev.ops == ops) {
		sdev.ops = NULL;
		rv = 0;
	}
	if (sdev.secs) {
		sec_obuf_put(sdev.secs);
		sdev.secs = NULL;
	}
	return rv;
}
EXPORT_SYMBOL(sec_unregister);

static const struct file_operations sec_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= sec_read,
	.write		= sec_write,
	.poll		= sec_poll,
	.unlocked_ioctl	= sec_ioctl,
	.open		= sec_open,
	.release	= sec_release,
};

static int __init sec_init(void)
{
	int rv = -ENODEV;

	pr_info("omap_sec driver\n");

	if (!kci) {
		pr_err("kci parameter is not set\n");
		return -EINVAL;
	}

	mutex_init(&sdev.lock);
	mutex_init(&sdev.buf_lock);

	sdev.misc_dev.minor = minor;
	sdev.misc_dev.name  = "omap_sec";
	sdev.misc_dev.fops  = &sec_fops;

	rv = misc_register(&sdev.misc_dev);
	if (rv < 0) {
		pr_err("Error registering misc device: %d\n", rv);
		return rv;
	}

	return 0;
}

static void __exit sec_exit(void)
{
	misc_deregister(&sdev.misc_dev);
	sec_obuf_release_all();
	if (sdev.obuf_cnt)
		pr_err("err, obuf count is not zero: %d\n", sdev.obuf_cnt);
	firmware_cleanup_all();
}

module_init(sec_init);
module_exit(sec_exit);

MODULE_DESCRIPTION("OMAP HS secure mode driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sami Tolvanen, Dmitry Kasatkin");
