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

#define PRCMPRCM_CLKCFG_CTRL_VALID_CONFIG_WRITE_CLK_VALID32(baseAddress)\
{\
    const u32 offset = PRCM_PRCM_CLKCFG_CTRL_OFFSET;\
    const u32 newValue = \
	(u32)PRCMPRCM_CLKCFG_CTRL_VALID_CONFIG_CLK_VALID <<\
      PRCM_PRCM_CLKCFG_CTRL_VALID_CONFIG_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(\
      EASIL1_PRCMPRCM_CLKCFG_CTRL_VALID_CONFIG_WRITE_CLK_VALID32);\
    data &= ~(PRCM_PRCM_CLKCFG_CTRL_VALID_CONFIG_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define CM_FCLKEN_PER_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_FCLKEN1_CORE_READ_REGISTER32),\
      __raw_readl(((u32)(baseAddress))+CM_FCLKEN_PER_OFFSET))

#define CM_ICLKEN_PER_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_FCLKEN1_CORE_READ_REGISTER32),\
      __raw_readl(((u32)(baseAddress))+CM_ICLKEN_PER_OFFSET))

#define CM_FCLKEN_PER_GPT5_WRITE_REGISTER32(baseAddress, value)\
{\
    const u32 offset = CM_FCLKEN_PER_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_CM_FCLKEN_PER_GPT5_WRITE_REGISTER32);\
   data &= ~(CM_FCLKEN_PER_GPT5_MASK);\
   newValue <<= CM_FCLKEN_PER_GPT5_OFFSET;\
   newValue &= CM_FCLKEN_PER_GPT5_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}

#define CM_FCLKEN_PER_GPT6_WRITE_REGISTER32(baseAddress, value)\
{\
    const u32 offset = CM_FCLKEN_PER_OFFSET;\
    register u32 data =\
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_CM_FCLKEN_PER_GPT5_WRITE_REGISTER32);\
   data &= ~(CM_FCLKEN_PER_GPT6_MASK);\
   newValue <<= CM_FCLKEN_PER_GPT6_OFFSET;\
   newValue &= CM_FCLKEN_PER_GPT6_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}

#define CM_ICLKEN_PER_GPT5_WRITE_REGISTER32(baseAddress, value)\
{\
    const u32 offset = CM_ICLKEN_PER_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_CM_ICLKEN_PER_GPT5_WRITE_REGISTER32);\
   data &= ~(CM_ICLKEN_PER_GPT5_MASK);\
   newValue <<= CM_ICLKEN_PER_GPT5_OFFSET;\
   newValue &= CM_ICLKEN_PER_GPT5_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}

#define CM_ICLKEN_PER_GPT6_WRITE_REGISTER32(baseAddress, value)\
{\
    const u32 offset = CM_ICLKEN_PER_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_CM_ICLKEN_PER_GPT5_WRITE_REGISTER32);\
   data &= ~(CM_ICLKEN_PER_GPT6_MASK);\
   newValue <<= CM_ICLKEN_PER_GPT6_OFFSET;\
   newValue &= CM_ICLKEN_PER_GPT6_MASK;\
   newValue |= data;\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}

#define CM_FCLKEN1_CORE_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_FCLKEN1_CORE_READ_REGISTER32),\
      __raw_readl(((u32)(baseAddress))+CM_FCLKEN1_CORE_OFFSET))

#define PRCMCM_FCLKEN1_COREEN_GPT8_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_FCLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_FCLKEN1_COREEN_GPT8_WRITE32);\
    data &= ~(PRCM_CM_FCLKEN1_CORE_EN_GPT8_MASK);\
    newValue <<= PRCM_CM_FCLKEN1_CORE_EN_GPT8_OFFSET;\
    newValue &= PRCM_CM_FCLKEN1_CORE_EN_GPT8_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_FCLKEN1_COREEN_GPT7_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_FCLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_FCLKEN1_COREEN_GPT7_WRITE32);\
    data &= ~(PRCM_CM_FCLKEN1_CORE_EN_GPT7_MASK);\
    newValue <<= PRCM_CM_FCLKEN1_CORE_EN_GPT7_OFFSET;\
    newValue &= PRCM_CM_FCLKEN1_CORE_EN_GPT7_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define CM_ICLKEN1_CORE_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_ICLKEN1_CORE_READ_REGISTER32),\
      __raw_readl(((u32)(baseAddress))+CM_ICLKEN1_CORE_OFFSET))

