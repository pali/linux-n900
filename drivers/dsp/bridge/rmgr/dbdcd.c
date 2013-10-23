/*
 * dbdcd.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This file contains the implementation of the DSP/BIOS Bridge
 * Configuration Database (DCD).
 *
 * Notes:
 *   The fxn dcd_get_objects can apply a callback fxn to each DCD object
 *   that is located in a specified COFF file.  At the moment,
 *   dcd_auto_register, dcd_auto_unregister, and NLDR module all use
 *   dcd_get_objects.
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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>
/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>

/*  ----------------------------------- Others */
#include <dspbridge/uuidutil.h>

/*  ----------------------------------- This */
#include <dspbridge/dbdcd.h>

/*  ----------------------------------- Global defines. */
#define SIGNATURE       0x5f444344	/* "DCD_" (in reverse). */

#define IS_VALID_HANDLE(h) (((h) != NULL) && (h->dw_signature == SIGNATURE))

#define MAX_INT2CHAR_LENGTH     16	/* Max int2char len of 32 bit int */

/* Name of section containing dependent libraries */
#define DEPLIBSECT		".dspbridge_deplibs"

/* DCD specific structures. */
struct dcd_manager {
	u32 dw_signature;	/* Used for object validation. */
	struct cod_manager *cod_mgr;	/* Handle to COD manager object. */
};

/* Global reference variables. */
static u32 refs;
static u32 enum_refs;

/* Helper function prototypes. */
static s32 atoi(char *psz_buf);
static dsp_status get_attrs_from_buf(char *psz_buf, u32 ul_buf_size,
				     enum dsp_dcdobjtype obj_type,
				     struct dcd_genericobj *pGenObj);
static void compress_buf(char *psz_buf, u32 ul_buf_size, s32 cCharSize);
static char dsp_char2_gpp_char(char *pWord, s32 cDspCharSize);
static dsp_status get_dep_lib_info(IN struct dcd_manager *hdcd_mgr,
				   IN struct dsp_uuid *uuid_obj,
				   IN OUT u16 *pNumLibs,
				   OPTIONAL OUT u16 *pNumPersLibs,
				   OPTIONAL OUT struct dsp_uuid *pDepLibUuids,
				   OPTIONAL OUT bool *pPersistentDepLibs,
				   IN enum nldr_phase phase);

/*
 *  ======== dcd_auto_register ========
 *  Purpose:
 *      Parses the supplied image and resigsters with DCD.
 */
dsp_status dcd_auto_register(IN struct dcd_manager *hdcd_mgr,
			     IN char *pszCoffPath)
{
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);

	if (IS_VALID_HANDLE(hdcd_mgr))
		status = dcd_get_objects(hdcd_mgr, pszCoffPath,
					 (dcd_registerfxn) dcd_register_object,
					 (void *)pszCoffPath);
	else
		status = DSP_EHANDLE;

	return status;
}

/*
 *  ======== dcd_auto_unregister ========
 *  Purpose:
 *      Parses the supplied DSP image and unresiters from DCD.
 */
dsp_status dcd_auto_unregister(IN struct dcd_manager *hdcd_mgr,
			       IN char *pszCoffPath)
{
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);

	if (IS_VALID_HANDLE(hdcd_mgr))
		status = dcd_get_objects(hdcd_mgr, pszCoffPath,
					 (dcd_registerfxn) dcd_register_object,
					 NULL);
	else
		status = DSP_EHANDLE;

	return status;
}

/*
 *  ======== dcd_create_manager ========
 *  Purpose:
 *      Creates DCD manager.
 */
dsp_status dcd_create_manager(IN char *pszZlDllName,
			      OUT struct dcd_manager **phDcdMgr)
{
	struct cod_manager *cod_mgr;	/* COD manager handle */
	struct dcd_manager *dcd_mgr_obj = NULL;	/* DCD Manager pointer */
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs >= 0);
	DBC_REQUIRE(phDcdMgr);

	status = cod_create(&cod_mgr, pszZlDllName, NULL);
	if (DSP_FAILED(status))
		goto func_end;

	/* Create a DCD object. */
	MEM_ALLOC_OBJECT(dcd_mgr_obj, struct dcd_manager, SIGNATURE);
	if (dcd_mgr_obj != NULL) {
		/* Fill out the object. */
		dcd_mgr_obj->cod_mgr = cod_mgr;

		/* Return handle to this DCD interface. */
		*phDcdMgr = dcd_mgr_obj;
	} else {
		status = DSP_EMEMORY;

		/*
		 * If allocation of DcdManager object failed, delete the
		 * COD manager.
		 */
		cod_delete(cod_mgr);
	}

	DBC_ENSURE((DSP_SUCCEEDED(status)) || ((cod_mgr == NULL) &&
					       (status == DSP_EFAIL))
		   || ((dcd_mgr_obj == NULL) && (status == DSP_EMEMORY)));

func_end:
	return status;
}

/*
 *  ======== dcd_destroy_manager ========
 *  Purpose:
 *      Frees DCD Manager object.
 */
dsp_status dcd_destroy_manager(IN struct dcd_manager *hdcd_mgr)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;
	dsp_status status = DSP_EHANDLE;

	DBC_REQUIRE(refs >= 0);

	if (IS_VALID_HANDLE(hdcd_mgr)) {
		/* Delete the COD manager. */
		cod_delete(dcd_mgr_obj->cod_mgr);

		/* Deallocate a DCD manager object. */
		MEM_FREE_OBJECT(dcd_mgr_obj);

		status = DSP_SOK;
	}

	return status;
}

