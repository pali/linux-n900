/*
 *  cs-ssi.h
 *
 * Part of the CMT speech driver.
 *
 * Copyright (C) 2008,2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Peter Ujfalusi <peter.ujfalusi@nokia.com>
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
 */


#ifndef _CS_SSI_H
#define _CS_SSI_H

int cs_ssi_init(void);
int cs_ssi_exit(void);

int cs_ssi_start(unsigned long mmap_base, unsigned long mmap_size);
void cs_ssi_stop(void);
int cs_ssi_buf_config(struct cs_buffer_config *buf_cfg);
void cs_ssi_set_wakeline(unsigned int new_state);
unsigned int cs_ssi_get_state(void);
int cs_ssi_command(u32 cmd);

#endif /* _CS_SSI_H */
