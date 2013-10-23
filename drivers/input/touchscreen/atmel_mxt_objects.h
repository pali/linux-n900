/*
 * atmel_mxt_objects.h -- Atmel mxt touchscreen driver
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

#define MAX_OBJ_NAMELEN  64
#define MAX_OBJ_SIZE     64

#define SETUP_OBJ_DESC(_type, _name)					\
	static const struct mxt_object_desc obj_desc_ ## _type = {	\
		.obj_type = (_type),					\
		.num_params = ARRAY_SIZE(params_desc_ ## _type),	\
		.params = params_desc_ ## _type,			\
		.name = # _name						\
	};

#define OBJECT_DESC_START(_type)					\
	static const struct mxt_param_desc params_desc_ ## _type[] = {

#define OBJECT_DESC_END(_type, _name) };		\
	SETUP_OBJ_DESC(_type, t## _type ## _## _name)

#define PARAM_DESC(_b, _o, _s, _l, _name) { _b, _o, _s, _l, #_name },

struct mxt_param_desc {
	u8 pos:7;
	u8 bit:3;
	u8 sign:1;
	u8 len:5;
	const char *name;
} __attribute__ ((packed));

struct mxt_object_desc {
	u8 obj_type;
	u8 num_params;
	const struct mxt_param_desc *params;
	const char *name;
} __attribute__ ((packed));

OBJECT_DESC_START(6)
PARAM_DESC(0, 0, 0, 8, reset)
PARAM_DESC(1, 0, 0, 8, backupnv)
PARAM_DESC(2, 0, 0, 8, calibrate)
PARAM_DESC(3, 0, 0, 8, reportall)
PARAM_DESC(4, 0, 0, 8, reserved4)
PARAM_DESC(5, 0, 0, 8, diagnostic)
OBJECT_DESC_END(6, gen_cmdprocessor)

OBJECT_DESC_START(7)
PARAM_DESC(0, 0, 0, 8, idleacqint)
PARAM_DESC(1, 0, 0, 8, actvacqint)
PARAM_DESC(2, 0, 0, 8, act2idleto)
OBJECT_DESC_END(7, gen_powerconfig)

OBJECT_DESC_START(8)
PARAM_DESC(0, 0, 0, 8, chrgtime)
PARAM_DESC(2, 0, 0, 8, tchdrift)
PARAM_DESC(3, 0, 0, 8, driftst)
PARAM_DESC(4, 0, 0, 8, tchautocal)
PARAM_DESC(5, 0, 0, 1, sync_enable)
PARAM_DESC(5, 1, 0, 1, sync_disfall)
PARAM_DESC(5, 2, 0, 1, sync_disrise)
PARAM_DESC(6, 0, 0, 8, atchcalst)
PARAM_DESC(7, 0, 0, 8, atchcalsthr)
PARAM_DESC(8, 0, 0, 8, atchcalfrcthr)
PARAM_DESC(9, 0, 1, 8, atchcalfrcratio)
OBJECT_DESC_END(8, gen_acquireconfig)

OBJECT_DESC_START(9)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(0, 2, 0, 1, ctrl_disamp)
PARAM_DESC(0, 3, 0, 1, ctrl_disvect)
PARAM_DESC(0, 4, 0, 1, ctrl_dismove)
PARAM_DESC(0, 5, 0, 1, ctrl_disrel)
PARAM_DESC(0, 6, 0, 1, ctrl_disprss)
PARAM_DESC(0, 7, 0, 1, ctrl_scanen)
PARAM_DESC(1, 0, 0, 8, xorigin)
PARAM_DESC(2, 0, 0, 8, yorigin)
PARAM_DESC(3, 0, 0, 8, xsize)
PARAM_DESC(4, 0, 0, 8, ysize)
PARAM_DESC(5, 0, 0, 8, akscfg)
PARAM_DESC(6, 4, 0, 4, blen_gain)
PARAM_DESC(7, 0, 0, 8, tchthr)
PARAM_DESC(8, 0, 0, 8, tchdi)
PARAM_DESC(9, 0, 0, 1, orient_switch)
PARAM_DESC(9, 1, 0, 1, orient_invertx)
PARAM_DESC(9, 2, 0, 1, orient_inverty)
PARAM_DESC(10, 0, 0, 8, mrgtimeout)
PARAM_DESC(11, 0, 0, 8, movhysti)
PARAM_DESC(12, 0, 0, 8, movhystn)
PARAM_DESC(13, 0, 1, 4, movfilter_adaptthr)
PARAM_DESC(13, 4, 0, 3, movfilter_filterlimit)
PARAM_DESC(13, 7, 0, 1, movfilter_disable)
PARAM_DESC(14, 0, 0, 8, numtouch)
PARAM_DESC(15, 0, 0, 8, mrghyst)
PARAM_DESC(16, 0, 0, 8, mrgthr)
PARAM_DESC(17, 0, 0, 8, amphyst)
PARAM_DESC(18, 0, 0, 16, xrange)
PARAM_DESC(20, 0, 0, 16, yrange)
PARAM_DESC(22, 0, 1, 8, xloclip)
PARAM_DESC(23, 0, 1, 8, xhiclip)
PARAM_DESC(24, 0, 1, 8, yloclip)
PARAM_DESC(25, 0, 1, 8, yhiclip)
PARAM_DESC(26, 0, 0, 6, xedgectrl_correctiongradient)
PARAM_DESC(26, 6, 0, 1, xedgectrl_dislock)
PARAM_DESC(26, 7, 0, 1, xedgectrl_span)
PARAM_DESC(27, 0, 0, 8, xedgedist)
PARAM_DESC(28, 0, 0, 6, yedgectrl_correctiongradient)
PARAM_DESC(28, 6, 0, 1, yedgectrl_relupdate)
PARAM_DESC(28, 7, 0, 1, yedgectrl_span)
PARAM_DESC(29, 0, 0, 8, yedgedist)
PARAM_DESC(30, 0, 0, 8, jumplimit)
OBJECT_DESC_END(9, touch_multitouchscreen)

OBJECT_DESC_START(18)
PARAM_DESC(0, 2, 0, 1, ctrl_mode)
PARAM_DESC(0, 7, 0, 1, ctrl_dismntr)
PARAM_DESC(1, 0, 0, 8, command)
OBJECT_DESC_END(18, spt_commsconfig)

OBJECT_DESC_START(19)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(0, 2, 0, 1, ctrl_forcerpt)
PARAM_DESC(1, 0, 0, 8, reportmask)
PARAM_DESC(2, 0, 0, 8, dir)
PARAM_DESC(3, 0, 0, 8, intpullup)
PARAM_DESC(4, 0, 0, 8, out)
PARAM_DESC(5, 0, 0, 8, wake)
PARAM_DESC(6, 0, 0, 8, pwm)
PARAM_DESC(7, 0, 0, 8, period)
PARAM_DESC(8, 0, 0, 8, duty_0)
PARAM_DESC(9, 0, 0, 8, duty_1)
PARAM_DESC(10, 0, 0, 8, duty_2)
PARAM_DESC(11, 0, 0, 8, duty_3)
PARAM_DESC(12, 0, 0, 8, trigger_0)
PARAM_DESC(13, 0, 0, 8, trigger_1)
PARAM_DESC(14, 0, 0, 8, trigger_2)
PARAM_DESC(15, 0, 0, 8, trigger_3)
OBJECT_DESC_END(19, spt_gpiopwm)

OBJECT_DESC_START(20)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(0, 2, 0, 1, ctrl_disgrip)
PARAM_DESC(0, 3, 0, 1, ctrl_disface)
PARAM_DESC(0, 4, 0, 1, ctrl_gripmode)
PARAM_DESC(1, 0, 0, 8, xlogrip)
PARAM_DESC(2, 0, 0, 8, xhigrip)
PARAM_DESC(3, 0, 0, 8, ylogrip)
PARAM_DESC(4, 0, 0, 8, yhigrip)
PARAM_DESC(5, 0, 0, 8, maxtchs)
PARAM_DESC(7, 0, 0, 8, szthr1)
PARAM_DESC(8, 0, 0, 8, szthr2)
PARAM_DESC(9, 0, 0, 8, shptrh1)
PARAM_DESC(10, 0, 0, 8, shpthr2)
PARAM_DESC(11, 0, 0, 8, supextto)
OBJECT_DESC_END(20, proci_gripfacesupression)

OBJECT_DESC_START(22)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(0, 2, 0, 1, ctrl_freqhen)
PARAM_DESC(0, 3, 0, 1, ctrl_medianen)
PARAM_DESC(0, 4, 0, 1, ctrl_gcafen)
PARAM_DESC(0, 7, 0, 1, ctrl_disgcafd)
PARAM_DESC(1, 5, 0, 3, virtrefrnkg)
PARAM_DESC(2, 0, 0, 8, reserved2)
PARAM_DESC(3, 0, 0, 16, gcaful)
PARAM_DESC(5, 0, 0, 16, gcafll)
PARAM_DESC(7, 0, 0, 6, actvgcafvalid)
PARAM_DESC(8, 0, 0, 8, noisethr)
PARAM_DESC(9, 0, 0, 8, reserved9)
PARAM_DESC(10, 1, 0, 2, freqhopscale)
PARAM_DESC(11, 0, 0, 8, freq_0)
PARAM_DESC(12, 0, 0, 8, freq_1)
PARAM_DESC(13, 0, 0, 8, freq_2)
PARAM_DESC(14, 0, 0, 8, freq_3)
PARAM_DESC(15, 0, 0, 8, freq_4)
PARAM_DESC(16, 0, 0, 6, idlegcafvalid)
OBJECT_DESC_END(22, procg_noisesupression)

OBJECT_DESC_START(24)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(1, 0, 0, 8, numgest)
PARAM_DESC(2, 0, 0, 1, gesten_press)
PARAM_DESC(2, 1, 0, 1, gesten_release)
PARAM_DESC(2, 2, 0, 1, gesten_tap)
PARAM_DESC(2, 3, 0, 1, gesten_dbltap)
PARAM_DESC(2, 4, 0, 1, gesten_flick)
PARAM_DESC(2, 5, 0, 1, gesten_drag)
PARAM_DESC(2, 6, 0, 1, gesten_spress)
PARAM_DESC(2, 7, 0, 1, gesten_lpress)
PARAM_DESC(3, 0, 0, 1, gesten_rpress)
PARAM_DESC(3, 1, 0, 1, gesten_throw)
PARAM_DESC(4, 0, 0, 1, process_shorten)
PARAM_DESC(4, 1, 0, 1, process_longen)
PARAM_DESC(4, 2, 0, 1, process_repen)
PARAM_DESC(4, 3, 0, 1, process_dbltapen)
PARAM_DESC(4, 4, 0, 1, process_flicken)
PARAM_DESC(4, 5, 0, 1, process_throwen)
PARAM_DESC(5, 0, 0, 7, tapto)
PARAM_DESC(6, 0, 0, 7, flickto)
PARAM_DESC(7, 0, 0, 7, dragto)
PARAM_DESC(8, 0, 0, 7, spressto)
PARAM_DESC(9, 0, 0, 7, lpressto)
PARAM_DESC(10, 0, 0, 7, represstapto)
PARAM_DESC(11, 0, 0, 16, flickthr)
PARAM_DESC(13, 0, 0, 16, dragthr)
PARAM_DESC(15, 0, 0, 16, tapthr)
PARAM_DESC(17, 0, 0, 16, throwthr)
OBJECT_DESC_END(24, proci_onetouchgestureprocessor)

OBJECT_DESC_START(25)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(1, 0, 0, 8, cmd)
PARAM_DESC(2, 0, 0, 16, hisiglim_0)
PARAM_DESC(4, 0, 0, 16, losiglim_0)
OBJECT_DESC_END(25, spt_selftest)

OBJECT_DESC_START(27)
PARAM_DESC(0, 0, 0, 1, ctrl_enable)
PARAM_DESC(0, 1, 0, 1, ctrl_rpten)
PARAM_DESC(1, 0, 0, 8, numgest)
PARAM_DESC(3, 5, 0, 1, gesten_pinch)
PARAM_DESC(3, 6, 0, 1, gesten_rotate)
PARAM_DESC(3, 7, 0, 1, gesten_stretch)
PARAM_DESC(4, 0, 0, 7, roratethr)
PARAM_DESC(5, 0, 0, 16, zoomthr)
OBJECT_DESC_END(27, proci_twotouchgestureprocessor)

OBJECT_DESC_START(28)
PARAM_DESC(0, 0, 0, 8, ctrl)
PARAM_DESC(1, 0, 0, 8, cmd)
PARAM_DESC(2, 0, 0, 8, mode)
PARAM_DESC(3, 0, 0, 6, idlegcafdepth)
PARAM_DESC(4, 0, 0, 6, actvgcafdepth)
PARAM_DESC(5, 0, 1, 8, voltage)
OBJECT_DESC_END(28, spt_cteconfig)

OBJECT_DESC_START(38)
PARAM_DESC(0, 0, 0, 8, data_0)
PARAM_DESC(1, 0, 0, 8, data_1)
PARAM_DESC(2, 0, 0, 8, data_2)
PARAM_DESC(3, 0, 0, 8, data_3)
PARAM_DESC(4, 0, 0, 8, data_4)
PARAM_DESC(5, 0, 0, 8, data_5)
PARAM_DESC(6, 0, 0, 8, data_6)
PARAM_DESC(7, 0, 0, 8, data_7)
OBJECT_DESC_END(38, spt_userdata)

static const struct mxt_object_desc *mxt_obj_descs[] = {
	&obj_desc_6,
	&obj_desc_7,
	&obj_desc_8,
	&obj_desc_9,
	&obj_desc_18,
	&obj_desc_19,
	&obj_desc_20,
	&obj_desc_22,
	&obj_desc_24,
	&obj_desc_25,
	&obj_desc_27,
	&obj_desc_28,
	&obj_desc_38,
};

