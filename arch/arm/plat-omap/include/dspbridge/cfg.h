/*
 * cfg.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * PM Configuration module.
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

#ifndef CFG_
#define CFG_
#include <dspbridge/host_os.h>
#include <dspbridge/cfgdefs.h>

/*
 *  ======== cfg_exit ========
 *  Purpose:
 *      Discontinue usage of the CFG module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      cfg_init(void) was previously called.
 *  Ensures:
 *      Resources acquired in cfg_init(void) are freed.
 */
extern void cfg_exit(void);

/*
 *  ======== cfg_get_auto_start ========
 *  Purpose:
 *      Retreive the autostart mask, if any, for this board.
 *  Parameters:
 *      dev_node_obj:       Handle to the dev_node who's WMD we are querying.
 *      pdwAutoStart:   Ptr to location for 32 bit autostart mask.
 *  Returns:
 *      DSP_SOK:                Success.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_RESOURCENOTAVAIL: Unable to retreive resource.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:        *pdwAutoStart contains autostart mask for this devnode.
 */
extern dsp_status cfg_get_auto_start(IN struct cfg_devnode *dev_node_obj,
				     OUT u32 *pdwAutoStart);

/*
 *  ======== cfg_get_cd_version ========
 *  Purpose:
 *      Retrieves the version of the PM Class Driver.
 *  Parameters:
 *      pdwVersion: Ptr to u32 to contain version number upon return.
 *  Returns:
 *      DSP_SOK:    Success.  pdwVersion contains Class Driver version in
 *                  the form: 0xAABBCCDD where AABB is Major version and
 *                  CCDD is Minor.
 *      DSP_EFAIL:  Failure.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    Success.
 *      else:       *pdwVersion is NULL.
 */
extern dsp_status cfg_get_cd_version(OUT u32 *pdwVersion);

/*
 *  ======== cfg_get_dev_object ========
 *  Purpose:
 *      Retrieve the Device Object handle for a given devnode.
 *  Parameters:
 *      dev_node_obj:	Platform's dev_node handle from which to retrieve
 *      		value.
 *      pdwValue:       Ptr to location to store the value.
 *  Returns:
 *      DSP_SOK:                Success.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_INVALIDPOINTER:   phDevObject is invalid.
 *      CFG_E_RESOURCENOTAVAIL: The resource is not available.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    *pdwValue is set to the retrieved u32.
 *      else:       *pdwValue is set to 0L.
 */
extern dsp_status cfg_get_dev_object(IN struct cfg_devnode *dev_node_obj,
				     OUT u32 *pdwValue);

/*
 *  ======== cfg_get_dsp_resources ========
 *  Purpose:
 *      Get the DSP resources available to a given device.
 *  Parameters:
 *      dev_node_obj:	Handle to the DEVNODE who's resources we are querying.
 *      pDSPResTable:   Ptr to a location to store the DSP resource table.
 *  Returns:
 *      DSP_SOK:                On success.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_RESOURCENOTAVAIL: The DSP Resource information is not
 *                              available
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    pDSPResTable points to a filled table of resources allocated
 *                  for the specified WMD.
 */
extern dsp_status cfg_get_dsp_resources(IN struct cfg_devnode *dev_node_obj,
					OUT struct cfg_dspres *pDSPResTable);

/*
 *  ======== cfg_get_exec_file ========
 *  Purpose:
 *      Retreive the default executable, if any, for this board.
 *  Parameters:
 *      dev_node_obj:       Handle to the dev_node who's WMD we are querying.
 *      buf_size:       Size of buffer.
 *      pstrExecFile:   Ptr to character buf to hold ExecFile.
 *  Returns:
 *      DSP_SOK:                Success.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_INVALIDPOINTER:   pstrExecFile is invalid.
 *      CFG_E_RESOURCENOTAVAIL: The resource is not available.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    Not more than buf_size bytes were copied into pstrExecFile,
 *                  and *pstrExecFile contains default executable for this
 *                  devnode.
 */
extern dsp_status cfg_get_exec_file(IN struct cfg_devnode *dev_node_obj,
				    IN u32 buf_size, OUT char *pstrExecFile);

/*
 *  ======== cfg_get_host_resources ========
 *  Purpose:
 *      Get the Host PC allocated resources assigned to a given device.
 *  Parameters:
 *      dev_node_obj:	Handle to the DEVNODE who's resources we are querying.
 *      pHostResTable:  Ptr to a location to store the host resource table.
 *  Returns:
 *      DSP_SOK:                On success.
 *      CFG_E_INVALIDPOINTER:   pHostResTable is invalid.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_RESOURCENOTAVAIL: The resource is not available.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    pHostResTable points to a filled table of resources
 *                  allocated for the specified WMD.
 *
 */
