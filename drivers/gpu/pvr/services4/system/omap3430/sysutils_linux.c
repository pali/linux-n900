/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include <linux/version.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <asm/bug.h>
#include <linux/semaphore.h>
#include <clock.h>
#include "sgxdefs.h"
#include "services_headers.h"
#include "sysinfo.h"
#include "sgxapi_km.h"
#include "sysconfig.h"
#include "sgxinfokm.h"
#include "syslocal.h"

#define	HZ_TO_MHZ(m) ((m) / 1000000)

#define	MUTEX_INIT(pl)  	init_MUTEX(pl)
#define	MUTEX_LOCK(pl)		down(pl)
#define	MUTEX_UNLOCK(pl)	up(pl)

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22))	

static SYS_SPECIFIC_DATA *psNotifierSysSpecData;

static inline unsigned long scale_by_rate(unsigned long val, unsigned long rate1, unsigned long rate2)
{
	if (rate1 >= rate2)
	{
		return val * (rate1 / rate2);
	}

	return val / (rate2 / rate1);
}

static inline unsigned long scale_prop_to_SGX_clock(unsigned long val, unsigned long rate)
{
	return scale_by_rate(val, rate, SYS_SGX_CLOCK_SPEED);
}

static inline unsigned long scale_inv_prop_to_SGX_clock(unsigned long val, unsigned long rate)
{
	return scale_by_rate(val, SYS_SGX_CLOCK_SPEED, rate);
}

static void post_clock_rate_change(unsigned long incoming_rate)
{
	unsigned long rate = 0;
	SGX_TIMING_INFORMATION *psTimingInfo = psNotifierSysSpecData->psSGXTimingInfo;

	if (0 == incoming_rate)
	{
		rate = clk_get_rate(psNotifierSysSpecData->psSGX_FCK);	
	}
	else
	{	
		rate = incoming_rate;
	}
	
	PVR_ASSERT(rate != 0);

	psTimingInfo->ui32CoreClockSpeed = rate;
	psTimingInfo->ui32HWRecoveryFreq = scale_prop_to_SGX_clock(SYS_SGX_HWRECOVERY_TIMEOUT_FREQ, rate);
	psTimingInfo->ui32uKernelFreq = scale_prop_to_SGX_clock(SYS_SGX_PDS_TIMER_FREQ, rate); 

	PVR_TRACE(("post_clock_rate_change: SGX clock rate: %dMHz", HZ_TO_MHZ(psTimingInfo->ui32CoreClockSpeed)));

#if 0
	PVR_TRACE(("post_clock_rate_change: HW Recovery frequency: %dHz", psTimingInfo->ui32HWRecoveryFreq));
	PVR_TRACE(("post_clock_rate_change: PDS Timer frequency: %dHz", psTimingInfo->ui32uKernelFreq));
#endif

	PVRSRVDevicePostClockSpeedChange(psNotifierSysSpecData->psSGXDevNode->sDevId.ui32DeviceIndex, IMG_FALSE, IMG_NULL);
}

static void pre_clock_rate_change(void)
{
	PVRSRVDevicePreClockSpeedChange(psNotifierSysSpecData->psSGXDevNode->sDevId.ui32DeviceIndex, IMG_FALSE, IMG_NULL);

}

static int vdd2_pre_post_func(struct notifier_block *n, unsigned long event, void *ptr)
{
	struct clk_notifier_data *cnd;

	PVR_UNREFERENCED_PARAMETER(n);

	cnd = (struct clk_notifier_data *)ptr;

	PVR_TRACE(("vdd2_pre_post_func: old clock rate = %lu", cnd->old_rate));
	PVR_TRACE(("vdd2_pre_post_func: new clock rate = %lu", cnd->new_rate));
	
	if (CLK_PRE_RATE_CHANGE == event)
	{
		PVR_TRACE(("vdd2_pre_post_func: CLK_PRE_RATE_CHANGE event"));
		
		BUG_ON(in_irq());		
		MUTEX_LOCK(&psNotifierSysSpecData->sConstraintNotifierLock);

		if (psNotifierSysSpecData->bSGXClocksEnabled)
		{
			pre_clock_rate_change();
		}		
	}
	else if (CLK_POST_RATE_CHANGE == event)
	{
		PVR_TRACE(("vdd2_pre_post_func: CLK_POST_RATE_CHANGE event"));				
		
		BUG_ON(in_irq());

		if (psNotifierSysSpecData->bSGXClocksEnabled)
		{
			post_clock_rate_change(cnd->new_rate);
		}

		MUTEX_UNLOCK(&psNotifierSysSpecData->sConstraintNotifierLock);				
	}
	else
	{	
		PVR_DPF((PVR_DBG_ERROR, "vdd2_pre_post_func: unexpected event (%ul)", event));
	}
	
	return 0;
}

