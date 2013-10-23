/*
 * dlclasses_hdr.h
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



#ifndef _DLCLASSES_HDR_H
#define _DLCLASSES_HDR_H

/*****************************************************************************
 *****************************************************************************
 *
 *                          DLCLASSES_HDR.H
 *
 * Sample classes in support of the dynamic loader
 *
 * These are just concrete derivations of the virtual ones in dynamic_loader.h
 * with a few additional interfaces for init, etc.
 *****************************************************************************
 *****************************************************************************/

#include <dspbridge/dynamic_loader.h>

#include "DLstream.h"
#include "DLsymtab.h"
#include "DLalloc.h"
#include "DLinit.h"

#endif				/* _DLCLASSES_HDR_H */
