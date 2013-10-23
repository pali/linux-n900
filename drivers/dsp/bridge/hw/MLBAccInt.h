/*
 * MLBAccInt.h
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


#ifndef _MLB_ACC_INT_H
#define _MLB_ACC_INT_H

/* Mappings of level 1 EASI function numbers to function names */

#define EASIL1_MLBMAILBOX_SYSCONFIGReadRegister32   (MLB_BASE_EASIL1 + 3)
#define EASIL1_MLBMAILBOX_SYSCONFIGWriteRegister32  (MLB_BASE_EASIL1 + 4)
#define EASIL1_MLBMAILBOX_SYSCONFIGSIdleModeRead32   (MLB_BASE_EASIL1 + 7)
#define EASIL1_MLBMAILBOX_SYSCONFIGSIdleModeWrite32  (MLB_BASE_EASIL1 + 17)
#define EASIL1_MLBMAILBOX_SYSCONFIGSoftResetWrite32 (MLB_BASE_EASIL1 + 29)
#define EASIL1_MLBMAILBOX_SYSCONFIGAutoIdleRead32 \
						(MLB_BASE_EASIL1 + 33)
#define EASIL1_MLBMAILBOX_SYSCONFIGAutoIdleWrite32   (MLB_BASE_EASIL1 + 39)
#define EASIL1_MLBMAILBOX_SYSSTATUSResetDoneRead32  (MLB_BASE_EASIL1 + 44)
#define EASIL1_MLBMAILBOX_MESSAGE___0_15ReadRegister32 \
						(MLB_BASE_EASIL1 + 50)
#define EASIL1_MLBMAILBOX_MESSAGE___0_15WriteRegister32  \
						(MLB_BASE_EASIL1 + 51)
#define EASIL1_MLBMAILBOX_FIFOSTATUS___0_15ReadRegister32  \
						(MLB_BASE_EASIL1 + 56)
#define EASIL1_MLBMAILBOX_FIFOSTATUS___0_15FifoFullMBmRead32 \
						(MLB_BASE_EASIL1 + 57)
#define EASIL1_MLBMAILBOX_MSGSTATUS___0_15NbOfMsgMBmRead32  \
						(MLB_BASE_EASIL1 + 60)
#define EASIL1_MLBMAILBOX_IRQSTATUS___0_3ReadRegister32  \
						(MLB_BASE_EASIL1 + 62)
#define EASIL1_MLBMAILBOX_IRQSTATUS___0_3WriteRegister32 \
						(MLB_BASE_EASIL1 + 63)
#define EASIL1_MLBMAILBOX_IRQENABLE___0_3ReadRegister32    \
						(MLB_BASE_EASIL1 + 192)
#define EASIL1_MLBMAILBOX_IRQENABLE___0_3WriteRegister32   \
						(MLB_BASE_EASIL1 + 193)

/* Register set MAILBOX_MESSAGE___REGSET_0_15 address offset, bank address
 * increment and number of banks */

#define MLB_MAILBOX_MESSAGE___REGSET_0_15_OFFSET    (u32)(0x0040)
#define MLB_MAILBOX_MESSAGE___REGSET_0_15_STEP   (u32)(0x0004)

/* Register offset address definitions relative to register set
 * MAILBOX_MESSAGE___REGSET_0_15 */

#define MLB_MAILBOX_MESSAGE___0_15_OFFSET   (u32)(0x0)


/* Register set MAILBOX_FIFOSTATUS___REGSET_0_15 address offset, bank address
 * increment and number of banks */

#define MLB_MAILBOX_FIFOSTATUS___REGSET_0_15_OFFSET  (u32)(0x0080)
#define MLB_MAILBOX_FIFOSTATUS___REGSET_0_15_STEP   (u32)(0x0004)

/* Register offset address definitions relative to register set
 * MAILBOX_FIFOSTATUS___REGSET_0_15 */

#define MLB_MAILBOX_FIFOSTATUS___0_15_OFFSET    (u32)(0x0)


/* Register set MAILBOX_MSGSTATUS___REGSET_0_15 address offset, bank address
 * increment and number of banks */

#define MLB_MAILBOX_MSGSTATUS___REGSET_0_15_OFFSET  (u32)(0x00c0)
#define MLB_MAILBOX_MSGSTATUS___REGSET_0_15_STEP    (u32)(0x0004)

/* Register offset address definitions relative to register set
 * MAILBOX_MSGSTATUS___REGSET_0_15 */

#define MLB_MAILBOX_MSGSTATUS___0_15_OFFSET    (u32)(0x0)


/* Register set MAILBOX_IRQSTATUS___REGSET_0_3 address offset, bank address
 * increment and number of banks */

#define MLB_MAILBOX_IRQSTATUS___REGSET_0_3_OFFSET        (u32)(0x0100)
#define MLB_MAILBOX_IRQSTATUS___REGSET_0_3_STEP          (u32)(0x0008)

/* Register offset address definitions relative to register set
 * MAILBOX_IRQSTATUS___REGSET_0_3 */

#define MLB_MAILBOX_IRQSTATUS___0_3_OFFSET        (u32)(0x0)


/* Register set MAILBOX_IRQENABLE___REGSET_0_3 address offset, bank address
 * increment and number of banks */

#define MLB_MAILBOX_IRQENABLE___REGSET_0_3_OFFSET     (u32)(0x0104)
#define MLB_MAILBOX_IRQENABLE___REGSET_0_3_STEP     (u32)(0x0008)

/* Register offset address definitions relative to register set
 * MAILBOX_IRQENABLE___REGSET_0_3 */

#define MLB_MAILBOX_IRQENABLE___0_3_OFFSET          (u32)(0x0)


/* Register offset address definitions */

#define MLB_MAILBOX_SYSCONFIG_OFFSET            (u32)(0x10)
#define MLB_MAILBOX_SYSSTATUS_OFFSET            (u32)(0x14)


/* Bitfield mask and offset declarations */

#define MLB_MAILBOX_SYSCONFIG_SIdleMode_MASK        (u32)(0x18)
#define MLB_MAILBOX_SYSCONFIG_SIdleMode_OFFSET      (u32)(3)
#define MLB_MAILBOX_SYSCONFIG_SoftReset_MASK        (u32)(0x2)
#define MLB_MAILBOX_SYSCONFIG_SoftReset_OFFSET      (u32)(1)
#define MLB_MAILBOX_SYSCONFIG_AutoIdle_MASK          (u32)(0x1)
#define MLB_MAILBOX_SYSCONFIG_AutoIdle_OFFSET        (u32)(0)
#define MLB_MAILBOX_SYSSTATUS_ResetDone_MASK         (u32)(0x1)
#define MLB_MAILBOX_SYSSTATUS_ResetDone_OFFSET         (u32)(0)
#define MLB_MAILBOX_FIFOSTATUS___0_15_FifoFullMBm_MASK   (u32)(0x1)
#define MLB_MAILBOX_FIFOSTATUS___0_15_FifoFullMBm_OFFSET  (u32)(0)
#define MLB_MAILBOX_MSGSTATUS___0_15_NbOfMsgMBm_MASK    (u32)(0x7f)
#define MLB_MAILBOX_MSGSTATUS___0_15_NbOfMsgMBm_OFFSET    (u32)(0)

#endif /* _MLB_ACC_INT_H */
