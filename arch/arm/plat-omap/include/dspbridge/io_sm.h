/*
 * io_sm.h
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
 *  ======== io_sm.h ========
 *  Description:
 *      IO dispatcher for a shared memory channel driver.
 *      Also, includes macros to simulate SHM via port io calls.
 *
 *  Public Functions:
 *      IO_Dispatch
 *      IO_DPC
 *      IO_ISR
 *      IO_RequestChnl
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 01-Mar-2004 vp: Added IVA releated functions.
 *! 23-Apr-2003 sb: Fixed mailbox deadlock
 *! 06-Feb-2003 kc  Added IO_DDMAClearChnlDesc and IO_DDZCClearChnlDesc.
 *! 21-Dec-2001 ag  Removed unused param in IO_DDMAInitChnlDesc().
 *                  Updated comments. Removed #ifdef CHNL_NOIPCINTR.
 *! 05-Nov-2001 kc  Updated IO_CALLISR macro.
 *! 01-May-2001 jeh Removed IO_RequestMsg.
 *! 29-Mar-2001 ag  Added #ifdef CHNL_NOIPCINTR.
 *! 04-Dec-2000 jeh Added IO_RequestMsg.
 *! 26-Oct-2000 jeh Added IO_GetLong, IO_SetLong, IO_ReadValueLong, and
 *!                 IO_WriteValueLong, for passing arg in SHM structure.
 *! 20-Jan-2000 ag: Updated header comments per code review.
 *! 05-Jan-2000 ag: Text format clean-up.
 *! 02-Nov-1999 ag: Updated header descriptions.
 *! 25-May-1999 jg: Removed assumption of 4 bytes / word.
 *! 12-Aug-1996 gp: Created.
 */

#ifndef IOSM_
#define IOSM_

#include <dspbridge/_chnl_sm.h>
#include <dspbridge/host_os.h>

#include <dspbridge/iodefs.h>

#define IO_INPUT            0
#define IO_OUTPUT           1
#define IO_SERVICE          2
#define IO_MAXSERVICE       IO_SERVICE

#define IO_MGRSIGNATURE     0x494f4D43	/* "IOGR" */

#define DSPFieldAddr(type, field, base, wordsize) \
    ((((s32)&(((type *)0)->field)) / wordsize) + (u32)base)

/* Access can be different SM access word size (e.g. 16/32 bit words) */
#define IO_SetValue(pContext, type, base, field, value) (base->field = value)
#define IO_GetValue(pContext, type, base, field)	(base->field)
#define IO_OrValue(pContext, type, base, field, value)  (base->field |= value)
#define IO_AndValue(pContext, type, base, field, value) (base->field &= value)
#define IO_SetLong(pContext, type, base, field, value)  (base->field = value)
#define IO_GetLong(pContext, type, base, field)         (base->field)

#define IO_DisableInterrupt(h)  CHNLSM_DisableInterrupt(h)
#define IO_EnableInterrupt(h)   CHNLSM_EnableInterrupt(h)
#define IO_CALLISR(h, pFlag, pwMBRegVal)   CHNLSM_ISR(h, pFlag, pwMBRegVal)

/*
 *  ======== IO_CancelChnl ========
 *  Purpose:
 *      Cancel IO on a given channel.
 *  Parameters:
 *      hIOMgr:     IO Manager.
 *      ulChnl:     Index of channel to cancel IO on.
 *  Returns:
 *  Requires:
 *      Valid hIOMgr.
 *  Ensures:
 */
	extern void IO_CancelChnl(struct IO_MGR *hIOMgr, u32 ulChnl);

/*
 *  ======== IO_DPC ========
 *  Purpose:
 *      Deferred procedure call for shared memory channel driver ISR.  Carries
 *      out the dispatch of I/O.
 *  Parameters:
 *      pRefData:   Pointer to reference data registered via a call to
 *                  DPC_Create().
 *  Returns:
 *  Requires:
 *      Must not block.
 *      Must not acquire resources.
 *      All data touched must be locked in memory if running in kernel mode.
 *  Ensures:
 *      Non-preemptible (but interruptible).
 */
	extern void IO_DPC(IN OUT void *pRefData);

