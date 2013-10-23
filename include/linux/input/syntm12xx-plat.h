/*
 * Platform configuration for Synaptic TM12XX touchscreen driver.
 */

#ifndef __SYNTM12XX_TS_H
#define __SYNTM12XX_TS_H

struct syntm12xx_platform_data {
	int      gpio_intr;

	u8       *button_map;  /* Button to keycode  */
	unsigned num_buttons;  /* Registered buttons */
	u8       repeat;       /* Input dev Repeat enable */
	u8       swap_xy;      /* ControllerX==InputDevY...*/
	u16      max_x;        /* Scale ABS_MT_POSITION_X to this */
	u16      max_y;        /* Scale ABS_MT_POSITION_Y to this */
};

#endif
