/*
 * atmel_mxt.c -- Atmel mxt touchscreen driver
 *
 * Copyright (C) 2009,2010 Nokia Corporation
 * Author: Mika Kuoppala <mika.kuoppala@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/input/atmel_mxt.h>
#include <linux/gpio.h>

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include "atmel_mxt_objects.h"
#endif

#define DRIVER_DESC	"Atmel mXT Touchscreen"
#define DRIVER_NAME	"atmel_mxt"

#define DFLAG_VERBOSE	(1 << 0)
#define DFLAG_I2C_DUMP	(1 << 1)
#define DFLAG_MSG	(1 << 2)
#define DFLAG_TOUCH_MSG	(1 << 3)

#define INFO_BLOCK_FAMILY_ID	0
#define INFO_BLOCK_VARIANT_ID	1
#define INFO_BLOCK_VERSION	2
#define INFO_BLOCK_BUILD	3
#define INFO_BLOCK_MATRIX_X	4
#define INFO_BLOCK_MATRIX_Y	5
#define INFO_BLOCK_NUM_OBJECTS	6
#define INFO_BLOCK_HEADER_SIZE	7

#define OBJECT_TYPE		0
#define OBJECT_ADDRESS_LSB	1
#define OBJECT_ADDRESS_MSB	2
#define OBJECT_SIZE_MINUS_ONE	3
#define OBJECT_INST_MINUS_ONE	4
#define OBJECT_RIDS_PER_INST	5
#define OBJECT_DESC_SIZE	6

#define MAX_OBJ_TABLE_ITEMS	20
#define CRC24_SIZE		3

#define INFO_BLOCK_MAX_SIZE	(INFO_BLOCK_HEADER_SIZE +		\
				 (MAX_OBJ_TABLE_ITEMS * OBJECT_DESC_SIZE) + \
				 CRC24_SIZE)

#define MAX_I2C_WRITE_SIZE	256
#define MAX_MSG_PER_INTERRUPT	1000
#define MAX_TOUCH_POINTS	10

#define OT_MSG_PROCESSOR	5
#define OT_CMD_PROCESSOR	6
#define OT_TOUCH		9
#define OT_POWERCONFIG		7
#define OT_COMMSCONFIG		18
#define OT_PROXIMITY		23
#define OT_ONETOUCHGESTURE	24
#define OT_SELFTEST		25
#define OT_CTECONFIG		28
#define OT_DEBUG_DIAG		37

#define MSG_STATUS_COMSERR	(1 << 2)
#define MSG_STATUS_CFGERR	(1 << 3)
#define MSG_STATUS_CALIBRATION	(1 << 4)
#define MSG_STATUS_SIGERR	(1 << 5)
#define MSG_STATUS_OVERFLOW	(1 << 6)
#define MSG_STATUS_RESET	(1 << 7)

#define CTRL_REG		0
#define CTRL_ENABLE		(1 << 0)
#define CTRL_REPORTS		(1 << 1)

#define CMD_RESET		0
#define CMD_BACKUPNV		1
#define CMD_CALIBRATE		2
#define CMD_DIAG		5

#define SELFTEST_TEST_ALL	0xfe
#define SELFTEST_RES_PASS	0xfe
#define SELFTEST_RES_UNKNOWN	0xff

#define PCONFIG_IDLEACQINT	0
#define PCONFIG_ACTACQINT	1

#define TOUCH_CTRL              0
#define TOUCH_ORIENT		9
#define TOUCH_MOVHYSTI		11
#define TOUCH_MOVHYSTN		12
#define TOUCH_XRANGE		18
#define TOUCH_YRANGE		20
#define TOUCH_YEDGECTRL		28

#define TOUCH_CTRL_DISAMP       (1 << 2)
#define TOUCH_CTRL_DISVEC       (1 << 3)

#define TOUCH_MSG_STATUS	0
#define TOUCH_MSG_XPOS_MSB	1
#define TOUCH_MSG_YPOS_MSB	2
#define TOUCH_MSG_XYPOS_LSB	3
#define TOUCH_MSG_AREA		4
#define TOUCH_MSG_AMPLITUDE	5
#define TOUCH_MSG_VECTOR	6

#define CTECONFIG_CTRL		0
#define CTECONFIG_CMD		1

#define CTECONFIG_CMD_CALIBRATE	0xA5

#define FSTATE_NOT_DETECTED	0
#define FSTATE_DETECTED		1
#define FSTATE_RELEASED		2

#define DIAG_PAGEUP		0x01
#define DIAG_PAGEDOWN		0x02
#define DIAG_DELTAS		0x10
#define DIAG_REFS		0x11
#define DIAG_GAINS		0x31
#define DIAG_XCOMMONS		0xF2

#define DIAG_MAX_PAGES		4

#define DIAG_MODE		0
#define DIAG_PAGE		1
#define DIAG_DATA_START		2
#define DIAG_DATA_END		130
#define DIAG_PAGE_LEN		(DIAG_DATA_END - DIAG_DATA_START)
#define DIAG_TOTAL_LEN		DIAG_DATA_END

#define POWER_ON_RESET_WAIT	65

static const char reg_avdd[] = "AVdd";
static const char reg_dvdd[] = "Vdd";

struct msg {
	union {
		struct {
			u8 report_id;
			u8 msg[7];
			u8 checksum;
		};
		u8 data[9];
	};
} __attribute__ ((packed));

struct mt_data {
	u8  state;
	u16 pos_x;
	u16 pos_y;
	u8  area;
	u8  ampl;
	u8  vec;
};

struct mxt_selftest {
	u8 test_num;
	u8 result;
	u8 info[5];
};

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS
struct mxt_debugfs_obj;

struct mxt_debugfs_param {
	u8 index;
	struct mxt_debugfs_obj *parent;
	struct dentry *dent;
};

struct mxt_debugfs_obj {
	const struct mxt_object_desc *obj;
	struct dentry *dent;
	struct dentry *objdump_dent;
	struct mxt_debugfs *parent;
	struct mxt_debugfs_param *params;
};

struct mxt;

#define DEBUGFS_MODE_NONE   0
#define DEBUGFS_MODE_CACHED 1
#define DEBUGFS_MODE_FW     2

struct mxt_debugfs {
	struct mxt *mxt_dev;
	struct dentry *dent;
	struct mxt_debugfs_obj objs[ARRAY_SIZE(mxt_obj_descs)];
	u8 mode;
};
#endif

struct mxt_obj_state {
	u16 len;
	u8 dirty;
	u8 *d;
};

struct mxt {
	struct mutex		lock;
	struct i2c_client	*client;
	struct regulator_bulk_data regs[2];
	u16			res_y;
	u16			res_x;
	u8                      touch_ctrl;
	struct input_dev	*idev;
	struct mt_data		finger_state[MAX_TOUCH_POINTS];
	u8			num_touch_reports;
	u16			active_mask;
	u16			last_active_mask;
	unsigned long		last_report_ts;
	unsigned long		last_finger_down_ts;
	unsigned long		rlimit_min_interval;
	unsigned long		rlimit_bypass_time;

	struct mxt_selftest	selftest;
	struct completion	selftest_done;

	unsigned long		debug_flags;

	u8			info_block[INFO_BLOCK_MAX_SIZE];
	u8			obj_count;
	bool			preinit_done;
	bool			objinit_done;
	u8			power_enabled;
	bool			flash_mode;

	int			addr_ptr;
	u32			nonvol_cfg_crc;

	/* Cache msg processor address */
	u16			msg_processor_addr;

	/* 2 extra for address, 1 for crc */
	u8			wb[MAX_I2C_WRITE_SIZE + 3];

	unsigned long		err_init;
	unsigned long		err_overflow;
	unsigned long		err_sigerr;
	unsigned long		err_i2c_crc;
	unsigned long		err_msg_crc;
	unsigned long		err_cfg;

	unsigned long		num_isr;
	unsigned long		num_work;
	unsigned long		num_syn;
	unsigned long		num_msg;
	unsigned long           num_msg_err;
	unsigned long		num_msg_255;
	unsigned long           num_msg_zero;
	unsigned long		num_reset_handled;
	unsigned long		num_reset_hw;
	unsigned long		num_reset_sw;
	unsigned long		num_cals;
	unsigned long		num_backup_nv;
	unsigned long		num_gesture_wakeups;

	char			phys[32];

	u8			wakeup_gesture;
	u8			wakeup_interval;

	struct mxt_obj_state	obj_state[MAX_OBJ_TABLE_ITEMS];
	u32			tcharea_lt[32];
	u32			pixels_per_channel;
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS
	struct mxt_debugfs	*dfs;
#endif
};

struct msg_handler {
	u8 obj_type;
	u8 oi;
	u8 rid_first;
	u8 rid_count;
	int (*handler)(struct mxt *td, const struct msg *m);
};

static int mxt_msg_handler_touch(struct mxt *td, const struct msg *m);
static int mxt_msg_handler_cmd(struct mxt *td, const struct msg *m);
static int mxt_msg_handler_proximity(struct mxt *td, const struct msg *m);
static int mxt_msg_handler_cteconfig(struct mxt *td, const struct msg *m);
static int mxt_msg_handler_selftest(struct mxt *td, const struct msg *m);
static int mxt_msg_handler_onetouchgesture(struct mxt *td, const struct msg *m);

#define MSG_HANDLER_INDEX_TOUCH	0

static struct msg_handler msg_handlers[] = {
	{ OT_TOUCH, 0, 0, 0, mxt_msg_handler_touch },
	{ OT_CMD_PROCESSOR, 0, 0, 0, mxt_msg_handler_cmd },
	{ OT_PROXIMITY, 0, 0, 0, mxt_msg_handler_proximity },
	{ OT_CTECONFIG, 0, 0, 0, mxt_msg_handler_cteconfig },
	{ OT_SELFTEST, 0, 0, 0, mxt_msg_handler_selftest },
	{ OT_ONETOUCHGESTURE, 0, 0, 0, mxt_msg_handler_onetouchgesture },
};

#define FW_MAX_NAME_SIZE	64

static inline u8 mxt_crc8_s(u8 crc, u8 data)
{
	u8 fb;
	int i;

	for (i = 0; i < 8; i++) {
		fb = (crc ^ data) & 0x01;
		data >>= 1;
		crc >>= 1;
		if (fb)
			crc ^= 0x8c;
	}

	return crc;
}

static u8 mxt_crc8(const u8 *data, const int len)
{
	u8 crc = 0;
	int i;

	for (i = 0; i < len; i++)
		crc = mxt_crc8_s(crc, *data++);

	return crc;
}

static inline u32 mxt_crc24_s(const u32 crc, const u8 b0, const u8 b1)
{
	u32 r;

	r = ((crc) << 1) ^ (u32)((u16)(b1 << 8) | b0);

	if (r & (1 << 24))
		r ^= 0x80001b;

	return r;
}

static u32 mxt_crc24(u32 crc, const u8 *data, const int len)
{
	const int swords = len >> 1;
	int i;

	for (i = 0; i < swords; i++)
		crc = mxt_crc24_s(crc, data[i * 2], data[i * 2 + 1]);

	if (len & 0x01)
		crc = mxt_crc24_s(crc, data[len - 1], 0);

	return crc & 0x00FFFFFF;
}

static int mxt_write_block(struct mxt *td, u16 addr, const u8 *data, int len)
{
	struct i2c_msg msg;
	int r;
	int i;

	msg.addr = addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = (u8 *)data,

	r = i2c_transfer(td->client->adapter, &msg, 1);

	if (td->debug_flags & DFLAG_I2C_DUMP) {
		for (i = 0; i < msg.len; i++)
			dev_info(&td->client->dev,
				 "0x%02x bw [%d]: 0x%02x (%d)\n", addr, i,
				 data[i], data[i]);

		dev_info(&td->client->dev,
			 "write (%d bytes) returned with %d\n",
			 msg.len, r);
	}

	if (r == 1)
		return 0;

	return r;
}

static void mxt_clear_finger_state(struct mxt *td)
{
	int i;

	for (i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (td->finger_state[i].state != FSTATE_NOT_DETECTED) {
			input_report_key(td->idev, BTN_TOUCH, 0);
			input_mt_sync(td->idev);
			input_sync(td->idev);
			goto out;
		}
	}
out:
	memset(&td->finger_state, 0, sizeof(td->finger_state));
}

static void mxt_invalidate_addr(struct mxt *td)
{
	td->addr_ptr = -1;
}

static void mxt_clear_info_block(struct mxt *td)
{
	memset(td->info_block, 0, sizeof(td->info_block));
	td->obj_count = 0;
	td->preinit_done = false;
}

static void mxt_uninit(struct mxt *td)
{
	mxt_invalidate_addr(td);
	td->objinit_done = false;
	td->num_touch_reports = 0;
	mxt_clear_finger_state(td);
}

static int mxt_write_reg(struct mxt *td, u16 reg, const u8 *data, int len,
			 const bool crc)
{
	int r;

	if (len < 0 ||
	    len > MAX_I2C_WRITE_SIZE) {
		dev_info(&td->client->dev, "too long write block len %d\n",
			 len);
		return -EINVAL;
	}

	if (crc)
		reg |= (1 << 15);

	td->wb[0] = reg & 0xff;
	td->wb[1] = (reg & 0xff00) >> 8;

	if (data && len)
		memcpy(td->wb + 2, data, len);

	if (crc)
		td->wb[2 + len] = mxt_crc8(td->wb, 2 + len);

	len = len + 2 + (crc ? 1 : 0);

	r = mxt_write_block(td, td->client->addr, td->wb, len);
	mxt_invalidate_addr(td);

	return r;
}

static int mxt_set_fw_addr_ptr(struct mxt *td, u16 addr, const bool crc)
{
	int r;

	r = mxt_write_reg(td, addr, NULL, 0, crc);
	if (r == 0)
		td->addr_ptr = addr;
	else
		td->addr_ptr = -1;

	return r;
}

static int mxt_fw_addr_ptr(struct mxt *td)
{
	if (td->addr_ptr == -1)
		return -1;

	/* Mask out the crc check bit */
	return td->addr_ptr & 0x7FFF;
}

static int mxt_read_block(struct mxt *td, u16 addr, u8 *data, int len)
{
	struct i2c_msg msg;
	int r;

	msg.addr = addr;
	msg.flags = I2C_M_RD;
	msg.len = len;
	msg.buf = data;

	r = i2c_transfer(td->client->adapter, &msg, 1);

	if (td->debug_flags & DFLAG_I2C_DUMP) {
		dev_info(&td->client->dev,
			 "read of len %d from 0x%04x: \n", len, td->addr_ptr);
		if (r == 1) {
			int i;

			for (i = 0; i < len; i++)
				dev_info(&td->client->dev,
					 "br 0x%02x: 0x%02x\n",
					 i, data[i]);
		} else {
			dev_info(&td->client->dev,
				 "read returned %d\n", r);
		}
	}

	if (r == 1)
		return 0;

	return r;
}

static inline u16 mxt_flash_addr(struct mxt *td)
{
	if (td->client->addr & 1)
		return 0x25;

	return 0x24;
}

static int mxt_flash_read(struct mxt *td, u8* data, int len)
{
	return mxt_read_block(td, mxt_flash_addr(td), data, len);
}

static int mxt_flash_write(struct mxt *td, const u8* data, int len)
{
	return mxt_write_block(td, mxt_flash_addr(td), data, len);
}

static int mxt_flash_read_status(struct mxt *td)
{
	u8 status;
	int r;

	r = mxt_flash_read(td, &status, 1);
	if (r < 0)
		return r;

	return status;
}

static int mxt_read_reg(struct mxt *td, u16 reg, u8 *data, int len)
{
	int r;

	r = mxt_set_fw_addr_ptr(td, reg, false);
	if (r < 0)
		return r;

	return mxt_read_block(td, td->client->addr, data, len);
}

static inline int mxt_obj_desc_offset(const u8 oi)
{
	return INFO_BLOCK_HEADER_SIZE + (oi * OBJECT_DESC_SIZE);
}

