/*
 * gb.h
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

/*
 *  ======== gb.h ========
 *  Generic bitmap manager.
 *
 *! Revision History
 *! ================
 */

#ifndef GB_
#define GB_

#define GB_NOBITS (~0)
#include <dspbridge/host_os.h>
typedef s32 GB_BitNum;
struct GB_TMap;

/*
 *  ======== GB_clear ========
 *  Clear the bit in position bitn in the bitmap map.  Bit positions are
 *  zero based.
 */

extern void GB_clear(struct GB_TMap *map, GB_BitNum bitn);

/*
 *  ======== GB_create ========
 *  Create a bit map with len bits.  Initially all bits are cleared.
 */

extern struct GB_TMap *GB_create(GB_BitNum len);

/*
 *  ======== GB_delete ========
 *  Delete previously created bit map
 */

extern void GB_delete(struct GB_TMap *map);

/*
 *  ======== GB_findandset ========
 *  Finds a clear bit, sets it, and returns the position
 */

extern GB_BitNum GB_findandset(struct GB_TMap *map);

/*
 *  ======== GB_minclear ========
 *  GB_minclear returns the minimum clear bit position.  If no bit is
 *  clear, GB_minclear returns -1.
 */
extern GB_BitNum GB_minclear(struct GB_TMap *map);

/*
 *  ======== GB_set ========
 *  Set the bit in position bitn in the bitmap map.  Bit positions are
 *  zero based.
 */

extern void GB_set(struct GB_TMap *map, GB_BitNum bitn);

/*
 *  ======== GB_test ========
 *  Returns TRUE if the bit in position bitn is set in map; otherwise
 *  GB_test returns FALSE.  Bit positions are zero based.
 */

extern bool GB_test(struct GB_TMap *map, GB_BitNum bitn);

#endif				/*GB_ */
