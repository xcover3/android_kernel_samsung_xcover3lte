#!/bin/sh

# NOTE:
# Keep the full config option name here with *CONFIG_*

./scripts/config --set-val CONFIG_NR_CPUS 8
./scripts/config -d CONFIG_NO_HOTPLUG_POLICY
./scripts/config -d CPU_HOTPLUG_POL_STANDALONE
./scripts/config -d CONFIG_CPU_IDLE
./scripts/config -d CONFIG_CPU_IDLE_MMP_V8
./scripts/config -e CONFIG_ARM_MMP_BL_CPUFREQ
./scripts/config -d CONFIG_ARM_MMP_SMP_CPUFREQ
./scripts/config -d CONFIG_PXA1928_THERMAL
./scripts/config -d CONFIG_HELAN2_THERMAL
./scripts/config -d CONFIG_PXA_DVFS
./scripts/config -e CONFIG_PXA1936_CLK
./scripts/config -d CONFIG_ANDROID_BINDER_IPC_32BIT