#define  CM_ICLKEN1_COREEN_MAILBOXES_WRITE32(baseAddress, value)\
{\
    const u32 offset = CM_ICLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_ICLKEN1_COREEN_MAILBOXES_WRITE32);\
    data &= ~(CM_ICLKEN1_CORE_EN_MAILBOXES_MASK);\
    newValue <<= CM_ICLKEN1_CORE_EN_MAILBOXES_OFFSET;\
    newValue &= CM_ICLKEN1_CORE_EN_MAILBOXES_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_ICLKEN1_COREEN_GPT8_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_ICLKEN1_CORE_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_ICLKEN1_COREEN_GPT8_WRITE32);\
    data &= ~(PRCM_CM_ICLKEN1_CORE_EN_GPT8_MASK);\
    newValue <<= PRCM_CM_ICLKEN1_CORE_EN_GPT8_OFFSET;\
    newValue &= PRCM_CM_ICLKEN1_CORE_EN_GPT8_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_ICLKEN1_COREEN_GPT7_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_ICLKEN1_CORE_OFFSET;\
    register u32 data =\
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_ICLKEN1_COREEN_GPT7_WRITE32);\
    data &= ~(PRCM_CM_ICLKEN1_CORE_EN_GPT7_MASK);\
    newValue <<= PRCM_CM_ICLKEN1_CORE_EN_GPT7_OFFSET;\
    newValue &= PRCM_CM_ICLKEN1_CORE_EN_GPT7_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE32K32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT832K <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE32K32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE_SYS32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT8_SYS <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE_SYS32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE_EXT32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT8_EXT <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE_EXT32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE32K32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT732K <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE32K32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE_SYS32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT7_SYS <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE_SYS32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE_EXT32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT7_EXT <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE_EXT32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT6_WRITE_SYS32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT6_SYS <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT6_WRITE_SYS32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT6_WRITE_EXT32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT6_EXT <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT6_WRITE_EXT32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define CM_CLKSEL_PER_GPT5_WRITE32K32(baseAddress)\
{\
    const u32 offset = CM_CLKSEL_PER_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT532K <<\
      CM_CLKSEL_PER_GPT5_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_CM_CLKSEL_PER_GPT5_WRITE32K32);\
    data &= ~(CM_CLKSEL_PER_GPT5_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define CM_CLKSEL_PER_GPT6_WRITE32K32(baseAddress)\
{\
    const u32 offset = CM_CLKSEL_PER_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT532K <<\
      CM_CLKSEL_PER_GPT6_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_CM_CLKSEL_PER_GPT6_WRITE32K32);\
    data &= ~(CM_CLKSEL_PER_GPT6_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT5_WRITE_SYS32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT5_SYS <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT5_WRITE_SYS32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL2_CORECLKSEL_GPT5_WRITE_EXT32(baseAddress)\
{\
    const u32 offset = PRCM_CM_CLKSEL2_CORE_OFFSET;\
    const u32 newValue = (u32)PRCMCM_CLKSEL2_CORECLKSEL_GPT5_EXT <<\
      PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_OFFSET;\
    register u32 data = __raw_readl((u32)(baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT5_WRITE_EXT32);\
    data &= ~(PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_MASK);\
    data |= newValue;\
    __raw_writel(data, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL1_PLLAPL_LS_CLKIN_READ32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL1_PLLAPL_LS_CLKIN_READ32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_CM_CLKSEL1_PLL_OFFSET)))) &\
      PRCM_CM_CLKSEL1_PLL_APL_LS_CLKIN_MASK) >>\
      PRCM_CM_CLKSEL1_PLL_APL_LS_CLKIN_OFFSET))

#define CM_FCLKEN_IVA2EN_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = CM_FCLKEN_IVA2_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_FCLKEN_DSPEN_DSP_WRITE32);\
    data &= ~(CM_FCLKEN_IVA2_EN_MASK);\
    newValue <<= CM_FCLKEN_IVA2_EN_OFFSET;\
    newValue &= CM_FCLKEN_IVA2_EN_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_ICLKEN_DSPEN_DSP_IPI_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_ICLKEN_DSP_OFFSET;\
    register u32 data = \
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_ICLKEN_DSPEN_DSP_IPI_WRITE32);\
    data &= ~(PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_MASK);\
    newValue <<= PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_OFFSET;\
    newValue &= PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_IDLEST_DSP_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_IDLEST_DSP_READ_REGISTER32),\
      __raw_readl(((u32)(baseAddress))+PRCM_CM_IDLEST_DSP_OFFSET))

#define PRCMCM_IDLEST_DSPST_IPI_READ32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_IDLEST_DSPST_IPI_READ32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_CM_IDLEST_DSP_OFFSET)))) &\
      PRCM_CM_IDLEST_DSP_ST_IPI_MASK) >>\
      PRCM_CM_IDLEST_DSP_ST_IPI_OFFSET))

#define PRM_IDLEST_IVA2ST_IVA2_READ32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_IDLEST_DSPST_DSP_READ32),\
      (((__raw_readl((((u32)(baseAddress))+\
	  (CM_IDLEST_IVA2_OFFSET)))) &\
      CM_IDLEST_IVA2_ST_IVA2_MASK) >>\
      CM_IDLEST_IVA2_ST_IVA2_OFFSET))

