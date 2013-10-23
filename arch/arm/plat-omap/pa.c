/*
 * linux/arch/arm/plat-omap/pa.c
 *
 * Copyright (C) 2007 - 2010 Nokia Corporation
 * Author: Sami Tolvanen
 *         Timo O. Karjalainen <timo.o.karjalainen@nokia.com>
 *         Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP HS protected application (PA) format handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <plat/pa.h>
#include <plat/sec.h>

#ifdef CONFIG_SECURITY_AEGIS_RESTOK
#include <linux/cred.h>
#include <linux/aegis/restok.h>
#include <crypto/hash.h>
#endif

#ifdef CONFIG_SECURITY_AEGIS_CREDS
#include <linux/aegis/creds.h>
#endif

/*
 * this code does parsing of Protected Applications command parameters and
 * results passed from/to user space and build parameters and result buffers
 * which are passed to secure environment.
 * formatting data are loaded from /lib/firmware/omap3_pafmt.bin
 * read Documentation/arm/OMAP/omap_sec.txt for syntax explanation
 */

typedef u64 pa_cred_t;

static int buffer_skip(u8 **buf, u8 *end, size_t size)
{
	if (end - *buf < size) {
		pr_debug("buffer too small\n");
		return -EINVAL;
	}
	*buf += size;
	return 0;
}

static int buffer_copy(u8 **buf, u8 *end, void *data, size_t size)
{
	if (end - *buf < size) {
		pr_debug("buffer too small\n");
		return -EINVAL;
	}
	memcpy(*buf, data, size);
	*buf += size;
	return 0;
}

static int buffer_move(u8 **dbuf, u8 *dend, u8 **sbuf, u8 *send, size_t size)
{
	if ((dend - *dbuf <  size) || (send - *sbuf < size)) {
		pr_debug("buffer too small\n");
		return -EINVAL;
	}
	memcpy(*dbuf, *sbuf, size);
	*dbuf += size;
	*sbuf += size;
	return 0;
}

static int buffer_copy_source(u8 *dbuf, u8 **sbuf, u8 *send, size_t size)
{
	if (send - *sbuf < size) {
		pr_debug("buffer too small\n");
		return -EINVAL;
	}
	memcpy(dbuf, *sbuf, size);
	*sbuf += size;
	return 0;
}

/*
 * pa_image_address() - finds start of PA in the binary
 *
 * @base:	Base pointer to PA firmware package's TOC.
 * @filename:	Filename to search from firmware package.
 * @size:	Returned size of the target PA.
 *
 * PA binary contains several PAs and prepended by Table of Contents (TOC)
 * this function finds PA start address to be passed to secure environment.
 *
 * Returns pointer to PA image if found or NULL if not.
 */
const void *pa_image_address(const struct pa_image_toc_entry *base,
			     const char *filename, size_t *size)
{
	const struct pa_image_toc_entry *p = base;

	for (; p->filename[0] != PA_TOC_ENTRY_LAST; p++) {
		if (!strcmp(filename, p->filename)) {
			*size = p->size;
			return (const void *)(((const u8 *)base) + p->start);
		}
	}

	return NULL;
}

/**
 * pa_find_max_pa_size() - finds maximum size of PAs.
 *
 * @base:	Base pointer to PA firmware package's TOC
 *
 * PA binary contains several PAs and prepended by Table of Contents (TOC)
 * this function finds maximum PA size.
 *
 * Returns number of bytes required by largest PA.
 */
size_t pa_find_max_pa_size(const struct pa_image_toc_entry *base)
{
	const struct pa_image_toc_entry *p = base;
	size_t size = 0;

	for (; p->filename[0] != PA_TOC_ENTRY_LAST; p++)
		size = max(size, p->size);

	return size;
}

/*
 * pa_format_parse - parse PA format binary
 *
 * format binary contains descriptions for all commands supported by PA binary
 * description includes command flags, argument types
 */
