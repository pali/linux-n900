/*
 * drv.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DRV Resource allocation module. Driver Object gets Created
 * at the time of Loading. It holds the List of Device Objects
 * in the system.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DRV_
#define DRV_

#include <dspbridge/dbdefs.h>
#include <dspbridge/devdefs.h>
#include <dspbridge/drvdefs.h>

#define DRV_ASSIGN     1
#define DRV_RELEASE    0

/* Provide the DSP Internal memory windows that can be accessed from L3 address
 * space */

#define OMAP_GEM_BASE   0x107F8000
#define OMAP_DSP_SIZE   0x00720000

/* MEM1 is L2 RAM + L2 Cache space */
#define OMAP_DSP_MEM1_BASE 0x5C7F8000
#define OMAP_DSP_MEM1_SIZE 0x18000
#define OMAP_DSP_GEM1_BASE 0x107F8000

/* MEM2 is L1P RAM/CACHE space */
#define OMAP_DSP_MEM2_BASE 0x5CE00000
#define OMAP_DSP_MEM2_SIZE 0x8000
#define OMAP_DSP_GEM2_BASE 0x10E00000

/* MEM3 is L1D RAM/CACHE space */
#define OMAP_DSP_MEM3_BASE 0x5CF04000
#define OMAP_DSP_MEM3_SIZE 0x14000
#define OMAP_DSP_GEM3_BASE 0x10F04000

#define OMAP_IVA2_PRM_BASE 0x48306000
#define OMAP_IVA2_PRM_SIZE 0x1000

#define OMAP_IVA2_CM_BASE 0x48004000
#define OMAP_IVA2_CM_SIZE 0x1000

#define OMAP_PER_CM_BASE 0x48005000
#define OMAP_PER_CM_SIZE 0x1000

#define OMAP_PER_PRM_BASE 0x48307000
#define OMAP_PER_PRM_SIZE 0x1000

#define OMAP_CORE_PRM_BASE 0x48306A00
#define OMAP_CORE_PRM_SIZE 0x1000

#define OMAP_SYSC_BASE 0x48002000
#define OMAP_SYSC_SIZE 0x1000

#define OMAP_DMMU_BASE 0x5D000000
#define OMAP_DMMU_SIZE 0x1000

#define OMAP_PRCM_VDD1_DOMAIN 1
#define OMAP_PRCM_VDD2_DOMAIN 2

/* GPP PROCESS CLEANUP Data structures */

/* New structure (member of process context) abstracts NODE resource info */
struct node_res_object {
	void *hnode;
	s32 node_allocated;	/* Node status */
	s32 heap_allocated;	/* Heap status */
	s32 streams_allocated;	/* Streams status */
	struct node_res_object *next;
};

/* Used for DMM mapped memory accounting */
struct dmm_map_object {
	struct list_head link;
	u32 dsp_addr;
};

/* Used for DMM reserved memory accounting */
struct dmm_rsv_object {
	struct list_head link;
	u32 dsp_reserved_addr;
};

/* New structure (member of process context) abstracts DMM resource info */
struct dspheap_res_object {
	s32 heap_allocated;	/* DMM status */
	u32 ul_mpu_addr;
	u32 ul_dsp_addr;
	u32 ul_dsp_res_addr;
	u32 heap_size;
	bhandle hprocessor;
	struct dspheap_res_object *next;
};

/* New structure (member of process context) abstracts stream resource info */
struct strm_res_object {
	s32 stream_allocated;	/* Stream status */
	void *hstream;
	u32 num_bufs;
	u32 dir;
	struct strm_res_object *next;
};

/* Overall Bridge process resource usage state */
enum gpp_proc_res_state {
	PROC_RES_ALLOCATED,
	PROC_RES_FREED
};

/* Process Context */
struct process_context {
	/* Process State */
	enum gpp_proc_res_state res_state;

	/* Handle to Processor */
	void *hprocessor;

	/* DSP Node resources */
	struct node_res_object *node_list;
	struct mutex node_mutex;

	/* DMM mapped memory resources */
	struct list_head dmm_map_list;
	spinlock_t dmm_map_lock;

	/* DMM reserved memory resources */
	struct list_head dmm_rsv_list;
	spinlock_t dmm_rsv_lock;

