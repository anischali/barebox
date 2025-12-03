// SPDX-License-Identifier: GPL-2.0
/*
 * Broadcom STB PCIe controller driver
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd.
 *
 * Based on upstream Linux kernel driver:
 * drivers/pci/controller/pcie-brcmstb.c
 * Copyright (C) 2009 - 2017 Broadcom
 *
 * Based driver by Nicolas Saenz Julienne
 * Copyright (C) 2020 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */

#include <common.h>
#include <malloc.h>
#include <io.h>
#include <of.h>
#include <of_address.h>
#include <init.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/pci-ecam.h>
#include <linux/log2.h>
#include <linux/iopoll.h>
#include <linux/bitfield.h>

/* BRCM_PCIE_CAP_REGS - Offset for the mandatory capability config regs */
#define BRCM_PCIE_CAP_REGS				0x00ac

/* Broadcom STB PCIe Register Offsets */
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1				0x0188
#define  PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK	0xc
#define  PCIE_RC_CFG_VENDOR_SPCIFIC_REG1_LITTLE_ENDIAN			0x0

#define PCIE_RC_CFG_PRIV1_ID_VAL3			0x043c
#define  PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK	0xffffff

#define PCIE_RC_CFG_PRIV1_LINK_CAPABILITY			0x04dc
#define  PCIE_RC_CFG_PRIV1_LINK_CAPABILITY_ASPM_SUPPORT_MASK	0xc00

#define PCIE_RC_DL_MDIO_ADDR				0x1100
#define PCIE_RC_DL_MDIO_WR_DATA				0x1104
#define PCIE_RC_DL_MDIO_RD_DATA				0x1108

#define PCIE_MISC_MISC_CTRL				0x4008
#define  PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK		0x1000
#define  PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK	0x2000
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK	0x300000
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_128		0x0
#define  PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK		0xf8000000

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO		0x400c
#define PCIE_MEM_WIN0_LO(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO + ((win) * 8)

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI		0x4010
#define PCIE_MEM_WIN0_HI(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI + ((win) * 8)

#define PCIE_MISC_RC_BAR1_CONFIG_LO			0x402c
#define  PCIE_MISC_RC_BAR1_CONFIG_LO_SIZE_MASK		0x1f

#define PCIE_MISC_RC_BAR2_CONFIG_LO			0x4034
#define  PCIE_MISC_RC_BAR2_CONFIG_LO_SIZE_MASK		0x1f
#define PCIE_MISC_RC_BAR2_CONFIG_HI			0x4038

#define PCIE_MISC_RC_BAR3_CONFIG_LO			0x403c
#define  PCIE_MISC_RC_BAR3_CONFIG_LO_SIZE_MASK		0x1f

#define PCIE_MISC_MSI_BAR_CONFIG_LO			0x4044
#define PCIE_MISC_MSI_BAR_CONFIG_HI			0x4048

#define PCIE_MISC_MSI_DATA_CONFIG			0x404c
#define  PCIE_MISC_MSI_DATA_CONFIG_VAL			0xffe06540

#define PCIE_MISC_PCIE_CTRL				0x4064
#define  PCIE_MISC_PCIE_CTRL_PCIE_L23_REQUEST_MASK	0x1

#define PCIE_MISC_PCIE_STATUS				0x4068
#define  PCIE_MISC_PCIE_STATUS_PCIE_PORT_MASK		0x80
#define  PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK	0x20
#define  PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK	0x10
#define  PCIE_MISC_PCIE_STATUS_PCIE_LINK_IN_L23_MASK	0x40

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT		0x4070
#define  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK	0xfff00000
#define  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK	0xfff0
#define PCIE_MEM_WIN0_BASE_LIMIT(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT + ((win) * 4)

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI			0x4080
#define  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI_BASE_MASK	0xff
#define PCIE_MEM_WIN0_BASE_HI(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI + ((win) * 8)

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI			0x4084
#define  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI_LIMIT_MASK	0xff
#define PCIE_MEM_WIN0_LIMIT_HI(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI + ((win) * 8)

#define PCIE_MISC_HARD_PCIE_HARD_DEBUG					0x4204
#define  PCIE_MISC_HARD_PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE_MASK	0x2
#define  PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK		0x08000000

#define PCIE_MSI_INTR2_STATUS				0x4500
#define PCIE_MSI_INTR2_CLR				0x4508
#define PCIE_MSI_INTR2_MASK_SET				0x4510
#define PCIE_MSI_INTR2_MASK_CLR				0x4514

