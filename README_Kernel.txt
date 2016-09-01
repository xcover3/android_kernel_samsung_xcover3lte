################################################################################
HOW TO BUILD KERNEL FOR SM-G388F_EUR_LL_XX

1. How to Build
	- get Toolchain
	download and install arm-eabi-4.8 toolchain for ARM EABI.(64bit)
	Extract kernel source and move into the top directory.

	$ make ARCH=arm64 pxa1908_xcover3lte_eur_defconfig
	$ make ARCH=arm64
	
2. Output files
	- Kernel : Kernel/arch/arm/boot/zImage
	- module : Kernel/drivers/*/*.ko
	
3. How to Clean	
    $ make clean
	
4. How to make .tar binary for downloading into target.
	- change current directory to Kernel/arch/arm/boot
	- type following command
	$ tar cvf SM-G388F_EUR_LL_XX_Kernel.tar zImage
#################################################################################