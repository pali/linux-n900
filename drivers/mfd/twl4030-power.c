/*
 * linux/drivers/i2c/chips/twl4030-power.c
 *
 * Handle TWL4030 Power initialization
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2006 Texas Instruments, Inc
 *
 * Written by 	Kalle Jokiniemi
 *		Peter De Schrijver <peter.de-schrijver@nokia.com>
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

#include <linux/module.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/i2c/twl4030.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>

static u8 triton_next_free_address = 0x2b;

#define PWR_P1_SW_EVENTS	0x10
#define PWR_DEVOFF	(1<<0)

#define PHY_TO_OFF_PM_MASTER(p)		(p - 0x36)
#define PHY_TO_OFF_PM_RECEIVER(p)	(p - 0x5b)

#define NUM_OF_RESOURCES	28

/* resource - hfclk */
#define R_HFCLKOUT_DEV_GRP 	PHY_TO_OFF_PM_RECEIVER(0xe6)

/* PM events */
#define R_P1_SW_EVENTS		PHY_TO_OFF_PM_MASTER(0x46)
#define R_P2_SW_EVENTS		PHY_TO_OFF_PM_MASTER(0x47)
#define R_P3_SW_EVENTS		PHY_TO_OFF_PM_MASTER(0x48)
#define R_CFG_P1_TRANSITION	PHY_TO_OFF_PM_MASTER(0x36)
#define R_CFG_P2_TRANSITION	PHY_TO_OFF_PM_MASTER(0x37)
#define R_CFG_P3_TRANSITION	PHY_TO_OFF_PM_MASTER(0x38)

#define LVL_WAKEUP	0x08

#define ENABLE_WARMRESET (1<<4)

#define END_OF_SCRIPT		0x3f

#define R_SEQ_ADD_A2S		PHY_TO_OFF_PM_MASTER(0x55)
#define R_SEQ_ADD_SA12		PHY_TO_OFF_PM_MASTER(0x56)
#define	R_SEQ_ADD_S2A3		PHY_TO_OFF_PM_MASTER(0x57)
#define	R_SEQ_ADD_WARM		PHY_TO_OFF_PM_MASTER(0x58)
#define R_MEMORY_ADDRESS	PHY_TO_OFF_PM_MASTER(0x59)
#define R_MEMORY_DATA		PHY_TO_OFF_PM_MASTER(0x5a)

#define R_PROTECT_KEY		0x0E
#define KEY_1			0xC0
#define KEY_2			0x0C

#define R_VDD1_OSC		0x5C
#define R_VDD2_OSC		0x6A
#define R_VIO_OSC		0x52
#define EXT_FS_CLK_EN		(0x1 << 6)

#define R_WDT_CFG		0x03
#define WDT_WRK_TIMEOUT		0x03

#define R_UNLOCK_TEST_REG	0x12
#define TWL_EEPROM_R_UNLOCK	0x49

#define TWL_SIL_TYPE(rev)	((rev) & 0x00FFFFFF)
#define TWL_SIL_REV(rev)	((rev) >> 24)
#define TWL_SIL_5030		0x09002F
#define TWL_REV_1_0		0x00
#define TWL_REV_1_1		0x10

/* resource configuration registers */

#define DEVGROUP_OFFSET		0
#define TYPE_OFFSET		1
#define REMAP_OFFSET		2

static int res_config_addrs[] = {
	[RES_VAUX1] 	= 0x17,
	[RES_VAUX2] 	= 0x1b,
	[RES_VAUX3] 	= 0x1f,
	[RES_VAUX4] 	= 0x23,
	[RES_VMMC1] 	= 0x27,
	[RES_VMMC2] 	= 0x2b,
	[RES_VPLL1] 	= 0x2f,
	[RES_VPLL2] 	= 0x33,
	[RES_VSIM] 	= 0x37,
	[RES_VDAC] 	= 0x3b,
	[RES_VINTANA1] 	= 0x3f,
	[RES_VINTANA2] 	= 0x43,
	[RES_VINTDIG] 	= 0x47,
	[RES_VIO] 	= 0x4b,
	[RES_VDD1] 	= 0x55,
	[RES_VDD2] 	= 0x63,
	[RES_VUSB_1v5] 	= 0x71,
	[RES_VUSB_1v8] 	= 0x74,
	[RES_VUSB_3v1] 	= 0x77,
	[RES_VUSBCP] 	= 0x7a,
	[RES_REGEN] 	= 0x7f,
	[RES_NRES_PWRON] = 0x82,
	[RES_CLKEN] 	= 0x85,
	[RES_SYSEN] 	= 0x88,
	[RES_HFCLKOUT] 	= 0x8b,
	[RES_32KCLKOUT] = 0x8e,
	[RES_RESET] 	= 0x91,
	[RES_Main_Ref] 	= 0x94,
};