#define PCIE_EXT_CFG_DATA				0x8000

#define PCIE_EXT_CFG_INDEX				0x9000
#define  PCIE_EXT_BUSNUM_SHIFT				20
#define  PCIE_EXT_SLOT_SHIFT				15
#define  PCIE_EXT_FUNC_SHIFT				12

#define PCIE_RGR1_SW_INIT_1				0x9210
#define  PCIE_RGR1_SW_INIT_1_PERST_MASK			0x1
#define  PCIE_RGR1_SW_INIT_1_INIT_MASK			0x2

/* PCIe parameters */
#define BRCM_NUM_PCIE_OUT_WINS		0x4
#define BRCM_INT_PCI_MSI_NR		32

/* MSI target adresses */
#define BRCM_MSI_TARGET_ADDR_LT_4GB	0x0fffffffcULL
#define BRCM_MSI_TARGET_ADDR_GT_4GB	0xffffffffcULL

/* MDIO registers */
#define MDIO_PORT0			0x0
#define MDIO_DATA_MASK			0x7fffffff
#define MDIO_PORT_MASK			0xf0000
#define MDIO_REGAD_MASK			0xffff
#define MDIO_CMD_MASK			0xfff00000
#define MDIO_CMD_READ			0x1
#define MDIO_CMD_WRITE			0x0
#define MDIO_DATA_DONE_MASK		0x80000000
#define MDIO_RD_DONE(x)			(((x) & MDIO_DATA_DONE_MASK) ? 1 : 0)
#define MDIO_WT_DONE(x)			(((x) & MDIO_DATA_DONE_MASK) ? 0 : 1)
#define SSC_REGS_ADDR			0x1100
#define SET_ADDR_OFFSET			0x1f
#define SSC_CNTL_OFFSET			0x2
#define SSC_CNTL_OVRD_EN_MASK		0x8000
#define SSC_CNTL_OVRD_VAL_MASK		0x4000
#define SSC_STATUS_OFFSET		0x1
#define SSC_STATUS_SSC_MASK		0x400
#define SSC_STATUS_PLL_LOCK_MASK	0x800


/**
 * struct brcm_pcie - the PCIe controller state
 * @base: Base address of memory mapped IO registers of the controller
 * @gen: Non-zero value indicates limitation of the PCIe controller operation
 *       to a specific generation (1, 2 or 3)
 * @ssc: true indicates active Spread Spectrum Clocking operation
 */
struct brcm_pcie {
    int gen;
	bool ssc;
    int first_busno;
	void __iomem *base;
    struct pci_controller pci;
    struct resource *cfg;
    struct resource io;
	struct resource mem;
	struct resource prefetch;
};

static inline struct brcm_pcie *host_to_brcm(struct pci_controller *host)
{
	return container_of(host, struct brcm_pcie, pci);
}

/**
 * brcm_pcie_encode_ibar_size() - Encode the inbound "BAR" region size
 * @size: The inbound region size
 *
 * This function converts size of the inbound "BAR" region to the non-linear
 * values of the PCIE_MISC_RC_BAR[123]_CONFIG_LO register SIZE field.
 *
 * Return: The encoded inbound region size
 */
static int brcm_pcie_encode_ibar_size(u64 size)
{
	int log2_in = ilog2(size);

	if (log2_in >= 12 && log2_in <= 15)
		/* Covers 4KB to 32KB (inclusive) */
		return (log2_in - 12) + 0x1c;
	else if (log2_in >= 16 && log2_in <= 37)
		/* Covers 64KB to 32GB, (inclusive) */
		return log2_in - 15;

	/* Something is awry so disable */
	return 0;
}

/* The controller is capable of serving in both RC and EP roles */
static bool brcm_pcie_rc_mode(struct brcm_pcie *pcie)
{
	void __iomem *base = pcie->base;
	u32 val = readl(base + PCIE_MISC_PCIE_STATUS);

	return !!FIELD_GET(PCIE_MISC_PCIE_STATUS_PCIE_PORT_MASK, val);
}

static bool brcm_pcie_link_up(struct brcm_pcie *pcie)
{
	u32 val = readl(pcie->base + PCIE_MISC_PCIE_STATUS);
	u32 dla = FIELD_GET(PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK, val);
	u32 plu = FIELD_GET(PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK, val);

	return dla && plu;
}

