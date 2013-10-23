/*
 * kfile.h
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
 *  ======== kfile.h ========
 *  Purpose:
 *      Provide file I/O management capabilities.
 *
 *  Public Functions:
 *      KFILE_Close
 *      KFILE_Exit
 *      KFILE_Init
 *      KFILE_Open
 *      KFILE_Read
 *      KFILE_Seek
 *      KFILE_Tell
 *      KFILE_Write
 *
 *  Notes:
 *      The KFILE module is not like most of the other DSP/BIOS Bridge modules
 *      in that it doesn't return WSX_STATUS type values.  Rather, it's
 *      prototypes are meant to match the stdio file prototypes
 *      (ie, fopen, fclose, etc.).
 *
 *! Revision History
 *! ================
 *! 29-Oct-1999 kc:  Clean up for code review.
 *! 07-Jan-1998 cr:  Clean up for code review.
 *! 15-Aug-1997 cr:  Added E_KFILE_ERROR for general error condition.
 *! 04-Aug-1997 cr:  Added explicit CDECL descriptions.
 *! 11-Nov-1996 cr:  Implemented changes based on code review.
 *! 05-Nov-1996 cr:  Cleaned up for code review.
 *! 29-May-1996 gp:  Added requirement that size != 0 in _Write() and _Read().
 *! 28-May-1996 mg:  Changed return values for Read/Write.
 *! 14-Dec-1995 cr:  Created.
 */

#ifndef KFILE_
#define KFILE_

/*
 *  Constants for KFILE_Seek.  Note that these MUST be the same definitions as
 *  those defined for fseek.
 */
#define KFILE_SEEK_SET          0x00	/* seek from beginning of file */
#define KFILE_SEEK_CUR          0x01	/* seek from current position */
#define KFILE_SEEK_END          0x02	/* seek from end of file */

	struct KFILE_FileObj;

/*
 *  ======== KFILE_Close ========
 *  Purpose:
 *      This function closes a file's stream.
 *  Parameters:
 *      hFile:      Handle of the file stream returned by KFILE_Open.
 *  Returns:
 *      E_KFILE_INVALIDHANDLE:  bad handle.
 *      0:                      success.
 *      E_KFILE_ERROR:          unable to close specified handle.
 *  Requires:
 *      KFILE initialized.
 *  Ensures:
 */
	extern s32 KFILE_Close(IN struct KFILE_FileObj *hFile);

/*
 *  ======== KFILE_Exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      KFILE initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
	extern void KFILE_Exit(void);

/*
 *  ======== KFILE_Init ========
 *  Purpose:
 *      Initializes private state of KFILE module.
 *  Parameters:
 *  Returns:
 *      TRUE if success, else FALSE.
 *  Requires:
 *  Ensures:
 *      KFILE initialized.
 */
	extern bool KFILE_Init(void);

/*
 *  ======== KFILE_Open ========
 *  Purpose:
 *      Opens a file for use.
 *  Parameters:
 *      pszFileName:    Full path to name of the file to open.
 *      pszMode:        String containing open status.  Only the first
 *                      character of the string is examined, for either
 *                      "r" (read) or "w" (write) mode.
 *  Returns:
 *      A valid file handle if success, else NULL.
 *  Requires:
 *      - KFILE initialized.
 *      - pszMode != NULL.
 *      - pszFileName != NULL.
 *  Ensures:
 */
	extern struct KFILE_FileObj *KFILE_Open(IN CONST char *filename,
				       IN CONST char *mode);

/*
 *  ======== KFILE_Read ========
 *  Purpose:
 *      This function reads a specified number of bytes into a buffer.
 *  Parameters:
 *      pBuffer:    Array to which the file data is copied.
 *      cSize:      Number of characters in each object.
 *      cCount:     Number of objects to read in.
 *      hFile:      Handle of the file stream returned by KFILE_Open.
 *  Returns:
 *      E_KFILE_INVALIDHANDLE:  bad file handle.
 *      E_KFILE_ERROR:          general failure.
 *      > 0:                    success; # of objects read from file.
 *  Requires:
 *      KFILE initialized.
 *      pBuffer is a valid pointer.
 *  Ensures:
 */
	extern s32 KFILE_Read(OUT void __user*buffer,
			      IN s32 size, IN s32 count,
			      IN struct KFILE_FileObj *hFile);

/*
 *  ======== KFILE_Seek ========
 *  Purpose:
 *      This function sets the file position indicator.  NOTE:  we don't
 *      support seeking beyond the boundaries of a file.
 *  Parameters:
 *      hFile:      Handle of the file stream returned by KFILE_Open.
 *      offset:     Number of bytes from the origin to move.
 *      origin:     File reference point, one of the following values:
 *                  KFILE_SEEK_SET:  Seek from beginning of file.
 *                  KFILE_SEEK_CUR:  Seek from current position.
 *                  KFILE_SEEK_END:  Seek from end of file.
 *  Returns:
 *      0: success.
 *      E_KFILE_INVALIDHANDLE:  bad handle.
 *      E_KFILE_BADORIGIN:      invalid origin paramater.
 *      E_KFILE_ERROR:          general failure.
 *  Requires:
 *      KFILE initialized.
 *  Ensures:
 */
	extern s32 KFILE_Seek(IN struct KFILE_FileObj *hFile,
			      IN s32 offset, IN s32 origin);

/*
 *  ======== KFILE_Tell ========
 *  Purpose:
 *      This function reports the current value of the position indicator.
 *  Parameters:
 *      hFile:          Handle of the file stream returned by KFILE_Open.
 *  Return value:
 *      > 0: success; returns # of bytes the position indicator is from
 *           beginning of file.
 *      E_KFILE_ERROR:          general failure.
 *      E_KFILE_INVALIDHANDLE:  bad file handle.
 *  Requires:
 *      KFILE initialized.
 *  Ensures:
 */
	extern s32 KFILE_Tell(IN struct KFILE_FileObj *hFile);

/*
 *  ======== KFILE_Write ========
 *  Purpose:
 *      This function writes a number of objects to the stream.
 *  Parameters:
 *      pBuffer:    Array from which the file data is written.
 *      cSize:      Number of characters in each object.
 *      cCount:     Number of objects to write out.
 *      hFile:      Handle of the file stream returned by KFILE_Open.
 *  Returns:
 *      E_KFILE_INVALIDHANDLE:  bad file handle.
 *      E_KFILE_ERROR:          general failure.
 *      > 0:                    success; # of objects written to file.
 *  Requires:
 *      KFILE initialized.
 *      pBuffer != NULL.
 *  Postcondition:
 *      The file position indicator is advanced by the number of
 *      characters written.
 */
	extern s32 KFILE_Write(OUT void *buffer,
			       IN s32 size,
			       IN s32 count,
			       IN struct KFILE_FileObj *hFile);

#endif				/* KFILE_ */
