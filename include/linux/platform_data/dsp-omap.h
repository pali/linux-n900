#ifndef __OMAP_DSP_H__
#define __OMAP_DSP_H__

#include <linux/types.h>
#include <linux/platform_device.h>

struct omap_dsp_platform_data {
	void (*dsp_set_min_opp) (u8 opp_id);
	u8 (*dsp_get_opp) (void);
	void (*cpu_set_freq) (unsigned long f);
	unsigned long (*cpu_get_freq) (void);
	unsigned long mpu_speed[6];

	/* functions to write and read PRCM registers */
	void (*dsp_prm_write)(u32, s16 , u16);
	u32 (*dsp_prm_read)(s16 , u16);
	u32 (*dsp_prm_rmw_bits)(u32, u32, s16, s16);
	void (*dsp_cm_write)(u32, s16 , u16);
	u32 (*dsp_cm_read)(s16 , u16);
	u32 (*dsp_cm_rmw_bits)(u32, u32, s16, s16);

	void (*set_bootaddr)(u32);
	void (*set_bootmode)(u8);

	int (*assert_reset)(struct platform_device *pdev);
	int (*deassert_reset)(struct platform_device *pdev);
};

#endif
