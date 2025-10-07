// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 *
 * derived from Linux kernel drivers/char/hw_random/core.c
 */

#include <common.h>
#include <linux/hw_random.h>
#include <malloc.h>
#include <crypto/pkcs11.h>

static LIST_HEAD(pkcs11_interfaces);

#define RNG_BUFFER_SIZE		32

static int pkcs11_init(struct pkcs11 *cki)
{
	int ret = 0;

	if (cki->ops && cki->ops->init)
		ret = cki->ops->init(cki);

	if (!ret)
		list_add_tail(&cki->list, &pkcs11_interfaces);

	return ret;
}

static int pkcs11_ioctl(struct cdev *cdev, unsigned int ioctl, void *args)
{
    return 0;
}

static int pkcs11_open(struct cdev *cdev, unsigned long flags)
{
    return 0;
}

static int pkcs11_close(struct cdev *cdev)
{
    return 0;
}

static struct cdev_operations rng_chrdev_ops = {
	.open = pkcs11_open,
    .close = pkcs11_close,
    .ioctl = pkcs11_ioctl,
};

static int pkcs11_register_cdev(struct pkcs11 *cki)
{
	struct device *dev = cki->dev;
	const char *alias;
	char *devname;
	int err;

	alias = of_alias_get(dev->of_node);
	if (alias) {
		devname = xstrdup(alias);
	} else {
		err = cdev_find_free_index("pkcs11");
		if (err < 0) {
			dev_err(dev, "no index found to name device\n");
			return err;
		}
		devname = xasprintf("pkcs11-%d", err);
	}

	cki->cdev.name = devname;
	cki->cdev.flags = DEVFS_IS_CHARACTER_DEV;
	cki->cdev.ops = &rng_chrdev_ops;
	cki->cdev.dev = cki->dev;

	return devfs_create(&cki->cdev);
}

static void pkcs11_unregister_cdev(struct pkcs11 *cki)
{
	devfs_remove(&cki->cdev);
	free(cki->cdev.name);
}

struct pkcs11 *pkcs11_get_first(void)
{
	if (list_empty(&pkcs11_interfaces))
		return ERR_PTR(-ENODEV);
	else
		return list_first_entry(&pkcs11_interfaces, struct pkcs11, list);
}

int pkcs11_register(struct device *dev, struct pkcs11 *cki)
{
	int err;

	if (cki->name == NULL || cki->ops == NULL)
		return -EINVAL;

	cki->dev = dev;

	err = pkcs11_init(cki);
	if (err) {
		return err;
	}

	err = pkcs11_register_cdev(cki);
	if (err)
        return err;

	return 0;
}

void pkcs11_unregister(struct pkcs11 *cki)
{
	pkcs11_unregister_cdev(cki);
}