int pa_format_parse(u8 *base, size_t size, struct pa_format *p, size_t offset)
{
	struct pa_format_header *hdr;
	struct pa_command *c;
	int i;
	u8 *end = base + size;

	if (offset && buffer_skip(&base, end, offset) < 0)
		return -EINVAL;

	hdr = (struct pa_format_header *)base;

	if (hdr->magic != PA_FMT_MAGIC || hdr->ncmd > PA_MAX_NCMD) {
		pr_err("invalid format file\n");
		return -EINVAL;
	}

	if (buffer_skip(&base, end, sizeof(*hdr)) < 0)
		return -EINVAL;

	p->ncmd = hdr->ncmd;
	p->version = hdr->version;
	/* kfree by sec_firnware_cleanup() in any case */
	p->cmd  = kzalloc(p->ncmd * sizeof(*p->cmd), GFP_KERNEL);
	if (!p->cmd)
		return -ENOMEM;

	for (i = 0; i < p->ncmd; i++) {
		c = &p->cmd[i];
		c->cmd = (struct pa_format_command *) base;

		if (buffer_skip(&base, end, sizeof(*c->cmd)) < 0)
			return -EINVAL;

		c->par = (struct pa_format_entry *) base;
		if (buffer_skip(&base, end, c->cmd->npar * sizeof(*c->par)) < 0)
			return -EINVAL;

		c->res = (struct pa_format_entry *) base;
		if (buffer_skip(&base, end, c->cmd->nres * sizeof(*c->res)) < 0)
			return -EINVAL;
	}

	return 0;
}

static int entry_io_size(struct pa_format_entry *p, int n,
		struct pa_command *c, u8 *input, size_t cinp, size_t *size);

static int entry_input_value(struct pa_command *c, size_t n, u8 *input,
			     size_t cinp, size_t *value)
{
	struct pa_format_entry *entry = &c->par[n];
	int i;
	size_t size;
	u8 *end = input + cinp;
	u8 *orig_input = input;

	*value = 0;

	pr_debug("enter\n");

	if (PA_TYPE(entry->type) != PA_TYPE_DATA ||
				entry->size > sizeof(*value)) {
		pr_debug("invalid type %u for ref %u\n",
			PA_TYPE(entry->type), PA_IO(entry->type));
		return -EINVAL;
	}

	for (i = 0; i < n; i++) {
		if (entry_io_size(c->par, i, c, orig_input, cinp, &size)
				  < 0)
			return -EINVAL;
		if (buffer_skip(&input, end, size) < 0)
			return -EINVAL;
	}

	if (end - input < entry->size) {
		pr_debug("buffer too small\n");
		return -EINVAL;
	}

	memcpy(value, input, entry->size);
#ifdef __BIG_ENDIAN
	/*
	  Preserve the numeric value
	  this is NOT a be_to_le or le_to_be conversion
	*/
	*value >>= (sizeof(*value) - entry->size) << 3;
#endif
	return 0;
}

static int entry_io_size(struct pa_format_entry *p, int n, struct pa_command *c,
			 u8 *input, size_t cinp, size_t *size)
{
	struct pa_format_entry *entry = &p[n];
	u32 type = PA_TYPE(entry->type);

	pr_debug("enter: %d\n", sizeof(*entry));

	*size = 0;

	switch (PA_IO(entry->type)) {
	case PA_IO_NORMAL:
	case PA_IO_DEFOUT:
		if (type == PA_TYPE_DATA ||
		    type == PA_TYPE_PTR_VIRT ||
		    type == PA_TYPE_PTR_PHYS)
			*size = entry->size;
		else if (type == PA_TYPE_BUF_PTR ||
				       type == PA_TYPE_BUF_LEN)
			*size = sizeof(int);
		break;
	case PA_IO_RESERVED:
		if (type != PA_TYPE_DATA &&
				  type != PA_TYPE_BUF_PTR)
			goto invalid_entry;
		break;
	case PA_IO_ONLY:
		if (type == PA_TYPE_BUF_PTR)
			*size = sizeof(int);
		else if (type != PA_TYPE_DATA)
			goto invalid_entry;
		else
			*size = entry->size;
		break;
	case PA_IO_SIZE_VAL:
		if (type != PA_TYPE_PTR_VIRT &&
		    type != PA_TYPE_PTR_PHYS &&
				  type != PA_TYPE_CRED &&
				  type != PA_TYPE_FOREIGN_CRED &&
				  type != PA_TYPE_DATA)
			goto invalid_entry;
		if (p == c->par && n <= entry->value) {
			pr_debug("invalid parameter reference %u\n",
				entry->value);
			return -EINVAL;
		}
		if (entry_input_value(c, entry->value, input, cinp, size) < 0)
			return -EINVAL;
		pr_debug("PA_IO_SIZE_VAL: save [%d] size: %d\n", n, *size);
		entry->size = *size; /* save entry size */
		break;
	case PA_IO_SIZE_PAR:
		if (type != PA_TYPE_PTR_VIRT &&
		    type != PA_TYPE_PTR_PHYS)
			goto invalid_entry;
		if (c->cmd->npar < entry->value) {
			pr_debug("invalid parameter reference %u\n",
				entry->value);
			return -EINVAL;
		}
		if (entry_io_size(c->par, entry->value, c, input, cinp,
				size) < 0)
			return -EINVAL;
		pr_debug("PA_IO_SIZE_PAR: save [%d] size: %d\n", n, *size);
		entry->size = *size; /* save entry size */
		break;
	default:
		goto invalid_entry;
	}
	return 0;
invalid_entry:
	pr_debug("invalid type %u for ref %u\n", PA_TYPE(entry->type),
	       PA_IO(entry->type));
	return -EINVAL;
}

