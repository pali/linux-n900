/*
 * gt.c
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
 * ======== gt.c ========
 * Description: This module implements the trace mechanism for bridge.
 *
 *! Revision History
 *! ================
 *! 16-May-1997 dr	Changed GT_Config member names to conform to coding
 *!			standards.
 *! 23-Apr-1997 ge	Check for GT->TIDFXN for NULL before calling it.
 *! 03-Jan-1997	ge	Changed GT_Config structure member names to eliminate
 *!			preprocessor confusion with other macros.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>

/*  ----------------------------------- This */
#include <dspbridge/gt.h>

#define GT_WILD	'*'

#define GT_CLEAR	'='
#define GT_ON		'+'
#define GT_OFF		'-'

enum GT_State {
	GT_SEP,
	GT_FIRST,
	GT_SECOND,
	GT_OP,
	GT_DIGITS
} ;

#ifdef CONFIG_BRIDGE_DEBUG
static char *GT_1format = "%s - %d: ";
static char *GT_2format = "%s - %d(%d): ";
#endif /* CONFIG_BRIDGE_DEBUG */

static unsigned char *GT_tMask[GT_BOUND];

static bool curInit;
static char *separator;
static unsigned char tabMem[GT_BOUND][sizeof(unsigned char) * GT_BOUND];

static void error(char *string);
static void setMask(s16 index1, s16 index2, char op, unsigned char mask);

/*
 *  ======== _GT_create ========
 *  purpose:
 *      Creates GT mask.
 */
void _GT_create(struct GT_Mask *mask, char *modName)
{
	mask->modName = modName;
	mask->flags = &(GT_tMask[modName[0] - 'A'][modName[1] - 'A']);
}

/*
 *  ======== GT_init ========
 *  purpose:
 *      Initializes GT module.
 */
#ifdef GT_init
#undef GT_init
#endif
void GT_init(void)
{
	register unsigned char index1;
	register unsigned char index2;

	if (!curInit) {
		curInit = true;

		separator = " ,;/";

		for (index1 = 0; index1 < GT_BOUND; index1++) {
			GT_tMask[index1] = tabMem[index1];
			for (index2 = 0; index2 < GT_BOUND; index2++) {
				/* no tracing */
				GT_tMask[index1][index2] = 0x00;
			}
		}
	}
}

/*
 *  ======== _GT_set ========
 *  purpose:
 *      Sets the trace string format.
 */

void _GT_set(char *str)
{
	enum GT_State state;
	char *sep;
	s16 index1 = GT_BOUND;	/* indicates all values */
	s16 index2 = GT_BOUND;	/* indicates all values */
	char op = GT_CLEAR;
	bool maskValid;
	s16 digit;
	register unsigned char mask = 0x0;	/* no tracing */

	if (str == NULL)
		return;

	maskValid = false;
	state = GT_SEP;
	while (*str != '\0') {
		switch ((s32) state) {
		case (s32) GT_SEP:
			maskValid = false;
			sep = separator;
			while (*sep != '\0') {
				if (*str == *sep) {
					str++;
					break;
				} else {
					sep++;
				}
			}
			if (*sep == '\0')
				state = GT_FIRST;

			break;
		case (s32) GT_FIRST:
			if (*str == GT_WILD) {
				/* indicates all values */
				index1 = GT_BOUND;
				/* indicates all values */
				index2 = GT_BOUND;
				state = GT_OP;
			} else {
				if (*str >= 'a')
					index1 = (s16) (*str - 'a');
				else
					index1 = (s16) (*str - 'A');
				if ((index1 >= 0) && (index1 < GT_BOUND))
					state = GT_SECOND;
				else
					state = GT_SEP;
			}
			str++;
			break;
		case (s32) GT_SECOND:
			if (*str == GT_WILD) {
				index2 = GT_BOUND;   /* indicates all values */
				state = GT_OP;
				str++;
			} else {
				if (*str >= 'a')
					index2 = (s16) (*str - 'a');
				else
					index2 = (s16) (*str - 'A');
				if ((index2 >= 0) && (index2 < GT_BOUND)) {
					state = GT_OP;
					str++;
				} else {
					state = GT_SEP;
				}
			}
			break;
		case (s32) GT_OP:
			op = *str;
			mask = 0x0;	/* no tracing */
			switch (op) {
			case (s32) GT_CLEAR:
				maskValid = true;
			case (s32) GT_ON:
			case (s32) GT_OFF:
				state = GT_DIGITS;
				str++;
				break;
			default:
				state = GT_SEP;
				break;
			}
			break;
		case (s32) GT_DIGITS:
			digit = (s16) (*str - '0');
			if ((digit >= 0) && (digit <= 7)) {
				mask |= (0x01 << digit);
				maskValid = true;
				str++;
			} else {
				if (maskValid == true) {
					setMask(index1, index2, op, mask);
					maskValid = false;
				}
				state = GT_SEP;
			}
			break;
		default:
			error("illegal trace mask");
			break;
		}
	}

	if (maskValid)
		setMask(index1, index2, op, mask);
}

