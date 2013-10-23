/*
 * dload_internal.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */



#ifndef __DLOAD_INTERNAL__
#define __DLOAD_INTERNAL__

#include <linux/types.h>

/*
 * Internal state definitions for the dynamic loader
 */

#define TRUE 1
#define FALSE 0
typedef int boolean;


/* type used for relocation intermediate results */
typedef s32 RVALUE;

/* unsigned version of same; must have at least as many bits */
typedef u32 URVALUE;

/*
 * Dynamic loader configuration constants
 */
/* error issued if input has more sections than this limit */
#define REASONABLE_SECTION_LIMIT 100

/* (Addressable unit) value used to clear BSS section */
#define dload_fill_bss 0

/*
 * Reorder maps explained (?)
 *
 * The doff file format defines a 32-bit pattern used to determine the
 * byte order of an image being read.  That value is
 * BYTE_RESHUFFLE_VALUE == 0x00010203
 * For purposes of the reorder routine, we would rather have the all-is-OK
 * for 32-bits pattern be 0x03020100.  This first macro makes the
 * translation from doff file header value to MAP value: */
#define REORDER_MAP(rawmap) ((rawmap) ^ 0x3030303)
/* This translation is made in dload_headers.  Thereafter, the all-is-OK
 * value for the maps stored in dlthis is REORDER_MAP(BYTE_RESHUFFLE_VALUE).
 * But sadly, not all bits of the doff file are 32-bit integers.
 * The notable exceptions are strings and image bits.
 * Strings obey host byte order: */
#if defined(_BIG_ENDIAN)
#define HOST_BYTE_ORDER(cookedmap) ((cookedmap) ^ 0x3030303)
#else
#define HOST_BYTE_ORDER(cookedmap) (cookedmap)
#endif
/* Target bits consist of target AUs (could be bytes, or 16-bits,
 * or 32-bits) stored as an array in host order.  A target order
 * map is defined by: */
#if !defined(_BIG_ENDIAN) || TARGET_AU_BITS > 16
#define TARGET_ORDER(cookedmap) (cookedmap)
#elif TARGET_AU_BITS > 8
#define TARGET_ORDER(cookedmap) ((cookedmap) ^ 0x2020202)
#else
#define TARGET_ORDER(cookedmap) ((cookedmap) ^ 0x3030303)
#endif

/* forward declaration for handle returned by dynamic loader */
struct my_handle;

/*
 * a list of module handles, which mirrors the debug list on the target
 */
struct dbg_mirror_root {
	/* must be same as dbg_mirror_list; __DLModules address on target */
	u32 dbthis;
	struct my_handle *hnext;	/* must be same as dbg_mirror_list */
	u16 changes;	/* change counter */
	u16 refcount;	/* number of modules referencing this root */
} ;

struct dbg_mirror_list {
	u32 dbthis;
	struct my_handle *hnext, *hprev;
	struct dbg_mirror_root *hroot;
	u16 dbsiz;
	u32 context;	/* Save context for .dllview memory allocation */
} ;

#define VARIABLE_SIZE 1
/*
 * the structure we actually return as an opaque module handle
 */
struct my_handle {
	struct dbg_mirror_list dm;	/* !!! must be first !!! */
	/* sections following << 1, LSB is set for big-endian target */
	u16 secn_count;
	struct LDR_SECTION_INFO secns[VARIABLE_SIZE];
} ;
#define MY_HANDLE_SIZE (sizeof(struct my_handle) -\
			sizeof(struct LDR_SECTION_INFO))
/* real size of my_handle */

/*
 * reduced symbol structure used for symbols during relocation
 */
struct Local_Symbol {
	s32 value;	/* Relocated symbol value */
	s32 delta;	/* Original value in input file */
	s16 secnn;		/* section number */
	s16 sclass;		/* symbol class */
} ;

/*
 * States of the .cinit state machine
 */
enum cinit_mode {
	CI_count = 0,		/* expecting a count */
	CI_address,		/* expecting an address */
#if CINIT_ALIGN < CINIT_ADDRESS	/* handle case of partial address field */
	CI_partaddress,		/* have only part of the address */
#endif
	CI_copy,		/* in the middle of copying data */
	CI_done			/* end of .cinit table */
};

/*
 * The internal state of the dynamic loader, which is passed around as
 * an object
 */