/*
 This function calculates how many bytes of input parameters comes from user
 space and how many byte will be read by user space as results
 */
static int command_io_size(struct pa_command *c, u8 *buf, size_t bufsize,
			   size_t *mininp, size_t *maxout)
{
	int i;
	size_t size, mini = 0, maxo = 0;

	for (i = 0; i < c->cmd->npar; i++) {
		if (entry_io_size(c->par, i, c, buf, bufsize, &size) < 0)
			return -EINVAL;
		pr_debug("par [%d] io_size: %d\n", i, size);
		if (!PA_DIR(c->par[i].type) ||
				   (PA_DIR(c->par[i].type) & PA_DIR_IN))
			mini += size;
		if (PA_DIR(c->par[i].type) & PA_DIR_OUT)
			maxo += size;
	}

	for (i = 0; i < c->cmd->nres; i++) {
		if (entry_io_size(c->res, i, c, buf, bufsize, &size) < 0)
			return -EINVAL;
		pr_debug("res [%d] io_size: %d\n", i, size);
		if (!PA_DIR(c->par[i].type) ||
				   (PA_DIR(c->par[i].type) & PA_DIR_IN))
			maxo += size;
	}

	*mininp = mini;
	*maxout = maxo;

	pr_debug("io_size: cpar: %d, cres: %d\n", mini, maxo);

	return 0;
}

static struct pa_command *command_find(u32 cmd, struct pa_format *p)
{
	int i;

	for (i = 0; i < p->ncmd; i++)
		if (p->cmd[i].cmd->cmd == cmd)
			return &p->cmd[i];

	return NULL;
}

/*
 * pa_command_query - find command description from format binary
 * and calculate parameter and results buffer requirements
 */
int pa_command_query(u32 cmd, u8 *input, size_t cinp, size_t *mininp,
		     size_t *maxout, struct pa_format *p)
{
	struct pa_command *c = command_find(cmd, p);

	if (!c)
		return -EINVAL;

	return command_io_size(c, input, cinp, mininp, maxout);
}

static size_t entry_size(struct pa_format_entry *entry)
{
	u32 type = PA_TYPE(entry->type);

	if (PA_IO(entry->type) == PA_IO_ONLY)
		return 0;

	if (type == PA_TYPE_CRED || type == PA_TYPE_FOREIGN_CRED)
		return sizeof(pa_cred_t);

	if (type == PA_TYPE_DATA || type == PA_TYPE_BUF_LEN)
		return entry->size;

	/* all rest are normally pointers */
	return sizeof(void *);
}

/*
 * calculate command size to be passed to secenv
 */
static void command_size(struct pa_command *c, size_t *cpar, size_t *cres)
{
	int i;

	*cpar = 0;
	*cres = 0;

	for (i = 0; i < c->cmd->npar; i++)
		*cpar += entry_size(&c->par[i]);

	for (i = 0; i < c->cmd->nres; i++)
		*cres += entry_size(&c->res[i]);
}

static int prepare_par_data(struct pa_command_data *p, int n, u8 **ppos,
			    u8 *pend, u8 **ipos, u8 *iend)
{
	struct pa_format_entry *entry = &p->c->par[n];

	if (PA_IO(entry->type) == PA_IO_ONLY)
		return buffer_skip(ipos, iend, entry->size);

	if (PA_IO(entry->type) == PA_IO_RESERVED)
		return buffer_copy(ppos, pend, &entry->value, entry->size);

	return buffer_move(ppos, pend, ipos, iend, entry->size);
}

