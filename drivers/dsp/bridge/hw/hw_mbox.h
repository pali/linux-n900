/*
 * hw_mbox.h
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
 *  ======== hw_mbox.h ========
 *  Description:
 *      HW Mailbox API and types definitions
 *
 *! Revision History:
 *! ================
 *! 16 Feb 2003 sb: Initial version
 */
#ifndef __MBOX_H
#define __MBOX_H

/* Bitmasks for Mailbox interrupt sources */
#define HW_MBOX_INT_NEW_MSG    0x1
#define HW_MBOX_INT_NOT_FULL   0x2
#define HW_MBOX_INT_ALL	0x3

/* Maximum number of messages that mailbox can hald at a time. */
#define HW_MBOX_MAX_NUM_MESSAGES   4

/* HW_MBOX_Id_t: Enumerated Type used to specify Mailbox Sub Module Id Number */
typedef enum HW_MBOX_Id_label {
    HW_MBOX_ID_0,
    HW_MBOX_ID_1,
    HW_MBOX_ID_2,
    HW_MBOX_ID_3,
    HW_MBOX_ID_4,
    HW_MBOX_ID_5

} HW_MBOX_Id_t, *pHW_MBOX_Id_t;

/* HW_MBOX_UserId_t:  Enumerated Type used to specify Mail box User Id */
typedef enum HW_MBOX_UserId_label {
    HW_MBOX_U0_ARM,
    HW_MBOX_U1_DSP1,
    HW_MBOX_U2_DSP2,
    HW_MBOX_U3_ARM

} HW_MBOX_UserId_t, *pHW_MBOX_UserId_t;

/* Mailbox context settings */
struct MAILBOX_CONTEXT {
	u32 sysconfig;
	u32 irqEnable0;
	u32 irqEnable1;
};

/*
* FUNCTION      : HW_MBOX_MsgRead
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*   Description : Base Address of instance of Mailbox module
*
*   Identifier  : mailBoxId
*   Type	: const HW_MBOX_Id_t
*   Description : Mail Box Sub module Id to read
*
* OUTPUTS:
*
*   Identifier  : pReadValue
*   Type	: u32 *const
*   Description : Value read from MailBox
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  Address/ptr Paramater was set to 0/NULL
*		 RET_INVALID_ID      Invalid Id used
*		 RET_EMPTY	   Mailbox empty
*
* PURPOSE:      : this function reads a u32 from the sub module message
*		 box Specified. if there are no messages in the mailbox
*		 then and error is returned.
*/
extern HW_STATUS HW_MBOX_MsgRead(const void __iomem *baseAddress,
				const HW_MBOX_Id_t mailBoxId,
				u32 *const pReadValue);

/*
* FUNCTION      : HW_MBOX_MsgWrite
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*   Description : Base Address of instance of Mailbox module
*
*   Identifier  : mailBoxId
*   Type	: const HW_MBOX_Id_t
*   Description : Mail Box Sub module Id to write
*
*   Identifier  : writeValue
*   Type	: const u32
*   Description : Value to write to MailBox
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  Address/pointer Paramater was set to 0/NULL
*		 RET_INVALID_ID      Invalid Id used
*
* PURPOSE:      : this function writes a u32 from the sub module message
*		 box Specified.
*/
extern HW_STATUS HW_MBOX_MsgWrite(
		      const void __iomem *baseAddress,
		      const HW_MBOX_Id_t   mailBoxId,
		      const u32	 writeValue
		  );

/*
* FUNCTION      : HW_MBOX_NumMsgGet
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*   Description : Base Address of instance of Mailbox module
*
*   Identifier  : mailBoxId
*   Type	: const HW_MBOX_Id_t
*   Description : Mail Box Sub module Id to get num messages
*
* OUTPUTS:
*
*   Identifier  : pNumMsg
*   Type	: u32 *const
*   Description : Number of messages in mailbox
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  Address/pointer Paramater was set to 0/NULL
*		 RET_INVALID_ID      Inavlid ID input at parameter
*
* PURPOSE:      : this function gets number of messages in a specified mailbox.
*/
extern HW_STATUS HW_MBOX_NumMsgGet(
		      const void	 __iomem *baseAddress,
		      const HW_MBOX_Id_t   mailBoxId,
		      u32 *const	pNumMsg
		  );

