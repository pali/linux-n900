/*
 * MLBRegAcM.h
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

#ifndef _MLB_REG_ACM_H
#define _MLB_REG_ACM_H

#include <GlobalTypes.h>
#include <linux/io.h>
#include <EasiGlobal.h>
#include "MLBAccInt.h"

#if defined(USE_LEVEL_1_MACROS)

#define MLBMAILBOX_SYSCONFIGReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGReadRegister32),\
      __raw_readl(((baseAddress))+ \
      MLB_MAILBOX_SYSCONFIG_OFFSET))


#define MLBMAILBOX_SYSCONFIGWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MLB_MAILBOX_SYSCONFIG_OFFSET;\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGWriteRegister32);\
    __raw_writel(newValue, ((baseAddress))+offset);\
}


#define MLBMAILBOX_SYSCONFIGSIdleModeRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGSIdleModeRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
      (MLB_MAILBOX_SYSCONFIG_OFFSET)))) &\
      MLB_MAILBOX_SYSCONFIG_SIdleMode_MASK) >>\
      MLB_MAILBOX_SYSCONFIG_SIdleMode_OFFSET))


#define MLBMAILBOX_SYSCONFIGSIdleModeWrite32(baseAddress, value)\
{\
    const u32 offset = MLB_MAILBOX_SYSCONFIG_OFFSET;\
    register u32 data = __raw_readl(((u32)(baseAddress)) +\
			    offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGSIdleModeWrite32);\
    data &= ~(MLB_MAILBOX_SYSCONFIG_SIdleMode_MASK);\
    newValue <<= MLB_MAILBOX_SYSCONFIG_SIdleMode_OFFSET;\
    newValue &= MLB_MAILBOX_SYSCONFIG_SIdleMode_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define MLBMAILBOX_SYSCONFIGSoftResetWrite32(baseAddress, value)\
{\
    const u32 offset = MLB_MAILBOX_SYSCONFIG_OFFSET;\
    register u32 data =\
    __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGSoftResetWrite32);\
    data &= ~(MLB_MAILBOX_SYSCONFIG_SoftReset_MASK);\
    newValue <<= MLB_MAILBOX_SYSCONFIG_SoftReset_OFFSET;\
    newValue &= MLB_MAILBOX_SYSCONFIG_SoftReset_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define MLBMAILBOX_SYSCONFIGAutoIdleRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGAutoIdleRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
      (MLB_MAILBOX_SYSCONFIG_OFFSET)))) &\
      MLB_MAILBOX_SYSCONFIG_AutoIdle_MASK) >>\
      MLB_MAILBOX_SYSCONFIG_AutoIdle_OFFSET))


#define MLBMAILBOX_SYSCONFIGAutoIdleWrite32(baseAddress, value)\
{\
    const u32 offset = MLB_MAILBOX_SYSCONFIG_OFFSET;\
    register u32 data =\
    __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSCONFIGAutoIdleWrite32);\
    data &= ~(MLB_MAILBOX_SYSCONFIG_AutoIdle_MASK);\
    newValue <<= MLB_MAILBOX_SYSCONFIG_AutoIdle_OFFSET;\
    newValue &= MLB_MAILBOX_SYSCONFIG_AutoIdle_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define MLBMAILBOX_SYSSTATUSResetDoneRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_SYSSTATUSResetDoneRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
      (MLB_MAILBOX_SYSSTATUS_OFFSET)))) &\
      MLB_MAILBOX_SYSSTATUS_ResetDone_MASK) >>\
      MLB_MAILBOX_SYSSTATUS_ResetDone_OFFSET))


#define MLBMAILBOX_MESSAGE___0_15ReadRegister32(baseAddress, bank)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_MESSAGE___0_15ReadRegister32),\
      __raw_readl(((baseAddress))+\
      (MLB_MAILBOX_MESSAGE___REGSET_0_15_OFFSET +\
      MLB_MAILBOX_MESSAGE___0_15_OFFSET+(\
      (bank)*MLB_MAILBOX_MESSAGE___REGSET_0_15_STEP))))


#define MLBMAILBOX_MESSAGE___0_15WriteRegister32(baseAddress, bank, value)\
{\
    const u32 offset = MLB_MAILBOX_MESSAGE___REGSET_0_15_OFFSET +\
    MLB_MAILBOX_MESSAGE___0_15_OFFSET +\
    ((bank)*MLB_MAILBOX_MESSAGE___REGSET_0_15_STEP);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_MESSAGE___0_15WriteRegister32);\
    __raw_writel(newValue, ((baseAddress))+offset);\
}


#define MLBMAILBOX_FIFOSTATUS___0_15ReadRegister32(baseAddress, bank)\
    (_DEBUG_LEVEL_1_EASI(\
      EASIL1_MLBMAILBOX_FIFOSTATUS___0_15ReadRegister32),\
      __raw_readl(((u32)(baseAddress))+\
      (MLB_MAILBOX_FIFOSTATUS___REGSET_0_15_OFFSET +\
      MLB_MAILBOX_FIFOSTATUS___0_15_OFFSET+\
      ((bank)*MLB_MAILBOX_FIFOSTATUS___REGSET_0_15_STEP))))


#define MLBMAILBOX_FIFOSTATUS___0_15FifoFullMBmRead32(baseAddress, bank)\
    (_DEBUG_LEVEL_1_EASI(\
      EASIL1_MLBMAILBOX_FIFOSTATUS___0_15FifoFullMBmRead32),\
      (((__raw_readl(((baseAddress))+\
      (MLB_MAILBOX_FIFOSTATUS___REGSET_0_15_OFFSET +\
      MLB_MAILBOX_FIFOSTATUS___0_15_OFFSET+\
      ((bank)*MLB_MAILBOX_FIFOSTATUS___REGSET_0_15_STEP)))) &\
      MLB_MAILBOX_FIFOSTATUS___0_15_FifoFullMBm_MASK) >>\
      MLB_MAILBOX_FIFOSTATUS___0_15_FifoFullMBm_OFFSET))


#define MLBMAILBOX_MSGSTATUS___0_15NbOfMsgMBmRead32(baseAddress, bank)\
    (_DEBUG_LEVEL_1_EASI(\
      EASIL1_MLBMAILBOX_MSGSTATUS___0_15NbOfMsgMBmRead32),\
      (((__raw_readl(((baseAddress))+\
      (MLB_MAILBOX_MSGSTATUS___REGSET_0_15_OFFSET +\
      MLB_MAILBOX_MSGSTATUS___0_15_OFFSET+\
      ((bank)*MLB_MAILBOX_MSGSTATUS___REGSET_0_15_STEP)))) &\
      MLB_MAILBOX_MSGSTATUS___0_15_NbOfMsgMBm_MASK) >>\
      MLB_MAILBOX_MSGSTATUS___0_15_NbOfMsgMBm_OFFSET))


#define MLBMAILBOX_IRQSTATUS___0_3ReadRegister32(baseAddress, bank)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_IRQSTATUS___0_3ReadRegister32),\
      __raw_readl(((baseAddress))+\
      (MLB_MAILBOX_IRQSTATUS___REGSET_0_3_OFFSET +\
      MLB_MAILBOX_IRQSTATUS___0_3_OFFSET+\
      ((bank)*MLB_MAILBOX_IRQSTATUS___REGSET_0_3_STEP))))


#define MLBMAILBOX_IRQSTATUS___0_3WriteRegister32(baseAddress, bank, value)\
{\
    const u32 offset = MLB_MAILBOX_IRQSTATUS___REGSET_0_3_OFFSET +\
    MLB_MAILBOX_IRQSTATUS___0_3_OFFSET +\
    ((bank)*MLB_MAILBOX_IRQSTATUS___REGSET_0_3_STEP);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_IRQSTATUS___0_3WriteRegister32);\
    __raw_writel(newValue, ((baseAddress))+offset);\
}


#define MLBMAILBOX_IRQENABLE___0_3ReadRegister32(baseAddress, bank)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_IRQENABLE___0_3ReadRegister32),\
      __raw_readl(((baseAddress))+\
      (MLB_MAILBOX_IRQENABLE___REGSET_0_3_OFFSET +\
      MLB_MAILBOX_IRQENABLE___0_3_OFFSET+\
       ((bank)*MLB_MAILBOX_IRQENABLE___REGSET_0_3_STEP))))


#define MLBMAILBOX_IRQENABLE___0_3WriteRegister32(baseAddress, bank, value)\
{\
    const u32 offset = MLB_MAILBOX_IRQENABLE___REGSET_0_3_OFFSET +\
      MLB_MAILBOX_IRQENABLE___0_3_OFFSET +\
      ((bank)*MLB_MAILBOX_IRQENABLE___REGSET_0_3_STEP);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_MLBMAILBOX_IRQENABLE___0_3WriteRegister32);\
    __raw_writel(newValue, ((baseAddress))+offset);\
}


#endif	/* USE_LEVEL_1_MACROS */

#endif /* _MLB_REG_ACM_H */
