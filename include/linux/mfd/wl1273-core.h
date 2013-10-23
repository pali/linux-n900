/*
 * include/media/radio/radio-wl1273.h
 *
 * Some definitions for the wl1273 radio receiver/transmitter chip.
 *
 * Copyright (C) Nokia Corporation
 * Author: Matti J. Aaltonen <matti.j.aaltonen@nokia.com>
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
 */

#ifndef RADIO_WL1273_H
#define RADIO_WL1273_H

#include <linux/i2c.h>
#include <linux/mfd/core.h>


#define WL1273_FM_DRIVER_NAME	"wl1273-fm"
#define RX71_FM_I2C_ADDR	0x22

#define WL1273_STEREO_GET		0
#define WL1273_RSSI_LVL_GET		1
#define WL1273_IF_COUNT_GET		2
#define WL1273_FLAG_GET			3
#define WL1273_RDS_SYNC_GET		4
#define WL1273_RDS_DATA_GET		5
#define WL1273_FREQ_SET			10
#define WL1273_AF_FREQ_SET		11
#define WL1273_MOST_MODE_SET		12
#define WL1273_MOST_BLEND_SET		13
#define WL1273_DEMPH_MODE_SET		14
#define WL1273_SEARCH_LVL_SET		15
#define WL1273_BAND_SET			16
#define WL1273_MUTE_STATUS_SET		17
#define WL1273_RDS_PAUSE_LVL_SET	18
#define WL1273_RDS_PAUSE_DUR_SET	19
#define WL1273_RDS_MEM_SET		20
#define WL1273_RDS_BLK_B_SET		21
#define WL1273_RDS_MSK_B_SET		22
#define WL1273_RDS_PI_MASK_SET		23
#define WL1273_RDS_PI_SET		24
#define WL1273_RDS_SYSTEM_SET		25
#define WL1273_INT_MASK_SET		26
#define WL1273_SEARCH_DIR_SET		27
#define WL1273_VOLUME_SET		28
#define WL1273_AUDIO_ENABLE		29
#define WL1273_PCM_MODE_SET		30
#define WL1273_I2S_MODE_CONFIG_SET	31
#define WL1273_POWER_SET		32
#define WL1273_INTX_CONFIG_SET		33
#define WL1273_PULL_EN_SET		34
#define WL1273_HILO_SET			35
#define WL1273_SWITCH2FREF		36
#define WL1273_FREQ_DRIFT_REPORT	37

#define WL1273_PCE_GET			40
#define WL1273_FIRM_VER_GET		41
#define WL1273_ASIC_VER_GET		42
#define WL1273_ASIC_ID_GET		43
#define WL1273_MAN_ID_GET		44
#define WL1273_TUNER_MODE_SET		45
#define WL1273_STOP_SEARCH		46
#define WL1273_RDS_CNTRL_SET		47

#define WL1273_WRITE_HARDWARE_REG	100
#define WL1273_CODE_DOWNLOAD		101
#define WL1273_RESET			102

#define WL1273_FM_POWER_MODE		254
#define WL1273_FM_INTERRUPT		255

/* Transmitter API */

