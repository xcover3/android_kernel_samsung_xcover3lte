#Android makefile to build kernel as a part of Android Build
#ifeq ($(TARGET_USE_MARVELL_KERNEL),true)

# Give other modules a nice, symbolic name to use as a dependent
# Yes, there are modules that cannot build unless the kernel has
# been built. Typical (only?) example: loadable kernel modules.
.PHONY: build-kernel clean-kernel

KERNEL_OUTPUT := $(abspath $(PRODUCT_OUT)/obj/kernel)
UBOOT_OUTPUT ?=  $(abspath $(PRODUCT_OUT)/obj/uboot)

#TODO: remove ARCH definition in pxa1928 device BoardConfig.mk and use TARGET_ARCH
#TODO: when android support arm64
ARCH ?= arm
ifeq ($(ARCH),arm64)
export PATH:=$(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.8/bin:$(PATH)
KERNEL_CROSS_COMPILE := aarch64-linux-android-
else
KERNEL_CROSS_COMPILE := $(CROSS_COMPILE)
endif

PRIVATE_KERNEL_ARGS := -C kernel ARCH=$(ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE)

PRIVATE_OUT := $(abspath $(PRODUCT_OUT)/root)

KERNEL_DTB_FILE ?= $(TARGET_DEVICE).dtb



#dtb padded to 128k
DTB_PADDING_BOOTIMG_SIZE := 131072

# only do this if we are buidling out of tree
ifneq ($(KERNEL_OUTPUT),)
ifneq ($(KERNEL_OUTPUT), $(abspath $(TOP)/kernel))
PRIVATE_KERNEL_ARGS += O=$(KERNEL_OUTPUT)
endif
else
KERNEL_OUTPUT := $(call my-dir)
endif

build-kernel: $(KERNEL_IMAGE)

LOCAL_PATH := kernel/scripts/dtc
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := kernel/scripts/dtc/libfdt
LOCAL_SRC_FILES := \
	srcpos.c \
	fstree.c \
	dtc.c \
	checks.c \
	livetree.c \
	data.c \
	util.c \
	treesource.c \
	flattree.c \
	../../../$(PRODUCT_OUT)/obj/kernel/scripts/dtc/dtc-lexer.lex.c \
	../../../$(PRODUCT_OUT)/obj/kernel/scripts/dtc/dtc-parser.tab.c
LOCAL_MODULE := dtc
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := EXECUTABLES
$(LOCAL_PATH)/../../../$(PRODUCT_OUT)/obj/kernel/scripts/dtc/dtc-lexer.lex.c : build-kernel
$(LOCAL_PATH)/../../../$(PRODUCT_OUT)/obj/kernel/scripts/dtc/dtc-parser.tab.c : build-kernel
include $(BUILD_EXECUTABLE)

# Include kernel in the Android build system
ifeq ($(KERNEL_IMAGE),zImage)
include $(CLEAR_VARS)

KERNEL_LIBPATH := $(KERNEL_OUTPUT)/arch/$(ARCH)/boot
LOCAL_PATH := $(KERNEL_LIBPATH)
LOCAL_SRC_FILES := zImage
LOCAL_MODULE := $(LOCAL_SRC_FILES)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)

include $(BUILD_PREBUILT)
endif

ifeq ($(KERNEL_IMAGE),uImage)
include $(CLEAR_VARS)

KERNEL_LIBPATH := $(KERNEL_OUTPUT)/arch/$(ARCH)/boot
LOCAL_PATH := $(KERNEL_LIBPATH)
LOCAL_SRC_FILES := uImage
LOCAL_MODULE := $(LOCAL_SRC_FILES)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)

include $(BUILD_PREBUILT)
endif

include $(CLEAR_VARS)
KERNEL_LIBPATH := $(KERNEL_OUTPUT)
LOCAL_PATH := $(KERNEL_LIBPATH)
LOCAL_SRC_FILES := vmlinux
LOCAL_MODULE := $(LOCAL_SRC_FILES)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)
$(KERNEL_LIBPATH)/$(LOCAL_SRC_FILES): build-kernel
include $(BUILD_PREBUILT)

#for boot image
$(PRODUCT_OUT)/kernel: $(KERNEL_IMAGE)
	ln -sf $^ $@

# Configures, builds and installs the kernel. KERNEL_DEFCONFIG usually
# comes from the BoardConfig.mk file, but can be overridden on the
# command line or by an environment variable.
# If KERNEL_DEFCONFIG is set to 'local', configuration is skipped.
# This is useful if you want to play with your own, custom configuration.

$(PRODUCT_OUT)/ramdisk.img: build-kernel

droidcore: $(KERNEL_IMAGE) vmlinux

