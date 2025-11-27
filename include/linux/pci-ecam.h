/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2016 Broadcom
 */
#ifndef DRIVERS_PCI_ECAM_H
#define DRIVERS_PCI_ECAM_H

#include <linux/pci.h>
#include <linux/kernel.h>

/*
 * Memory address shift values for the byte-level address that
 * can be used when accessing the PCI Express Configuration Space.
 */

/*
 * Enhanced Configuration Access Mechanism (ECAM)
 *
 * See PCI Express Base Specification, Revision 5.0, Version 1.0,
 * Section 7.2.2, Table 7-1, p. 677.
 */
#define PCIE_ECAM_BUS_SHIFT	20 /* Bus number */
#define PCIE_ECAM_DEVFN_SHIFT	12 /* Device and Function number */

#define PCIE_ECAM_BUS_MASK	0xff
#define PCIE_ECAM_DEVFN_MASK	0xff
#define PCIE_ECAM_REG_MASK	0xfff /* Limit offset to a maximum of 4K */

#define PCIE_ECAM_BUS(x)	(((x) & PCIE_ECAM_BUS_MASK) << PCIE_ECAM_BUS_SHIFT)
#define PCIE_ECAM_DEVFN(x)	(((x) & PCIE_ECAM_DEVFN_MASK) << PCIE_ECAM_DEVFN_SHIFT)
#define PCIE_ECAM_REG(x)	((x) & PCIE_ECAM_REG_MASK)

#define PCIE_ECAM_OFFSET(bus, devfn, where) \
	(PCIE_ECAM_BUS(bus) | \
	 PCIE_ECAM_DEVFN(devfn) | \
	 PCIE_ECAM_REG(where))

#endif