/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Markku Kylänpää <ext-markku.kylanpaa@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Authors: Markku Kylänpää, 2010
 */

/*
 * This file contains an interface to Runtime Policy Framework
 * functions. Aegis Validator is used to implement source id check
 * functionality of Runtime Policy Framework when an object
 * (application or library) is loaded.
 */

#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/hash.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/aegis/restok.h>
#include <linux/aegis/credp.h>

#include "validator.h"
#include "fs.h"
#include "hashlist.h"
#include "sidcheck.h"

/* Resource token id for the credential that protects securityfs entries */


/**
 * validator_sid_define() - Define or get source identifier
 * @str: Source identifier string
 *
 * The function translates source identifier text string to numeric identifier
 * using restok function. If the identifier is already defined then existing
 * value is returned.
 *
 * Return numeric source identifier
 */
long validator_sid_define(const char *str)
{
	long src_ns = restok_define("SRC", 0);
	long src_id;
	while (isspace(*str))
		str++;
	src_id = restok_define(str, src_ns);
	return src_id;
}

/**
 * validator_sid_check() - Check whether the current process can load lib/app
 * @name:   process name
 * @src_id: source identifier of the loaded component
 * @cred:   credentials to check against
 *
 * Check whether we allow loading of the new component to the current process.
 * Checking is done by calling Runtime Policy Framework functions.
 *
 * Return 0 if OK and negative value if there was an error.
 */
int validator_sid_check(const char *name, long src_id, const struct cred *cred)
{
	int retval = 0;
	if (credp_check(src_id, cred)) {
		pr_info("Aegis: credp_kcheck failed %ld %s\n", src_id, name);
		retval = -EFAULT;
	}
	return valinfo.sidmode ? retval : 0;
}