/*
 *  ======== dcd_enumerate_object ========
 *  Purpose:
 *      Enumerates objects in the DCD.
 */
dsp_status dcd_enumerate_object(IN s32 cIndex, IN enum dsp_dcdobjtype obj_type,
				OUT struct dsp_uuid *uuid_obj)
{
	dsp_status status = DSP_SOK;
	char sz_reg_key[REG_MAXREGPATHLENGTH];
	char sz_value[REG_MAXREGPATHLENGTH];
	char sz_data[REG_MAXREGPATHLENGTH];
	u32 dw_value_size;
	u32 dw_data_size;
	struct dsp_uuid dsp_uuid_obj;
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */
	u32 dw_key_len = 0;

	DBC_REQUIRE(refs >= 0);
	DBC_REQUIRE(cIndex >= 0);
	DBC_REQUIRE(uuid_obj != NULL);

	if ((cIndex != 0) && (enum_refs == 0)) {
		/*
		 * If an enumeration is being performed on an index greater
		 * than zero, then the current enum_refs must have been
		 * incremented to greater than zero.
		 */
		status = DSP_ECHANGEDURINGENUM;
	} else {
		/* Enumerate a specific key in the registry by index. */
		dw_value_size = REG_MAXREGPATHLENGTH;
		dw_data_size = REG_MAXREGPATHLENGTH;

		/*
		 * Pre-determine final key length. It's length of DCD_REGKEY +
		 *  "_\0" + length of sz_obj_type string + terminating NULL.
		 */
		dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;
		DBC_ASSERT(dw_key_len < REG_MAXREGPATHLENGTH);

		/* Create proper REG key; concatenate DCD_REGKEY with
		 * obj_type. */
		strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
		if ((strlen(sz_reg_key) + strlen("_\0")) <
		    REG_MAXREGPATHLENGTH) {
			strncat(sz_reg_key, "_\0", 2);
		} else {
			status = DSP_EFAIL;
		}

		/* This snprintf is guaranteed not to exceed max size of an
		 * integer. */
		status = snprintf(sz_obj_type, MAX_INT2CHAR_LENGTH, "%d",
				  obj_type);

		if (status == -1) {
			status = DSP_EFAIL;
		} else {
			status = DSP_SOK;
			if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
			    REG_MAXREGPATHLENGTH) {
				strncat(sz_reg_key, sz_obj_type,
					strlen(sz_obj_type) + 1);
			} else {
				status = DSP_EFAIL;
			}
		}

		if (DSP_SUCCEEDED(status)) {
			status = reg_enum_value(cIndex, sz_reg_key, sz_value,
						&dw_value_size, sz_data,
						&dw_data_size);
		}

		if (DSP_SUCCEEDED(status)) {
			/* Create UUID value using string retrieved from
			 * registry. */
			uuid_uuid_from_string(sz_value, &dsp_uuid_obj);

			*uuid_obj = dsp_uuid_obj;

			/* Increment enum_refs to update reference count. */
			enum_refs++;

			status = DSP_SOK;
		} else if (status == REG_E_NOMOREITEMS) {
			/* At the end of enumeration. Reset enum_refs. */
			enum_refs = 0;

			status = DSP_SENUMCOMPLETE;
		} else {
			status = DSP_EFAIL;
		}
	}

	DBC_ENSURE(uuid_obj || (status == DSP_EFAIL));

	return status;
}

/*
 *  ======== dcd_exit ========
 *  Purpose:
 *      Discontinue usage of the DCD module.
 */
void dcd_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;
	if (refs == 0)
		cod_exit();

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== dcd_get_dep_libs ========
 */
dsp_status dcd_get_dep_libs(IN struct dcd_manager *hdcd_mgr,
			    IN struct dsp_uuid *uuid_obj,
			    u16 numLibs, OUT struct dsp_uuid *pDepLibUuids,
			    OUT bool *pPersistentDepLibs,
			    IN enum nldr_phase phase)
{
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(IS_VALID_HANDLE(hdcd_mgr));
	DBC_REQUIRE(uuid_obj != NULL);
	DBC_REQUIRE(pDepLibUuids != NULL);
	DBC_REQUIRE(pPersistentDepLibs != NULL);

	status =
	    get_dep_lib_info(hdcd_mgr, uuid_obj, &numLibs, NULL, pDepLibUuids,
			     pPersistentDepLibs, phase);

	return status;
}

/*
 *  ======== dcd_get_num_dep_libs ========
 */
dsp_status dcd_get_num_dep_libs(IN struct dcd_manager *hdcd_mgr,
				IN struct dsp_uuid *uuid_obj,
				OUT u16 *pNumLibs, OUT u16 *pNumPersLibs,
				IN enum nldr_phase phase)
{
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(IS_VALID_HANDLE(hdcd_mgr));
	DBC_REQUIRE(pNumLibs != NULL);
	DBC_REQUIRE(pNumPersLibs != NULL);
	DBC_REQUIRE(uuid_obj != NULL);

	status = get_dep_lib_info(hdcd_mgr, uuid_obj, pNumLibs, pNumPersLibs,
				  NULL, NULL, phase);

	return status;
}

/*
 *  ======== dcd_get_object_def ========
 *  Purpose:
 *      Retrieves the properties of a node or processor based on the UUID and
 *      object type.
 */