#define PRCMCM_AUTOIDLE_DSPAUTO_DSP_IPI_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_AUTOIDLE_DSP_OFFSET;\
    register u32 data =\
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_AUTOIDLE_DSPAUTO_DSP_IPI_WRITE32);\
    data &= ~(PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_MASK);\
    newValue <<= PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_OFFSET;\
    newValue &= PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL_DSPSYNC_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSEL_DSP_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL_DSPSYNC_DSP_WRITE32);\
    data &= ~(PRCM_CM_CLKSEL_DSP_SYNC_DSP_MASK);\
    newValue <<= PRCM_CM_CLKSEL_DSP_SYNC_DSP_OFFSET;\
    newValue &= PRCM_CM_CLKSEL_DSP_SYNC_DSP_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL_DSPCLKSEL_DSP_IF_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSEL_DSP_OFFSET;\
    register u32 data = \
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL_DSPCLKSEL_DSP_IF_WRITE32);\
    data &= ~(PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_MASK);\
    newValue <<= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_OFFSET;\
    newValue &= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSEL_DSPCLKSEL_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSEL_DSP_OFFSET;\
    register u32 data = \
      __raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSEL_DSPCLKSEL_DSP_WRITE32);\
    data &= ~(PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_MASK);\
    newValue <<= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_OFFSET;\
    newValue &= PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMCM_CLKSTCTRL_IVA2_WRITE_REGISTER32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSTCTRL_IVA2_OFFSET;\
    register u32 data = \
      __raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSTCTRL_IVA2_WRITE_REGISTER32);\
    data &= ~(CM_CLKSTCTRL_IVA2_MASK);\
    newValue <<= CM_CLKSTCTRL_IVA2_OFFSET;\
    newValue &= CM_CLKSTCTRL_IVA2_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}

#define PRCMCM_CLKSTCTRL_DSP_AUTOSTATE_DSP_READ32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSTCTRL_DSP_AUTOSTATE_DSP_READ32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_CM_CLKSTCTRL_DSP_OFFSET)))) &\
      PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_MASK) >>\
      PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_OFFSET))

#define PRCMCM_CLKSTCTRL_DSP_AUTOSTATE_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_CM_CLKSTCTRL_DSP_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMCM_CLKSTCTRL_DSP_AUTOSTATE_DSP_WRITE32);\
    data &= ~(PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_MASK);\
    newValue <<= PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_OFFSET;\
    newValue &= PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMRM_RSTCTRL_DSP_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMRM_RSTCTRL_DSP_READ_REGISTER32),\
      __raw_readl(((baseAddress))+PRCM_RM_RSTCTRL_DSP_OFFSET))

#define PRM_RSTCTRL_IVA2RST1_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRM_RSTCTRL_IVA2_OFFSET;\
    register u32 data =\
    __raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSP_WRITE32);\
    data &= ~(PRM_RSTCTRL_IVA2_RST1_MASK);\
    newValue <<= PRM_RSTCTRL_IVA2_RST1_OFFSET;\
    newValue &= PRM_RSTCTRL_IVA2_RST1_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}

#define PRM_RSTCTRL_IVA2RST2_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRM_RSTCTRL_IVA2_OFFSET;\
    register u32 data =\
	__raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSP_WRITE32);\
    data &= ~(PRM_RSTCTRL_IVA2_RST2_MASK);\
    newValue <<= PRM_RSTCTRL_IVA2_RST2_OFFSET;\
    newValue &= PRM_RSTCTRL_IVA2_RST2_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}

#define PRM_RSTCTRL_IVA2RST3_DSP_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRM_RSTCTRL_IVA2_OFFSET;\
    register u32 data =\
      __raw_readl(((baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSP_WRITE32);\
    data &= ~(PRM_RSTCTRL_IVA2_RST3_MASK);\
    newValue <<= PRM_RSTCTRL_IVA2_RST3_OFFSET;\
    newValue &= PRM_RSTCTRL_IVA2_RST3_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (baseAddress)+offset);\
}

#define PRCMRM_RSTST_DSP_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMRM_RSTST_DSP_READ_REGISTER32),\
      __raw_readl(((baseAddress))+PRCM_RM_RSTST_DSP_OFFSET))

#define PRCMRM_RSTST_DSP_WRITE_REGISTER32(baseAddress, value)\
{\
    const u32 offset = PRCM_RM_RSTST_DSP_OFFSET;\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMRM_RSTST_DSP_WRITE_REGISTER32);\
    __raw_writel(newValue, ((u32)(baseAddress))+offset);\
}