static int prepare_par_buf(struct pa_command_data *p, int n, u8 **ppos,
			    u8 *pend, u8 **ipos, u8 *iend)
{
	struct pa_format_entry *entry = &p->c->par[n];
	int	len;
	struct sec_obuf	*obuf;
	dma_addr_t	ptr;

	if (PA_IO(entry->type) == PA_IO_RESERVED) {
		if (entry->value >= p->cpar)
			return -EINVAL;
		obuf = (struct sec_obuf *)p->c->par[entry->value].value;
	} else {
		int	id;
		if (buffer_copy_source((u8 *)&id, ipos, iend, sizeof(id)) < 0)
			return -EINVAL;
		obuf = sec_obuf_get_id(id);
		if (obuf)
			p->resource++;
		/*
		  save buffer pointer for later use
		  buffer id can be also saved instead of pointer
		*/
		WARN(sizeof(entry->value) != sizeof(obuf),
		     "entry value size is incorrect\n");
		entry->value = (u32)obuf;
	}

	if (!obuf)
		return -EINVAL;

	if (PA_TYPE(entry->type) == PA_TYPE_BUF_LEN) {
		len = sec_obuf_len(obuf);
		pr_debug("buf id: %d, len: %d\n", obuf->id, len);
		return buffer_copy(ppos, pend, &len, entry->size);
	}

	if (PA_IO(entry->type) == PA_IO_ONLY)
		return 0;

	ptr = p->vtp(obuf->data);
	pr_debug("buf id: %d, ptr: %x\n", obuf->id, ptr);
	return buffer_copy(ppos, pend, &ptr, sizeof(ptr));
}

#ifdef CONFIG_SECURITY_AEGIS_RESTOK
static inline int aegis_get_cred(const char *str, int foreign, pa_cred_t *cred)
{
	gid_t gid;
	struct crypto_shash *tfm;
	u8	sha1[20];
	int	rv;

	gid = restok_locate(str);
	if (gid <= 0)
		return -EPERM;

	if (!foreign && !in_egroup_p(gid))
		return -EPERM;

	tfm = crypto_alloc_shash("sha1", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("tfm allocation failed\n");
		return PTR_ERR(tfm);
	} else {
		struct {
			struct shash_desc shash;
			char ctx[crypto_shash_descsize(tfm)];
		} desc;

		desc.shash.tfm = tfm;
		desc.shash.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

		rv = crypto_shash_digest(&desc.shash, str, strlen(str), sha1);
	}
	crypto_free_shash(tfm);
	memcpy(cred, sha1, sizeof(*cred));
	pr_debug("rv: %d\n", rv);
	return rv;
}
#else
static inline int aegis_get_cred(const char *str, int foreign, pa_cred_t *cred)
{
	*cred = 0;
	return 0;
}
#endif

static int prepare_par_cred(struct pa_command_data *p, int n, u8 **ppos,
			    u8 *pend, u8 **ipos, u8 *iend)
{
	struct pa_format_entry *entry = &p->c->par[n];
	pa_cred_t	cred;
	int	rv;

	if (entry->size == 0)
		return -EINVAL;

	/* PA_IO(entry->type) == PA_IO_SIZE_VAL */
	pr_debug("cred, len: %d: %s\n", entry->size, *ipos);
	(*ipos)[entry->size - 1] = '\0'; /* sanity check */

	rv = aegis_get_cred(*ipos, PA_TYPE(entry->type) == PA_TYPE_FOREIGN_CRED,
			     &cred);
	if (rv)
		return rv;

	if (buffer_skip(ipos, iend, entry->size) < 0 ||
		buffer_copy(ppos, pend, &cred, sizeof(pa_cred_t)) < 0)
		return -EINVAL;

	return 0;
}

