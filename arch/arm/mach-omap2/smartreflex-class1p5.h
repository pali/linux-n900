/*
 * Smartreflex Class 1p5 exposed routines
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Nishanth Menon <nm@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_OMAP_SMARTREFLEX_CLASS1P5
int sr_class1p5_init(void);
#else
static inline int sr_class1p5_init(void) { return 0; }
#endif
