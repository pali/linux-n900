/*
 * gt.h
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
 *  ======== gt.h ========
 *  Purpose:
 *      There are two definitions that affect which portions of trace
 *      are acutally compiled into the client: GT_TRACE and GT_ASSERT. If
 *      GT_TRACE is set to 0 then all trace statements (except for assertions)
 *      will be compiled out of the client. If GT_ASSERT is set to 0 then
 *      assertions will be compiled out of the client. GT_ASSERT can not be
 *      set to 0 unless GT_TRACE is also set to 0 (i.e. GT_TRACE == 1 implies
 *      GT_ASSERT == 1).
 *
 *! Revision History
 *! ================
 *! 02-Feb-2000 rr: Renamed this file to gtce.h. GT CLASS and trace definitions
 *!                 are WinCE Specific.
 *! 03-Jan-1997	ge	Replaced "GT_" prefix to GT_Config structure members
 *!                 to eliminate preprocessor confusion with other macros.
 */
#include <linux/types.h>
#ifndef GT_
#define GT_

#ifndef GT_TRACE
#define GT_TRACE 0	    /* 0 = "trace compiled out"; 1 = "trace active" */
#endif

#include <dspbridge/host_os.h>

#if !defined(GT_ASSERT) || GT_TRACE
#define GT_ASSERT 1
#endif

struct GT_Config {
	Fxn PRINTFXN;
	Fxn PIDFXN;
	Fxn TIDFXN;
	Fxn ERRORFXN;
};

extern struct GT_Config *GT;

struct GT_Mask {
	char *modName;
	u8 *flags;
} ;

/*
 *  New GT Class defenitions.
 *
 *  The following are the explanations and how it could be used in the code
 *
 *  -   GT_ENTER    On Entry to Functions
 *
 *  -   GT_1CLASS   Display level of debugging status- Object/Automatic
 *                  variables
 *  -   GT_2CLASS   ---- do ----
 *
 *  -   GT_3CLASS   ---- do ---- + It can be used(recommended) for debug
 *		    status in the ISR, IST
 *  -   GT_4CLASS   ---- do ----
 *
 *  -   GT_5CLASS   Display entry for module init/exit functions
 *
 *  -   GT_6CLASS   Warn whenever SERVICES function fails
 *
 *  -   GT_7CLASS   Warn failure of Critical failures
 *
 */

#define GT_ENTER	((u8)0x01)
#define GT_1CLASS	((u8)0x02)
#define GT_2CLASS	((u8)0x04)
#define GT_3CLASS	((u8)0x08)
#define GT_4CLASS	((u8)0x10)
#define GT_5CLASS	((u8)0x20)
#define GT_6CLASS	((u8)0x40)
#define GT_7CLASS	((u8)0x80)

#ifdef _LINT_

/* LINTLIBRARY */

/*
 *  ======== GT_assert ========
 */
/* ARGSUSED */
void GT_assert(struct GT_Mask mask, s32 expr)
{
}

/*
 *  ======== GT_config ========
 */
/* ARGSUSED */
void GT_config(struct GT_Config config)
{
}

/*
 *  ======== GT_create ========
 */
/* ARGSUSED */
void GT_create(struct GT_Mask *mask, char *modName)
{
}

/*
 *  ======== GT_curLine ========
 *  Purpose:
 *      Returns the current source code line number. Is useful for performing
 *      branch testing using trace.  For example,
 *
 *      GT_1trace(curTrace, GT_1CLASS,
 *          "in module XX_mod, executing line %u\n", GT_curLine());
 */
/* ARGSUSED */
u16 GT_curLine(void)
{
	return (u16)NULL;
}

/*
 *  ======== GT_exit ========
 */
/* ARGSUSED */
void GT_exit(void)
{
}

/*
 *  ======== GT_init ========
 */
/* ARGSUSED */
void GT_init(void)
{
}

/*
 *  ======== GT_query ========
 */
/* ARGSUSED */
bool GT_query(struct GT_Mask mask, u8 class)
{
	return false;
}

/*
 *  ======== GT_set ========
 *  sets trace mask according to settings
 */

/* ARGSUSED */
void GT_set(char *settings)
{
}

