/*
 * board-rx51-secure.h: OMAP Secure infrastructure header.
 *
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OMAP_RX51_SECURE_H
#define OMAP_RX51_SECURE_H

/* HAL API error codes */
#define  API_HAL_RET_VALUE_OK           0x00
#define  API_HAL_RET_VALUE_FAIL         0x01

/* Secure HAL API flags */
#define FLAG_START_CRITICAL             0x4
#define FLAG_IRQFIQ_MASK                0x3
#define FLAG_IRQ_ENABLE                 0x2
#define FLAG_FIQ_ENABLE                 0x1
#define NO_FLAG                         0x0

/* Secure PPA(Primary Protected Application) APIs */
#define RX51_PPA_L2_INVAL               40
#define RX51_PPA_WRITE_ACR              42

#ifndef __ASSEMBLER__

extern u32 rx51_secure_dispatcher(u32 idx, u32 flag, u32 nargs,
                                u32 arg1, u32 arg2, u32 arg3, u32 arg4);
extern u32 rx51_ppa_smc(u32 id, u32 flag, u32 pargs);

extern u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits);
#endif /* __ASSEMBLER__ */
#endif /* OMAP_RX51_SECURE_H */