static void __iomem *brcm_pcie_map_bus(struct pci_bus *bus,
				       unsigned int devfn, int offset)
{
    struct brcm_pcie *pcie = host_to_brcm(bus->host);
	void __iomem *base = pcie->base;
	int idx;

    /* Busses 0 (host PCIe bridge) and 1 (its immediate child)
	 * are limited to a single device each
	 */
	if (bus->number < 2 && PCI_SLOT(devfn) > 0)
		return NULL;

	/* Accesses to the RC go right to the RC registers if !devfn */
	if (!bus->number)
		return base + PCIE_ECAM_REG(offset);

	/* An access to our HW w/o link-up will cause a CPU Abort */
	if (!brcm_pcie_link_up(pcie))
		return NULL;

	/* For devices, write to the config space index register */
	idx = PCIE_ECAM_OFFSET(bus->number, devfn, 0);
	writel(idx, base + PCIE_EXT_CFG_INDEX);

	return base + PCIE_EXT_CFG_DATA + PCIE_ECAM_REG(offset);
}


static int brcm_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
    void __iomem *addr;

	addr = brcm_pcie_map_bus(bus, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 1)
		*val = readb(addr);
	else if (size == 2)
		*val = readw(addr);
	else
		*val = readl(addr);

	return PCIBIOS_SUCCESSFUL;
}

static int brcm_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
    void __iomem *addr;

	addr = brcm_pcie_map_bus(bus, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 1)
		writeb(val, addr);
	else if (size == 2)
		writew(val, addr);
	else
		writel(val, addr);

	return PCIBIOS_SUCCESSFUL;
}

static const char *link_speed_to_str(unsigned int cls)
{
	switch (cls) {
	case PCI_EXP_LNKSTA_CLS_2_5GB: return "2.5";
	case PCI_EXP_LNKSTA_CLS_5_0GB: return "5.0";
	case PCI_EXP_LNKSTA_CLS_8_0GB: return "8.0";
	default:
		break;
	}

	return "??";
}

static u32 brcm_pcie_mdio_form_pkt(int port, int regad, int cmd)
{
	u32 pkt = 0;

	pkt |= FIELD_PREP(MDIO_PORT_MASK, port);
	pkt |= FIELD_PREP(MDIO_REGAD_MASK, regad);
	pkt |= FIELD_PREP(MDIO_CMD_MASK, cmd);

	return pkt;
}

/* negative return value indicates error */
static int brcm_pcie_mdio_read(void __iomem *base, u8 port, u8 regad, u32 *val)
{
	int tries;
	u32 data;

	writel(brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_READ),
		   base + PCIE_RC_DL_MDIO_ADDR);
	readl(base + PCIE_RC_DL_MDIO_ADDR);

	data = readl(base + PCIE_RC_DL_MDIO_RD_DATA);
	for (tries = 0; !MDIO_RD_DONE(data) && tries < 10; tries++) {
		udelay(10);
		data = readl(base + PCIE_RC_DL_MDIO_RD_DATA);
	}

	*val = FIELD_GET(MDIO_DATA_MASK, data);
	return MDIO_RD_DONE(data) ? 0 : -EIO;
}

/* negative return value indicates error */
static int brcm_pcie_mdio_write(void __iomem *base, u8 port,
				u8 regad, u16 wrdata)
{
	int tries;
	u32 data;

	writel(brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_WRITE),
		   base + PCIE_RC_DL_MDIO_ADDR);
	readl(base + PCIE_RC_DL_MDIO_ADDR);
	writel(MDIO_DATA_DONE_MASK | wrdata, base + PCIE_RC_DL_MDIO_WR_DATA);

	data = readl(base + PCIE_RC_DL_MDIO_WR_DATA);
	for (tries = 0; !MDIO_WT_DONE(data) && tries < 10; tries++) {
		udelay(10);
		data = readl(base + PCIE_RC_DL_MDIO_WR_DATA);
	}

	return MDIO_WT_DONE(data) ? 0 : -EIO;
}

/*
 * Configures device for Spread Spectrum Clocking (SSC) mode; a negative
 * return value indicates error.
 */
