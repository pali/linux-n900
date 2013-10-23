/*
 * linux/include/asm-arm/arch-omap/pa.h
 *
 * Copyright (C) 2007 - 2010 Nokia Corporation
 * Author: Sami Tolvanen
 *         Timo O. Karjalainen <timo.o.karjalainen@nokia.com>
 *         Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP HS protected applications (PA) format handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __ARCH_ARM_OMAP_PA_H
#define __ARCH_ARM_OMAP_PA_H

/* we remove those sections when everything is proven to work */
#define PA_FMT_MAGIC		0x4D464151

#define PA_MAX_NCMD	128

enum {
	PA_TYPE_DATA,
	PA_TYPE_PTR_VIRT,
	PA_TYPE_PTR_PHYS,
	PA_TYPE_UNUSED, /* was PA_TYPE_PTR_KEYS */
	PA_TYPE_PTR_PAPUB,
	PA_TYPE_BUF_LEN,
	PA_TYPE_BUF_PTR,
	PA_TYPE_CRED,
	PA_TYPE_FOREIGN_CRED,
};

enum {
	PA_IO_NORMAL,
	PA_IO_RESERVED,
	PA_IO_DEFOUT,
	PA_IO_SIZE_VAL,
	PA_IO_SIZE_PAR,
	PA_IO_ONLY,
};

#define PA_DIR_IN	0x01
#define PA_DIR_OUT	0x02
#define PA_DIR_INOUT	0x03

#define PA_CREDS_LEN_MAX        52
#define PA_CREDS_COUNT_MAX      4

struct pa_format_command {
	u32 cmd;
	u32 creds_count;
	u8 creds[PA_CREDS_COUNT_MAX][PA_CREDS_LEN_MAX];
	u8 filename[12];	/* TOC filename */
	u8 sa;			/* Sub-application */
	u8 index;		/* Function index */
#define PA_FLAGS_NEED_PAPUB	0x01
#define PA_FLAGS_NEED_SECS	0x02
	u8 flags;
	u8 npar;
	u8 nres;
};

#define PA_MAKE_TYPE(type, io, dir)	((type & 0xff) | \
					((io << 8) & 0xff00) | \
					((dir << 16) & 0xff0000))
#define PA_TYPE(T)	((T) & 0xff)
#define PA_IO(T)	(((T) >> 8) & 0xff)
#define PA_DIR(T)	(((T) >> 16) & 0xff)

#define PA_FLAG_PREP	0x01000000

struct pa_format_entry {
	u32 type;
	u32 size;
	u32 value;
};

#define PA_IMAGE_VERSION_OFFSET		56

struct pa_format_header {
	u32 magic;
	u32 version;
	u32 ncmd;
};

#define PA_TOC_ENTRY_LAST	0xFF

struct pa_image_toc_entry {
	u32 start;
	u32 size;
	u32 spare0;
	u32 spare1;
	u32 spare2;
	u8 filename[12];
};

struct pa_command {
	struct pa_format_command *cmd;
	struct pa_format_entry *par;
	struct pa_format_entry *res;
};

struct pa_format {
	struct pa_command *cmd;	/* Commands */
	u32 ncmd;
	u32 version;
};

struct pa_command_data {
	struct pa_command *c;
	struct pa_format *format;
	dma_addr_t (*vtp)(const void *virt);	/* Virtual to physical */
	void *(*ptv)(const dma_addr_t phys);	/* Physical to virtual */
	u8 *input;
	size_t cinp;
	u8 *output;
	size_t coup;
	size_t cpar;		/* PA parameter buffer size */
	size_t cres;		/* PA result buffer size */
	u8 *par;
	u8 *res;
	void *papub;	/* PA public keys (PAPUBKEYS) */
	u8 *opos;
	u8 *oend;
	u8 hal;
	unsigned long resource;
};

struct pa_par_common {
	u16 length;
	u8 index;
	u8 reserved;
};

struct pa_res_common {
	u32 pa_rv;
};

struct hal_par_common {
	int	npar;
};

extern const void *pa_image_address(const struct pa_image_toc_entry *base,
				    const char *filename, size_t *size);

extern size_t pa_find_max_pa_size(const struct pa_image_toc_entry *base);

extern int pa_format_parse(u8 *base, size_t size, struct pa_format *p,
			   size_t offset);

extern int pa_command_query(u32 cmd, u8 *input, size_t cinp, size_t *mininp,
			    size_t *maxout, struct pa_format *p);

extern int pa_command_prepare(u32 cmd, struct pa_command_data *p);

extern int pa_command_finish(struct pa_command_data *p);

extern int pa_image_version_get(const struct pa_image_toc_entry *pa_base,
				const struct pa_format *fmt,
				u32 *pa_ver, u32 *fmt_ver);

#endif /* __ARCH_ARM_OMAP_PA_H */
