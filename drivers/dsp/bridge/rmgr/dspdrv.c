/*
 * dspdrv.c
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
 *  ======== dspdrv.c ========
 *  Description:
 *      Interface to allocate and free bridge resources.
 *
 *! Revision History
 *! ================
 *! 12-Apr-2004 hp: Compile IVA only for 24xx.
 *! 09-Feb-2004 vp: Updated to support IVA.
 *! 10-Feb-2003 vp: Code review updates.
 *! 18-oct-2002 vp: Ported to the Linux platform.
 *! 03-Mar-2002 rr: DSP_Deinit bug fixed (gets the Mgrhandle from registry
 *!		            before calling MGR_Destroy.
 *! 11-Jul-2001 jeh Moved MGR_Create() from DSP_Init() to DEV_StartDevice().
 *! 02-Apr-2001 rr: WCD_InitComplete2 return value is not checked thus
 *!                 sllowing the class driver to load irrespective of
 *!                 the image load.
 *! 30-Oct-2000 kc: Made changes w.r.t. usage of REG_SetValue.
 *! 05-Oct-2000 rr: WCD_InitComplete2 return value checked for RM.
 *!                 Failure in WCD_InitComplete2 will cause the
 *!                 DSP_Init to fail.
 *! 12-Aug-2000 kc: Changed REG_EnumValue to REG_EnumKey.
 *! 07-Aug-2000 rr: MGR_Create does the job of loading the DCD Dll.
 *! 26-Jul-2000 rr: Driver Object holds the DevNodeStrings for each
 *!                 DevObjects. Static variables removed. Returns
 *!                 the Driver Object in DSP_Init.
 *! 17-Jul-2000 rr: Driver Object is created in DSP_Init and that holds
 *!                 the list of Device objects.
 *! 07-Jul-2000 rr: RM implementaion started.
 *! 24-May-2000 ag: Cleaned up debug msgs.
 *! 02-May-2000 rr: DSP_Open returns GetCallerProcess as dwOpenContext.
 *! 03-Feb-2000 rr: GT Changes.
 *! 28-Jan-2000 rr: Code Cleaned up.Type void changed to void.
 *!                 DSP_Deinit checks return values.dwCode in
 *!                 DSP_IO_CONTROL is decoded(not hard coded)
 *! 27-Jan-2000 rr: REG_EnumValue Used .EnumerateKey fxn removed.
 *! 13-Jan-2000 rr: CFG_GetPrivateDword renamed to CFG_GetDevObject.
 *! 29-Dec-1999 rr: Code Cleaned up
 *! 09-Dec-1999 rr: EnumerateKey changed for retail build.
 *! 06-Dec-1999 rr: ArrayofInstalledNode, index and  ArrayofInstalledDev
 *!                 is Global.DevObject stores this pointer as hDevNode.
 *! 02-Dec-1999 rr: DBG_SetGT and RetailMSG conditionally included.
 *!                 Comments changed.Deinit handled.Code cleaned up.
 *!                 DSP_IOControl, Close, Deinit returns bool values.
 *!                 Calls WCD_InitComplete2 for Board AutoStart.
 *! 29-Nov-1999 rr: DSP_IOControl returns the result through pBufOut.
 *!                 Global Arrays keeps track of installed devices.
 *! 19-Nov-1999 rr: DSP_Init handles multiple drivers.
 *! 12-Nov-1999 rr: GetDriverKey and EnumerateKey functions added.
 *!                 for multiple mini driver support.PCCARD flag
 *!                 checking to include PCMCIA related stuff.
 *! 25-Oct-1999 rr: GT_Init is called within the Process Attach.
 *!                 return value initalized to S_OK upfront in the
 *!                 Process Attach.
 *! 15-Oct-1999 rr: DSP_DeInit handles the return values
 *! 05-Oct-1999 rr: All the PCMCIA related functions are now in PCCARD.c
 *!                 DRV_Request Resources is used instead of the
 *!                 RegisterMiniDriver as it sounds close to what we are doing.
 *! 24-Sep-1999 rr: DRV_RegisterMiniDriver is being called from here. Only
 *!                 neccessaryPCMCIA fxns are here. Soon they will move out
 *!                  either to a seperate file for bus specific inits.
 *! 10-Sep-1999 rr: GT Enabled. Considerably changed the  driver structure as
 *!                 - This is the Class driver. After successfully initialized
 *!                   the Class driver will attempt to load the Mini driver.
 *!                 - Need to seperate the PCMCIA stuff based on bus type.
 *!                 - Changed the name of the file to wcdce.c
 *!                 - Made the Media Handle as Global again
 *!
 *! 19-Aug-1999 rr: Removed the Global hbhMediaHandle. Included the MemTest.
 *!                 Modified the DSP_Init, now three windows are opened.
 *!                 Split the driver into PDD so that hardware dependent
 *!                 functions will reside in PDD.
 *! 16-Jul-1999 ag  Adapted from rkw's CAC Bullet card driver.
 *!
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
#include <dspbridge/cfg.h>
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>
#include <dspbridge/_dcd.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/mgr.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbreg.h>

/*  ----------------------------------- This */
#include <dspbridge/dspdrv.h>

/*  ----------------------------------- Globals */
struct GT_Mask curTrace;

/*
 *  ======== DSP_Init ========
 *  	Allocates bridge resources. Loads a base image onto DSP, if specified.
 */