static int brcm_pcie_set_ssc(struct brcm_pcie *pcie)
{
	int pll, ssc;
	int ret;
	u32 tmp;

	ret = brcm_pcie_mdio_write(pcie->base, MDIO_PORT0, SET_ADDR_OFFSET,
				   SSC_REGS_ADDR);
	if (ret < 0)
		return ret;

	ret = brcm_pcie_mdio_read(pcie->base, MDIO_PORT0,
				  SSC_CNTL_OFFSET, &tmp);
	if (ret < 0)
		return ret;

	u32p_replace_bits(&tmp, 1, SSC_CNTL_OVRD_EN_MASK);
	u32p_replace_bits(&tmp, 1, SSC_CNTL_OVRD_VAL_MASK);
	ret = brcm_pcie_mdio_write(pcie->base, MDIO_PORT0,
				   SSC_CNTL_OFFSET, tmp);
	if (ret < 0)
		return ret;

	udelay(2000);
	ret = brcm_pcie_mdio_read(pcie->base, MDIO_PORT0,
				  SSC_STATUS_OFFSET, &tmp);
	if (ret < 0)
		return ret;

	ssc = FIELD_GET(SSC_STATUS_SSC_MASK, tmp);
	pll = FIELD_GET(SSC_STATUS_PLL_LOCK_MASK, tmp);

	return ssc && pll ? 0 : -EIO;
}

/**
 * brcm_pcie_set_gen() - Limits operation to a specific generation (1, 2 or 3)
 * @pcie: pointer to the PCIe controller state
 * @gen: PCIe generation to limit the controller's operation to
 */
static void brcm_pcie_set_gen(struct brcm_pcie *pcie, unsigned int gen)
{
	void __iomem *cap_base = pcie->base + BRCM_PCIE_CAP_REGS;

	u16 lnkctl2 = readw(cap_base + PCI_EXP_LNKCTL2);
	u32 lnkcap = readl(cap_base + PCI_EXP_LNKCAP);

	lnkcap = (lnkcap & ~PCI_EXP_LNKCAP_SLS) | gen;
	writel(lnkcap, cap_base + PCI_EXP_LNKCAP);

	lnkctl2 = (lnkctl2 & ~0xf) | gen;
	writew(lnkctl2, cap_base + PCI_EXP_LNKCTL2);
}

static void brcm_pcie_set_outbound_win(struct brcm_pcie *pcie,
				       unsigned int win, u64 cpu_addr,
				       u64 pcie_addr, u64 size)
{
	u32 cpu_addr_mb_high, limit_addr_mb_high;
	phys_addr_t cpu_addr_mb, limit_addr_mb;
	int high_addr_shift;
	u32 tmp;

	/* Set the base of the pcie_addr window */
	writel(lower_32_bits(pcie_addr), pcie->base + PCIE_MEM_WIN0_LO(win));
	writel(upper_32_bits(pcie_addr), pcie->base + PCIE_MEM_WIN0_HI(win));

	/* Write the addr base & limit lower bits (in MBs) */
	cpu_addr_mb = cpu_addr / SZ_1M;
	limit_addr_mb = (cpu_addr + size - 1) / SZ_1M;

	tmp = readl(pcie->base + PCIE_MEM_WIN0_BASE_LIMIT(win));
	u32p_replace_bits(&tmp, cpu_addr_mb,
			  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK);
	u32p_replace_bits(&tmp, limit_addr_mb,
			  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK);
	writel(tmp, pcie->base + PCIE_MEM_WIN0_BASE_LIMIT(win));

	/* Write the cpu & limit addr upper bits */
	high_addr_shift =
		hweight32(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK);

	cpu_addr_mb_high = cpu_addr_mb >> high_addr_shift;
	tmp = readl(pcie->base + PCIE_MEM_WIN0_BASE_HI(win));
	u32p_replace_bits(&tmp, cpu_addr_mb_high,
			  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI_BASE_MASK);
	writel(tmp, pcie->base + PCIE_MEM_WIN0_BASE_HI(win));

	limit_addr_mb_high = limit_addr_mb >> high_addr_shift;
	tmp = readl(pcie->base + PCIE_MEM_WIN0_LIMIT_HI(win));
	u32p_replace_bits(&tmp, limit_addr_mb_high,
			  PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI_LIMIT_MASK);
	writel(tmp, pcie->base + PCIE_MEM_WIN0_LIMIT_HI(win));
}

static void brcm_pcie_set_local_bus_nr(struct pci_controller *host, int busno)
{
	struct brcm_pcie *pcie = host_to_brcm(host);

	pcie->first_busno = busno;
}

static const struct pci_ops brcm_pcie_ops = {
	.read	= brcm_pcie_read_config,
	.write	= brcm_pcie_write_config,
};

