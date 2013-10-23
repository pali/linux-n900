/*
 * reg.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provides registry functions.
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

#ifndef _REG_H
#define _REG_H

#include <linux/types.h>

#define REG_MAXREGPATHLENGTH    255

/*
 *  ======== reg_delete_value ========
 *  Purpose:
 *      Deletes a registry entry. NOTE: A registry entry is not the same as
 *      a registry key.
 *  Parameters:
 *      pstrValue:  Name of entry to delete.
 *  Returns:
 *      DSP_SOK:    Success.
 *      DSP_EFAIL:  General failure.
 *  Requires:
 *      - REG initialized.
 *      - pstrValue is non-NULL value.
 *      - length of pstrValue < REG_MAXREGPATHLENGTH.
 *  Ensures:
 *  Details:
 */
extern dsp_status reg_delete_value(IN CONST char *pstrValue);

/*
 *  ======== reg_enum_value ========
 *  Purpose:
 *      Enumerates values of a specified key. Retrieves each value name and
 *      the data associated with the value.
 *  Parameters:
 *      dw_index:        Specifies the index of the value to retrieve.
 *      pstrKey:        The name of the registry key to be enumerated.
 *      pstrValue:      Pointer to buffer that receives the name of the value.
 *      pdwValueSize:   Specifies bytes of memory pstrValue points to on input,
 *                      On output, specifies actual memory bytes written into.
 *                      If there is no value, pdwValueSize returns NULL
 *      pstrData:       Pointer to buffer that receives the data of a value.
 *      pdwDataSize:    Specifies bytes of memory in pstrData on input and
 *                      bytes of memory written into pstrData on output.
 *                      If there is no data, pdwDataSize returns NULL.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      REG initialized.
 *      pstrKey is a non-NULL value.
 *      pstrValue, pstrData, pdwValueSize and pdwDataSize are valid pointers.
 *      Length of pstrKey is less than REG_MAXREGPATHLENGTH.
 *  Ensures:
 */
extern dsp_status reg_enum_value(IN u32 dw_index, IN CONST char *pstrKey,
				 IN OUT char *pstrValue,
				 IN OUT u32 *pdwValueSize,
				 IN OUT char *pstrData,
				 IN OUT u32 *pdwDataSize);

/*
 *  ======== reg_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      REG initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
extern void reg_exit(void);

/*
 *  ======== reg_get_value ========
 *  Purpose:
 *      Retrieve a value from the registry.
 *  Parameters:
 *      pstrEntry:      Name of entry to retrieve.
 *      pbValue:        Upon return, points to retrieved value.
 *      pdwValueSize:   Specifies bytes of memory pbValue points to on input,
 *                      on output, specifies actual memory bytes written into.
 *                      If pbValue is NULL, pdwValueSize reports the size of
 *                      the entry in pstrEntry.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      - REG initialized.
 *      - pstrEntry is non-NULL value.
 *      - pbValue is a valid pointer.
 *      - length of pstrEntry < REG_MAXREGPATHLENGTH.
 *  Ensures:
 */
extern dsp_status reg_get_value(IN CONST char *pstrEntry, OUT u8 * pbValue,
				IN OUT u32 *pdwValueSize);

/*
 *  ======== reg_init ========
 *  Purpose:
 *      Initializes private state of REG module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      REG initialized.
 */
extern bool reg_init(void);

/*
 *  ======== reg_set_value ========
 *  Purpose:
 *      Set a value in the registry.
 *  Parameters:
 *      pstrEntry:      Name of entry to set.
 *      pbValue:        Points to buffer containing new data.
 *      dw_value_size:    Specifies bytes of memory bValue points to.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      - REG initialized.
 *      - pstrEntry is non-NULL value.
 *      - pbValue is a valid pointer.
 *      - dwValuSize > 0.
 *      - length of pstrEntry < REG_MAXREGPATHLENGTH.
 *  Ensures:
 */
extern dsp_status reg_set_value(IN CONST char *pstrEntry, IN u8 * pbValue,
				IN u32 dw_value_size);

#endif /* _REG_H */