dsp_status dcd_get_object_def(IN struct dcd_manager *hdcd_mgr,
			      IN struct dsp_uuid *pObjUuid,
			      IN enum dsp_dcdobjtype obj_type,
			      OUT struct dcd_genericobj *pObjDef)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;	/* ptr to DCD mgr */
	struct cod_libraryobj *lib = NULL;
	dsp_status status = DSP_SOK;
	u32 ul_addr = 0;	/* Used by cod_get_section */
	u32 ul_len = 0;		/* Used by cod_get_section */
	u32 dw_buf_size;	/* Used by REG functions */
	char sz_reg_key[REG_MAXREGPATHLENGTH];
	char *sz_uuid;		/*[MAXUUIDLEN]; */
	char sz_reg_data[REG_MAXREGPATHLENGTH];
	char sz_sect_name[MAXUUIDLEN + 2];	/* ".[UUID]\0" */
	char *psz_coff_buf;
	u32 dw_key_len;		/* Len of REG key. */
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pObjDef != NULL);
	DBC_REQUIRE(pObjUuid != NULL);

	sz_uuid = (char *)mem_calloc(MAXUUIDLEN, MEM_PAGED);
	if (!sz_uuid) {
		status = DSP_EMEMORY;
		goto func_end;
	}

	if (!IS_VALID_HANDLE(hdcd_mgr)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	/* Pre-determine final key length. It's length of DCD_REGKEY +
	 *  "_\0" + length of sz_obj_type string + terminating NULL */
	dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;
	DBC_ASSERT(dw_key_len < REG_MAXREGPATHLENGTH);

	/* Create proper REG key; concatenate DCD_REGKEY with obj_type. */
	strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);

	if ((strlen(sz_reg_key) + strlen("_\0")) < REG_MAXREGPATHLENGTH)
		strncat(sz_reg_key, "_\0", 2);
	else
		status = DSP_EFAIL;

	status = snprintf(sz_obj_type, MAX_INT2CHAR_LENGTH, "%d", obj_type);
	if (status == -1) {
		status = DSP_EFAIL;
	} else {
		status = DSP_SOK;

		if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
		    REG_MAXREGPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else {
			status = DSP_EFAIL;
		}

		/* Create UUID value to set in registry. */
		uuid_uuid_to_string(pObjUuid, sz_uuid, MAXUUIDLEN);

		if ((strlen(sz_reg_key) + MAXUUIDLEN) < REG_MAXREGPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = DSP_EFAIL;

		/* Retrieve paths from the registry based on struct dsp_uuid */
		dw_buf_size = REG_MAXREGPATHLENGTH;
	}
	if (DSP_SUCCEEDED(status))
		status =
		    reg_get_value(sz_reg_key, (u8 *) sz_reg_data, &dw_buf_size);

	if (DSP_FAILED(status)) {
		status = DSP_EUUID;
		goto func_end;
	}

	/* Open COFF file. */
	status = cod_open(dcd_mgr_obj->cod_mgr, sz_reg_data, COD_NOLOAD, &lib);
	if (DSP_FAILED(status)) {
		status = DSP_EDCDLOADBASE;
		goto func_end;
	}

	/* Ensure sz_uuid + 1 is not greater than sizeof sz_sect_name. */
	DBC_ASSERT((strlen(sz_uuid) + 1) < sizeof(sz_sect_name));

	/* Create section name based on node UUID. A period is
	 * pre-pended to the UUID string to form the section name.
	 * I.e. ".24BC8D90_BB45_11d4_B756_006008BDB66F" */
	strncpy(sz_sect_name, ".", 2);
	strncat(sz_sect_name, sz_uuid, strlen(sz_uuid));

	/* Get section information. */
	status = cod_get_section(lib, sz_sect_name, &ul_addr, &ul_len);
	if (DSP_FAILED(status)) {
		status = DSP_EDCDGETSECT;
		goto func_end;
	}

	/* Allocate zeroed buffer. */
	psz_coff_buf = mem_calloc(ul_len + 4, MEM_PAGED);
#ifdef _DB_TIOMAP
	if (strstr(sz_reg_data, "iva") == NULL) {
		/* Locate section by objectID and read its content. */
		status =
		    cod_read_section(lib, sz_sect_name, psz_coff_buf, ul_len);
	} else {
		status =
		    cod_read_section(lib, sz_sect_name, psz_coff_buf, ul_len);
		dev_dbg(bridge, "%s: Skipped Byte swap for IVA!!\n", __func__);
	}
#else
	status = cod_read_section(lib, sz_sect_name, psz_coff_buf, ul_len);
#endif
	if (DSP_SUCCEEDED(status)) {
		/* Compres DSP buffer to conform to PC format. */
		if (strstr(sz_reg_data, "iva") == NULL) {
			compress_buf(psz_coff_buf, ul_len, DSPWORDSIZE);
		} else {
			compress_buf(psz_coff_buf, ul_len, 1);
			dev_dbg(bridge, "%s: Compressing IVA COFF buffer by 1 "
				"for IVA!!\n", __func__);
		}

		/* Parse the content of the COFF buffer. */
		status =
		    get_attrs_from_buf(psz_coff_buf, ul_len, obj_type, pObjDef);
		if (DSP_FAILED(status))
			status = DSP_EDCDPARSESECT;
	} else {
		status = DSP_EDCDREADSECT;
	}

	/* Free the previously allocated dynamic buffer. */
	kfree(psz_coff_buf);
func_end:
	if (lib)
		cod_close(lib);

	kfree(sz_uuid);

	return status;
}

/*
 *  ======== dcd_get_objects ========
 */
