/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#include <linux/kernel.h>
#include <linux/module.h>

#include "wl1271.h"
#include "spi.h"
#include "boot.h"
#include "acx.h"
#include "init.h"

static struct wl12xx_partition_set wl1271_part_table[PART_TABLE_LEN] = {
	[PART_DOWN] = {
		.mem = {
			.start = 0x00000000,
			.size  = 0x000177c0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x00008800
		},
	},

	[PART_WORK] = {
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x0000b000
		},
	},

	[PART_DRPW] = {
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = DRPW_BASE,
			.size  = 0x00006000
		}
	}
};

static enum wl12xx_acx_int_reg wl1271_acx_reg_table[ACX_REG_TABLE_LEN] = {
	[ACX_REG_INTERRUPT_TRIG]     = (REGISTERS_BASE + 0x0474),
	[ACX_REG_INTERRUPT_TRIG_H]   = (REGISTERS_BASE + 0x0478),
	[ACX_REG_INTERRUPT_MASK]     = (REGISTERS_BASE + 0x04DC),
	[ACX_REG_HINT_MASK_SET]      = (REGISTERS_BASE + 0x04E0),
	[ACX_REG_HINT_MASK_CLR]      = (REGISTERS_BASE + 0x04E4),
	[ACX_REG_INTERRUPT_NO_CLEAR] = (REGISTERS_BASE + 0x04E8),
	[ACX_REG_INTERRUPT_CLEAR]    = (REGISTERS_BASE + 0x04F8),
	[ACX_REG_INTERRUPT_ACK]      = (REGISTERS_BASE + 0x04F0),
	[ACX_REG_SLV_SOFT_RESET]     = (REGISTERS_BASE + 0x0000),
	[ACX_REG_EE_START]           = (REGISTERS_BASE + 0x080C),
	[ACX_REG_ECPU_CONTROL]       = (REGISTERS_BASE + 0x0804)
};

static int wl1271_upload_firmware_chunk(struct wl12xx *wl, void *buf,
					size_t fw_data_len, u32 dest)
{
	int addr, chunk_num, partition_limit;
	u8 *p;

	/* whal_FwCtrl_LoadFwImageSm() */

	wl12xx_debug(DEBUG_BOOT, "starting firmware upload");

	wl12xx_debug(DEBUG_BOOT, "fw_data_len %d chunk_size %d", fw_data_len,
		CHUNK_SIZE);


	if ((fw_data_len % 4) != 0) {
		wl12xx_error("firmware length not multiple of four");
		return -EIO;
	}

	wl12xx_set_partition(wl, dest,
			     wl->chip.p_table[PART_DOWN].mem.size,
			     wl->chip.p_table[PART_DOWN].reg.start,
			     wl->chip.p_table[PART_DOWN].reg.size);

	/* 10.1 set partition limit and chunk num */
	chunk_num = 0;
	partition_limit = wl->chip.p_table[PART_DOWN].mem.size;

	while (chunk_num < fw_data_len / CHUNK_SIZE) {
		/* 10.2 update partition, if needed */
		addr = dest + (chunk_num + 2) * CHUNK_SIZE;
		if (addr > partition_limit) {
			addr = dest + chunk_num * CHUNK_SIZE;
			partition_limit = chunk_num * CHUNK_SIZE +
				wl->chip.p_table[PART_DOWN].mem.size;

			/* FIXME: Over 80 chars! */
			wl12xx_set_partition(wl,
					     addr,
					     wl->chip.p_table[PART_DOWN].mem.size,
					     wl->chip.p_table[PART_DOWN].reg.start,
					     wl->chip.p_table[PART_DOWN].reg.size);
		}

		/* 10.3 upload the chunk */
		addr = dest + chunk_num * CHUNK_SIZE;
		p = buf + chunk_num * CHUNK_SIZE;
		wl12xx_debug(DEBUG_BOOT, "uploading fw chunk 0x%p to 0x%x",
			     p, addr);
		wl12xx_spi_mem_write(wl, addr, p, CHUNK_SIZE);

		chunk_num++;
	}

	/* 10.4 upload the last chunk */
	addr = dest + chunk_num * CHUNK_SIZE;
	p = buf + chunk_num * CHUNK_SIZE;
	wl12xx_debug(DEBUG_BOOT, "uploading fw last chunk (%d B) 0x%p to 0x%x",
		     fw_data_len % CHUNK_SIZE, p, addr);
	wl12xx_spi_mem_write(wl, addr, p, fw_data_len % CHUNK_SIZE);

	return 0;
}

