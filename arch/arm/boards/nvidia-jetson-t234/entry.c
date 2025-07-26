// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2014 Lucas Stach <l.stach@pengutronix.de>

#include <common.h>
#include <mach/tegra/lowlevel.h>
#include <mach/tegra/lowlevel-dvc.h>

extern char __dtb_tegra234_jetson_t23x_start[];

ENTRY_FUNCTION(start_nvidia_jetson_t23x, r0, r1, r2)
{
	tegra_cpu_lowlevel_setup(__dtb_tegra234_jetson_t23x_start);

	tegra_dvc_init();
	tegra124_dvc_pinmux();
	tegra124_as3722_enable_essential_rails(0x3c00);

	tegra_avp_reset_vector();
}