static inline const u8 *mxt_obj_desc(const struct mxt *td, const u8 oi)
{
	return td->info_block + mxt_obj_desc_offset(oi);
}

static inline int mxt_firmware_version(const struct mxt *td)
{
	return td->info_block[INFO_BLOCK_VERSION];
}

static inline int mxt_variant(const struct mxt *td)
{
	return td->info_block[INFO_BLOCK_VARIANT_ID];
}

static inline int mxt_build(const struct mxt *td)
{
	return td->info_block[INFO_BLOCK_BUILD];
}

static inline int mxt_lines_x(const struct mxt *td)
{
	return td->info_block[INFO_BLOCK_MATRIX_X];
}

static inline int mxt_lines_y(const struct mxt *td)
{
	return td->info_block[INFO_BLOCK_MATRIX_Y];
}

static void mxt_print_version(const struct mxt *td)
{
	const u8 ver = mxt_firmware_version(td);

	if (td->num_reset_handled <= 1 ||
	    td->debug_flags & DFLAG_VERBOSE)
		printk(KERN_INFO DRIVER_NAME ": " DRIVER_DESC
		       " v%d.%d (0x%x) var:0x%x bld:0x%x\n",
		       (ver & 0xf0) >> 4, ver & 0x0f,
		       td->nonvol_cfg_crc,
		       mxt_variant(td), mxt_build(td));
}

static int mxt_check_if_mxt(struct mxt *td, const u8* d, int len)
{
	if (len < INFO_BLOCK_HEADER_SIZE)
		return -EINVAL;

	if (d[INFO_BLOCK_FAMILY_ID] != 0x80)
		return -ENODEV;

	return 0;
}

static int mxt_check_object_table(struct mxt *td)
{
	u8 *d;
	u16 crc_start_off;
	u32 crc_read;
	u32 crc_calc;
	u8 num_objects;

	d = td->info_block;

	num_objects = d[6];

	/* Get offset past last object to find where crc starts */
	crc_start_off = mxt_obj_desc_offset(num_objects);

	crc_read = d[crc_start_off];
	crc_read |= d[crc_start_off + 1] << 8;
	crc_read |= d[crc_start_off + 2] << 16;

	crc_calc = mxt_crc24(0, d, crc_start_off);

	if (crc_calc != crc_read) {
		dev_err(&td->client->dev,
			"object table crc mismatch %x vs %x (read)\n",
			crc_calc, crc_read);

		/* Special case for broken firmware */
		if (mxt_firmware_version(td) == 0x11) {
			dev_warn(&td->client->dev,
				 "broken fw detected, ignoring crc failure\n");
			return 0;
		}

		return -EINVAL;
	}

	return 0;
}

static int mxt_info_block_read(struct mxt *td)
{
	int r;
	int ib_size;
	int num_objects = 0;

	td->obj_count = 0;

	r = mxt_read_reg(td, 0x0000, td->info_block, INFO_BLOCK_HEADER_SIZE);
	if (r < 0)
		return -EIO;

	r = mxt_check_if_mxt(td, td->info_block, INFO_BLOCK_HEADER_SIZE);
	if (r < 0)
		return r;

	num_objects = td->info_block[INFO_BLOCK_NUM_OBJECTS];
	if (num_objects > MAX_OBJ_TABLE_ITEMS) {
		dev_err(&td->client->dev,
			"can only support %d object elements, got %d\n",
			MAX_OBJ_TABLE_ITEMS, num_objects);
		return -EINVAL;
	}

	/* crc starts after last object */
	ib_size = mxt_obj_desc_offset(num_objects) + CRC24_SIZE;

	/* first read has set the address pointer */
	r = mxt_read_block(td, td->client->addr, td->info_block, ib_size);
	if (r < 0)
		return r;

	r = mxt_check_object_table(td);
	if (r < 0) {
		dev_info(&td->client->dev, "check obj table returned %d\n", r);
		return r;
	}

	td->obj_count = num_objects;
	td->flash_mode = false;

	return 0;
}

static int mxt_obj_type(const struct mxt *td, u8 oi)
{
	const u8 *d;

	if (oi >= td->obj_count)
		return -EINVAL;

	d = mxt_obj_desc(td, oi);
	return d[OBJECT_TYPE];
}

static int mxt_obj_index(const struct mxt *td, u8 obj_type)
{
	int i;

	for (i = 0; i < td->obj_count; i++) {
		if (mxt_obj_type(td, i) == obj_type)
			return i;
	}

	return -1;
}

static int mxt_obj_size(const struct mxt *td, u8 oi)
{
	const u8 *d;

	if (oi >= td->obj_count)
		return -EINVAL;

	d = mxt_obj_desc(td, oi);

	return d[OBJECT_SIZE_MINUS_ONE] + 1;
}

static int mxt_obj_addr(const struct mxt *td, u8 oi)
{
	const u8 *d;

	if (oi >= td->obj_count)
		return -EINVAL;

	d = mxt_obj_desc(td, oi);

	return d[OBJECT_ADDRESS_LSB] | (d[OBJECT_ADDRESS_MSB] << 8);
}

static int mxt_obj_num_instances(const struct mxt *td, u8 oi)
{
	const u8 *d;

	if (oi >= td->obj_count)
		return -EINVAL;

	d = mxt_obj_desc(td, oi);

	return d[OBJECT_INST_MINUS_ONE] + 1;
}

static int mxt_obj_num_rids_per_instance(const struct mxt *td, u8 oi)
{
	const u8 *d;

	if (oi >= td->obj_count)
		return -EINVAL;

	d = mxt_obj_desc(td, oi);

	return d[OBJECT_RIDS_PER_INST];
}

static int mxt_obj_num_rids(const struct mxt *td, u8 oi)
{
	const u8 *d;

	if (oi >= td->obj_count)
		return -EINVAL;

	d = mxt_obj_desc(td, oi);

	return (d[OBJECT_INST_MINUS_ONE] + 1) *
		d[OBJECT_RIDS_PER_INST];
}

static int mxt_obj_rid_base(const struct mxt *td, u8 oi, int *count)
{
	int i;
	int s = 1;
	int num_rids = 0;

	for (i = 0; i < td->obj_count; i++) {
		s = s + num_rids;
		num_rids = mxt_obj_num_rids(td, i);

		if (i == oi) {
			if (count)
				*count = num_rids;

			return s;
		}
	}

	return -EINVAL;
}

static int mxt_setup_msg_handlers(struct mxt *td)
{
	int i;
	int j;
	int r;
	u8 obj_type;
	int total = 0;

	for (i = 0; i < td->obj_count; i++) {
		r = mxt_obj_type(td, i);
		if (r < 0)
			return r;

		obj_type = r;

		for (j = 0; j < ARRAY_SIZE(msg_handlers); j++) {
			if (msg_handlers[j].obj_type == obj_type) {
				int count;

				msg_handlers[j].oi = i;

				r = mxt_obj_rid_base(td, i, &count);
				if (r < 0)
					return r;

				msg_handlers[j].rid_first = r;
				msg_handlers[j].rid_count = count;
				total++;

				break;
			}
		}
	}

	return total;
}

static int mxt_set_addr_msg(struct mxt *td)
{
	return mxt_set_fw_addr_ptr(td, td->msg_processor_addr, true);
}

static int mxt_object_params(struct mxt *td, u8 ot,
			     int *oaddr, int *osize, int *oi)
{
	int r;
	int t_oaddr;
	int t_oi;

	r = mxt_obj_index(td, ot);
	if (r < 0)
		return r;

	t_oi = r;

	r = mxt_obj_addr(td, t_oi);
	if (r < 0)
		return r;

	t_oaddr = r;

	r = mxt_obj_size(td, t_oi);
	if (r < 0)
		return r;

	*oi = t_oi;
	*osize = r;
	*oaddr = t_oaddr;

	return 0;
}

/* Non cached reading */
static int mxt_object_read_nc(struct mxt *td, u8 ot, u16 reg,
			      u8 *data, int len)
{
	int oaddr;
	int osize;
	int oi;
	int r;

	r = mxt_object_params(td, ot, &oaddr, &osize, &oi);
	if (r < 0)
		return r;

	if (reg + len > osize) {
		dev_err(&td->client->dev,
			"tried to read %d nc bytes past object %d, offset %d\n",
			len, ot, reg);
		return -EINVAL;
	}

	return mxt_read_reg(td, oaddr + reg, data, len);
}

static int mxt_object_read_c(struct mxt *td, u8 ot, u16 reg,
			     u8 *data, int len)
{
	int oaddr;
	int osize;
	int oi;
	int r;

	r = mxt_object_params(td, ot, &oaddr, &osize, &oi);
	if (r < 0)
		return r;

	if (reg + len > osize) {
		dev_err(&td->client->dev,
			"tried to read %d c bytes past object %d, offset %d\n",
			len, ot, reg);
		return -EINVAL;
	}

	memcpy(data, td->obj_state[oi].d + reg, len);

	return 0;
}

static int mxt_object_read(struct mxt *td, u8 ot, u16 reg,
			   u8 *data, int len)
{
	int r;
	int oi;
	int oaddr;
	int osize;

	r = mxt_object_params(td, ot, &oaddr, &osize, &oi);
	if (r < 0)
		return r;

	if (reg + len > osize) {
		dev_err(&td->client->dev,
			"tried to read %d bytes past object %d, offset %d\n",
			len, ot, reg);
		return -EINVAL;
	}

	if (td->power_enabled) {
		r = mxt_read_reg(td, oaddr + reg, data, len);
		if (r < 0)
			return r;

		if (!td->obj_state[oi].d) {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_warn(&td->client->dev,
					 "no cache for object %d\n", ot);
			return r;
		}

		if (memcmp(data, td->obj_state[oi].d + reg, len)) {
			dev_warn(&td->client->dev,
				 "obj %d inconsistent cache %d!\n",
				 ot, reg);
			memcpy(td->obj_state[oi].d + reg, data, len);
		}

		return r;
	} else {
		if (td->debug_flags & DFLAG_VERBOSE)
			dev_warn(&td->client->dev,
				 "obj %d read %d from cache\n", ot, reg);

		memcpy(data, td->obj_state[oi].d + reg, len);
	}

	return 0;
}

/* Non cached write */
static int mxt_object_write_nc(struct mxt *td, u8 ot, u16 reg,
			       const u8 *data, int len)
{
	int oaddr;
	int osize;
	int oi;
	int r;

	r = mxt_object_params(td, ot, &oaddr, &osize, &oi);
	if (r < 0)
		return r;

	if (reg + len > osize) {
		dev_err(&td->client->dev,
			"tried to write %d bytes past object %d, offset %d\n",
			len, ot, reg);
		return -EINVAL;
	}

	return mxt_write_reg(td, oaddr + reg, data, len, true);
}

static int mxt_object_write(struct mxt *td, u8 ot, u16 reg,
			    const u8 *data, int len)
{
	int r;
	int oi;
	int oaddr;
	int osize;

	r = mxt_object_params(td, ot, &oaddr, &osize, &oi);
	if (r < 0)
		return r;

	if (reg + len > osize) {
		dev_err(&td->client->dev,
			"tried to write %d bytes past object %d, offset %d\n",
			len, ot, reg);
		return -EINVAL;
	}

	if (td->power_enabled) {
		r = mxt_write_reg(td, oaddr + reg, data, len, true);
		if (r < 0)
			return r;
	}

	/* Don't cache writes for cmd processor */
	if (ot == OT_CMD_PROCESSOR)
		return 0;

	if (!td->obj_state[oi].d) {
		if (td->debug_flags & DFLAG_VERBOSE)
			dev_warn(&td->client->dev,
				 "no state for object %d\n", ot);
		return -EIO;
	} else {
		if (memcmp(td->obj_state[oi].d + reg, data, len)) {
			td->obj_state[oi].dirty = 1;
			memcpy(td->obj_state[oi].d + reg, data, len);
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "obj %d write to %d set dirty\n",
					 ot, reg);
		}
	}

	return 0;
}

static int mxt_object_read_u8(struct mxt *td, u8 ot, u16 reg)
{
	u8 b[1];
	int r;

	r = mxt_object_read(td, ot, reg, b, 1);
	if (r < 0)
		return r;

	return (int)b[0];
}

static int mxt_object_read_u16(struct mxt *td, u8 ot, u16 reg)
{
	int r;
	u8 data[2];

	r = mxt_object_read(td, ot, reg, data, 2);
	if (r < 0)
		return r;

	return (int)data[0] | ((int)(data[1]) << 8);
}

static int mxt_object_write_u8(struct mxt *td, u8 ot, u16 reg, u8 val)
{
	return mxt_object_write(td, ot, reg, &val, 1);
}

static int mxt_sw_reset(struct mxt *td, int to_bootloader)
{
	u8 reset = 0x01;
	int r;

	if (to_bootloader)
		reset = 0xa5;

	/* Re-read info block to get the current state */
	r = mxt_info_block_read(td);
	if (r < 0)
		return r;

	r = mxt_object_write_u8(td, OT_CMD_PROCESSOR, CMD_RESET, reset);
	if (r < 0)
		return r;

	msleep(POWER_ON_RESET_WAIT);

	td->num_reset_sw++;

	return r;
}

static int mxt_hw_reset(struct mxt *td)
{
	const struct mxt_platform_data *pd = td->client->dev.platform_data;

	if (pd->reset_gpio == -1)
		return -EINVAL;

	gpio_set_value(pd->reset_gpio, 0);
	udelay(5);
	gpio_set_value(pd->reset_gpio, 1);
	td->num_reset_hw++;
	return 0;
}

static int mxt_reset(struct mxt *td)
{
	const struct mxt_platform_data *pd = td->client->dev.platform_data;
	int r;

	mxt_invalidate_addr(td);

	/* Some boards do not have hw reset line */
	if (pd->reset_gpio != -1)
		r = mxt_hw_reset(td);
	else
		r = mxt_sw_reset(td, 0);

	mxt_uninit(td);

	return r;
}

static int mxt_locate_config_area(struct mxt *td, u16 *start, u16 *len)
{
	u16 s = 0;
	u16 c = 0;
	int i;

	if (td->obj_count == 0)
		return -EINVAL;

	for (i = 0; i < td->obj_count; i++) {
		const u16 addr = mxt_obj_addr(td, i);
		const u16 size = mxt_obj_size(td, i);
		const u8 type = mxt_obj_type(td, i);

		if (type == OT_POWERCONFIG) {
			s = addr;
			c = size;
		} else if (s && ((s + c) == addr)) {
			c += size;
		} else if (s) {
			break;
		}
	}

	if (s == 0 || c == 0)
		return -EINVAL;

	*start = s;
	*len = c;

	return 0;
}

static int mxt_clear_config(struct mxt *td)
{
	int r = -1;
	int i;
	u8 *zd = NULL;
	int count = 0;

	if (td->obj_count == 0)
		return -EINVAL;

	zd = kzalloc(MAX_I2C_WRITE_SIZE, GFP_KERNEL);
	if (zd == NULL)
		return -ENOMEM;

	for (i = 0; i < td->obj_count; i++) {
		const u16 addr = mxt_obj_addr(td, i);
		const u16 size = mxt_obj_size(td, i);
		const u8 type = mxt_obj_type(td, i);

		if (size > MAX_I2C_WRITE_SIZE) {
			dev_err(&td->client->dev,
				"object too big: %d bytes\n",
				size);
			continue;
		}

		r = mxt_write_reg(td, addr, zd, size, false);
		if (r < 0) {
			dev_err(&td->client->dev,
				"object %d error in clear\n", r);
		} else {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "obj %d: at 0x%04x, "
					 "size %2d cleared\n",
					 type, addr, size);
			count++;
		}
	}

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev,
			 "%d objects cleared out of %d\n",
			 count, td->obj_count);

	kfree(zd);

	return r;
}