/*
 *  ======== IO_ISR ========
 *  Purpose:
 *      Main interrupt handler for the shared memory WMD channel manager.
 *      Calls the WMD's CHNLSM_ISR to determine if this interrupt is ours, then
 *      schedules a DPC to dispatch I/O..
 *  Parameters:
 *      pRefData:   Pointer to the channel manager object for this board.
 *                  Set in an initial call to ISR_Install().
 *  Returns:
 *      TRUE if interrupt handled; FALSE otherwise.
 *  Requires:
 *      Must be in locked memory if executing in kernel mode.
 *      Must only call functions which are in locked memory if Kernel mode.
 *      Must only call asynchronous services.
 *      Interrupts are disabled and EOI for this interrupt has been sent.
 *  Ensures:
 */
       irqreturn_t IO_ISR(int irq, IN void *pRefData);
/*
 *  ======== IO_RequestChnl ========
 *  Purpose:
 *      Request I/O from the DSP. Sets flags in shared memory, then interrupts
 *      the DSP.
 *  Parameters:
 *      hIOMgr:     IO manager handle.
 *      pChnl:      Ptr to the channel requesting I/O.
 *      iMode:      Mode of channel: {IO_INPUT | IO_OUTPUT}.
 *  Returns:
 *  Requires:
 *      pChnl != NULL
 *  Ensures:
 */
	extern void IO_RequestChnl(struct IO_MGR *hIOMgr,
				   struct CHNL_OBJECT *pChnl,
				   u32 iMode, OUT u16 *pwMbVal);

/*
 *  ======== IO_Schedule ========
 *  Purpose:
 *      Schedule DPC for IO.
 *  Parameters:
 *      pIOMgr:     Ptr to a I/O manager.
 *  Returns:
 *  Requires:
 *      pChnl != NULL
 *  Ensures:
 */
	extern void IO_Schedule(struct IO_MGR *hIOMgr);

/*
 * DSP-DMA IO functions
 */

/*
 *  ======== IO_DDMAInitChnlDesc ========
 *  Purpose:
 *      Initialize DSP DMA channel descriptor.
 *  Parameters:
 *      hIOMgr:         Handle to a I/O manager.
 *      uDDMAChnlId:    DDMA channel identifier.
 *      uNumDesc:       Number of buffer descriptors(equals # of IOReqs &
 *                      Chirps)
 *      pDsp:           Dsp address;
 *  Returns:
 *  Requires:
 *     uDDMAChnlId < DDMA_MAXDDMACHNLS
 *     uNumDesc > 0
 *     pVa != NULL
 *     pDspPa != NULL
 *
 *  Ensures:
 */
	extern void IO_DDMAInitChnlDesc(struct IO_MGR *hIOMgr, u32 uDDMAChnlId,
					u32 uNumDesc, void *pDsp);

/*
 *  ======== IO_DDMAClearChnlDesc ========
 *  Purpose:
 *      Clear DSP DMA channel descriptor.
 *  Parameters:
 *      hIOMgr:         Handle to a I/O manager.
 *      uDDMAChnlId:    DDMA channel identifier.
 *  Returns:
 *  Requires:
 *     uDDMAChnlId < DDMA_MAXDDMACHNLS
 *  Ensures:
 */
	extern void IO_DDMAClearChnlDesc(struct IO_MGR *hIOMgr,
					 u32 uDDMAChnlId);

/*
 *  ======== IO_DDMARequestChnl ========
 *  Purpose:
 *      Request channel DSP-DMA from the DSP. Sets up SM descriptors and
 *      control fields in shared memory.
 *  Parameters:
 *      hIOMgr:     Handle to a I/O manager.
 *      pChnl:      Ptr to channel object
 *      pChirp:     Ptr to channel i/o request packet.
 *  Returns:
 *  Requires:
 *      pChnl != NULL
 *      pChnl->cIOReqs > 0
 *      pChirp != NULL
 *  Ensures:
 */
	extern void IO_DDMARequestChnl(struct IO_MGR *hIOMgr,
				       struct CHNL_OBJECT *pChnl,
				       struct CHNL_IRP *pChirp,
				       OUT u16 *pwMbVal);