dsp_status dcd_get_objects(IN struct dcd_manager *hdcd_mgr,
			   IN char *pszCoffPath, dcd_registerfxn registerFxn,
			   void *handle)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;
	dsp_status status = DSP_SOK;
	char *psz_coff_buf;
	char *psz_cur;
	struct cod_libraryobj *lib = NULL;
	u32 ul_addr = 0;	/* Used by cod_get_section */
	u32 ul_len = 0;		/* Used by cod_get_section */
	char seps[] = ":, ";
	char *token = NULL;
	struct dsp_uuid dsp_uuid_obj;
	s32 object_type;

	DBC_REQUIRE(refs > 0);
	if (!IS_VALID_HANDLE(hdcd_mgr)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	/* Open DSP coff file, don't load symbols. */
	status = cod_open(dcd_mgr_obj->cod_mgr, pszCoffPath, COD_NOLOAD, &lib);
	if (DSP_FAILED(status)) {
		status = DSP_EDCDLOADBASE;
		goto func_cont;
	}

	/* Get DCD_RESIGER_SECTION section information. */
	status = cod_get_section(lib, DCD_REGISTER_SECTION, &ul_addr, &ul_len);
	if (DSP_FAILED(status) || !(ul_len > 0)) {
		status = DSP_EDCDNOAUTOREGISTER;
		goto func_cont;
	}

	/* Allocate zeroed buffer. */
	psz_coff_buf = mem_calloc(ul_len + 4, MEM_PAGED);
#ifdef _DB_TIOMAP
	if (strstr(pszCoffPath, "iva") == NULL) {
		/* Locate section by objectID and read its content. */
		status = cod_read_section(lib, DCD_REGISTER_SECTION,
					  psz_coff_buf, ul_len);
	} else {
		dev_dbg(bridge, "%s: Skipped Byte swap for IVA!!\n", __func__);
		status = cod_read_section(lib, DCD_REGISTER_SECTION,
					  psz_coff_buf, ul_len);
	}
#else
	status =
	    cod_read_section(lib, DCD_REGISTER_SECTION, psz_coff_buf, ul_len);
#endif
	if (DSP_SUCCEEDED(status)) {
		/* Compress DSP buffer to conform to PC format. */
		if (strstr(pszCoffPath, "iva") == NULL) {
			compress_buf(psz_coff_buf, ul_len, DSPWORDSIZE);
		} else {
			compress_buf(psz_coff_buf, ul_len, 1);
			dev_dbg(bridge, "%s: Compress COFF buffer with 1 word "
				"for IVA!!\n", __func__);
		}

		/* Read from buffer and register object in buffer. */
		psz_cur = psz_coff_buf;
		while ((token = strsep(&psz_cur, seps)) && *token != '\0') {
			/*  Retrieve UUID string. */
			uuid_uuid_from_string(token, &dsp_uuid_obj);

			/*  Retrieve object type */
			token = strsep(&psz_cur, seps);

			/*  Retrieve object type */
			object_type = atoi(token);

			/*
			 *  Apply registerFxn to the found DCD object.
			 *  Possible actions include:
			 *
			 *  1) Register found DCD object.
			 *  2) Unregister found DCD object (when handle == NULL)
			 *  3) Add overlay node.
			 */
			status =
			    registerFxn(&dsp_uuid_obj, object_type, handle);
			if (DSP_FAILED(status)) {
				/* if error occurs, break from while loop. */
				break;
			}
		}
	} else {
		status = DSP_EDCDREADSECT;
	}

	/* Free the previously allocated dynamic buffer. */
	kfree(psz_coff_buf);
func_cont:
	if (lib)
		cod_close(lib);

func_end:
	return status;
}

/*
 *  ======== dcd_get_library_name ========
 *  Purpose:
 *      Retrieves the library name for the given UUID.
 *
 */
dsp_status dcd_get_library_name(IN struct dcd_manager *hdcd_mgr,
				IN struct dsp_uuid *uuid_obj,
				IN OUT char *pstrLibName, IN OUT u32 * pdwSize,
				enum nldr_phase phase, OUT bool *phase_split)
{
	char sz_reg_key[REG_MAXREGPATHLENGTH];
	char sz_uuid[MAXUUIDLEN];
	u32 dw_key_len;		/* Len of REG key. */
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(uuid_obj != NULL);
	DBC_REQUIRE(pstrLibName != NULL);
	DBC_REQUIRE(pdwSize != NULL);
	DBC_REQUIRE(IS_VALID_HANDLE(hdcd_mgr));

	dev_dbg(bridge, "%s: hdcd_mgr %p, uuid_obj %p, pstrLibName %p, pdwSize "
		"%p\n", __func__, hdcd_mgr, uuid_obj, pstrLibName, pdwSize);

	/*
	 *  Pre-determine final key length. It's length of DCD_REGKEY +
	 *  "_\0" + length of sz_obj_type string + terminating NULL.
	 */
	dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;
	DBC_ASSERT(dw_key_len < REG_MAXREGPATHLENGTH);

	/* Create proper REG key; concatenate DCD_REGKEY with obj_type. */
	strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
	if ((strlen(sz_reg_key) + strlen("_\0")) < REG_MAXREGPATHLENGTH)
		strncat(sz_reg_key, "_\0", 2);
	else
		status = DSP_EFAIL;