static int __init twl4030_write_script_byte(u8 address, u8 byte)
{
	int err;

	err = twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, address,
					R_MEMORY_ADDRESS);
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, byte,
					R_MEMORY_DATA);

	return err;
}

static int __init twl4030_write_script_ins(u8 address, u16 pmb_message,
						u8 delay, u8 next)
{
	int err = 0;

	address *= 4;
	err |= twl4030_write_script_byte(address++, pmb_message >> 8);
	err |= twl4030_write_script_byte(address++, pmb_message & 0xff);
	err |= twl4030_write_script_byte(address++, delay);
	err |= twl4030_write_script_byte(address++, next);

	return err;
}

static int __init twl4030_write_script(u8 address, struct twl4030_ins *script,
					int len)
{
	int err = 0;

	for (; len; len--, address++, script++) {
		if (len == 1)
			err |= twl4030_write_script_ins(address,
							script->pmb_message,
							script->delay,
							END_OF_SCRIPT);
		else
			err |= twl4030_write_script_ins(address,
							script->pmb_message,
							script->delay,
							address + 1);
	}

	return err;
}

static int __init config_wakeup3_sequence(u8 address)
{

	int err = 0;
	u8 data;

	/* Set SLEEP to ACTIVE SEQ address for P3 */
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, address,
				  R_SEQ_ADD_S2A3);

	err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &data,
					R_P3_SW_EVENTS);
	data |= LVL_WAKEUP;
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, data,
					R_P3_SW_EVENTS);
	if (err)
		printk(KERN_ERR "TWL4030 wakeup sequence for P3" \
				"config error\n");

	return err;
}

static int __init config_wakeup12_sequence(u8 address)
{
	int err = 0;
	u8 data;

	/* Set SLEEP to ACTIVE SEQ address for P1 and P2 */
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, address,
				  R_SEQ_ADD_SA12);

	/* P1/P2 LVL_WAKEUP should be on LEVEL */
	err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &data,
					R_P1_SW_EVENTS);
	data |= LVL_WAKEUP;
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, data,
					R_P1_SW_EVENTS);

	err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &data,
					R_P2_SW_EVENTS);
	data |= LVL_WAKEUP;
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, data,
					R_P2_SW_EVENTS);

	if (machine_is_omap_3430sdp() || machine_is_omap_ldp()) {
		/* Disabling AC charger effect on sleep-active transitions */
		err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &data,
						R_CFG_P1_TRANSITION);
		data &= ~(1<<1);
		err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, data ,
						R_CFG_P1_TRANSITION);
	}

	if (err)
		printk(KERN_ERR "TWL4030 wakeup sequence for P1 and P2" \
				"config error\n");

	return err;
}

static int __init config_sleep_sequence(u8 address)
{
	int err = 0;

	/* Set ACTIVE to SLEEP SEQ address in T2 memory*/
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, address,
				  R_SEQ_ADD_A2S);

	if (err)
		printk(KERN_ERR "TWL4030 sleep sequence config error\n");

	return err;
}

static int __init config_warmreset_sequence(u8 address)
{

	int err = 0;
	u8 rd_data;

	/* Set WARM RESET SEQ address for P1 */
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, address,
					R_SEQ_ADD_WARM);

	/* P1/P2/P3 enable WARMRESET */
	err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &rd_data,
					R_P1_SW_EVENTS);
	rd_data |= ENABLE_WARMRESET;
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, rd_data,
					R_P1_SW_EVENTS);

	err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &rd_data,
					R_P2_SW_EVENTS);
	rd_data |= ENABLE_WARMRESET;
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, rd_data,
					R_P2_SW_EVENTS);

	err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &rd_data,
					R_P3_SW_EVENTS);
	rd_data |= ENABLE_WARMRESET;
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, rd_data,
					R_P3_SW_EVENTS);

	if (err)
		printk(KERN_ERR
			"TWL4030 warmreset seq config error\n");
	return err;
}