	/* DSP Heap resources */
	struct dspheap_res_object *pdspheap_list;

	/* Stream resources */
	struct strm_res_object *pstrm_list;
	struct mutex strm_mutex;

	/* Policy handle */
	void *policy;
};

/*
 *  ======== drv_create ========
 *  Purpose:
 *      Creates the Driver Object. This is done during the driver loading.
 *      There is only one Driver Object in the DSP/BIOS Bridge.
 *  Parameters:
 *      phDrvObject:    Location to store created DRV Object handle.
 *  Returns:
 *      DSP_SOK:        Sucess
 *      DSP_EMEMORY:    Failed in Memory allocation
 *      DSP_EFAIL:      General Failure
 *  Requires:
 *      DRV Initialized (refs > 0 )
 *      phDrvObject != NULL.
 *  Ensures:
 *      DSP_SOK:        - *phDrvObject is a valid DRV interface to the device.
 *                      - List of DevObject Created and Initialized.
 *                      - List of dev_node String created and intialized.
 *                      - Registry is updated with the DRV Object.
 *      !DSP_SOK:       DRV Object not created
 *  Details:
 *      There is one Driver Object for the Driver representing
 *      the driver itself. It contains the list of device
 *      Objects and the list of Device Extensions in the system.
 *      Also it can hold other neccessary
 *      information in its storage area.
 */
extern dsp_status drv_create(struct drv_object **phDrvObject);

/*
 *  ======== drv_destroy ========
 *  Purpose:
 *      destroys the Dev Object list, DrvExt list
 *      and destroy the DRV object
 *      Called upon driver unLoading.or unsuccesful loading of the driver.
 *  Parameters:
 *      hdrv_obj:     Handle to Driver object .
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Failed to destroy DRV Object
 *  Requires:
 *      DRV Initialized (cRegs > 0 )
 *      hdrv_obj is not NULL and a valid DRV handle .
 *      List of DevObject is Empty.
 *      List of DrvExt is Empty
 *  Ensures:
 *      DSP_SOK:        - DRV Object destroyed and hdrv_obj is not a valid
 *                        DRV handle.
 *                      - Registry is updated with "0" as the DRV Object.
 */
extern dsp_status drv_destroy(struct drv_object *hdrv_obj);

/*
 *  ======== drv_exit ========
 *  Purpose:
 *      Exit the DRV module, freeing any modules initialized in drv_init.
 *  Parameters:
 *  Returns:
 *  Requires:
 *  Ensures:
 */
extern void drv_exit(void);

/*
 *  ======== drv_get_first_dev_object ========
 *  Purpose:
 *      Returns the Ptr to the FirstDev Object in the List
 *  Parameters:
 *  Requires:
 *      DRV Initialized
 *  Returns:
 *      dw_dev_object:  Ptr to the First Dev Object as a u32
 *      0 if it fails to retrieve the First Dev Object
 *  Ensures:
 */
extern u32 drv_get_first_dev_object(void);

/*
 *  ======== drv_get_first_dev_extension ========
 *  Purpose:
 *      Returns the Ptr to the First Device Extension in the List
 *  Parameters:
 *  Requires:
 *      DRV Initialized
 *  Returns:
 *      dw_dev_extension:     Ptr to the First Device Extension as a u32
 *      0:                  Failed to Get the Device Extension
 *  Ensures:
 */
extern u32 drv_get_first_dev_extension(void);

/*
 *  ======== drv_get_dev_object ========
 *  Purpose:
 *      Given a index, returns a handle to DevObject from the list
 *  Parameters:
 *      hdrv_obj:     Handle to the Manager
 *      phDevObject:    Location to store the Dev Handle
 *  Requires:
 *      DRV Initialized
 *      index >= 0
 *      hdrv_obj is not NULL and Valid DRV Object
 *      phDevObject is not NULL
 *      Device Object List not Empty
 *  Returns:
 *      DSP_SOK:        Success
 *      DSP_EFAIL:      Failed to Get the Dev Object
 *  Ensures:
 *      DSP_SOK:        *phDevObject != NULL
 *      DSP_EFAIL:      *phDevObject = NULL
 */
extern dsp_status drv_get_dev_object(u32 index,
				     struct drv_object *hdrv_obj,
				     struct dev_object **phDevObject);

