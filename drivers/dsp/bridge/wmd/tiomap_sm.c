/*
 * tiomap_sm.c
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

#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>

#include <dspbridge/dbg.h>

#include "_tiomap.h"
#include "_tiomap_pwr.h"

#define MAILBOX_FIFOSTATUS(m) (0x80 + 4 * (m))

static inline unsigned int fifo_full(void __iomem *mbox_base, int mbox_id)
{
	return __raw_readl(mbox_base + MAILBOX_FIFOSTATUS(mbox_id)) & 0x1;
}

DSP_STATUS CHNLSM_EnableInterrupt(struct WMD_DEV_CONTEXT *pDevContext)
{
	DSP_STATUS status = DSP_SOK;
	u32 numMbxMsg;
	u32 mbxValue;
	struct CFG_HOSTRES resources;
	u32 devType;
	struct IO_MGR *hIOMgr;

	DBG_Trace(DBG_ENTER, "CHNLSM_EnableInterrupt(0x%x)\n", pDevContext);

	/* Read the messages in the mailbox until the message queue is empty */

	CFG_GetHostResources((struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
			     &resources);
	DEV_GetDevType(pDevContext->hDevObject, &devType);
	status = DEV_GetIOMgr(pDevContext->hDevObject, &hIOMgr);
	if (devType == DSP_UNIT) {
		HW_MBOX_NumMsgGet(resources.dwMboxBase,
				  MBOX_DSP2ARM, &numMbxMsg);
		while (numMbxMsg != 0) {
			HW_MBOX_MsgRead(resources.dwMboxBase,
					MBOX_DSP2ARM,
					&mbxValue);
			numMbxMsg--;
		}
		/* clear the DSP mailbox as well...*/
		HW_MBOX_NumMsgGet(resources.dwMboxBase,
				  MBOX_ARM2DSP, &numMbxMsg);
		while (numMbxMsg != 0) {
			HW_MBOX_MsgRead(resources.dwMboxBase,
					MBOX_ARM2DSP, &mbxValue);
			numMbxMsg--;
			udelay(10);

			HW_MBOX_EventAck(resources.dwMboxBase, MBOX_ARM2DSP,
					 HW_MBOX_U1_DSP1,
					 HW_MBOX_INT_NEW_MSG);
		}
		/* Enable the new message events on this IRQ line */
		HW_MBOX_EventEnable(resources.dwMboxBase,
				    MBOX_DSP2ARM,
				    MBOX_ARM,
				    HW_MBOX_INT_NEW_MSG);
	}

	return status;
}

DSP_STATUS CHNLSM_DisableInterrupt(struct WMD_DEV_CONTEXT *pDevContext)
{
	struct CFG_HOSTRES resources;

	DBG_Trace(DBG_ENTER, "CHNLSM_DisableInterrupt(0x%x)\n", pDevContext);

	CFG_GetHostResources((struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
			     &resources);
	HW_MBOX_EventDisable(resources.dwMboxBase, MBOX_DSP2ARM,
			     MBOX_ARM, HW_MBOX_INT_NEW_MSG);
	return DSP_SOK;
}

DSP_STATUS CHNLSM_InterruptDSP2(struct WMD_DEV_CONTEXT *pDevContext,
				u16 wMbVal)
{
#ifdef CONFIG_BRIDGE_DVFS
	struct dspbridge_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;
	u32 opplevel = 0;
#endif
	struct CFG_HOSTRES resources;
	DSP_STATUS status = DSP_SOK;
	unsigned long timeout;
	u32 temp;

	status = CFG_GetHostResources((struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
				      &resources);
	if (DSP_FAILED(status))
		return DSP_EFAIL;
#ifdef CONFIG_BRIDGE_DVFS
	if (pDevContext->dwBrdState == BRD_DSP_HIBERNATION ||
	    pDevContext->dwBrdState == BRD_HIBERNATION) {
		if (pdata->dsp_get_opp)
			opplevel = (*pdata->dsp_get_opp)();
		if (opplevel == 1) {
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp)(opplevel+1);
		}
	}
#endif

	if (pDevContext->dwBrdState == BRD_DSP_HIBERNATION ||
	    pDevContext->dwBrdState == BRD_HIBERNATION) {
		/* Restore mailbox settings */
		/* Restart the peripheral clocks that were disabled only
		 * in DSP initiated Hibernation case.*/
		if (pDevContext->dwBrdState == BRD_DSP_HIBERNATION) {
			DSP_PeripheralClocks_Enable(pDevContext, NULL);
			/* Enabling Dpll in lock mode*/
			temp = (u32) *((REG_UWORD32 *)
				       ((u32) (resources.dwCmBase) + 0x34));
			temp = (temp & 0xFFFFFFFE) | 0x1;
			*((REG_UWORD32 *) ((u32) (resources.dwCmBase) + 0x34)) =
				(u32) temp;
			temp = (u32) *((REG_UWORD32 *)
				       ((u32) (resources.dwCmBase) + 0x4));
			temp = (temp & 0xFFFFFC8) | 0x37;

			*((REG_UWORD32 *) ((u32) (resources.dwCmBase) + 0x4)) =
				(u32) temp;
		}
		HW_MBOX_restoreSettings(resources.dwMboxBase);

		/*  Access MMU SYS CONFIG register to generate a short wakeup */
		temp = (u32) *((REG_UWORD32 *) ((u32)
						(resources.dwDmmuBase) + 0x10));

		pDevContext->dwBrdState = BRD_RUNNING;
	}
	timeout = jiffies + msecs_to_jiffies(1);
	while (fifo_full((void __iomem *) resources.dwMboxBase, 0)) {
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "dspbridge: timed out waiting for mailbox\n");
			return WMD_E_TIMEOUT;
		}
	}
	DBG_Trace(DBG_LEVEL3, "writing %x to Mailbox\n",
		  wMbVal);

	HW_MBOX_MsgWrite(resources.dwMboxBase, MBOX_ARM2DSP,
			 wMbVal);
	return DSP_SOK;
}

bool CHNLSM_ISR(struct WMD_DEV_CONTEXT *pDevContext, bool *pfSchedDPC,
		u16 *pwIntrVal)
{
	struct CFG_HOSTRES resources;
	u32 numMbxMsg;
	u32 mbxValue;

	DBG_Trace(DBG_ENTER, "CHNLSM_ISR(0x%x)\n", pDevContext);

	CFG_GetHostResources((struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);

	HW_MBOX_NumMsgGet(resources.dwMboxBase, MBOX_DSP2ARM, &numMbxMsg);

	if (numMbxMsg > 0) {
		HW_MBOX_MsgRead(resources.dwMboxBase, MBOX_DSP2ARM, &mbxValue);

		HW_MBOX_EventAck(resources.dwMboxBase, MBOX_DSP2ARM,
				 HW_MBOX_U0_ARM, HW_MBOX_INT_NEW_MSG);

		DBG_Trace(DBG_LEVEL3, "Read %x from Mailbox\n", mbxValue);
		*pwIntrVal = (u16) mbxValue;
	}
	/* Set *pfSchedDPC to true; */
	*pfSchedDPC = true;
	return true;
}