dtb_files:= $(foreach n, $(KERNEL_DTB_FILE), $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/dts/$(n))
local_dtb_files = $(foreach n, $(KERNEL_DTB_FILE), $(PRODUCT_OUT)/$(n))
local_dtb_files_padded = $(foreach n, $(KERNEL_DTB_FILE), $(ANDROID_PRODUCT_OUT)/$(n).padded)

$(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE): FORCE

# only do this if we are buidling out of tree
ifneq ($(KERNEL_OUTPUT),)
ifneq ($(KERNEL_OUTPUT), $(abspath $(TOP)/kernel))
	@mkdir -p $(KERNEL_OUTPUT)
	@mkdir -p $(KERNEL_OUTPUT)/root
endif
endif
	echo "KERNEL_DEFCONFIG is "+$(KERNEL_DEFCONFIG)+"PRIVATE_KERNEL_ARGS is "+$(PRIVATE_KERNEL_ARGS)
ifeq ($(KERNEL_DEFCONFIG),local)
	@echo Skipping kernel configuration, KERNEL_DEFCONFIG set to local
else
	echo "KERNEL_DEFCONFIG is "+$(KERNEL_DEFCONFIG)+"PRIVATE_KERNEL_ARGS is "+$(PRIVATE_KERNEL_ARGS)
	$(MAKE) $(PRIVATE_KERNEL_ARGS) $(KERNEL_DEFCONFIG)
ifeq ($(HAVE_SECURITY_TZ_FEATURE),true)
	$(TOP)/kernel/scripts/config --file $(KERNEL_OUTPUT)/.config -e CONFIG_TZ_HYPERVISOR
endif
endif
	echo "PRIVATE_KERNEL_ARGS is "+$(PRIVATE_KERNEL_ARGS)
	$(MAKE) $(PRIVATE_KERNEL_ARGS)

ifeq ($(KERNEL_NO_MODULES),)
	echo "PRIVATE_KERNEL_ARGS is KERNEL_NO_MODULES is blank "+$(PRIVATE_KERNEL_ARGS)
	$(MAKE) $(PRIVATE_KERNEL_ARGS) modules
	$(MAKE) $(PRIVATE_KERNEL_ARGS) INSTALL_MOD_PATH:=$(PRIVATE_OUT) modules_install
else
	@echo Skipping building of kernel modules, KERNEL_NO_MODULES set
endif
	echo "cp vmlinux "
	cp -u $(KERNEL_OUTPUT)/vmlinux $(PRODUCT_OUT)
	cp -u $(KERNEL_OUTPUT)/System.map $(PRODUCT_OUT)
	cp -u $(dtb_files) $(PRODUCT_OUT)

	for tmp_dtbfile in $(local_dtb_files) ;\
	do \
		echo $${tmp_dtbfile} && \
		cp  $${tmp_dtbfile} $${tmp_dtbfile}.orig && \
		cat $${tmp_dtbfile}.orig /dev/zero |head -c $(DTB_PADDING_BOOTIMG_SIZE) > $${tmp_dtbfile}.padded; \
	done

	if [ -e $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/zImage ]; then cp -u $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/zImage $(PRODUCT_OUT); fi
ifeq ($(ARCH),arm64)
	$(UBOOT_OUTPUT)/tools/mkimage -A arm64 -O linux -C gzip -a 0x01280000 -e 0x01280000 -n "$(TARGET_DEVICE) linux" -d $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/Image.gz $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE)
else
	$(MAKE) $(PRIVATE_KERNEL_ARGS) $(KERNEL_IMAGE)
endif
	cat $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE) /dev/zero|head -c `expr \`ls -l $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE) | awk -F' ' '{print $$5}'\` + 2048 - \`ls -l $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE) | awk -F' ' '{print $$5}'\` % 2048` > $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE).padded
	cat $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE).padded ${local_dtb_files_padded} > $(KERNEL_OUTPUT)/arch/$(ARCH)/boot/$(KERNEL_IMAGE)

# Configures and runs menuconfig on the kernel based on
# KERNEL_DEFCONFIG given on commandline or in BoardConfig.mk.
# The build after running menuconfig must be run with
# KERNEL_DEFCONFIG=local to not override the configuration modification done.

menuconfig-kernel:
# only do this if we are buidling out of tree
ifneq ($(KERNEL_OUTPUT),)
ifneq ($(KERNEL_OUTPUT), $(abspath $(TOP)/kernel))
	@mkdir -p $(KERNEL_OUTPUT)
endif
endif
	$(MAKE) $(PRIVATE_KERNEL_ARGS) $(KERNEL_DEFCONFIG)
	$(MAKE) $(PRIVATE_KERNEL_ARGS) menuconfig

clean clobber : clean-kernel

clean-kernel:
	$(MAKE) $(PRIVATE_KERNEL_ARGS) clean
#endif
