/*
 * services.h
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
 *  ======== services.h ========
 *  Purpose:
 *      Provide loading and unloading of SERVICES modules.
 *
 *  Public Functions:
 *      SERVICES_Exit(void)
 *      SERVICES_Init(void)
 *
 *! Revision History:
 *! ================
 *! 01-Feb-2000 kc: Created.
 */

#ifndef SERVICES_
#define SERVICES_

#include <dspbridge/host_os.h>
/*
 *  ======== SERVICES_Exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      SERVICES initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
	extern void SERVICES_Exit(void);

/*
 *  ======== SERVICES_Init ========
 *  Purpose:
 *      Initializes SERVICES modules.
 *  Parameters:
 *  Returns:
 *      TRUE if all modules initialized; otherwise FALSE.
 *  Requires:
 *  Ensures:
 *      SERVICES modules initialized.
 */
	extern bool SERVICES_Init(void);

#endif				/* SERVICES_ */
