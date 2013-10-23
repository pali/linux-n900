#ifndef _LINUX_SPI_WL12XX_H
#define _LINUX_SPI_WL12XX_H

struct wl12xx_platform_data {
	void (*set_power)(bool enable);
};

#endif
