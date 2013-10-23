/*
 * MMURegAcM.h
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


#ifndef _MMU_REG_ACM_H
#define _MMU_REG_ACM_H

#include <GlobalTypes.h>
#include <linux/io.h>
#include <EasiGlobal.h>

#include "MMUAccInt.h"

#if defined(USE_LEVEL_1_MACROS)


#define MMUMMU_SYSCONFIGReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_SYSCONFIGReadRegister32),\
      __raw_readl((baseAddress)+MMU_MMU_SYSCONFIG_OFFSET))


#define MMUMMU_SYSCONFIGIdleModeWrite32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_SYSCONFIG_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_SYSCONFIGIdleModeWrite32);\
    data &= ~(MMU_MMU_SYSCONFIG_IdleMode_MASK);\
    newValue <<= MMU_MMU_SYSCONFIG_IdleMode_OFFSET;\
    newValue &= MMU_MMU_SYSCONFIG_IdleMode_MASK;\
    newValue |= data;\
    __raw_writel(newValue, baseAddress+offset);\
}


#define MMUMMU_SYSCONFIGAutoIdleWrite32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_SYSCONFIG_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_SYSCONFIGAutoIdleWrite32);\
    data &= ~(MMU_MMU_SYSCONFIG_AutoIdle_MASK);\
    newValue <<= MMU_MMU_SYSCONFIG_AutoIdle_OFFSET;\
    newValue &= MMU_MMU_SYSCONFIG_AutoIdle_MASK;\
    newValue |= data;\
    __raw_writel(newValue, baseAddress+offset);\
}


#define MMUMMU_IRQSTATUSReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_IRQSTATUSReadRegister32),\
      __raw_readl((baseAddress)+MMU_MMU_IRQSTATUS_OFFSET))


#define MMUMMU_IRQSTATUSWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_IRQSTATUS_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_IRQSTATUSWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_IRQENABLEReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_IRQENABLEReadRegister32),\
      __raw_readl((baseAddress)+MMU_MMU_IRQENABLE_OFFSET))


#define MMUMMU_IRQENABLEWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_IRQENABLE_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_IRQENABLEWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_WALKING_STTWLRunningRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_WALKING_STTWLRunningRead32),\
      (((__raw_readl(((baseAddress)+(MMU_MMU_WALKING_ST_OFFSET))))\
      & MMU_MMU_WALKING_ST_TWLRunning_MASK) >>\
      MMU_MMU_WALKING_ST_TWLRunning_OFFSET))


#define MMUMMU_CNTLTWLEnableRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_CNTLTWLEnableRead32),\
      (((__raw_readl(((baseAddress)+(MMU_MMU_CNTL_OFFSET)))) &\
      MMU_MMU_CNTL_TWLEnable_MASK) >>\
      MMU_MMU_CNTL_TWLEnable_OFFSET))


#define MMUMMU_CNTLTWLEnableWrite32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_CNTL_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_CNTLTWLEnableWrite32);\
    data &= ~(MMU_MMU_CNTL_TWLEnable_MASK);\
    newValue <<= MMU_MMU_CNTL_TWLEnable_OFFSET;\
    newValue &= MMU_MMU_CNTL_TWLEnable_MASK;\
    newValue |= data;\
    __raw_writel(newValue, baseAddress+offset);\
}


#define MMUMMU_CNTLMMUEnableWrite32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_CNTL_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_CNTLMMUEnableWrite32);\
    data &= ~(MMU_MMU_CNTL_MMUEnable_MASK);\
    newValue <<= MMU_MMU_CNTL_MMUEnable_OFFSET;\
    newValue &= MMU_MMU_CNTL_MMUEnable_MASK;\
    newValue |= data;\
    __raw_writel(newValue, baseAddress+offset);\
}


#define MMUMMU_FAULT_ADReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_FAULT_ADReadRegister32),\
      __raw_readl((baseAddress)+MMU_MMU_FAULT_AD_OFFSET))


#define MMUMMU_TTBWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_TTB_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_TTBWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_LOCKReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKReadRegister32),\
      __raw_readl((baseAddress)+MMU_MMU_LOCK_OFFSET))


#define MMUMMU_LOCKWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_LOCK_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_LOCKBaseValueRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKBaseValueRead32),\
      (((__raw_readl(((baseAddress)+(MMU_MMU_LOCK_OFFSET)))) &\
      MMU_MMU_LOCK_BaseValue_MASK) >>\
      MMU_MMU_LOCK_BaseValue_OFFSET))


#define MMUMMU_LOCKBaseValueWrite32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_LOCK_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKBaseValueWrite32);\
    data &= ~(MMU_MMU_LOCK_BaseValue_MASK);\
    newValue <<= MMU_MMU_LOCK_BaseValue_OFFSET;\
    newValue &= MMU_MMU_LOCK_BaseValue_MASK;\
    newValue |= data;\
    __raw_writel(newValue, baseAddress+offset);\
}


#define MMUMMU_LOCKCurrentVictimRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKCurrentVictimRead32),\
      (((__raw_readl(((baseAddress)+(MMU_MMU_LOCK_OFFSET)))) &\
      MMU_MMU_LOCK_CurrentVictim_MASK) >>\
      MMU_MMU_LOCK_CurrentVictim_OFFSET))


#define MMUMMU_LOCKCurrentVictimWrite32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_LOCK_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKCurrentVictimWrite32);\
    data &= ~(MMU_MMU_LOCK_CurrentVictim_MASK);\
    newValue <<= MMU_MMU_LOCK_CurrentVictim_OFFSET;\
    newValue &= MMU_MMU_LOCK_CurrentVictim_MASK;\
    newValue |= data;\
    __raw_writel(newValue, baseAddress+offset);\
}


#define MMUMMU_LOCKCurrentVictimSet32(var, value)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LOCKCurrentVictimSet32),\
      (((var) & ~(MMU_MMU_LOCK_CurrentVictim_MASK)) |\
      (((value) << MMU_MMU_LOCK_CurrentVictim_OFFSET) &\
      MMU_MMU_LOCK_CurrentVictim_MASK)))


#define MMUMMU_LD_TLBReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LD_TLBReadRegister32),\
      __raw_readl((baseAddress)+MMU_MMU_LD_TLB_OFFSET))


#define MMUMMU_LD_TLBWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_LD_TLB_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_LD_TLBWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_CAMWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_CAM_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_CAMWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_RAMWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_RAM_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_RAMWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define MMUMMU_FLUSH_ENTRYWriteRegister32(baseAddress, value)\
{\
    const u32 offset = MMU_MMU_FLUSH_ENTRY_OFFSET;\
    register u32 newValue = (value);\
    _DEBUG_LEVEL_1_EASI(EASIL1_MMUMMU_FLUSH_ENTRYWriteRegister32);\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#endif	/* USE_LEVEL_1_MACROS */

#endif /* _MMU_REG_ACM_H */
