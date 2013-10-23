#
# Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
# 
# This program is distributed in the hope it will be useful but, except 
# as otherwise stated in writing, without any warranty; without even the 
# implied warranty of merchantability or fitness for a particular purpose. 
# See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
# 
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Contact Information:
# Imagination Technologies Ltd. <gpl-support@imgtec.com>
# Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
# 
#

include $(EURASIAROOT)/eurasiacon/build/linux/kbuild/Makefile.kbuild_subdir_common

MODULE		= pvrsrvkm

KBUILDROOT 	= ../../../..

INCLUDES = 	-I$(EURASIAROOT)/include4 \
			-I$(EURASIAROOT)/services4/include \
			-I$(EURASIAROOT)/services4/srvkm/env/linux \
			-I$(EURASIAROOT)/services4/srvkm/include \
			-I$(EURASIAROOT)/services4/srvkm/bridged \
			-I$(EURASIAROOT)/services4/srvkm/devices/sgx \
			-I$(EURASIAROOT)/services4/system/$(PVR_SYSTEM) \
			-I$(EURASIAROOT)/services4/system/include 


SOURCES	      = $(KBUILDROOT)/srvkm/env/linux/osfunc.c \
				$(KBUILDROOT)/srvkm/env/linux/mmap.c \
				$(KBUILDROOT)/srvkm/env/linux/module.c \
				$(KBUILDROOT)/srvkm/env/linux/pdump.c \
				$(KBUILDROOT)/srvkm/env/linux/proc.c \
				$(KBUILDROOT)/srvkm/env/linux/pvr_bridge_k.c \
				$(KBUILDROOT)/srvkm/env/linux/pvr_debug.c \
				$(KBUILDROOT)/srvkm/env/linux/mm.c \
				$(KBUILDROOT)/srvkm/env/linux/mutex.c \
				$(KBUILDROOT)/srvkm/env/linux/event.c

SOURCES	     += $(KBUILDROOT)/srvkm/common/buffer_manager.c \
				$(KBUILDROOT)/srvkm/common/devicemem.c \
				$(KBUILDROOT)/srvkm/common/deviceclass.c \
				$(KBUILDROOT)/srvkm/common/handle.c \
				$(KBUILDROOT)/srvkm/common/hash.c \
				$(KBUILDROOT)/srvkm/common/metrics.c \
				$(KBUILDROOT)/srvkm/common/pvrsrv.c \
				$(KBUILDROOT)/srvkm/common/queue.c \
				$(KBUILDROOT)/srvkm/common/ra.c \
				$(KBUILDROOT)/srvkm/common/resman.c \
				$(KBUILDROOT)/srvkm/common/power.c \
				$(KBUILDROOT)/srvkm/common/mem.c \
				$(KBUILDROOT)/srvkm/bridged/bridged_pvr_bridge.c \
				$(KBUILDROOT)/srvkm/devices/sgx/sgxinit.c \
				$(KBUILDROOT)/srvkm/devices/sgx/sgxreset.c \
				$(KBUILDROOT)/srvkm/devices/sgx/sgxutils.c \
				$(KBUILDROOT)/srvkm/devices/sgx/sgxkick.c \
				$(KBUILDROOT)/srvkm/devices/sgx/sgxtransfer.c \
				$(KBUILDROOT)/srvkm/devices/sgx/mmu.c \
				$(KBUILDROOT)/srvkm/devices/sgx/pb.c \
				$(KBUILDROOT)/srvkm/common/perproc.c \
				$(KBUILDROOT)/../services4/system/$(PVR_SYSTEM)/sysconfig.c \
				$(KBUILDROOT)/../services4/system/$(PVR_SYSTEM)/sysutils.c


INCLUDES += -I$(EURASIAROOT)/services4/srvkm/hwdefs 

ifeq ($(PVR_MODULE_TEST),1)
INCLUDES += -I$(EURASIAROOT)/moduletests/include

SOURCES	 += $(KBUILDROOT)/../moduletests/services_test/kern_test.c
SYS_CFLAGS += -DMODULE_TEST
endif
