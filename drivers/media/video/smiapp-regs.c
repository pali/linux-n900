/*
 * drivers/media/video/smiapp-regs.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/sort.h>
#include <linux/v4l2-subdev.h>
#include <media/smiapp-regs.h>

/*
 *
 * Reglist helpers
 *
 */

static int smia_reglist_cmp(const void *a, const void *b)
{
	const struct smia_reglist **list1 = (const struct smia_reglist **)a,
		**list2 = (const struct smia_reglist **)b;

	/* Put real modes in the beginning. */
	if ((*list1)->type == SMIA_REGLIST_MODE &&
	    (*list2)->type != SMIA_REGLIST_MODE)
		return -1;
	else if ((*list1)->type != SMIA_REGLIST_MODE &&
		 (*list2)->type == SMIA_REGLIST_MODE)
		return 1;

	/* Descending width. */
	if ((*list1)->mode.window_width > (*list2)->mode.window_width)
		return -1;
	else if ((*list1)->mode.window_width < (*list2)->mode.window_width)
		return 1;

	if ((*list1)->mode.window_height > (*list2)->mode.window_height)
		return -1;
	else if ((*list1)->mode.window_height < (*list2)->mode.window_height)
		return 1;
	else
		return 0;
}

/*
 * Prepare register list created by dcc-pulautin for use in kernel.
 * The pointers in the list are actually offsets from the beginning of
 * the blob.
 */
int smia_reglist_import(struct smia_meta_reglist *meta)
{
	uintptr_t nlists = 0;

	if (meta->magic != SMIA_MAGIC) {
		printk(KERN_ERR "invalid camera sensor firmware (0x%08X)\n",
		       meta->magic);
		return -EILSEQ;
	}

	printk(KERN_ALERT "%s: meta_reglist version %s\n",
	       __func__, meta->version);

	while (meta->reglist[nlists].offset != 0) {
		struct smia_reglist *list;

		meta->reglist[nlists].offset =
			(uintptr_t)meta + meta->reglist[nlists].offset;

		list = meta->reglist[nlists].ptr;

		nlists++;
	}

	if (!nlists)
		return -EINVAL;

	sort(&meta->reglist[0].offset, nlists, sizeof(meta->reglist[0].offset),
	     smia_reglist_cmp, NULL);

	nlists = 0;
	while (meta->reglist[nlists].offset != 0) {
		struct smia_reglist *list;

		list = meta->reglist[nlists].ptr;

		printk(KERN_DEBUG
		       "%s: type %d\tw %d\th %d\tfmt %x\tival %d/%d\tpclk %d\n",
		       __func__,
		       list->type,
		       list->mode.window_width, list->mode.window_height,
		       list->mode.pixel_format,
		       list->mode.timeperframe.numerator,
		       list->mode.timeperframe.denominator,
		       list->mode.pixel_clock);

		nlists++;
	}

	return 0;
}

struct smia_reglist *smia_reglist_find_type(struct smia_meta_reglist *meta,
					    u16 type)
{
	struct smia_reglist **next = &meta->reglist[0].ptr;

	while (*next) {
		if ((*next)->type == type)
			return *next;

		next++;
	}

	return NULL;
}

struct smia_reglist **smia_reglist_first(struct smia_meta_reglist *meta)
{
	return &meta->reglist[0].ptr;
}

struct smia_reglist *smia_reglist_find_mode_fmt(struct smia_meta_reglist *meta,
						struct v4l2_mbus_framefmt *fmt)
{
	struct smia_reglist **list = smia_reglist_first(meta);
	struct smia_reglist *best_match = NULL;
	struct smia_reglist *best_other = NULL;
	struct v4l2_mbus_framefmt format;
	unsigned int max_dist_match = (unsigned int)-1;
	unsigned int max_dist_other = (unsigned int)-1;

