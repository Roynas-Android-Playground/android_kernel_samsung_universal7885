ccflags-y += -O3
obj-$(CONFIG_PM_DEVFREQ)	+= devfreq.o
obj-$(CONFIG_PM_DEVFREQ_EVENT)	+= devfreq-event.o
obj-$(CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND)	+= governor_simpleondemand.o
obj-$(CONFIG_DEVFREQ_GOV_SIMPLE_EXYNOS)	+= governor_simpleexynos.o
obj-$(CONFIG_DEVFREQ_GOV_SIMPLE_INTERACTIVE)	+= governor_simpleinteractive.o
obj-$(CONFIG_DEVFREQ_GOV_SIMPLE_USAGE)	+= governor_simpleusage.o
obj-$(CONFIG_DEVFREQ_GOV_PERFORMANCE)	+= governor_performance.o
obj-$(CONFIG_DEVFREQ_GOV_POWERSAVE)	+= governor_powersave.o
obj-$(CONFIG_DEVFREQ_GOV_USERSPACE)	+= governor_userspace.o

# DEVFREQ Drivers
obj-$(CONFIG_ARM_EXYNOS4_BUS_DEVFREQ)	+= exynos/
obj-$(CONFIG_ARM_EXYNOS5_BUS_DEVFREQ)	+= exynos/
obj-$(CONFIG_ARM_TEGRA_DEVFREQ)		+= tegra-devfreq.o

# DEVFREQ Event Drivers
obj-$(CONFIG_PM_DEVFREQ_EVENT)		+= event/
obj-$(CONFIG_ARM_EXYNOS_DEVFREQ)	+= exynos/

# DEVFREQ Boost
obj-$(CONFIG_DEVFREQ_BOOST)		+= devfreq_boost.o
