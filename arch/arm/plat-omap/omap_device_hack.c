/*
 * omap_device hack implementation
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/omap_device.h>
#include <plat/omap_hwmod.h>
#include <plat/powerdomain.h>

static const char omap_pdev_name[][20] = {
       {"omapdss"},
       {"omap_ssi"},
       {"omap-mcbsp"},
       {"mmci-omap-hs"},
       {"core_test"},
       {"neon_test"},
       {"usbhost_test"},
#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
       {"iva2_test"},
#endif
       {"sgx_test"},
       {"dss_test"},
       {"per_test"},
       {"cam_test"},
       {"mpu_test"},
};

static const char dss_pwrdm_name[] = "dss_pwrdm";
static const char ssi_pwrdm_name[] = "core_pwrdm";
static const char mcbsp_pwrdm_name[] = "per_pwrdm";
static const char mmc_pwrdm_name[] = "core_pwrdm";
static const char core_test_pwrdm_name[] = "core_pwrdm";
static const char neon_test_pwrdm_name[] = "neon_pwrdm";
static const char usbhost_test_pwrdm_name[] = "usbhost_pwrdm";
static const char sgx_test_pwrdm_name[] = "sgx_pwrdm";
static const char dss_test_pwrdm_name[] = "dss_pwrdm";
static const char per_test_pwrdm_name[] = "per_pwrdm";
static const char cam_test_pwrdm_name[] = "cam_pwrdm";
static const char mpu_test_pwrdm_name[] = "mpu_pwrdm";

#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
static const char iva2_test_pwrdm_name[] = "iva2_pwrdm";
#endif

static struct omap_hwmod dss_hwmod = {
       .name = "dss_hwmod",
       .dev_attr = &dss_pwrdm_name,
};

static struct omap_hwmod *dss_hwmods[] = {
	&dss_hwmod,
};

static struct omap_device dss_omapdev = {
       .pdev   = { .name = "omapdss",
       },
       .hwmods = dss_hwmods,
};

static struct omap_hwmod ssi_hwmod = {
       .name = "ssi_hwmod",
       .dev_attr = &ssi_pwrdm_name,
};

static struct omap_hwmod *ssi_hwmods[] = {
	&ssi_hwmod,
};

static struct omap_device ssi_omapdev = {
       .pdev = { .name = "omap_ssi",
       },
       .hwmods = ssi_hwmods,
};

static struct omap_hwmod mcbsp_hwmod = {
       .name = "mcbsp_hwmod",
       .dev_attr = &mcbsp_pwrdm_name,
};

static struct omap_hwmod *mcbsp_hwmods[] = {
	&mcbsp_hwmod,
};

static struct omap_device mcbsp_omapdev = {
       .pdev = { .name = "omap-mcbsp",
       },
       .hwmods = mcbsp_hwmods,
};

static struct omap_hwmod mmc_hwmod = {
       .name = "mmci-omap-hs_hwmod",
       .dev_attr = &mmc_pwrdm_name,
};

static struct omap_hwmod *mmc_hwmods[] = {
       &mmc_hwmod,
};

static struct omap_device hsmmc_omapdev = {
       .pdev = { .name = "mmci-omap-hs",
       },
       .hwmods = mmc_hwmods,
};

static struct omap_hwmod core_test_hwmod = {
       .name = "core_test_hwmod",
       .dev_attr = &core_test_pwrdm_name,
};

static struct omap_hwmod *core_test_hwmods[] = {
	&core_test_hwmod,
};

static struct omap_device core_test_omapdev = {
       .pdev   = { .name = "core_test",
       },
       .hwmods = core_test_hwmods,
};

static struct omap_hwmod neon_test_hwmod = {
       .name = "neon_test_hwmod",
       .dev_attr = &neon_test_pwrdm_name,
};

static struct omap_hwmod *neon_test_hwmods[] = {
	&neon_test_hwmod,
};

static struct omap_device neon_test_omapdev = {
       .pdev   = { .name = "neon_test",
       },
       .hwmods = neon_test_hwmods,
};

static struct omap_hwmod usbhost_test_hwmod = {
       .name = "usbhost_test_hwmod",
       .dev_attr = &usbhost_test_pwrdm_name,
};

static struct omap_hwmod *usbhost_test_hwmods[] = {
	&usbhost_test_hwmod,
};

static struct omap_device usbhost_test_omapdev = {
       .pdev   = { .name = "usbhost_test",
       },
       .hwmods = usbhost_test_hwmods,
};

#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
static struct omap_hwmod iva2_test_hwmod = {
       .name = "iva2_test_hwmod",
       .dev_attr = &iva2_test_pwrdm_name,
};

static struct omap_hwmod *iva2_test_hwmods[] = {
	&iva2_test_hwmod,
};

static struct omap_device iva2_test_omapdev = {
       .pdev   = { .name = "iva2_test",
       },
       .hwmods = iva2_test_hwmods,
};
#endif

static struct omap_hwmod sgx_test_hwmod = {
       .name = "sgx_test_hwmod",
       .dev_attr = &sgx_test_pwrdm_name,
};

static struct omap_hwmod *sgx_test_hwmods[] = {
	&sgx_test_hwmod,
};

static struct omap_device sgx_test_omapdev = {
       .pdev   = { .name = "sgx_test",
       },
       .hwmods = sgx_test_hwmods,
};

static struct omap_hwmod dss_test_hwmod = {
       .name = "dss_test_hwmod",
       .dev_attr = &dss_test_pwrdm_name,
};

static struct omap_hwmod *dss_test_hwmods[] = {
	&dss_test_hwmod,
};

static struct omap_device dss_test_omapdev = {
       .pdev   = { .name = "dss_test",
       },
       .hwmods = dss_test_hwmods,
};

static struct omap_hwmod per_test_hwmod = {
       .name = "per_test_hwmod",
       .dev_attr = &per_test_pwrdm_name,
};

static struct omap_hwmod *per_test_hwmods[] = {
	&per_test_hwmod,
};

static struct omap_device per_test_omapdev = {
       .pdev   = { .name = "per_test",
       },
       .hwmods = per_test_hwmods,
};

static struct omap_hwmod cam_test_hwmod = {
       .name = "cam_test_hwmod",
       .dev_attr = &cam_test_pwrdm_name,
};

static struct omap_hwmod *cam_test_hwmods[] = {
	&cam_test_hwmod,
};

static struct omap_device cam_test_omapdev = {
       .pdev   = { .name = "cam_test",
       },
       .hwmods = cam_test_hwmods,
};

static struct omap_hwmod mpu_test_hwmod = {
       .name = "mpu_test_hwmod",
       .dev_attr = &mpu_test_pwrdm_name,
};

static struct omap_hwmod *mpu_test_hwmods[] = {
	&mpu_test_hwmod,
};

static struct omap_device mpu_test_omapdev = {
       .pdev   = { .name = "mpu_test",
       },
       .hwmods = mpu_test_hwmods,
};

static struct omap_device *omapdevs[] = {
	&dss_omapdev,
	&ssi_omapdev,
	&mcbsp_omapdev,
	&hsmmc_omapdev,
	&core_test_omapdev,
	&neon_test_omapdev,
	&usbhost_test_omapdev,
#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
	&iva2_test_omapdev,
#endif
	&sgx_test_omapdev,
	&dss_test_omapdev,
	&per_test_omapdev,
	&cam_test_omapdev,
	&mpu_test_omapdev,
};

struct powerdomain *omap_device_get_pwrdm(struct omap_device *od)
{
	int i;
	struct platform_device p;

	if (!od)
		return NULL;

	p = od->pdev;

	for (i = 0; i < ARRAY_SIZE(omapdevs); i++) {
		if (strcmp(omap_pdev_name[i], p.name) == 0) {
			struct omap_hwmod *h;
			h = od->hwmods[0];
			return pwrdm_lookup((const char *) h->dev_attr);
		}
	}
	/*
	 * XXX Assumes that all omap_hwmod powerdomains are identical.
	 * This may not necessarily be true.  There should be a sanity
	 * check in here to WARN() if any difference appears.
	 */
	if (!od->hwmods_cnt)
		return NULL;
	return omap_hwmod_get_pwrdm(od->hwmods[0]);
}

struct omap_device *to_omap_device(struct platform_device *pdev)
{
	int i;

	if (!pdev)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(omapdevs); i++) {
		if (strcmp(omap_pdev_name[i], pdev->name) == 0)
			return omapdevs[i];
	}
	return (struct omap_device *)
			container_of((pdev), struct omap_device, pdev);
}