	/* Find the mode with the closest image size. The distance between
	 * image sizes is the size in pixels of the non-overlapping regions
	 * between the requested size and the frame-specified size.
	 *
	 * Store both the closest mode that matches the requested format, and
	 * the closest mode for all other formats. The best match is returned
	 * if found, otherwise the best mode with a non-matching format is
	 * returned.
	 */
	for (; *list; list++) {
		unsigned int dist;

		if ((*list)->type != SMIA_REGLIST_MODE)
			continue;

		smia_reglist_to_mbus(*list, &format);

		dist = min(fmt->width, format.width)
		     * min(fmt->height, format.height);
		dist = format.width * format.height
		     + fmt->width * fmt->height - 2 * dist;


		if (fmt->code == format.code) {
			if (dist < max_dist_match || best_match == NULL) {
				best_match = *list;
				max_dist_match = dist;
			}
		} else {
			if (dist < max_dist_other || best_other == NULL) {
				best_other = *list;
				max_dist_other = dist;
			}
		}
	}

	return best_match ? best_match : best_other;
}

#define TIMEPERFRAME_TO_US(t) \
	div_u64((t).numerator * 1000000ULL, (t).denominator)
struct smia_reglist *smia_reglist_find_mode_ival(
	struct smia_meta_reglist *meta,
	struct smia_reglist *current_reglist,
	struct v4l2_fract *timeperframe)
{
	u32 tpf = TIMEPERFRAME_TO_US(*timeperframe);
	struct smia_reglist **list = smia_reglist_first(meta);
	struct smia_mode *current_mode = &current_reglist->mode;
	struct smia_reglist *best_mode = NULL;
	u32 best_diff = ~0;

	for (; *list; list++) {
		struct smia_mode *mode = &(*list)->mode;
		u32 tpf_diff;

		if ((*list)->type != SMIA_REGLIST_MODE)
			continue;

		if (mode->window_width != current_mode->window_width
		    || mode->window_height != current_mode->window_height
		    || mode->pixel_format != current_mode->pixel_format)
			continue;

		tpf_diff = abs(TIMEPERFRAME_TO_US(mode->timeperframe) - tpf);

		if (tpf_diff < best_diff || best_mode == NULL) {
			best_mode = *list;
			best_diff = tpf_diff;
		}
	}

	if (best_mode != NULL)
		*timeperframe = best_mode->mode.timeperframe;

	return best_mode;
}

#define MAX_FMTS 4
int smia_reglist_enum_mbus_code(struct smia_meta_reglist *meta,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct smia_reglist **list = smia_reglist_first(meta);
	u32 pixelformat[MAX_FMTS];
	int npixelformat = 0;

	if (code->index >= MAX_FMTS)
		return -EINVAL;

	for (; *list; list++) {
		struct smia_mode *mode = &(*list)->mode;
		int i;

		if ((*list)->type != SMIA_REGLIST_MODE)
			continue;

		for (i = 0; i < npixelformat; i++) {
			if (pixelformat[i] == mode->pixel_format)
				break;
		}
		if (i != npixelformat)
			continue;

		if (code->index == npixelformat) {
			if (mode->pixel_format == V4L2_PIX_FMT_SGRBG10DPCM8)
				code->code = V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8;
			else
				code->code = V4L2_MBUS_FMT_SGRBG10_1X10;
			return 0;
		}

		pixelformat[npixelformat] = mode->pixel_format;
		npixelformat++;
	}

	return -EINVAL;
}

int smia_reglist_enum_frame_size(struct smia_meta_reglist *meta,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	struct smia_reglist **list = smia_reglist_first(meta);
	struct v4l2_mbus_framefmt format;
	int cmp_width = INT_MAX;
	int cmp_height = INT_MAX;
	int index = fse->index;

	for (; *list; list++) {
		if ((*list)->type != SMIA_REGLIST_MODE)
			continue;

		smia_reglist_to_mbus(*list, &format);
		if (fse->code != format.code)
			continue;

		/* Assume that the modes are grouped by frame size. */
		if (format.width == cmp_width && format.height == cmp_height)
			continue;

		cmp_width = format.width;
		cmp_height = format.height;

		if (index-- == 0) {
			fse->min_width = format.width;
			fse->min_height = format.height;
			fse->max_width = format.width;
			fse->max_height = format.height;
			return 0;
		}
	}

	return -EINVAL;
}

