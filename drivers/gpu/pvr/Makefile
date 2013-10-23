PVR_SYSTEM	:=	omap3430

EXTRA_CFLAGS += -D__linux__

obj-$(CONFIG_PVR)	+=	\
	omaplfb.o \
	pvrsrvkm.o

omaplfb-objs	:=	\
	services4/3rdparty/dc_omap3430_linux/omaplfb_displayclass.o \
	services4/3rdparty/dc_omap3430_linux/omaplfb_linux.o

pvrsrvkm-objs	:=	\
	services4/srvkm/env/linux/osfunc.o \
	services4/srvkm/env/linux/mmap.o \
	services4/srvkm/env/linux/module.o \
	services4/srvkm/env/linux/pdump.o \
	services4/srvkm/env/linux/proc.o \
	services4/srvkm/env/linux/pvr_bridge_k.o \
	services4/srvkm/env/linux/pvr_debug.o \
	services4/srvkm/env/linux/mm.o \
	services4/srvkm/env/linux/mutex.o \
	services4/srvkm/env/linux/event.o \
	services4/srvkm/common/buffer_manager.o \
	services4/srvkm/common/devicemem.o \
	services4/srvkm/common/deviceclass.o \
	services4/srvkm/common/handle.o \
	services4/srvkm/common/hash.o \
	services4/srvkm/common/metrics.o \
	services4/srvkm/common/pvrsrv.o \
	services4/srvkm/common/queue.o \
	services4/srvkm/common/ra.o \
	services4/srvkm/common/resman.o \
	services4/srvkm/common/power.o \
	services4/srvkm/common/mem.o \
	services4/srvkm/bridged/bridged_pvr_bridge.o \
	services4/srvkm/devices/sgx/sgxinit.o \
	services4/srvkm/devices/sgx/sgxreset.o \
	services4/srvkm/devices/sgx/sgxutils.o \
	services4/srvkm/devices/sgx/sgxkick.o \
	services4/srvkm/devices/sgx/sgxtransfer.o \
	services4/srvkm/devices/sgx/mmu.o \
	services4/srvkm/devices/sgx/pb.o \
	services4/srvkm/common/perproc.o \
	services4/system/$(PVR_SYSTEM)/sysconfig.o \
	services4/system/$(PVR_SYSTEM)/sysutils.o

ccflags-y	:=	\
	-I$(src)/include4 \
	-I$(src)/services4/include \
	-I$(src)/services4/srvkm/env/linux \
	-I$(src)/services4/srvkm/include \
	-I$(src)/services4/srvkm/bridged \
	-I$(src)/services4/srvkm/devices/sgx \
	-I$(src)/services4/system/$(PVR_SYSTEM) \
	-I$(src)/services4/system/include \
	-I$(src)/services4/srvkm/hwdefs \
	-Iarch/arm/plat-omap/include/mach

# Work out our origins.  eurasiacon.pj is not supplied to end-users
# so instead we supply pvrversion.h.  We produce pvrversion.h if
# if doesn't exist.
#
DATE			:= $(shell date "+%a %B %d %Z %Y" )

SGXCORE = 530
CORE = -DSGX530 -DSUPPORT_SGX530 -DSGX_CORE_REV=121

# --------------------------------------------------------------------------------

SUPPORT_SGX = 1
SUPPORT_HW_RECOVERY = 1
SUPPORT_SGX_HWPERF = 1
SYS_USING_INTERRUPTS = 1

PVR2D_ALT_2DHW = 1

LDM_PLATFORM ?= 1

# Only enable active power management if passive power management is
# enabled, as indicated by LDM_PLATFORM being set to 1.  On OMAP,
# the system can suspend in the case where active power management is
# enabled in the SGX driver, but passive power management isn't. As
# passive power management isn't enabled, the driver won't see the
# system suspend/resume events, and so won't take appropriate action.
ifeq ($(LDM_PLATFORM),1)
SUPPORT_ACTIVE_POWER_MANAGEMENT ?= 1
else
SUPPORT_ACTIVE_POWER_MANAGEMENT = 0
endif

BUILD                  ?= release
# The standard CFLAGS macro can be overridden on the 'make' command line.  We
# put CBUILD in a separate macro so its setting doesn't get lost when a user
# *does* override CFLAGS.
#

CBUILD			=	-DPVR_BUILD_DIR="\"$(PVR_BUILD_DIR)\"" \
					-DPVR_BUILD_DATE="\"$(DATE)\"" \
					-DPVR_BUILD_TYPE="\"$(BUILD)\""