/*
* FUNCTION      : HW_MBOX_EventEnable
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*		 RET_BAD_NULL_PARAM  Address/pointer Paramater was set to 0/NULL
*
*   Identifier  : mailBoxId
*   Type	: const HW_MBOX_Id_t
*   Description : Mail Box Sub module Id to enable
*
*   Identifier  : userId
*   Type	: const HW_MBOX_UserId_t
*   Description : Mail box User Id to enable
*
*   Identifier  : enableIrq
*   Type	: const u32
*   Description : Irq value to enable
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  A Pointer Paramater was set to NULL
*		 RET_INVALID_ID      Invalid Id used
*
* PURPOSE:      : this function enables the specified IRQ.
*/
extern HW_STATUS HW_MBOX_EventEnable(
		      const void __iomem *baseAddress,
		      const HW_MBOX_Id_t       mailBoxId,
		      const HW_MBOX_UserId_t   userId,
		      const u32	     events
		  );

/*
* FUNCTION      : HW_MBOX_EventDisable
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*		 RET_BAD_NULL_PARAM  Address/pointer Paramater was set to 0/NULL
*
*   Identifier  : mailBoxId
*   Type	: const HW_MBOX_Id_t
*   Description : Mail Box Sub module Id to disable
*
*   Identifier  : userId
*   Type	: const HW_MBOX_UserId_t
*   Description : Mail box User Id to disable
*
*   Identifier  : enableIrq
*   Type	: const u32
*   Description : Irq value to disable
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  A Pointer Paramater was set to NULL
*		 RET_INVALID_ID      Invalid Id used
*
* PURPOSE:      : this function disables the specified IRQ.
*/
extern HW_STATUS HW_MBOX_EventDisable(
		      const void __iomem *baseAddress,
		      const HW_MBOX_Id_t       mailBoxId,
		      const HW_MBOX_UserId_t   userId,
		      const u32	     events
		  );

/*
* FUNCTION      : HW_MBOX_EventAck
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*   Description : Base Address of instance of Mailbox module
*
*   Identifier  : mailBoxId
*   Type	: const HW_MBOX_Id_t
*   Description : Mail Box Sub module Id to set
*
*   Identifier  : userId
*   Type	: const HW_MBOX_UserId_t
*   Description : Mail box User Id to set
*
*   Identifier  : irqStatus
*   Type	: const u32
*   Description : The value to write IRQ status
*
* OUTPUTS:
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  Address Paramater was set to 0
*		 RET_INVALID_ID      Invalid Id used
*
* PURPOSE:      : this function sets the status of the specified IRQ.
*/
extern HW_STATUS HW_MBOX_EventAck(
		      const void	__iomem *baseAddress,
		      const HW_MBOX_Id_t	mailBoxId,
		      const HW_MBOX_UserId_t    userId,
		      const u32	      event
		  );

/*
* FUNCTION      : HW_MBOX_saveSettings
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*   Description : Base Address of instance of Mailbox module
*
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  Address/pointer Paramater was set to 0/NULL
*		 RET_INVALID_ID      Invalid Id used
*		 RET_EMPTY	   Mailbox empty
*
* PURPOSE:      : this function saves the context of mailbox
*/
extern HW_STATUS HW_MBOX_saveSettings(void __iomem *baseAddres);

/*
* FUNCTION      : HW_MBOX_restoreSettings
*
* INPUTS:
*
*   Identifier  : baseAddress
*   Type	: const u32
*   Description : Base Address of instance of Mailbox module
*
*
* RETURNS:
*
*   Type	: ReturnCode_t
*   Description : RET_OK	      No errors occured
*		 RET_BAD_NULL_PARAM  Address/pointer Paramater was set to 0/NULL
*		 RET_INVALID_ID      Invalid Id used
*		 RET_EMPTY	   Mailbox empty
*
* PURPOSE:      : this function restores the context of mailbox
*/
extern HW_STATUS HW_MBOX_restoreSettings(void __iomem *baseAddres);

static inline void HW_MBOX_initSettings(void __iomem *baseAddres)
{
	HW_MBOX_restoreSettings(baseAddres);
}

#endif  /* __MBOX_H */