static int mxt_get_config_crc(struct mxt *td, u32* cfg_crc)
{
	int r;
	u16 l;
	u32 crc = 0;
	u16 start_addr;
	u8 *d = NULL;

	r = mxt_locate_config_area(td, &start_addr, &l);
	if (r < 0)
		return r;

	if (l > 4096)
		return -EINVAL;

	d = kzalloc(l, GFP_KERNEL);
	if (d == NULL)
		return -ENOMEM;

	r = mxt_read_reg(td, start_addr, d, l);
	if (r < 0)
		goto out;

	crc = mxt_crc24(crc, d, l);

out:
	kfree(d);

	if (r >= 0)
		*cfg_crc = crc;

	return r;
}

static int mxt_recalibrate(struct mxt *td)
{
	return mxt_object_write_u8(td, OT_CMD_PROCESSOR, CMD_CALIBRATE, 0x01);
}

static int mxt_area_to_width(const struct mxt *td, const u8 n)
{
	/* We are very likely to get it from lookup table */
	if (likely(n < ARRAY_SIZE(td->tcharea_lt)))
		return td->tcharea_lt[n];

	return int_sqrt(n * td->pixels_per_channel);
}

static inline bool mxt_x_has_10bit_res(const struct mxt *td)
{
	return td->res_x < 1024;
}

static inline bool mxt_y_has_10bit_res(const struct mxt *td)
{
	return td->res_y < 1024;
}

static inline u16 mxt_touch_msg_x(const struct mxt *td, const struct msg *m)
{
	if (mxt_x_has_10bit_res(td))
		return (m->msg[TOUCH_MSG_XPOS_MSB] << 2) |
			((m->msg[TOUCH_MSG_XYPOS_LSB] & 0xc0) >> 6);
	else
		return (m->msg[TOUCH_MSG_XPOS_MSB] << 4) |
			((m->msg[TOUCH_MSG_XYPOS_LSB] & 0xf0) >> 4);
}

static inline u16 mxt_touch_msg_y(const struct mxt *td, const struct msg *m)
{
	if (mxt_y_has_10bit_res(td))
		return m->msg[TOUCH_MSG_YPOS_MSB] << 2 |
			((m->msg[TOUCH_MSG_XYPOS_LSB] & 0x0c) >> 2);
	else
		/* Double parentheses to make checkpatch happy */
		return (m->msg[TOUCH_MSG_YPOS_MSB] << 4) |
			((m->msg[TOUCH_MSG_XYPOS_LSB] & 0x0f));

}

static void mxt_report_events(struct mxt *td)
{
	struct mt_data *s;
	struct input_dev *idev = td->idev;
	int tn;
	int num_contacts;
	int num_released;
	int wx;

	do {
		num_contacts = 0;
		num_released = 0;

		for (tn = 0; tn < MAX_TOUCH_POINTS; tn++) {
			s = &td->finger_state[tn];

			if (s->state == FSTATE_NOT_DETECTED)
				continue;

			num_contacts++;

			if (s->pos_x == 0)
				s->pos_x = 1;

			if (s->pos_y == 0)
				s->pos_y = 1;

			if (tn == 0) {
				input_report_key(idev, BTN_TOUCH, 1);
				input_report_abs(idev, ABS_X, s->pos_x);
				input_report_abs(idev, ABS_Y, s->pos_y);
			}

			input_report_abs(idev, ABS_MT_POSITION_X, s->pos_x);
			input_report_abs(idev, ABS_MT_POSITION_Y, s->pos_y);

			wx = mxt_area_to_width(td, s->area);

			/*
			 * If we have amplitude information, use it to
			 * adjust size
			 */
			if (s->ampl && !(td->touch_ctrl & TOUCH_CTRL_DISAMP))
				wx = (wx * s->ampl) >> 8;
			else
				wx = wx >> 1;

			input_report_abs(idev, ABS_MT_TOUCH_MAJOR, wx);
			input_report_abs(idev, ABS_MT_TRACKING_ID, tn);
			input_mt_sync(idev);

			if (s->state == FSTATE_RELEASED) {
				num_released++;
				s->state = FSTATE_NOT_DETECTED;
			}
		}
		input_sync(idev);
		td->num_syn++;
	} while (num_released);

	if (num_contacts == 0) {
		input_report_key(idev, BTN_TOUCH, 0);
		input_sync(idev);
		td->num_syn++;
	}
}

static int mxt_msg_handler_touch(struct mxt *td, const struct msg *m)
{
	const u8 rid = m->report_id;
	const u8 status = m->msg[TOUCH_MSG_STATUS];
	const int tn = rid - msg_handlers[MSG_HANDLER_INDEX_TOUCH].rid_first;
	struct mt_data *s;

	if (td->debug_flags & DFLAG_TOUCH_MSG)
		dev_info(&td->client->dev,
			 "%d: f 0x%x x %d y %d area %d ampl %d vec %d:%d\n",
			 tn,
			 status,
			 mxt_touch_msg_x(td, m),
			 mxt_touch_msg_y(td, m),
			 m->msg[TOUCH_MSG_AREA],
			 m->msg[TOUCH_MSG_AMPLITUDE],
			 (m->msg[TOUCH_MSG_VECTOR] & 0xf0) >> 4,
			 m->msg[TOUCH_MSG_VECTOR] & 0x0f);

	if (tn < 0 || tn >= MAX_TOUCH_POINTS) {
		dev_warn(&td->client->dev, "no support for %d touchpoint\n",
			 tn);
		return 0;
	}

	td->num_touch_reports++;

	s = &td->finger_state[tn];

	if (s->state == FSTATE_RELEASED) {
		/*
		 * This means that we had a release and press on a
		 * same slot. Flush the state to userspace so that it
		 * can see the release first.
		 */
		mxt_report_events(td);

		if (td->debug_flags & DFLAG_VERBOSE)
			dev_info(&td->client->dev,
				 "update on a released contact %d, 0x%02x\n",
				 tn, status);
	}

	if (status & (1 << 7)) {
		s->state = FSTATE_DETECTED;

		if (!(td->active_mask & (1 << tn))) {
			td->active_mask |= (1 << tn);
			td->last_finger_down_ts = jiffies;
		}
	} else {
		s->state = FSTATE_RELEASED;
		td->active_mask &= ~(1 << tn);

	}

	if (status) {
		s->pos_x = mxt_touch_msg_x(td, m);
		s->pos_y = mxt_touch_msg_y(td, m);

		s->area  = m->msg[TOUCH_MSG_AREA];

		if (status & TOUCH_CTRL_DISAMP)
			s->ampl = m->msg[TOUCH_MSG_AMPLITUDE];

		if (status & TOUCH_CTRL_DISVEC)
			s->vec = m->msg[TOUCH_MSG_VECTOR];
	}

	return 0;
}

static int mxt_msg_handler_proximity(struct mxt *td, const struct msg *m)
{
	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "proximity status: 0x%02x\n",
			 m->msg[0]);

	return 0;
}

static int mxt_msg_handler_cteconfig(struct mxt *td, const struct msg *m)
{
	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "cteconfig status: 0x%02x\n",
			 m->msg[0]);

	if (m->msg[0] & (1 << 0))
		dev_warn(&td->client->dev,
			 "CHKERR: power-up crc error in device\n");

	return 0;
}

static int mxt_get_pin_number(const unsigned long b0, const unsigned long b1)
{
	if (b0)
		return find_first_bit(&b0, 8);

	if (b1)
		return 8 + find_first_bit(&b1, 8);

	return -1;
}

static void mxt_decode_selftest_result(struct mxt *td,
					  const struct msg *m)
{
	const u8 status = m->msg[0];
	const u8 *info = &m->msg[1];

	switch (status) {
	case 0xfe: /* Passed */
		dev_info(&td->client->dev, "selftest: passed\n");
		break;
	case 0xfd: /* Not a valid test */
		dev_warn(&td->client->dev, "selftest: invalid test\n");
		break;
	case 0x01: /* Avdd not present */
		dev_warn(&td->client->dev, "selftest: AVdd not present\n");
		break;
	case 0x11: /* Pin fault */
		dev_warn(&td->client->dev, "selftest: pin fault, "
			 "sequence 0x%02x pin %s%d\n",
			 info[0], (info[1] || info[2]) ? "X" : "Y",
			 (info[1] || info[2]) ?
			 mxt_get_pin_number(info[1], info[2]) :
			 mxt_get_pin_number(info[3], info[4]));
		break;
	case 0x17: /* Signal limit fault */
		dev_warn(&td->client->dev, "selftest: signal limit fault "
			 "object %d, instance %d\n",
			 info[0], info[1]);
		break;
	case 0x20: /* Gain error */
		dev_warn(&td->client->dev, "selftest: gain error pin Y%d\n",
			 mxt_get_pin_number(info[0], info[1]));
		break;
	default:
		dev_warn(&td->client->dev, "selftest: unknown error\n");
	}
}

static int mxt_msg_handler_selftest(struct mxt *td, const struct msg *m)
{
	if (m->msg[0] == 0) {
		dev_err(&td->client->dev,
			"selftest result code zero!\n");
		if (td->selftest.test_num)
			td->selftest.result = SELFTEST_RES_UNKNOWN;
	} else {
		if (td->selftest.test_num) {
			td->selftest.result = m->msg[0];
			memcpy(&td->selftest.info, &m->msg[1], 5);
			dev_info(&td->client->dev,
				 "test 0x%02x: ",
				 td->selftest.test_num);
			complete(&td->selftest_done);
		} else {
			dev_info(&td->client->dev,
				 "internal self test: ");
		}
	}

	mxt_decode_selftest_result(td, m);

	return 0;
}

/* Only works on objects with RPTEN and ENABLE bits! */
static int mxt_set_obj_reports_nc(struct mxt *td, const u8 ot, const bool state)
{
	int r;
	u8 nw;

	r = mxt_object_read_u8(td, ot, CTRL_REG);
	if (r < 0)
		return r;

	if (state) {
		if (r & CTRL_REPORTS) {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "obj: %d reports already enabled\n",
					 ot);
			return 0;
		}

		nw = r | CTRL_REPORTS;
	} else {
		if (!(r & CTRL_REPORTS)) {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "obj: %d reports already disabled\n",
					 ot);
			return 0;
		}

		nw = r & (~CTRL_REPORTS);
	}

	r = mxt_object_write_nc(td, ot, CTRL_REG, &nw, 1);

	return r;
}

static int mxt_wait_for_gesture(struct mxt *td, const u8 gesture)
{
	int r;
	u8 d[2];

	mxt_clear_finger_state(td);

	/* Use noncached write. Reset after wakeup will inject normal state */
	d[0] = CTRL_REPORTS | CTRL_ENABLE;
	r = mxt_object_write_nc(td, OT_ONETOUCHGESTURE, CTRL_REG, d, 1);
	if (r < 0)
		return r;

	r = mxt_set_obj_reports_nc(td, OT_TOUCH, false);
	if (r < 0)
		return r;

	d[0] = d[1] = td->wakeup_interval;

	r = mxt_object_write_nc(td, OT_POWERCONFIG, PCONFIG_IDLEACQINT, d, 2);
	if (r < 0)
		return r;

	td->wakeup_gesture = gesture;

	return 0;
}

static int mxt_got_wakeup_gesture(struct mxt *td)
{
	if (td->wakeup_gesture == 0)
		return 0;

	td->wakeup_gesture = 0;

	mxt_clear_finger_state(td);

	/*
	 * No need to restore state as wait mode was entered with
	 * noncached writes. Reset will rollback to a state prior to
	 * wait mode. See mxt_wait_for_gesture().
	 */

	return mxt_reset(td);
}

static int mxt_msg_handler_onetouchgesture(struct mxt *td, const struct msg *m)
{
	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "onetouchgesture status 0x%02x\n",
			 m->msg[0]);

	if (td->wakeup_gesture && ((m->msg[0] & 0x0f) == td->wakeup_gesture)) {
		int r;

		if (td->debug_flags & DFLAG_VERBOSE)
			dev_info(&td->client->dev, "got wakeup gesture\n");

		r = mxt_got_wakeup_gesture(td);
		if (r < 0)
			dev_err(&td->client->dev,
				"unable to wakeup %d\n", r);
		else
			td->num_gesture_wakeups++;

		input_event(td->idev, EV_MSC, MSC_GESTURE, m->msg[0] & 0x0f);
		input_sync(td->idev);
	}

	return 0;
}

static int mxt_process_msg(struct mxt *td, struct msg *m)
{
	const u8 rid = m->report_id;
	int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(msg_handlers); i++) {
		const u8 last = msg_handlers[i].rid_first +
			msg_handlers[i].rid_count;
		if (rid >= msg_handlers[i].rid_first &&
		    rid < last) {

			if (td->objinit_done == false &&
			    msg_handlers[i].obj_type != OT_CMD_PROCESSOR) {
				dev_warn(&td->client->dev,
					 "msg to uninitialized object %d, "
					 "dropping\n",
					 msg_handlers[i].obj_type);
				return -ENODEV;
			}

			r = msg_handlers[i].handler(td, m);
			if (r < 0 && (td->debug_flags & DFLAG_VERBOSE))
				dev_info(&td->client->dev,
					 "rid %d to %d returned %d\n",
					 rid, msg_handlers[i].obj_type, r);
			return r;
		}
	}

	return 0;
}

static int mxt_selftest(struct mxt *td, struct mxt_selftest *st)
{
	int r;
	u8 cmd[] = { 0x03, 0x00 };

	if (td->selftest.test_num != 0)
		return -EBUSY;

	mutex_lock(&td->lock);

	memset(&td->selftest, 0, sizeof(td->selftest));
	cmd[1] = td->selftest.test_num = st->test_num;
	td->selftest.result = 0;

	init_completion(&td->selftest_done);

	r = mxt_object_write(td, OT_SELFTEST, 0, cmd, 2);
	mutex_unlock(&td->lock);

	if (r < 0)
		goto out;

	r = wait_for_completion_interruptible_timeout(&td->selftest_done,
						      10 * HZ);
	if (r < 0) {
		r = -EINTR;
		goto out;
	} else if (!r) {
		r = -ETIMEDOUT;
		goto out;
	}

	if (td->selftest.result == 0) {
		r = -EINVAL;
		goto out;
	}

	memcpy(st, &td->selftest, sizeof(*st));
	r = 0;

out:
	memset(&td->selftest, 0, sizeof(td->selftest));
	return r;
}

static int mxt_tcharea_init(struct mxt *td)
{
	int i;

	/* We assume here that resolution doesn't change
	 * between resets.
	 */
	if (td->tcharea_lt[0] == 1)
		return 0;

	/*
	 * We need to calculate the amount of pixels per
	 * each channel in sensor matrix. Info block contain
	 * the sensor matrix dimensions.
	 */

	if (td->obj_count == 0 ||
	    td->info_block[4] == 0 ||
	    td->info_block[5] == 0)
		return -ENODEV;

	td->pixels_per_channel = ((td->res_x + 1) * (td->res_y + 1)) /
		(td->info_block[4] * td->info_block[5]);

	/* We use lookup table for most common sizes */
	for (i = 1; i < ARRAY_SIZE(td->tcharea_lt); i++)
		td->tcharea_lt[i] = int_sqrt(i * td->pixels_per_channel);

	td->tcharea_lt[0] = 1;

	return 0;
}

static int mxt_obj_setup_touch(struct mxt *td)
{
	int r;

	r = mxt_object_read_u16(td, OT_TOUCH, TOUCH_XRANGE);
	if (r < 0)
		return r;

	/* Zero means 1023 */
	if (r == 0)
		td->res_x = 1023;
	else
		td->res_x = r;

	r = mxt_object_read_u16(td, OT_TOUCH, TOUCH_YRANGE);
	if (r < 0)
		return r;

	if (r == 0)
		td->res_y = 1023;
	else
		td->res_y = r;

	r = mxt_object_read_u8(td, OT_TOUCH, TOUCH_ORIENT);
	if (r < 0)
		return r;

	if (r & (1 << 0)) {
		int tmp;
		tmp = td->res_x;
		td->res_x = td->res_y;
		td->res_y = tmp;
	}

	r = mxt_object_read_u8(td, OT_TOUCH, TOUCH_CTRL);
	if (r < 0)
		return r;

	td->touch_ctrl = r;

	r = mxt_tcharea_init(td);

	return r;
}

