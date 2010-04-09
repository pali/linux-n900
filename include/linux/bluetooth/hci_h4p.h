/*
 * This file is part of hci_h4p bluetooth driver
 *
 * Copyright (C) 2010 Nokia Corporation.
 *
 * Contact: Roger Quadros <roger.quadros@nokia.com>
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


/**
 * struct hci_h4p_platform data - hci_h4p Platform data structure
 * @uart_base:	UART base address
 * @uart_irq:	UART Interrupt number
 * @host_wu:	Function hook determine if Host should wakeup or not.
 * @bt_wu:	Function hook to enable/disable Bluetooth transmission
 * @reset:	Function hook to set/clear reset conditiona
 * @host_wu_gpio:	Gpio used to wakeup host
 */
struct hci_h4p_platform_data {
	void *uart_base;
	unsigned int uart_irq;
	bool (*host_wu)(void);
	void (*bt_wu)(bool);
	void (*reset)(bool);
	unsigned int host_wu_gpio;
};
