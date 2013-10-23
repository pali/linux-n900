/*
 * regsup.h
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

#ifndef _REGSUP_H_
#define _REGSUP_H_

/*  Init function. MUST be called BEFORE any calls are */
/*  made into this psuedo-registry!!!  Returns TRUE/FALSE for SUCCESS/ERROR */
extern bool regsup_init(void);

/*  Release all registry support allocations. */
extern void regsup_exit(void);

/*
 *  ======== regsup_delete_value ========
 */
extern dsp_status regsup_delete_value(IN CONST char *pstrValue);

/*  Get the value of the entry having the given name.  Returns DSP_SOK */
/*  if an entry was found and the value retrieved.  Returns DSP_EFAIL
 *  otherwise. */
extern dsp_status regsup_get_value(char *valName, void *pbuf, u32 * data_size);

/*  Sets the value of the entry having the given name.  Returns DSP_SOK */
/*  if an entry was found and the value set.  Returns DSP_EFAIL otherwise. */
extern dsp_status regsup_set_value(char *valName, void *pbuf, u32 data_size);

/*  Returns registry "values" and their "data" under a (sub)key. */
extern dsp_status regsup_enum_value(IN u32 dw_index, IN CONST char *pstrKey,
				    IN OUT char *pstrValue,
				    IN OUT u32 *pdwValueSize,
				    IN OUT char *pstrData,
				    IN OUT u32 *pdwDataSize);

#endif
