// SPDX-License-Identifier: GPL-2.0
/*
 * Raspberry Pi 4 firmware reset driver
 *
 * Copyright (C) 2020 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 * Copyright (C) 2025 Anis Chali <anis.chali@ro-main.com>

 */
#include <common.h>
#include <init.h>
#include <linux/err.h>
#include <linux/reset-controller.h>
#include <linux/reset/reset-simple.h>
#include <restart.h>
#include <reset_source.h>
#include <asm/io.h>
#include <mach/bcm283x/mbox.h>

#define RASPBERRYPI_FIRMWARE_RESET_ID_USB 0
#define RASPBERRYPI_FIRMWARE_RESET_NUM_IDS	1

struct rpi_reset {
	struct reset_controller_dev rcdev;
};

struct bcm2835_mbox_tag_pci_dev_addr {
	struct bcm2835_mbox_tag_hdr tag_hdr;
	union {
		struct {
			u32 dev_addr;
		} req;
		struct {
		} resp;
	} body;
};


struct msg_notify_xhci_reset {
	struct bcm2835_mbox_hdr hdr;
	struct bcm2835_mbox_tag_pci_dev_addr dev_addr;
	u32 end_tag;
};

static int rpi_firmware_notify_xhci_reset(void)
{
	int ret;
	BCM2835_MBOX_STACK_ALIGN(struct msg_notify_xhci_reset, msg_notify_vl805_reset);
	BCM2835_MBOX_INIT_HDR(msg_notify_vl805_reset);
	BCM2835_MBOX_INIT_TAG(&msg_notify_vl805_reset->dev_addr,
			      		NOTIFY_XHCI_RESET);

	/*
	 * The pci device address is expected like this:
	 *
	 *   PCI_BUS << 20 | PCI_SLOT << 15 | PCI_FUNC << 12
	 *
	 * But since RPi4's PCIe setup is hardwired, we know the address in
	 * advance.
	 */
	msg_notify_vl805_reset->dev_addr.body.req.dev_addr = 0x100000;

	ret = bcm2835_mbox_call_prop(BCM2835_MBOX_PROP_CHAN,
				     &msg_notify_vl805_reset->hdr);
	if (ret) {
		pr_err("Failed to notify vl805's firmware with xhci reset, %d\n", ret);
		return -EIO;
	}

	pr_info("xHCI reset successfull\n");

	udelay(200);

	return 0;
}

static int rpi_reset_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	int ret;

	switch (id) {
	case RASPBERRYPI_FIRMWARE_RESET_ID_USB:
		ret = rpi_firmware_notify_xhci_reset();
		if (ret)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct reset_control_ops rpi_reset_ops = {
	.reset	= rpi_reset_reset,
};

static int rpi_reset_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct rpi_reset *priv;

	if (!np) {
		dev_err(dev, "Missing firmware node\n");
		return -ENOENT;
	}

	priv = xzalloc(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	dev->priv = priv;
	priv->rcdev.nr_resets = RASPBERRYPI_FIRMWARE_RESET_NUM_IDS;
	priv->rcdev.ops = &rpi_reset_ops;
	priv->rcdev.of_node = dev->of_node;

	return reset_controller_register(&priv->rcdev);
}

static const struct of_device_id rpi_reset_of_match[] = {
	{ .compatible = "raspberrypi,firmware-reset" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rpi_reset_of_match);

static struct driver rpi_reset_driver = {
	.probe	= rpi_reset_probe,
	.name = "raspberrypi-reset",
	.of_compatible = DRV_OF_COMPAT(rpi_reset_of_match),
};
device_platform_driver(rpi_reset_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de>");
MODULE_AUTHOR("Anis Chali <chalianis1@gmail.com>");
MODULE_DESCRIPTION("Raspberry Pi 4 firmware reset driver");
MODULE_LICENSE("GPL");
