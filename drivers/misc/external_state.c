// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (C) 2025 Anis Chali <anis.chali#ro-main.com>, Ro-Main
 */

#include <common.h>
#include <driver.h>
#include <init.h>
#include <state.h>
#include <libfile.h>

#include <linux/err.h>

static int state_external_init(void)
{
	const char *dt_path = CONFIG_EXTERNAL_STATE_DTB_PATH;
	struct device_node *state_root = NULL;
	size_t size;
	void *fdt;
	int ret;

	if (strlen(dt_path) <= 0)
		return -EINVAL;

	fdt = read_file(dt_path, &size);
	if (!fdt) {
		pr_info("unable to read %s: %m\n", dt_path);
		return 0;
	}

	state_root = of_unflatten_dtb(fdt, size);
	if (!IS_ERR(state_root)) {
		struct device_node *np = NULL;
		struct state *state;

		ret = barebox_register_of(state_root);
		if (ret)
			pr_warn("Failed to register device-tree: %pe\n", ERR_PTR(ret));

		np = of_find_node_by_alias(state_root, "state");

		state = state_new_from_node(np, false);
		if (IS_ERR(state))
			return PTR_ERR(state);

		ret = state_load(state);
		if (ret != -ENOMEDIUM)
			pr_warn("Failed to load persistent state, continuing with defaults, %d\n",
				ret);

		return 0;
	}

	return -EINVAL;
}

late_initcall(state_external_init);
