/*
 * linux/include/asm-arm/arch-omap/sec.h
 *
 * Copyright (C) 2007 - 2010 Nokia Corporation
 * Authors: Sami Tolvanen
 *          Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP HS secure mode driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __ARCH_ARM_OMAP_SEC_H
#define __ARCH_ARM_OMAP_SEC_H

enum {
	SEC_CMD_NONE,
	SEC_CMD_INIT_SECS,
	SEC_CMD_SETPANAME,
	SEC_CMD_CREATE,
	SEC_CMD_READ,
	SEC_CMD_FREE,
	SEC_CMD_RANDOM1,
	SEC_CMD_VERSIONS_GET,
	SEC_CMD_ROM_FIRST = 0x50,
	SEC_CMD_RANDOM = SEC_CMD_ROM_FIRST,
	SEC_CMD_PA_FIRST = 0x100,
	SEC_CMD_ROT13 = SEC_CMD_PA_FIRST
};

#define IOCTL_SET_CMD	_IOW(0xB5, 0, u32)

/* Secure environment error codes */
#define SEC_OK		0x00000000
#define SEC_FAIL	0x00000001

#define SEC_STORAGE_SIZE	4096

#define SEC_PABIN_NAME_SIZE	128
/* 20 bytes goes to name postfix and KCI */
#define SEC_PABIN_PATH_SIZE	(SEC_PABIN_NAME_SIZE + 20)
#define SEC_BUF_NAME_SIZE	16


struct sec_result {
	u32 rom_rv;	/* Secure ROM return value */
	u32 pa_rv;
	u8  data[0];	/* Variable length */
};

/*
 * DEVICE I/O INTERFACE
 *
 * Begin all commands with a 32-bit command code (SEC_CMD_* or SEC_CMD_PA_*)
 * followed by 32-bit length of argument data. Structure of argument data is
 * documented in per-command structure definitions below.
 */
struct sec_cmd_header {
	u32 cmd;
	u32 length;
};

struct sec_cmd_random_args {
	int len;	/* How many bytes of random data you want */
};
/* Output of SEC_CMD_RANDOM is the requested number of random bytes. */

struct sec_cmd_load_args {
	int	len;
	char	name[SEC_BUF_NAME_SIZE]; /* buffer name */
	char	data[0];
};

struct sec_cmd_save_args {
	int	id;	/* buffer id */
	char	name[SEC_BUF_NAME_SIZE]; /* buffer name */
	char	data[0];
};

struct sec_obuf;

struct sec_operations {
	struct module	*owner;
	int (*init_secs)(const void *secs, int secs_len,
	      struct sec_result *out);
	int (*import_papub)(const void *papub, struct sec_result *out);
	int (*random_get)(void *buf, int len, struct sec_result *out);
	int (*pa_service)(const void *pa_addr, int pa_sub,
	      const void *params, void *results, struct sec_result *output);
	int (*rom_service)(int cmd, const void *params,
	      struct sec_result *output);
	int (*rom_verify)(const void *img, size_t size, size_t *startup);
	dma_addr_t (*vtp)(const void *virt);
	void *(*ptv)(const dma_addr_t phys);
#define	SEC_FLAGS_NEW_SECS		0x01
#define	SEC_FLAGS_SECS_CHANGED		0x02
#define	SEC_FLAGS_NEW_PAPUB		0x04
	int	flags;
};

struct sec_obuf {
	u8		*data;		/* Output buffer */
	u8		*rp;		/* Read position */
	u8		*wp;		/* Write position */
	u8		*end;
	atomic_t	refcnt;
	int		id;
	char		name[SEC_BUF_NAME_SIZE];
};

void sec_obuf_put(struct sec_obuf *obuf);
struct sec_obuf *sec_obuf_get_id(int id);

static inline int sec_obuf_len(struct sec_obuf *obuf)
{
	return obuf->wp - obuf->data;
}

static inline int sec_obuf_space(struct sec_obuf *obuf)
{
	return obuf->end - obuf->data;
}

static inline struct sec_obuf *sec_obuf_get(struct sec_obuf *obuf)
{
	if (obuf)
		atomic_inc(&obuf->refcnt);
	return obuf;
}

static inline void sec_obuf_add(struct sec_obuf *obuf, size_t len)
{
	obuf->wp += len;
}

extern int sec_register(struct sec_operations *ops);
extern int sec_unregister(struct sec_operations *ops);
extern int sec_pa_service(const char *pa_name, int pa_sub, int flags,
		const void *params, void *results, struct sec_result *output);
extern int sec_rom_service(int cmd, const void *params,
			    struct sec_result *output);

#endif /* __ARCH_ARM_OMAP_SEC_H */