static int wl1271_upload_firmware(struct wl12xx *wl)
{
	u32 chunks, addr, len;
	u8 *fw;

	fw = wl->fw;
	chunks = be32_to_cpup((u32 *) fw);
	fw += sizeof(u32);

	wl12xx_debug(DEBUG_BOOT, "firmware chunks to be uploaded: %u", chunks);

	while (chunks--) {
		addr = be32_to_cpup((u32 *) fw);
		fw += sizeof(u32);
		len = be32_to_cpup((u32 *) fw);
		fw += sizeof(u32);

		if (len > 300000) {
			wl12xx_info("firmware chunk too long: %u", len);
			return -EINVAL;
		}
		wl12xx_debug(DEBUG_BOOT, "chunk %d addr 0x%x len %u",
			     chunks, addr, len);
		wl1271_upload_firmware_chunk(wl, fw, len, addr);
		fw += len;
	}

	return 0;
}

static int wl1271_upload_nvs(struct wl12xx *wl)
{
	size_t nvs_len, burst_len;
	int i;
	u32 dest_addr, val;
	u8 *nvs_ptr, *nvs, *nvs_aligned;

	nvs = wl->nvs;
	if (nvs == NULL)
		return -ENODEV;

	nvs_ptr = nvs;

	nvs_len = wl->nvs_len;

	/*
	 * Layout before the actual NVS tables:
	 * 1 byte : burst length.
	 * 2 bytes: destination address.
	 * n bytes: data to burst copy.
	 *
	 * This is ended by a 0 length, then the NVS tables.
	 */

	/* FIXME: Do we need to check here whether the LSB is 1? */
	while (nvs_ptr[0]) {
		burst_len = nvs_ptr[0];
		dest_addr = (nvs_ptr[1] & 0xfe) | ((u32)(nvs_ptr[2] << 8));

		/* FIXME: Due to our new wl12xx_translate_reg_addr function,
		   we need to add the REGISTER_BASE to the destination */
		dest_addr += REGISTERS_BASE;

		/* We move our pointer to the data */
		nvs_ptr += 3;

		for (i = 0; i < burst_len; i++) {
			val = (nvs_ptr[0] | (nvs_ptr[1] << 8)
			       | (nvs_ptr[2] << 16) | (nvs_ptr[3] << 24));

			wl12xx_debug(DEBUG_BOOT,
				     "nvs burst write 0x%x: 0x%x",
				     dest_addr, val);
			wl12xx_reg_write32(wl, dest_addr, val);

			nvs_ptr += 4;
			dest_addr += 4;
		}
	}

	/*
	 * We've reached the first zero length, the first NVS table
	 * is 7 bytes further.
	 */
	nvs_ptr += 7;
	nvs_len -= nvs_ptr - nvs;
	nvs_len = ALIGN(nvs_len, 4);

	/* FIXME: The driver sets the partition here, but this is not needed,
	   since it sets to the same one as currently in use */
	/* Now we must set the partition correctly */
	wl12xx_set_partition(wl,
			     wl->chip.p_table[PART_WORK].mem.start,
			     wl->chip.p_table[PART_WORK].mem.size,
			     wl->chip.p_table[PART_WORK].reg.start,
			     wl->chip.p_table[PART_WORK].reg.size);

	/* Copy the NVS tables to a new block to ensure alignment */
	nvs_aligned = kmemdup(nvs_ptr, nvs_len, GFP_KERNEL);

	/* And finally we upload the NVS tables */
	/* FIXME: In wl1271, we upload everything at once.
	   No endianness handling needed here?! The ref driver doesn't do
	   anything about it at this point */
	wl12xx_spi_mem_write(wl, CMD_MBOX_ADDRESS, nvs_aligned, nvs_len);

	kfree(nvs_aligned);
	return 0;
}

