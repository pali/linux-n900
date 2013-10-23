/*
 * cfg.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of platform specific config services.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/reg.h>

/*  ----------------------------------- This */
#include <dspbridge/cfg.h>

struct drv_ext {
	struct list_head link;
	char sz_string[MAXREGPATHLENGTH];
};

/*
 *  ======== cfg_exit ========
 *  Purpose:
 *      Discontinue usage of the CFG module.
 */
void cfg_exit(void)
{
	/* Do nothing */
}

/*
 *  ======== cfg_get_auto_start ========
 *  Purpose:
 *      Retreive the autostart mask, if any, for this board.
 */
dsp_status cfg_get_auto_start(struct cfg_devnode *dev_node_obj,
			      OUT u32 *pdwAutoStart)
{
	dsp_status status = DSP_SOK;
	u32 dw_buf_size;

	dw_buf_size = sizeof(*pdwAutoStart);
	if (!dev_node_obj)
		status = CFG_E_INVALIDHDEVNODE;
	if (!pdwAutoStart)
		status = CFG_E_INVALIDPOINTER;
	if (DSP_SUCCEEDED(status)) {
		status = reg_get_value(AUTOSTART, (u8 *) pdwAutoStart,
				       &dw_buf_size);
		if (DSP_FAILED(status))
			status = CFG_E_RESOURCENOTAVAIL;
	}

	DBC_ENSURE((status == DSP_SOK &&
		    (*pdwAutoStart == 0 || *pdwAutoStart == 1))
		   || status != DSP_SOK);
	return status;
}

/*
 *  ======== cfg_get_dev_object ========
 *  Purpose:
 *      Retrieve the Device Object handle for a given devnode.
 */
dsp_status cfg_get_dev_object(struct cfg_devnode *dev_node_obj,
			      OUT u32 *pdwValue)
{
	dsp_status status = DSP_SOK;
	u32 dw_buf_size;

	if (!dev_node_obj)
		status = CFG_E_INVALIDHDEVNODE;

	if (!pdwValue)
		status = CFG_E_INVALIDHDEVNODE;

	dw_buf_size = sizeof(pdwValue);
	if (DSP_SUCCEEDED(status)) {

		/* check the device string and then call the reg_set_value */
		if (!
		    (strcmp
		     ((char *)((struct drv_ext *)dev_node_obj)->sz_string,
		      "TIOMAP1510")))
			status =
			    reg_get_value("DEVICE_DSP", (u8 *) pdwValue,
					  &dw_buf_size);
	}
	if (DSP_FAILED(status))
		pr_err("%s: Failed, status 0x%x\n", __func__, status);
	return status;
}

/*
 *  ======== cfg_get_dsp_resources ========
 *  Purpose:
 *      Get the DSP resources available to a given device.
 */
dsp_status cfg_get_dsp_resources(struct cfg_devnode *dev_node_obj,
				 OUT struct cfg_dspres *pDSPResTable)
{
	dsp_status status = DSP_SOK;	/* return value */
	u32 dw_res_size;

	if (!dev_node_obj) {
		status = CFG_E_INVALIDHDEVNODE;
	} else if (!pDSPResTable) {
		status = CFG_E_INVALIDPOINTER;
	} else {
		status = reg_get_value(DSPRESOURCES, (u8 *) pDSPResTable,
				       &dw_res_size);
	}
	if (DSP_FAILED(status)) {
		status = CFG_E_RESOURCENOTAVAIL;
		pr_err("%s: Failed, status 0x%x\n", __func__, status);
	}
	/* assert that resource values are reasonable */
	DBC_ASSERT(pDSPResTable->chip_type < 256);
	DBC_ASSERT(pDSPResTable->word_size > 0);
	DBC_ASSERT(pDSPResTable->word_size < 32);
	DBC_ASSERT(pDSPResTable->chip_number > 0);
	DBC_ASSERT(pDSPResTable->chip_number < 256);
	return status;
}

/*
 *  ======== cfg_get_exec_file ========
 *  Purpose:
 *      Retreive the default executable, if any, for this board.
 */
dsp_status cfg_get_exec_file(struct cfg_devnode *dev_node_obj, u32 ul_buf_size,
			     OUT char *pstrExecFile)
{
	dsp_status status = DSP_SOK;
	u32 exec_size = ul_buf_size;

	if (!dev_node_obj)
		status = CFG_E_INVALIDHDEVNODE;
	else if (!pstrExecFile)
		status = CFG_E_INVALIDPOINTER;

	if (DSP_SUCCEEDED(status)) {
		status =
		    reg_get_value(DEFEXEC, (u8 *) pstrExecFile, &exec_size);
		if (DSP_FAILED(status))
			status = CFG_E_RESOURCENOTAVAIL;
		else if (exec_size > ul_buf_size)
			status = DSP_ESIZE;

	}
	if (DSP_FAILED(status))
		pr_err("%s: Failed, status 0x%x\n", __func__, status);
	DBC_ENSURE(((status == DSP_SOK) &&
		    (strlen(pstrExecFile) <= ul_buf_size))
		   || (status != DSP_SOK));
	return status;
}