static const struct mxt_obj_config *mxt_plat_obj_config(struct mxt *td, int ot)
{
	const struct mxt_platform_data *pdata = td->client->dev.platform_data;
	const struct mxt_config *config = pdata->config;
	int i;

	if (config == NULL)
		return NULL;

	for (i = 0; i < config->num_objs; i++)
		if (config->obj[i].type == ot)
			return &config->obj[i];

	return NULL;
}

static int mxt_write_obj_config(struct mxt *td, const struct mxt_obj_config *oc)
{
	int size;
	int oi;
	int r;

	oi = mxt_obj_index(td, oc->type);
	if (oi < 0)
		return oi;

	size = mxt_obj_size(td, oi);
	if (size < 0)
		return size;

	if (size != oc->obj_len) {
		if (td->num_reset_handled == 0 ||
		    td->debug_flags & DFLAG_VERBOSE)
			dev_warn(&td->client->dev,
				 "obj %d config data len diff %d vs %d\n",
				 oc->type, oc->obj_len, size);

		/*
		 * If firmware object is bigger than our config data
		 * zero it first so that tail bytes will initialized
		 */
		if (size > oc->obj_len) {
			u8 *zero;
			zero = kzalloc(size, GFP_KERNEL);
			if (zero == NULL)
				return -ENOMEM;

			r = mxt_object_write(td, oc->type, 0, zero, size);
			kfree(zero);
			if (r < 0)
				return r;
		}
	}

	return mxt_object_write(td, oc->type, 0, oc->obj_data,
				min_t(int, size, oc->obj_len));
}

static int mxt_write_config(struct mxt *td, const struct firmware *fw)
{
	int r;
	unsigned i = 0;
	const u8 *d;
	unsigned cfg_bytes = 0;
	const int fw_len = fw->size;

	if (!td->power_enabled)
		dev_warn(&td->client->dev, "config write while powers off\n");

	if (td->wakeup_gesture)
		dev_warn(&td->client->dev, "config write while gesture wait\n");

	do {
		struct mxt_obj_config ocfg;

		if (fw_len - i <= 2) {
			dev_warn(&td->client->dev,
				 "object entry too small at %d\n", i);
			return -EINVAL;
		}

		d = fw->data + i;
		ocfg.type = d[0];
		ocfg.obj_len = d[1] + 1;

		d += 2;
		i += 2;

		ocfg.obj_data = d;

		if (ocfg.obj_len > fw_len - i) {
			dev_warn(&td->client->dev, "size too big %d\n",
				 ocfg.obj_len);
			return -EINVAL;
		}

		if (td->debug_flags & DFLAG_VERBOSE)
			dev_info(&td->client->dev, "obj: %d, len %d:\n",
				 ocfg.type, ocfg.obj_len);

		r = mxt_write_obj_config(td, &ocfg);
		/* If we fail just skip to the next object */
		if (r < 0)
			dev_warn(&td->client->dev,
				 "failed to write object %d, error %d\n",
				 ocfg.type, r);

		cfg_bytes += ocfg.obj_len;
		i += ocfg.obj_len;
	} while (i < fw_len);

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "wrote total %d cfg bytes\n",
			 cfg_bytes);

	if (i != fw_len) {
		dev_warn(&td->client->dev,
			 "config size mismatch %d vs %d\n", i, fw_len);
		return -EIO;
	}

	return r;
}

static int mxt_init_object_state(struct mxt *td, int ot, int oi, int osize)
{
	const struct mxt_obj_config *p;
	int r;

	if (td->obj_state[oi].d && td->obj_state[oi].len != osize) {
		kfree(td->obj_state[oi].d);
		td->obj_state[oi].d = NULL;
		td->obj_state[oi].len = 0;
	}

	if (!td->obj_state[oi].d) {
		if (td->debug_flags & DFLAG_VERBOSE)
			dev_info(&td->client->dev,
				 "obj: %d allocating %d bytes\n", ot, osize);

		td->obj_state[oi].d = kzalloc(osize, GFP_KERNEL);
		if (td->obj_state[oi].d == NULL)
			return -ENOMEM;

		r = mxt_object_read_nc(td, ot, 0, td->obj_state[oi].d, osize);
		if (r < 0) {
			kfree(td->obj_state[oi].d);
			td->obj_state[oi].d = NULL;
			td->obj_state[oi].len = 0;
			return r;
		}

		td->obj_state[oi].dirty = 0;
		td->obj_state[oi].len = osize;
	}

	if (td->num_reset_handled == 0) {
		p = mxt_plat_obj_config(td, ot);

		/*
		 * If platform configuration exists for this object use it
		 * for initial object state. If not, fallback to firmware
		 * configuration state.
		 */
		if (p) {
			/* Check if platform config is identical fw state */
			if (memcmp(td->obj_state[oi].d, p->obj_data,
				   min_t(int, osize, p->obj_len))) {
				memcpy(td->obj_state[oi].d, p->obj_data,
				       min_t(int, osize, p->obj_len));
				td->obj_state[oi].dirty = 1;
				if (td->debug_flags & DFLAG_VERBOSE)
					dev_info(&td->client->dev,
						 "obj %d init from pcfg\n", ot);
			}
		} else {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "obj %d init from fw config\n", ot);
		}
	}

	return 0;
}

static int mxt_init_object_states(struct mxt *td)
{
	int i;
	int r;

	if (td->obj_count <= 0)
		return -ENODEV;

	for (i = 0; i < td->obj_count; i++) {
		int ot;
		int osize = mxt_obj_size(td, i);
		if (osize < 0)
			return osize;

		ot = mxt_obj_type(td, i);
		if (ot < 0)
			return ot;

		/* Don't cache cmd processor state */
		if (ot == OT_CMD_PROCESSOR)
			continue;

		r = mxt_init_object_state(td, ot, i, osize);
		if (r < 0)
			return r;

		if (td->obj_state[i].dirty) {
			r = mxt_object_write_nc(td, ot, 0, td->obj_state[i].d,
						osize);
			if (r < 0)
				return r;

			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "obj %d state written to fw\n", ot);
		}
	}

	return 0;
}

static int mxt_clear_object_states(struct mxt *td)
{
	int i;

	if (td->obj_count <= 0)
		return -ENODEV;

	for (i = 0; i < td->obj_count; i++) {
		if (td->obj_state[i].d) {
			kfree(td->obj_state[i].d);
			td->obj_state[i].d = NULL;
			td->obj_state[i].len = 0;
			td->obj_state[i].dirty = 0;
		}
	}

	return 0;
}

static int mxt_setup_objects(struct mxt *td)
{
	int r;

	r = mxt_init_object_states(td);
	if (r < 0)
		return r;

	r = mxt_obj_setup_touch(td);
	if (r < 0)
		return r;

	if (td->wakeup_gesture)
		r = mxt_wait_for_gesture(td, td->wakeup_gesture);

	return r;
}

static int mxt_set_input_dev_params(struct mxt *td)
{
	struct input_dev *dev;

	dev = td->idev;
	if (!dev)
		return -ENODEV;

	set_bit(EV_ABS, dev->evbit);
	set_bit(EV_KEY, dev->evbit);
	set_bit(EV_MSC, dev->evbit);

	set_bit(ABS_MT_TOUCH_MAJOR, dev->absbit);
	set_bit(ABS_MT_POSITION_X, dev->absbit);
	set_bit(ABS_MT_POSITION_Y, dev->absbit);
	set_bit(ABS_MT_TRACKING_ID, dev->absbit);
	set_bit(BTN_TOUCH, dev->keybit);
	set_bit(MSC_GESTURE, dev->mscbit);

	input_set_abs_params(dev, ABS_X, 0, td->res_x, 0, 0);
	input_set_abs_params(dev, ABS_Y, 0, td->res_y, 0, 0);

	input_set_abs_params(dev, ABS_MT_POSITION_X,
			     0, td->res_x, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y,
			     0, td->res_y, 0, 0);

	input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR,
			     0,
			     max(td->res_x, td->res_y),
			     0, 0);

	input_set_abs_params(dev, ABS_MT_TRACKING_ID, 0,
			     MAX_TOUCH_POINTS - 1, 0, 0);

	snprintf(td->phys, sizeof(td->phys), "%s/input0",
		 dev_name(&td->client->dev));

	dev->name = DRIVER_DESC;
	dev->phys = td->phys;

	dev->dev.parent = &td->client->dev;

	return 0;
}

static void mxt_unregister_input_device(struct mxt *td)
{
	if (td->idev) {
		input_unregister_device(td->idev);
		td->idev = NULL;
	}
}

static int mxt_register_input_device(struct mxt *td)
{
	int r;

	if (td->idev)
		return -EINVAL;

	td->idev = input_allocate_device();
	if (td->idev == NULL)
		return -ENOMEM;

	r = mxt_set_input_dev_params(td);
	if (r != 0)
		goto err;

	r = input_register_device(td->idev);
	if (r < 0)
		goto err;

	return 0;

err:
	input_free_device(td->idev);
	td->idev = NULL;

	return r;
}

static int mxt_preinit(struct mxt *td)
{
	int r;

	r = mxt_info_block_read(td);
	if (r < 0) {
		int f;

		f = mxt_flash_read_status(td);
		if (f >= 0) {
			dev_warn(&td->client->dev,
				 "controller in flash mode, status 0x%x\n", f);
			td->flash_mode = true;
		}

		return r;
	}

	r = mxt_obj_index(td, OT_MSG_PROCESSOR);
	if (r < 0)
		return r;

	r = mxt_obj_addr(td, r);
	if (r < 0)
		return r;

	td->msg_processor_addr = r;

	r = mxt_setup_msg_handlers(td);
	if (r < 0) {
		dev_err(&td->client->dev, "msg handler setup error %d\n", r);
		return -ENODEV;
	}

	r = mxt_set_addr_msg(td);
	if (r < 0) {
		dev_err(&td->client->dev,
			"failed to set msg processor addr\n");
		return r;
	}

	td->preinit_done = true;

	return 0;
}

static int mxt_object_init(struct mxt *td)
{
	int r;

	if (td->obj_count == 0)
		dev_warn(&td->client->dev, "object_init without preinit\n");

	r = mxt_setup_objects(td);
	if (r < 0)
		return r;

	r = mxt_set_addr_msg(td);
	if (r < 0) {
		dev_err(&td->client->dev,
			"failed to set msg processor addr\n");
		return r;
	}

	if (td->idev == NULL)
		r = mxt_register_input_device(td);
	else
		r = 0;

	td->objinit_done = true;

	return r;
}

static int mxt_backup_settings(struct mxt *td)
{
	int r;

	r = mxt_object_write_u8(td, OT_CMD_PROCESSOR, CMD_BACKUPNV, 0x55);
	if (r < 0)
		return r;

	td->num_backup_nv++;

	return mxt_clear_object_states(td);
}

static int mxt_handle_config_error(struct mxt *td)
{
	int r;
	u32 config_crc;

	td->err_cfg++;

	/* If there are no objects, we can't do anything */
	if (!td->objinit_done)
		return -ENODEV;

	r = mxt_get_config_crc(td, &config_crc);
	if (r < 0)
		return r;

	/*
	 * It might be impossible to recover even if settings are
	 * stored into nonvolatile memory as they might be just plain
	 * broken. So backup settings only once
	 */
	if (td->num_backup_nv == 0 && (td->nonvol_cfg_crc != config_crc))
		return mxt_backup_settings(td);
	else if (td->num_backup_nv == 1)
		return mxt_reset(td);

	/*
	 * If backup settings didn't help, only way forward is to
	 * ignore the error and let user correct the configuration
	 */

	return 0;
}

static int mxt_msg_handler_cmd(struct mxt *td, const struct msg *m)
{
	static unsigned long cfg_error_warntime;

	td->nonvol_cfg_crc = m->msg[1] | (m->msg[2] << 8) |
		(m->msg[3] << 16);

	if (m->msg[0] & MSG_STATUS_RESET) {
		int r;

		r = mxt_object_init(td);
		if (r < 0)
			dev_err(&td->client->dev, "init error %d\n", r);

		if (r == 0)
			td->num_reset_handled++;

		mxt_print_version(td);
	}

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "cfg 0x%x status 0x%02x\n",
			 td->nonvol_cfg_crc, m->msg[0]);

	if (m->msg[0] & MSG_STATUS_COMSERR) {
		dev_info(&td->client->dev, "i2c crc error\n");
		td->err_i2c_crc++;
		mxt_reset(td);
	}

	if (m->msg[0] & MSG_STATUS_CFGERR) {
		int r;

		if (printk_timed_ratelimit(&cfg_error_warntime, 15 * 1000))
			dev_warn(&td->client->dev, "configuration error\n");

		r = mxt_handle_config_error(td);
		if (r < 0)
			dev_err(&td->client->dev,
				"can't handle config error: %d\n", r);
	}

	if (m->msg[0] & MSG_STATUS_CALIBRATION)
		td->num_cals++;

	if (m->msg[0] & MSG_STATUS_SIGERR) {
		dev_warn(&td->client->dev, "acquisition error\n");
		td->err_sigerr++;
	}

	if (m->msg[0] & MSG_STATUS_OVERFLOW)
		td->err_overflow++;

	return 0;
}

static void mxt_show_msg(struct mxt *td, const struct msg *m)
{
	dev_info(&td->client->dev,
		 "rid: %d %02x %02x %02x %02x %02x %02x %02x (%02x)\n",
		 m->report_id, m->msg[0], m->msg[1], m->msg[2], m->msg[3],
		 m->msg[4], m->msg[5], m->msg[6], m->checksum);
}

static int mxt_message_waiting(struct mxt *td)
{
	const struct mxt_platform_data *pd = td->client->dev.platform_data;

	if (pd->int_gpio != -1)
		return gpio_get_value(pd->int_gpio) == 0;

	/*
	 * If we don't have CHG (interrupt) gpio, we rely on message id 255
	 * to signal when everything was read.
	 */
	return 1;
}

static int mxt_check_valid_state(struct mxt *td)
{
	int r;

	if (td->power_enabled == 0) {
		dev_warn(&td->client->dev, "irq while powers off\n");
		return 0;
	}

	if (!td->preinit_done) {
		r = mxt_preinit(td);
		if (r < 0) {
			dev_err(&td->client->dev,
				"error in preinit %d\n", r);
			return r;
		}
	}

	return 0;
}

static irqreturn_t mxt_irq_thread(int irq, void *dev_id)
{
	struct mxt *td = dev_id;
	struct msg m;
	int r;
	int msg_count;

	td->num_work++;

	mutex_lock(&td->lock);

	r = mxt_check_valid_state(td);
	if (r < 0) {
		td->err_init++;
		goto out;
	}

	msg_count = 0;
	td->num_touch_reports = 0;

	while (mxt_message_waiting(td)) {
		u8 msg_crc;

		if (++msg_count > MAX_MSG_PER_INTERRUPT) {
			dev_err(&td->client->dev,
				"max msgs per intr reached, resetting\n");
			mxt_reset(td);
			goto out;
		}

		/*
		 * FW sets the read pointer correctly by itself
		 * for consecutive messages. Check is still needed if
		 * some message handler did change it by reading.
		 */
		if (mxt_fw_addr_ptr(td) != td->msg_processor_addr) {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "read address was 0x%x, "
					 "should be 0x%x\n",
					 mxt_fw_addr_ptr(td),
					 td->msg_processor_addr);

			r = mxt_set_addr_msg(td);
			if (r < 0) {
				dev_err(&td->client->dev,
					"failed to set msg processor addr\n");
				goto out;
			}
		}

		r = mxt_read_block(td, td->client->addr, m.data,
				   sizeof(struct msg));
		if (r < 0) {
			/* We couldn't read due to flashing? */
			dev_err(&td->client->dev,
				"msg read error %d, (flashing mode?)\n", r);
			goto out;
		}

		/*
		 * If chip got reset without driver knowing it, the read
		 * address is not set by driver and thus no crc calculation was
		 * requested giving us zero as a checksum. This is not
		 * 100% watertight. But it is as good as we can get
		 * until display and touch HW reset is properly separated.
		 * Also this is for statisical purposes for now.
		 */
		if (m.checksum) {
			msg_crc = mxt_crc8(m.data, 8);
			if (msg_crc != m.checksum) {
				dev_warn(&td->client->dev,
					 "msg crc fail: %x vs %x calc\n",
					 m.checksum, msg_crc);
				td->err_msg_crc++;
				mxt_invalidate_addr(td);
				mxt_show_msg(td, &m);
				/* Drop msg */
				continue;
			}
		}

		if (likely(m.report_id)) {
			/* Check if no more messages */
			if (m.report_id == 255) {
				td->num_msg_255++;
				goto out;
			}

			r = mxt_process_msg(td, &m);
			if (r < 0) {
				dev_warn(&td->client->dev,
					 "message processing error %d\n", r);
				mxt_show_msg(td, &m);
				td->num_msg_err++;
			}
			td->num_msg++;
		} else {
			dev_err(&td->client->dev,
				"got report id of zero\n");
			mxt_show_msg(td, &m);
			mxt_invalidate_addr(td);
			td->num_msg_zero++;
		}
	}
