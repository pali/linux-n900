/*
 * arch/arm/plat-omap/include/plat/dfl61-audio.h
 *
 * Copyright (C) 2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DFL61_AUDIO_H_
#define _DFL61_AUDIO_H_

enum dfl61audio_twl4030_config {
	AUDIO_CONFIG1,
	AUDIO_CONFIG2,
	AUDIO_CONFIG3,
	AUDIO_CONFIG4,
};

struct dfl61audio_twl4030_platform_data {
	enum dfl61audio_twl4030_config audio_config;
	unsigned int freq;
};

/* IDs for the soc cards on DFL61 */
enum dfl61_soc_id {
	DFL61_TWL4030_ID,
	DFL61_TLV320DAC33_ID,
	DFL61_WL1273_ID,
};

struct dfl61audio_hsmic_event {
	void *private;
	void (*event)(void *priv, bool on);
};

void dfl61_jack_report(int status);
int dfl61_request_hsmicbias(bool enable);
void dfl61_register_hsmic_event_cb(struct dfl61audio_hsmic_event *event);
void dfl61dac33_interconnect_enable(int enable);
int dfl61_request_hp_enable(bool enable);
#endif