/*
 *  ======== _GT_trace ========
 *  purpose:
 *      Prints the input string onto standard output
 */

s32 _GT_trace(struct GT_Mask *mask, char *format, ...)
{
	s32 arg1, arg2, arg3, arg4, arg5, arg6;
	va_list va;

	va_start(va, format);

	arg1 = va_arg(va, s32);
	arg2 = va_arg(va, s32);
	arg3 = va_arg(va, s32);
	arg4 = va_arg(va, s32);
	arg5 = va_arg(va, s32);
	arg6 = va_arg(va, s32);

	va_end(va);
#ifdef DEBUG
	if (GT->PIDFXN == NULL) {
		printk(GT_1format, mask->modName, GT->TIDFXN ?
		(*GT->TIDFXN)() : 0);
	} else {
		printk(GT_2format, mask->modName, (*GT->PIDFXN)(),
		GT->TIDFXN ? (*GT->TIDFXN)() : 0);
	}
#endif
	printk(format, arg1, arg2, arg3, arg4, arg5, arg6);

	return 0;
}

/*
 *  ======== error ========
 *  purpose:
 *      Prints errors onto the standard output.
 */
static void error(char *string)
{
	printk("GT: %s", string);
}

/*
 *  ======== setmask ========
 *  purpose:
 *      Sets mask for the GT module.
 */

static void setMask(s16 index1, s16 index2, char op, u8 mask)
{
	register s16 index;

	if (index1 < GT_BOUND) {
		if (index2 < GT_BOUND) {
			switch (op) {
			case (s32) GT_CLEAR:
				GT_tMask[index1][index2] = mask;
				break;
			case (s32) GT_ON:
				GT_tMask[index1][index2] |= mask;
				break;
			case (s32) GT_OFF:
				GT_tMask[index1][index2] &= ~mask;
				break;
			default:
				error("illegal trace mask");
				break;
			}
		} else {
			for (index2--; index2 >= 0; index2--) {
				switch (op) {
				case (s32) GT_CLEAR:
					GT_tMask[index1][index2] = mask;
					break;
				case (s32) GT_ON:
					GT_tMask[index1][index2] |= mask;
					break;
				case (s32) GT_OFF:
					GT_tMask[index1][index2] &= ~mask;
					break;
				default:
					error("illegal trace mask");
					break;
				}
			}
		}
	} else {
		for (index1--; index1 >= 0; index1--) {
			if (index2 < GT_BOUND) {
				switch (op) {
				case (s32) GT_CLEAR:
					GT_tMask[index1][index2] = mask;
					break;
				case (s32) GT_ON:
					GT_tMask[index1][index2] |= mask;
					break;
				case (s32) GT_OFF:
					GT_tMask[index1][index2] &= ~mask;
					break;
				default:
					error("illegal trace mask");
					break;
				}
			} else {
				index = GT_BOUND;
				for (index--; index >= 0; index--) {
					switch (op) {
					case (s32) GT_CLEAR:
						GT_tMask[index1][index] = mask;
						break;
					case (s32) GT_ON:
						GT_tMask[index1][index] |= mask;
						break;
					case (s32) GT_OFF:
						GT_tMask[index1][index] &=
						    ~mask;
						break;
					default:
						error("illegal trace mask");
						break;
					}
				}
			}
		}
	}
}