out:
	if (td->num_touch_reports) {
		if (td->rlimit_min_interval == 0 ||
		    td->active_mask != td->last_active_mask ||
		    jiffies_to_usecs(jiffies - td->last_finger_down_ts) <
		    td->rlimit_bypass_time ||
		    jiffies_to_usecs(jiffies - td->last_report_ts) >
		    td->rlimit_min_interval) {
			mxt_report_events(td);

			td->last_report_ts = jiffies;
			td->last_active_mask = td->active_mask;
		}
	}

	mutex_unlock(&td->lock);

	if (r < 0)
		dev_warn(&td->client->dev, "handler returned with %d\n", r);

	return IRQ_HANDLED;
}

static irqreturn_t mxt_isr(int irq, void *data)
{
	struct mxt *td = data;
	td->num_isr++;

	return IRQ_WAKE_THREAD;
}

static int mxt_disable_selftests(struct mxt *td)
{
	const u8 d[1] = { 0x00 };

	return mxt_object_write_nc(td, OT_SELFTEST, 0, d, 1);
}

static int mxt_enter_deep_sleep(struct mxt *td)
{
	const u8 d[2] = { 0x00, 0x00 };

	return mxt_object_write_nc(td, OT_POWERCONFIG, 0, d, 2);
}

static void mxt_disable(struct mxt *td)
{
	mutex_lock(&td->lock);

	if (!td->power_enabled || td->flash_mode)
		goto out;

	td->power_enabled = 0;

	mutex_unlock(&td->lock);
	disable_irq(td->client->irq);
	mutex_lock(&td->lock);

	disable_irq_wake(td->client->irq);

	/*
	 * By default selftest object would complain
	 * if we remove the Avdd from the chip. Disable selftests
	 * while in deep sleep.
	 */
	mxt_disable_selftests(td);
	mxt_enter_deep_sleep(td);
	mxt_uninit(td);
	regulator_bulk_disable(ARRAY_SIZE(td->regs), td->regs);
out:
	mutex_unlock(&td->lock);
}

static void mxt_enable(struct mxt *td, bool wait_for_attn)
{
	const struct mxt_platform_data *pd = td->client->dev.platform_data;

	mutex_lock(&td->lock);

	if (td->power_enabled)
		goto out;

	if (pd->reset_gpio != -1)
		gpio_set_value(pd->reset_gpio, 0);

	enable_irq_wake(td->client->irq);
	enable_irq(td->client->irq);

	regulator_bulk_enable(ARRAY_SIZE(td->regs), td->regs);
	mxt_uninit(td);

	/* Wait for voltage to stabilize */
	msleep(2);

	/*
	 * We can't rely solely on power on reset because regulators
	 * could have been enabled already
	 */
	if (pd->reset_gpio != -1) {
		gpio_set_value(pd->reset_gpio, 1);
		td->num_reset_hw++;
	} else {
		int r;
		/* We need to wait possible POR before we can issue sw reset */
		msleep(POWER_ON_RESET_WAIT);
		r = mxt_sw_reset(td, 0);
		if (r < 0)
			dev_warn(&td->client->dev,
				 "failed to issue enable sw reset: %d\n", r);
	}

	if (wait_for_attn) {
		if (pd->int_gpio == -1) {
			msleep(POWER_ON_RESET_WAIT);
		} else {
			int i = 0;
			while ((i++ < 30) &&
			       gpio_get_value(pd->int_gpio))
				msleep(10);

			if (gpio_get_value(pd->int_gpio))
				dev_warn(&td->client->dev,
					 "attn timeout on enable\n");
		}
	}

	td->power_enabled = 1;
out:
	mutex_unlock(&td->lock);
}

enum {
	FS_WAITING_BOOTLOAD_CMD = 0,
	FS_WAITING_FRAME_DATA,
	FS_FRAME_CRC_CHECK,
	FS_FRAME_CRC_FAIL,
	FS_FRAME_CRC_PASS,
	FS_APPL_CRC_FAIL
};

static const char * const fs_str[] = {
	"FS_WAITING_BOOTLOAD_CMD",
	"FS_WAITING_FRAME_DATA",
	"FS_FRAME_CRC_CHECK",
	"FS_FRAME_CRC_FAIL",
	"FS_FRAME_CRC_PASS",
	"FS_APPL_CRC_FAIL"
};

static int mxt_flash_decode_status(struct mxt *td, const u8 status)
{
	if (status & (1 << 7)) {
		if (status & (1 << 6))
			return FS_WAITING_BOOTLOAD_CMD;
		else
			return FS_WAITING_FRAME_DATA;
	}

	if (status == 0x02)
		return FS_FRAME_CRC_CHECK;

	if (status == 0x03)
		return FS_FRAME_CRC_FAIL;

	if (status == 0x04)
		return FS_FRAME_CRC_PASS;

	if (status == (1 << 6))
		return FS_APPL_CRC_FAIL;

	return -1;
}

static int mxt_flash_status(struct mxt *td)
{
	int r;

	r = mxt_flash_read_status(td);
	if (r < 0)
		return r;

	if (td->debug_flags == DFLAG_VERBOSE)
		dev_info(&td->client->dev, "flash raw status 0x%x\n", r);

	r = mxt_flash_decode_status(td, r);
	if (r < 0)
		return r;

	if (td->debug_flags == DFLAG_VERBOSE)
		dev_info(&td->client->dev, "FS: %s (%d)\n", fs_str[r], r);

	return r;
}

static void mxt_force_flash_mode(struct mxt *td)
{
	const struct mxt_platform_data *pd = td->client->dev.platform_data;
	int i;

	/*
	 * We need to toggle reset line ten times to force
	 * controller to go into flash mode.
	 */
	for (i = 0; i < 10; i++) {
		gpio_set_value(pd->reset_gpio, 0);
		/* Keep the reset down atleast 1us */
		udelay(20);
		gpio_set_value(pd->reset_gpio, 1);
		/* Need to wait atleast 50ms before new reset can be issued */
		msleep(55);
	}

	/* Chip should be ready for communications after 100ms */
	msleep(100);
}

static int mxt_flash_wait_for_chg(struct mxt *td,
				  unsigned long timeout_usecs,
				  int state)
{
	const struct mxt_platform_data *pd = td->client->dev.platform_data;
	const unsigned long one_wait = 20;
	unsigned long waited = 0;
	int r;

	do {
		if (pd->int_gpio != -1) {
			r = gpio_get_value(pd->int_gpio);
		} else {
			msleep(100);
			r = state;
		}

		if (r == state)
			break;

		udelay(one_wait);
		waited += one_wait;
	} while (waited < timeout_usecs);

	if (waited > 400000 ||
	    waited >= timeout_usecs ||
	    (td->debug_flags & DFLAG_VERBOSE))
		dev_info(&td->client->dev, "waited %lu usecs for chg\n",
			 waited);

	return r == state ? 0 : -1;
}

static int mxt_flash_reset(struct mxt *td)
{
	const u8 reset_cmd[2] = { 0x00, 0x00 };
	dev_info(&td->client->dev, "flash mode reset\n");
	return mxt_flash_write(td, reset_cmd, 2);
}

static int mxt_flash_enter(struct mxt *td)
{
	int r;
	int count = 10;

	/*
	 * Try first with sw reset and if that doesn't help,
	 * use the reset toggling
	 */
	while (count-- > 0) {
		if (count > 5)
			mxt_sw_reset(td, 1);
		else
			mxt_force_flash_mode(td);

		msleep(100);

		r = mxt_flash_status(td);
		if (r == FS_WAITING_BOOTLOAD_CMD)
			break;
	}

	if (r == FS_WAITING_BOOTLOAD_CMD) {
		td->flash_mode = true;
		return 0;
	}

	return -EIO;
}

static void mxt_flash_leave(struct mxt *td)
{
	int r;

	r = mxt_flash_read_status(td);
	if (r >= 0)
		mxt_flash_reset(td);

	mxt_uninit(td);
	mxt_clear_info_block(td);
	td->num_backup_nv = 0;

	mxt_flash_wait_for_chg(td, 500 * 1000, 0);

	td->flash_mode = false;
}

static int mxt_flash_unlock(struct mxt *td)
{
	const u8 unlock[2] = { 0xdc, 0xaa };
	int r;

	r = mxt_flash_write(td, unlock, 2);
	if (r < 0)
		dev_err(&td->client->dev, "error writing unlock %d\n", r);

	return r;
}

static int mxt_flash_frames(struct mxt *td, const struct firmware *fw)
{
	const int len = fw->size;
	const u8 *d = fw->data;
	int r;
	int i;
	int retry_count = 0;

	i = 0;
	r = -1;

	if (len < 0 || len > 1024*1024 || !d)
		return -EINVAL;

	while (i < (len-1)) {
		const u16 block_len = (d[i] << 8) | d[i+1];

		if (block_len == 0 || block_len > 276) {
			dev_err(&td->client->dev, "invalid block len %d\n",
				block_len);
			return -EINVAL;
		}

		r = mxt_flash_wait_for_chg(td, 1000*1000, 0);
		if (r < 0) {
			dev_err(&td->client->dev, "CHG not asserted\n");
			goto out;
		}

		r = mxt_flash_status(td);
		if (r < 0) {
			dev_err(&td->client->dev, "can't read status %d\n", r);
			return r;
		}

		if (r != FS_WAITING_FRAME_DATA) {
			dev_err(&td->client->dev, "not waiting data %d\n", r);
			return -EIO;
		}

		if (i + block_len >= len) {
			dev_err(&td->client->dev, "block out of bounds\n");
			return -EINVAL;
		}

		r = mxt_flash_write(td, d + i, block_len + 2);
		if (r < 0) {
			dev_err(&td->client->dev, "error %d writing data\n", r);
			return r;
		}

		r = mxt_flash_wait_for_chg(td, 1000*1000, 0);
		if (r < 0) {
			dev_err(&td->client->dev, "CHG not asserted\n");
			goto out;
		}

		r = mxt_flash_status(td);
		if (r < 0) {
			dev_err(&td->client->dev, "can't read status %d\n", r);
			goto out;
		}

		if (r == FS_FRAME_CRC_CHECK) {
			r = mxt_flash_wait_for_chg(td, 1000*1000, 0);
			if (r < 0) {
				dev_err(&td->client->dev, "CHG not asserted\n");
				goto out;
			}

			r = mxt_flash_status(td);
			if (r < 0) {
				dev_err(&td->client->dev,
					"can't read status %d\n", r);
				return r;
			}
		}

		if (r == FS_FRAME_CRC_FAIL) {
			dev_err(&td->client->dev,
				"retry %d: crc failed in index %d\n",
				retry_count, i);
			retry_count++;
			if (retry_count > 10)
				return r;
			else
				continue;
		}

		if (r == FS_FRAME_CRC_PASS) {
			retry_count = 0;
			i += block_len + 2;

			dev_info(&td->client->dev,
				 "flashing %5d bytes "
				 "%3d%% (%d of %d) (%s)\n",
				 block_len, i * 100 / len, i,
				 len, r > 0 && r < 6 ? fs_str[r] : "unknown");
		} else {
			goto out;
		}
	}

out:
	dev_info(&td->client->dev, "last flash status %s (%d), "
		 "%d done of %d\n", r > 0 && r < 6 ?
		 fs_str[r] : "unknown", r, i, len);

	if (r == FS_FRAME_CRC_PASS && i == len)
		return 0;

	return -1;
}

static int mxt_flash_firmware(struct mxt *td, const struct firmware *fw)
{
	int r;

	mutex_lock(&td->lock);

	r = mxt_flash_enter(td);
	if (r < 0)
		goto out;

	r = mxt_flash_unlock(td);
	if (r < 0)
		goto out;

	r = mxt_flash_frames(td, fw);
	if (r < 0)
		goto out;

out:
	mxt_flash_leave(td);
	mutex_unlock(&td->lock);

	return r;
}

static int mxt_diag_get_page(struct mxt *td, u8 mode, u8 page, u8 *data, int l)
{
	u8 hdr[2];
	int r;
	unsigned long wait_start;

	if (l < 0 || l > DIAG_PAGE_LEN)
		return -EINVAL;

	wait_start = jiffies;

	/*
	 * Apparently controller is slow to get the values.
	 * We need to retry/wait until the right page appears.
	 * There is no mentioning of having to wait in protocol guide.
	 * Assumption is that the next sensor sampling will trigger
	 * the page change. So all we can do is wait it to happen.
	 */
	while (1) {
		r = mxt_object_read_nc(td, OT_DEBUG_DIAG, 0,
				hdr, sizeof(hdr));
		if (r < 0)
			goto out;

		if (hdr[DIAG_MODE] == mode && hdr[DIAG_PAGE] == page)
			break;

		if (jiffies_to_msecs(jiffies - wait_start) > 500)
			break;

		/* Let's wait for controller to get data ready */
		msleep(5);
	}

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev,
			"got page %d in %d ms\n", page,
			jiffies_to_msecs(jiffies - wait_start));

	if (hdr[DIAG_MODE] != mode) {
		dev_err(&td->client->dev,
			"diag mode differs 0x%02x vs 0x%02x\n",
			mode, hdr[DIAG_MODE]);
		r = -EIO;
		goto out;
	}

	if (hdr[DIAG_PAGE] != page) {
		dev_err(&td->client->dev,
			"diag page differs 0x%02x vs 0x%02x\n",
			page, hdr[DIAG_PAGE]);
		r = -EIO;
		goto out;
	}

	if (l)
		r = mxt_object_read_nc(td, OT_DEBUG_DIAG, DIAG_DATA_START,
				data, l);
	else
		r = 0;

out:
	return r;
}

static int mxt_diag_write_cmd(struct mxt *td, u8 cmd)
{
	int r;
	u8 rb;
	unsigned long wait_start;

	r = mxt_object_write_nc(td, OT_CMD_PROCESSOR, CMD_DIAG, &cmd, 1);
	if (r < 0)
		return r;

	wait_start = jiffies;

	while (1) {
		r = mxt_object_read_nc(td, OT_CMD_PROCESSOR, CMD_DIAG, &rb, 1);
		if (r < 0)
			return r;

		if (rb == 0)
			break;

		if (jiffies_to_msecs(jiffies - wait_start) > 500)
			break;

		msleep(5);
	}

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev,
			"cmd %d %s in %d ms\n", cmd, (r == 0) ? "" : "timeout",
			jiffies_to_msecs(jiffies - wait_start));

	if (r >= 0 && rb == 0)
		return 0;

	return -EIO;
}