#define PRCMPM_PWSTCTRL_DSP_FORCE_STATE_WRITE32(baseAddress, value)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_DSP_OFFSET;\
    register u32 data = \
	__raw_readl(((u32)(baseAddress))+offset);\
    register u32 newValue = ((u32)(value));\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTCTRL_DSP_FORCE_STATE_WRITE32);\
    data &= ~(PRCM_PM_PWSTCTRL_DSP_FORCE_STATE_MASK);\
    newValue <<= PRCM_PM_PWSTCTRL_DSP_FORCE_STATE_OFFSET;\
    newValue &= PRCM_PM_PWSTCTRL_DSP_FORCE_STATE_MASK;\
    newValue |= data;\
    __raw_writel(newValue, (u32)(baseAddress)+offset);\
}

#define PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_ON32(baseAddress)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_IVA2_OFFSET;\
    const u32 newValue = (u32)PRCMPM_PWSTCTRL_IVA2_POWER_STATE_ON <<\
      PRCM_PM_PWSTCTRL_IVA2_POWER_STATE_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_ON32);\
    data &= ~(PRCM_PM_PWSTCTRL_IVA2_POWER_STATE_MASK);\
    data |= newValue;\
    __raw_writel(data, (baseAddress)+offset);\
}

#define PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_OFF32(baseAddress)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_IVA2_OFFSET;\
    const u32 newValue = (u32)PRCMPM_PWSTCTRL_IVA2_POWER_STATE_OFF <<\
      PRCM_PM_PWSTCTRL_IVA2_POWER_STATE_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_OFF32);\
    data &= ~(PRCM_PM_PWSTCTRL_IVA2_POWER_STATE_MASK);\
    data |= newValue;\
    __raw_writel(data, (baseAddress)+offset);\
}

#define PRCMPM_PWSTCTRL_DSP_POWER_STATE_WRITE_RET32(baseAddress)\
{\
    const u32 offset = PRCM_PM_PWSTCTRL_DSP_OFFSET;\
    const u32 newValue = (u32)PRCMPM_PWSTCTRL_DSP_POWER_STATE_RET <<\
      PRCM_PM_PWSTCTRL_DSP_POWER_STATE_OFFSET;\
    register u32 data = __raw_readl((baseAddress)+offset);\
    _DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTCTRL_DSP_POWER_STATE_WRITE_RET32);\
    data &= ~(PRCM_PM_PWSTCTRL_DSP_POWER_STATE_MASK);\
    data |= newValue;\
    __raw_writel(data, (baseAddress)+offset);\
}

#define PRCMPM_PWSTST_DSP_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTST_DSP_READ_REGISTER32),\
      __raw_readl(((u32)(baseAddress))+PRCM_PM_PWSTST_DSP_OFFSET))

#define PRCMPM_PWSTST_IVA2_READ_REGISTER32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTST_IVA2_READ_REGISTER32),\
      __raw_readl((baseAddress) + PRCM_PM_PWSTST_IVA2_OFFSET))

#define PRCMPM_PWSTST_DSP_IN_TRANSITION_READ32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTST_DSP_IN_TRANSITION_READ32),\
      (((__raw_readl((((u32)(baseAddress))+\
	(PRCM_PM_PWSTST_DSP_OFFSET)))) &\
      PRCM_PM_PWSTST_DSP_IN_TRANSITION_MASK) >>\
      PRCM_PM_PWSTST_DSP_IN_TRANSITION_OFFSET))

#define PRCMPM_PWSTST_IVA2_IN_TRANSITION_READ32(baseAddress)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTST_IVA2_IN_TRANSITION_READ32),\
      (((__raw_readl((((baseAddress))+\
	(PRCM_PM_PWSTST_IVA2_OFFSET)))) &\
      PRCM_PM_PWSTST_IVA2_IN_TRANSITION_MASK) >>\
      PRCM_PM_PWSTST_IVA2_IN_TRANSITION_OFFSET))

#define PRCMPM_PWSTST_DSP_POWER_STATE_ST_GET32(var)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTST_DSP_POWER_STATE_ST_GET32),\
      (u32)((((u32)(var)) & PRCM_PM_PWSTST_DSP_POWER_STATE_ST_MASK) >>\
	PRCM_PM_PWSTST_DSP_POWER_STATE_ST_OFFSET))

#define PRCMPM_PWSTST_IVA2_POWER_STATE_ST_GET32(var)\
    (_DEBUG_LEVEL1_EASI(EASIL1_PRCMPM_PWSTST_IVA2_POWER_STATE_ST_GET32),\
      (u32)((((u32)(var)) & PRCM_PM_PWSTST_IVA2_POWER_STATE_ST_MASK) >>\
      PRCM_PM_PWSTST_IVA2_POWER_STATE_ST_OFFSET))

#endif /* USE_LEVEL_1_MACROS */

#endif /* _PRCM_REG_ACM_H */