static int prepare_entry_ptr(struct pa_command_data *p,
			     struct pa_format_entry *entry,
			     int n, u8 **epos, u8 *eend, u8 **spos, u8 *send)
{
	size_t size = entry[n].size;
	u8 *buf;

	if (entry == p->c->par && (PA_DIR(entry[n].type) & PA_DIR_OUT)) {
		if (PA_DIR(entry[n].type) & PA_DIR_IN) {
			/* copy input data to output buffer */
			if (buffer_copy_source(p->opos, spos, send, size) < 0)
				return -EINVAL;
		}
		spos = &p->opos;
		send = p->oend;
	}

	buf = *spos;

	/* Pointers passed to secure mode must be word-aligned */
	if (!IS_ALIGNED((unsigned long)buf, sizeof(buf))) {
		pr_debug("NOT ALIGNED\n");
		if (buffer_skip(&buf, send, size) < 0)
			return -EINVAL;
		/*
		   kfree is in finish_entry_ptr() and called also in error cases
		   see pa_command_prepare() error handling
		*/
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		p->resource++;
		memcpy(buf, *spos, size);
	}

	if (PA_TYPE(entry[n].type) == PA_TYPE_PTR_PHYS) {
		buf = (u8 *)p->vtp(buf);
		pr_debug("phys: %p\n", buf);
	}

	if (buffer_copy(epos, eend, &buf, sizeof(buf)) < 0 ||
	    buffer_skip(spos, send, size) < 0)
		return -EINVAL;

	return 0;
}

static int command_param_count(struct pa_command_data *p)
{
	int i, count = 0;

	for (i = 0; i < p->c->cmd->npar; i++) {
		if (PA_IO(p->c->par[i].type) != PA_IO_ONLY)
			count++;
	}
	return count;
}

/*
 * prepare_par - prepare parameter buffer for PA command
 *
 * goes through PA command formatting data and interpret driver input buffer
 * for each input parameter setups corresponding parameter for secenv
 * parameter
 */
static int prepare_par(struct pa_command_data *p)
{
	int rv = 0;
	int i;
	u8 *ipos = p->input;
	u8 *iend = p->input + p->cinp;
	u8 *ppos = p->par + (p->hal ? sizeof(struct hal_par_common) :
			sizeof(struct pa_par_common));
	u8 *pend = ppos + p->cpar;

	p->opos = p->output;
	p->oend = p->output + p->coup;

	if (p->hal) {
		struct hal_par_common *sp = (struct hal_par_common *) p->par;
		sp->npar  = command_param_count(p);
		pr_debug("number of params: %d\n", sp->npar);
	} else {
		struct pa_par_common *sp = (struct pa_par_common *) p->par;
		sp->length = p->cpar + sizeof(*sp);
		sp->index  = p->c->cmd->index;
	}
	pr_debug("cpar: %d\n", p->cpar);

	for (i = 0; i < p->c->cmd->npar && !rv; i++) {
		pr_debug("[%d] type: %d\n", i, PA_TYPE(p->c->par[i].type));
		switch (PA_TYPE(p->c->par[i].type)) {
		case PA_TYPE_DATA:
			rv = prepare_par_data(p, i, &ppos, pend, &ipos, iend);
			break;
		case PA_TYPE_PTR_VIRT:
		case PA_TYPE_PTR_PHYS:
			rv = prepare_entry_ptr(p, p->c->par, i, &ppos, pend,
				&ipos, iend);
			break;
		case PA_TYPE_PTR_PAPUB: {
			dma_addr_t papub;
			if (!p->papub)
				return -EINVAL;
			papub = p->vtp(p->papub);
			rv = buffer_copy(&ppos, pend, &papub, sizeof(papub));
			}
			break;
		case PA_TYPE_BUF_LEN:
		case PA_TYPE_BUF_PTR:
			rv = prepare_par_buf(p, i, &ppos, pend, &ipos, iend);
			break;
		case PA_TYPE_FOREIGN_CRED:
		case PA_TYPE_CRED:
			rv = prepare_par_cred(p, i, &ppos, pend, &ipos, iend);
			break;
		default:
			pr_debug("unsupported type %u\n",
				 PA_TYPE(p->c->par[i].type));
			return -EINVAL;
		}
		if (!rv)
			p->c->par[i].type |= PA_FLAG_PREP;
	}

	return rv;
}

static int prepare_res_data(struct pa_command_data *p, int n, u8 **rpos,
			    u8 *rend, u8 **opos, u8 *oend)
{
	struct pa_format_entry *entry = &p->c->res[n];

	if (PA_IO(entry->type) == PA_IO_RESERVED)
		return buffer_copy(rpos, rend, &entry->value, entry->size);

	if (PA_IO(entry->type) == PA_IO_DEFOUT) {
		if (buffer_skip(opos, oend, entry->size) < 0 ||
		    buffer_copy(rpos, rend, &entry->value, entry->size) < 0)
			return -EINVAL;

		return 0;
	}

	if (buffer_skip(opos, oend, entry->size) < 0 ||
	    buffer_skip(rpos, rend, entry->size) < 0)
		return -EINVAL;

	return 0;
}