static int mxt_diag_get_values(struct mxt *td, u8 mode, char *buf, ssize_t size)
{
	ssize_t l = 0;
	u8 *d;
	int i;
	int r;
	int num_vals;
	int bytes_per_value;

	if (mode != DIAG_REFS &&
	    mode != DIAG_DELTAS &&
	    mode != DIAG_GAINS &&
	    mode != DIAG_XCOMMONS)
		return -EINVAL;

	if (!td->preinit_done)
		return -ENODEV;

	if (mode == DIAG_GAINS) {
		const u8 cal_cmd[1] = { CTECONFIG_CMD_CALIBRATE };

		num_vals = 2 + mxt_lines_y(td);
		bytes_per_value = 1;

		/* Gains need to be measured explicitly */
		r = mxt_object_write_nc(td, OT_CTECONFIG, CTECONFIG_CMD,
					cal_cmd, sizeof(cal_cmd));
		if (r < 0)
			goto out_nofree;
	} else if (mode == DIAG_XCOMMONS) {
		/* For some odd reason xcommons are in page 1 */
		num_vals = (DIAG_PAGE_LEN >> 1) + mxt_lines_x(td);
		bytes_per_value = 2;
	} else {
		num_vals = mxt_lines_x(td) * mxt_lines_y(td);
		bytes_per_value = 2;
	}

	d = kmalloc(num_vals * bytes_per_value, GFP_KERNEL);
	if (d == NULL)
		return -ENOMEM;

	r = mxt_diag_write_cmd(td, mode);
	if (r < 0)
		goto out;

	for (i = 0; i < DIAG_MAX_PAGES; i++) {
		int bytes_to_read = (num_vals * bytes_per_value) -
			(i * DIAG_PAGE_LEN);

		if (bytes_to_read >= DIAG_PAGE_LEN)
			bytes_to_read = DIAG_PAGE_LEN;

		r = mxt_diag_get_page(td, mode, i,
				      d + (i * DIAG_PAGE_LEN), bytes_to_read);

		if (bytes_to_read < DIAG_PAGE_LEN)
			break;

		if (i != (DIAG_MAX_PAGES - 1)) {
			r = mxt_diag_write_cmd(td, DIAG_PAGEUP);
			if (r < 0)
				goto out;
		}
	}

	switch (mode) {
	case DIAG_REFS:
	{
		u16 v;

		/* With some firmwares adjusting with xcommons is needed */
		if (mxt_firmware_version(td) == 0x11) {
			u8 *xc;

			xc = kmalloc(mxt_lines_x(td) * 2, GFP_KERNEL);
			if (xc == NULL) {
				r = -ENOMEM;
				goto out;
			}

			r = mxt_diag_write_cmd(td, DIAG_XCOMMONS);
			if (r < 0)
				goto endref;

			/*
			 * Don't read. Just make sure page has changed.
			 * Seems that issuing page up command too quickly will
			 * get firmware to ignore it.
			 */
			r = mxt_diag_get_page(td, DIAG_XCOMMONS, 0, NULL, 0);
			if (r < 0)
				goto endref;

			r = mxt_diag_write_cmd(td, DIAG_PAGEUP);
			if (r < 0)
				goto endref;

			r = mxt_diag_get_page(td, DIAG_XCOMMONS, 1,
					      xc, mxt_lines_x(td) * 2);
			if (r < 0)
				goto endref;

			for (i = 0; i < num_vals; i++) {
				const int xline = i / mxt_lines_y(td);
				int real_ref;
				u16 com;

				if (xline >= mxt_lines_x(td)) {
					r = -EINVAL;
					goto endref;
				}

				v = (int)d[i * 2] | ((int)(d[i * 2 + 1]) << 8);
				com = (int)xc[xline * 2] |
					((int)(xc[xline * 2 + 1]) << 8);
				real_ref = v + com - 65536;
				d[i * 2] = real_ref & 0xff;
				d[i * 2 + 1] = (real_ref & 0xff00) >> 8;
			}

endref:
			kfree(xc);
			if (r < 0)
				goto out;
		}

		for (i = 0; i < num_vals; i++) {
			v = (int)d[i * 2] | ((int)(d[i * 2 + 1]) << 8);
			l += snprintf(buf + l, size - l, "%d,", v);
		}

	}
	break;

	case DIAG_XCOMMONS:
	{
		u16 v;
		num_vals = mxt_lines_x(td);

		for (i = 0; i < num_vals; i++) {
			v = (int)d[DIAG_PAGE_LEN + i * 2] |
				((int)(d[DIAG_PAGE_LEN + i * 2 + 1]) << 8);
			l += snprintf(buf + l, size - l, "%d,", v);
		}
	}
	break;

	case DIAG_DELTAS:
	{
		s16 v;

		for (i = 0; i < num_vals; i++) {
			v = (int)d[i * 2] | ((int)(d[i * 2 + 1]) << 8);
			l += snprintf(buf + l, size - l, "%d,", v);
		}
	}
	break;

	case DIAG_GAINS:
	{
		u8 v;
		const u8 nv = d[0] < (num_vals - 2) ? d[0] : (num_vals - 2);

		for (i = 0; i < nv; i++) {
			v = d[i + 2];
			l += snprintf(buf + l, size - l, "%d,", v);
		}
	}
	break;
	}

	l -= (l > 0); /* overwrite final comma, if any */
	l += snprintf(buf + l, size - l, "\n");
out:
	kfree(d);
out_nofree:
	if (r < 0)
		return r;

	return l;
}

static ssize_t mxt_show_attr_flash(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;

	return snprintf(buf, size, "%d\n", td->flash_mode);
}

