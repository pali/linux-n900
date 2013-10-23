#ifndef _RX51_H_
#define _RX51_H_

/*
 *  rx51.h - SoC audio for Nokia RX51
 *
 *  Copyright (C) 2008 Nokia Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

int rx51_set_eci_mode(int mode);
void rx51_jack_report(int status);
int allow_button_press(void);

#endif /* _RX51_H_ */
