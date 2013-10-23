/*
 * Smartreflex Class 3 Routines
 *
 * Author: Thara Gopinath      <thara@ti.com>
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_OMAP_SMARTREFLEX_CLASS3
int sr_class3_init(void);
#else
static inline int sr_class3_init(void) { return 0; }
#endif
