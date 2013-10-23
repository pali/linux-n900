/*
 * include/linux/i2c/twl4030-madc.h
 *
 * TWL4030 MADC module driver header
 *
 * Mikko Ylinen <mikko.k.ylinen@nokia.com>
 * Tuukka Tikkanen <tuukka.tikkanen@nokia.com>
 *
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef _TWL4030_MADC_H
#define _TWL4030_MADC_H

#define TWL4030_MADC_MAX_CHANNELS 16

#define	TWL4030_MADC_ADCIN0		(1<<0)
#define	TWL4030_MADC_ADCIN1		(1<<1)
#define	TWL4030_MADC_ADCIN2		(1<<2)
#define	TWL4030_MADC_ADCIN3		(1<<3)
#define	TWL4030_MADC_ADCIN4		(1<<4)
#define	TWL4030_MADC_ADCIN5		(1<<5)
#define	TWL4030_MADC_ADCIN6		(1<<6)
#define	TWL4030_MADC_ADCIN7		(1<<7)
#define	TWL4030_MADC_ADCIN8		(1<<8)
#define	TWL4030_MADC_ADCIN9		(1<<9)
#define	TWL4030_MADC_ADCIN10		(1<<10)
#define	TWL4030_MADC_ADCIN11		(1<<11)
#define	TWL4030_MADC_ADCIN12		(1<<12)
#define	TWL4030_MADC_ADCIN13		(1<<13)
#define	TWL4030_MADC_ADCIN14		(1<<14)
#define	TWL4030_MADC_ADCIN15		(1<<15)

/* Fixed channels */
#define TWL4030_MADC_BTEMP		TWL4030_MADC_ADCIN1
#define TWL4030_MADC_VBUS		TWL4030_MADC_ADCIN8
#define TWL4030_MADC_VBKB		TWL4030_MADC_ADCIN9
#define	TWL4030_MADC_ICHG		TWL4030_MADC_ADCIN10
#define TWL4030_MADC_VCHG		TWL4030_MADC_ADCIN11
#define	TWL4030_MADC_VBAT		TWL4030_MADC_ADCIN12


/* User space interface */

#define TWL4030_MADC_IOC_MAGIC '`'

/*
 * ADC_RAW_READ (deprecated; use ADC_SETUP_CONVERSION instead)
 *
 * Argument is pointer to twl4030_madc_user_parms.
 * Performs a conversion synchronously on single channel with minimal delay.
 * Error is returned only if the parameters are invalid (EINVAL or EACCES).
 * Any other errors are reported in the status member of the structure.
 *
 * channel - Channel number to convert
 * average - Nonzero value indicates hardware averaging should be enabled
 * status - 0 after successful conversion, otherwise -ERRNO
 * result - Value obtained from the AD conversion
 */
#define TWL4030_MADC_IOCX_ADC_RAW_READ		_IO(TWL4030_MADC_IOC_MAGIC, 0)

struct twl4030_madc_user_parms {
	int channel;
	int average;
	int status;
	u16 result;
};

/*
 * ADC_SETUP_CONVERSION
 * ADC_ABORT_CONVERSION
 *
 * Argument is pointer to twl4030_madc_setup_parms.
 * Requests a conversion sequence on one or more channels. Only one conversion
 * request per file handle may be active at any time. When the request has
 * been processed, the file handle is flagged ready for data reading and
 * the results (or error code) may be obtained with read(). All data must
 * be read with single read (16*2 bytes) and returned data contains all
 * channels (16), even those which were not included in the conversion.
 *
 * Any outstanding conversion request may be cleared with ADC_ABORT_CONVERSION
 * at any time, this will also purge any possible data which is available.
 * A new conversion request may be initiated immediately. (Due to the nature
 * of the hardware, any aborted conversion may or may not still be carried
 * out, but the results will be discarded.)
 *
 * The ioctl call may indicate immediate errors such as:
 * EACCESS (invalid pointer)
 * EINVAL (channel mask, averaging mask and/or flags are invalid)
 * EBUSY (existing conversion request active or data has not been read)
 * ENOMEM
 *
 * Any errors encountered later during processing the request are reported
 * by flagging the file handle ready for reading and returning the error
 * code as the return value of read().
 *
 * channel_mask - Channels to convert, value 1 in any bit position enables
 *                corresponding channel to be converted. Bit0 = ch0 etc.
 * average_mask - Each bit position controls hardware averaging, 1 = enabled.
 * delay - Amount of time in nanoseconds for the hardware to wait before
 *         starting the sampling and conversion sequence. For RT triggered
 *         conversions this time starts when trigger edge is detected.
 *         For hybrid conversions this value will affect both SW and RT
 *         components; see below for more information. For pure SW conversions,
 *         this parameter is ignored and minimum delay is used.
 * flags - Combination (logical or) of the following:
 *   TWL4030_MADC_TRIG_MODE_SW - Conversion is to be done as soon as possible
 *   TWL4030_MADC_TRIG_MODE_RT - Conversion is to be started by HW trigger
 *   TWL4030_MADC_TRIG_MODE_HYBRID - See below
 *   Additionally for MODE_RT and MODE_HYBRID one of:
 *   TWL4030_MADC_RT_TRIG_DEFAULT
 *   TWL4030_MADC_RT_TRIG_RISING
 *   TWL4030_MADC_RT_TRIG_FALLING
 *
 * Hybrid mode:
 * In devices where a hardware indication of significant battery drain is
 * connected to the startadc signal the hybrid mode can be used to obtain
 * samples guaranteed to be taken outside of such drain period.
 * With proper delay and polarity settings, the RT conversion samples after
 * battery voltage leves have had chance to recover from a drain period.
 * This makes the RT conversion the preferred source for conversion results.
 * In order to return results withing acceptable amount of time in case no
 * drain indications are forthcoming a SW conversion is started as well.
 * In case the SW conversion completes without RT completion in 10 ms, the
 * SW conversion values are returned and the RT conversion is aborted. It
 * should be noted that BOTH conversions will use the delay parameter, for
 * SW conversion the delay is inserted between triggering the hardware start
 * bit and sampling.
 */
