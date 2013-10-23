/*
 * sound/soc/codec/wl1273.h
 *
 * ALSA SoC WL1273 codec driver
 *
 * Copyright (C) Nokia Corporation
 * Author: Matti Aaltonen <matti.j.aaltonen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1273_CODEC_H__
#define __WL1273_CODEC_H__

enum wl1273_mode { WL1273_MODE_BT, WL1273_MODE_FM_RX, WL1273_MODE_FM_TX };

/* codec private data */
struct wl1273_priv {
	struct snd_soc_codec codec;
	enum wl1273_mode mode;
	int ctune;
	int tx_power;
	int rds_mode;
	int pwr_mode;
	int audio_mode;
	int region;
	int preemphasis;
	int spacing;

	struct wl1273_core *core;
};

extern struct snd_soc_dai wl1273_dai;
extern struct snd_soc_codec_device soc_codec_dev_wl1273;

enum wl1273_mode wl1273_get_codec_mode(struct snd_soc_codec *codec);

#endif	/* End of __WL1273_CODEC_H__ */