	switch (phase) {
	case NLDR_CREATE:
		/* create phase type */
		sprintf(sz_obj_type, "%d", DSP_DCDCREATELIBTYPE);
		break;
	case NLDR_EXECUTE:
		/* execute phase type */
		sprintf(sz_obj_type, "%d", DSP_DCDEXECUTELIBTYPE);
		break;
	case NLDR_DELETE:
		/* delete phase type */
		sprintf(sz_obj_type, "%d", DSP_DCDDELETELIBTYPE);
		break;
	case NLDR_NOPHASE:
		/* known to be a dependent library */
		sprintf(sz_obj_type, "%d", DSP_DCDLIBRARYTYPE);
		break;
	default:
		status = DSP_EINVALIDARG;
		DBC_ASSERT(false);
	}
	if (DSP_SUCCEEDED(status)) {
		if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
		    REG_MAXREGPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else {
			status = DSP_EFAIL;
		}
		/* Create UUID value to find match in registry. */
		uuid_uuid_to_string(uuid_obj, sz_uuid, MAXUUIDLEN);
		if ((strlen(sz_reg_key) + MAXUUIDLEN) < REG_MAXREGPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = DSP_EFAIL;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Retrieve path from the registry based on dsp_uuid */
		status = reg_get_value(sz_reg_key, (u8 *) pstrLibName, pdwSize);
	}

	/* If can't find, phases might be registered as generic LIBRARYTYPE */
	if (DSP_FAILED(status) && phase != NLDR_NOPHASE) {
		if (phase_split)
			*phase_split = false;

		strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
		if ((strlen(sz_reg_key) + strlen("_\0")) <
		    REG_MAXREGPATHLENGTH) {
			strncat(sz_reg_key, "_\0", 2);
		} else {
			status = DSP_EFAIL;
		}
		sprintf(sz_obj_type, "%d", DSP_DCDLIBRARYTYPE);
		if ((strlen(sz_reg_key) + strlen(sz_obj_type))
		    < REG_MAXREGPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else {
			status = DSP_EFAIL;
		}
		uuid_uuid_to_string(uuid_obj, sz_uuid, MAXUUIDLEN);
		if ((strlen(sz_reg_key) + MAXUUIDLEN) < REG_MAXREGPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = DSP_EFAIL;

		status = reg_get_value(sz_reg_key, (u8 *) pstrLibName, pdwSize);
	}

	return status;
}

/*
 *  ======== dcd_init ========
 *  Purpose:
 *      Initialize the DCD module.
 */
bool dcd_init(void)
{
	bool init_cod;
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (refs == 0) {
		/* Initialize required modules. */
		init_cod = cod_init();

		if (!init_cod) {
			ret = false;
			/* Exit initialized modules. */
			if (init_cod)
				cod_exit();
		}
	}

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs == 0)));

	return ret;
}

/*
 *  ======== dcd_register_object ========
 *  Purpose:
 *      Registers a node or a processor with the DCD.
 *      If psz_path_name == NULL, unregister the specified DCD object.
 */