static int brcm_pcie_parse_dt(struct brcm_pcie *pcie)
{
	struct device *dev = pcie->pci.parent;
	struct device_node *np = dev->of_node;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct resource res;
    int max_link_speed, ret;

	if (of_pci_range_parser_init(&parser, np)) {
		pr_err("missing \"ranges\" property\n");
		return -EINVAL;
	}

	for_each_of_pci_range(&parser, &range) {
		of_pci_range_to_resource(&range, np, &res);

		switch (res.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			memcpy(&pcie->io, &res, sizeof(res));
			pcie->io.name = "I/O";
			break;

		case IORESOURCE_MEM:
			if (res.flags & IORESOURCE_PREFETCH) {
				memcpy(&pcie->prefetch, &res, sizeof(res));
				pcie->prefetch.name = "PREFETCH";
			} else {
				memcpy(&pcie->mem, &res, sizeof(res));
				pcie->mem.name = "MEM";
			}
			break;
		}
	}

    pcie->ssc = of_property_read_bool(np, "brcm,enable-ssc");

	ret = of_property_read_u32(np, "max-link-speed", &max_link_speed);
	if (ret < 0 || max_link_speed > 4)
		pcie->gen = 0;
	else
		pcie->gen = max_link_speed;

	return 0;
}


static inline void brcm_pcie_bridge_sw_init_set(struct brcm_pcie *pcie, u32 val)
{
	u32 tmp;

	tmp = readl(pcie->base + PCIE_RGR1_SW_INIT_1);
	u32p_replace_bits(&tmp, val, PCIE_RGR1_SW_INIT_1_INIT_MASK);
	writel(tmp, pcie->base + PCIE_RGR1_SW_INIT_1);
}

static inline void brcm_pcie_perst_set(struct brcm_pcie *pcie, u32 val)
{
	u32 tmp;

	tmp = readl(pcie->base + PCIE_RGR1_SW_INIT_1);
	u32p_replace_bits(&tmp, val, PCIE_RGR1_SW_INIT_1_PERST_MASK);
	writel(tmp, pcie->base + PCIE_RGR1_SW_INIT_1);
}

static inline int brcm_pcie_get_rc_bar2_size_and_offset(struct brcm_pcie *pcie,
							u64 *rc_bar2_size,
							u64 *rc_bar2_offset)
{
	struct pci_controller *pci = &pcie->pci;
	/*
	 * The controller expects the inbound window offset to be calculated as
	 * the difference between PCIe's address space and CPU's. The offset
	 * provided by the firmware is calculated the opposite way, so we
	 * negate it.
	 */
	*rc_bar2_offset = pci->mem_offset;
	*rc_bar2_size = 1ULL << fls64(resource_size(&pcie->mem) - 1);

	/*
	 * We validate the inbound memory view even though we should trust
	 * whatever the device-tree provides. This is because of an HW issue on
	 * early Raspberry Pi 4's revisions (bcm2711). It turns out its
	 * firmware has to dynamically edit dma-ranges due to a bug on the
	 * PCIe controller integration, which prohibits any access above the
	 * lower 3GB of memory. Given this, we decided to keep the dma-ranges
	 * in check, avoiding hard to debug device-tree related issues in the
	 * future:
	 *
	 * The PCIe host controller by design must set the inbound viewport to
	 * be a contiguous arrangement of all of the system's memory.  In
	 * addition, its size mut be a power of two.  To further complicate
	 * matters, the viewport must start on a pcie-address that is aligned
	 * on a multiple of its size.  If a portion of the viewport does not
	 * represent system memory -- e.g. 3GB of memory requires a 4GB
	 * viewport -- we can map the outbound memory in or after 3GB and even
	 * though the viewport will overlap the outbound memory the controller
	 * will know to send outbound memory downstream and everything else
	 * upstream.
	 *
	 * For example:
	 *
	 * - The best-case scenario, memory up to 3GB, is to place the inbound
	 *   region in the first 4GB of pcie-space, as some legacy devices can
	 *   only address 32bits. We would also like to put the MSI under 4GB
	 *   as well, since some devices require a 32bit MSI target address.
	 *
	 * - If the system memory is 4GB or larger we cannot start the inbound
	 *   region at location 0 (since we have to allow some space for
	 *   outbound memory @ 3GB). So instead it will  start at the 1x
	 *   multiple of its size
	 */
	if (!*rc_bar2_size || *rc_bar2_offset % *rc_bar2_size ||
	    (*rc_bar2_offset < SZ_4G && *rc_bar2_offset > SZ_2G)) {
		pr_err("Invalid rc_bar2_offset/size: size 0x%llx, off 0x%llx\n",
			*rc_bar2_size, *rc_bar2_offset);
		return -EINVAL;
	}

	return 0;
}

