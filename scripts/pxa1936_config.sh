#!/bin/sh

./scripts/config --set-val NR_CPUS 8
./scripts/config -d CONFIG_NO_HOTPLUG_POLICY
./scripts/config -d CPU_HOTPLUG_POL_STANDALONE
./scripts/config -d CONFIG_CPU_IDLE
./scripts/config -d CONFIG_CPU_IDLE_MMP_V8
./scripts/config -d CONFIG_CPU_FREQ
./scripts/config -d ARM_MMP_SMP_CPUFREQ
./scripts/config -d CONFIG_CPU_THERMAL
./scripts/config -d CONFIG_PXA1928_THERMAL
./scripts/config -d CONFIG_HELAN2_THERMAL
./scripts/config -d CONFIG_PXA_DVFS
./scripts/config -d CONFIG_PM_DEVFREQ
./scripts/config -e PXA1936_CLK
