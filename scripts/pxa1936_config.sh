#!/bin/sh

# NOTE:
# Keep the full config option name here with *CONFIG_*

./scripts/config --set-val CONFIG_NR_CPUS 8
./scripts/config -d CONFIG_NO_HOTPLUG_POLICY
./scripts/config -d CPU_HOTPLUG_POL_STANDALONE
./scripts/config -e CONFIG_ARM_MMP_BL_CPUFREQ
./scripts/config -d CONFIG_ARM_MMP_SMP_CPUFREQ
./scripts/config -d CONFIG_PXA1928_THERMAL
./scripts/config -d CONFIG_HELAN2_THERMAL
./scripts/config -e CONFIG_PXA1936_CLK
./scripts/config -d CONFIG_ANDROID_BINDER_IPC_32BIT
./scripts/config -d SCHED_AUTOGROUP
./scripts/config -e SCHED_MC
./scripts/config -e DISABLE_CPU_SCHED_DOMAIN_BALANCE
./scripts/config -e SCHED_HMP
./scripts/config -d SCHED_HMP_PRIO_FILTER
./scripts/config --set-str HMP_FAST_CPU_MASK 4-7
./scripts/config --set-str HMP_SLOW_CPU_MASK 0-3
./scripts/config -e HMP_VARIABLE_SCALE
./scripts/config -d HMP_FREQUENCY_INVARIANT_SCALE
./scripts/config -e SCHED_HMP_LITTLE_PACKING
