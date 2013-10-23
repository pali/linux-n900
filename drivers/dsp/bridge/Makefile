obj-$(CONFIG_MPU_BRIDGE)	+= bridgedriver.o

libgen = gen/gb.o gen/gs.o gen/gh.o gen/uuidutil.o
libservices = services/mem.o services/sync.o \
		services/clk.o services/cfg.o services/reg.o \
               services/regsup.o services/ntfy.o \
		services/services.o
libwmd = wmd/chnl_sm.o wmd/msg_sm.o wmd/io_sm.o wmd/tiomap3430.o \
		wmd/tiomap3430_pwr.o wmd/tiomap_io.o \
		wmd/mmu_fault.o wmd/ue_deh.o wmd/wdt.o
libpmgr = pmgr/chnl.o pmgr/io.o pmgr/msg.o pmgr/cod.o pmgr/dev.o pmgr/wcd.o \
		pmgr/dmm.o pmgr/cmm.o pmgr/dbll.o
librmgr = rmgr/dbdcd.o rmgr/disp.o rmgr/drv.o rmgr/mgr.o rmgr/node.o \
		rmgr/proc.o rmgr/pwr.o rmgr/rmm.o rmgr/strm.o rmgr/dspdrv.o \
		rmgr/nldr.o rmgr/drv_interface.o
librmgr += rmgr/policy.o
libdload = dynload/cload.o dynload/getsection.o dynload/reloc.o \
		 dynload/tramp.o
libhw = hw/hw_prcm.o hw/hw_dspssC64P.o hw/hw_mmu.o

bridgedriver-objs = $(libgen) $(libservices) $(libwmd) $(libpmgr) $(librmgr) \
			$(libdload) $(libhw)

#Machine dependent
ccflags-y += -D_TI_ -D_DB_TIOMAP -DTMS32060 \
		-DTICFG_PROC_VER -DTICFG_EVM_TYPE -DCHNL_SMCLASS \
		-DCHNL_MESSAGES -DUSE_LEVEL_1_MACROS

#Header files
ccflags-y += -Idrivers/dsp/bridge/services
ccflags-y += -Idrivers/dsp/bridge/wmd
ccflags-y += -Idrivers/dsp/bridge/pmgr
ccflags-y += -Idrivers/dsp/bridge/rmgr
ccflags-y += -Idrivers/dsp/bridge/hw
ccflags-y += -Iarch/arm