static struct notifier_block vdd2_pre_post = {
	vdd2_pre_post_func,
	 NULL
};

static IMG_VOID RegisterConstraintNotifications(SYS_DATA *psSysData, SGX_TIMING_INFORMATION *psSGXTimingInfo)
{
	PVR_TRACE(("Registering constraint notifications"));

	PVR_ASSERT(psNotifierSysSpecData == IMG_NULL);

	psNotifierSysSpecData = (SYS_SPECIFIC_DATA *)psSysData->pvSysSpecificData;
	psNotifierSysSpecData->psSGXTimingInfo = psSGXTimingInfo;

	clk_notifier_register(psNotifierSysSpecData->psSGX_FCK, &vdd2_pre_post);
}

static IMG_VOID UnRegisterConstraintNotifications(IMG_VOID)
{
	if (psNotifierSysSpecData == IMG_NULL)
	{
		return;
	}

	PVR_TRACE(("Unregistering constraint notifications"));
	
	clk_notifier_unregister(psNotifierSysSpecData->psSGX_FCK, &vdd2_pre_post);

	psNotifierSysSpecData = IMG_NULL;
}
#endif 

PVRSRV_ERROR EnableSGXClocks(SYS_DATA *psSysData)
{
#if !defined(NO_HARDWARE)
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;
	unsigned long rate;
	int res;

	
	if (psSysSpecData->bSGXClocksEnabled)
	{
		return PVRSRV_OK;
	}

	PVR_TRACE(("EnableSGXClocks: Enabling SGX Clocks"));

#if defined(DEBUG)
	rate = clk_get_rate(psSysSpecData->psMPU_CK);
	PVR_TRACE(("CPU Clock is %dMhz", HZ_TO_MHZ(rate)));
#endif

	res = clk_enable(psSysSpecData->psSGX_FCK);
	if (res < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSGXClocks: Couldn't enable SGX functional clock (%d)", res));
		return PVRSRV_ERROR_GENERIC;
	}

	res = clk_enable(psSysSpecData->psSGX_ICK); 
	if (res < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSGXClocks: Couldn't enable SGX interface clock (%d)", res));

		clk_disable(psSysSpecData->psSGX_FCK);
		return PVRSRV_ERROR_GENERIC;
	}

#if 0
	/* Code section removed for Fremnatle -
	 * call to clk_set_rate was causing crash */

	rate = clk_get_rate(psSysSpecData->psSGX_FCK);
	if(rate < SYS_SGX_CLOCK_SPEED)
	{
		PVR_TRACE(("SGX Functional Clock rate is %dMhz. Attempting to set to %dMhz", HZ_TO_MHZ(rate), HZ_TO_MHZ(SYS_SGX_CLOCK_SPEED)));
		res = clk_set_rate(psSysSpecData->psSGX_FCK, SYS_SGX_CLOCK_SPEED);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_WARNING, "EnableSGXClocks: Couldn't set SGX Functional Clock rate (%d)", res));
		}
	}
	PVR_TRACE(("SGX Functional Clock rate is %dMhz", HZ_TO_MHZ(clk_get_rate(psSysSpecData->psSGX_FCK))));
	BUG_ON(in_irq());
#endif

	MUTEX_LOCK(&psSysSpecData->sConstraintNotifierLock);

	
	psSysSpecData->bSGXClocksEnabled = IMG_TRUE;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22))	

	pre_clock_rate_change();

	post_clock_rate_change(0);
#endif 

	MUTEX_UNLOCK(&psSysSpecData->sConstraintNotifierLock);