static int prepare_res_buf(struct pa_command_data *p, int n, u8 **rpos,
			    u8 *rend, u8 **opos, u8 *oend)
{
	struct pa_format_entry *entry = &p->c->res[n];
	struct pa_format_entry *e;
	struct sec_obuf	*obuf;
	dma_addr_t ptr;

	if (entry->value >= p->cpar)
		return -EINVAL;
	e = &p->c->par[entry->value];

	obuf = (struct sec_obuf *)e->value;
	if (!obuf)
		return -EINVAL; /* should not happen*/

	ptr = p->vtp(obuf->data);
	pr_debug("id: %d, ptr: %x\n", obuf->id, ptr);
	return buffer_copy(rpos, rend, &ptr, sizeof(ptr));
}

static int prepare_res(struct pa_command_data *p)
{
	int rv = 0;
	int i;
	u8 *opos = p->output;
	u8 *oend = p->output + p->coup;
	u8 *rpos;
	u8 *rend;

	if (p->hal)
		rpos = p->par + sizeof(struct hal_par_common) + p->cpar;
	else
		rpos = p->res + sizeof(struct pa_res_common);

	rend = rpos + p->cres;

	pr_debug("cres: %d\n", p->cres);

	for (i = 0; i < p->c->cmd->nres && !rv; i++) {
		pr_debug("[%d] type: %d\n", i, PA_TYPE(p->c->res[i].type));
		switch (PA_TYPE(p->c->res[i].type)) {
		case PA_TYPE_DATA:
			rv = prepare_res_data(p, i, &rpos, rend, &opos, oend);
			break;
		case PA_TYPE_PTR_VIRT:
		case PA_TYPE_PTR_PHYS:
			rv = prepare_entry_ptr(p, p->c->res, i, &rpos, rend,
				&opos, oend);
			break;
		case PA_TYPE_BUF_PTR:
			rv = prepare_res_buf(p, i, &rpos, rend, &opos, oend);
			break;
		default:
			pr_debug("unsupported type %u\n",
				 PA_TYPE(p->c->res[i].type));
			return -EINVAL;
		}
		if (!rv)
			p->c->res[i].type |= PA_FLAG_PREP;
	}

	return rv;
}

static int finish_par_data(struct pa_command_data *p, int n, u8 **ppos,
			   u8 *pend, u8 **ipos, u8 *iend)
{
	struct pa_format_entry *entry = &p->c->par[n];

	if (PA_IO(entry->type) == PA_IO_ONLY)
		return buffer_skip(ipos, iend, entry->size);

	if (PA_IO(entry->type) == PA_IO_RESERVED)
		return buffer_skip(ppos, pend, entry->size);

	if  (buffer_skip(ppos, pend, entry->size) < 0 ||
	     buffer_skip(ipos, iend, entry->size) < 0)
		return -EINVAL;

	return 0;
}

static int finish_par_buf(struct pa_command_data *p, int n, u8 **ppos,
			   u8 *pend, u8 **ipos, u8 *iend)
{
	struct pa_format_entry *entry = &p->c->par[n];

	if (PA_IO(entry->type) != PA_IO_RESERVED && entry->value) {
		p->resource--;
		sec_obuf_put((struct sec_obuf *)entry->value);
	}

	if (PA_IO(entry->type) == PA_IO_ONLY)
		return buffer_skip(ipos, iend, sizeof(int));

	if (PA_IO(entry->type) == PA_IO_RESERVED)
		return buffer_skip(ppos, pend, entry->size);

	if (buffer_skip(ppos, pend, entry->size) < 0 ||
	     buffer_skip(ipos, iend, sizeof(int)) < 0)
		return -EINVAL;

	return 0;
}

static int finish_par_cred(struct pa_command_data *p, int n, u8 **ppos,
			   u8 *pend, u8 **ipos, u8 *iend)
{
	struct pa_format_entry *entry = &p->c->par[n];

	if (buffer_skip(ipos, iend, entry->size) < 0 ||
		   buffer_skip(ppos, pend, sizeof(pa_cred_t)) < 0)
		return -EINVAL;

	return 0;
}