int smia_reglist_enum_frame_ival(struct smia_meta_reglist *meta,
				 struct v4l2_subdev_frame_interval_enum *fie)
{
	struct smia_reglist **list = smia_reglist_first(meta);
	struct v4l2_mbus_framefmt format;
	int index = fie->index;

	for (; *list; list++) {
		struct smia_mode *mode = &(*list)->mode;

		if ((*list)->type != SMIA_REGLIST_MODE)
			continue;

		smia_reglist_to_mbus(*list, &format);
		if (fie->code != format.code)
			continue;

		if (fie->width != format.width || fie->height != format.height)
			continue;

		if (index-- == 0) {
			fie->interval = mode->timeperframe;
			return 0;
		}
	}

	return -EINVAL;
}

void smia_reglist_to_mbus(const struct smia_reglist *reglist,
			  struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = reglist->mode.window_width;
	fmt->height = reglist->mode.window_height;

	if (reglist->mode.pixel_format == V4L2_PIX_FMT_SGRBG10DPCM8)
		fmt->code = V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8;
	else
		fmt->code = V4L2_MBUS_FMT_SGRBG10_1X10;
}

/*
 *
 * Register access helpers
 *
 */

/*
 * Read a 8/16/32-bit i2c register.  The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
int smia_i2c_read_reg(struct i2c_client *client, u16 data_length,
		      u16 reg, u32 *val)
{
	int r;
	struct i2c_msg msg[1];
	unsigned char data[4];

	if (!client->adapter)
		return -ENODEV;
	if (data_length != SMIA_REG_8BIT && data_length != SMIA_REG_16BIT)
		return -EINVAL;

	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = 2;
	msg->buf = data;

	/* high byte goes out first */
	data[0] = (u8) (reg >> 8);;
	data[1] = (u8) (reg & 0xff);
	r = i2c_transfer(client->adapter, msg, 1);
	if (r < 0)
		goto err;

	msg->len = data_length;
	msg->flags = I2C_M_RD;
	r = i2c_transfer(client->adapter, msg, 1);
	if (r < 0)
		goto err;

	*val = 0;
	/* high byte comes first */
	if (data_length == SMIA_REG_8BIT)
		*val = data[0];
	else
		*val = (data[0] << 8) + data[1];

	return 0;

err:
	dev_err(&client->dev, "read from offset 0x%x error %d\n", reg, r);

	return r;
}

static void smia_i2c_create_msg(struct i2c_client *client, u16 len, u16 reg,
				u32 val, struct i2c_msg *msg,
				unsigned char *buf)
{
	msg->addr = client->addr;
	msg->flags = 0; /* Write */
	msg->len = 2 + len;
	msg->buf = buf;

	/* high byte goes out first */
	buf[0] = (u8) (reg >> 8);;
	buf[1] = (u8) (reg & 0xff);

	switch (len) {
	case SMIA_REG_8BIT:
		buf[2] = (u8) (val) & 0xff;
		break;
	case SMIA_REG_16BIT:
		buf[2] = (u8) (val >> 8) & 0xff;
		buf[3] = (u8) (val & 0xff);
		break;
	case SMIA_REG_32BIT:
		buf[2] = (u8) (val >> 24) & 0xff;
		buf[3] = (u8) (val >> 16) & 0xff;
		buf[4] = (u8) (val >> 8) & 0xff;
		buf[5] = (u8) (val & 0xff);
		break;
	default:
		BUG();
	}
}

/*
 * Write to a 8/16-bit register.
 * Returns zero if successful, or non-zero otherwise.
 */
