#
# Makefile for mmp specific clk
#

obj-y += clk-apbc.o clk-apmu.o clk-frac.o clk-mix.o clk-gate.o clk.o clk-gate2.o clk-pll-pxa1928.o clk-core-pxa1928.o clk-core-helanx.o clk-pll-helanx.o clk-plat-comm.o simple_dip.o clk-audio-map.o

ifneq ($(CONFIG_OF),)
obj-y += clk-pxa1U88.o clk-pxa1928.o clk-pxa1L88.o
endif

obj-$(CONFIG_CPU_PXA168) += clk-pxa168.o
obj-$(CONFIG_CPU_PXA910) += clk-pxa910.o
obj-$(CONFIG_CPU_MMP2) += clk-mmp2.o

ifeq ($(CONFIG_PXA_DVFS), y)
obj-y += dvfs.o dvfs-dvc.o hwdvc-stat.o dvfs-pxa1u88.o dvfs-pxa1L88.o dvfs-pxa1928.o
endif

obj-$(CONFIG_DEBUG_FS) += clk-dcstat.o
