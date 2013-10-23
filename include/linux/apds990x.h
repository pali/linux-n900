#ifndef __APDS990X_H__
#define __APDS990X_H__


#define APDS_IRLED_CURR_12mA	0x3
#define APDS_IRLED_CURR_25mA	0x2
#define APDS_IRLED_CURR_50mA	0x1
#define APDS_IRLED_CURR_100mA	0x0

/*
 * Structure for tuning ALS calculation to match with environment.
 * There depends on the material above the sensor and the sensor
 * itself. If the GA is zero, driver will use uncovered sensor default values
 * format: decimal value * APDS_PARAM_SCALE
 */
#define APDS_PARAM_SCALE 4096
struct apds990x_chip_factors {
	int ga;         /* Glass attenuation */
	int cf1;        /* Clear channel factor 1 */
	int irf1;       /* Ir channel factor 1 */
	int cf2;        /* Clear channel factor 2 */
	int irf2;       /* Ir channel factor 2 */
	int df;         /* Device factor. Decimal number */
};

struct apds990x_platform_data {
	struct apds990x_chip_factors cf;
	u8     pdrive;
	int    (*setup_resources)(void);
	int    (*release_resources)(void);
};

#define APDS990X_ALS_SATURATED	0x1 /* ADC overflow. result unreliable */
#define APDS990X_PS_ENABLED	0x2 /* Proximity sensor active */
#define APDS990X_ALS_UPDATED	0x4 /* ALS result updated in the response */
#define APDS990X_PS_UPDATED	0x8 /* Prox result updated in the response */

#define APDS990X_ALS_OUTPUT_SCALE 10

/* Device name: /dev/apds990x0 */
struct apds990x_data {
	__u32 lux; /* 10x scale */
	__u32 lux_raw; /* 10x scale */
	__u16 ps;
	__u16 ps_raw;
	__u16 status;
} __attribute__((packed));

/* This is for sensor diagnostig purposes */
struct apds990x_data_full {
	struct apds990x_data data;
	__u16 status;
	__u16 als_clear;
	__u16 als_ir;
	__u16 als_gain;
	__u16 als_atime;
	__u16 ps_gain;
} __attribute__((packed));

#endif
