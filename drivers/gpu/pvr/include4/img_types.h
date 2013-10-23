/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#ifndef __IMG_TYPES_H__
#define __IMG_TYPES_H__

typedef unsigned int	IMG_UINT,	*IMG_PUINT;
typedef signed int		IMG_INT,	*IMG_PINT;

typedef unsigned char	IMG_UINT8,	*IMG_PUINT8;
typedef unsigned char	IMG_BYTE,	*IMG_PBYTE;
typedef signed char		IMG_INT8,	*IMG_PINT8;
typedef char			IMG_CHAR,	*IMG_PCHAR;

typedef unsigned short	IMG_UINT16,	*IMG_PUINT16;
typedef signed short	IMG_INT16,	*IMG_PINT16;
typedef unsigned long	IMG_UINT32,	*IMG_PUINT32;
typedef signed long		IMG_INT32,	*IMG_PINT32;

	#if defined(LINUX)
#if !defined(USE_CODE)
		typedef unsigned long long		IMG_UINT64,	*IMG_PUINT64;
		typedef long long 				IMG_INT64,	*IMG_PINT64;
#endif
	#else

		#error("define an OS")

	#endif

#if !(defined(LINUX) && defined (__KERNEL__))
typedef float			IMG_FLOAT,	*IMG_PFLOAT;
typedef double			IMG_DOUBLE, *IMG_PDOUBLE;
#endif

typedef	enum tag_img_bool
{
	IMG_FALSE		= 0,
	IMG_TRUE		= 1,
	IMG_FORCE_ALIGN = 0x7FFFFFFF
} IMG_BOOL, *IMG_PBOOL;

typedef void            IMG_VOID,	*IMG_PVOID;

typedef IMG_INT32       IMG_RESULT;

typedef IMG_UINT32      IMG_UINTPTR_T;

typedef IMG_PVOID       IMG_HANDLE;

typedef void**          IMG_HVOID,	* IMG_PHVOID;

typedef IMG_UINT32      IMG_SIZE_T;

#define IMG_NULL        0


typedef IMG_PVOID IMG_CPU_VIRTADDR;

typedef struct {IMG_UINT32 uiAddr;} IMG_CPU_PHYADDR;

typedef struct {IMG_UINT32 uiAddr;} IMG_DEV_VIRTADDR;

typedef struct {IMG_UINT32 uiAddr;} IMG_DEV_PHYADDR;

typedef struct {IMG_UINT32 uiAddr;} IMG_SYS_PHYADDR;

typedef struct _SYSTEM_ADDR_
{
	
	IMG_UINT32	ui32PageCount;
	union
	{
		


		IMG_SYS_PHYADDR	sContig;		

		





		IMG_SYS_PHYADDR	asNonContig[1];
	} u;
} SYSTEM_ADDR;

#include "img_defs.h"

#endif	