#define WL1273_CHANL_SET		55
#define WL1273_SCAN_SPACING_SET		56
#define WL1273_REF_SET			57
#define WL1273_POWER_ENB_SET		90
#define WL1273_POWER_ATT_SET		58
#define WL1273_POWER_LEV_SET		59
#define WL1273_AUDIO_DEV_SET		60
#define WL1273_PILOT_DEV_SET		61
#define WL1273_RDS_DEV_SET		62
#define WL1273_PUPD_SET			91
#define WL1273_AUDIO_IO_SET		63
#define WL1273_PREMPH_SET		64
#define WL1273_MONO_SET			66
#define WL1273_MUTE			92
#define WL1273_MPX_LMT_ENABLE		67
#define WL1273_PI_SET			93
#define WL1273_ECC_SET			69
#define WL1273_PTY			70
#define WL1273_AF			71
#define WL1273_DISPLAY_MODE		74
#define WL1273_RDS_REP_SET		77
#define WL1273_RDS_CONFIG_DATA_SET	98
#define WL1273_RDS_DATA_SET		99
#define WL1273_RDS_DATA_ENB		94
#define WL1273_TA_SET			78
#define WL1273_TP_SET			79
#define WL1273_DI_SET			80
#define WL1273_MS_SET			81
#define WL1273_PS_SCROLL_SPEED		82
#define WL1273_TX_AUDIO_LEVEL_TEST	96
#define WL1273_TX_AUDIO_LEVEL_TEST_THRESHOLD	73
#define WL1273_TX_AUDIO_INPUT_LEVEL_RANGE_SET	54
#define WL1273_RX_ANTENNA_SELECT	87
#define WL1273_I2C_DEV_ADDR_SET		86
#define WL1273_REF_ERR_CALIB_PARAM_SET		88
#define WL1273_REF_ERR_CALIB_PERIODICITY_SET	89
#define WL1273_SOC_INT_TRIGGER			52
#define WL1273_SOC_AUDIO_PATH_SET		83
#define WL1273_SOC_PCMI_OVERRIDE		84
#define WL1273_SOC_I2S_OVERRIDE		85
#define WL1273_RSSI_BLOCK_SCAN_FREQ_SET	95
#define WL1273_RSSI_BLOCK_SCAN_START	97
#define WL1273_RSSI_BLOCK_SCAN_DATA_GET	 5
#define WL1273_READ_FMANT_TUNE_VALUE		104

#define WL1273_RDS_OFF		0
#define WL1273_RDS_ON		1
#define WL1273_RDS_RESET	2

#define WL1273_AUDIO_DIGITAL	0
#define WL1273_AUDIO_ANALOG	1

#define WL1273_SPACING_50KHZ	0
#define WL1273_SPACING_100KHZ	1
#define WL1273_SPACING_200KHZ	2

#define WL1273_MODE_RX		0
#define WL1273_MODE_TX		1
#define WL1273_MODE_OFF		2
#define WL1273_MODE_SUSPENDED	3

#define WL1273_RADIO_CHILD	(1 << 0)
#define WL1273_CODEC_CHILD	(1 << 1)

#define WL1273_RX_MONO		1
#define WL1273_RX_STEREO	0
#define WL1273_TX_MONO		0
#define WL1273_TX_STEREO	1

#define WL1273_MAX_VOLUME	0xffff
#define WL1273_DEFAULT_VOLUME	0x78b8

/* Private IOCTLS */
#define WL1273_CID_FM_RADIO_MODE	(V4L2_CID_PRIVATE_BASE + 0)
#define WL1273_CID_FM_AUDIO_MODE	(V4L2_CID_PRIVATE_BASE + 1)
#define WL1273_CID_FM_REGION		(V4L2_CID_PRIVATE_BASE + 2)
#define WL1273_CID_FM_CHAN_SPACING	(V4L2_CID_PRIVATE_BASE + 3)
#define WL1273_CID_FM_RDS_CTRL		(V4L2_CID_PRIVATE_BASE + 4)
#define WL1273_CID_FM_CTUNE_VAL		(V4L2_CID_PRIVATE_BASE + 5)
#define WL1273_CID_TUNE_PREEMPHASIS	(V4L2_CID_PRIVATE_BASE + 6)
#define WL1273_CID_TX_POWER		(V4L2_CID_PRIVATE_BASE + 7)
#define WL1273_CID_SEARCH_LVL		(V4L2_CID_PRIVATE_BASE + 8)

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127

#define WL1273_FR_EVENT			(1 << 0)
#define WL1273_BL_EVENT			(1 << 1)
#define WL1273_RDS_EVENT		(1 << 2)
#define WL1273_BBLK_EVENT		(1 << 3)
#define WL1273_LSYNC_EVENT		(1 << 4)
#define WL1273_LEV_EVENT		(1 << 5)
#define WL1273_IFFR_EVENT		(1 << 6)
#define WL1273_PI_EVENT			(1 << 7)
#define WL1273_PD_EVENT			(1 << 8)
#define WL1273_STIC_EVENT		(1 << 9)
#define WL1273_MAL_EVENT		(1 << 10)
#define WL1273_POW_ENB_EVENT		(1 << 11)
#define WL1273_SCAN_OVER_EVENT		(1 << 12)
#define WL1273_ERROR_EVENT		(1 << 13)

