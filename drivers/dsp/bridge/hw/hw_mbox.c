/*
 * hw_mbox.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
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
 *  ======== hw_mbox.c ========
 *  Description:
 *      Mailbox messaging & configuration API definitions
 *
 *! Revision History:
 *! ================
 *! 16 Feb 2003 sb: Initial version
 */

#include <GlobalTypes.h>
#include "MLBRegAcM.h"
#include <hw_defs.h>
#include <hw_mbox.h>

/* width in bits of MBOX Id */
#define HW_MBOX_ID_WIDTH	   2

struct MAILBOX_CONTEXT mboxsetting = {
	.sysconfig = 2 << 3 | 1, /* SMART/AUTO-IDLE */
};

/* Saves the mailbox context */
HW_STATUS HW_MBOX_saveSettings(void __iomem *baseAddress)
{
	HW_STATUS status = RET_OK;

	mboxsetting.sysconfig = MLBMAILBOX_SYSCONFIGReadRegister32(baseAddress);
	/* Get current enable status */
	mboxsetting.irqEnable0 = MLBMAILBOX_IRQENABLE___0_3ReadRegister32
				 (baseAddress, HW_MBOX_U0_ARM);
	mboxsetting.irqEnable1 = MLBMAILBOX_IRQENABLE___0_3ReadRegister32
				 (baseAddress, HW_MBOX_U1_DSP1);
	return status;
}

/* Restores the mailbox context */
HW_STATUS HW_MBOX_restoreSettings(void __iomem *baseAddress)
{
	 HW_STATUS status = RET_OK;
	/* Restor IRQ enable status */
	MLBMAILBOX_IRQENABLE___0_3WriteRegister32(baseAddress, HW_MBOX_U0_ARM,
						 mboxsetting.irqEnable0);
	MLBMAILBOX_IRQENABLE___0_3WriteRegister32(baseAddress, HW_MBOX_U1_DSP1,
						 mboxsetting.irqEnable1);
	/* Restore Sysconfig register */
	MLBMAILBOX_SYSCONFIGWriteRegister32(baseAddress, mboxsetting.sysconfig);
	return status;
}

/* Reads a u32 from the sub module message box Specified. if there are no
 * messages in the mailbox then and error is returned. */
HW_STATUS HW_MBOX_MsgRead(const void __iomem *baseAddress,
	const HW_MBOX_Id_t mailBoxId, u32 *const pReadValue)
{
	HW_STATUS status = RET_OK;

	/* Check input parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
		      RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_PARAM(pReadValue, NULL, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
		      RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(mailBoxId, HW_MBOX_ID_MAX, RET_INVALID_ID,
			   RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);

	/* Read 32-bit message in mail box */
	*pReadValue = MLBMAILBOX_MESSAGE___0_15ReadRegister32(baseAddress,
							 (u32)mailBoxId);

	return status;
}

/* Writes a u32 from the sub module message box Specified. */
HW_STATUS HW_MBOX_MsgWrite(const void __iomem *baseAddress,
	const HW_MBOX_Id_t mailBoxId, const u32 writeValue)
{
	HW_STATUS status = RET_OK;

	/* Check input parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
			RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(mailBoxId, HW_MBOX_ID_MAX, RET_INVALID_ID,
			RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);

	/* Write 32-bit value to mailbox */
	MLBMAILBOX_MESSAGE___0_15WriteRegister32(baseAddress, (u32)mailBoxId,
					    (u32)writeValue);

	return status;
}

/* Gets number of messages in a specified mailbox. */
HW_STATUS HW_MBOX_NumMsgGet(const void __iomem *baseAddress,
	const HW_MBOX_Id_t mailBoxId, u32 *const pNumMsg)
{
	HW_STATUS status = RET_OK;

	/* Check input parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
		      RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_PARAM(pNumMsg,  NULL, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
		      RES_INVALID_INPUT_PARAM);

	CHECK_INPUT_RANGE_MIN0(mailBoxId, HW_MBOX_ID_MAX, RET_INVALID_ID,
			   RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);

	/* Get number of messages available for MailBox */
	*pNumMsg = MLBMAILBOX_MSGSTATUS___0_15NbOfMsgMBmRead32(baseAddress,
							  (u32)mailBoxId);

	return status;
}

