/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#if defined(LMA)
#include <linux/pci.h>
#else
#include <linux/dma-mapping.h>
#endif

#include "bufferclass_example.h"
#include "bufferclass_example_linux.h"
#include "pvrmodule.h"

#define DEVNAME	"bc_example"
#define	DRVNAME	DEVNAME

MODULE_SUPPORTED_DEVICE(DEVNAME);

int BC_Example_Bridge(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
int FillBuffer(unsigned int ui32BufferIndex);
int GetBufferCount(unsigned int *pui32BufferCount);

static int AssignedMajorNumber;

static struct file_operations bufferclass_example_fops = {
	ioctl:BC_Example_Bridge,
};


#define unref__ __attribute__ ((unused))

#if defined(LMA)
#define PVR_BUFFERCLASS_MEMOFFSET (220 * 1024 * 1024) 
#define PVR_BUFFERCLASS_MEMSIZE	  (4 * 1024 * 1024)	  

unsigned int g_ui32MemBase = 0;
unsigned int g_ui32MemCurrent = 0;

#define VENDOR_ID_PVR					0x1010
#define DEVICE_ID_PVR					0x1CF1


#define PVR_MEM_PCI_BASENUM			2
#endif


static int __init BC_Example_ModInit(void)
{
#if defined(LMA)
	struct pci_dev *psPCIDev;
	int error;
#endif

#if defined(LMA)
	psPCIDev = pci_get_device(VENDOR_ID_PVR, DEVICE_ID_PVR, NULL);
	if (psPCIDev == NULL)
	{
		printk(KERN_ERR DRVNAME ": BC_Example_ModInit:  pci_get_device failed\n");

		goto ExitError;
	}

	if ((error = pci_enable_device(psPCIDev)) != 0)
	{
		printk(KERN_ERR DRVNAME ": BC_Example_ModInit: pci_enable_device failed (%d)\n", error);
		goto ExitError;
	}
#endif

	AssignedMajorNumber = register_chrdev(0, DEVNAME, &bufferclass_example_fops);

	if (AssignedMajorNumber <= 0)
	{
		printk(KERN_ERR DRVNAME ": BC_Example_ModInit: unable to get major number\n");

		goto ExitDisable;
	}

#if defined(DEBUG)
	printk(KERN_ERR DRVNAME ": BC_Example_ModInit: major device %d\n", AssignedMajorNumber);
#endif

#if defined(LMA)
	
	g_ui32MemBase =  pci_resource_start(psPCIDev, PVR_MEM_PCI_BASENUM) + PVR_BUFFERCLASS_MEMOFFSET;
#endif

	if(BC_Example_Init() != PVRSRV_OK)
	{
		printk (KERN_ERR DRVNAME ": BC_Example_ModInit: can't init device\n");
		goto ExitUnregister;
	}

#if defined(LMA)
	
	pci_disable_device(psPCIDev);
#endif

	return 0;

ExitUnregister:
	unregister_chrdev(AssignedMajorNumber, DEVNAME);
ExitDisable:
#if defined(LMA)
	pci_disable_device(psPCIDev);
ExitError:
#endif
	return -EBUSY;
} 

static void __exit BC_Example_ModCleanup(void)
{    
	unregister_chrdev(AssignedMajorNumber, DEVNAME);
	
	if(BC_Example_Deinit() != PVRSRV_OK)
	{
		printk (KERN_ERR DRVNAME ": BC_Example_ModCleanup: can't deinit device\n");
	}

} 


IMG_VOID *BCAllocKernelMem(IMG_UINT32 ui32Size)
{
	return kmalloc(ui32Size, GFP_KERNEL);
}

IMG_VOID BCFreeKernelMem(IMG_VOID *pvMem)
{
	kfree(pvMem);
}

PVRSRV_ERROR BCAllocContigMemory(	IMG_UINT32 ui32Size,
								IMG_HANDLE unref__ *phMemHandle, 
								IMG_CPU_VIRTADDR *pLinAddr, 
								IMG_CPU_PHYADDR *pPhysAddr)
{
#if defined(LMA)
	IMG_VOID *pvLinAddr;
	
	
	if(g_ui32MemCurrent + ui32Size >= PVR_BUFFERCLASS_MEMSIZE)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	pvLinAddr = ioremap(g_ui32MemBase + g_ui32MemCurrent, ui32Size);

	if(pvLinAddr)
	{
		pPhysAddr->uiAddr = g_ui32MemBase + g_ui32MemCurrent;
		*pLinAddr = pvLinAddr;	

		
		g_ui32MemCurrent += ui32Size;
		return PVRSRV_OK;
	}
	return PVRSRV_ERROR_OUT_OF_MEMORY;
#else
	dma_addr_t dma;
	IMG_VOID *pvLinAddr;
	
	pvLinAddr = dma_alloc_coherent(NULL, ui32Size, &dma, GFP_KERNEL);

	if(pvLinAddr == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	pPhysAddr->uiAddr = dma;
	*pLinAddr = pvLinAddr;

	return PVRSRV_OK;
#endif
}

void BCFreeContigMemory(  IMG_UINT32 ui32Size,
					    IMG_HANDLE unref__ hMemHandle, 
						IMG_CPU_VIRTADDR LinAddr, 
						IMG_CPU_PHYADDR PhysAddr)
{
#if defined(LMA)
	g_ui32MemCurrent -= ui32Size;
	iounmap(LinAddr);
#else
	dma_free_coherent(NULL, ui32Size, LinAddr, (dma_addr_t)PhysAddr.uiAddr);
#endif
}

IMG_SYS_PHYADDR CpuPAddrToSysPAddrBC(IMG_CPU_PHYADDR cpu_paddr)
{
	IMG_SYS_PHYADDR sys_paddr;
	
	
	sys_paddr.uiAddr = cpu_paddr.uiAddr;
	return sys_paddr;
}

IMG_CPU_PHYADDR SysPAddrToCpuPAddrBC(IMG_SYS_PHYADDR sys_paddr)
{
	
	IMG_CPU_PHYADDR cpu_paddr;
	
	cpu_paddr.uiAddr = sys_paddr.uiAddr;
	return cpu_paddr;
}

PVRSRV_ERROR BCOpenPVRServices (IMG_HANDLE *phPVRServices)
{
	
	*phPVRServices = 0;
	return PVRSRV_OK;
}


PVRSRV_ERROR BCClosePVRServices (IMG_HANDLE unref__ hPVRServices)
{
	
	return PVRSRV_OK;
}

PVRSRV_ERROR BCGetLibFuncAddr (IMG_HANDLE unref__ hExtDrv, IMG_CHAR *szFunctionName, PFN_BC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetBufferClassJTable", szFunctionName) != 0)
		return PVRSRV_ERROR_INVALID_PARAMS;

	
	*ppfnFuncTable = PVRGetBufferClassJTable;

	return PVRSRV_OK;
}


int BC_Example_Bridge(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = -EFAULT;
	int command = _IOC_NR(cmd);
	BC_Example_ioctl_package *psBridge = (BC_Example_ioctl_package *)arg;

	if(!access_ok(VERIFY_WRITE, psBridge, sizeof(BC_Example_ioctl_package)))
		return err;

	switch(command)
	{
		case _IOC_NR(BC_Example_ioctl_fill_buffer):
		{
			if(FillBuffer(psBridge->inputparam) == -1)
				return err;
			break;
		}
		case _IOC_NR(BC_Example_ioctl_get_buffer_count):
		{	
			if(GetBufferCount(&psBridge->outputparam) == -1)
				return err;
			
			break;
		}
		default:
			return err;
	}

	return 0;
}


module_init(BC_Example_ModInit);
module_exit(BC_Example_ModCleanup);

