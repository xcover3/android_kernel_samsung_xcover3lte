#
# Makefile for Mstar Touchscreen shared transport driver based on  Input Touchscreen
#
obj-$(CONFIG_TOUCHSCREEN_MSTAR)	+= mstar_drv.o
mstar_drv-objs		:= mstar_drv_marvell.o mstar_drv_main.o mstar_drv_common.o mstar_drv_utility_adaption.o \
					   mstar_drv_platform_interface.o mstar_drv_self_fw_control.o \
					   mstar_drv_platform_porting_layer.o mstar_drv_ic_fw_porting_layer.o hipad_firmware_upgrade.o
