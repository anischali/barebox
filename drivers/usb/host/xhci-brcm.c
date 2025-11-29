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

#define DRD2U3H_XHC_REGS_AXIWRA	0xC08
#define DRD2U3H_XHC_REGS_AXIRDA	0xC0C

#define USBAXI_CACHE		0xF
#define USBAXI_PROT		0x8
#define USBAXI_SA_MASK		0x1FF
#define USBAXI_UA_MASK		(0x1FF << 16)
#define USBAXI_SA_VAL		((USBAXI_CACHE << 4) | USBAXI_PROT)
#define USBAXI_UA_VAL		(USBAXI_SA_VAL << 16)
#define USBAXI_SA_UA_MASK	(USBAXI_UA_MASK | USBAXI_SA_MASK)
#define USBAXI_SA_UA_VAL	(USBAXI_UA_VAL | USBAXI_SA_VAL)

struct xhci_brcm {
	struct xhci_ctrl ctrl;
	struct device *dev;
	struct reset_control *reset;

	unsigned int arcache;
	unsigned int awcache;
};

static int xhci_brcm_probe(struct device *dev)
{
	struct xhci_brcm *xhci;
	struct xhci_ctrl *ctrl;
	struct resource *io;
	int ret;

	xhci = xzalloc(sizeof(*xhci));
	if (!xhci)
		return -ENOMEM;

	xhci->dev = dev;

	io = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(io)) {
		ret = PTR_ERR(io);
		dev_err(dev, "Failed to get IORESOURCE_MEM, err: %d\n", ret);
		return ret;
	}

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

	ctrl = &xhci->ctrl;	
	ctrl->dev = dev;
	dev->priv = xhci;
	ctrl->hccr = IOMEM(io->start);
	ctrl->hcor = (struct xhci_hcor *)((uintptr_t)ctrl->hccr + 
				HC_LENGTH(xhci_readl(&ctrl->hccr->cr_capbase)));

	/* Save the default values of AXI read and write attributes */
	xhci->awcache = readl(ctrl->hccr + DRD2U3H_XHC_REGS_AXIWRA);
	xhci->arcache = readl(ctrl->hccr + DRD2U3H_XHC_REGS_AXIRDA);

	/* Enable AXI write attributes */
	clrsetbits_le32(ctrl->hccr + DRD2U3H_XHC_REGS_AXIWRA,
			USBAXI_SA_UA_MASK, USBAXI_SA_UA_VAL);

	/* Enable AXI read attributes */
	clrsetbits_le32(ctrl->hccr + DRD2U3H_XHC_REGS_AXIRDA,
			USBAXI_SA_UA_MASK, USBAXI_SA_UA_VAL);

	ret = xhci_register(ctrl);
	if (ret) {
		pr_err("Failed to register xhci, err: %d\n", ret);
		return ret;
	}

	return 0;
}

static void xhci_brcm_remove(struct device *dev)
{
	struct xhci_brcm *xhci = dev->priv;
	struct xhci_ctrl *ctrl = &xhci->ctrl;

	/* Restore the default values for AXI read and write attributes */
	writel(xhci->awcache, ctrl->hccr + DRD2U3H_XHC_REGS_AXIWRA);
	writel(xhci->arcache, ctrl->hccr + DRD2U3H_XHC_REGS_AXIRDA);

	xhci_deregister(ctrl);
}

static const struct of_device_id xhci_brcm_of_ids[] = {
	{ .compatible = "brcm,generic-xhci",
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(of, xhci_brcm_of_ids);

static struct driver xhci_brcm_driver = {
	.name = "xhci-pci",
	.probe = xhci_brcm_probe,
	.remove = xhci_brcm_remove,
	.of_compatible = DRV_OF_COMPAT(xhci_brcm_of_ids),
};
device_platform_driver(xhci_brcm_driver);