static int wl1271_boot(struct wl12xx *wl)
{
	int ret = 0;
	u32 tmp, clk, pause;

	if (REF_CLOCK == 0 || REF_CLOCK == 2)
		/* ref clk: 19.2/38.4 */
		clk = 0x3;
	else if (REF_CLOCK == 1 || REF_CLOCK == 3)
		/* ref clk: 26/52 */
		clk = 0x5;

	wl12xx_reg_write32(wl, PLL_PARAMETERS, clk);

	pause = wl12xx_reg_read32(wl, PLL_PARAMETERS);

	wl12xx_debug(DEBUG_BOOT, "pause1 0x%x", pause);

	pause &= ~(WU_COUNTER_PAUSE_VAL); /* FIXME: This should probably be
					   * WU_COUNTER_PAUSE_VAL instead of
					   * 0x3ff (magic number ).  How does
					   * this work?! */
	pause |= WU_COUNTER_PAUSE_VAL;
	wl12xx_reg_write32(wl, WU_COUNTER_PAUSE, pause);

	/* Continue the ELP wake up sequence */
	wl12xx_reg_write32(wl, WELP_ARM_COMMAND, WELP_ARM_COMMAND_VAL);
	udelay(500);
	/* FIXME: Magic numbers! Argh! */
	wl12xx_reg_write32(wl, REGISTERS_BASE + 0x9b4, 0x30222);
	wl12xx_reg_write32(wl, REGISTERS_BASE + 0x9b8, 0x201);
	wl12xx_reg_write32(wl, REGISTERS_BASE + 0x9c0, 0x1);

#define WORKAROUND_FOR_VNWA
#ifdef WORKAROUND_FOR_VNWA
	/*
	 * workaround fix for VNWA control signal short with floating pin
	 * in TRIO PMS The PRCM register address is 0x454 and the
	 * prcm_dig_ldo_tload_en is bit 8. In order to write '1' to this
	 * field do the following:
	 *
	 * write to 0x3009b4 -> 0x30000 | (0x454/2)
	 *   - Address
	 * write to 0x3009b8 -> 0x300
	 *   - Data to be written (bit 8 is the fix and 9 is reset value)
	 * write to 0x3009c0 -> 0x1
	 */
	wl12xx_reg_write32(wl, REGISTERS_BASE + 0x9b4, 0x3022a);
	wl12xx_reg_write32(wl, REGISTERS_BASE + 0x9b8, 0x300);
	wl12xx_reg_write32(wl, REGISTERS_BASE + 0x9c0, 0x1);
#endif

	wl12xx_set_partition(wl,
			     wl->chip.p_table[PART_DRPW].mem.start,
			     wl->chip.p_table[PART_DRPW].mem.size,
			     wl->chip.p_table[PART_DRPW].reg.start,
			     wl->chip.p_table[PART_DRPW].reg.size);

	/* Read-modify-write DRPW_SCRATCH_START register (see next state)
	   to be used by DRPw FW. The RTRIM value will be added by the FW
	   before taking DRPw out of reset */

	wl12xx_debug(DEBUG_BOOT, "DRPW_SCRATCH_START %08x", DRPW_SCRATCH_START);
	clk = wl12xx_reg_read32(wl, DRPW_SCRATCH_START);

	wl12xx_debug(DEBUG_BOOT, "clk2 0x%x", clk);

	/* 2 */
	clk |= (REF_CLOCK << 1) << 4;
	wl12xx_reg_write32(wl, DRPW_SCRATCH_START, clk);

	wl12xx_set_partition(wl,
			     wl->chip.p_table[PART_WORK].mem.start,
			     wl->chip.p_table[PART_WORK].mem.size,
			     wl->chip.p_table[PART_WORK].reg.start,
			     wl->chip.p_table[PART_WORK].reg.size);

	/* Disable interrupts */
	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_MASK, WL1271_ACX_INTR_ALL);

	ret = wl12xx_boot_soft_reset(wl);
	if (ret < 0)
		goto out;

	/* 2. start processing NVS file */
	ret = wl->chip.op_upload_nvs(wl);
	if (ret < 0)
		goto out;

	/* write firmware's last address (ie. it's length) to
	 * ACX_EEPROMLESS_IND_REG */
	wl12xx_debug(DEBUG_BOOT, "ACX_EEPROMLESS_IND_REG");

	wl12xx_reg_write32(wl, ACX_EEPROMLESS_IND_REG, ACX_EEPROMLESS_IND_REG);

	tmp = wl12xx_reg_read32(wl, CHIP_ID_B);

	wl12xx_debug(DEBUG_BOOT, "chip id 0x%x", tmp);

	/* 6. read the EEPROM parameters */
	tmp = wl12xx_reg_read32(wl, SCR_PAD2);

	/* WL1271: The reference driver skips steps 7 to 10 (jumps directly
	 * to upload_fw) */

	ret = wl->chip.op_upload_fw(wl);
	if (ret < 0)
		goto out;

	/* 10.5 start firmware */
	ret = wl12xx_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int wl1271_mem_cfg(struct wl12xx *wl)
{
	struct wl1271_acx_config_memory mem_conf;
	int ret = 0;

	wl12xx_debug(DEBUG_ACX, "wl1271 mem cfg");

	/* memory config */
	mem_conf.num_stations = cpu_to_le16(DEFAULT_NUM_STATIONS);
	mem_conf.rx_mem_block_num = ACX_RX_MEM_BLOCKS;
	mem_conf.tx_min_mem_block_num = ACX_TX_MIN_MEM_BLOCKS;
	mem_conf.num_ssid_profiles = ACX_NUM_SSID_PROFILES;
	mem_conf.total_tx_descriptors = ACX_TX_DESCRIPTORS;

	mem_conf.header.id = ACX_MEM_CFG;
	mem_conf.header.len = sizeof(struct wl1271_acx_config_memory) -
		sizeof(struct acx_header);

	ret = wl12xx_cmd_configure(wl, &mem_conf,
				   sizeof(struct wl1271_acx_config_memory));
	if (ret < 0)
		wl12xx_warning("wl1271 mem config failed: %d", ret);

	return ret;
}

