#ifndef _PVRCONFIG_H
#define _PVRCONFIG_H

#define SGX530				1
#define SGX_CORE_REV			121

#ifdef CONFIG_PVR_DEBUG
# define PVR_BUILD_TYPE			"debug"
# define DEBUG				1
#elif defined(CONFIG_PVR_TIMING)
# define PVR_BUILD_TYPE			"timing"
# define TIMING				1
#elif defined(CONFIG_PVR_RELEASE)
# define PVR_BUILD_TYPE			"release"
#endif

#ifdef DEBUG
# define DEBUG_LINUX_MEMORY_ALLOCATIONS	1
# define DEBUG_LINUX_MEM_AREAS		1
# define DEBUG_LINUX_MMAP_AREAS		1
# define DEBUG_BRIDGE_KM		1
#endif

#endif