/*
 *  ======== cfg_get_host_resources ========
 *  Purpose:
 *      Get the Host allocated resources assigned to a given device.
 */
dsp_status cfg_get_host_resources(struct cfg_devnode *dev_node_obj,
				  OUT struct cfg_hostres *pHostResTable)
{
	dsp_status status = DSP_SOK;
	u32 dw_buf_size;

	if (!dev_node_obj)
		status = CFG_E_INVALIDHDEVNODE;

	if (!pHostResTable)
		status = CFG_E_INVALIDPOINTER;

	if (DSP_SUCCEEDED(status)) {
		dw_buf_size = sizeof(struct cfg_hostres);
		if (DSP_FAILED
		    (reg_get_value
		     (CURRENTCONFIG, (u8 *) pHostResTable, &dw_buf_size))) {
			status = CFG_E_RESOURCENOTAVAIL;
		}
	}
	if (DSP_FAILED(status))
		dev_dbg(bridge, "%s Failed, status 0x%x\n", __func__, status);
	return status;
}

/*
 *  ======== cfg_get_object ========
 *  Purpose:
 *      Retrieve the Object handle from the Registry
 */
dsp_status cfg_get_object(OUT u32 *pdwValue, u32 dw_type)
{
	dsp_status status = DSP_EINVALIDARG;
	u32 dw_buf_size;
	DBC_REQUIRE(pdwValue != NULL);

	dw_buf_size = sizeof(pdwValue);
	switch (dw_type) {
	case (REG_DRV_OBJECT):
		status =
		    reg_get_value(DRVOBJECT, (u8 *) pdwValue, &dw_buf_size);
		if (DSP_FAILED(status))
			status = CFG_E_RESOURCENOTAVAIL;
		break;
	case (REG_MGR_OBJECT):
		status =
		    reg_get_value(MGROBJECT, (u8 *) pdwValue, &dw_buf_size);
		if (DSP_FAILED(status))
			status = CFG_E_RESOURCENOTAVAIL;
		break;
	default:
		break;
	}
	if (DSP_FAILED(status)) {
		*pdwValue = 0;
		pr_err("%s: Failed, status 0x%x\n", __func__, status);
	}
	DBC_ENSURE((DSP_SUCCEEDED(status) && *pdwValue != 0) ||
		   (DSP_FAILED(status) && *pdwValue == 0));
	return status;
}

/*
 *  ======== cfg_init ========
 *  Purpose:
 *      Initialize the CFG module's private state.
 */
bool cfg_init(void)
{
	struct cfg_dspres dsp_resources;

	dsp_resources.chip_type = DSPTYPE64;
	dsp_resources.chip_number = 1;
	dsp_resources.word_size = DSPWORDSIZE;
	dsp_resources.mem_types = 0;
	dsp_resources.mem_desc[0].mem_type = 0;
	dsp_resources.mem_desc[0].ul_min = 0;
	dsp_resources.mem_desc[0].ul_max = 0;
	if (DSP_FAILED(reg_set_value(DSPRESOURCES, (u8 *) &dsp_resources,
				     sizeof(struct cfg_dspres))))
		pr_err("Failed to initialize DSP resources in registry\n");

	return true;
}

/*
 *  ======== cfg_set_dev_object ========
 *  Purpose:
 *      Store the Device Object handle and dev_node pointer for a given devnode.
 */
dsp_status cfg_set_dev_object(struct cfg_devnode *dev_node_obj, u32 dwValue)
{
	dsp_status status = DSP_SOK;
	u32 dw_buff_size;

	if (!dev_node_obj)
		status = CFG_E_INVALIDHDEVNODE;

	dw_buff_size = sizeof(dwValue);
	if (DSP_SUCCEEDED(status)) {
		/* Store the WCD device object in the Registry */

		if (!(strcmp((char *)dev_node_obj, "TIOMAP1510"))) {
			status = reg_set_value("DEVICE_DSP", (u8 *) &dwValue,
					       dw_buff_size);
		}
	}
	if (DSP_FAILED(status))
		pr_err("%s: Failed, status 0x%x\n", __func__, status);

	return status;
}

/*
 *  ======== cfg_set_object ========
 *  Purpose:
 *      Store the Driver Object handle
 */
dsp_status cfg_set_object(u32 dwValue, u32 dw_type)
{
	dsp_status status = DSP_EINVALIDARG;
	u32 dw_buff_size;

	dw_buff_size = sizeof(dwValue);
	switch (dw_type) {
	case (REG_DRV_OBJECT):
		status =
		    reg_set_value(DRVOBJECT, (u8 *) &dwValue, dw_buff_size);
		break;
	case (REG_MGR_OBJECT):
		status =
		    reg_set_value(MGROBJECT, (u8 *) &dwValue, dw_buff_size);
		break;
	default:
		break;
	}
	if (DSP_FAILED(status))
		pr_err("%s: Failed, status 0x%x\n", __func__, status);
	return status;
}