# OPTIM contains the optimisation level in timing and release builds
OPTIM			= -O2

#  Don't support HW recovery on debug builds
CBUILD.debug	= -DDEBUG
CBUILD.timing	= -DTIMING
CBUILD.release	= -DRELEASE
_CFLAGS.debug	= -g -O0 -DDLL_METRIC=1
_CFLAGS.timing	= $(OPTIM) -g -DDLL_METRIC=1 -DTIMING
_CFLAGS.release	= $(OPTIM) -g
_CFLAGS          = $(_CFLAGS.$(BUILD)) -DSGX_DYNAMIC_TIMING_INFO

# Defaults for useful things.
#

ifeq ("$(BUILD)", "debug")
DEBUG_LINUX_MEMORY_ALLOCATIONS ?= 1
DEBUG_LINUX_MEM_AREAS ?= 1
DEBUG_LINUX_MMAP_AREAS ?= 1
DEBUG_LINUX_XML_PROC_FILES ?= 0
DEBUG_LINUX_SLAB_ALLOCATIONS ?= 0
DEBUG_BRIDGE_KM ?= 1
DEBUG_TRACE_BRIDGE_KM ?= 0
DEBUG_BRIDGE_KM_DISPATCH_TABLE ?= 0
endif

TRANSFER_QUEUE ?= 1
SUPPORT_SGX_EVENT_OBJECT ?=1
SUPPORT_SECURE_HANDLES		?= 1
SUPPORT_SRVINIT = 1
SUPPORT_PERCONTEXT_PB = 1

SYS_CFLAGS += -DSERVICES4 -D_XOPEN_SOURCE=600 -DPVR2D_VALIDATE_INPUT_PARAMS

# Thread support
USE_PTHREADS ?= 1
USE_GCC__thread_KEYWORD ?= 0
OPTIMISE_NON_NPTL_SINGLE_THREAD_TLS_LOOKUP ?= 0
DISABLE_THREADS ?= 0

# Automatically define C compiler macros for features possible (or not) in use.

SYS_CFLAGS.$(SUPPORT_SRVINIT)				+= -DSUPPORT_SRVINIT

SYS_CFLAGS.$(SUPPORT_SGX)					+= -DSUPPORT_SGX
SYS_CFLAGS.$(SUPPORT_XWS)					+= -DSUPPORT_XWS
SYS_CFLAGS.$(PDUMP)							+= -DPDUMP
SYS_CFLAGS.$(SUPPORT_POWER_MANAGEMENT)		+= -DSUPPORT_POWER_MANAGEMENT
SYS_CFLAGS.$(SUPPORT_BUFFER_CLASS)			+= -DSUPPORT_BUFFER_CLASS

SYS_CFLAGS.$(SUPPORT_PERCONTEXT_PB)			+= -DSUPPORT_PERCONTEXT_PB 
SYS_CFLAGS.$(SUPPORT_DYNAMIC_PBRESIZE)		+= -DSUPPORT_DYNAMIC_PBRESIZE

SYS_CFLAGS.$(USE_FBDEV)						+= -DUSE_FBDEV
SYS_CFLAGS.$(USE_FBDEV)						+= -DFBDEV_NAME="\"$(FBDEV_NAME)\""
SYS_CFLAGS.$(SUPPORT_DYNAMIC_3DCLOCKGATING) += -DSUPPORT_DYNAMIC_3DCLOCKGATING
SYS_CFLAGS.$(REENTRANCY_PROTECTION)			+= -DREENTRANCY_PROTECTION
SYS_CFLAGS.$(SCHEDULER_CONTROL_SUPPORT) 	+= -DSCHEDULER_CONTROL_SUPPORT
SYS_CFLAGS.$(USE_IMG_POWER_DOMAIN_FUNCTION) += -DUSE_IMG_POWER_DOMAIN_FUNCTION

SYS_CFLAGS.$(USE_DMALLOC)					+= -DDMALLOC

