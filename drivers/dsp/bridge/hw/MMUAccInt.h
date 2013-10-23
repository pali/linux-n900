/*
 * MMUAccInt.h
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

#ifndef _MMU_ACC_INT_H
#define _MMU_ACC_INT_H

/* Mappings of level 1 EASI function numbers to function names */

#define EASIL1_MMUMMU_SYSCONFIGReadRegister32 (MMU_BASE_EASIL1 + 3)
#define EASIL1_MMUMMU_SYSCONFIGIdleModeWrite32  (MMU_BASE_EASIL1 + 17)
#define EASIL1_MMUMMU_SYSCONFIGAutoIdleWrite32    (MMU_BASE_EASIL1 + 39)
#define EASIL1_MMUMMU_IRQSTATUSWriteRegister32   (MMU_BASE_EASIL1 + 51)
#define EASIL1_MMUMMU_IRQENABLEReadRegister32 (MMU_BASE_EASIL1 + 102)
#define EASIL1_MMUMMU_IRQENABLEWriteRegister32 (MMU_BASE_EASIL1 + 103)
#define EASIL1_MMUMMU_WALKING_STTWLRunningRead32 (MMU_BASE_EASIL1 + 156)
#define EASIL1_MMUMMU_CNTLTWLEnableRead32 (MMU_BASE_EASIL1 + 174)
#define EASIL1_MMUMMU_CNTLTWLEnableWrite32   (MMU_BASE_EASIL1 + 180)
#define EASIL1_MMUMMU_CNTLMMUEnableWrite32     (MMU_BASE_EASIL1 + 190)
#define EASIL1_MMUMMU_FAULT_ADReadRegister32   (MMU_BASE_EASIL1 + 194)
#define EASIL1_MMUMMU_TTBWriteRegister32  (MMU_BASE_EASIL1 + 198)
#define EASIL1_MMUMMU_LOCKReadRegister32   (MMU_BASE_EASIL1 + 203)
#define EASIL1_MMUMMU_LOCKWriteRegister32  (MMU_BASE_EASIL1 + 204)
#define EASIL1_MMUMMU_LOCKBaseValueRead32  (MMU_BASE_EASIL1 + 205)
#define EASIL1_MMUMMU_LOCKCurrentVictimRead32 (MMU_BASE_EASIL1 + 209)
#define EASIL1_MMUMMU_LOCKCurrentVictimWrite32 (MMU_BASE_EASIL1 + 211)
#define EASIL1_MMUMMU_LOCKCurrentVictimSet32  (MMU_BASE_EASIL1 + 212)
#define EASIL1_MMUMMU_LD_TLBReadRegister32    (MMU_BASE_EASIL1 + 213)
#define EASIL1_MMUMMU_LD_TLBWriteRegister32   (MMU_BASE_EASIL1 + 214)
#define EASIL1_MMUMMU_CAMWriteRegister32   (MMU_BASE_EASIL1 + 226)
#define EASIL1_MMUMMU_RAMWriteRegister32 (MMU_BASE_EASIL1 + 268)
#define EASIL1_MMUMMU_FLUSH_ENTRYWriteRegister32  (MMU_BASE_EASIL1 + 322)

/* Register offset address definitions */
#define MMU_MMU_SYSCONFIG_OFFSET   0x10
#define MMU_MMU_IRQSTATUS_OFFSET  0x18
#define MMU_MMU_IRQENABLE_OFFSET    0x1c
#define MMU_MMU_WALKING_ST_OFFSET 0x40
#define MMU_MMU_CNTL_OFFSET   0x44
#define MMU_MMU_FAULT_AD_OFFSET  0x48
#define MMU_MMU_TTB_OFFSET  0x4c
#define MMU_MMU_LOCK_OFFSET   0x50
#define MMU_MMU_LD_TLB_OFFSET  0x54
#define MMU_MMU_CAM_OFFSET   0x58
#define MMU_MMU_RAM_OFFSET   0x5c
#define MMU_MMU_GFLUSH_OFFSET  0x60
#define MMU_MMU_FLUSH_ENTRY_OFFSET  0x64
/* Bitfield mask and offset declarations */
#define MMU_MMU_SYSCONFIG_IdleMode_MASK  0x18
#define MMU_MMU_SYSCONFIG_IdleMode_OFFSET  3
#define MMU_MMU_SYSCONFIG_AutoIdle_MASK  0x1
#define MMU_MMU_SYSCONFIG_AutoIdle_OFFSET   0
#define MMU_MMU_WALKING_ST_TWLRunning_MASK 0x1
#define MMU_MMU_WALKING_ST_TWLRunning_OFFSET  0
#define MMU_MMU_CNTL_TWLEnable_MASK 0x4
#define MMU_MMU_CNTL_TWLEnable_OFFSET 2
#define MMU_MMU_CNTL_MMUEnable_MASK    0x2
#define MMU_MMU_CNTL_MMUEnable_OFFSET   1
#define MMU_MMU_LOCK_BaseValue_MASK 0xfc00
#define MMU_MMU_LOCK_BaseValue_OFFSET   10
#define MMU_MMU_LOCK_CurrentVictim_MASK   0x3f0
#define MMU_MMU_LOCK_CurrentVictim_OFFSET    4

#endif /* _MMU_ACC_INT_H */
