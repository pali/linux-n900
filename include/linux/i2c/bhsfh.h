#ifndef __BHSFH_H__
#define __BHSFH_H__

struct bhsfh_platform_data {
/* IR-Led configuration for proximity sensing */
#define BHSFH_LED1	    0x00
	__u8 leds;
/*
 * led_max_curr is a safetylimit: select smaller one - LED component limit
 * or brightness level which is still safe for eyes.
 */
#define BHSFH_LED_5mA   0
#define BHSFH_LED_10mA  1
#define BHSFH_LED_20mA  2
#define BHSFH_LED_50mA  3
#define BHSFH_LED_100mA 4
#define BHSFH_LED_150mA 5
#define BHSFH_LED_200mA 6
	__u8 led_max_curr;
	__u8 led_def_curr;
#define BHFSH_NEUTRAL_GA 16384 /* 16384 / 16384 = 1 */
	__u32 glass_attenuation;
	int (*setup_resources)(void);
	int (*release_resources)(void);
};

/* Device name: /dev/bh1770glc_ps - depreciated */
struct bhsfh_ps {
	__u8 led1;
	__u8 led2;
	__u8 led3;
} __attribute__((packed));

/* Device name: /dev/bh1770glc_als - depreciated */
struct bhsfh_als {
	__u16 lux;
} __attribute__((packed));

#define BHSFH_ALS_UPDATED 0x04 /* ALS result updated in the response */
#define BHSFH_PS_UPDATED  0x08 /* Prox result updated in the response */

/* Device name: /dev/bhsfh0 */
struct bhfsh_data {
	__u16 lux;
	__u16 lux_raw;
	__u8  ps;
	__u8  ps_raw;
	__u16 status;
} __attribute__((packed));

#endif