static int __init load_triton_script(struct twl4030_script *tscript)
{
	u8 address = triton_next_free_address;
	int err;
	int i;

	err = twl4030_write_script(address, tscript->script, tscript->size);
	if (err)
		return err;

	triton_next_free_address += tscript->size;

	for (i = 0; i < tscript->number_of_events; i++) {

		/* Check if script is going beyond last valid address */
		if (address + tscript->size > END_OF_SCRIPT) {

			WARN(1, "TWL4030 script event %d" \
				 " do not fit in memory\n"
				, tscript->events[i].event);
			return -EINVAL;
		}

		/* Check if event pointer is in script area */
		if (tscript->events[i].offset >= tscript->size) {

			WARN(1,	"TWL4030 script event %d has invalid start" \
				" at 0x%x in the script ending at 0x%x\n",
				 tscript->events[i].event,
				 address + tscript->events[i].offset,
				 address + tscript->size);
			return -EINVAL;
		}

		switch (tscript->events[i].event) {
		case TRITON_WRST:
			err |= config_warmreset_sequence(address
						+ tscript->events[i].offset);
			break;
		case TRITON_WAKEUP12:
			err |= config_wakeup12_sequence(address
						+ tscript->events[i].offset);
			break;
		case TRITON_WAKEUP3:
			err |= config_wakeup3_sequence(address
						+ tscript->events[i].offset);
			break;
		case TRITON_SLEEP:
			err |= config_sleep_sequence(address
						+ tscript->events[i].offset);
			break;
		default:
			WARN(1, "Event number %d unknown\n"
				, tscript->events[i].event);
			return  -EINVAL;
		}
	}
	return err;
}

static int __init twl4030_configure_resource(struct twl4030_resconfig *rconfig)
{
	int rconfig_addr;
	int err;
	u8 type;

	if (rconfig->resource > NUM_OF_RESOURCES) {
		printk(KERN_ERR
			"TWL4030 Resource %d does not exist\n",
			rconfig->resource);
		return -EINVAL;
	}

	rconfig_addr = res_config_addrs[rconfig->resource];

	/* Set resource group */
	if (rconfig->devgroup >= 0) {
		err = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					rconfig->devgroup << 5,
					rconfig_addr + DEVGROUP_OFFSET);
		if (err < 0) {
			printk(KERN_ERR
				"TWL4030 failed to program devgroup");
			return err;
		}
	}

	/* Set resource types */
	err = twl4030_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, &type,
				       rconfig_addr + TYPE_OFFSET);
	if (err < 0) {
		printk(KERN_ERR
		       "TWL4030 Resource %d type could not read\n",
		       rconfig->resource);
		return err;
	}

	if (rconfig->type >= 0) {
		type &= ~7;
		type |= rconfig->type;
	}

	if (rconfig->type2 >= 0) {
		type &= ~(3 << 3);
		type |= rconfig->type2 << 3;
	}

	err = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				type, rconfig_addr + TYPE_OFFSET);
	if (err < 0) {
		printk(KERN_ERR
		       "TWL4030 failed to program resource type");
		return err;
	}

	if (rconfig->remap >= 0) {
		err = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				rconfig->remap,
				rconfig_addr + REMAP_OFFSET);
		if (err < 0) {
			printk(KERN_ERR "TWL4030 failed to program remap");
			return err;
		}
	}

	return 0;
}

struct twl4030_reg_data {
	int usecount;
	u8 resource;
	u8 base;
	u8 offstate;
	u8 initdone;
	int delay;
};
struct twl4030_reg_data twl4030_custom_regs[] = {
	{ 0, RES_VAUX1, TWL4030_VAUX1_DEV_GRP, 0x88, 0, 500 },
	{ 0, RES_VMMC2, TWL4030_VMMC2_DEV_GRP, 0x88, 0, 100 },
	{ 0, 0, 0, 0, 0 },
};
static DEFINE_MUTEX(reg_mutex);

static int twl4030_set_regulator_state(int res, int enable)
{
	u8 val;
	int ret;
	struct twl4030_reg_data *reg_data = twl4030_custom_regs;

	while (1) {
		if (!reg_data->resource)
			return -EINVAL;
		if (reg_data->resource == res)
			break;
		reg_data++;
	}
	if (enable && !(reg_data->usecount++))
		val = 0xee;
	else if (!enable && !(--reg_data->usecount))
		val = reg_data->offstate;
	else
		return 0;

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			val, reg_data->base + 2);

	if (!ret && !reg_data->initdone) {
		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			0, reg_data->base);
		if (!ret)
			reg_data->initdone = 1;
	}

	if (ret) {
		if (enable)
			reg_data->usecount--;
		else
			reg_data->usecount++;
	}
	/* Wait until voltage stabilizes */
	if (enable)
		udelay(reg_data->delay);
	return ret;
}