static int brcm_pcie_setup(struct brcm_pcie *pcie)
{
	struct pci_controller *pci = &pcie->pci;
	u64 rc_bar2_offset, rc_bar2_size;
	void __iomem *base = pcie->base;
	struct resource_entry *entry;
	bool ssc_good = false;
	struct resource *res;
	int num_out_wins = 0;
	u16 nlw, cls, lnksta;
	unsigned int scb_size_val;
	int i, ret;
	u32 tmp;

	/* Reset the bridge */
	brcm_pcie_bridge_sw_init_set(pcie, 1);
	brcm_pcie_perst_set(pcie, 1);

	udelay(200);

	/* Take the bridge out of reset */
	brcm_pcie_bridge_sw_init_set(pcie, 0);

	tmp = readl(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG);
	tmp &= ~PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK;
	writel(tmp, base + PCIE_MISC_HARD_PCIE_HARD_DEBUG);
	/* Wait for SerDes to be stable */
	udelay(200);

	/*
	 * SCB_MAX_BURST_SIZE is a two bit field.  For GENERIC chips it
	 * is encoded as 0=128, 1=256, 2=512, 3=Rsvd, for BCM7278 it
	 * is encoded as 0=Rsvd, 1=128, 2=256, 3=512.
	 */

	/* Set SCB_MAX_BURST_SIZE, CFG_READ_UR_MODE, SCB_ACCESS_EN */
	tmp = readl(base + PCIE_MISC_MISC_CTRL);
	u32p_replace_bits(&tmp, 1, PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK);
	u32p_replace_bits(&tmp, 1, PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK);
	u32p_replace_bits(&tmp, 0, PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK);
	writel(tmp, base + PCIE_MISC_MISC_CTRL);

	ret = brcm_pcie_get_rc_bar2_size_and_offset(pcie, &rc_bar2_size,
						    &rc_bar2_offset);
	if (ret)
		return ret;

	tmp = lower_32_bits(rc_bar2_offset);
	u32p_replace_bits(&tmp, brcm_pcie_encode_ibar_size(rc_bar2_size),
			  PCIE_MISC_RC_BAR2_CONFIG_LO_SIZE_MASK);
	writel(tmp, base + PCIE_MISC_RC_BAR2_CONFIG_LO);
	writel(upper_32_bits(rc_bar2_offset),
	       base + PCIE_MISC_RC_BAR2_CONFIG_HI);

	scb_size_val = rc_bar2_size ?
		       ilog2(rc_bar2_size) - 15 : 0xf; /* 0xf is 1GB */
	tmp = readl(base + PCIE_MISC_MISC_CTRL);
	u32p_replace_bits(&tmp, scb_size_val,
			  PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK);
	writel(tmp, base + PCIE_MISC_MISC_CTRL);

	/* disable the PCIe->GISB memory window (RC_BAR1) */
	tmp = readl(base + PCIE_MISC_RC_BAR1_CONFIG_LO);
	tmp &= ~PCIE_MISC_RC_BAR1_CONFIG_LO_SIZE_MASK;
	writel(tmp, base + PCIE_MISC_RC_BAR1_CONFIG_LO);

	/* disable the PCIe->SCB memory window (RC_BAR3) */
	tmp = readl(base + PCIE_MISC_RC_BAR3_CONFIG_LO);
	tmp &= ~PCIE_MISC_RC_BAR3_CONFIG_LO_SIZE_MASK;
	writel(tmp, base + PCIE_MISC_RC_BAR3_CONFIG_LO);

	/* Mask all interrupts since we are not handling any yet */
	writel(0xffffffff, base + PCIE_MSI_INTR2_MASK_SET);

	/* clear any interrupts we find on boot */
	writel(0xffffffff, base + PCIE_MSI_INTR2_CLR);

	if (pcie->gen)
		brcm_pcie_set_gen(pcie, pcie->gen);

	/* Unassert the fundamental reset */
	brcm_pcie_perst_set(pcie, 0);

	/*
	 * Give the RC/EP time to wake up, before trying to configure RC.
	 * Intermittently check status for link-up, up to a total of 100ms.
	 */
	for (i = 0; i < 100 && !brcm_pcie_link_up(pcie); i += 5)
		mdelay(5);

	if (!brcm_pcie_link_up(pcie)) {
		pr_err("link down\n");
		return -ENODEV;
	}

	if (!brcm_pcie_rc_mode(pcie)) {
		pr_err("PCIe misconfigured; is in EP mode\n");
		return -EINVAL;
	}

	resource_list_for_each_entry(entry, &pci->windows) {
		res = entry->res;

		if (resource_type(res) != IORESOURCE_MEM)
			continue;

		if (num_out_wins >= BRCM_NUM_PCIE_OUT_WINS) {
			pr_err("too many outbound wins\n");
			return -EINVAL;
		}

		brcm_pcie_set_outbound_win(pcie, num_out_wins, res->start,
					   res->start - entry->offset,
					   resource_size(res));
		num_out_wins++;
	}

	clrbits_le32(base + PCIE_RC_CFG_PRIV1_LINK_CAPABILITY,
		PCIE_RC_CFG_PRIV1_LINK_CAPABILITY_ASPM_SUPPORT_MASK);
	/*
	 * For config space accesses on the RC, show the right class for
	 * a PCIe-PCIe bridge (the default setting is to be EP mode).
	 */
	tmp = readl(base + PCIE_RC_CFG_PRIV1_ID_VAL3);
	u32p_replace_bits(&tmp, 0x060400,
			  PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK);
	writel(tmp, base + PCIE_RC_CFG_PRIV1_ID_VAL3);

	if (pcie->ssc) {
		ret = brcm_pcie_set_ssc(pcie);
		if (ret == 0)
			ssc_good = true;
		else
			pr_err("failed attempt to enter ssc mode\n");
	}

	lnksta = readw(base + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKSTA);
	cls = FIELD_GET(PCI_EXP_LNKSTA_CLS, lnksta);
	nlw = FIELD_GET(PCI_EXP_LNKSTA_NLW, lnksta);
	pr_info("link up, %s x%u %s\n",
		 link_speed_to_str(cls), nlw,
		 ssc_good ? "(SSC)" : "(!SSC)");

	/* PCIe->SCB endian mode for BAR */
	tmp = readl(base + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);
	u32p_replace_bits(&tmp, PCIE_RC_CFG_VENDOR_SPCIFIC_REG1_LITTLE_ENDIAN,
		PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK);
	writel(tmp, base + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);

	/*
	 * Refclk from RC should be gated with CLKREQ# input when ASPM L0s,L1
	 * is enabled => setting the CLKREQ_DEBUG_ENABLE field to 1.
	 */
	tmp = readl(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG);
	tmp |= PCIE_MISC_HARD_PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE_MASK;
	writel(tmp, base + PCIE_MISC_HARD_PCIE_HARD_DEBUG);

	return 0;
}

