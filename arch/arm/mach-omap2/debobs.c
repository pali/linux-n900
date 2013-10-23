/*
 * arch/arm/mach-omap2/debobs.c
 *
 * Handle debobs pads
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Written by Peter De Schrijver <peter.de-schrijver@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <mach/control.h>
#include <mach/mux.h>
#include <mach/gpio.h>
#include <mach/board.h>

#define ETK_GPIO_BEGIN		12
#define ETK_GPIO(i)		(ETK_GPIO_BEGIN + i)
#define NUM_OF_DEBOBS_PADS	18

static int debobs_initialized;

enum debobs_pad_mode {
	GPIO = 0,
	OBS = 1,
	ETK = 2,
	NO_MODE = 3,
};

static char *debobs_pad_mode_names[] = {
	[GPIO] = "GPIO",
	[OBS] = "OBS",
	[ETK] = "ETK",
};

struct obs {
	u16 offset;
	u8 value;
	u8 mask;
};

struct debobs_pad {
	enum debobs_pad_mode mode;
	struct obs core_obs;
	struct obs wakeup_obs;
};

static struct debobs_pad debobs_pads[NUM_OF_DEBOBS_PADS];

static int debobs_mode_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t debobs_mode_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char buffer[10];
	int size;
	int pad_number = (int)file->private_data;
	struct debobs_pad *e = &debobs_pads[pad_number];

	size = snprintf(buffer, sizeof(buffer), "%s\n",
			debobs_pad_mode_names[e->mode]);
	return simple_read_from_buffer(user_buf, count, ppos, buffer, size);
}

static ssize_t debobs_mode_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char buffer[10];
	int buf_size, i, pad_number;
	u16 muxmode = OMAP34XX_MUX_MODE7;

	memset(buffer, 0, sizeof(buffer));
	buf_size = min(count, (sizeof(buffer)-1));

	if (copy_from_user(buffer, user_buf, buf_size))
		return -EFAULT;

	pad_number = (int)file->private_data;

	for (i = 0; i < NO_MODE; i++) {
		if (!strnicmp(debobs_pad_mode_names[i],
				buffer,
				strlen(debobs_pad_mode_names[i]))) {
			switch (i) {
			case ETK:
				muxmode = OMAP34XX_MUX_MODE0;
				break;
			case GPIO:
				muxmode = OMAP34XX_MUX_MODE4;
				break;
			case OBS:
				muxmode = OMAP34XX_MUX_MODE7;
				break;
			}
			omap_ctrl_writew(muxmode,
					OMAP343X_PADCONF_ETK(pad_number));
			debobs_pads[pad_number].mode = i;

			return count;
		}
	}

	return -EINVAL;
}

static const struct file_operations debobs_mode_fops = {
	.open 	= debobs_mode_open,
	.read	= debobs_mode_read,
	.write	= debobs_mode_write,
};

static int debobs_get(void *data, u64 *val)
{
	struct obs *o = data;

	*val = o->value;

	return 0;
}

static int debobs_set(void *data, u64 val)
{
	struct obs *o = data;

	val &= BIT(o->mask) - 1;

	omap_ctrl_writeb(val, o->offset);
	o->value = val;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debobs_fops, debobs_get, debobs_set, "%llu\n");

static inline int __init _new_debobs_pad(struct debobs_pad *pad, char *name,
					int number, struct dentry *root)
{
	struct dentry *d;
	struct obs *o;

	d = debugfs_create_dir(name, root);
	if (IS_ERR(d))
		return PTR_ERR(d);

	omap_ctrl_writew(OMAP34XX_MUX_MODE4, OMAP343X_PADCONF_ETK(number));
	gpio_direction_input(ETK_GPIO(number));
	gpio_export(ETK_GPIO(number), 1);
	(void) debugfs_create_file("mode", S_IRUGO | S_IWUGO, d,
					(void *)number, &debobs_mode_fops);

	o = &pad->core_obs;
	o->offset = OMAP343X_CONTROL_DEBOBS(number);
	o->value = omap_ctrl_readw(o->offset);
	o->mask = 7;
	(void) debugfs_create_file("coreobs", S_IRUGO | S_IWUGO, d, o,
					&debobs_fops);

	o = &pad->wakeup_obs;
	o->offset = OMAP343X_CONTROL_WKUP_DEBOBSMUX(number);
	o->value = omap_ctrl_readb(o->offset);
	o->mask = 5;
	(void) debugfs_create_file("wakeupobs", S_IRUGO | S_IWUGO, d, o,
					&debobs_fops);

	return 0;
}

/* Public functions */

void debug_gpio_set(unsigned gpio, int value)
{
	if (!debobs_initialized)
		return ;

	WARN_ON(gpio >= NUM_OF_DEBOBS_PADS);
	if (gpio < NUM_OF_DEBOBS_PADS)
		__gpio_set_value(ETK_GPIO(gpio), value);
}

int debug_gpio_get(unsigned gpio)
{
	if (!debobs_initialized)
		return -EINVAL;

	WARN_ON(gpio >= NUM_OF_DEBOBS_PADS);
	if (gpio < NUM_OF_DEBOBS_PADS)
		return __gpio_get_value(ETK_GPIO(gpio));

	return -EINVAL;
}

int __init init_debobs(void)
{
	struct dentry *debobs_root;
	int i, err;
	char name[10];

	debobs_root = debugfs_create_dir("debobs", NULL);
	if (IS_ERR(debobs_root))
		return PTR_ERR(debobs_root);

	for (i = 0; i < NUM_OF_DEBOBS_PADS; i++) {
		snprintf(name, sizeof(name), "hw_dbg%d", i);
		if (!gpio_request(ETK_GPIO(i), name)) {
			err = _new_debobs_pad(&debobs_pads[i], name, i,
						debobs_root);
		} else
			gpio_free(ETK_GPIO(i));
	}

	debobs_initialized = 1;

	return 0;
}

late_initcall_sync(init_debobs);