int twl4030_enable_regulator(int res)
{
	int ret;
	mutex_lock(&reg_mutex);
	ret = twl4030_set_regulator_state(res, 1);
	mutex_unlock(&reg_mutex);
	return ret;
}
EXPORT_SYMBOL(twl4030_enable_regulator);

int twl4030_disable_regulator(int res)
{
	int ret;
	mutex_lock(&reg_mutex);
	ret = twl4030_set_regulator_state(res, 0);
	mutex_unlock(&reg_mutex);
	return ret;
}
EXPORT_SYMBOL(twl4030_disable_regulator);

/**
 * @brief twl_workaround - implement errata XYZ
 * XYZ errata workaround requires the TWL DCDCs to use
 * HFCLK - for this you need to write to all OSC regs to
 * enable this path
 * WARNING: you SHOULD change your board dependent script
 * file to handle RET and OFF mode sequences correctly
 *
 * @return
 */
static void __init twl_workaround(void)
{
	u8 val;
	u8 reg[]={R_VDD1_OSC, R_VDD2_OSC, R_VIO_OSC};
	u8 wdt_orig = 0;
	int i;
	int err;
	/* Setup the twl wdt to take care of borderline failure case */
	err = twl4030_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, &wdt_orig,
			R_WDT_CFG);
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, WDT_WRK_TIMEOUT,
			R_WDT_CFG);

	for (i = 0; i < sizeof(reg); i++) {
		err |= twl4030_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, &val, reg[i]);
		val |= EXT_FS_CLK_EN;
		err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, val, reg[i]);
	}

	/* restore the original value */
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, wdt_orig,
			R_WDT_CFG);
	if (err)
		pr_warning("TWL4030: workaround setup failed!\n");
}

void __init twl4030_power_init(struct twl4030_power_data *triton2_scripts)
{
	int err = 0;
	int i;
	struct twl4030_resconfig *resconfig;
	u32 twl4030_rev = 0;
	bool apply_workaround = 0;

	err = twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, KEY_1,
				R_PROTECT_KEY);
	err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, KEY_2,
				R_PROTECT_KEY);
	if (err)
		pr_err("TWL4030 Unable to unlock registers\n");

	err = twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, TWL_EEPROM_R_UNLOCK,
			R_UNLOCK_TEST_REG);
	if (err)
		pr_err("TWL4030 Unable to unlock IDCODE registers\n");

	err = twl4030_i2c_read(TWL4030_MODULE_INTBR, (u8 *)(&twl4030_rev),
			0x0, 4);
	if (err)
		pr_err("TWL4030: unable to read IDCODE-%d\n", err);

	err = twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x0,
			R_UNLOCK_TEST_REG);
	if (err)
		pr_err("TWL4030 Unable to relock IDCODE registers\n");

	/* introduce workaround based on TWL4030 revision */
	if ((TWL_SIL_TYPE(twl4030_rev) == TWL_SIL_5030) &&
		(TWL_SIL_REV(twl4030_rev) <= TWL_REV_1_1))
		apply_workaround = 1;

	if (apply_workaround) {
		pr_err("TWL5030: Enabling workaround for rev 0x%04X\n",
				twl4030_rev);
		twl_workaround();
	}

	for (i = 0; i < triton2_scripts->scripts_size; i++) {
		err = load_triton_script(triton2_scripts->scripts[i]);
		if (err < 0) {
			printk(KERN_ERR "TWL4030 failed to load scripts");
			break;
		}
	}

	resconfig = triton2_scripts->resource_config;
	if (resconfig) {
		while (resconfig->resource) {
			err = twl4030_configure_resource(resconfig);
			resconfig++;
			if (err < 0) {
				printk(KERN_ERR
				       "TWL4030 failed to configure resource");
				break;
			}
		}
	}

	if (twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, 0, R_PROTECT_KEY))
		printk(KERN_ERR
			"TWL4030 Unable to relock registers\n");
}
