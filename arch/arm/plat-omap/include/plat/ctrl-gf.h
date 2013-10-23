#ifndef __ARCH_ARM_PLAT_OMAP_CTRL_GF_H
#define __ARCH_ARM_PLAT_OMAP_CTRL_GF_H

struct ctrl_gf_platform_data {
	int	reset_gpio;
	int	pdx_gpio;
	char	*sysclk_name;
	int	panel_gpio;
	int	te_gpio;
};

#endif