static int wl1271_hw_init_mem_config(struct wl12xx *wl)
{
	int ret;

	ret = wl1271_mem_cfg(wl);
	if (ret < 0)
		return ret;

	wl->target_mem_map = kzalloc(sizeof(struct wl1271_acx_mem_map),
					  GFP_KERNEL);
	if (!wl->target_mem_map) {
		wl12xx_error("couldn't allocate target memory map");
		return -ENOMEM;
	}

	/* we now ask for the firmware built memory map */
	ret = wl12xx_acx_mem_map(wl, wl->target_mem_map,
				 sizeof(struct wl1271_acx_mem_map));
	if (ret < 0) {
		wl12xx_error("couldn't retrieve firmware memory map");
		kfree(wl->target_mem_map);
		wl->target_mem_map = NULL;
		return ret;
	}

	return 0;
}

static int wl1271_hw_init_tx_interrupt(struct wl12xx *wl)
{
	struct wl1271_tx_config_opt tx_conf;
	int ret = 0;

	wl12xx_debug(DEBUG_ACX, "wl1271 tx interrupt config");

	tx_conf.threshold = WL1271_TX_CMPLT_THRESHOLD_DEF;
	tx_conf.timeout = WL1271_TX_CMPLT_TIMEOUT_DEF;

	tx_conf.header.id = ACX_TX_CONFIG_OPT;
	tx_conf.header.len = sizeof(struct wl1271_tx_config_opt) -
		sizeof(struct acx_header);

	ret = wl12xx_cmd_configure(wl, &tx_conf,
				   sizeof(struct wl1271_tx_config_opt));
	if (ret < 0)
		wl12xx_warning("wl1271 tx config opt failed: %d", ret);

	return ret;
}

static int wl1271_hw_init_rx_interrupt(struct wl12xx *wl)
{
	struct wl1271_rx_config_opt rx_conf;
	int ret = 0;

	wl12xx_debug(DEBUG_ACX, "wl1271 rx interrupt config");

	rx_conf.threshold = WL1271_RX_INTR_THRESHOLD_DEF;
	rx_conf.timeout = WL1271_RX_INTR_TIMEOUT_DEF;
	rx_conf.mblk_threshold = USHORT_MAX; /* Disabled */
	rx_conf.queue_type = RX_QUEUE_TYPE_RX_LOW_PRIORITY;

	rx_conf.header.id = ACX_RX_CONFIG_OPT;
	rx_conf.header.len = sizeof(struct wl1271_rx_config_opt) -
		sizeof(struct acx_header);

	ret = wl12xx_cmd_configure(wl, &rx_conf,
				   sizeof(struct wl1271_rx_config_opt));
	if (ret < 0)
		wl12xx_warning("wl1271 rx config opt failed: %d", ret);

	return ret;
}

