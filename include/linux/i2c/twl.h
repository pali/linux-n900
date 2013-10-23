#ifndef __TWL_H_
#define __TWL_H_

/*
 * Wrapper file to include twl4030.h for smartreflex.c
 */
#include <linux/i2c/twl4030.h>

#define twl_i2c_write_u8  twl4030_i2c_write_u8
#define twl_i2c_read_u8	 twl4030_i2c_read_u8

#endif