struct region_info {
	u32 bottom_frequency;
	u32 top_frequency;
	u8 region;
};

struct wl1273_fm_platform_data {
	int (*request_resources) (struct i2c_client *client);
	void (*free_resources) (void);
	void (*enable) (void);
	void (*disable) (void);

	u8 modes;
	unsigned int children;
};

#define WL1273_FM_CORE_CELLS	2

/* Allowed modes */
#define WL1273_RX_ALLOWED	0x01
#define WL1273_TX_ALLOWED	0x02
#define WL1273_RXTX_ALLOWED	(WL1273_RX_ALLOWED | WL1273_TX_ALLOWED)

struct wl1273_core {
	struct mfd_cell cells[WL1273_FM_CORE_CELLS];
	struct i2c_client *i2c_dev;

	/* driver management */
	u8 allowed_modes;
	unsigned int mode;
	unsigned int preemphasis;
	unsigned int audio_mode;
	unsigned int tx_power;
	unsigned int spacing;
	unsigned int rx_frequency;
	unsigned int tx_frequency;
	unsigned int region;
	unsigned int i2s_mode;
	unsigned int channel_number;
	unsigned int volume;
	bool rds_on;
	int search_level;

	const struct region_info *regions;

	/* RDS */
	struct delayed_work work;

	wait_queue_head_t read_queue;
	struct mutex lock; /* for serializing fm radio operations */
	struct completion busy;

	unsigned char *buffer;
	unsigned int buf_size;
	unsigned int rd_index;
	unsigned int wr_index;

	/* Selected interrupts */
	u16 irq_flags;
	u16 irq_received;
};

int wl1273_fm_write_cmd(struct wl1273_core *core, u8 cmd, u16 param);
int wl1273_fm_write_data(struct wl1273_core *core, u8 *data, u16 len);
int wl1273_fm_read_reg(struct wl1273_core *core, u8 reg, u16 *value);
int wl1273_fm_get_freq(struct wl1273_core *core);
int wl1273_fm_set_tx_freq(struct wl1273_core *core, unsigned int freq);
int wl1273_fm_set_rx_freq(struct wl1273_core *core, unsigned int freq);
int wl1273_fm_set_seek(struct wl1273_core *core,
		       unsigned int wrap_around,
		       unsigned int seek_upward);

int wl1273_fm_set_channel_number(struct wl1273_core *core, int ch_num);
int wl1273_fm_set_i2s_mode(struct wl1273_core *core, int rate, int width);

unsigned int wl1273_fm_get_tx_ctune(struct wl1273_core *core);
unsigned int wl1273_fm_get_audio(struct wl1273_core *core);
int wl1273_fm_set_audio(struct wl1273_core *core, unsigned int mode);
int wl1273_fm_tx_get_spacing(struct wl1273_core *core);
int wl1273_fm_tx_set_spacing(struct wl1273_core *core, unsigned int spa);
unsigned int wl1273_fm_get_preemphasis(struct wl1273_core *core);
int wl1273_fm_set_preemphasis(struct wl1273_core *core, unsigned int emph);
unsigned int wl1273_fm_get_tx_power(struct wl1273_core *core);
int wl1273_fm_set_tx_power(struct wl1273_core *core, u16 p);
unsigned int wl1273_fm_get_mode(struct wl1273_core *core);
int wl1273_fm_set_mode(struct wl1273_core *core, int mode);
u8 wl1273_fm_get_region(struct wl1273_core *core);
int wl1273_fm_set_region(struct wl1273_core *core, unsigned int region);
int wl1273_fm_get_rds(struct wl1273_core *core);
int wl1273_fm_set_rds(struct wl1273_core *core, unsigned int mode);
int wl1273_fm_get_search_level(struct wl1273_core *core);
int wl1273_fm_set_search_level(struct wl1273_core *core, s16 level);
int wl1273_fm_set_volume(struct wl1273_core *core, u16 volume);

#endif	/* ifndef RADIO_WL1273_H */
