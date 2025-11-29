// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) Copyright 2010 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 */

#include <common.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <driver.h>
#include <init.h>
#include <linux/pci.h>
#include <regulator.h>
#include <linux/bitfield.h>
#include <linux/usb/usb.h>
#include <linux/usb/xhci.h>
#include <linux/reset.h>
#include <errno.h>
#include <io.h>

#include "xhci.h"

struct xhci_pci {
	struct xhci_ctrl ctrl;
	struct device *dev;
	struct reset_control *reset;
};

static int xhci_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct xhci_pci *xhci;
	struct xhci_ctrl *ctrl;
	int ret, region;

	xhci = xzalloc(sizeof(*xhci));
	if (!xhci)
		return -ENOMEM;

	xhci->dev = dev;

	xhci->reset = reset_control_get_optional(dev->parent, "xhci-reset");
	if (IS_ERR(xhci->reset)) {
		ret = PTR_ERR(xhci->reset);
		pr_err("Failed to get reset, err: %d\n", ret);
		return ret;
	}

	if (xhci->reset) {
		ret = reset_control_reset(xhci->reset);
		if (ret) {
			pr_err("Failed to reset, err: %d\n", ret);
			return ret;
		}
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		pr_err("Failed to enable device, err: %d\n", ret);
	}

	pci_set_master(pdev);
	
	region = ffs(pci_select_bars(pdev, IORESOURCE_MEM)) - 1;
	if (region < 0) {
		dev_err(&pdev->dev, "no MMIO resource found\n");
		return -ENODEV;
	}

	ctrl = &xhci->ctrl;	
	ctrl->dev = dev;
	dev->priv = ctrl;
	ctrl->hccr = pci_iomap(pdev, region);
	ctrl->hcor = (struct xhci_hcor *)((uintptr_t)ctrl->hccr + 
				HC_LENGTH(xhci_readl(&ctrl->hccr->cr_capbase)));

	ret = xhci_register(ctrl);
	if (ret) {
		pr_err("Failed to register xhci, err: %d\n", ret);
		return ret;
	}

	return 0;
}

static void xhci_pci_remove(struct pci_dev *pdev)
{
	struct xhci_ctrl *ctrl = pdev->dev.priv;

	xhci_deregister(ctrl);
}

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id xhci_pci_ids[] = {
	/* handle any USB xHCI controller */
	{ PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_XHCI, ~0),
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, xhci_pci_ids);

static struct pci_driver xhci_pci_driver = {
	.name = "xhci-pci",
	.probe = xhci_pci_probe,
	.remove = xhci_pci_remove,
	.id_table = xhci_pci_ids,
};
device_pci_driver(xhci_pci_driver);