#define TWL4030_MADC_IOCX_ADC_SETUP_CONVERSION	_IO(TWL4030_MADC_IOC_MAGIC, 1)
#define TWL4030_MADC_IOCX_ADC_ABORT_CONVERSION	_IO(TWL4030_MADC_IOC_MAGIC, 2)

struct twl4030_madc_setup_parms {
	u16 channel_mask;
	u16 average_mask;
	u16 delay; /* ns */
	u16 flags;
};

#define TWL4030_MADC_RT_TRIG_DEFAULT		(1<<0)
#define TWL4030_MADC_RT_TRIG_RISING		(1<<0)
#define TWL4030_MADC_RT_TRIG_FALLING		(0<<0)
#define TWL4030_MADC_RT_TRIG_MASK		(1<<0)
#define TWL4030_MADC_TRIG_MODE_RT		(1<<1)
#define TWL4030_MADC_TRIG_MODE_SW		(2<<1)
#define TWL4030_MADC_TRIG_MODE_HYBRID		(3<<1)
#define TWL4030_MADC_TRIG_MODE_MASK		(3<<1)
#define TWL4030_MADC_HYBRID_SW_COMPLETED	(1<<3) /* Driver internal */
#define TWL4030_MADC_HYBRID_RT_COMPLETED	(1<<4) /* Driver internal */
#define TWL4030_MADC_DATA_TAINTED		(1<<5) /* Driver internal */
#define TWL4030_MADC_HYBRID_SW_FAILED		(1<<6) /* Driver internal */
#define TWL4030_MADC_INTERNAL_FLAGS		(0xf<<3)
#define TWL4030_MADC_CB_RELEASES_MEM	       	(1<<7)

/* Kernel space interface */

struct twl4030_madc_request;


/**
 * twl4030_madc_halt_ch0 - Stop processing conversions on channel 0
 *
 * This function is used to signal the twl4030 madc driver that it should not
 * perform conversions which involve ch0. Any other driver which reconfigures
 * the chip in a way which might affect the results (such as enabling
 * pullups) should call this function before making the reconfiguration. This
 * function must not be called in any context where sleeping is not possible.
 */
void twl4030_madc_halt_ch0(void);


/**
 * twl4030_madc_resume_ch0 - Resume processing conversions involving ch0
 *
 * This function is used to signal the twl4030 madc driver that it can
 * resume operations on channel 0. The hardware must be broght back to a state
 * where this is possible before calling this function. This function must
 * not be called in any context where sleeping is not possible.
 */
void twl4030_madc_resume_ch0(void);


/**
 * twl4030_madc_cancel_request - Abort a TWL4030 madc conversion if possible
 * @req:	Pointer identifying the request to abort
 *
 * This function aborts a conversion request created with any of the
 * asynchronous madc conversion request functions, preventing it from being
 * performed if possible.
 *
 * The associated callback function might still be called during this function
 * and the caller must be prepared to handle the callback until this function
 * has returned. The request pointer must be considered invalid (although it
 * may appear in the potential callback) immediately upon calling this
 * function.
 *
 * This function may block execution and cannot be used in any context where
 * sleeping is not possible.
 *
 * The argument must be a pointer returned by twl4030_madc_start_conversion
 * or twl4030_madc_trig_conversion.
 */
void twl4030_madc_cancel_request(struct twl4030_madc_request *req);


/**
 * twl4030_madc_single_conversion - Synchronous TWL4030 madc conversion
 * @channel:	The channel number to be converted
 * @average:	A non-zero value indicates hardware averaging should be used
 *
 * This function performs a madc conversion for one channel using TWL4030
 * madc. This function may block execution and cannot be used in any context
 * where sleeping is not possible.
 *
 * A non-negative return value contains the raw sampled value in
 * the lowest 16 bits. A negative value indicates error condition.
 */
