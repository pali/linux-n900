/*
 * PRCMRegAcM.h
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

#ifndef _PRCM_REG_ACM_H
#define _PRCM_REG_ACM_H

#include <GlobalTypes.h>
#include <linux/io.h>

#include <EasiGlobal.h>

#include "PRCMAccInt.h"

#if defined(USE_LEVEL_1_MACROS)

#define PRCMPRCM_CLKCFG_CTRLValid_configWriteClk_valid32(baseAddress)\
{\
    const u32 offset = PRCM_PRCM_CLKCFG_CTRL_OFFSET;\
    const u32 newValue = \
	(u32)PRCMPRCM_CLKCFG_CTRLValid_configClk_valid <<\
      PRCM_PRCM_CLKCFG_CTRL_Valid_config_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(\
      EASIL1_PRCMPRCM_CLKCFG_CTRLValid_configWriteClk_valid32);\
    data &= ~(PRCM_PRCM_CLKCFG_CTRL_Valid_config_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define CM_FCLKEN_PERReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_FCLKEN1_COREReadRegister32),\
      __raw_readl(((u32)(baseAddress))+CM_FCLKEN_PER_OFFSET))


#define CM_ICLKEN_PERReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_FCLKEN1_COREReadRegister32),\
      __raw_readl(((u32)(baseAddress))+CM_ICLKEN_PER_OFFSET))


#define CM_FCLKEN_PER_GPT5WriteRegister32(baseAddress,value)\
{\
    const u32 offset = CM_FCLKEN_PER_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_CM_FCLKEN_PER_GPT5WriteRegister32);\
   data &= ~(CM_FCLKEN_PER_GPT5_MASK);\
   newValue <<= CM_FCLKEN_PER_GPT5_OFFSET;\
   newValue &= CM_FCLKEN_PER_GPT5_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}


#define CM_FCLKEN_PER_GPT6WriteRegister32(baseAddress,value)\
{\
    const u32 offset = CM_FCLKEN_PER_OFFSET;\
    register u32 data =\
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_CM_FCLKEN_PER_GPT5WriteRegister32);\
   data &= ~(CM_FCLKEN_PER_GPT6_MASK);\
   newValue <<= CM_FCLKEN_PER_GPT6_OFFSET;\
   newValue &= CM_FCLKEN_PER_GPT6_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}


#define CM_ICLKEN_PER_GPT5WriteRegister32(baseAddress,value)\
{\
    const u32 offset = CM_ICLKEN_PER_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_CM_ICLKEN_PER_GPT5WriteRegister32);\
   data &= ~(CM_ICLKEN_PER_GPT5_MASK);\
   newValue <<= CM_ICLKEN_PER_GPT5_OFFSET;\
   newValue &= CM_ICLKEN_PER_GPT5_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}


#define CM_ICLKEN_PER_GPT6WriteRegister32(baseAddress,value)\
{\
    const u32 offset = CM_ICLKEN_PER_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_CM_ICLKEN_PER_GPT5WriteRegister32);\
   data &= ~(CM_ICLKEN_PER_GPT6_MASK);\
   newValue <<= CM_ICLKEN_PER_GPT6_OFFSET;\
   newValue &= CM_ICLKEN_PER_GPT6_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}


#define CM_FCLKEN1_COREReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_FCLKEN1_COREReadRegister32),\
      __raw_readl(((u32)(baseAddress))+CM_FCLKEN1_CORE_OFFSET))


#define PRCMCM_FCLKEN1_COREEN_GPT8Write32(baseAddress,value)\
{\
    const u32 offset = PRCM_CM_FCLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_FCLKEN1_COREEN_GPT8Write32);\
    data &= ~(PRCM_CM_FCLKEN1_CORE_EN_GPT8_MASK);\
    newValue <<= PRCM_CM_FCLKEN1_CORE_EN_GPT8_OFFSET;\
    newValue &= PRCM_CM_FCLKEN1_CORE_EN_GPT8_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_FCLKEN1_COREEN_GPT7Write32(baseAddress,value)\
{\
    const u32 offset = PRCM_CM_FCLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_FCLKEN1_COREEN_GPT7Write32);\
    data &= ~(PRCM_CM_FCLKEN1_CORE_EN_GPT7_MASK);\
    newValue <<= PRCM_CM_FCLKEN1_CORE_EN_GPT7_OFFSET;\
    newValue &= PRCM_CM_FCLKEN1_CORE_EN_GPT7_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define CM_ICLKEN1_COREReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_ICLKEN1_COREReadRegister32),\
      __raw_readl(((u32)(baseAddress))+CM_ICLKEN1_CORE_OFFSET))


#define  CM_ICLKEN1_COREEN_MAILBOXESWrite32(baseAddress, value)\
{\
    const u32 offset = CM_ICLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_ICLKEN1_COREEN_MAILBOXESWrite32);\
    data &= ~(CM_ICLKEN1_CORE_EN_MAILBOXES_MASK);\
    newValue <<= CM_ICLKEN1_CORE_EN_MAILBOXES_OFFSET;\
    newValue &= CM_ICLKEN1_CORE_EN_MAILBOXES_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_ICLKEN1_COREEN_GPT8Write32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_ICLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_ICLKEN1_COREEN_GPT8Write32);\
    data &= ~(PRCM_CM_ICLKEN1_CORE_EN_GPT8_MASK);\
    newValue <<= PRCM_CM_ICLKEN1_CORE_EN_GPT8_OFFSET;\
    newValue &= PRCM_CM_ICLKEN1_CORE_EN_GPT8_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_ICLKEN1_COREEN_GPT7Write32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_ICLKEN1_CORE_OFFSET;\
    register u32 data =\
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_ICLKEN1_COREEN_GPT7Write32);\
    data &= ~(PRCM_CM_ICLKEN1_CORE_EN_GPT7_MASK);\
    newValue <<= PRCM_CM_ICLKEN1_CORE_EN_GPT7_OFFSET;\
    newValue &= PRCM_CM_ICLKEN1_CORE_EN_GPT7_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT8Write32k32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT832k <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8Write32k32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT8WriteSys32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT8Sys <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8WriteSys32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT8WriteExt32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT8Ext <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8WriteExt32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT7Write32k32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT732k <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7Write32k32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT7WriteSys32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT7Sys <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7WriteSys32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT7WriteExt32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT7Ext <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7WriteExt32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT6WriteSys32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT6Sys <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT6WriteSys32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT6WriteExt32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT6Ext <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT6WriteExt32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define CM_CLKSEL_PER_GPT5Write32k32(baseAddress)\
{\
    const u32 offset = CM_CLKSEL_PER_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT532k <<\
      CM_CLKSEL_PER_GPT5_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_CM_CLKSEL_PER_GPT5Write32k32);\
    data &= ~(CM_CLKSEL_PER_GPT5_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define CM_CLKSEL_PER_GPT6Write32k32(baseAddress)\
{\
    const u32 offset = CM_CLKSEL_PER_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT532k <<\
      CM_CLKSEL_PER_GPT6_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_CM_CLKSEL_PER_GPT6Write32k32);\
    data &= ~(CM_CLKSEL_PER_GPT6_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT5WriteSys32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT5Sys <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT5WriteSys32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL2_CORECLKSEL_GPT5WriteExt32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT5Ext <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT5WriteExt32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL1_PLLAPLLs_ClkinRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL1_PLLAPLLs_ClkinRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_CM_CLKSEL1_PLL_OFFSET)))) &\
      PRCM_CM_CLKSEL1_PLL_APLLs_Clkin_MASK) >>\
      PRCM_CM_CLKSEL1_PLL_APLLs_Clkin_OFFSET))


#define CM_FCLKEN_IVA2EN_DSPWrite32(baseAddress,value)\
{\
    const u32 offset = CM_FCLKEN_IVA2_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_FCLKEN_DSPEN_DSPWrite32);\
    data &= ~(CM_FCLKEN_IVA2_EN_MASK);\
    newValue <<= CM_FCLKEN_IVA2_EN_OFFSET;\
    newValue &= CM_FCLKEN_IVA2_EN_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_ICLKEN_DSPEN_DSP_IPIWrite32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_ICLKEN_DSP_OFFSET;\
    register u32 data = \
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_ICLKEN_DSPEN_DSP_IPIWrite32);\
    data &= ~(PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_MASK);\
    newValue <<= PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_OFFSET;\
    newValue &= PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_IDLEST_DSPReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_IDLEST_DSPReadRegister32),\
      __raw_readl(((u32)(baseAddress))+PRCM_CM_IDLEST_DSP_OFFSET))


#define PRCMCM_IDLEST_DSPST_IPIRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_IDLEST_DSPST_IPIRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_CM_IDLEST_DSP_OFFSET)))) &\
      PRCM_CM_IDLEST_DSP_ST_IPI_MASK) >>\
      PRCM_CM_IDLEST_DSP_ST_IPI_OFFSET))


#define PRM_IDLEST_IVA2ST_IVA2Read32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_IDLEST_DSPST_DSPRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
	  (CM_IDLEST_IVA2_OFFSET)))) &\
      CM_IDLEST_IVA2_ST_IVA2_MASK) >>\
      CM_IDLEST_IVA2_ST_IVA2_OFFSET))


#define PRCMCM_AUTOIDLE_DSPAUTO_DSP_IPIWrite32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_AUTOIDLE_DSP_OFFSET;\
    register u32 data =\
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_AUTOIDLE_DSPAUTO_DSP_IPIWrite32);\
    data &= ~(PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_MASK);\
    newValue <<= PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_OFFSET;\
    newValue &= PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL_DSPSYNC_DSPWrite32(baseAddress,value)\
{\
    const u32 offset = PRCM_CM_CLKSEL_DSP_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL_DSPSYNC_DSPWrite32);\
    data &= ~(PRCM_CM_CLKSEL_DSP_SYNC_DSP_MASK);\
    newValue <<= PRCM_CM_CLKSEL_DSP_SYNC_DSP_OFFSET;\
    newValue &= PRCM_CM_CLKSEL_DSP_SYNC_DSP_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL_DSPCLKSEL_DSP_IFWrite32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSEL_DSP_OFFSET;\
    register u32 data = \
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL_DSPCLKSEL_DSP_IFWrite32);\
    data &= ~(PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_MASK);\
    newValue <<= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_OFFSET;\
    newValue &= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSEL_DSPCLKSEL_DSPWrite32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSEL_DSP_OFFSET;\
    register u32 data = \
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSEL_DSPCLKSEL_DSPWrite32);\
    data &= ~(PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_MASK);\
    newValue <<= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_OFFSET;\
    newValue &= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMCM_CLKSTCTRL_IVA2WriteRegister32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSTCTRL_IVA2_OFFSET;\
    register u32 data = \
      __raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSTCTRL_IVA2WriteRegister32);\
    data &= ~(CM_CLKSTCTRL_IVA2_MASK);\
    newValue <<= CM_CLKSTCTRL_IVA2_OFFSET;\
    newValue &= CM_CLKSTCTRL_IVA2_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define PRCMCM_CLKSTCTRL_DSPAutostate_DSPRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSTCTRL_DSPAutostate_DSPRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_CM_CLKSTCTRL_DSP_OFFSET)))) &\
      PRCM_CM_CLKSTCTRL_DSP_Autostate_DSP_MASK) >>\
      PRCM_CM_CLKSTCTRL_DSP_Autostate_DSP_OFFSET))


#define PRCMCM_CLKSTCTRL_DSPAutostate_DSPWrite32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSTCTRL_DSP_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMCM_CLKSTCTRL_DSPAutostate_DSPWrite32);\
    data &= ~(PRCM_CM_CLKSTCTRL_DSP_Autostate_DSP_MASK);\
    newValue <<= PRCM_CM_CLKSTCTRL_DSP_Autostate_DSP_OFFSET;\
    newValue &= PRCM_CM_CLKSTCTRL_DSP_Autostate_DSP_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMRM_RSTCTRL_DSPReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPReadRegister32),\
      __raw_readl(((baseAddress))+PRCM_RM_RSTCTRL_DSP_OFFSET))


#define PRM_RSTCTRL_IVA2RST1_DSPWrite32(baseAddress,value)\
{\
    const u32 offset = PRM_RSTCTRL_IVA2_OFFSET;\
    register u32 data =\
    __raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSPWrite32);\
    data &= ~(PRM_RSTCTRL_IVA2_RST1_MASK);\
    newValue <<= PRM_RSTCTRL_IVA2_RST1_OFFSET;\
    newValue &= PRM_RSTCTRL_IVA2_RST1_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define PRM_RSTCTRL_IVA2RST2_DSPWrite32(baseAddress,value)\
{\
    const u32 offset = PRM_RSTCTRL_IVA2_OFFSET;\
    register u32 data =\
	__raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSPWrite32);\
    data &= ~(PRM_RSTCTRL_IVA2_RST2_MASK);\
    newValue <<= PRM_RSTCTRL_IVA2_RST2_OFFSET;\
    newValue &= PRM_RSTCTRL_IVA2_RST2_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define PRM_RSTCTRL_IVA2RST3_DSPWrite32(baseAddress,value)\
{\
    const u32 offset = PRM_RSTCTRL_IVA2_OFFSET;\
    register u32 data =\
      __raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSPWrite32);\
    data &= ~(PRM_RSTCTRL_IVA2_RST3_MASK);\
    newValue <<= PRM_RSTCTRL_IVA2_RST3_OFFSET;\
    newValue &= PRM_RSTCTRL_IVA2_RST3_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}


#define PRCMRM_RSTST_DSPReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMRM_RSTST_DSPReadRegister32),\
      __raw_readl(((baseAddress))+PRCM_RM_RSTST_DSP_OFFSET))


#define PRCMRM_RSTST_DSPWriteRegister32(baseAddress,value)\
{\
    const u32 offset = PRCM_RM_RSTST_DSP_OFFSET;\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMRM_RSTST_DSPWriteRegister32);\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}


#define PRCMPM_PWSTCTRL_DSPForceStateWrite32(baseAddress, value)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_DSP_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTCTRL_DSPForceStateWrite32);\
    data &= ~(PRCM_PM_PWSTCTRL_DSP_ForceState_MASK);\
    newValue <<= PRCM_PM_PWSTCTRL_DSP_ForceState_OFFSET;\
    newValue &= PRCM_PM_PWSTCTRL_DSP_ForceState_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}


#define PRCMPM_PWSTCTRL_IVA2PowerStateWriteON32(baseAddress)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_IVA2_OFFSET;\
    const u32 newValue = (u32)PRCMPM_PWSTCTRL_IVA2PowerStateON <<\
      PRCM_PM_PWSTCTRL_IVA2_PowerState_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTCTRL_IVA2PowerStateWriteON32);\
    data &= ~(PRCM_PM_PWSTCTRL_IVA2_PowerState_MASK);\
    data |= newValue;\
    __raw_writel(data, (baseAddress)+offset);\
}


#define PRCMPM_PWSTCTRL_IVA2PowerStateWriteOFF32(baseAddress)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_IVA2_OFFSET;\
    const u32 newValue = (u32)PRCMPM_PWSTCTRL_IVA2PowerStateOFF <<\
      PRCM_PM_PWSTCTRL_IVA2_PowerState_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTCTRL_IVA2PowerStateWriteOFF32);\
    data &= ~(PRCM_PM_PWSTCTRL_IVA2_PowerState_MASK);\
    data |= newValue;\
    __raw_writel(data, (baseAddress)+offset);\
}


#define PRCMPM_PWSTCTRL_DSPPowerStateWriteRET32(baseAddress)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_DSP_OFFSET;\
    const u32 newValue = (u32)PRCMPM_PWSTCTRL_DSPPowerStateRET <<\
      PRCM_PM_PWSTCTRL_DSP_PowerState_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    _DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTCTRL_DSPPowerStateWriteRET32);\
    data &= ~(PRCM_PM_PWSTCTRL_DSP_PowerState_MASK);\
    data |= newValue;\
    __raw_writel(data, (baseAddress)+offset);\
}


#define PRCMPM_PWSTST_DSPReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTST_DSPReadRegister32),\
      __raw_readl(((u32)(baseAddress))+PRCM_PM_PWSTST_DSP_OFFSET))


#define PRCMPM_PWSTST_IVA2ReadRegister32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTST_IVA2ReadRegister32),\
      __raw_readl((baseAddress) + PRCM_PM_PWSTST_IVA2_OFFSET))


#define PRCMPM_PWSTST_DSPInTransitionRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTST_DSPInTransitionRead32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_PM_PWSTST_DSP_OFFSET)))) &\
      PRCM_PM_PWSTST_DSP_InTransition_MASK) >>\
      PRCM_PM_PWSTST_DSP_InTransition_OFFSET))


#define PRCMPM_PWSTST_IVA2InTransitionRead32(baseAddress)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTST_IVA2InTransitionRead32),\
      (((__raw_readl((((baseAddress))+\
	(PRCM_PM_PWSTST_IVA2_OFFSET)))) &\
      PRCM_PM_PWSTST_IVA2_InTransition_MASK) >>\
      PRCM_PM_PWSTST_IVA2_InTransition_OFFSET))


#define PRCMPM_PWSTST_DSPPowerStateStGet32(var)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTST_DSPPowerStateStGet32),\
      (u32)((((u32)(var)) & PRCM_PM_PWSTST_DSP_PowerStateSt_MASK) >>\
	PRCM_PM_PWSTST_DSP_PowerStateSt_OFFSET))


#define PRCMPM_PWSTST_IVA2PowerStateStGet32(var)\
    (_DEBUG_LEVEL_1_EASI(EASIL1_PRCMPM_PWSTST_IVA2PowerStateStGet32),\
      (u32)((((u32)(var)) & PRCM_PM_PWSTST_IVA2_PowerStateSt_MASK) >>\
      PRCM_PM_PWSTST_IVA2_PowerStateSt_OFFSET))


#endif  /* USE_LEVEL_1_MACROS */

#endif /* _PRCM_REG_ACM_H */