int smia_i2c_write_reg(struct i2c_client *client, u16 data_length, u16 reg,
		       u32 val)
{
	int r;
	struct i2c_msg msg[1];
	unsigned char data[6];
	unsigned int retries = 5;

	if (!client->adapter)
		return -ENODEV;
	if (data_length != SMIA_REG_8BIT && data_length != SMIA_REG_16BIT)
		return -EINVAL;

	smia_i2c_create_msg(client, data_length, reg, val, msg, data);

	do {
		/*
		 * Due to unknown reason sensor stops responding. This
		 * loop is a temporaty solution until the root cause
		 * is found.
		 */
		r = i2c_transfer(client->adapter, msg, 1);
		if (r >= 0)
			break;

		mdelay(2);
	} while (retries--);

	if (r < 0)
		dev_err(&client->dev,
			"wrote 0x%x to offset 0x%x error %d\n", val, reg, r);
	else
		r = 0; /* on success i2c_transfer() return messages trasfered */

	if (retries < 5)
		dev_err(&client->dev, "sensor i2c stall encountered. "
			"retries: %d\n", 5 - retries);

	return r;
}

/*
 * A buffered write method that puts the wanted register write
 * commands in a message list and passes the list to the i2c framework
 */
static int smia_i2c_buffered_write_regs(struct i2c_client *client,
					const struct smia_reg *wnext, int cnt)
{
	/* FIXME: check how big cnt is */
	struct i2c_msg msg[cnt];
	unsigned char data[cnt][6];
	int wcnt = 0;
	u16 reg, data_length;
	u32 val;

	/* Create new write messages for all writes */
	while (wcnt < cnt) {
		data_length = wnext->type;
		reg = wnext->reg;
		val = wnext->val;
		wnext++;

		smia_i2c_create_msg(client, data_length, reg,
				    val, &msg[wcnt], &data[wcnt][0]);

		/* Update write count */
		wcnt++;
	}

	/* Now we send everything ... */
	return i2c_transfer(client->adapter, msg, wcnt);
}

/*
 * Write a list of registers to i2c device.
 *
 * The list of registers is terminated by SMIA_REG_TERM.
 * Returns zero if successful, or non-zero otherwise.
 */
int smia_i2c_write_regs(struct i2c_client *client,
			const struct smia_reg reglist[])
{
	int r, cnt = 0;
	const struct smia_reg *next, *wnext;
	unsigned int retries = 5;

	if (!client->adapter)
		return -ENODEV;

	if (reglist == NULL)
		return -EINVAL;

	/* Initialize list pointers to the start of the list */
	next = wnext = reglist;

	do {
		/*
		 * We have to go through the list to figure out how
		 * many regular writes we have in a row
		 */
		while (next->type != SMIA_REG_TERM
		       && next->type != SMIA_REG_DELAY) {
			/*
			 * Here we check that the actual lenght fields
			 * are valid
			 */
			if (next->type != SMIA_REG_8BIT
			    &&  next->type != SMIA_REG_16BIT) {
				dev_err(&client->dev,
					"Invalid value on entry %d 0x%x\n",
					cnt, next->type);
				return -EINVAL;
			}

			/*
			 * Increment count of successive writes and
			 * read pointer
			 */
			cnt++;
			next++;
		}

		/* Now we start writing ... */
		do {
			/*
			 * Due to unknown reason sensor stops responding. This
			 * loop is a temporaty solution until the root cause
			 * is found.
			 */
			r = smia_i2c_buffered_write_regs(client, wnext, cnt);
			if (r >= 0)
				break;

			mdelay(2);
		} while (retries--);

		/* ... and then check that everything was OK */
		if (r < 0) {
			dev_err(&client->dev, "i2c transfer error !!!\n");
			return r;
		}

		if (retries < 5)
			dev_err(&client->dev, "sensor i2c stall encountered. "
				"retries: %d\n", 5 - retries);

		/*
		 * If we ran into a sleep statement when going through
		 * the list, this is where we snooze for the required time
		 */
		if (next->type == SMIA_REG_DELAY) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(msecs_to_jiffies(next->val));
			/*
			 * ZZZ ...
			 * Update list pointers and cnt and start over ...
			 */
			next++;
			wnext = next;
			cnt = 0;
		}
	} while (next->type != SMIA_REG_TERM);

	return 0;
}

int smia_i2c_reglist_find_write(struct i2c_client *client,
				struct smia_meta_reglist *meta, u16 type)
{
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_type(meta, type);
	if (IS_ERR(reglist))
		return PTR_ERR(reglist);

	return smia_i2c_write_regs(client, reglist->regs);
}