int twl4030_madc_single_conversion(u16 channel, u16 average);


/**
 * twl4030_madc_conversion - Synchronous TWL4030 madc conversion
 * @channel_mask:	Bitmask indicating channels to be converted.
 * @average_mask:	Bitmask indicating which channels should be sampled and
 *			converted 4 times for averaging by the hardware.
 * @rbuf:		Pointer to array for converted values. The array must
 *			have at least (highest numbered channel converted + 1)
 *			elements. Channel numbers are used to index the array.
 *
 * This function performs a madc conversion for one or more channels using
 * TWL4030 madc. This function may block execution and cannot be used in
 * any context where sleeping is not possible.
 *
 * On success the function returns 0.
 * A negative value indicates error condition.
 */
int twl4030_madc_conversion(u16 channel_mask, u16 average_mask, u16 *rbuf);


/**
 * twl4030_madc_start_conversion - Simple asynchronous TWL4030 madc conversion
 * @channel_mask:	Bitmask indicating channels to be converted.
 * @average_mask:	Bitmask indicating which channels should be sampled and
 *			converted 4 times for averaging by the hardware.
 * @rbuf:		Pointer to array for converted values. The array must
 *			have at least (highest numbered channel converted + 1)
 *			elements. Channel numbers are used to index the array.
 * @func_cb:		Callback function to be called after conversion.
 *			First argument indicates success (0) or error code.
 * @user_data:		Used as second argument to callback function.
 *
 * This function performs a madc conversion for one or more channels using
 * TWL4030 madc. When the conversion is complete or has irrevocably failed,
 * the callback function is called. The first argument of the callback is
 * the status of completed operation: 0 indicates success, otherwise -errno.
 * The second argument to the callback is the pointer returned by this
 * function.
 *
 * This function may block execution and cannot be used in any context where
 * sleeping is not possible.
 *
 * On success the function returns pointer to internal structure used by the
 * driver, which may be used to call twl4030_madc_cancel_request.
 * IS_ERR may be used to test for error condition.
 */
struct twl4030_madc_request *
    twl4030_madc_start_conversion(u16 channel_mask, u16 average_mask,
				  u16 *rbuf,
				  void (*func_cb)(int,
					    struct twl4030_madc_request *),
				  void *user_data);

/**
 * twl4030_madc_start_conversion_ex - Complex asynchronous madc conversion
 * @channel_mask:	Bitmask indicating channels to be converted.
 * @average_mask:	Bitmask indicating which channels should be sampled and
 *			converted 4 times for averaging by the hardware.
 * @flags:		Combination of flags. Exactly one of
 *			TWL4030_MADC_TRIG_MODE_RT,
 *			TWL4030_MADC_TRIG_MODE_SW or
 *			TWL4030_MADC_TRIG_MODE_HYBRID must be included in
 *			the value. In addition, caller must specify one of
 *                      TWL4030_MADC_RT_TRIG_DEFAULT,
 *			TWL4030_MADC_RT_TRIG_RISING or
 *			TWL4030_MADC_RT_TRIG_FALLING in case hardware trigger
 *			is used (TRIG_RT and TRIG_HYBRID).
 * @delay:		HW-triggered conversion only. This member specifies
 *			the delay between triggering event and the conversion
 *			of first channel in nanoseconds. The value is rounded
 *			up to the next possible delay supported by hardware.
 *			If a delay longer than possible is requested, maximum
 *			supported value is used.
 * @rbuf:		Pointer to array for converted values. The array must
 *			have at least (highest numbered channel converted + 1)
 *			elements. Channel numbers are used to index the array.
 * @func_cb:		Callback function to be called after conversion.
 *			First argument indicates success (0) or error code.
 * @user_data:		Used as second argument to callback function.
 *
 * This function performs a madc conversion for one or more channels using
 * TWL4030 madc. When the conversion is complete or has irrevocably failed,
 * the callback function is called. The first argument of the callback is
 * the status of completed operation: 0 indicates success, otherwise -errno.
 * The second argument to the callback is the pointer returned by this
 * function.
 *
 * This function may block execution and cannot be used in any context where
 * sleeping is not possible.
 *
 * On success the function returns pointer to internal structure used by the
 * driver, which may be used to call twl4030_madc_cancel_request.
 * IS_ERR may be used to test for error condition.
 */
struct twl4030_madc_request *
    twl4030_madc_start_conversion_ex(u16 channel_mask, u16 average_mask,
				     u16 flags, u16 delay,
				     u16 *rbuf,
				     void (*func_cb)(int,
					   struct twl4030_madc_request *),
				     void *user_data);

/**
 * twl4030_madc_get_user_data - get user data from madc request struct
 * @req:                Pointer to madc request
 *
 */

void *twl4030_madc_get_user_data(struct twl4030_madc_request *req);

#endif
