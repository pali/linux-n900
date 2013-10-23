/*
 * regsup.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provide registry support functions.
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
#include <dspbridge/list.h>

/*  ----------------------------------- This */
#include <regsup.h>

struct reg_value {
	struct list_head link;	/* Make it linked to a list */
	char name[MAXREGPATHLENGTH];	/*  Name of a given value entry */
	u32 data_size;		/*  Size of the data */
	void *pdata;		/*  Pointer to the actual data */
};

/*  Pointer to the registry support key */
static struct lst_list reg_key, *reg_key_list = &reg_key;

/*
 *  ======== regsup_init ========
 *  Purpose:
 *      Initialize the Registry Support module's private state.
 */
bool regsup_init(void)
{
	INIT_LIST_HEAD(&reg_key_list->head);
	return true;
}

/*
 *  ======== regsup_exit ========
 *  Purpose:
 *      Release all registry support allocations.
 */
void regsup_exit(void)
{
	struct reg_value *rv;
	/*  Now go through each entry and free all resources. */
	while (!LST_IS_EMPTY(reg_key_list)) {
		rv = (struct reg_value *)lst_get_head(reg_key_list);

		kfree(rv->pdata);
		kfree(rv);
	}
}

/*
 *  ======== regsup_get_value ========
 *  Purpose:
 *      Get the value of the entry having the given name.
 */
dsp_status regsup_get_value(char *valName, void *pbuf, u32 * data_size)
{
	dsp_status ret = DSP_EFAIL;
	struct reg_value *rv = (struct reg_value *)lst_first(reg_key_list);

	/*  Need to search through the entries looking for the right one. */
	while (rv) {
		/*  See if the name matches. */
		if (strncmp(rv->name, valName, MAXREGPATHLENGTH) == 0) {
			/*  We have a match!  Copy out the data. */
			memcpy(pbuf, rv->pdata, rv->data_size);

			/*  Get the size for the caller. */
			*data_size = rv->data_size;

			/*  Set our status to good and exit. */
			ret = DSP_SOK;
			break;
		}
		rv = (struct reg_value *)lst_next(reg_key_list,
						  (struct list_head *)rv);
	}

	dev_dbg(bridge, "REG: get %s, status = 0x%x\n", valName, ret);

	return ret;
}

/*
 *  ======== regsup_set_value ========
 *  Purpose:
 *      Sets the value of the entry having the given name.
 */
dsp_status regsup_set_value(char *valName, void *pbuf, u32 data_size)
{
	dsp_status ret = DSP_EFAIL;
	struct reg_value *rv = (struct reg_value *)lst_first(reg_key_list);

	/*  Need to search through the entries looking for the right one. */
	while (rv) {
		/*  See if the name matches. */
		if (strncmp(rv->name, valName, MAXREGPATHLENGTH) == 0) {
			/*  Make sure the new data size is the same. */
			if (data_size != rv->data_size) {
				/*  The caller needs a different data size! */
				kfree(rv->pdata);
				rv->pdata = mem_alloc(data_size, MEM_NONPAGED);
				if (rv->pdata == NULL)
					break;
			}

			/*  We have a match!  Copy out the data. */
			memcpy(rv->pdata, pbuf, data_size);

			/* Reset datasize - overwrite if new or same */
			rv->data_size = data_size;

			/*  Set our status to good and exit. */
			ret = DSP_SOK;
			break;
		}
		rv = (struct reg_value *)lst_next(reg_key_list,
						  (struct list_head *)rv);
	}

	/*  See if we found a match or if this is a new entry */
	if (!rv) {
		/*  No match, need to make a new entry */
		struct reg_value *new = mem_calloc(sizeof(struct reg_value),
						   MEM_NONPAGED);

		strncat(new->name, valName, MAXREGPATHLENGTH - 1);
		new->pdata = mem_alloc(data_size, MEM_NONPAGED);
		if (new->pdata != NULL) {
			memcpy(new->pdata, pbuf, data_size);
			new->data_size = data_size;
			lst_put_tail(reg_key_list, (struct list_head *)new);
			ret = DSP_SOK;
		}
	}

	dev_dbg(bridge, "REG: set %s, status = 0x%x", valName, ret);

	return ret;
}

/*
 *  ======== regsup_enum_value ========
 *  Purpose:
 *      Returns registry "values" and their "data" under a (sub)key.
 */
dsp_status regsup_enum_value(IN u32 dw_index, IN CONST char *pstrKey,
			     IN OUT char *pstrValue, IN OUT u32 * pdwValueSize,
			     IN OUT char *pstrData, IN OUT u32 * pdwDataSize)
{
	dsp_status ret = REG_E_INVALIDSUBKEY;
	struct reg_value *rv = (struct reg_value *)lst_first(reg_key_list);
	u32 dw_key_len;
	u32 count = 0;

	DBC_REQUIRE(pstrKey);
	dw_key_len = strlen(pstrKey);

	/*  Need to search through the entries looking for the right one. */
	while (rv) {
		/*  See if the name matches. */
		if (strncmp(rv->name, pstrKey, dw_key_len) == 0 &&
		    count++ == dw_index) {
			/*  We have a match!  Copy out the data. */
			memcpy(pstrData, rv->pdata, rv->data_size);
			/*  Get the size for the caller. */
			*pdwDataSize = rv->data_size;
			*pdwValueSize = strlen(&(rv->name[dw_key_len]));
			strncpy(pstrValue, &(rv->name[dw_key_len]),
				*pdwValueSize + 1);
			/*  Set our status to good and exit. */
			ret = DSP_SOK;
			break;
		}
		rv = (struct reg_value *)lst_next(reg_key_list,
						  (struct list_head *)rv);
	}

	if (count && DSP_FAILED(ret))
		ret = REG_E_NOMOREITEMS;

	dev_dbg(bridge, "REG: enum Key %s, Value %s, status = 0x%x",
		pstrKey, pstrValue, ret);

	return ret;
}

/*
 *  ======== regsup_delete_value ========
 */
dsp_status regsup_delete_value(IN CONST char *pstrValue)
{
	dsp_status ret = DSP_EFAIL;
	struct reg_value *rv = (struct reg_value *)lst_first(reg_key_list);

	while (rv) {
		/*  See if the name matches. */
		if (strncmp(rv->name, pstrValue, MAXREGPATHLENGTH) == 0) {
			/* We have a match!  Delete this key.  To delete a
			 * key, we free all resources associated with this
			 * key and, if we're not already the last entry in
			 * the array, we copy that entry into this deleted
			 * key.
			 */
			lst_remove_elem(reg_key_list, (struct list_head *)rv);
			kfree(rv->pdata);
			kfree(rv);

			/*  Set our status to good and exit... */
			ret = DSP_SOK;
			break;
		}
		rv = (struct reg_value *)lst_next(reg_key_list,
						  (struct list_head *)rv);
	}

	dev_dbg(bridge, "REG: del %s, status = 0x%x", pstrValue, ret);

	return ret;

}
