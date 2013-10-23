#ifndef _LINUX_SPI_TSC2005_H
#define _LINUX_SPI_TSC2005_H

#include <linux/types.h>

struct tsc2005_platform_data {
	u16	ts_x_plate_ohm;
	u32	ts_stab_time;	/* voltage settling time */
	u8	ts_hw_avg;	/* HW assiseted averaging. Can be
				   0, 4, 8, 16 samples per reading */
	u32	ts_touch_pressure;	/* Pressure limit until we report a
					   touch event. After that we switch
					   to ts_max_pressure. */
	u32	ts_pressure_max;/* Samples with bigger pressure value will
				   be ignored, since the corresponding X, Y
				   values are unreliable */
	u32	ts_pressure_fudge;
	u32	ts_x_max;
	u32	ts_x_fudge;
	u32	ts_y_max;
	u32	ts_y_fudge;

	u32	esd_timeout;    /* msec of inactivity before we check */

	unsigned ts_ignore_last : 1;

	void (*set_reset)(bool enable);
};

#endif