static int finish_entry_ptr(struct pa_command_data *p,
			    struct pa_format_entry *entry, int n,
			    u8 **epos, u8 *eend, u8 **spos, u8 *send, int copy)
{
	size_t size = entry[n].size;
	u8 *buf = *(u8 **) *epos;

	if (entry == p->c->par && (PA_DIR(entry[n].type) & PA_DIR_OUT)) {
		if (PA_DIR(entry[n].type) & PA_DIR_IN) {
			/* data copied to output buffer */
			if (buffer_skip(spos, send, size) < 0)
				return -EINVAL;
		}
		spos = &p->opos;
		send = p->oend;
	}

	if (buf) {
		if (PA_TYPE(entry[n].type) == PA_TYPE_PTR_PHYS)
			buf = p->ptv((dma_addr_t)buf);

		if (buf != *spos) {
			if (copy)
				memcpy(*spos, buf, size);
			/* allocated in prepare_entry_ptr() */
			p->resource--;
			kfree(buf);
		}
	}

	if (buffer_skip(epos, eend, sizeof(buf)) < 0 ||
	    buffer_skip(spos, send, size) < 0)
		return -EINVAL;

	return 0;
}

static int finish_par(struct pa_command_data *p)
{
	int rv = 0;
	int i;
	u8 *ipos = p->input;
	u8 *iend = p->input + p->cinp;
	u8 *ppos = p->par + (p->hal ? sizeof(struct hal_par_common) :
			sizeof(struct pa_par_common));
	u8 *pend = ppos + p->cpar;

	p->opos = p->output;
	p->oend = p->output + p->coup;

	pr_debug("cpar: %d\n", p->cpar);

	for (i = 0; i < p->c->cmd->npar && !rv; i++) {
		pr_debug("[%d] type: %d\n", i, PA_TYPE(p->c->par[i].type));
		if (!(p->c->par[i].type & PA_FLAG_PREP))
			continue;
		switch (PA_TYPE(p->c->par[i].type)) {
		case PA_TYPE_DATA:
			rv = finish_par_data(p, i, &ppos, pend, &ipos, iend);
			break;
		case PA_TYPE_PTR_VIRT:
		case PA_TYPE_PTR_PHYS:
			rv = finish_entry_ptr(p, p->c->par, i, &ppos, pend,
				&ipos, iend, 0);
			break;
		case PA_TYPE_PTR_PAPUB:
			rv = buffer_skip(&ppos, pend, sizeof(u8 *));
			break;
		case PA_TYPE_BUF_LEN:
		case PA_TYPE_BUF_PTR:
			rv = finish_par_buf(p, i, &ppos, pend, &ipos, iend);
			break;
		case PA_TYPE_FOREIGN_CRED:
		case PA_TYPE_CRED:
			rv = finish_par_cred(p, i, &ppos, pend, &ipos, iend);
			break;
		default:
			pr_debug("unsupported type %u\n",
				 PA_TYPE(p->c->par[i].type));
			return -EINVAL;
		}
		p->c->par[i].type &= ~PA_FLAG_PREP;
	}

	return rv;
}

static int finish_res_data(struct pa_command_data *p, int n, u8 **rpos,
			   u8 *rend, u8 **opos, u8 *oend)
{
	struct pa_format_entry *entry = &p->c->res[n];

	if (PA_IO(entry->type) == PA_IO_RESERVED)
		return buffer_skip(rpos, rend, entry->size);

	return buffer_move(opos, oend, rpos, rend, entry->size);
}

static int finish_res(struct pa_command_data *p)
{
	int rv = 0;
	int i;
	u8 *opos = p->output;
	u8 *oend = p->output + p->coup;
	u8 *rpos;
	u8 *rend;

	if (p->hal)
		rpos = p->par + sizeof(struct hal_par_common) + p->cpar;
	else
		rpos = p->res + sizeof(struct pa_res_common);

	rend = rpos + p->cres;

	pr_debug("cres: %d\n", p->cres);

	for (i = 0; i < p->c->cmd->nres && !rv; i++) {
		pr_debug("[%d] type: %d\n", i, PA_TYPE(p->c->res[i].type));
		if (!(p->c->res[i].type & PA_FLAG_PREP))
			continue;
		switch (PA_TYPE(p->c->res[i].type)) {
		case PA_TYPE_DATA:
			rv = finish_res_data(p, i, &rpos, rend, &opos, oend);
			break;
		case PA_TYPE_PTR_VIRT:
		case PA_TYPE_PTR_PHYS:
			rv = finish_entry_ptr(p, p->c->res, i, &rpos, rend,
				&opos, oend, 1);
			break;
		case PA_TYPE_BUF_PTR:
			rv = buffer_skip(&rpos, rend, sizeof(int));
			break;
		default:
			pr_debug("unsupported type %u\n",
				 PA_TYPE(p->c->res[i].type));
			return -EINVAL;
		}
		p->c->res[i].type &= ~PA_FLAG_PREP;
	}

	return rv;
}

