/*
 * dspdrv.h
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
 *  ======== dspdrv.h ========
 *  Purpose:
 *      This is the Stream Interface for the DDSP Class driver.
 *      All Device operations are performed via DeviceIOControl.
 *      Read, Seek and Write not used.
 *
 *  Public Functions
 *      DSP_Close
 *      DSP_Deinit
 *      DSP_Init
 *      DSP_IOControl
 *      DSP_Open
 *      DSP_PowerUp
 *      DSP_PowerDown
 *
 *! Revision History
 *! ================
 *! 28-Jan-2000 rr: Type void changed to Void.
 *! 02-Dec-1999 rr: MAX_DEV define moved from wcdce.c file.Code cleaned up.
 *! 12-Nov-1999 rr: "#include<wncnxerr.h> removed.
 *! 05-Oct-1999 rr  Renamed the file name to wcdce.h Removed Bus Specific
 *!                 code and #defines to PCCARD.h.
 *! 24-Sep-1999 rr  Changed the DSP_COMMON_WINDOW_SIZE to 0x4000(16k) for the
 *!                 Memory windows.
 *! 16-Jul-1999 ag  Adapted from rkw's CAC Bullet driver.
 *!
 */

#if !defined __DSPDRV_h__
#define __DSPDRV_h__

#define MAX_DEV     10		/* Max support of 10 devices */

/*
 *  ======== DSP_Close ========
 *  Purpose:
 *      Called when the client application/driver unloads the DDSP DLL. Upon
 *      unloading, the DDSP DLL will call CloseFile().
 *  Parameters:
 *      dwDeviceContext:    Handle returned by XXX_Open used to identify
 *                          the open context of the device
 *  Returns:
 *      TRUE indicates the device is successfully closed. FALSE indicates
 *      otherwise.
 *  Requires:
 *      dwOpenContext!= NULL.
 *  Ensures:The Application instance owned objects are cleaned up.
 */
extern bool DSP_Close(u32 dwDeviceContext);

/*
 *  ======== DSP_Deinit ========
 *  Purpose:
 *      This function is called by Device Manager to de-initialize a device.
 *      This function is not called by applications.
 *  Parameters:
 *      dwDeviceContext:Handle to the device context. The XXX_Init function
 *      creates and returns this identifier.
 *  Returns:
 *      TRUE indicates the device successfully de-initialized. Otherwise it
 *      returns FALSE.
 *  Requires:
 *      dwDeviceContext!= NULL. For a built in device this should never
 *      get called.
 *  Ensures:
 */
extern bool DSP_Deinit(u32 dwDeviceContext);

/*
 *  ======== DSP_Init ========
 *  Purpose:
 *      This function is called by Device Manager to initialize a device.
 *      This function is not called by applications
 *  Parameters:
 *      dwContext:  Specifies a pointer to a string containing the registry
 *                  path to the active key for the stream interface driver.
 *                  HKEY_LOCAL_MACHINE\Drivers\Active
 *  Returns:
 *      Returns a handle to the device context created. This is the our actual
 *      Device Object representing the DSP Device instance.
 *  Requires:
 *  Ensures:
 *      Succeeded:  device context > 0
 *      Failed:     device Context = 0
 */
extern u32 DSP_Init(OUT u32 *initStatus);

#endif