extern dsp_status cfg_get_host_resources(IN struct cfg_devnode *dev_node_obj,
					 OUT struct cfg_hostres *pHostResTable);

/*
 *  ======== cfg_get_object ========
 *  Purpose:
 *      Retrieve the Driver Object handle From the Registry
 *  Parameters:
 *      pdwValue:   Ptr to location to store the value.
 *      dw_type      Type of Object to Get
 *  Returns:
 *      DSP_SOK:    Success.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    *pdwValue is set to the retrieved u32(non-Zero).
 *      else:       *pdwValue is set to 0L.
 */
extern dsp_status cfg_get_object(OUT u32 *pdwValue, u32 dw_type);

/*
 *  ======== cfg_get_perf_value ========
 *  Purpose:
 *      Retrieve a flag indicating whether PERF should log statistics for the
 *      PM class driver.
 *  Parameters:
 *      pfEnablePerf:   Location to store flag.  0 indicates the key was
 *                      not found, or had a zero value.  A nonzero value
 *                      means the key was found and had a nonzero value.
 *  Returns:
 *  Requires:
 *      pfEnablePerf != NULL;
 *  Ensures:
 */
extern void cfg_get_perf_value(OUT bool *pfEnablePerf);

/*
 *  ======== cfg_get_wmd_file_name ========
 *  Purpose:
 *    Get the mini-driver file name for a given device.
 *  Parameters:
 *      dev_node_obj:       Handle to the dev_node who's WMD we are querying.
 *      buf_size:       Size of buffer.
 *      pWMDFileName:   Ptr to a character buffer to hold the WMD filename.
 *  Returns:
 *      DSP_SOK:                On success.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_RESOURCENOTAVAIL: The filename is not available.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:        Not more than buf_size bytes were copied
 *                      into pWMDFileName.
 *
 */
extern dsp_status cfg_get_wmd_file_name(IN struct cfg_devnode *dev_node_obj,
					IN u32 buf_size,
					OUT char *pWMDFileName);

/*
 *  ======== cfg_get_zl_file ========
 *  Purpose:
 *      Retreive the ZLFile, if any, for this board.
 *  Parameters:
 *      dev_node_obj:       Handle to the dev_node who's WMD we are querying.
 *      buf_size:       Size of buffer.
 *      pstrZLFileName: Ptr to character buf to hold ZLFileName.
 *  Returns:
 *      DSP_SOK:                Success.
 *      CFG_E_INVALIDPOINTER:   pstrZLFileName is invalid.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      CFG_E_RESOURCENOTAVAIL: couldn't find the ZLFileName.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    Not more than buf_size bytes were copied into
 *                  pstrZLFileName, and *pstrZLFileName contains ZLFileName
 *                  for this devnode.
 */
extern dsp_status cfg_get_zl_file(IN struct cfg_devnode *dev_node_obj,
				  IN u32 buf_size, OUT char *pstrZLFileName);

/*
 *  ======== cfg_init ========
 *  Purpose:
 *      Initialize the CFG module's private state.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public CFG functions.
 */
extern bool cfg_init(void);

/*
 *  ======== cfg_set_dev_object ========
 *  Purpose:
 *      Store the Device Object handle for a given devnode.
 *  Parameters:
 *      dev_node_obj:   Platform's dev_node handle we are storing value with.
 *      dwValue:    Arbitrary value to store.
 *  Returns:
 *      DSP_SOK:                Success.
 *      CFG_E_INVALIDHDEVNODE:  dev_node_obj is invalid.
 *      DSP_EFAIL:              Internal Error.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:    The Private u32 was successfully set.
 */
extern dsp_status cfg_set_dev_object(IN struct cfg_devnode *dev_node_obj,
				     IN u32 dwValue);

/*
 *  ======== CFG_SetDrvObject ========
 *  Purpose:
 *      Store the Driver Object handle.
 *  Parameters:
 *      dwValue:        Arbitrary value to store.
 *      dw_type          Type of Object to Store
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Internal Error.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      DSP_SOK:        The Private u32 was successfully set.
 */
extern dsp_status cfg_set_object(IN u32 dwValue, IN u32 dw_type);

#endif /* CFG_ */
