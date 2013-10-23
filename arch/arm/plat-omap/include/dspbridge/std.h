/*
 * std.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
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
 *  ======== std.h ========
 *
 *! Revision History
 *! ================
 *! 16-Feb-2004 vp	GNU compiler 3.x defines inline keyword. Added
 *!			appropriate macros not to redefine inline keyword in
 *!			this file.
 *! 24-Oct-2002	ashu	defined _TI_ and _FIXED_ symbols for 28x.
 *! 24-Oct-2002	ashu	defined _TI_ for 24x.
 *! 01-Mar-2002 kln	changed LARGE_MODEL and Arg definition for 28x
 *! 01-Feb-2002 kln	added definitions for 28x
 *! 08-Dec-2000 kw:	added 'ArgToInt' and 'ArgToPtr' macros
 *! 30-Nov-2000 mf:	Added _64_, _6x_; removed _7d_
 *! 30-May-2000 srid:	Added   __TMS320C55X__ for 55x; Arg is void * for 55 .
 *! 18-Jun-1999 dr:	Added '_TI_', fixed __inline for SUN4, added inline
 *! 10-Feb-1999 rt:	Added '55' support, changed 54's symbol to _TMS320C5XX
 *! 29-Aug-1998 mf: 	fixed typo, removed obsolete targets
 *! 08-Jun-1998 mf: 	_67_ is synonym for _7d_
 *! 10-Oct-1997 rt;	added _7d_ for Raytheon C7DSP triggered by _TMS320C6700
 *! 04-Aug-1997 cc:	added _29_ for _TMS320C2XX
 *! 11-Jul-1997 dlr:	_5t_, and STD_SPOXTASK keyword for Tasking
 *! 12-Jun-1997 mf: 	_TMS320C60 -> _TMS320C6200
 *! 13-Feb-1997 mf:	_62_, with 32-bit LgInt
 *! 26-Nov-1996 kw: 	merged bios-c00's and wsx-a27's <dspbridge/std.h> changes
 *!			*and* revision history
 *! 12-Sep-1996 kw: 	added C54x #ifdef's
 *! 21-Aug-1996 mf: 	removed #define main smain for _21_
 *! 14-May-1996 gp:     def'd out INT, FLOAT, and COMPLEX defines for WSX.
 *! 11-Apr-1996 kw:     define _W32_ based on _WIN32 (defined by MS compiler)
 *! 07-Mar-1996 mg:     added Win32 support
 *! 06-Sep-1995 dh:	added _77_ dynamic stack support via fxns77.h
 *! 27-Jun-1995 dh:	added _77_ support
 *! 16-Mar-1995 mf: 	for _21_: #define main smain
 *! 01-Mar-1995 mf: 	set _20_ and _60_ (as well as _21_ for both)
 *! 22-Feb-1995 mf: 	Float is float for _SUN_ and _80_
 *! 22-Dec-1994 mf: 	Added _80_ definition, for PP or MP.
 *! 09-Dec-1994 mf: 	Added _53_ definition.
 *!			Added definitions of _30_, etc.
 *! 23-Aug-1994 dh	removed _21_ special case (kw)
 *! 17-Aug-1994 dh	added _51_ support
 *! 03-Aug-1994 kw	updated _80_ support
 *! 30-Jun-1994 kw	added _80_ support
 *! 05-Apr-1994 kw:	Added _SUN_ to _FLOAT_ definition
 *! 01-Mar-1994 kw: 	Made Bool an int (was u16) for _56_ (more efficient).
 *!			Added _53_ support.
 */

#ifndef STD_
#define STD_

#include <linux/types.h>

/*
 *  ======== _TI_ ========
 *  _TI_ is defined for all TI targets
 */
#if defined(_29_) || defined(_30_) || defined(_40_) || defined(_50_) || \
    defined(_54_) || defined(_55_) || defined(_6x_) || defined(_80_) || \
    defined(_28_) || defined(_24_)
#define _TI_	1
#endif

/*
 *  ======== _FLOAT_ ========
 *  _FLOAT_ is defined for all targets that natively support floating point
 */
#if defined(_SUN_) || defined(_30_) || defined(_40_) || defined(_67_) || \
    defined(_80_)
#define _FLOAT_	1
#endif

/*
 *  ======== _FIXED_ ========
 *  _FIXED_ is defined for all fixed point target architectures
 */
#if defined(_29_) || defined(_50_) || defined(_54_) || defined(_55_) || \
    defined(_62_) || defined(_64_) || defined(_28_)
#define _FIXED_	1
#endif

/*
 *  ======== _TARGET_ ========
 *  _TARGET_ is defined for all target architectures (as opposed to
 *  host-side software)
 */
#if defined(_FIXED_) || defined(_FLOAT_)
#define _TARGET_ 1
#endif

/*
 *  8, 16, 32-bit type definitions
 *
 *  Sm*	- 8-bit type
 *  Md* - 16-bit type
 *  Lg* - 32-bit type
 *
 *  *s32 - signed type
 *  *u32 - unsigned type
 *  *Bits - unsigned type (bit-maps)
 */

/*
 *  Aliases for standard C types
 */

typedef s32(*Fxn) (void);		/* generic function type */

#ifndef NULL
#define NULL 0
#endif


/*
 * These macros are used to cast 'Arg' types to 's32' or 'Ptr'.
 * These macros were added for the 55x since Arg is not the same
 * size as s32 and Ptr in 55x large model.
 */
#if defined(_28l_) || defined(_55l_)
#define ArgToInt(A)	((s32)((long)(A) & 0xffff))
#define ArgToPtr(A)	((Ptr)(A))
#else
#define ArgToInt(A)	((s32)(A))
#define ArgToPtr(A)	((Ptr)(A))
#endif

#endif				/* STD_ */