static void brcm_pcie_dump_regs(struct brcm_pcie *pcie)
{
	void __iomem *b = pcie->base;
	int i;

	pr_info("\n==== BRCM PCIe Debug Dump ====\n");

	/* -------- STATUS REGISTERS -------- */
	pr_info("PCIE_MISC_PCIE_STATUS        = 0x%08x\n",
		readl(b + PCIE_MISC_PCIE_STATUS));

	pr_info("PCIE_MISC_MISC_CTRL          = 0x%08x\n",
		readl(b + PCIE_MISC_MISC_CTRL));

	pr_info("PCIE_RGR1_SW_INIT_1          = 0x%08x\n",
		readl(b + PCIE_RGR1_SW_INIT_1));

	pr_info("PCIE_RC_CFG_VENDOR_SPEC_REG1 = 0x%08x\n",
		readl(b + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1));

	/* -------- BAR CONFIG -------- */
	pr_info("BAR1 LO = 0x%08x\n", readl(b + PCIE_MISC_RC_BAR1_CONFIG_LO));
	pr_info("BAR2 LO = 0x%08x\n", readl(b + PCIE_MISC_RC_BAR2_CONFIG_LO));
	pr_info("BAR2 HI = 0x%08x\n", readl(b + PCIE_MISC_RC_BAR2_CONFIG_HI));
	pr_info("BAR3 LO = 0x%08x\n", readl(b + PCIE_MISC_RC_BAR3_CONFIG_LO));

	/* -------- MEMORY OUTBOUND WINDOWS -------- */
	for (i = 0; i < BRCM_NUM_PCIE_OUT_WINS; i++) {
		u32 lo  = readl(b + PCIE_MEM_WIN0_LO(i));
		u32 hi  = readl(b + PCIE_MEM_WIN0_HI(i));
		u32 bl  = readl(b + PCIE_MEM_WIN0_BASE_LIMIT(i));
		u32 bhi = readl(b + PCIE_MEM_WIN0_BASE_HI(i));
		u32 lhi = readl(b + PCIE_MEM_WIN0_LIMIT_HI(i));

		pr_info("Outbound WIN%d:\n", i);
		pr_info("  PCIE_ADDR      = %08x:%08x\n", hi, lo);
		pr_info("  BASE_LIMIT_LO  = 0x%08x\n", bl);
		pr_info("  BASE_HI        = 0x%08x\n", bhi);
		pr_info("  LIMIT_HI       = 0x%08x\n", lhi);
	}

	/* -------- MSI INTERRUPTS -------- */
	pr_info("MSI_INTR2_MASK_SET = 0x%08x\n",
		readl(b + PCIE_MSI_INTR2_MASK_SET));
	pr_info("MSI_INTR2_CLR      = 0x%08x\n",
		readl(b + PCIE_MSI_INTR2_CLR));

	/* -------- LINK CAPABILITIES -------- */
	pr_info("Link Cap Reg  (0x4dc) = 0x%08x\n",
		readl(b + PCIE_RC_CFG_PRIV1_LINK_CAPABILITY));
	pr_info("Link Status Reg      = 0x%04x\n",
		readw(b + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKSTA));
	pr_info("Link Ctrl2 Reg       = 0x%04x\n",
		readw(b + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKCTL2));

	/* -------- MDIO / SSC / SERDES -------- */
	u32 mdio_rd;
	brcm_pcie_mdio_read(b, MDIO_PORT0, SSC_CNTL_OFFSET, &mdio_rd);
	pr_info("MDIO SSC_CNTL         = 0x%08x\n", mdio_rd);
	brcm_pcie_mdio_read(b, MDIO_PORT0, SSC_STATUS_OFFSET, &mdio_rd);
	pr_info("MDIO SSC_STATUS       = 0x%08x\n", mdio_rd);

	pr_info("PCIE_HARD_DEBUG       = 0x%08x\n",
		readl(b + PCIE_MISC_HARD_PCIE_HARD_DEBUG));

	/* -------- EXT CFG -------- */
	pr_info("EXT_CFG_INDEX         = 0x%08x\n",
		readl(b + PCIE_EXT_CFG_INDEX));

	pr_info("==== END =====\n\n");
}