/*
 *  ======== drv_get_next_dev_object ========
 *  Purpose:
 *      Returns the Ptr to the Next Device Object from the the List
 *  Parameters:
 *      hdev_obj:     Handle to the Device Object
 *  Requires:
 *      DRV Initialized
 *      hdev_obj != 0
 *  Returns:
 *      dw_dev_object:    Ptr to the Next Dev Object as a u32
 *      0:              If it fail to get the next Dev Object.
 *  Ensures:
 */
extern u32 drv_get_next_dev_object(u32 hdev_obj);

/*
 *  ======== drv_get_next_dev_extension ========
 *  Purpose:
 *      Returns the Ptr to the Next Device Extension from the the List
 *  Parameters:
 *      hDevExtension:      Handle to the Device Extension
 *  Requires:
 *      DRV Initialized
 *      hDevExtension != 0.
 *  Returns:
 *      dw_dev_extension:     Ptr to the Next Dev Extension
 *      0:                  If it fail to Get the next Dev Extension
 *  Ensures:
 */
extern u32 drv_get_next_dev_extension(u32 hDevExtension);

/*
 *  ======== drv_init ========
 *  Purpose:
 *      Initialize the DRV module.
 *  Parameters:
 *  Returns:
 *      TRUE if success; FALSE otherwise.
 *  Requires:
 *  Ensures:
 */
extern dsp_status drv_init(void);

/*
 *  ======== drv_insert_dev_object ========
 *  Purpose:
 *      Insert a DeviceObject into the list of Driver object.
 *  Parameters:
 *      hdrv_obj:     Handle to DrvObject
 *      hdev_obj:     Handle to DeviceObject to insert.
 *  Returns:
 *      DSP_SOK:        If successful.
 *      DSP_EFAIL:      General Failure:
 *  Requires:
 *      hdrv_obj != NULL and Valid DRV Handle.
 *      hdev_obj != NULL.
 *  Ensures:
 *      DSP_SOK:        Device Object is inserted and the List is not empty.
 */
extern dsp_status drv_insert_dev_object(struct drv_object *hdrv_obj,
					struct dev_object *hdev_obj);

/*
 *  ======== drv_remove_dev_object ========
 *  Purpose:
 *      Search for and remove a Device object from the given list of Device Obj
 *      objects.
 *  Parameters:
 *      hdrv_obj:     Handle to DrvObject
 *      hdev_obj:     Handle to DevObject to Remove
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Unable to find dev_obj.
 *  Requires:
 *      hdrv_obj != NULL and a Valid DRV Handle.
 *      hdev_obj != NULL.
 *      List exists and is not empty.
 *  Ensures:
 *      List either does not exist (NULL), or is not empty if it does exist.
 */
extern dsp_status drv_remove_dev_object(struct drv_object *hdrv_obj,
					struct dev_object *hdev_obj);

/*
 *  ======== drv_request_resources ========
 *  Purpose:
 *      Assigns the Resources or Releases them.
 *  Parameters:
 *      dw_context:          Path to the driver Registry Key.
 *      pDevNodeString:     Ptr to dev_node String stored in the Device Ext.
 *  Returns:
 *      TRUE if success; FALSE otherwise.
 *  Requires:
 *  Ensures:
 *      The Resources are assigned based on Bus type.
 *      The hardware is initialized. Resource information is
 *      gathered from the Registry(ISA, PCMCIA)or scanned(PCI)
 *      Resource structure is stored in the registry which will be
 *      later used by the CFG module.
 */
extern dsp_status drv_request_resources(IN u32 dw_context,
					OUT u32 *pDevNodeString);

/*
 *  ======== drv_release_resources ========
 *  Purpose:
 *      Assigns the Resources or Releases them.
 *  Parameters:
 *      dw_context:      Path to the driver Registry Key.
 *      hdrv_obj:     Handle to the Driver Object.
 *  Returns:
 *      TRUE if success; FALSE otherwise.
 *  Requires:
 *  Ensures:
 *      The Resources are released based on Bus type.
 *      Resource structure is deleted from the registry
 */
extern dsp_status drv_release_resources(IN u32 dw_context,
					struct drv_object *hdrv_obj);

#ifdef CONFIG_BRIDGE_RECOVERY
void bridge_recover_schedule(void);
#endif

#endif /* DRV_ */
