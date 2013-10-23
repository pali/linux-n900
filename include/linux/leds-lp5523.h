#ifndef __LINUX_LP5523_H
#define __LINUX_LP5523_H

struct lp5523_led_config {
	const char	*name;
	u8		led_nr;
	u8		led_current; /* mA x10 */
};

struct lp5523_platform_data {
	struct lp5523_led_config *led_config;
	u8 num_leds;
	int irq;
	int chip_en;
};

#endif /* __LINUX_LP5523_H */