static ssize_t mxt_store_attr_flash(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	char name[FW_MAX_NAME_SIZE + 1];
	const struct firmware *fw;
	int r;

	if (count > FW_MAX_NAME_SIZE || count == 0) {
		dev_err(&td->client->dev, "firmware name check failure\n");
		return -EINVAL;
	}

	memcpy(name, buf, count);
	name[count] = 0;

	if (name[count - 1] == '\n')
		name[count - 1] = 0;

	r = request_firmware(&fw, name, &td->client->dev);
	if (r < 0) {
		dev_err(&td->client->dev, "firmware '%s' not found\n", name);
		goto store_out;
	}

	r = mxt_flash_firmware(td, fw);
	if (r < 0) {
		dev_err(&td->client->dev, "failed to write firmware: %d\n", r);
		goto firmware_out;
	}

	dev_info(&td->client->dev, "firmware written successfully\n");

firmware_out:
	release_firmware(fw);

store_out:
	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_store_attr_backup_config(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	int r;

	mutex_lock(&td->lock);
	r = mxt_backup_settings(td);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_store_attr_clear_config(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	int r;

	mutex_lock(&td->lock);
	r = mxt_clear_config(td);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_store_attr_write_config(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	char name[FW_MAX_NAME_SIZE + 1];
	const struct firmware *fw;
	int r;

	if (count > FW_MAX_NAME_SIZE || count == 0) {
		dev_err(&td->client->dev, "config name check failure\n");
		return -EINVAL;
	}

	memcpy(name, buf, count);
	name[count] = 0;

	if (name[count - 1] == '\n')
		name[count - 1] = 0;

	mutex_lock(&td->lock);

	r = request_firmware(&fw, name, &td->client->dev);
	if (r < 0) {
		dev_err(&td->client->dev, "config '%s' not found\n", name);
		goto store_out;
	}

	r = mxt_write_config(td, fw);
	if (r < 0) {
		dev_err(&td->client->dev, "failed to write cfg\n");
		goto firmware_out;
	}

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "config written successfully\n");

firmware_out:
	release_firmware(fw);

store_out:
	mutex_unlock(&td->lock);
	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_store_attr_reset(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long flag;
	int r;

	r = strict_strtoul(buf, 0, &flag);
	if (r < 0)
		return r;

	td->err_init = 0;

	mutex_lock(&td->lock);
	r = mxt_reset(td);
	if (flag >= 2)
		mxt_clear_object_states(td);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_store_attr_calibrate(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	int r;

	td->err_init = 0;

	mutex_lock(&td->lock);
	r = mxt_recalibrate(td);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_objects(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const size_t size = PAGE_SIZE;
	ssize_t l = 0;
	int i;

	mutex_lock(&td->lock);

	for (i = 0; i < td->obj_count; i++) {
		int r;

		r = snprintf(buf + l, size - l,
			     "%2d: 0x%04x %2d %2d %2d\n",
			     mxt_obj_type(td, i),
			     mxt_obj_addr(td, i),
			     mxt_obj_size(td, i),
			     mxt_obj_num_instances(td, i),
			     mxt_obj_num_rids_per_instance(td, i));

		if (r >= (size - l)) {
			l = size - 1;
			break;
		}

		l += r;
	}

	mutex_unlock(&td->lock);

	return l;
}

static ssize_t mxt_show_attr_dump_config(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t max_size = PAGE_SIZE;
	ssize_t l = 0;
	ssize_t c = 0;
	int i;
	int j;
	int r;
	u16 cfg_start = 0;
	u16 cfg_len = 0;
	u8 *zd = NULL;

	zd = kzalloc(MAX_I2C_WRITE_SIZE, GFP_KERNEL);
	if (zd == NULL)
		return -ENOMEM;

	mutex_lock(&td->lock);

	r = mxt_locate_config_area(td, &cfg_start, &cfg_len);
	if (r < 0)
		goto out;

	for (i = 0; i < td->obj_count; i++) {
		const u8 type = mxt_obj_type(td, i);
		const u16 addr = mxt_obj_addr(td, i);
		const u16 size = mxt_obj_size(td, i);

		if (addr >= cfg_start && addr < cfg_start + cfg_len) {

			if (size > MAX_I2C_WRITE_SIZE) {
				dev_err(&td->client->dev,
					"object too big: %d bytes\n",
					size);
				r = -EINVAL;
				goto out;
			}

			r = mxt_read_reg(td, addr, zd, size);
			if (r < 0)
				goto out;

			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "read %d bytes with code %d\n",
					 size, r);

			if ((l + 2 + size) > max_size) {
				dev_err(&td->client->dev, "no room\n");
				goto out;
			}

			buf[l++] = type;
			buf[l++] = size - 1;

			for (j = 0; j < size; j++)
				buf[l++] = zd[j];
		} else {
			if (td->debug_flags & DFLAG_VERBOSE)
				dev_info(&td->client->dev,
					 "%d is outside config area\n", type);
		}
	}

	if (td->debug_flags & DFLAG_VERBOSE)
		dev_info(&td->client->dev, "config out is %d bytes\n", l);

	c = l;
out:
	if (zd != NULL)
		kfree(zd),

	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return c;
}

/* Debug interface to inspect the address space of controller */
static ssize_t mxt_store_attr_objects(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	int reg;
	int range;
	int r;
	u8 *rb;
	int i;

	if (sscanf(buf, "%i %i", &reg, &range) != 2)
		return -EINVAL;

	if (range < 0 || reg < 0 || reg > 0xffff || range > 255)
		return -EINVAL;

	rb = kzalloc(range, GFP_KERNEL);
	if (rb == NULL)
		return -ENOMEM;

	mutex_lock(&td->lock);

	r = mxt_read_reg(td, reg, rb, range);
	if (r < 0)
		goto out;

	for (i = 0; i < range; i++) {
		dev_info(&td->client->dev,
			 "0x%04x[%d]: 0x%02x (%d)\n", reg + i, i, rb[i], rb[i]);
	}

out:
	mutex_unlock(&td->lock);
	kfree(rb);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_debug(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l;

	mutex_lock(&td->lock);
	l = snprintf(buf, size, "0x%02lx\n", td->debug_flags);
	mutex_unlock(&td->lock);

	return l;
}

static ssize_t mxt_store_attr_debug(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long flag;
	int r;

	r = strict_strtoul(buf, 0, &flag);
	if (r < 0)
		return r;

	mutex_lock(&td->lock);
	td->debug_flags = flag;
	mutex_unlock(&td->lock);

	return count;
}

static ssize_t mxt_show_attr_selftest(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	struct mxt_selftest st;
	ssize_t l;
	int r;

	memset(&st, 0, sizeof(st));

	st.test_num = SELFTEST_TEST_ALL;
	st.result = 0;

	r = mxt_selftest(td, &st);
	if (r != 0) {
		l = snprintf(buf, size, "FAIL (io error: %d)\n", r);
	} else {
		if (st.result == SELFTEST_RES_PASS)
			l = snprintf(buf, size, "PASS\n");
		else
			l = snprintf(buf, size, "FAIL (result: 0x%02x info: "
				     "0x%x 0x%x 0x%x 0x%x 0x%x)\n",
				     st.result,
				     st.info[0],
				     st.info[1],
				     st.info[2],
				     st.info[3],
				     st.info[4]
				);
	}

	return l;
}

static ssize_t mxt_show_attr_config_crc(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	u32 cfg_crc = 0;
	int r;

	mutex_lock(&td->lock);
	r = mxt_get_config_crc(td, &cfg_crc);
	mutex_unlock(&td->lock);
	if (r < 0)
		return r;

	return snprintf(buf, size, "0x%x\n", cfg_crc);
}

static ssize_t mxt_show_attr_nv_config_crc(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);

	return snprintf(buf, PAGE_SIZE, "0x%x\n", td->nonvol_cfg_crc);
}

static ssize_t mxt_show_attr_firmware_version(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;

	mutex_lock(&td->lock);
	if (td->obj_count)
		r = mxt_firmware_version(td);
	else
		r = -ENODEV;
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return snprintf(buf, size, "%d.%d\n", (r & 0xF0) >> 4, (r & 0x0F));
}

static ssize_t mxt_show_attr_firmware_build(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;

	mutex_lock(&td->lock);
	if (td->obj_count)
		r = mxt_build(td);
	else
		r = -ENODEV;
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return snprintf(buf, size, "0x%x\n", r);
}

static ssize_t mxt_show_attr_variant(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;

	mutex_lock(&td->lock);
	if (td->obj_count)
		r = mxt_variant(td);
	else
		r = -ENODEV;
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return snprintf(buf, size, "0x%x\n", r);
}

static ssize_t mxt_show_attr_active_interval(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;
	u8 d;

	mutex_lock(&td->lock);
	if (td->wakeup_gesture) {
		r = mxt_object_read_c(td, OT_POWERCONFIG,
				      PCONFIG_ACTACQINT, &d, 1);
		if (r < 0)
			goto out;

		r = d;
	} else {
		r = mxt_object_read_u8(td, OT_POWERCONFIG, PCONFIG_ACTACQINT);
	}

out:
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return snprintf(buf, size, "%d\n", r);
}

static ssize_t mxt_store_attr_active_interval(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long irate;
	int r;

	if (td->wakeup_gesture)
		return -EBUSY;

	r = strict_strtoul(buf, 0, &irate);
	if (r < 0)
		return r;

	if (irate > 0xff)
		return -EINVAL;

	mutex_lock(&td->lock);
	r = mxt_object_write_u8(td, OT_POWERCONFIG, PCONFIG_ACTACQINT, irate);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_idle_interval(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;
	u8 d;

	mutex_lock(&td->lock);
	if (td->wakeup_gesture) {
		r = mxt_object_read_c(td, OT_POWERCONFIG,
				      PCONFIG_IDLEACQINT, &d, 1);
		if (r < 0)
			goto out;

		r = d;
	} else {
		r = mxt_object_read_u8(td, OT_POWERCONFIG, PCONFIG_IDLEACQINT);
	}

out:
	mutex_unlock(&td->lock);
	if (r < 0)
		return r;

	return snprintf(buf, size, "%d\n", r);
}

static ssize_t mxt_store_attr_idle_interval(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long irate;
	int r;

	r = strict_strtoul(buf, 0, &irate);
	if (r < 0)
		return r;

	if (irate > 0xff)
		return -EINVAL;

	mutex_lock(&td->lock);
	r = mxt_object_write_u8(td, OT_POWERCONFIG, PCONFIG_IDLEACQINT, irate);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_rlimit_min_interval(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;

	return snprintf(buf, size, "%lu\n", td->rlimit_min_interval);
}

static ssize_t mxt_store_attr_rlimit_min_interval(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long irate;
	int r;

	if (td->wakeup_gesture)
		return -EBUSY;

	r = strict_strtoul(buf, 0, &irate);
	if (r < 0)
		return r;

	mutex_lock(&td->lock);
	td->rlimit_min_interval = irate;
	mutex_unlock(&td->lock);

	return count;
}

static ssize_t mxt_show_attr_rlimit_bypass(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;

	return snprintf(buf, size, "%lu\n", td->rlimit_bypass_time);
}

static ssize_t mxt_store_attr_rlimit_bypass(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long irate;
	int r;

	r = strict_strtoul(buf, 0, &irate);
	if (r < 0)
		return r;

	mutex_lock(&td->lock);
	td->rlimit_bypass_time = irate;
	mutex_unlock(&td->lock);

	return count;
}

static ssize_t mxt_show_attr_wait_interval(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;

	return snprintf(buf, size, "%d\n", td->wakeup_interval);
}

static ssize_t mxt_store_attr_wait_interval(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long irate;
	int r;

	if (td->wakeup_gesture)
		return -EBUSY;

	r = strict_strtoul(buf, 0, &irate);
	if (r < 0)
		return r;

	if (irate > 0xff)
		return -EINVAL;

	mutex_lock(&td->lock);
	td->wakeup_interval = irate;
	mutex_unlock(&td->lock);

	return count;
}

static ssize_t mxt_show_attr_movhysti(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;

	mutex_lock(&td->lock);
	r = mxt_object_read_u8(td, OT_TOUCH, TOUCH_MOVHYSTI);
	mutex_unlock(&td->lock);
	if (r < 0)
		return r;

	return snprintf(buf, size, "%d\n", r);
}

static ssize_t mxt_store_attr_movhysti(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long ival;
	int r;

	r = strict_strtoul(buf, 10, &ival);
	if (r < 0)
		return r;

	if (ival > 0xff)
		return -EINVAL;

	mutex_lock(&td->lock);
	r = mxt_object_write_u8(td, OT_TOUCH, TOUCH_MOVHYSTI, ival);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_movhystn(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	int r;

	mutex_lock(&td->lock);
	r = mxt_object_read_u8(td, OT_TOUCH, TOUCH_MOVHYSTN);
	mutex_unlock(&td->lock);
	if (r < 0)
		return r;

	return snprintf(buf, size, "%d\n", r);
}

static ssize_t mxt_store_attr_movhystn(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long ival;
	int r;

	r = strict_strtoul(buf, 10, &ival);
	if (r < 0)
		return r;

	if (ival > 0xff)
		return -EINVAL;

	mutex_lock(&td->lock);
	r = mxt_object_write_u8(td, OT_TOUCH, TOUCH_MOVHYSTN, ival);
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_disable_ts(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);

	return sprintf(buf, "%u\n", td->power_enabled ? 0 : 1);
}

static ssize_t mxt_store_attr_disable_ts(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long res;
	int i;

	if (strict_strtoul(buf, 10, &res) < 0)
		return -EINVAL;

	i = res ? 1 : 0;

	if (i)
		mxt_disable(td);
	else
		mxt_enable(td, true);

	return count;
}

static ssize_t mxt_show_attr_wait_for_gesture(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);

	return sprintf(buf, "0x%02x\n", td->wakeup_gesture);
}

static ssize_t mxt_store_attr_wait_for_gesture(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long res;
	int r;

	if (strict_strtoul(buf, 0, &res) < 0)
		return -EINVAL;

	mutex_lock(&td->lock);

	if (!td->objinit_done) {
		td->wakeup_gesture = res;
		r = 0;
		goto out;
	}

	if (res) {
		if (td->wakeup_gesture) {
			r = -EBUSY;
			goto out;
		}
		r = mxt_wait_for_gesture(td, res);
	} else {
		r = mxt_got_wakeup_gesture(td);
	}

out:
	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_show_attr_diag(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const size_t size = PAGE_SIZE;
	int r;
	u8 mode;

	if (!strcmp(attr->attr.name, "deltas"))
		mode = DIAG_DELTAS;
	else if (!strcmp(attr->attr.name, "gains"))
		mode = DIAG_GAINS;
	else if (!strcmp(attr->attr.name, "refs"))
		mode = DIAG_REFS;
	else if (!strcmp(attr->attr.name, "xcommons"))
		mode = DIAG_XCOMMONS;
	else
		return -EINVAL;

	mutex_lock(&td->lock);
	r = mxt_diag_get_values(td, mode, buf, size);
	mutex_unlock(&td->lock);

	return r;
}

static ssize_t mxt_show_attr_lines(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);

	if (!strcmp(attr->attr.name, "xlines"))
		return sprintf(buf, "%u\n", mxt_lines_x(td));

	if (!strcmp(attr->attr.name, "ylines"))
		return sprintf(buf, "%u\n", mxt_lines_y(td));

	return -EINVAL;
}

static ssize_t mxt_show_attr_stats(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	const size_t size = PAGE_SIZE;
	size_t l = 0;
	int r;

#define SHOW_STAT(x) do {						\
		r = snprintf(buf + l, size - l, "%s:%lu\n", #x, td->x); \
		if (r >= (size - l))					\
			return size - 1;				\
		else							\
			l += r;						\
	} while (0)

	SHOW_STAT(num_isr);
	SHOW_STAT(num_work);
	SHOW_STAT(num_syn);
	SHOW_STAT(num_msg);
	SHOW_STAT(num_msg_err);
	SHOW_STAT(num_msg_255);
	SHOW_STAT(num_msg_zero);
	SHOW_STAT(num_reset_handled);
	SHOW_STAT(num_reset_sw);
	SHOW_STAT(num_reset_hw);
	SHOW_STAT(num_cals);
	SHOW_STAT(num_backup_nv);

	SHOW_STAT(err_init);
	SHOW_STAT(err_overflow);
	SHOW_STAT(err_sigerr);
	SHOW_STAT(err_i2c_crc);
	SHOW_STAT(err_msg_crc);
	SHOW_STAT(err_cfg);

#undef SHOW_STAT

	return l;
}

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS
static int mxt_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static int mxt_bits_to_int(int bits, int len, bool sign)
{
	int const m = 1U << (len - 1);
	int r;

	r = bits & ((1U << len) - 1);

	if (sign)
		r = (r ^ m) - m;

	return r;
}

static unsigned long mxt_int_to_bits(int v, int len)
{
	return v & ((1U << len) - 1);
}

static int mxt_object_read_bits(struct mxt_debugfs *dfs, const u8 otype,
				const u16 bit_pos, const u8 num_bits)
{
	const u16 addr = bit_pos >> 3;
	const u8 bit_offset = bit_pos & 0x07;
	int len;
	u8 vals[4];
	u32 val;
	int r;
	int i;

	if (num_bits > 16)
		return -EINVAL;

	len = (((num_bits - 1) + bit_offset) >> 3) + 1;

	if (dfs->mode == DEBUGFS_MODE_CACHED)
		r = mxt_object_read(dfs->mxt_dev, otype, addr, vals, len);
	else
		r = mxt_object_read_nc(dfs->mxt_dev, otype, addr, vals, len);

	if (r < 0)
		return r;

	val = 0;

	for (i = 0; i < num_bits; i++) {
		const u8 pos = i + bit_offset;

		if (vals[pos >> 3] & (1 << (pos % 8)))
			val |= (1 << i);
	}

	return val;
}

static int mxt_object_write_bits(struct mxt_debugfs *dfs, const u8 otype,
				 const u16 bit_pos, const u8 num_bits,
				 const u32 bits)
{
	const u16 addr = bit_pos >> 3;
	const u8 bit_offset = bit_pos & 0x07;
	int len;
	u8 vals[4];
	int r;
	int i;

	if (num_bits > 16)
		return -EINVAL;

	len = (((num_bits - 1) + bit_offset) >> 3) + 1;

	if (dfs->mode == DEBUGFS_MODE_CACHED)
		r = mxt_object_read(dfs->mxt_dev, otype, addr, vals, len);
	else
		r = mxt_object_read_nc(dfs->mxt_dev, otype, addr, vals, len);

	if (r < 0)
		return r;

	for (i = 0; i < num_bits; i++) {
		const u8 pos = i + bit_offset;

		if (bits & (1 << i))
			vals[pos >> 3] |= (1 << (pos % 8));
		else
			vals[pos >> 3] &= ~(1 << (pos % 8));
	}

	return mxt_object_write(dfs->mxt_dev, otype, addr, vals, len);
}

static ssize_t mxt_debugfs_write(struct file *file,
				 const char __user *ubuf, size_t count,
				 loff_t *pos)
{
	struct mxt_debugfs_param *p = file->private_data;
	struct mxt_debugfs *dfs;
	const struct mxt_param_desc *pd;
	char buf[16];
	long result;
	unsigned long bits;
	int r;

	pd = &p->parent->obj->params[p->index];
	dfs = p->parent->parent;

	if (count <= 0 || count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, count))
		return -EFAULT;

	buf[count] = 0;

	r = strict_strtol(buf, 0, &result);
	if (r < 0)
		return r;

	if (result >= (1 << (pd->len - pd->sign)))
		return -EINVAL;

	if (result < -(pd->sign << (pd->len - 1)))
		return -EINVAL;

	bits = mxt_int_to_bits(result, pd->len);

	r = mxt_object_write_bits(dfs, p->parent->obj->obj_type,
				  (pd->pos << 3) + pd->bit, pd->len, bits);
	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_debugfs_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct mxt_debugfs_param *p = file->private_data;
	const struct mxt_param_desc *pd;
	struct mxt_debugfs *dfs;
	char buf[32];
	int r;
	int v;

	pd = &p->parent->obj->params[p->index];
	dfs = p->parent->parent;

	r = mxt_object_read_bits(dfs, p->parent->obj->obj_type,
				 (pd->pos << 3) + pd->bit, pd->len);
	if (r < 0)
		return r;

	v = mxt_bits_to_int(r, pd->len, pd->sign);
	r = sprintf(buf, "%d\n", v);

	return simple_read_from_buffer(userbuf, count, ppos, buf, r);
}

static ssize_t mxt_debugfs_dumpobj_write(struct file *file,
					 const char __user *ubuf, size_t count,
					 loff_t *pos)
{
	struct mxt_debugfs_obj *o = file->private_data;
	struct mxt_debugfs *dfs;
	struct mxt *td;
	u8 values[MAX_OBJ_SIZE];
	int num_values;
	int r;
	char *cp, *p, *tok;

	dfs = o->parent;
	td = dfs->mxt_dev;

	if (count <= 0 || count > 1024)
		return -EINVAL;

	cp = kmalloc(count + 1, GFP_KERNEL);
	if (cp == NULL)
		return -ENOMEM;
	p = cp;

	if (copy_from_user(p, ubuf, count)) {
		kfree(cp);
		return -EFAULT;
	}

	p[count] = 0;

	if (p[count - 1] == '\n')
		p[count - 1] = 0;

	num_values = 0;
	while ((tok = strsep(&p, " ")) != NULL) {
		unsigned long v;

		if (strlen(tok) == 0)
			continue;

		if (num_values == MAX_OBJ_SIZE) {
			r = -ENOMEM;
			goto out;
		}

		r = strict_strtoul(tok, 0, &v);
		if (r < 0)
			goto out;

		values[num_values++] = (u8)v;
	}

	mutex_lock(&td->lock);
	if (num_values > 0)
		r = mxt_object_write(td, o->obj->obj_type,
				     0, values, num_values);
	else
		r = 0;
	mutex_unlock(&td->lock);
out:
	kfree(cp);

	return count;
}

static ssize_t mxt_debugfs_dumpobj_read(struct file *file, char __user *userbuf,
					size_t count, loff_t *ppos)
{
	struct mxt_debugfs_obj *o = file->private_data;
	struct mxt_debugfs *dfs;
	struct mxt *td;
	char *buf;
	u8 *raw;
	int r;
	int oi;
	int osize;
	int bufsize;
	int i;

	dfs = o->parent;
	td = dfs->mxt_dev;

	r = mxt_obj_index(td, o->obj->obj_type);
	if (r < 0)
		return -EINVAL;

	oi = r;

	r = mxt_obj_size(td, oi);
	if (r < 0)
		return r;

	osize = r;

	raw = kmalloc(osize, GFP_KERNEL);
	if (raw == NULL)
		return -ENOMEM;

	mutex_lock(&td->lock);
	if (dfs->mode == DEBUGFS_MODE_CACHED)
		r = mxt_object_read(td, o->obj->obj_type, 0, raw, osize);
	else
		r = mxt_object_read_nc(td, o->obj->obj_type, 0, raw, osize);
	mutex_unlock(&td->lock);
	if (r < 0)
		goto err1;

	/* Five ascii characters per byte */
	bufsize = (osize + 1) * 5;

	buf = kmalloc((osize + 1) * 5, GFP_KERNEL);
	if (buf == NULL) {
		r = -ENOMEM;
		goto err1;
	}

	r = 0;

	for (i = 0; i < osize; i++)
		r += sprintf(buf + r, "0x%02x%s",
			     raw[i], i == (osize - 1) ? "\n" : " ");

	r = simple_read_from_buffer(userbuf, count, ppos, buf, r);

	kfree(buf);
err1:
	kfree(raw);
	return r;
}

static const struct file_operations mxt_debugfs_fops = {
	.read   = mxt_debugfs_read,
	.write  = mxt_debugfs_write,
	.open   = mxt_debugfs_open,
};

static const struct file_operations mxt_debugfs_dumpobj_fops = {
	.read   = mxt_debugfs_dumpobj_read,
	.write  = mxt_debugfs_dumpobj_write,
	.open   = mxt_debugfs_open,
};

static int mxt_debugfs_create_obj(struct mxt_debugfs *dfs, const int index,
				  struct mxt_debugfs_obj *dobj)
{
	int i;
	int r;
	const struct mxt_object_desc *od;
	struct dentry *d;
	char rawname[MAX_OBJ_NAMELEN];

	if (index < 0 || index >= ARRAY_SIZE(mxt_obj_descs))
		return -EINVAL;

	od = mxt_obj_descs[index];

	r = snprintf(rawname, MAX_OBJ_NAMELEN, "%s.raw", od->name);
	if (r < 0 || r >= MAX_OBJ_NAMELEN)
		return -EINVAL;

	d = debugfs_create_dir(od->name,
			       dfs->dent);

	if (IS_ERR_OR_NULL(d)) {
		r = d ? PTR_ERR(d) : -ENOMEM;
		return r;
	}

	dobj->objdump_dent = debugfs_create_file(rawname,
						 0644,
						 dfs->dent,
						 dobj,
						 &mxt_debugfs_dumpobj_fops);

	if (IS_ERR_OR_NULL(dobj->objdump_dent)) {
		r = dobj->objdump_dent ? PTR_ERR(dobj->objdump_dent) : -ENOMEM;
		debugfs_remove(d);
		return r;
	}

	dobj->dent = d;
	dobj->obj = od;
	dobj->parent = dfs;

	dobj->params = kzalloc(sizeof(struct mxt_debugfs_param) *
			       od->num_params, GFP_KERNEL);

	if (dobj->params == NULL) {
		r = -ENOMEM;
		goto err1;
	}

	for (i = 0; i < od->num_params; i++) {
		const struct mxt_param_desc * const opd = &od->params[i];
		struct mxt_debugfs_param *dpd = &dobj->params[i];
		dpd->index = i;
		dpd->parent = dobj;
		dobj->params[i].dent = debugfs_create_file(opd->name,
							   0644,
							   dobj->dent,
							   dpd,
							   &mxt_debugfs_fops);

		if (IS_ERR(dobj->params[i].dent)) {
			r = PTR_ERR(dobj->params[i].dent);
			goto err2;
		}
	}

	return 0;

err2:
	for (i = 0; i < od->num_params; i++) {
		if (dobj->params[i].dent)
			debugfs_remove(dobj->params[i].dent);
	}
	kfree(dobj->params);
err1:
	debugfs_remove(dobj->dent);

	return r;
}

static void mxt_debugfs_remove_obj(const int index,
				   struct mxt_debugfs_obj *dobj)
{
	const struct mxt_object_desc * const od = mxt_obj_descs[index];
	int i;

	for (i = 0; i < od->num_params; i++) {
		if (dobj->params[i].dent)
			debugfs_remove(dobj->params[i].dent);
	}

	if (dobj->objdump_dent)
		debugfs_remove(dobj->objdump_dent);

	if (dobj->dent)
		debugfs_remove(dobj->dent);

	kfree(dobj->params);
}

static void mxt_debugfs_remove_objects(struct mxt_debugfs *dfs)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mxt_obj_descs); i++)
		mxt_debugfs_remove_obj(i, &dfs->objs[i]);
}

static void mxt_debugfs_remove(struct mxt_debugfs *dfs)
{
	if (dfs) {
		if (dfs->dent) {
			mxt_debugfs_remove_objects(dfs);
			debugfs_remove(dfs->dent);
		}

		dfs->mxt_dev = NULL;
		kfree(dfs);
	}
}

static struct mxt_debugfs *mxt_debugfs_register(struct mxt *td,
						const char *name,
						u8 initial_mode)
{
	int r;
	int i;
	struct dentry *d;
	struct mxt_debugfs *dfs;

	dfs = kzalloc(sizeof(*dfs), GFP_KERNEL);
	if (dfs == NULL)
		return ERR_PTR(-ENOMEM);

	dfs->mxt_dev = td;

	if (initial_mode != DEBUGFS_MODE_FW)
		initial_mode = DEBUGFS_MODE_CACHED;

	dfs->mode = initial_mode;

	d = debugfs_create_dir(name, NULL);
	if (IS_ERR_OR_NULL(d)) {
		r = d ? PTR_ERR(d) : -ENOMEM;
		goto out;
	}

	dfs->dent = d;

	for (i = 0; i < ARRAY_SIZE(mxt_obj_descs); i++) {
		r = mxt_debugfs_create_obj(dfs, i, &dfs->objs[i]);
		if (r < 0)
			goto out;
	}

	r = 0;
out:
	if (r < 0) {
		mxt_debugfs_remove(dfs);
		return ERR_PTR(r);
	}

	return dfs;
}

static ssize_t mxt_show_attr_debug_fs(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);

	if (td->dfs)
		return snprintf(buf, PAGE_SIZE, "%d (%s)\n", td->dfs->mode,
				td->dfs->mode == DEBUGFS_MODE_FW ?
				"FW" : "Cached");
	else
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t mxt_store_attr_debug_fs(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	unsigned long ival;
	int r = 0;

	if (strict_strtoul(buf, 10, &ival) < 0)
		return -EINVAL;

	if (ival != DEBUGFS_MODE_CACHED &&
	    ival != DEBUGFS_MODE_FW &&
	    ival != 0)
		return -EINVAL;

	mutex_lock(&td->lock);

	if (ival) {
		if (td->dfs == NULL) {
			char name[128];
			sprintf(name, "%s-%d-0x%x", KBUILD_MODNAME,
				td->client->adapter->nr, td->client->addr);
			td->dfs = mxt_debugfs_register(td, name, ival);
			if (IS_ERR(td->dfs)) {
				dev_err(&td->client->dev,
					"error %ld registering debugfs\n",
					PTR_ERR(td->dfs));
				r = PTR_ERR(td->dfs);
				td->dfs = NULL;
			}
		}
	} else {
		if (td->dfs != NULL) {
			mxt_debugfs_remove(td->dfs);
			td->dfs = NULL;
		}
	}

	mutex_unlock(&td->lock);

	if (r < 0)
		return r;

	return count;
}

static ssize_t mxt_store_attr_play(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mxt *td = platform_get_drvdata(pdev);
	struct input_dev *idev = td->idev;
	char delim;
	char *p;
	int i = 0;

	if (count <= 0 || count > 1024)
		return -EINVAL;

	p = kstrndup(buf, count, GFP_KERNEL);
	if (p == NULL)
		return -ENOMEM;

	mutex_lock(&td->lock);

	while (i < count) {
		int x, y, wx, id, c;

		if (sscanf(p + i, "%d,%d,%d%n", &x, &y, &id, &c) == 3) {
			;
		} else if (sscanf(p + i, "%d,%d%n", &x, &y, &c) == 2) {
			id = 0;
		} else if (sscanf(p + i, "%c%n", &delim, &c) == 1) {
			if (delim == ':') {
				input_sync(idev);
				if (td->debug_flags & DFLAG_VERBOSE)
					dev_info(&td->client->dev,
						 "play: syn\n");
				msleep(7);
				i += c;
				continue;
			} else {
				goto out;
			}
		} else {
			goto out;
		}

		if (id == 0) {
			input_report_key(idev, BTN_TOUCH, 1);
			input_report_abs(idev, ABS_X, x);
			input_report_abs(idev, ABS_Y, y);
		}

		input_report_abs(idev, ABS_MT_POSITION_X, x);
		input_report_abs(idev, ABS_MT_POSITION_Y, y);

		wx = mxt_area_to_width(td, 7);
		wx = wx >> 1;

		input_report_abs(idev, ABS_MT_TOUCH_MAJOR, wx);
		input_report_abs(idev, ABS_MT_TRACKING_ID, id);

		input_mt_sync(idev);

		if (td->debug_flags & DFLAG_VERBOSE)
			dev_info(&td->client->dev, "play: %dx%d (%d)\n",
				 x, y, id);
		i += c;
	}
out:
	/* Generate extra syn if user didn't provide it */
	if (i < count && p[i-1] != ':') {
		input_sync(idev);

		if (td->debug_flags & DFLAG_VERBOSE)
			dev_info(&td->client->dev, "play: extra syn\n");
	}

	input_report_key(idev, BTN_TOUCH, 0);
	input_mt_sync(idev);
	input_sync(idev);

	mutex_unlock(&td->lock);

	kfree(p);

	return count;
}

static DEVICE_ATTR(debug_fs, S_IRUSR | S_IWUSR,
		   mxt_show_attr_debug_fs, mxt_store_attr_debug_fs);
#endif /* CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS */

static DEVICE_ATTR(flash, S_IRUSR | S_IWUSR,
		   mxt_show_attr_flash, mxt_store_attr_flash);

static DEVICE_ATTR(write_config, S_IWUSR,
		   NULL, mxt_store_attr_write_config);

static DEVICE_ATTR(backup_config, S_IWUSR,
		   NULL, mxt_store_attr_backup_config);

static DEVICE_ATTR(clear_config, S_IWUSR,
		   NULL, mxt_store_attr_clear_config);

static DEVICE_ATTR(reset, S_IWUSR,
		   NULL, mxt_store_attr_reset);

static DEVICE_ATTR(calibrate, S_IWUSR,
		   NULL, mxt_store_attr_calibrate);

static DEVICE_ATTR(objects, S_IRUSR | S_IWUSR,
		   mxt_show_attr_objects, mxt_store_attr_objects);

static DEVICE_ATTR(dump_config, S_IRUSR,
		   mxt_show_attr_dump_config, NULL);

static DEVICE_ATTR(debug, S_IRUSR | S_IWUSR,
		   mxt_show_attr_debug, mxt_store_attr_debug);

static DEVICE_ATTR(selftest, S_IRUGO,
		   mxt_show_attr_selftest, NULL);

static DEVICE_ATTR(config_crc, S_IRUSR,
		   mxt_show_attr_config_crc, NULL);

static DEVICE_ATTR(nv_config_crc, S_IRUSR,
		   mxt_show_attr_nv_config_crc, NULL);

static DEVICE_ATTR(firmware_version, S_IRUGO,
		   mxt_show_attr_firmware_version, NULL);

static DEVICE_ATTR(firmware_build, S_IRUGO,
		   mxt_show_attr_firmware_build, NULL);

static DEVICE_ATTR(variant, S_IRUGO,
		   mxt_show_attr_variant, NULL);

static DEVICE_ATTR(active_interval_ms, S_IRUSR | S_IWUSR,
		   mxt_show_attr_active_interval,
		   mxt_store_attr_active_interval);

static DEVICE_ATTR(idle_interval_ms, S_IRUSR | S_IWUSR,
		   mxt_show_attr_idle_interval,
		   mxt_store_attr_idle_interval);

static DEVICE_ATTR(wait_interval_ms, S_IRUSR | S_IWUSR,
		   mxt_show_attr_wait_interval,
		   mxt_store_attr_wait_interval);

static DEVICE_ATTR(rlimit_min_interval_us, S_IRUSR | S_IWUSR,
		   mxt_show_attr_rlimit_min_interval,
		   mxt_store_attr_rlimit_min_interval);

static DEVICE_ATTR(rlimit_bypass_us, S_IRUSR | S_IWUSR,
		   mxt_show_attr_rlimit_bypass,
		   mxt_store_attr_rlimit_bypass);

static DEVICE_ATTR(movhysti, S_IRUSR | S_IWUSR,
		   mxt_show_attr_movhysti,
		   mxt_store_attr_movhysti);

static DEVICE_ATTR(movhystn, S_IRUSR | S_IWUSR,
		   mxt_show_attr_movhystn,
		   mxt_store_attr_movhystn);

static DEVICE_ATTR(disable_ts, S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP,
		   mxt_show_attr_disable_ts,
		   mxt_store_attr_disable_ts);

static DEVICE_ATTR(wait_for_gesture, S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP,
		   mxt_show_attr_wait_for_gesture,
		   mxt_store_attr_wait_for_gesture);

static DEVICE_ATTR(stats, S_IRUGO,
		   mxt_show_attr_stats,
		   NULL);

static DEVICE_ATTR(deltas, S_IRUSR, mxt_show_attr_diag, NULL);
static DEVICE_ATTR(refs, S_IRUSR, mxt_show_attr_diag, NULL);
static DEVICE_ATTR(gains, S_IRUSR, mxt_show_attr_diag, NULL);
static DEVICE_ATTR(xcommons, S_IRUSR, mxt_show_attr_diag, NULL);

static DEVICE_ATTR(xlines, S_IRUGO, mxt_show_attr_lines, NULL);
static DEVICE_ATTR(ylines, S_IRUGO, mxt_show_attr_lines, NULL);

static DEVICE_ATTR(play, S_IWUSR,
		   NULL,
		   mxt_store_attr_play);

static struct attribute *mxt_attrs[] = {
	&dev_attr_flash.attr,
	&dev_attr_write_config.attr,
	&dev_attr_backup_config.attr,
	&dev_attr_clear_config.attr,
	&dev_attr_reset.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_objects.attr,
	&dev_attr_dump_config.attr,
	&dev_attr_debug.attr,
	&dev_attr_selftest.attr,
	&dev_attr_config_crc.attr,
	&dev_attr_nv_config_crc.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_firmware_build.attr,
	&dev_attr_variant.attr,
	&dev_attr_active_interval_ms.attr,
	&dev_attr_idle_interval_ms.attr,
	&dev_attr_wait_interval_ms.attr,
	&dev_attr_rlimit_min_interval_us.attr,
	&dev_attr_rlimit_bypass_us.attr,
	&dev_attr_movhysti.attr,
	&dev_attr_movhystn.attr,
	&dev_attr_disable_ts.attr,
	&dev_attr_wait_for_gesture.attr,
	&dev_attr_stats.attr,
	&dev_attr_deltas.attr,
	&dev_attr_refs.attr,
	&dev_attr_gains.attr,
	&dev_attr_xcommons.attr,
	&dev_attr_xlines.attr,
	&dev_attr_ylines.attr,
	&dev_attr_play.attr,
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS
	&dev_attr_debug_fs.attr,
#endif
	NULL,
};

static const struct attribute_group mxt_attr_group = {
	.attrs = mxt_attrs,
};

static int mxt_probe(struct i2c_client *client,
		     const struct i2c_device_id *id)
{
	struct mxt *td;
	struct mxt_platform_data *pd;
	int r;

	td = kzalloc(sizeof(struct mxt), GFP_KERNEL);
	if (td == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, td);
	td->client = client;

	mutex_init(&td->lock);

	pd = td->client->dev.platform_data;
	if (!pd) {
		dev_err(&client->dev, "no platform data found\n");
		r = -ENODEV;
		goto err_free_dev;
	}

	td->regs[0].supply = reg_avdd;
	td->regs[1].supply = reg_dvdd;

	td->rlimit_bypass_time = pd->rlimit_bypass_time_us;
	td->rlimit_min_interval = pd->rlimit_min_interval_us;

	/* If there is not wakeup rate defined, use some sane default */
	if (pd->wakeup_interval_ms > 0)
		td->wakeup_interval = pd->wakeup_interval_ms;
	else
		td->wakeup_interval = 20;

	r = regulator_bulk_get(&client->dev,
			       ARRAY_SIZE(td->regs), td->regs);
	if (r < 0) {
		dev_err(&client->dev, "cannot get regulators\n");
		goto err_free_dev;
	}

	r = sysfs_create_group(&td->client->dev.kobj, &mxt_attr_group);
	if (r < 0)
		goto err_free_regs;

	r = request_threaded_irq(client->irq, mxt_isr, mxt_irq_thread,
				 IRQF_TRIGGER_FALLING,
				 DRIVER_NAME, td);
	if (r) {
		dev_dbg(&client->dev,  "can't get IRQ %d, err %d\n",
			client->irq, r);
		goto err_free_sysfs;
	}

	/*
	  Disable irq for mxt_enable pairing.
	  mxt_enable doesn't need to wait for first interrupt in here.
	*/

	disable_irq(client->irq);
	mxt_enable(td, false);

	return 0;

err_free_sysfs:
	sysfs_remove_group(&td->client->dev.kobj, &mxt_attr_group);

err_free_regs:
	regulator_bulk_free(ARRAY_SIZE(td->regs), td->regs);

err_free_dev:
	kfree(td);
	return r;
}

static void mxt_shutdown(struct i2c_client *client)
{
	struct mxt *td = i2c_get_clientdata(client);

	mxt_disable(td);
}

static int __exit mxt_remove(struct i2c_client *client)
{
	struct mxt *td = i2c_get_clientdata(client);

	mxt_disable(td);

	free_irq(client->irq, td);

	mutex_lock(&td->lock);
	mxt_unregister_input_device(td);

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT_DEBUGFS
	mxt_debugfs_remove(td->dfs);
#endif
	sysfs_remove_group(&td->client->dev.kobj, &mxt_attr_group);
	regulator_bulk_free(ARRAY_SIZE(td->regs), td->regs);
	mxt_clear_object_states(td);
	mutex_unlock(&td->lock);

	kfree(td);
	i2c_set_clientdata(client, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int mxt_suspend(struct i2c_client *client, pm_message_t msg)
{
	struct mxt *td = i2c_get_clientdata(client);

	mxt_disable(td);

	return 0;
}

static int mxt_resume(struct i2c_client *client)
{
	struct mxt *td = i2c_get_clientdata(client);

	mxt_enable(td, true);

	return 0;
}
#endif

static const struct i2c_device_id mxt_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

static struct i2c_driver mxt_i2c_driver = {
	.driver = {
		.name           = DRIVER_NAME,
	},
	.probe          = mxt_probe,
	.remove         = __exit_p(mxt_remove),
	.id_table       = mxt_id,
	.shutdown       = mxt_shutdown,

#ifdef CONFIG_PM
	.suspend        = mxt_suspend,
	.resume         = mxt_resume,
#endif

};

static int __init mxt_module_init(void)
{
	return i2c_add_driver(&mxt_i2c_driver);
}

static void __exit mxt_module_exit(void)
{
	i2c_del_driver(&mxt_i2c_driver);
}

module_init(mxt_module_init);
module_exit(mxt_module_exit);

MODULE_AUTHOR("Mika Kuoppala <mika.kuoppala@nokia.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
