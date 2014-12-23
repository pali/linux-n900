/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.
 * Copyright (C) 2014 Pavel Machek <pavel@ucw.cz>
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
#include <linux/clk.h>

#include <linux/io.h>

#include "nokia_h4p.h"

int h4p_wait_for_cts(struct h4p_info *info, bool active, int timeout_ms)
{
	unsigned long timeout;
	int state;

	timeout = jiffies + msecs_to_jiffies(timeout_ms);
	for (;;) {
		state = h4p_inb(info, UART_MSR) & UART_MSR_CTS;
		if (active == !!state)
			return 0;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		msleep(1);
	}
}

void __h4p_set_auto_ctsrts(struct h4p_info *info, bool on, u8 which)
{
	u8 lcr, b;

	lcr = h4p_inb(info, UART_LCR);
	h4p_outb(info, UART_LCR, 0xbf);
	b = h4p_inb(info, UART_EFR);
	if (on)
		b |= which;
	else
		b &= ~which;
	h4p_outb(info, UART_EFR, b);
	h4p_outb(info, UART_LCR, lcr);
}

void h4p_set_auto_ctsrts(struct h4p_info *info, bool on, u8 which)
{
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	__h4p_set_auto_ctsrts(info, on, which);
	spin_unlock_irqrestore(&info->lock, flags);
}

void h4p_change_speed(struct h4p_info *info, unsigned long speed)
{
	unsigned int divisor;
	u8 lcr, mdr1;

	BT_DBG("Setting speed %lu", speed);

	if (speed >= 460800) {
		divisor = UART_CLOCK / 13 / speed;
		mdr1 = 3;
	} else {
		divisor = UART_CLOCK / 16 / speed;
		mdr1 = 0;
	}

	/* Make sure UART mode is disabled */
	h4p_outb(info, UART_OMAP_MDR1, 7);

	lcr = h4p_inb(info, UART_LCR);
	h4p_outb(info, UART_LCR, UART_LCR_DLAB);     /* Set DLAB */
	h4p_outb(info, UART_DLL, divisor & 0xff);    /* Set speed */
	h4p_outb(info, UART_DLM, divisor >> 8);
	h4p_outb(info, UART_LCR, lcr);

	/* Make sure UART mode is enabled */
	h4p_outb(info, UART_OMAP_MDR1, mdr1);
}

int h4p_reset_uart(struct h4p_info *info)
{
	int count = 0;

	/* Reset the UART */
	h4p_outb(info, UART_OMAP_SYSC, UART_SYSC_OMAP_RESET);
	while (!(h4p_inb(info, UART_OMAP_SYSS) & UART_SYSS_RESETDONE)) {
		if (count++ > 100) {
			dev_err(info->dev, "nokia_h4p: UART reset timeout\n");
			return -ENODEV;
		}
		udelay(1);
	}

	return 0;
}

void h4p_store_regs(struct h4p_info *info)
{
	u16 lcr = 0;

	lcr = h4p_inb(info, UART_LCR);
	h4p_outb(info, UART_LCR, 0xBF);
	info->dll = h4p_inb(info, UART_DLL);
	info->dlh = h4p_inb(info, UART_DLM);
	info->efr = h4p_inb(info, UART_EFR);
	h4p_outb(info, UART_LCR, lcr);
	info->mdr1 = h4p_inb(info, UART_OMAP_MDR1);
	info->ier = h4p_inb(info, UART_IER);
}

void h4p_restore_regs(struct h4p_info *info)
{
	u16 lcr = 0;

	h4p_init_uart(info);

	h4p_outb(info, UART_OMAP_MDR1, 7);
	lcr = h4p_inb(info, UART_LCR);
	h4p_outb(info, UART_LCR, 0xBF);
	h4p_outb(info, UART_DLL, info->dll);    /* Set speed */
	h4p_outb(info, UART_DLM, info->dlh);
	h4p_outb(info, UART_EFR, info->efr);
	h4p_outb(info, UART_LCR, lcr);
	h4p_outb(info, UART_OMAP_MDR1, info->mdr1);
	h4p_outb(info, UART_IER, info->ier);
}

void h4p_init_uart(struct h4p_info *info)
{
	u8 mcr, efr;

	/* Enable and setup FIFO */
	h4p_outb(info, UART_OMAP_MDR1, 0x00);

	h4p_outb(info, UART_LCR, 0xbf);
	efr = h4p_inb(info, UART_EFR);
	h4p_outb(info, UART_EFR, UART_EFR_ECB);
	h4p_outb(info, UART_LCR, UART_LCR_DLAB);
	mcr = h4p_inb(info, UART_MCR);
	h4p_outb(info, UART_MCR, UART_MCR_TCRTLR);
	h4p_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO |
			UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT |
			(3 << 6) | (0 << 4));
	h4p_outb(info, UART_LCR, 0xbf);
	h4p_outb(info, UART_TI752_TLR, 0xed);
	h4p_outb(info, UART_TI752_TCR, 0xef);
	h4p_outb(info, UART_EFR, efr);
	h4p_outb(info, UART_LCR, UART_LCR_DLAB);
	h4p_outb(info, UART_MCR, 0x00);
	h4p_outb(info, UART_LCR, UART_LCR_WLEN8);
	h4p_outb(info, UART_IER, UART_IER_RDI);
	h4p_outb(info, UART_OMAP_SYSC, (1 << 0) | (1 << 2) | (2 << 3));
}