/* Enables the specified IRQ. */
HW_STATUS HW_MBOX_EventEnable(const void __iomem *baseAddress,
				const HW_MBOX_Id_t mailBoxId,
				const HW_MBOX_UserId_t userId,
				const u32 events)
{
	HW_STATUS status = RET_OK;
	u32 irqEnableReg;

	/* Check input parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
			  RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(mailBoxId, HW_MBOX_ID_MAX, RET_INVALID_ID,
			 RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(enableIrq, HW_MBOX_INT_MAX, RET_INVALID_ID,
			 RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(userId, HW_MBOX_USER_MAX, RET_INVALID_ID,
			 RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);

	/* Get current enable status */
	irqEnableReg = MLBMAILBOX_IRQENABLE___0_3ReadRegister32(baseAddress,
							     (u32)userId);

	/* update enable value */
	irqEnableReg |= ((u32)(events)) << (((u32)(mailBoxId)) *
			HW_MBOX_ID_WIDTH);

	/* write new enable status */
	MLBMAILBOX_IRQENABLE___0_3WriteRegister32(baseAddress, (u32)userId,
						 (u32)irqEnableReg);

	mboxsetting.sysconfig = MLBMAILBOX_SYSCONFIGReadRegister32(baseAddress);
	/* Get current enable status */
	mboxsetting.irqEnable0 = MLBMAILBOX_IRQENABLE___0_3ReadRegister32
				(baseAddress, HW_MBOX_U0_ARM);
	mboxsetting.irqEnable1 = MLBMAILBOX_IRQENABLE___0_3ReadRegister32
				(baseAddress, HW_MBOX_U1_DSP1);
	return status;
}

/* Disables the specified IRQ. */
HW_STATUS HW_MBOX_EventDisable(const void __iomem *baseAddress,
				const HW_MBOX_Id_t mailBoxId,
				const HW_MBOX_UserId_t userId,
				const u32 events)
{
	HW_STATUS status = RET_OK;
	u32 irqDisableReg;

	/* Check input parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
		      RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(mailBoxId, HW_MBOX_ID_MAX, RET_INVALID_ID,
		     RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(disableIrq, HW_MBOX_INT_MAX, RET_INVALID_ID,
		     RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(userId, HW_MBOX_USER_MAX, RET_INVALID_ID,
		     RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);

	/* Get current enable status */
	irqDisableReg = MLBMAILBOX_IRQENABLE___0_3ReadRegister32(baseAddress,
		    (u32)userId);

	/* update enable value */
	irqDisableReg &= ~(((u32)(events)) << (((u32)(mailBoxId)) *
		     HW_MBOX_ID_WIDTH));

	/* write new enable status */
	MLBMAILBOX_IRQENABLE___0_3WriteRegister32(baseAddress, (u32)userId,
					     (u32)irqDisableReg);

	return status;
}

/* Sets the status of the specified IRQ. */
HW_STATUS HW_MBOX_EventAck(const void __iomem *baseAddress,
	const HW_MBOX_Id_t mailBoxId, const HW_MBOX_UserId_t userId,
	const u32 event)
{
	HW_STATUS status = RET_OK;
	u32 irqStatusReg;

	/* Check input parameters */
	CHECK_INPUT_PARAM(baseAddress,   0, RET_BAD_NULL_PARAM, RES_MBOX_BASE +
		      RES_INVALID_INPUT_PARAM);

	CHECK_INPUT_RANGE_MIN0(irqStatus, HW_MBOX_INT_MAX, RET_INVALID_ID,
		     RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(mailBoxId, HW_MBOX_ID_MAX, RET_INVALID_ID,
		     RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(userId, HW_MBOX_USER_MAX, RET_INVALID_ID,
		     RES_MBOX_BASE + RES_INVALID_INPUT_PARAM);

	/* calculate status to write */
	irqStatusReg = ((u32)event) << (((u32)(mailBoxId)) *
		   HW_MBOX_ID_WIDTH);

	/* clear Irq Status for specified mailbox/User Id */
	MLBMAILBOX_IRQSTATUS___0_3WriteRegister32(baseAddress, (u32)userId,
					     (u32)irqStatusReg);

	/*
	 * FIXME: Replace all this custom register access with standard
	 * __raw_read/write().
	 *
	 * FIXME: Replace all interrupt handlers with standard linux style
	 * interrupt handlers.
	 *
	 * FIXME: Replace direct access to PRCM registers with omap standard
	 * PRCM register access.
	 *
	 * Flush posted write for the irq status to avoid spurious interrupts.
	 */
	MLBMAILBOX_IRQSTATUS___0_3ReadRegister32(baseAddress, (u32)userId);

	return status;
}