/*
 * Zero-copy IO functions
 */

/*
 *  ======== IO_DDZCInitChnlDesc ========
 *  Purpose:
 *      Initialize ZCPY channel descriptor.
 *  Parameters:
 *      hIOMgr:     Handle to a I/O manager.
 *      uZId:       zero-copy channel identifier.
 *  Returns:
 *  Requires:
 *     uDDMAChnlId < DDMA_MAXZCPYCHNLS
 *     hIOMgr != Null
 *  Ensures:
 */
	extern void IO_DDZCInitChnlDesc(struct IO_MGR *hIOMgr, u32 uZId);

/*
 *  ======== IO_DDZCClearChnlDesc ========
 *  Purpose:
 *      Clear DSP ZC channel descriptor.
 *  Parameters:
 *      hIOMgr:         Handle to a I/O manager.
 *      uChnlId:        ZC channel identifier.
 *  Returns:
 *  Requires:
 *      hIOMgr is valid
 *      uChnlId < DDMA_MAXZCPYCHNLS
 *  Ensures:
 */
	extern void IO_DDZCClearChnlDesc(struct IO_MGR *hIOMgr, u32 uChnlId);

/*
 *  ======== IO_DDZCRequestChnl ========
 *  Purpose:
 *      Request zero-copy channel transfer. Sets up SM descriptors and
 *      control fields in shared memory.
 *  Parameters:
 *      hIOMgr:         Handle to a I/O manager.
 *      pChnl:          Ptr to channel object
 *      pChirp:         Ptr to channel i/o request packet.
 *  Returns:
 *  Requires:
 *      pChnl != NULL
 *      pChnl->cIOReqs > 0
 *      pChirp != NULL
 *  Ensures:
 */
	extern void IO_DDZCRequestChnl(struct IO_MGR *hIOMgr,
				       struct CHNL_OBJECT *pChnl,
				       struct CHNL_IRP *pChirp,
				       OUT u16 *pwMbVal);

/*
 *  ======== IO_SHMsetting ========
 *  Purpose:
 *      Sets the shared memory setting
 *  Parameters:
 *      hIOMgr:         Handle to a I/O manager.
 *      desc:             Shared memory type
 *      pArgs:          Ptr to SHM setting
 *  Returns:
 *  Requires:
 *      hIOMgr != NULL
 *      pArgs != NULL
 *  Ensures:
 */
	extern DSP_STATUS IO_SHMsetting(IN struct IO_MGR *hIOMgr,
					IN enum SHM_DESCTYPE desc,
					IN void *pArgs);

/*
 *  Misc functions for the CHNL_IO shared memory library:
 */

/* Maximum channel bufsize that can be used. */
	extern u32 IO_BufSize(struct IO_MGR *hIOMgr);

	extern u32 IO_ReadValue(struct WMD_DEV_CONTEXT *hDevContext,
				  u32 dwDSPAddr);

	extern void IO_WriteValue(struct WMD_DEV_CONTEXT *hDevContext,
				  u32 dwDSPAddr, u32 dwValue);

	extern u32 IO_ReadValueLong(struct WMD_DEV_CONTEXT *hDevContext,
				      u32 dwDSPAddr);

	extern void IO_WriteValueLong(struct WMD_DEV_CONTEXT *hDevContext,
				      u32 dwDSPAddr, u32 dwValue);

	extern void IO_OrSetValue(struct WMD_DEV_CONTEXT *hDevContext,
				  u32 dwDSPAddr, u32 dwValue);

	extern void IO_AndSetValue(struct WMD_DEV_CONTEXT *hDevContext,
				   u32 dwDSPAddr, u32 dwValue);

	extern void IO_IntrDSP2(IN struct IO_MGR *pIOMgr, IN u16 wMbVal);

       extern void IO_SM_init(void);

/*
 *  ========PrintDspTraceBuffer ========
 *      Print DSP tracebuffer.
 */
       extern DSP_STATUS PrintDspTraceBuffer(struct WMD_DEV_CONTEXT
                                               *hWmdContext);

#endif				/* IOSM_ */