static void wl1271_set_ecpu_ctrl(struct wl12xx *wl, u32 flag)
{
	u32 cpu_ctrl;

	/* 10.5.0 run the firmware (I) */
	cpu_ctrl = wl12xx_reg_read32(wl, ACX_REG_ECPU_CONTROL);

	/* 10.5.1 run the firmware (II) */
	cpu_ctrl |= flag;
	wl12xx_reg_write32(wl, ACX_REG_ECPU_CONTROL, cpu_ctrl);
}

static void wl1271_target_enable_interrupts(struct wl12xx *wl)
{
	/* Enable target's interrupts */
	wl->intr_mask = WL1271_ACX_INTR_CMD_COMPLETE |
		WL1271_ACX_INTR_EVENT_A |
		WL1271_ACX_INTR_EVENT_B;
	wl12xx_boot_target_enable_interrupts(wl);
}

static void wl1271_irq_work(struct work_struct *work)
{
	wl12xx_debug(DEBUG_IRQ, "IRQ work -- not implemented yet");
}

static int wl1271_hw_init(struct wl12xx *wl)
{
	int ret;
	struct wl1271_acx_mem_map *wl_mem_map;

	ret = wl12xx_hw_init_hwenc_config(wl);
	if (ret < 0)
		return ret;

	/* Template settings */
	ret = wl12xx_hw_init_templates_config(wl);
	if (ret < 0)
		return ret;

	/* Default memory configuration */
	ret = wl1271_hw_init_mem_config(wl);
	if (ret < 0)
		return ret;

	/* RX config */
	ret = wl12xx_hw_init_rx_config(wl,
				       RX_CFG_PROMISCUOUS | RX_CFG_TSF,
				       RX_FILTER_OPTION_DEF);
	/* RX_CONFIG_OPTION_ANY_DST_ANY_BSS,
	   RX_FILTER_OPTION_FILTER_ALL); */
	if (ret < 0)
		goto out_free_memmap;

	/* PHY layer config */
	ret = wl12xx_hw_init_phy_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Beacon filtering */
	ret = wl12xx_hw_init_beacon_filter(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* TX complete interrupt pacing */
	ret = wl1271_hw_init_tx_interrupt(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* RX complete interrupt pacing */
	ret = wl1271_hw_init_rx_interrupt(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Bluetooth WLAN coexistence */
	ret = wl12xx_hw_init_pta(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Energy detection */
	ret = wl12xx_hw_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Beacons and boradcast settings */
	ret = wl12xx_hw_init_beacon_broadcast(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Enable data path */
	ret = wl12xx_cmd_data_path(wl, wl->channel, 1);
	if (ret < 0)
		goto out_free_memmap;

	/* Default power state */
	ret = wl12xx_hw_init_power_auth(wl);
	if (ret < 0)
		goto out_free_memmap;

	wl_mem_map = wl->target_mem_map;

	return 0;

 out_free_memmap:
	kfree(wl->target_mem_map);

	return ret;
}

static int wl1271_plt_init(struct wl12xx *wl)
{
	int ret;

	ret = wl1271_hw_init_mem_config(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_data_path(wl, wl->channel, 1);
	if (ret < 0)
		return ret;

	return 0;
}

void wl1271_setup(struct wl12xx *wl)
{
	/* FIXME: Is it better to use strncpy here or is this ok? */
	wl->chip.fw_filename = WL1271_FW_NAME;
	wl->chip.nvs_filename = WL1271_NVS_NAME;

	/* Now we know what chip we're using, so adjust the power on sleep
	 * time accordingly */
	wl->chip.power_on_sleep = WL1271_POWER_ON_SLEEP;

	wl->chip.intr_cmd_complete = WL1271_ACX_INTR_CMD_COMPLETE;
	wl->chip.intr_init_complete = WL1271_ACX_INTR_INIT_COMPLETE;

	wl->chip.op_upload_nvs = wl1271_upload_nvs;
	wl->chip.op_upload_fw = wl1271_upload_firmware;
	wl->chip.op_boot = wl1271_boot;
	wl->chip.op_set_ecpu_ctrl = wl1271_set_ecpu_ctrl;
	wl->chip.op_target_enable_interrupts = wl1271_target_enable_interrupts;
	wl->chip.op_hw_init = wl1271_hw_init;
	wl->chip.op_plt_init = wl1271_plt_init;

	wl->chip.p_table = wl1271_part_table;
	wl->chip.acx_reg_table = wl1271_acx_reg_table;

	INIT_WORK(&wl->irq_work, wl1271_irq_work);
}