u32 DSP_Init(OUT u32 *initStatus)
{
	char devNode[MAXREGPATHLENGTH] = "TIOMAP1510";
	DSP_STATUS status = DSP_EFAIL;
	struct DRV_OBJECT *drvObject = NULL;
	u32 index = 0;
	u32 deviceNode;
	u32 deviceNodeString;

	GT_create(&curTrace, "DD");

	GT_0trace(curTrace, GT_ENTER, "Entering DSP_Init \r\n");

	if (DSP_FAILED(WCD_Init())) {
		GT_0trace(curTrace, GT_7CLASS, "DSP_Init Failed \n");
		goto func_cont;
	}			/* End WCD_Exit */
	if (DSP_FAILED(DRV_Create(&drvObject))) {
		GT_0trace(curTrace, GT_7CLASS, "DSP_Init:DRV_Create Failed \n");
		WCD_Exit();
		goto func_cont;
	}		/* End DRV_Create */
	GT_0trace(curTrace, GT_5CLASS, "DSP_Init:DRV Created \r\n");

	/* Request Resources */
	if (DSP_SUCCEEDED(DRV_RequestResources((u32)&devNode,
	   &deviceNodeString))) {
		/* Attempt to Start the Device */
		if (DSP_SUCCEEDED(DEV_StartDevice(
		   (struct CFG_DEVNODE *)deviceNodeString))) {
			/* Retreive the DevObject from the Registry */
			GT_2trace(curTrace, GT_1CLASS,
				 "DSP_Init Succeeded for Device1:"
				 "%d: value: %x\n", index, deviceNodeString);
			status = DSP_SOK;
		} else {
			GT_0trace(curTrace, GT_7CLASS,
				 "DSP_Init:DEV_StartDevice Failed\n");
			(void)DRV_ReleaseResources
				((u32) deviceNodeString, drvObject);
			status = DSP_EFAIL;
		}
	} else {
		GT_0trace(curTrace, GT_7CLASS,
			 "DSP_Init:DRV_RequestResources Failed \r\n");
		status = DSP_EFAIL;
	}	/* DRV_RequestResources */
	index++;

	/* Unwind whatever was loaded */
	if (DSP_FAILED(status)) {
		/* irrespective of the status of DEV_RemoveDevice we conitinue
		 * unloading. Get the Driver Object iterate through and remove.
		 * Reset the status to E_FAIL to avoid going through
		 * WCD_InitComplete2. */
		status = DSP_EFAIL;
		for (deviceNode = DRV_GetFirstDevExtension(); deviceNode != 0;
		    deviceNode = DRV_GetNextDevExtension(deviceNode)) {
			(void)DEV_RemoveDevice
				((struct CFG_DEVNODE *)deviceNode);
			(void)DRV_ReleaseResources((u32)deviceNode,
				drvObject);
		}
		/* Remove the Driver Object */
		(void)DRV_Destroy(drvObject);
		drvObject = NULL;
		WCD_Exit();
		GT_0trace(curTrace, GT_7CLASS,
			 "DSP_Init:Logical device Failed to Load\n");
	}	/* Unwinding the loaded drivers */
func_cont:
	/* Attempt to Start the Board */
	if (DSP_SUCCEEDED(status)) {
		/* BRD_AutoStart could fail if the dsp execuetable is not the
		 * correct one. We should not propagate that error
		 * into the device loader. */
		(void)WCD_InitComplete2();
		GT_0trace(curTrace, GT_1CLASS, "DSP_Init Succeeded\n");
	} else {
		GT_0trace(curTrace, GT_7CLASS, "DSP_Init Failed\n");
	}			/* End WCD_InitComplete2 */
	DBC_Ensure((DSP_SUCCEEDED(status) && drvObject != NULL) ||
		  (DSP_FAILED(status) && drvObject == NULL));
	*initStatus = status;
	/* Return the Driver Object */
	return (u32)drvObject;
}

/*
 *  ======== DSP_Deinit ========
 *  	Frees the resources allocated for bridge.
 */
bool DSP_Deinit(u32 deviceContext)
{
	bool retVal = true;
	u32 deviceNode;
	struct MGR_OBJECT *mgrObject = NULL;

	GT_0trace(curTrace, GT_ENTER, "Entering DSP_Deinit \r\n");

	while ((deviceNode = DRV_GetFirstDevExtension()) != 0) {
		(void)DEV_RemoveDevice((struct CFG_DEVNODE *)deviceNode);

		(void)DRV_ReleaseResources((u32)deviceNode,
			 (struct DRV_OBJECT *)deviceContext);
	}

	(void) DRV_Destroy((struct DRV_OBJECT *) deviceContext);

	/* Get the Manager Object from Registry
	 * MGR Destroy will unload the DCD dll */
	if (DSP_SUCCEEDED(CFG_GetObject((u32 *)&mgrObject, REG_MGR_OBJECT)))
		(void)MGR_Destroy(mgrObject);

	WCD_Exit();

	return retVal;
}

/*
 *  ======== DSP_Close ========
 *  	The Calling Process handle is passed to DEV_CleanupProcesState
 *      for cleaning up of any resources used by the application
 */
bool DSP_Close(u32 dwOpenContext)
{
	bool retVal = false;

	DBC_Require(dwOpenContext != 0);

	GT_0trace(curTrace, GT_ENTER, "Entering DSP_Close\n");

#ifdef RES_CLEANUP_DISABLE

	if (DSP_SUCCEEDED(DEV_CleanupProcessState((HANDLE) dwOpenContext))) {
		GT_0trace(curTrace, GT_1CLASS, "DSP_Close Succeeded \r\n");
		retVal = true;
	} else {
		GT_0trace(curTrace, GT_7CLASS, "DSP_Close failed \r\n");
	}
#endif

	return retVal;
}