static int pa_command_alloc(struct pa_command_data *p)
{
	if (p->hal) {
		p->par = kzalloc(sizeof(struct hal_par_common) +
				p->cpar + p->cres, GFP_KERNEL);
		p->res = NULL;
		if (!p->par)
			return -ENOMEM;
	} else {
		p->par = kzalloc(p->cpar + sizeof(struct pa_par_common),
				 GFP_KERNEL);
		p->res = kzalloc(p->cres + sizeof(struct pa_res_common),
				 GFP_KERNEL);
		if (!p->par || !p->res)
			return -ENOMEM;
	}
	return 0;
}

static void pa_command_free(struct pa_command_data *p)
{
	kfree(p->res);
	kfree(p->par);
}

/*
 * pa_command_creds_check - check the credentials of the current task
 *
 * checks if the current task has the needed credentials to call
 * the given PA service.
 *
 * Returns 1 if current task has the needed credentials
 * Returns 0 if current task does not have the needed credentials
 * Returns <0 in case of error.
 */
int pa_command_creds_check(struct pa_format_command *cmd)
{
	u32 i = 0;
	long creds_type = 0;
	long creds_value = 0;

	if (!cmd->creds_count) {
		pr_debug("no tokens needed for this command");
		return 0;
	} else if (cmd->creds_count > PA_CREDS_COUNT_MAX) {
		pr_debug("incorrect creds count");
		return -EINVAL;
	}

#ifdef CONFIG_SECURITY_AEGIS_CREDS
	for (i = 0; i < cmd->creds_count; i++) {
		cmd->creds[i][PA_CREDS_LEN_MAX - 1] = '\0';
		creds_type = creds_kstr2creds(cmd->creds[i], &creds_value);
		if (creds_type == -1) {
			pr_debug("invalid credential string %s\n",
				 cmd->creds[i]);
			return -EPERM;
		}

		if (creds_khave_p(creds_type, creds_value) == 0)
			return -EPERM;
	}
#endif

	return 0;
}

/*
 * pa_command_prepare - prepare parameters and results
 *
 * parameters passed to the driver from user space is in different format
 * than it is needed for secure environment. Also command results will be
 * in different format than it is passed back to user space
 * see Documentation/arm/OMAP/omap_sec.txt for details of command data types
 */
int pa_command_prepare(u32 cmd, struct pa_command_data *p)
{
	int rv = -ENOMEM;

	pr_debug("enter:\n");
	p->c = command_find(cmd, p->format);
	if (!p->c)
		return -EINVAL;

	rv = pa_command_creds_check(p->c->cmd);
	if (rv)
		return rv;

	command_size(p->c, &p->cpar, &p->cres);
	pr_debug("command_size: cpar: %d, cres: %d\n", p->cpar, p->cres);

	if (pa_command_alloc(p))
		return -ENOMEM;

	rv = prepare_par(p);
	if (rv < 0)
		goto err;

	rv = prepare_res(p);
	if (rv < 0)
		goto err;

	return 0;
err:
	/*
	   we need to call "finish" functions even if "prepare" failed
	   in oder to kfree allocated alligned buffers before "prepare" failed
	*/
	finish_par(p);
	finish_res(p);
	pa_command_free(p);
	return rv;
}

/*
 * pa_command_finish - release command parameters and results
 *
 * copy command results to driver output buffers and deallocate all buffers
 */
int pa_command_finish(struct pa_command_data *p)
{
	int rv = 0, err;

	err = finish_par(p);
	if (err < 0)
		rv = err;
	err = finish_res(p);
	if (err < 0)
		rv = err;

	pa_command_free(p);
	return rv;
}

int pa_image_version_get(const struct pa_image_toc_entry *pa_base,
			 const struct pa_format *fmt,
				u32 *pa_ver, u32 *fmt_ver)
{
	const u8 *ptr;

	if (!pa_base || !fmt)
		return -EINVAL;

	ptr = (const u8 *)pa_base + pa_base->start + PA_IMAGE_VERSION_OFFSET;

	*pa_ver = *(const u32 *)ptr;
	*fmt_ver = fmt->version;

	return 0;
}

