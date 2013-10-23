#ifndef _DSS_BOOTTIME_H
#define _DSS_BOOTTIME_H

int	dss_boottime_get_clocks(void);
void	dss_boottime_put_clocks(void);
int	dss_boottime_enable_clocks(void);
void	dss_boottime_disable_clocks(void);
u32	dss_boottime_get_plane_base(int pidx);
enum omapfb_color_format dss_boottime_get_plane_format(int pidx);
int	dss_boottime_get_plane_bpp(int plane_idx);
size_t	dss_boottime_get_plane_size(int pidx);
int	dss_boottime_plane_is_enabled(int pdix);
int	dss_boottime_reset(void);

#endif