struct dload_state {
	struct Dynamic_Loader_Stream *strm;	/* The module input stream */
	struct Dynamic_Loader_Sym *mysym;	/* Symbols for this session */
	struct Dynamic_Loader_Allocate *myalloc; /* target memory allocator */
	struct Dynamic_Loader_Initialize *myio;	/* target memory initializer */
	unsigned myoptions;	/* Options parameter Dynamic_Load_Module */

	char *str_head;		/* Pointer to string table */
#if BITS_PER_AU > BITS_PER_BYTE
	char *str_temp;		/* Pointer to temporary buffer for strings */
	/* big enough to hold longest string */
	unsigned temp_len;	/* length of last temporary string */
	char *xstrings;		/* Pointer to buffer for expanded */
	/* strings for sec names */
#endif
	/* Total size of strings for DLLView section names */
	unsigned debug_string_size;
	/* Pointer to parallel section info for allocated sections only */
	struct doff_scnhdr_t *sect_hdrs;	/* Pointer to section table */
	struct LDR_SECTION_INFO *ldr_sections;
#if TMS32060
	/* The address of the start of the .bss section */
	LDR_ADDR bss_run_base;
#endif
	struct Local_Symbol *local_symtab;	/* Relocation symbol table */

	/* pointer to DL section info for the section being relocated */
	struct LDR_SECTION_INFO *image_secn;
	/* change in run address for current section during relocation */
	LDR_ADDR delta_runaddr;
	LDR_ADDR image_offset;	/* offset of current packet in section */
	enum cinit_mode cinit_state;	/* current state of cload_cinit() */
	int cinit_count;	/* the current count */
	LDR_ADDR cinit_addr;	/* the current address */
	s16 cinit_page;	/* the current page */
	/* Handle to be returned by Dynamic_Load_Module */
	struct my_handle *myhandle;
	unsigned dload_errcount;	/* Total # of errors reported so far */
	/* Number of target sections that require allocation and relocation */
	unsigned allocated_secn_count;
#ifndef TARGET_ENDIANNESS
	boolean big_e_target;	/* Target data in big-endian format */
#endif
	/* map for reordering bytes, 0 if not needed */
	u32 reorder_map;
	struct doff_filehdr_t dfile_hdr;	/* DOFF file header structure */
	struct doff_verify_rec_t verify;	/* Verify record */

	int relstkidx;		/* index into relocation value stack */
	/* relocation value stack used in relexp.c */
	RVALUE relstk[STATIC_EXPR_STK_SIZE];

} ;

#ifdef TARGET_ENDIANNESS
#define TARGET_BIG_ENDIAN TARGET_ENDIANNESS
#else
#define TARGET_BIG_ENDIAN (dlthis->big_e_target)
#endif

/*
 * Exports from cload.c to rest of the world
 */
extern void dload_error(struct dload_state *dlthis, const char *errtxt, ...);
extern void dload_syms_error(struct Dynamic_Loader_Sym *syms,
			     const char *errtxt, ...);
extern void dload_headers(struct dload_state *dlthis);
extern void dload_strings(struct dload_state *dlthis, boolean sec_names_only);
extern void dload_sections(struct dload_state *dlthis);
extern void dload_reorder(void *data, int dsiz, u32 map);
extern u32 dload_checksum(void *data, unsigned siz);

#if HOST_ENDIANNESS
extern uint32_t dload_reverse_checksum(void *data, unsigned siz);
#if (TARGET_AU_BITS > 8) && (TARGET_AU_BITS < 32)
extern uint32_t dload_reverse_checksum_16(void *data, unsigned siz);
#endif
#endif

#define is_data_scn(zzz) (DLOAD_SECTION_TYPE((zzz)->type) != DLOAD_TEXT)
#define is_data_scn_num(zzz) \
		(DLOAD_SECT_TYPE(&dlthis->sect_hdrs[(zzz)-1]) != DLOAD_TEXT)

/*
 * exported by reloc.c
 */
extern void dload_relocate(struct dload_state *dlthis, TgtAU_t *data,
			   struct reloc_record_t *rp);

extern RVALUE dload_unpack(struct dload_state *dlthis, TgtAU_t *data,
			   int fieldsz, int offset, unsigned sgn);

extern int dload_repack(struct dload_state *dlthis, RVALUE val, TgtAU_t *data,
			int fieldsz, int offset, unsigned sgn);

#endif				/* __DLOAD_INTERNAL__ */