/*
 *  ======== GT_setprintf ========
 *  sets printf function
 */

/* ARGSUSED */
void GT_setprintf(Fxn fxn)
{
}

/* ARGSUSED */
void GT_0trace(struct GT_Mask mask, u8 class, char *format)
{
}

/* ARGSUSED */
void GT_1trace(struct GT_Mask mask, u8 class, char *format, ...)
{
}

/* ARGSUSED */
void GT_2trace(struct GT_Mask mask, u8 class, char *format, ...)
{
}

/* ARGSUSED */
void GT_3trace(struct GT_Mask mask, u8 class, char *format, ...)
{
}

/* ARGSUSED */
void GT_4trace(struct GT_Mask mask, u8 class, char *format, ...)
{
}

/* ARGSUSED */
void GT_5trace(struct GT_Mask mask, u8 class, char *format, ...)
{
}

/* ARGSUSED */
void GT_6trace(struct GT_Mask mask, u8 class, char *format, ...)
{
}

#else

#define	GT_BOUND    26		/* 26 letters in alphabet */

extern void _GT_create(struct GT_Mask *mask, char *modName);

#define GT_exit()

extern void GT_init(void);
extern void _GT_set(char *str);
extern s32 _GT_trace(struct GT_Mask *mask, char *format, ...);

#if GT_ASSERT == 0

#define GT_assert(mask, expr)
#define GT_config(config)
#define GT_configInit(config)
#define GT_seterror(fxn)

#else

extern struct GT_Config _GT_params;

#define GT_assert(mask, expr) \
	(!(expr) ? \
	    printk("assertion violation: %s, line %d\n", \
			    __FILE__, __LINE__), NULL : NULL)

#define GT_config(config)     (_GT_params = *(config))
#define GT_configInit(config) (*(config) = _GT_params)
#define GT_seterror(fxn)      (_GT_params.ERRORFXN = (Fxn)(fxn))

#endif

#if GT_TRACE == 0

#define GT_curLine()                ((u16)__LINE__)
#define GT_create(mask, modName)
#define GT_exit()
#define GT_init()
#define GT_set(settings)
#define GT_setprintf(fxn)

#define GT_query(mask, class)     false

#define GT_0trace(mask, class, format)
#define GT_1trace(mask, class, format, arg1)
#define GT_2trace(mask, class, format, arg1, arg2)
#define GT_3trace(mask, class, format, arg1, arg2, arg3)
#define GT_4trace(mask, class, format, arg1, arg2, arg3, arg4)
#define GT_5trace(mask, class, format, arg1, arg2, arg3, arg4, arg5)
#define GT_6trace(mask, class, format, arg1, arg2, arg3, arg4, arg5, arg6)

#else				/* GT_TRACE == 1 */


#define GT_create(mask, modName)    _GT_create((mask), (modName))
#define GT_curLine()                ((u16)__LINE__)
#define GT_set(settings)          _GT_set(settings)
#define GT_setprintf(fxn)         (_GT_params.PRINTFXN = (Fxn)(fxn))

#define GT_query(mask, class) ((*(mask).flags & (class)))

#define GT_0trace(mask, class, format) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format)) : 0)

#define GT_1trace(mask, class, format, arg1) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format), (arg1)) : 0)

#define GT_2trace(mask, class, format, arg1, arg2) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format), (arg1), (arg2)) : 0)

#define GT_3trace(mask, class, format, arg1, arg2, arg3) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format), (arg1), (arg2), (arg3)) : 0)

#define GT_4trace(mask, class, format, arg1, arg2, arg3, arg4) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format), (arg1), (arg2), (arg3), (arg4)) : 0)

#define GT_5trace(mask, class, format, arg1, arg2, arg3, arg4, arg5) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format), (arg1), (arg2), (arg3), (arg4), (arg5)) : 0)

#define GT_6trace(mask, class, format, arg1, arg2, arg3, arg4, arg5, arg6) \
    ((*(mask).flags & (class)) ? \
    _GT_trace(&(mask), (format), (arg1), (arg2), (arg3), (arg4), (arg5), \
	(arg6)) : 0)

#endif				/* GT_TRACE */

#endif				/* _LINT_ */

#endif				/* GTCE_ */