static int brcm_pcie_probe(struct device *dev)
{
	struct brcm_pcie *pcie;
    struct pci_controller *hose;
	struct resource *iores;
	int ret;

    iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);

	pcie = xzalloc(sizeof(*pcie));
    if (!pcie) {
        return -ENOMEM;
    }

	pcie->pci.parent = dev;
	pci_controller_init(&pcie->pci);

    ret = brcm_pcie_parse_dt(pcie);
    if (ret) {
        pr_err("failed to initialize brcm pcie with %d\n", ret);
        return ret;
    }

	pcie->base = IOMEM(iores->start);
	pcie->pci.pci_ops = &brcm_pcie_ops;
	pcie->pci.set_busno = brcm_pcie_set_local_bus_nr;
	pcie->pci.mem_resource = &pcie->mem;
	pcie->pci.io_resource = &pcie->io;
	pcie->pci.mem_pref_resource = &pcie->prefetch;
	dev->priv = pcie;
    hose = &pcie->pci;

	ret = brcm_pcie_setup(pcie);
	if (ret) {
		pr_err("Failed to setup brcm pcie, err: %d\n", ret);
		return ret;
	}

	register_pci_controller(&pcie->pci);

	if (IS_ENABLED(CONFIG_PCI_DEBUG))
		brcm_pcie_dump_regs(pcie);

    return 0;
}


static void brcm_pcie_remove(struct device *dev)
{
	struct brcm_pcie *pcie = dev->priv;
	void __iomem *base = pcie->base;

	// Assert fundamental reset
	setbits_le32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_PERST_MASK);

	// Turn off SerDes
	setbits_le32(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG,
		     PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK);

	// Shutdown bridge
	setbits_le32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT_MASK);
}

static const struct of_device_id brcm_pcie_ids[] = {
	{ .compatible = "brcm,bcm2711-pcie" },
	{ }
};
MODULE_DEVICE_TABLE(of, brcm_pcie_ids);

static struct driver pcie_ecam_driver = {
	.name = "pcie-brcmstb",
	.probe = brcm_pcie_probe,
	.remove = brcm_pcie_remove,
	.of_compatible = brcm_pcie_ids,
};
device_platform_driver(pcie_ecam_driver);