#else	
	PVR_UNREFERENCED_PARAMETER(psSysData);
#endif	
	return PVRSRV_OK;
}


IMG_VOID DisableSGXClocks(SYS_DATA *psSysData)
{
#if !defined(NO_HARDWARE)
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	
	if (!psSysSpecData->bSGXClocksEnabled)
	{
		return;
	}

	PVR_TRACE(("DisableSGXClocks: Disabling SGX Clocks"));

	if (psSysSpecData->psSGX_ICK)
	{
		clk_disable(psSysSpecData->psSGX_ICK); 
	}

	if (psSysSpecData->psSGX_FCK)
	{
		clk_disable(psSysSpecData->psSGX_FCK);
	}

	MUTEX_LOCK(&psSysSpecData->sConstraintNotifierLock);

	
	psSysSpecData->bSGXClocksEnabled = IMG_FALSE;

	MUTEX_UNLOCK(&psSysSpecData->sConstraintNotifierLock);
#else	
	PVR_UNREFERENCED_PARAMETER(psSysData);
#endif	
}

PVRSRV_ERROR EnableSystemClocks(SYS_DATA *psSysData, SGX_TIMING_INFORMATION *psSGXTimingInfo)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;
	struct clk *psCLK;
	int res;
#if !defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	PVRSRV_ERROR eError;
#endif
#if defined(DEBUG) || defined(TIMING)
	int rate;
	struct clk *sys_ck;
	IMG_CPU_PHYADDR     TimerRegPhysBase;
	IMG_HANDLE hTimerEnable;
	IMG_UINT32 *pui32TimerEnable;

#endif	

	PVR_TRACE(("EnableSystemClocks: Enabling System Clocks"));

	if (!psSysSpecData->bSysClocksOneTimeInit)
	{
		MUTEX_INIT(&psSysSpecData->sConstraintNotifierLock);

		psCLK = clk_get(NULL, "core_ck");
		if (IS_ERR(psCLK))
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSsystemClocks: Couldn't get Core Clock"));
			goto ExitError;
		}
		psSysSpecData->psCORE_CK = psCLK;

		psCLK = clk_get(NULL, "sgx_fck");
		if (IS_ERR(psCLK))
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSsystemClocks: Couldn't get SGX Functional Clock"));
			goto ExitError;
		}
		psSysSpecData->psSGX_FCK = psCLK;

		psCLK = clk_get(NULL, "sgx_ick");
		if (IS_ERR(psCLK))
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't get SGX Interface Clock"));
			goto ExitError;
		}
		psSysSpecData->psSGX_ICK = psCLK;

#if defined(DEBUG)
		psCLK = clk_get(NULL, "mpu_ck");
		if (IS_ERR(psCLK))
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't get MPU Clock"));
			goto ExitError;
		}
		psSysSpecData->psMPU_CK = psCLK;
#endif
		res = clk_set_parent(psSysSpecData->psSGX_FCK, psSysSpecData->psCORE_CK);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't set SGX parent clock (%d)", res));
			goto ExitError;
		}
	
		psSysSpecData->bSysClocksOneTimeInit = IMG_TRUE;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22))
	RegisterConstraintNotifications(psSysData, psSGXTimingInfo);
#endif

#if !defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	
	eError = EnableSGXClocks(psSysData);
	if (eError != PVRSRV_OK)
	{
		goto ExitUnRegisterConstraintNotifications;
	}
#endif

