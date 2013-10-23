/*
 * kfile.c
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
 *  ======== kfilece.c ========
 *  Purpose:
 *      This module provides file i/o services.
 *
 *  Public Functions:
 *      KFILE_Close
 *      KFILE_Exit
 *      KFILE_Init
 *      KFILE_Open
 *      KFILE_Read
 *      KFILE_Seek
 *      KFILE_Tell
 *
 *! Revision History
 *! ================
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 22-Nov-1999 kc: Added changes from code review.
 *! 12-Nov-1999 kc: Enabled CSL for UNICODE/ANSI string conversions.
 *! 30-Sep-1999 ag: Changed KFILE_Read() GT level from _ENTER to _4CLASS.
 *!                 Removed GT_set().
 *! 25-Aug-1999 ag: Changed MEM_Calloc allocation type to MEM_PAGED.
 *! 13-Jul-1999 a0216266(ww - TID): Stubbed from kfilent.c.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/kfile.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define SIGNATURE           0x4c49464b	/* hex code of KFIL (reversed) */
#define MAXFILENAMELENGTH   256
#define GENERAL_FAILURE     0xffffffff	/* SetFilePointer error */

/* The KFILE_FileObj abstracts the true file handle from a KFILE handle. */
struct KFILE_FileObj {
    u32 dwSignature;
    __kernel_pid_t owner_pid;	/* PID of process that opened this file */
    char 	*fileName  ;
    bool          isOpen    ;
    u32        size      ;
    u32        curPos    ;
    long 	  hInternal;		/* internal handle of file */
    struct file *fileDesc;

};

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask KFILE_debugMask = { NULL, NULL };	/* Debug mask */
#endif

/*
 *  ======== KFILE_Close ========
 *  Purpose:
 *      This function closes a file's stream.
 */
s32 KFILE_Close(struct KFILE_FileObj *hFile)
{
	s32 cRetVal = 0;	/* 0 indicates success */
	s32 fRetVal = 0;
	__kernel_pid_t curr_pid;

	GT_1trace(KFILE_debugMask, GT_ENTER, "KFILE_Close: hFile 0x%x\n",
		  hFile);

	/* Check for valid handle */
	if (MEM_IsValidHandle(hFile, SIGNATURE)) {
		/* Close file only if opened by the same process (id). Otherwise
		 * Linux closes all open file handles when process exits.*/
               /* Return PID instead of process handle */
               curr_pid = (__kernel_pid_t)current->pid;
		fRetVal = filp_close(hFile->fileDesc, NULL) ;
		if (fRetVal) {
			cRetVal = E_KFILE_ERROR;
			GT_1trace(KFILE_debugMask, GT_6CLASS,
				  "KFILE_Close: sys_close "
				  "returned %d\n", fRetVal);
		}
		MEM_FreeObject(hFile);
	} else {
		cRetVal = E_KFILE_INVALIDHANDLE;
		GT_0trace(KFILE_debugMask, GT_6CLASS, "KFILE_Close: "
			  "invalid file handle\n");
	}
	return cRetVal;
}

/*
 *  ======== KFILE_Exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count
 *      is 0.
 */
void KFILE_Exit(void)
{
	GT_0trace(KFILE_debugMask, GT_5CLASS, "KFILE_Exit\n");
}

/*
 *  ======== KFILE_Init ========
 */
bool KFILE_Init(void)
{
	GT_create(&KFILE_debugMask, "KF");	/* "KF" for KFile */

	GT_0trace(KFILE_debugMask, GT_5CLASS, "KFILE_Init\n");

	return true;
}

/*
 *  ======== KFILE_Open ========
 *  Purpose:
 *      Open a file for reading ONLY
 */
struct KFILE_FileObj *KFILE_Open(CONST char *pszFileName, CONST char *pszMode)
{
	struct KFILE_FileObj *hFile;	/* file handle */
	DSP_STATUS status;
	mm_segment_t fs;

	struct file*fileDesc = NULL;
	DBC_Require(pszMode != NULL);
	DBC_Require(pszFileName != NULL);

	GT_2trace(KFILE_debugMask, GT_ENTER,
		  "KFILE_Open: pszFileName %s, pszMode "
		  "%s\n", pszFileName, pszMode);

	/* create a KFILE object */
	MEM_AllocObject(hFile, struct KFILE_FileObj, SIGNATURE);

	if (hFile) {
		fs = get_fs();
		set_fs(get_ds());
	/* Third argument is mode (permissions). Ignored unless creating file */
		fileDesc = filp_open(pszFileName, O_RDONLY, 0);
		if ((IS_ERR(fileDesc)) || (fileDesc == NULL) ||
		     (fileDesc->f_op == NULL) || (fileDesc->f_op->read == NULL)
		     || (fileDesc->f_op->llseek == NULL)) {
			status = DSP_EFILE;
		} else {
			hFile->fileDesc = fileDesc;
			hFile->fileName = (char *)pszFileName;
			hFile->isOpen	   = true;
			hFile->curPos   = 0;
			hFile->size = fileDesc->f_op->llseek(fileDesc, 0,
							    SEEK_END);
			fileDesc->f_op->llseek(fileDesc, 0, SEEK_SET);
                       /* Return PID instead of process handle */
                       hFile->owner_pid = current->pid;

			status = DSP_SOK;
		}
		set_fs(fs);
		if (DSP_FAILED(status)) {
			/* free memory, and clear handle */
			MEM_FreeObject(hFile);
			hFile = NULL;
		}
	} else {
			GT_0trace(KFILE_debugMask, GT_6CLASS,
				  "KFILE_Open: MEM_AllocObject failed\n");
			status = DSP_EMEMORY;
	}
	return hFile;
}