SYS_CFLAGS.$(DEBUG_LINUX_MEMORY_ALLOCATIONS)	+= -DDEBUG_LINUX_MEMORY_ALLOCATIONS
SYS_CFLAGS.$(DEBUG_LINUX_MEM_AREAS)				+= -DDEBUG_LINUX_MEM_AREAS
SYS_CFLAGS.$(DEBUG_LINUX_MMAP_AREAS)			+= -DDEBUG_LINUX_MMAP_AREAS
SYS_CFLAGS.$(DEBUG_LINUX_XML_PROC_FILES)		+= -DDEBUG_LINUX_XML_PROC_FILES
SYS_CFLAGS.$(DEBUG_LINUX_SLAB_ALLOCATIONS)		+= -DDEBUG_LINUX_SLAB_ALLOCATIONS
SYS_CFLAGS.$(DEBUG_BRIDGE_KM)					+= -DDEBUG_BRIDGE_KM
SYS_CFLAGS.$(DEBUG_TRACE_BRIDGE_KM)				+= -DDEBUG_TRACE_BRIDGE_KM
SYS_CFLAGS.$(DEBUG_BRIDGE_KM_DISPATCH_TABLE)	+= -DDEBUG_BRIDGE_KM_DISPATCH_TABLE

SYS_CFLAGS.$(SUPPORT_LINUX_X86_WRITECOMBINE)	+= -DSUPPORT_LINUX_X86_WRITECOMBINE

SYS_CFLAGS.$(SGX_PDS_EVENTS_DISABLED)			+= -DSGX_PDS_EVENTS_DISABLED
SYS_CFLAGS.$(USE_SUPPORT_NO_TA3D_OVERLAP)		+= -DUSE_SUPPORT_NO_TA3D_OVERLAP
SYS_CFLAGS.$(SUPPORT_SGX_TILING)				+= -DSUPPORT_SGX_TILING
SYS_CFLAGS.$(TRANSFER_QUEUE)					+= -DTRANSFER_QUEUE

SYS_CFLAGS.$(SUPPORT_SGX_MMU_DUMMY_PAGE)		+= -DSUPPORT_SGX_MMU_DUMMY_PAGE

SYS_CFLAGS.$(PVRSRV_USSE_EDM_STATUS_DEBUG)		+= -DPVRSRV_USSE_EDM_STATUS_DEBUG

SYS_CFLAGS.$(NO_HARDWARE)				+= -DNO_HARDWARE

SYS_CFLAGS.$(SUPPORT_DRI_DRM)				+= -DSUPPORT_DRI_DRM

ifneq ("$(NO_HARDWARE)", "1")
SYS_CFLAGS.$(SYS_USING_INTERRUPTS)		+= -DSYS_USING_INTERRUPTS
SYS_CFLAGS.$(SUPPORT_HW_RECOVERY)		+= -DSUPPORT_HW_RECOVERY
SYS_CFLAGS.$(SUPPORT_ACTIVE_POWER_MANAGEMENT)	+= -DSUPPORT_ACTIVE_POWER_MANAGEMENT
endif

SYS_CFLAGS.$(SUPPORT_SECURE_HANDLES)	+= -DPVR_SECURE_HANDLES

SYS_CFLAGS.$(USE_PTHREADS)				+= -DUSE_PTHREADS
SYS_CFLAGS.$(USE_GCC__thread_KEYWORD)	+= -DUSE_GCC__thread_KEYWORD
SYS_CFLAGS.$(OPTIMISE_NON_NPTL_SINGLE_THREAD_TLS_LOOKUP) += -DOPTIMISE_NON_NPTL_SINGLE_THREAD_TLS_LOOKUP
SYS_CFLAGS.$(DISABLE_THREADS)			+= -DDISABLE_THREADS
SYS_CFLAGS.$(SUPPORT_SGX_EVENT_OBJECT)			+= -DSUPPORT_SGX_EVENT_OBJECT
SYS_CFLAGS.$(LDM_PLATFORM)			+= -DLDM_PLATFORM
SYS_CFLAGS.$(LDM_PCI)				+= -DLDM_PCI
SYS_CFLAGS.$(PVR_MANUAL_POWER_CONTROL)		+= -DPVR_MANUAL_POWER_CONTROL

SYS_CFLAGS.$(PVR2D_ALT_2DHW)			+= -DPVR2D_ALT_2DHW

SYS_CFLAGS.$(SUPPORT_SGX_HWPERF)		+= -DSUPPORT_SGX_HWPERF

SYS_INCLUDES	=	-I$(EURASIAROOT)/include4 \
					-I$(EURASIAROOT)/eurasiacon/includeext \
					-isystem $(KERNELDIR)/include

ALL_CFLAGS_kbuild	=	-DLINUX \
						$(CBUILD) $(CBUILD.$(BUILD)) \
						$(SYS_CFLAGS) $(SYS_CFLAGS.1) \
						$(MODULE_CFLAGS) $(MODULE_CFLAGS.$(BUILD)) \
						$(CORE) \
						-Wall -fno-strict-aliasing \
						$(_CFLAGS)

ccflags-y += $(ALL_CFLAGS_kbuild)