#if defined(DEBUG) || defined(TIMING)
	
	psCLK = clk_get(NULL, "gpt11_fck");
	if (IS_ERR(psCLK))
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't get GPTIMER11 functional clock"));
		goto ExitDisableSGXClocks;
	}
	psSysSpecData->psGPT11_FCK = psCLK;
	
	psCLK = clk_get(NULL, "gpt11_ick");
	if (IS_ERR(psCLK))
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't get GPTIMER11 interface clock"));
		goto ExitDisableSGXClocks;
	}
	psSysSpecData->psGPT11_ICK = psCLK;

	sys_ck = clk_get(NULL, "sys_ck");
	if (IS_ERR(sys_ck))
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't get System clock"));
		goto ExitDisableSGXClocks;
	}

	if(clk_get_parent(psSysSpecData->psGPT11_FCK) != sys_ck)
	{
		PVR_TRACE(("Setting GPTIMER11 parent to System Clock"));
		res = clk_set_parent(psSysSpecData->psGPT11_FCK, sys_ck);
		if (res < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't set GPTIMER11 parent clock (%d)", res));
		goto ExitDisableSGXClocks;
		}
	}

	rate = clk_get_rate(psSysSpecData->psGPT11_FCK);
	PVR_TRACE(("GPTIMER11 clock is %dMHz", HZ_TO_MHZ(rate)));
	
	res = clk_enable(psSysSpecData->psGPT11_FCK);
	if (res < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't enable GPTIMER11 functional clock (%d)", res));
		goto ExitDisableSGXClocks;
	}

	res = clk_enable(psSysSpecData->psGPT11_ICK);
	if (res < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: Couldn't enable GPTIMER11 interface clock (%d)", res));
		goto ExitDisableGPT11FCK;
	}
	
	
	TimerRegPhysBase.uiAddr = SYS_OMAP3430_GP11TIMER_TSICR_SYS_PHYS_BASE;
	pui32TimerEnable = OSMapPhysToLin(TimerRegPhysBase,
                  4,
                  PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
                  &hTimerEnable);

	if (pui32TimerEnable == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: OSMapPhysToLin failed"));
		goto ExitDisableGPT11ICK;
	}

	rate = *pui32TimerEnable;
	if(!(rate & 4))
	{
		PVR_TRACE(("Setting GPTIMER11 mode to posted (currently is non-posted)"));
		
		
		*pui32TimerEnable = rate | 4;
	}

	OSUnMapPhysToLin(pui32TimerEnable,
		    4,
		    PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
		    hTimerEnable);

	
	TimerRegPhysBase.uiAddr = SYS_OMAP3430_GP11TIMER_ENABLE_SYS_PHYS_BASE;
	pui32TimerEnable = OSMapPhysToLin(TimerRegPhysBase,
                  4,
                  PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
                  &hTimerEnable);

	if (pui32TimerEnable == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSystemClocks: OSMapPhysToLin failed"));
		goto ExitDisableGPT11ICK;
	}

	
	*pui32TimerEnable = 3;

	OSUnMapPhysToLin(pui32TimerEnable,
		    4,
		    PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
		    hTimerEnable);

#endif 

	return PVRSRV_OK;

#if defined(DEBUG) || defined(TIMING)
ExitDisableGPT11ICK:
	clk_disable(psSysSpecData->psGPT11_ICK);
ExitDisableGPT11FCK:
	clk_disable(psSysSpecData->psGPT11_FCK);
ExitDisableSGXClocks:
#if !defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	DisableSGXClocks(psSysData);
#endif
#endif	
#if !defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
ExitUnRegisterConstraintNotifications:
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22))	
	UnRegisterConstraintNotifications();
#endif	
ExitError:
	return PVRSRV_ERROR_GENERIC;
}

IMG_VOID DisableSystemClocks(SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;
#if defined(DEBUG) || defined(TIMING)
	IMG_CPU_PHYADDR TimerRegPhysBase;
	IMG_HANDLE hTimerDisable;
	IMG_UINT32 *pui32TimerDisable;
#endif	

	PVR_TRACE(("DisableSystemClocks: Disabling System Clocks"));

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22))
	UnRegisterConstraintNotifications();
#endif
	
	DisableSGXClocks(psSysData);

#if defined(DEBUG) || defined(TIMING)
	
	TimerRegPhysBase.uiAddr = SYS_OMAP3430_GP11TIMER_ENABLE_SYS_PHYS_BASE;
	pui32TimerDisable = OSMapPhysToLin(TimerRegPhysBase,
				4,
				PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
				&hTimerDisable);
	
	if (pui32TimerDisable == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "DisableSystemClocks: OSMapPhysToLin failed"));
	}
	else
	{
		*pui32TimerDisable = 0;
		
		OSUnMapPhysToLin(pui32TimerDisable,
				4,
				PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
				hTimerDisable);
	}

	clk_disable(psSysSpecData->psGPT11_ICK);

	clk_disable(psSysSpecData->psGPT11_FCK);

#endif 
}
