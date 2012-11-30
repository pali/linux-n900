/*
 *  linux/arch/arm/plat-omap/component-version.c
 *
 *  Copyright (C) 2005 Nokia Corporation
 *  Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <plat/board.h>

static struct omap_version_config *version_configs = NULL;
static int version_configs_n = 0;
static int component_version_read_proc(char *page, char **start, off_t off,
				       int count, int *eof, void *data)
{
	int len, i;
	const struct omap_version_config *ver;
	char *p;

	i = 0;
	p = page;
	for (i = 0; i< version_configs_n; i++) {
		ver = &version_configs[i];
		p += sprintf(p, "%-12s%s\n", ver->component, ver->version);
		i++;
	}

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

static int __init component_version_init(void)
{
	int i;
	const struct omap_version_config *ver;

	if (omap_get_config(OMAP_TAG_VERSION_STR, struct omap_version_config) == NULL)
		return -ENODEV;

	i = 0;
	while (omap_get_nr_config(OMAP_TAG_VERSION_STR,
					 struct omap_version_config, i) != NULL) {
		i++;
	}
	
	version_configs = (struct omap_version_config *)
			  kmalloc(sizeof(struct omap_version_config) * i, GFP_KERNEL);
	
	if(!version_configs)
		return -ENOMEM;

	version_configs_n = i;
	i = 0;
	
	while ((ver = omap_get_nr_config(OMAP_TAG_VERSION_STR,
					 struct omap_version_config, i)) != NULL) {
		memcpy(&version_configs[i],ver,sizeof(struct omap_version_config));
		i++;
	}

	if (!create_proc_read_entry("component_version", S_IRUGO, NULL,
				    component_version_read_proc, NULL))
		return -ENOMEM;

	return 0;
}

static void __exit component_version_exit(void)
{
	version_configs_n = 0;
	if(version_configs){
		kfree(version_configs);
		version_configs = NULL;
	}
	remove_proc_entry("component_version", NULL);
}

late_initcall(component_version_init);
module_exit(component_version_exit);

MODULE_AUTHOR("Juha Yrjölä <juha.yrjola@nokia.com>");
MODULE_DESCRIPTION("Component version driver");
MODULE_LICENSE("GPL");