/*
 *  ======== KFILE_Read ========
 *  Purpose:
 *      Reads a specified number of bytes into a buffer.
 */
s32
KFILE_Read(void __user*pBuffer, s32 cSize, s32 cCount,
			struct KFILE_FileObj *hFile)
{
	u32 dwBytesRead = 0;
	s32 cRetVal = 0;
	mm_segment_t fs;

	DBC_Require(pBuffer != NULL);

	GT_4trace(KFILE_debugMask, GT_4CLASS,
		  "KFILE_Read: buffer 0x%x, cSize 0x%x,"
		  "cCount 0x%x, hFile 0x%x\n", pBuffer, cSize, cCount, hFile);

	/* check for valid file handle */
	if (MEM_IsValidHandle(hFile, SIGNATURE)) {
		if ((cSize > 0) && (cCount > 0) && pBuffer) {
			/* read from file */
			fs = get_fs();
			set_fs(get_ds());
			dwBytesRead = hFile->fileDesc->f_op->read(hFile->
				      fileDesc, pBuffer, cSize *cCount,
				      &(hFile->fileDesc->f_pos));
			set_fs(fs);
			if (dwBytesRead) {
				cRetVal = dwBytesRead / cSize;
				hFile->curPos += dwBytesRead;
				DBC_Assert((dwBytesRead / cSize) <= \
					  (u32)cCount);
			} else {
				cRetVal = E_KFILE_ERROR;
				GT_0trace(KFILE_debugMask, GT_6CLASS,
					  "KFILE_Read: sys_read() failed\n");
			}
		} else {
			cRetVal = DSP_EINVALIDARG;
			GT_0trace(KFILE_debugMask, GT_6CLASS,
				  "KFILE_Read: Invalid argument(s)\n");
		}
	} else {
		cRetVal = E_KFILE_INVALIDHANDLE;
		GT_0trace(KFILE_debugMask, GT_6CLASS,
			  "KFILE_Read: invalid file handle\n");
	}

	return cRetVal;
}

/*
 *  ======== KFILE_Seek ========
 *  Purpose:
 *      Sets the file position indicator. NOTE:  we don't support seeking
 *      beyond the boundaries of a file.
 */
s32 KFILE_Seek(struct KFILE_FileObj *hFile, s32 lOffset, s32 cOrigin)
{
	s32 cRetVal = 0;	/* 0 for success */
	u32 dwCurPos = 0;

	struct file *fileDesc = NULL;

	GT_3trace(KFILE_debugMask, GT_ENTER, "KFILE_Seek: hFile 0x%x, "
		  "lOffset 0x%x, cOrigin 0x%x\n",
		  hFile, lOffset, cOrigin);

	/* check for valid file handle */
	if (MEM_IsValidHandle(hFile, SIGNATURE)) {
		/* based on the origin flag, move the internal pointer */

	fileDesc = hFile->fileDesc;
		switch (cOrigin) {
		case KFILE_SEEK_SET:
			dwCurPos = hFile->fileDesc->f_op->llseek(hFile->
				   fileDesc, lOffset, SEEK_SET);
			cRetVal = ((dwCurPos >= 0) ? 0 : E_KFILE_ERROR);
			break;

		case KFILE_SEEK_CUR:
			dwCurPos = hFile->fileDesc->f_op->llseek(hFile->
				   fileDesc, lOffset, SEEK_CUR);
			cRetVal = ((dwCurPos >= 0) ? 0 : E_KFILE_ERROR);
			break;
		case KFILE_SEEK_END:
			dwCurPos = hFile->fileDesc->f_op->llseek(hFile->
				   fileDesc, lOffset, SEEK_END);
			cRetVal = ((dwCurPos >= 0) ? 0 : E_KFILE_ERROR);
			break;
		default:
			cRetVal = E_KFILE_BADORIGINFLAG;
			GT_0trace(KFILE_debugMask, GT_6CLASS,
				  "KFILE_Seek:bad origin flag\n");
			break;
		}
	} else {
		cRetVal = E_KFILE_INVALIDHANDLE;
		GT_0trace(KFILE_debugMask, GT_6CLASS,
			  "KFILE_Seek:invalid file handle\n");
	}
	return cRetVal;
}

/*
 *  ======== KFILE_Tell ========
 *  Purpose:
 *      Reports the current value of the position indicator. We did not
 *	    consider 64 bit long file size, which implies a 4GB file limit
 *      (2 to 32 power).
 */
s32 KFILE_Tell(struct KFILE_FileObj *hFile)
{
	u32 dwCurPos = 0;
	s32 lRetVal = E_KFILE_ERROR;

	GT_1trace(KFILE_debugMask, GT_ENTER, "KFILE_Tell: hFile 0x%x\n", hFile);

	if (MEM_IsValidHandle(hFile, SIGNATURE)) {

		/* Get current position. */
		dwCurPos = hFile->fileDesc->f_op->llseek(hFile->fileDesc, 0,
			   SEEK_CUR);
		if (dwCurPos >= 0)
			lRetVal = dwCurPos;

	} else {
		lRetVal = E_KFILE_INVALIDHANDLE;
		GT_0trace(KFILE_debugMask, GT_6CLASS,
			  "KFILE_Seek:invalid file handle\n");
	}
	return lRetVal;
}

