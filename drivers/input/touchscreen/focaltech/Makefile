#
# Makefile for the input core drivers.
#
# Each configuration option enables a list of files.
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH)  += focaltech_ts.o
#focaltech_ts-objs += focaltech.o focaltech_ctl.o focaltech_ex_fun.o
focaltech_ts-objs := focaltech.o
focaltech_ts-objs += focaltech_ctl.o
focaltech_ts-objs += focaltech_ex_fun.o
$(obj)/ft_gesture_lib.o:$(srctree)/$(obj)/ft_gesture_lib.a
	cp $(srctree)/$(obj)/ft_gesture_lib.a $(obj)/ft_gesture_lib.o
focaltech_ts-objs += ft_gesture_lib.o
