#ifndef __ARCH_ARM_MACH_OMAP2_DSS_H
#define __ARCH_ARM_MACH_OMAP2_DSS_H

#if defined(CONFIG_OMAP2_DSS) || defined(CONFIG_OMAP2_DSS_MODULE)
void omap_setup_dss_device(struct platform_device *pdev);
#else
static inline void omap_setup_dss_device(struct platform_device *pdev) { }
#endif

#endif /* __ARCH_ARM_MACH_OMAP2_DSS_H */