dsp_status dcd_register_object(IN struct dsp_uuid *uuid_obj,
			       IN enum dsp_dcdobjtype obj_type,
			       IN char *psz_path_name)
{
	dsp_status status = DSP_SOK;
	char sz_reg_key[REG_MAXREGPATHLENGTH];
	char sz_uuid[MAXUUIDLEN + 1];
	u32 dw_path_size = 0;
	u32 dw_key_len;		/* Len of REG key. */
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(uuid_obj != NULL);
	DBC_REQUIRE((obj_type == DSP_DCDNODETYPE) ||
		    (obj_type == DSP_DCDPROCESSORTYPE) ||
		    (obj_type == DSP_DCDLIBRARYTYPE) ||
		    (obj_type == DSP_DCDCREATELIBTYPE) ||
		    (obj_type == DSP_DCDEXECUTELIBTYPE) ||
		    (obj_type == DSP_DCDDELETELIBTYPE));

	dev_dbg(bridge, "%s: object UUID %p, obj_type %d, szPathName %s\n",
		__func__, uuid_obj, obj_type, psz_path_name);

	/*
	 * Pre-determine final key length. It's length of DCD_REGKEY +
	 *  "_\0" + length of sz_obj_type string + terminating NULL.
	 */
	dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;
	DBC_ASSERT(dw_key_len < REG_MAXREGPATHLENGTH);

	/* Create proper REG key; concatenate DCD_REGKEY with obj_type. */
	strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
	if ((strlen(sz_reg_key) + strlen("_\0")) < REG_MAXREGPATHLENGTH)
		strncat(sz_reg_key, "_\0", 2);
	else {
		status = DSP_EFAIL;
		goto func_end;
	}

	status = snprintf(sz_obj_type, MAX_INT2CHAR_LENGTH, "%d", obj_type);
	if (status == -1) {
		status = DSP_EFAIL;
	} else {
		status = DSP_SOK;
		if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
		    REG_MAXREGPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else
			status = DSP_EFAIL;

		/* Create UUID value to set in registry. */
		uuid_uuid_to_string(uuid_obj, sz_uuid, MAXUUIDLEN);
		if ((strlen(sz_reg_key) + MAXUUIDLEN) < REG_MAXREGPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = DSP_EFAIL;
	}

	if (DSP_FAILED(status))
		goto func_end;

	/*
	 * If psz_path_name != NULL, perform registration, otherwise,
	 * perform unregistration.
	 */
	if (psz_path_name) {
		/* Add new reg value (UUID+obj_type) with COFF path info */
		dw_path_size = strlen(psz_path_name) + 1;
		status =
		    reg_set_value(sz_reg_key, (u8 *) psz_path_name,
				  dw_path_size);
		dev_dbg(bridge, "%s: psz_path_name=%s, dw_path_size=%d\n",
			__func__, psz_path_name, dw_path_size);
	} else {
		/* Deregister an existing object */
		status = reg_delete_value(sz_reg_key);
	}

	if (DSP_SUCCEEDED(status)) {
		/*
		 *  Because the node database has been updated through a
		 *  successful object registration/de-registration operation,
		 *  we need to reset the object enumeration counter to allow
		 *  current enumerations to reflect this update in the node
		 *  database.
		 */
		enum_refs = 0;
	}
func_end:
	return status;
}

/*
 *  ======== dcd_unregister_object ========
 *  Call DCD_Register object with psz_path_name set to NULL to
 *  perform actual object de-registration.
 */
dsp_status dcd_unregister_object(IN struct dsp_uuid *uuid_obj,
				 IN enum dsp_dcdobjtype obj_type)
{
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(uuid_obj != NULL);
	DBC_REQUIRE((obj_type == DSP_DCDNODETYPE) ||
		    (obj_type == DSP_DCDPROCESSORTYPE) ||
		    (obj_type == DSP_DCDLIBRARYTYPE) ||
		    (obj_type == DSP_DCDCREATELIBTYPE) ||
		    (obj_type == DSP_DCDEXECUTELIBTYPE) ||
		    (obj_type == DSP_DCDDELETELIBTYPE));

	/*
	 *  When dcd_register_object is called with NULL as pathname,
	 *  it indicates an unregister object operation.
	 */
	status = dcd_register_object(uuid_obj, obj_type, NULL);

	return status;
}

/*
 **********************************************************************
 * DCD Helper Functions
 **********************************************************************
 */

/*
 *  ======== atoi ========
 *  Purpose:
 *      This function converts strings in decimal or hex format to integers.
 */
static s32 atoi(char *psz_buf)
{
	char *pch = psz_buf;
	s32 base = 0;

	while (isspace(*pch))
		pch++;

	if (*pch == '-' || *pch == '+') {
		base = 10;
		pch++;
	} else if (*pch && tolower(pch[strlen(pch) - 1]) == 'h') {
		base = 16;
	}

	return simple_strtoul(pch, NULL, base);
}

/*
 *  ======== get_attrs_from_buf ========
 *  Purpose:
 *      Parse the content of a buffer filled with DSP-side data and
 *      retrieve an object's attributes from it. IMPORTANT: Assume the
 *      buffer has been converted from DSP format to GPP format.
 */
static dsp_status get_attrs_from_buf(char *psz_buf, u32 ul_buf_size,
				     enum dsp_dcdobjtype obj_type,
				     struct dcd_genericobj *pGenObj)
{
	dsp_status status = DSP_SOK;
	char seps[] = ", ";
	char *psz_cur;
	char *token;
	s32 token_len = 0;
	u32 i = 0;
#ifdef _DB_TIOMAP
	s32 entry_id;
#endif

	DBC_REQUIRE(psz_buf != NULL);
	DBC_REQUIRE(ul_buf_size != 0);
	DBC_REQUIRE((obj_type == DSP_DCDNODETYPE)
		    || (obj_type == DSP_DCDPROCESSORTYPE));
	DBC_REQUIRE(pGenObj != NULL);

	switch (obj_type) {
	case DSP_DCDNODETYPE:
		/*
		 * Parse COFF sect buffer to retrieve individual tokens used
		 * to fill in object attrs.
		 */
		psz_cur = psz_buf;
		token = strsep(&psz_cur, seps);

		/* u32 cb_struct */
		pGenObj->obj_data.node_obj.ndb_props.cb_struct =
		    (u32) atoi(token);
		token = strsep(&psz_cur, seps);

		/* dsp_uuid ui_node_id */
		uuid_uuid_from_string(token,
				      &pGenObj->obj_data.node_obj.ndb_props.
				      ui_node_id);
		token = strsep(&psz_cur, seps);

		/* ac_name */
		DBC_REQUIRE(token);
		token_len = strlen(token);
		if (token_len > DSP_MAXNAMELEN - 1)
			token_len = DSP_MAXNAMELEN - 1;

		strncpy(pGenObj->obj_data.node_obj.ndb_props.ac_name,
			token, token_len);
		pGenObj->obj_data.node_obj.ndb_props.ac_name[token_len] = '\0';
		token = strsep(&psz_cur, seps);
		/* u32 ntype */
		pGenObj->obj_data.node_obj.ndb_props.ntype = atoi(token);
		token = strsep(&psz_cur, seps);
		/* u32 cache_on_gpp */
		pGenObj->obj_data.node_obj.ndb_props.cache_on_gpp = atoi(token);
		token = strsep(&psz_cur, seps);
		/* dsp_resourcereqmts dsp_resource_reqmts */
		pGenObj->obj_data.node_obj.ndb_props.dsp_resource_reqmts.
		    cb_struct = (u32) atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.static_data_size = atoi(token);
		token = strsep(&psz_cur, seps);
		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.global_data_size = atoi(token);
		token = strsep(&psz_cur, seps);
		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.program_mem_size = atoi(token);
		token = strsep(&psz_cur, seps);
		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.uwc_execution_time = atoi(token);
		token = strsep(&psz_cur, seps);
		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.uwc_period = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.uwc_deadline = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.avg_exection_time = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.minimum_period = atoi(token);
		token = strsep(&psz_cur, seps);

		/* s32 prio */
		pGenObj->obj_data.node_obj.ndb_props.prio = atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 stack_size */
		pGenObj->obj_data.node_obj.ndb_props.stack_size = atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 sys_stack_size */
		pGenObj->obj_data.node_obj.ndb_props.sys_stack_size =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 stack_seg */
		pGenObj->obj_data.node_obj.ndb_props.stack_seg = atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 message_depth */
		pGenObj->obj_data.node_obj.ndb_props.message_depth =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 num_input_streams */
		pGenObj->obj_data.node_obj.ndb_props.num_input_streams =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 num_output_streams */
		pGenObj->obj_data.node_obj.ndb_props.num_output_streams =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 utimeout */
		pGenObj->obj_data.node_obj.ndb_props.utimeout = atoi(token);
		token = strsep(&psz_cur, seps);

		/* char *pstr_create_phase_fxn */
		DBC_REQUIRE(token);
		token_len = strlen(token);
		pGenObj->obj_data.node_obj.pstr_create_phase_fxn =
		    mem_calloc(token_len + 1, MEM_PAGED);
		strncpy(pGenObj->obj_data.node_obj.pstr_create_phase_fxn,
			token, token_len);
		pGenObj->obj_data.node_obj.pstr_create_phase_fxn[token_len] =
		    '\0';
		token = strsep(&psz_cur, seps);

		/* char *pstr_execute_phase_fxn */
		DBC_REQUIRE(token);
		token_len = strlen(token);
		pGenObj->obj_data.node_obj.pstr_execute_phase_fxn =
		    mem_calloc(token_len + 1, MEM_PAGED);
		strncpy(pGenObj->obj_data.node_obj.pstr_execute_phase_fxn,
			token, token_len);
		pGenObj->obj_data.node_obj.pstr_execute_phase_fxn[token_len] =
		    '\0';
		token = strsep(&psz_cur, seps);

		/* char *pstr_delete_phase_fxn */
		DBC_REQUIRE(token);
		token_len = strlen(token);
		pGenObj->obj_data.node_obj.pstr_delete_phase_fxn =
		    mem_calloc(token_len + 1, MEM_PAGED);
		strncpy(pGenObj->obj_data.node_obj.pstr_delete_phase_fxn,
			token, token_len);
		pGenObj->obj_data.node_obj.pstr_delete_phase_fxn[token_len] =
		    '\0';
		token = strsep(&psz_cur, seps);

		/* Segment id for message buffers */
		pGenObj->obj_data.node_obj.msg_segid = atoi(token);
		token = strsep(&psz_cur, seps);

		/* Message notification type */
		pGenObj->obj_data.node_obj.msg_notify_type = atoi(token);
		token = strsep(&psz_cur, seps);

		/* char *pstr_i_alg_name */
		if (token) {
			token_len = strlen(token);
			pGenObj->obj_data.node_obj.pstr_i_alg_name =
			    mem_calloc(token_len + 1, MEM_PAGED);
			strncpy(pGenObj->obj_data.node_obj.pstr_i_alg_name,
				token, token_len);
			pGenObj->obj_data.node_obj.pstr_i_alg_name[token_len] =
			    '\0';
			token = strsep(&psz_cur, seps);
		}

		/* Load type (static, dynamic, or overlay) */
		if (token) {
			pGenObj->obj_data.node_obj.us_load_type = atoi(token);
			token = strsep(&psz_cur, seps);
		}

		/* Dynamic load data requirements */
		if (token) {
			pGenObj->obj_data.node_obj.ul_data_mem_seg_mask =
			    atoi(token);
			token = strsep(&psz_cur, seps);
		}

		/* Dynamic load code requirements */
		if (token) {
			pGenObj->obj_data.node_obj.ul_code_mem_seg_mask =
			    atoi(token);
			token = strsep(&psz_cur, seps);
		}

		/* Extract node profiles into node properties */
		if (token) {

			pGenObj->obj_data.node_obj.ndb_props.count_profiles =
			    atoi(token);
			for (i = 0;
			     i <
			     pGenObj->obj_data.node_obj.
			     ndb_props.count_profiles; i++) {
				token = strsep(&psz_cur, seps);
				if (token) {
					/* Heap Size for the node */
					pGenObj->obj_data.node_obj.
					    ndb_props.node_profiles[i].
					    ul_heap_size = atoi(token);
				}
			}
		}
		token = strsep(&psz_cur, seps);
		if (token) {
			pGenObj->obj_data.node_obj.ndb_props.stack_seg_name =
			    (u32) (token);
		}

		break;

	case DSP_DCDPROCESSORTYPE:
		/*
		 * Parse COFF sect buffer to retrieve individual tokens used
		 * to fill in object attrs.
		 */
		psz_cur = psz_buf;
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.cb_struct = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.processor_family = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.processor_type = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.clock_rate = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.ul_internal_mem_size = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.ul_external_mem_size = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.processor_id = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.ty_running_rtos = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.node_min_priority = atoi(token);
		token = strsep(&psz_cur, seps);

		pGenObj->obj_data.proc_info.node_max_priority = atoi(token);

#ifdef _DB_TIOMAP
		/* Proc object may contain additional(extended) attributes. */
		/* attr must match proc.hxx */
		for (entry_id = 0; entry_id < 7; entry_id++) {
			token = strsep(&psz_cur, seps);
			pGenObj->obj_data.ext_proc_obj.ty_tlb[entry_id].
			    ul_gpp_phys = atoi(token);

			token = strsep(&psz_cur, seps);
			pGenObj->obj_data.ext_proc_obj.ty_tlb[entry_id].
			    ul_dsp_virt = atoi(token);
		}
#endif

		break;

	default:
		status = DSP_EFAIL;
		break;
	}

	return status;
}

/*
 *  ======== CompressBuffer ========
 *  Purpose:
 *      Compress the DSP buffer, if necessary, to conform to PC format.
 */
static void compress_buf(char *psz_buf, u32 ul_buf_size, s32 cCharSize)
{
	char *p;
	char ch;
	char *q;

	p = psz_buf;
	if (p == NULL)
		return;

	for (q = psz_buf; q < (psz_buf + ul_buf_size);) {
		ch = dsp_char2_gpp_char(q, cCharSize);
		if (ch == '\\') {
			q += cCharSize;
			ch = dsp_char2_gpp_char(q, cCharSize);
			switch (ch) {
			case 't':
				*p = '\t';
				break;

			case 'n':
				*p = '\n';
				break;

			case 'r':
				*p = '\r';
				break;

			case '0':
				*p = '\0';
				break;

			default:
				*p = ch;
				break;
			}
		} else {
			*p = ch;
		}
		p++;
		q += cCharSize;
	}

	/* NULL out remainder of buffer. */
	while (p < q)
		*p++ = '\0';
}

/*
 *  ======== dsp_char2_gpp_char ========
 *  Purpose:
 *      Convert DSP char to host GPP char in a portable manner
 */
static char dsp_char2_gpp_char(char *pWord, s32 cDspCharSize)
{
	char ch = '\0';
	char *ch_src;
	s32 i;

	for (ch_src = pWord, i = cDspCharSize; i > 0; i--)
		ch |= *ch_src++;

	return ch;
}

/*
 *  ======== get_dep_lib_info ========
 */
static dsp_status get_dep_lib_info(IN struct dcd_manager *hdcd_mgr,
				   IN struct dsp_uuid *uuid_obj,
				   IN OUT u16 *pNumLibs,
				   OPTIONAL OUT u16 *pNumPersLibs,
				   OPTIONAL OUT struct dsp_uuid *pDepLibUuids,
				   OPTIONAL OUT bool *pPersistentDepLibs,
				   enum nldr_phase phase)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;
	char *psz_coff_buf = NULL;
	char *psz_cur;
	char *psz_file_name = NULL;
	struct cod_libraryobj *lib = NULL;
	u32 ul_addr = 0;	/* Used by cod_get_section */
	u32 ul_len = 0;		/* Used by cod_get_section */
	u32 dw_data_size = COD_MAXPATHLENGTH;
	char seps[] = ", ";
	char *token = NULL;
	bool get_uuids = (pDepLibUuids != NULL);
	u16 dep_libs = 0;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);

	DBC_REQUIRE(IS_VALID_HANDLE(hdcd_mgr));
	DBC_REQUIRE(pNumLibs != NULL);
	DBC_REQUIRE(uuid_obj != NULL);

	/*  Initialize to 0 dependent libraries, if only counting number of
	 *  dependent libraries */
	if (!get_uuids) {
		*pNumLibs = 0;
		*pNumPersLibs = 0;
	}

	/* Allocate a buffer for file name */
	psz_file_name = mem_calloc(dw_data_size, MEM_PAGED);
	if (psz_file_name == NULL) {
		status = DSP_EMEMORY;
	} else {
		/* Get the name of the library */
		status = dcd_get_library_name(hdcd_mgr, uuid_obj, psz_file_name,
					      &dw_data_size, phase, NULL);
	}

	/* Open the library */
	if (DSP_SUCCEEDED(status)) {
		status = cod_open(dcd_mgr_obj->cod_mgr, psz_file_name,
				  COD_NOLOAD, &lib);
	}
	if (DSP_SUCCEEDED(status)) {
		/* Get dependent library section information. */
		status = cod_get_section(lib, DEPLIBSECT, &ul_addr, &ul_len);

		if (DSP_FAILED(status)) {
			/* Ok, no dependent libraries */
			ul_len = 0;
			status = DSP_SNODEPENDENTLIBS;
		}
	}

	if (DSP_FAILED(status) || !(ul_len > 0))
		goto func_cont;

	/* Allocate zeroed buffer. */
	psz_coff_buf = mem_calloc(ul_len + 4, MEM_PAGED);
	if (psz_coff_buf == NULL)
		status = DSP_EMEMORY;

	/* Read section contents. */
	status = cod_read_section(lib, DEPLIBSECT, psz_coff_buf, ul_len);
	if (DSP_FAILED(status))
		goto func_cont;

	/* Compress and format DSP buffer to conform to PC format. */
	compress_buf(psz_coff_buf, ul_len, DSPWORDSIZE);

	/* Read from buffer */
	psz_cur = psz_coff_buf;
	while ((token = strsep(&psz_cur, seps)) && *token != '\0') {
		if (get_uuids) {
			if (dep_libs >= *pNumLibs) {
				/* Gone beyond the limit */
				break;
			} else {
				/* Retrieve UUID string. */
				uuid_uuid_from_string(token,
						      &(pDepLibUuids
							[dep_libs]));
				/* Is this library persistent? */
				token = strsep(&psz_cur, seps);
				pPersistentDepLibs[dep_libs] = atoi(token);
				dep_libs++;
			}
		} else {
			/* Advanc to next token */
			token = strsep(&psz_cur, seps);
			if (atoi(token))
				(*pNumPersLibs)++;

			/* Just counting number of dependent libraries */
			(*pNumLibs)++;
		}
	}
func_cont:
	if (lib)
		cod_close(lib);

	/* Free previously allocated dynamic buffers. */
	kfree(psz_file_name);

	kfree(psz_coff_buf);

	return status;
}
