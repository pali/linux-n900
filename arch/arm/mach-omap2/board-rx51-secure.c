/*
 * RX51 Secure PPA API.
 *
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 *
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/cacheflush.h>

#include "board-rx51-secure.h"

/**
 * rx51_secure_dispatcher: Routine to dispatch secure PPA API calls
 * @idx: The PPA API index
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the non-zero error value on failure.
 */
u32 rx51_secure_dispatcher(u32 idx, u32 flag, u32 nargs, u32 arg1, u32 arg2,
			   u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs+1;
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));
	ret = rx51_ppa_smc(idx, flag, __pa(param));

	return ret;
}

/**
 * rx51_secure_update_aux_cr: Routine to modify the contents of Auxiliary Control Register
 *  @set_bits: bits to set in ACR
 *  @clr_bits: bits to clear in ACR
 *
 * Return the non-zero error value on failure.
*/
u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits)
{
	u32 acr;

	/* Read ACR */
	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r" (acr));
	acr &= ~clear_bits;
	acr |= set_bits;

	return rx51_secure_dispatcher(RX51_PPA_WRITE_ACR,
			       FLAG_START_CRITICAL,
			       1,acr,0,0,0);
}
