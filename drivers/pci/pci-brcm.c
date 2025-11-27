// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2009 - 2019 Broadcom */

#include <common.h>
#include <clock.h>
#include <abort.h>
#include <malloc.h>
#include <io.h>
#include <init.h>
#include <gpio.h>
#include <asm/mmu.h>
#include <of_gpio.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <of_address.h>
#include <of_pci.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/bitfield.h>

/* BRCM_PCIE_CAP_REGS - Offset for the mandatory capability config regs */
#define BRCM_PCIE_CAP_REGS				0x00ac

/* Broadcom STB PCIe Register Offsets */
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1				0x0188
#define  PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK	0xc
#define  PCIE_RC_CFG_VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN			0x0

#define PCIE_RC_CFG_PRIV1_ID_VAL3			0x043c
#define  PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK	0xffffff

#define PCIE_RC_CFG_PRIV1_LINK_CAPABILITY			0x04dc
#define  PCIE_RC_CFG_PRIV1_LINK_CAPABILITY_ASPM_SUPPORT_MASK	0xc00

#define PCIE_RC_CFG_PRIV1_ROOT_CAP			0x4f8
#define  PCIE_RC_CFG_PRIV1_ROOT_CAP_L1SS_MODE_MASK	0xf8

#define PCIE_RC_DL_MDIO_ADDR				0x1100
#define PCIE_RC_DL_MDIO_WR_DATA				0x1104
#define PCIE_RC_DL_MDIO_RD_DATA				0x1108

#define PCIE_RC_PL_PHY_CTL_15				0x184c
#define  PCIE_RC_PL_PHY_CTL_15_DIS_PLL_PD_MASK		0x400000
#define  PCIE_RC_PL_PHY_CTL_15_PM_CLK_PERIOD_MASK	0xff

#define PCIE_MISC_MISC_CTRL				0x4008
#define  PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE_MASK	0x80
#define  PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE_MASK	0x400
#define  PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK		0x1000
#define  PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK	0x2000
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK	0x300000

#define  PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK		0xf8000000
#define  PCIE_MISC_MISC_CTRL_SCB1_SIZE_MASK		0x07c00000
#define  PCIE_MISC_MISC_CTRL_SCB2_SIZE_MASK		0x0000001f
#define  SCB_SIZE_MASK(x) PCIE_MISC_MISC_CTRL_SCB ## x ## _SIZE_MASK

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO		0x400c
#define PCIE_MEM_WIN0_LO(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO + ((win) * 8)

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI		0x4010
#define PCIE_MEM_WIN0_HI(win)	\
		PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI + ((win) * 8)

/*
 * NOTE: You may see the term "BAR" in a number of register names used by
 *   this driver.  The term is an artifact of when the HW core was an
 *   endpoint device (EP).  Now it is a root complex (RC) and anywhere a
 *   register has the term "BAR" it is related to an inbound window.
 */

#define PCIE_BRCM_MAX_INBOUND_WINS			16
#define PCIE_MISC_RC_BAR1_CONFIG_LO			0x402c
#define  PCIE_MISC_RC_BAR1_CONFIG_LO_SIZE_MASK		0x1f

#define PCIE_MISC_RC_BAR4_CONFIG_LO			0x40d4

#define PCIE_MISC_PCIE_CTRL				0x4064
#define  PCIE_MISC_PCIE_CTRL_PCIE_L23_REQUEST_MASK	0x1
#define PCIE_MISC_PCIE_CTRL_PCIE_PERSTB_MASK		0x4

#define PCIE_MISC_PCIE_STATUS				0x4068
#define  PCIE_MISC_PCIE_STATUS_PCIE_PORT_MASK		0x80
#define  PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK	0x20
#define  PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK	0x10
#define  PCIE_MISC_PCIE_STATUS_PCIE_LINK_IN_L23_MASK	0x40

#define PCIE_MISC_REVISION				0x406c
#define  BRCM_PCIE_HW_REV_33				0x0303
#define  BRCM_PCIE_HW_REV_3_20				0x0320

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

#define  PCIE_MISC_HARD_PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE_MASK	0x2
#define  PCIE_MISC_HARD_PCIE_HARD_DEBUG_L1SS_ENABLE_MASK		0x200000
#define  PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK		0x08000000
#define  PCIE_BMIPS_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK		0x00800000
#define  PCIE_CLKREQ_MASK \
	  (PCIE_MISC_HARD_PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE_MASK | \
	   PCIE_MISC_HARD_PCIE_HARD_DEBUG_L1SS_ENABLE_MASK)

#define PCIE_MISC_UBUS_BAR1_CONFIG_REMAP			0x40ac
#define  PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_ACCESS_EN_MASK	BIT(0)
#define PCIE_MISC_UBUS_BAR4_CONFIG_REMAP			0x410c

#define  PCIE_RGR1_SW_INIT_1_PERST_MASK			0x1
#define  PCIE_RGR1_SW_INIT_1_PERST_SHIFT		0x0

#define RGR1_SW_INIT_1_INIT_GENERIC_MASK		0x2
#define RGR1_SW_INIT_1_INIT_GENERIC_SHIFT		0x1
#define RGR1_SW_INIT_1_INIT_7278_MASK			0x1
#define RGR1_SW_INIT_1_INIT_7278_SHIFT			0x0

/* PCIe parameters */
#define BRCM_NUM_PCIE_OUT_WINS		0x4

/* MDIO registers */
#define MDIO_PORT0			0x0
#define MDIO_DATA_MASK			0x7fffffff
#define MDIO_PORT_MASK			0xf0000
#define MDIO_PORT_EXT_MASK		0x200000
#define MDIO_REGAD_MASK			0xffff
#define MDIO_CMD_MASK			0x00100000
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
#define PCIE_BRCM_MAX_MEMC		3

#define IDX_ADDR(pcie)			((pcie)->cfg->offsets[EXT_CFG_INDEX])
#define DATA_ADDR(pcie)			((pcie)->cfg->offsets[EXT_CFG_DATA])
#define PCIE_RGR1_SW_INIT_1(pcie)	((pcie)->cfg->offsets[RGR1_SW_INIT_1])
#define HARD_DEBUG(pcie)		((pcie)->cfg->offsets[PCIE_HARD_DEBUG])
#define INTR2_CPU_BASE(pcie)		((pcie)->cfg->offsets[PCIE_INTR2_CPU_BASE])

/* Rescal registers */
#define PCIE_DVT_PMU_PCIE_PHY_CTRL				0xc700
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_NFLDS			0x3
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_DIG_RESET_MASK		0x4
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_DIG_RESET_SHIFT	0x2
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_RESET_MASK		0x2
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_RESET_SHIFT		0x1
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_PWRDN_MASK		0x1
#define  PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_PWRDN_SHIFT		0x0

/* Forward declarations */
struct brcm_pcie;

enum {
	RGR1_SW_INIT_1,
	EXT_CFG_INDEX,
	EXT_CFG_DATA,
	PCIE_HARD_DEBUG,
	PCIE_INTR2_CPU_BASE,
};

enum pcie_soc_base {
	GENERIC,
	BCM2711,
	BCM4908,
	BCM7278,
	BCM7425,
	BCM7435,
	BCM7712,
};

struct inbound_win {
	u64 size;
	u64 pci_offset;
	u64 cpu_addr;
};

/*
 * The RESCAL block is tied to PCIe controller #1, regardless of the number of
 * controllers, and turning off PCIe controller #1 prevents access to the RESCAL
 * register blocks, therefore no other controller can access this register
 * space, and depending upon the bus fabric we may get a timeout (UBUS/GISB),
 * or a hang (AXI).
 */
#define CFG_QUIRK_AVOID_BRIDGE_SHUTDOWN		BIT(0)

struct pcie_cfg_data {
	const int *offsets;
	const enum pcie_soc_base soc_base;
	const bool has_phy;
	const u32 quirks;
	u8 num_inbound_wins;
	int (*perst_set)(struct brcm_pcie *pcie, u32 val);
	int (*bridge_sw_init_set)(struct brcm_pcie *pcie, u32 val);
	int (*post_setup)(struct brcm_pcie *pcie);
};

struct subdev_regulators {
	unsigned int num_supplies;
	struct regulator_bulk_data supplies[];
};

/* Internal PCIe Host Controller Information.*/
struct brcm_pcie {
	struct device		*dev;
	void __iomem		*base;
    struct pci_controller pci;
	struct clk		*clk;
	struct device_node	*np;
	bool			ssc;
	int			gen;
	struct reset_control	*rescal;
	struct reset_control	*perst_reset;
	struct reset_control	*bridge_reset;
	struct reset_control	*swinit_reset;
	int			num_memc;
	u64			memc_size[PCIE_BRCM_MAX_MEMC];
	u32			hw_rev;
	struct subdev_regulators *sr;
	const struct pcie_cfg_data	*cfg;
};

static inline bool is_bmips(const struct brcm_pcie *pcie)
{
	return pcie->cfg->soc_base == BCM7435 || pcie->cfg->soc_base == BCM7425;
}

static inline struct brcm_pcie *host_to_pcie(struct pci_bus *host)
{
	return container_of(host, struct brcm_pcie, pci);
}

/*
 * This is to convert the size of the inbound "BAR" region to the
 * non-linear values of PCIE_X_MISC_RC_BAR[123]_CONFIG_LO.SIZE
 */
static int brcm_pcie_encode_ibar_size(u64 size)
{
	int log2_in = __ilog2(size);

	if (log2_in >= 12 && log2_in <= 15)
		/* Covers 4KB to 32KB (inclusive) */
		return (log2_in - 12) + 0x1c;
	else if (log2_in >= 16 && log2_in <= 36)
		/* Covers 64KB to 64GB, (inclusive) */
		return log2_in - 15;
	/* Something is awry so disable */
	return 0;
}

static u32 brcm_pcie_mdio_form_pkt(int port, int regad, int cmd)
{
	u32 pkt = 0;

	pkt |= FIELD_PREP(MDIO_PORT_EXT_MASK, port >> 4);
	pkt |= FIELD_PREP(MDIO_PORT_MASK, port);
	pkt |= FIELD_PREP(MDIO_REGAD_MASK, regad);
	pkt |= FIELD_PREP(MDIO_CMD_MASK, cmd);

	return pkt;
}

/* negative return value indicates error */
static int brcm_pcie_mdio_read(void __iomem *base, u8 port, u8 regad, u32 *val)
{
	u32 data;
	int err;

	writel(brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_READ),
		   base + PCIE_RC_DL_MDIO_ADDR);
	readl(base + PCIE_RC_DL_MDIO_ADDR);
	err = readl_poll_timeout(base + PCIE_RC_DL_MDIO_RD_DATA, data,
					MDIO_RD_DONE(data), 100);
	*val = FIELD_GET(MDIO_DATA_MASK, data);

	return err;
}

/* negative return value indicates error */
static int brcm_pcie_mdio_write(void __iomem *base, u8 port,
				u8 regad, u16 wrdata)
{
	u32 data;
	int err;

	writel(brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_WRITE),
		   base + PCIE_RC_DL_MDIO_ADDR);
	readl(base + PCIE_RC_DL_MDIO_ADDR);
	writel(MDIO_DATA_DONE_MASK | wrdata, base + PCIE_RC_DL_MDIO_WR_DATA);

	err = readl_poll_timeout_atomic(base + PCIE_RC_DL_MDIO_WR_DATA, data,
					MDIO_WT_DONE(data), 10, 100);
	return err;
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

	usleep(2000);
	ret = brcm_pcie_mdio_read(pcie->base, MDIO_PORT0,
				  SSC_STATUS_OFFSET, &tmp);
	if (ret < 0)
		return ret;

	ssc = FIELD_GET(SSC_STATUS_SSC_MASK, tmp);
	pll = FIELD_GET(SSC_STATUS_PLL_LOCK_MASK, tmp);

	return ssc && pll ? 0 : -EIO;
}

/* Limits operation to a specific generation (1, 2, or 3) */
static void brcm_pcie_set_gen(struct brcm_pcie *pcie, int gen)
{
	u16 lnkctl2 = readw(pcie->base + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKCTL2);
	u32 lnkcap = readl(pcie->base + PCIE_RC_CFG_PRIV1_LINK_CAPABILITY);

	u32p_replace_bits(&lnkcap, gen, PCI_EXP_LNKCAP_SLS);
	writel(lnkcap, pcie->base + PCIE_RC_CFG_PRIV1_LINK_CAPABILITY);

	u16p_replace_bits(&lnkctl2, gen, PCI_EXP_LNKCTL2_TLS);
	writew(lnkctl2, pcie->base + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKCTL2);
}

static void brcm_pcie_set_outbound_win(struct brcm_pcie *pcie,
				       u8 win, u64 cpu_addr,
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

	if (is_bmips(pcie))
		return;

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
				       unsigned int devfn, int where)
{
	struct brcm_pcie *pcie = host_to_pcie(bus);
	void __iomem *base = pcie->base;
	int idx;

	/* Accesses to the RC go right to the RC registers if !devfn */
	if (!bus->parent)
		return devfn ? NULL : base + PCIE_ECAM_REG(where);

	/* An access to our HW w/o link-up will cause a CPU Abort */
	if (!brcm_pcie_link_up(pcie))
		return NULL;

	/* For devices, write to the config space index register */
	idx = PCIE_ECAM_OFFSET(bus->number, devfn, 0);
	writel(idx, base + IDX_ADDR(pcie));
	return base + DATA_ADDR(pcie) + PCIE_ECAM_REG(where);
}

static void __iomem *brcm7425_pcie_map_bus(struct pci_bus *bus,
					   unsigned int devfn, int where)
{
	struct brcm_pcie *pcie = host_to_pcie(bus);
	void __iomem *base = pcie->base;
	int idx;

	/* Accesses to the RC go right to the RC registers if !devfn */
	if (!bus->parent)
		return devfn ? NULL : base + PCIE_ECAM_REG(where);

	/* An access to our HW w/o link-up will cause a CPU Abort */
	if (!brcm_pcie_link_up(pcie))
		return NULL;

	/* For devices, write to the config space index register */
	idx = PCIE_ECAM_OFFSET(bus->number, devfn, where);
	writel(idx, base + IDX_ADDR(pcie));
	return base + DATA_ADDR(pcie);
}

static int brcm_pcie_bridge_sw_init_set_generic(struct brcm_pcie *pcie, u32 val)
{
	u32 tmp, mask = RGR1_SW_INIT_1_INIT_GENERIC_MASK;
	u32 shift = RGR1_SW_INIT_1_INIT_GENERIC_SHIFT;
	int ret = 0;

	if (pcie->bridge_reset) {
		if (val)
			ret = reset_control_assert(pcie->bridge_reset);
		else
			ret = reset_control_deassert(pcie->bridge_reset);

		if (ret)
			pr_err("failed to %s 'bridge' reset, err=%d\n",
				val ? "assert" : "deassert", ret);

		return ret;
	}

	tmp = readl(pcie->base + PCIE_RGR1_SW_INIT_1(pcie));
	tmp = (tmp & ~mask) | ((val << shift) & mask);
	writel(tmp, pcie->base + PCIE_RGR1_SW_INIT_1(pcie));

	return ret;
}

static int brcm_pcie_bridge_sw_init_set_7278(struct brcm_pcie *pcie, u32 val)
{
	u32 tmp, mask =  RGR1_SW_INIT_1_INIT_7278_MASK;
	u32 shift = RGR1_SW_INIT_1_INIT_7278_SHIFT;

	tmp = readl(pcie->base + PCIE_RGR1_SW_INIT_1(pcie));
	tmp = (tmp & ~mask) | ((val << shift) & mask);
	writel(tmp, pcie->base + PCIE_RGR1_SW_INIT_1(pcie));

	return 0;
}

static int brcm_pcie_perst_set_4908(struct brcm_pcie *pcie, u32 val)
{
	int ret;

	if (WARN_ONCE(!pcie->perst_reset, "missing PERST# reset controller\n"))
		return -EINVAL;

	if (val)
		ret = reset_control_assert(pcie->perst_reset);
	else
		ret = reset_control_deassert(pcie->perst_reset);

	if (ret)
		pr_err("failed to %s 'perst' reset, err=%d\n",
			val ? "assert" : "deassert", ret);
	return ret;
}

static int brcm_pcie_perst_set_7278(struct brcm_pcie *pcie, u32 val)
{
	u32 tmp;

	/* Perst bit has moved and assert value is 0 */
	tmp = readl(pcie->base + PCIE_MISC_PCIE_CTRL);
	u32p_replace_bits(&tmp, !val, PCIE_MISC_PCIE_CTRL_PCIE_PERSTB_MASK);
	writel(tmp, pcie->base +  PCIE_MISC_PCIE_CTRL);

	return 0;
}

static int brcm_pcie_perst_set_generic(struct brcm_pcie *pcie, u32 val)
{
	u32 tmp;

	tmp = readl(pcie->base + PCIE_RGR1_SW_INIT_1(pcie));
	u32p_replace_bits(&tmp, val, PCIE_RGR1_SW_INIT_1_PERST_MASK);
	writel(tmp, pcie->base + PCIE_RGR1_SW_INIT_1(pcie));

	return 0;
}

static int brcm_pcie_post_setup_bcm2712(struct brcm_pcie *pcie)
{
	static const u16 data[] = { 0x50b9, 0xbda1, 0x0094, 0x97b4, 0x5030,
				    0x5030, 0x0007 };
	static const u8 regs[] = { 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1e };
	int ret, i;
	u32 tmp;

	/* Allow a 54MHz (xosc) refclk source */
	ret = brcm_pcie_mdio_write(pcie->base, MDIO_PORT0, SET_ADDR_OFFSET, 0x1600);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = brcm_pcie_mdio_write(pcie->base, MDIO_PORT0, regs[i], data[i]);
		if (ret < 0)
			return ret;
	}

	usleep(100, 200);

	/*
	 * Set L1SS sub-state timers to avoid lengthy state transitions,
	 * PM clock period is 18.52ns (1/54MHz, round down).
	 */
	tmp = readl(pcie->base + PCIE_RC_PL_PHY_CTL_15);
	tmp &= ~PCIE_RC_PL_PHY_CTL_15_PM_CLK_PERIOD_MASK;
	tmp |= 0x12;
	writel(tmp, pcie->base + PCIE_RC_PL_PHY_CTL_15);

	return 0;
}

static void add_inbound_win(struct inbound_win *b, u8 *count, u64 size,
			    u64 cpu_addr, u64 pci_offset)
{
	b->size = size;
	b->cpu_addr = cpu_addr;
	b->pci_offset = pci_offset;
	(*count)++;
}

static int brcm_pcie_get_inbound_wins(struct brcm_pcie *pcie,
				      struct inbound_win inbound_wins[])
{
	struct pci_controller *pci = &pcie->pci;
	u64 pci_offset, cpu_addr, size = 0, tot_size = 0;
	struct resource_entry *entry;
	struct device *dev = pcie->dev;
	u64 lowest_pcie_addr = ~(u64)0;
	int ret, i = 0;
	u8 n = 0;

	/*
	 * The HW registers (and PCIe) use order-1 numbering for BARs.  As such,
	 * we have inbound_wins[0] unused and BAR1 starts at inbound_wins[1].
	 */
	struct inbound_win *b_begin = &inbound_wins[1];
	struct inbound_win *b = b_begin;

	/*
	 * STB chips beside 7712 disable the first inbound window default.
	 * Rather being mapped to system memory it is mapped to the
	 * internal registers of the SoC.  This feature is deprecated, has
	 * security considerations, and is not implemented in our modern
	 * SoCs.
	 */
	if (pcie->cfg->soc_base != BCM7712)
		add_inbound_win(b++, &n, 0, 0, 0);

	resource_list_for_each_entry(entry, pci->dma_ranges) {
		u64 pcie_start = entry->res->start - entry->offset;
		u64 cpu_start = entry->res->start;

		size = resource_size(entry->res);
		tot_size += size;
		if (pcie_start < lowest_pcie_addr)
			lowest_pcie_addr = pcie_start;
		/*
		 * 7712 and newer chips may have many BARs, with each
		 * offering a non-overlapping viewport to system memory.
		 * That being said, each BARs size must still be a power of
		 * two.
		 */
		if (pcie->cfg->soc_base == BCM7712)
			add_inbound_win(b++, &n, size, cpu_start, pcie_start);

		if (n > pcie->cfg->num_inbound_wins)
			break;
	}

	if (lowest_pcie_addr == ~(u64)0) {
		pr_err("DT node has no dma-ranges\n");
		return -EINVAL;
	}

	/*
	 * 7712 and newer chips do not have an internal memory mapping system
	 * that enables multiple memory controllers.  As such, it can return
	 * now w/o doing special configuration.
	 */
	if (pcie->cfg->soc_base == BCM7712)
		return n;

	ret = of_property_read_variable_u64_array(pcie->np, "brcm,scb-sizes", pcie->memc_size, 1,
						  PCIE_BRCM_MAX_MEMC);
	if (ret <= 0) {
		/* Make an educated guess */
		pcie->num_memc = 1;
		pcie->memc_size[0] = 1ULL << fls64(tot_size - 1);
	} else {
		pcie->num_memc = ret;
	}

	/* Each memc is viewed through a "port" that is a power of 2 */
	for (i = 0, size = 0; i < pcie->num_memc; i++)
		size += pcie->memc_size[i];

	/* Our HW mandates that the window size must be a power of 2 */
	size = 1ULL << fls64(size - 1);

	/*
	 * For STB chips, the BAR2 cpu_addr is hardwired to the start
	 * of system memory, so we set it to 0.
	 */
	cpu_addr = 0;
	pci_offset = lowest_pcie_addr;

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
	 * - If the system memory is 4GB or larger we cannot start the inbound
	 *   region at location 0 (since we have to allow some space for
	 *   outbound memory @ 3GB). So instead it will  start at the 1x
	 *   multiple of its size
	 */
	if (!size || (pci_offset & (size - 1)) ||
	    (pci_offset < SZ_4G && pci_offset > SZ_2G)) {
		pr_err("Invalid inbound_win2_offset/size: size 0x%llx, off 0x%llx\n",
			size, pci_offset);
		return -EINVAL;
	}

	/* Enable inbound window 2, the main inbound window for STB chips */
	add_inbound_win(b++, &n, size, cpu_addr, pci_offset);

	/*
	 * Disable inbound window 3.  On some chips presents the same
	 * window as #2 but the data appears in a settable endianness.
	 */
	add_inbound_win(b++, &n, 0, 0, 0);

	return n;
}

static u32 brcm_bar_reg_offset(int bar)
{
	if (bar <= 3)
		return PCIE_MISC_RC_BAR1_CONFIG_LO + 8 * (bar - 1);
	else
		return PCIE_MISC_RC_BAR4_CONFIG_LO + 8 * (bar - 4);
}

static u32 brcm_ubus_reg_offset(int bar)
{
	if (bar <= 3)
		return PCIE_MISC_UBUS_BAR1_CONFIG_REMAP + 8 * (bar - 1);
	else
		return PCIE_MISC_UBUS_BAR4_CONFIG_REMAP + 8 * (bar - 4);
}

static void set_inbound_win_registers(struct brcm_pcie *pcie,
				      const struct inbound_win *inbound_wins,
				      u8 num_inbound_wins)
{
	void __iomem *base = pcie->base;
	int i;

	for (i = 1; i <= num_inbound_wins; i++) {
		u64 pci_offset = inbound_wins[i].pci_offset;
		u64 cpu_addr = inbound_wins[i].cpu_addr;
		u64 size = inbound_wins[i].size;
		u32 reg_offset = brcm_bar_reg_offset(i);
		u32 tmp = lower_32_bits(pci_offset);

		u32p_replace_bits(&tmp, brcm_pcie_encode_ibar_size(size),
				  PCIE_MISC_RC_BAR1_CONFIG_LO_SIZE_MASK);

		/* Write low */
		writel_relaxed(tmp, base + reg_offset);
		/* Write high */
		writel_relaxed(upper_32_bits(pci_offset), base + reg_offset + 4);

		/*
		 * Most STB chips:
		 *     Do nothing.
		 * 7712:
		 *     All of their BARs need to be set.
		 */
		if (pcie->cfg->soc_base == BCM7712) {
			/* BUS remap register settings */
			reg_offset = brcm_ubus_reg_offset(i);
			tmp = lower_32_bits(cpu_addr) & ~0xfff;
			tmp |= PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_ACCESS_EN_MASK;
			writel_relaxed(tmp, base + reg_offset);
			tmp = upper_32_bits(cpu_addr);
			writel_relaxed(tmp, base + reg_offset + 4);
		}
	}
}

static int brcm_pcie_setup(struct brcm_pcie *pcie)
{
	struct inbound_win inbound_wins[PCIE_BRCM_MAX_INBOUND_WINS];
	void __iomem *base = pcie->base;
	struct pci_controller *pci;
	struct resource_entry *entry;
	u32 tmp, burst, aspm_support;
	u8 num_out_wins = 0;
	int num_inbound_wins = 0;
	int memc, ret;

	/* Reset the bridge */
	ret = pcie->cfg->bridge_sw_init_set(pcie, 1);
	if (ret)
		return ret;

	/* Ensure that PERST# is asserted; some bootloaders may deassert it. */
	if (pcie->cfg->soc_base == BCM2711) {
		ret = pcie->cfg->perst_set(pcie, 1);
		if (ret) {
			pcie->cfg->bridge_sw_init_set(pcie, 0);
			return ret;
		}
	}

	usleep(200);

	/* Take the bridge out of reset */
	ret = pcie->cfg->bridge_sw_init_set(pcie, 0);
	if (ret)
		return ret;

	tmp = readl(base + HARD_DEBUG(pcie));
	if (is_bmips(pcie))
		tmp &= ~PCIE_BMIPS_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK;
	else
		tmp &= ~PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK;
	writel(tmp, base + HARD_DEBUG(pcie));
	/* Wait for SerDes to be stable */
	usleep(200);

	/*
	 * SCB_MAX_BURST_SIZE is a two bit field.  For GENERIC chips it
	 * is encoded as 0=128, 1=256, 2=512, 3=Rsvd, for BCM7278 it
	 * is encoded as 0=Rsvd, 1=128, 2=256, 3=512.
	 */
	if (is_bmips(pcie))
		burst = 0x1; /* 256 bytes */
	else if (pcie->cfg->soc_base == BCM2711)
		burst = 0x0; /* 128 bytes */
	else if (pcie->cfg->soc_base == BCM7278)
		burst = 0x3; /* 512 bytes */
	else
		burst = 0x2; /* 512 bytes */

	/*
	 * Set SCB_MAX_BURST_SIZE, CFG_READ_UR_MODE, SCB_ACCESS_EN,
	 * RCB_MPS_MODE, RCB_64B_MODE
	 */
	tmp = readl(base + PCIE_MISC_MISC_CTRL);
	u32p_replace_bits(&tmp, 1, PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK);
	u32p_replace_bits(&tmp, 1, PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK);
	u32p_replace_bits(&tmp, burst, PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK);
	u32p_replace_bits(&tmp, 1, PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE_MASK);
	u32p_replace_bits(&tmp, 1, PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE_MASK);
	writel(tmp, base + PCIE_MISC_MISC_CTRL);

	num_inbound_wins = brcm_pcie_get_inbound_wins(pcie, inbound_wins);
	if (num_inbound_wins < 0)
		return num_inbound_wins;

	set_inbound_win_registers(pcie, inbound_wins, num_inbound_wins);

	if (!brcm_pcie_rc_mode(pcie)) {
		pr_err("PCIe RC controller misconfigured as Endpoint\n");
		return -EINVAL;
	}

	tmp = readl(base + PCIE_MISC_MISC_CTRL);
	for (memc = 0; memc < pcie->num_memc; memc++) {
		u32 scb_size_val = __ilog2(pcie->memc_size[memc]) - 15;

		if (memc == 0)
			u32p_replace_bits(&tmp, scb_size_val, SCB_SIZE_MASK(0));
		else if (memc == 1)
			u32p_replace_bits(&tmp, scb_size_val, SCB_SIZE_MASK(1));
		else if (memc == 2)
			u32p_replace_bits(&tmp, scb_size_val, SCB_SIZE_MASK(2));
	}
	writel(tmp, base + PCIE_MISC_MISC_CTRL);

	/*
	 * For config space accesses on the RC, show the right class for
	 * a PCIe-PCIe bridge (the default setting is to be EP mode).
	 */
	tmp = readl(base + PCIE_RC_CFG_PRIV1_ID_VAL3);
	u32p_replace_bits(&tmp, 0x060400,
			  PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK);
	writel(tmp, base + PCIE_RC_CFG_PRIV1_ID_VAL3);
    
    pci = &pcie->pci;
	resource_list_for_each_entry(entry, pci->windows) {
		struct resource *res = entry->res;

		if (resource_type(res) != IORESOURCE_MEM)
			continue;

		if (num_out_wins >= BRCM_NUM_PCIE_OUT_WINS) {
			pr_err("too many outbound wins\n");
			return -EINVAL;
		}

		if (is_bmips(pcie)) {
			u64 start = res->start;
			unsigned int j, nwins = resource_size(res) / SZ_128M;

			/* bmips PCIe outbound windows have a 128MB max size */
			if (nwins > BRCM_NUM_PCIE_OUT_WINS)
				nwins = BRCM_NUM_PCIE_OUT_WINS;
			for (j = 0; j < nwins; j++, start += SZ_128M)
				brcm_pcie_set_outbound_win(pcie, j, start,
							   start - entry->offset,
							   SZ_128M);
			break;
		}
		brcm_pcie_set_outbound_win(pcie, num_out_wins, res->start,
					   res->start - entry->offset,
					   resource_size(res));
		num_out_wins++;
	}

	/* PCIe->SCB endian mode for inbound window */
	tmp = readl(base + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);
	u32p_replace_bits(&tmp, PCIE_RC_CFG_VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN,
		PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK);
	writel(tmp, base + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);

	if (pcie->cfg->post_setup) {
		ret = pcie->cfg->post_setup(pcie);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * This extends the timeout period for an access to an internal bus.  This
 * access timeout may occur during L1SS sleep periods, even without the
 * presence of a PCIe access.
 */
static void brcm_extend_rbus_timeout(struct brcm_pcie *pcie)
{
	/* TIMEOUT register is two registers before RGR1_SW_INIT_1 */
	const unsigned int REG_OFFSET = PCIE_RGR1_SW_INIT_1(pcie) - 8;
	u32 timeout_us = 4000000; /* 4 seconds, our setting for L1SS */

	/* 7712 does not have this (RGR1) timer */
	if (pcie->cfg->soc_base == BCM7712)
		return;

	/* Each unit in timeout register is 1/216,000,000 seconds */
	writel(216 * timeout_us, pcie->base + REG_OFFSET);
}

static void brcm_config_clkreq(struct brcm_pcie *pcie)
{
	static const char err_msg[] = "invalid 'brcm,clkreq-mode' DT string\n";
	const char *mode = "default";
	u32 clkreq_cntl;
	int ret, tmp;

	ret = of_property_read_string(pcie->np, "brcm,clkreq-mode", &mode);
	if (ret && ret != -EINVAL) {
		pr_err(err_msg);
		mode = "safe";
	}

	/* Start out assuming safe mode (both mode bits cleared) */
	clkreq_cntl = readl(pcie->base + HARD_DEBUG(pcie));
	clkreq_cntl &= ~PCIE_CLKREQ_MASK;

	if (strcmp(mode, "no-l1ss") == 0) {
		/*
		 * "no-l1ss" -- Provides Clock Power Management, L0s, and
		 * L1, but cannot provide L1 substate (L1SS) power
		 * savings. If the downstream device connected to the RC is
		 * L1SS capable AND the OS enables L1SS, all PCIe traffic
		 * may abruptly halt, potentially hanging the system.
		 */
		clkreq_cntl |= PCIE_MISC_HARD_PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE_MASK;
		/*
		 * We want to un-advertise L1 substates because if the OS
		 * tries to configure the controller into using L1 substate
		 * power savings it may fail or hang when the RC HW is in
		 * "no-l1ss" mode.
		 */
		tmp = readl(pcie->base + PCIE_RC_CFG_PRIV1_ROOT_CAP);
		u32p_replace_bits(&tmp, 2, PCIE_RC_CFG_PRIV1_ROOT_CAP_L1SS_MODE_MASK);
		writel(tmp, pcie->base + PCIE_RC_CFG_PRIV1_ROOT_CAP);

	} else if (strcmp(mode, "default") == 0) {
		/*
		 * "default" -- Provides L0s, L1, and L1SS, but not
		 * compliant to provide Clock Power Management;
		 * specifically, may not be able to meet the Tclron max
		 * timing of 400ns as specified in "Dynamic Clock Control",
		 * section 3.2.5.2.2 of the PCIe spec.  This situation is
		 * atypical and should happen only with older devices.
		 */
		clkreq_cntl |= PCIE_MISC_HARD_PCIE_HARD_DEBUG_L1SS_ENABLE_MASK;
		brcm_extend_rbus_timeout(pcie);

	} else {
		/*
		 * "safe" -- No power savings; refclk is driven by RC
		 * unconditionally.
		 */
		if (strcmp(mode, "safe") != 0)
			pr_err(err_msg);
		mode = "safe";
	}
	writel(clkreq_cntl, pcie->base + HARD_DEBUG(pcie));

	pr_info("clkreq-mode set to %s\n", mode);
}

static int brcm_pcie_start_link(struct brcm_pcie *pcie)
{
	void __iomem *base = pcie->base;
	u16 nlw, cls, lnksta;
	bool ssc_good = false;
	int ret, i;

	/* Limit the generation if specified */
	if (pcie->gen)
		brcm_pcie_set_gen(pcie, pcie->gen);

	/* Unassert the fundamental reset */
	ret = pcie->cfg->perst_set(pcie, 0);
	if (ret)
		return ret;

	/*
	 * Wait for 100ms after PERST# deassertion; see PCIe CEM specification
	 * sections 2.2, PCIe r5.0, 6.6.1.
	 */
	usleep(1000);

	/*
	 * Give the RC/EP even more time to wake up, before trying to
	 * configure RC.  Intermittently check status for link-up, up to a
	 * total of 100ms.
	 */
	for (i = 0; i < 100 && !brcm_pcie_link_up(pcie); i += 5)
		usleep(5000);

	if (!brcm_pcie_link_up(pcie)) {
		pr_err("link down\n");
		return -ENODEV;
	}

	brcm_config_clkreq(pcie);

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
	pr_info("link up x%u %s\n", nlw,
		 ssc_good ? "(SSC)" : "(!SSC)");

	return 0;
}

static const char * const supplies[] = {
	"vpcie3v3",
	"vpcie3v3aux",
	"vpcie12v",
};

static void *alloc_subdev_regulators(struct device *dev)
{
	const size_t size = sizeof(struct subdev_regulators) +
		sizeof(struct regulator_bulk_data) * ARRAY_SIZE(supplies);
	struct subdev_regulators *sr;
	int i;

	sr = xzalloc(size);
	if (sr) {
		sr->num_supplies = ARRAY_SIZE(supplies);
		for (i = 0; i < ARRAY_SIZE(supplies); i++)
			sr->supplies[i].supply = supplies[i];
	}

	return sr;
}

static int brcm_phy_cntl(struct brcm_pcie *pcie, const int start)
{
	static const u32 shifts[PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_NFLDS] = {
		PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_PWRDN_SHIFT,
		PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_RESET_SHIFT,
		PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_DIG_RESET_SHIFT,};
	static const u32 masks[PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_NFLDS] = {
		PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_PWRDN_MASK,
		PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_RESET_MASK,
		PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_DIG_RESET_MASK,};
	const int beg = start ? 0 : PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_NFLDS - 1;
	const int end = start ? PCIE_DVT_PMU_PCIE_PHY_CTRL_DAST_NFLDS : -1;
	u32 tmp, combined_mask = 0;
	u32 val;
	void __iomem *base = pcie->base;
	int i, ret;

	for (i = beg; i != end; start ? i++ : i--) {
		val = start ? BIT_MASK(shifts[i]) : 0;
		tmp = readl(base + PCIE_DVT_PMU_PCIE_PHY_CTRL);
		tmp = (tmp & ~masks[i]) | (val & masks[i]);
		writel(tmp, base + PCIE_DVT_PMU_PCIE_PHY_CTRL);
		usleep(200);
		combined_mask |= masks[i];
	}

	tmp = readl(base + PCIE_DVT_PMU_PCIE_PHY_CTRL);
	val = start ? combined_mask : 0;

	ret = (tmp & combined_mask) == val ? 0 : -EIO;
	if (ret)
		pr_err("failed to %s phy\n", (start ? "start" : "stop"));

	return ret;
}

static inline int brcm_phy_start(struct brcm_pcie *pcie)
{
	return pcie->cfg->has_phy ? brcm_phy_cntl(pcie, 1) : 0;
}

static inline int brcm_phy_stop(struct brcm_pcie *pcie)
{
	return pcie->cfg->has_phy ? brcm_phy_cntl(pcie, 0) : 0;
}

static const int pcie_offsets[] = {
	[RGR1_SW_INIT_1]	= 0x9210,
	[EXT_CFG_INDEX]		= 0x9000,
	[EXT_CFG_DATA]		= 0x8000,
	[PCIE_HARD_DEBUG]	= 0x4204,
	[PCIE_INTR2_CPU_BASE]	= 0x4300,
};

static const int pcie_offsets_bcm7278[] = {
	[RGR1_SW_INIT_1]	= 0xc010,
	[EXT_CFG_INDEX]		= 0x9000,
	[EXT_CFG_DATA]		= 0x8000,
	[PCIE_HARD_DEBUG]	= 0x4204,
	[PCIE_INTR2_CPU_BASE]	= 0x4300,
};

static const int pcie_offsets_bcm7425[] = {
	[RGR1_SW_INIT_1]	= 0x8010,
	[EXT_CFG_INDEX]		= 0x8300,
	[EXT_CFG_DATA]		= 0x8304,
	[PCIE_HARD_DEBUG]	= 0x4204,
	[PCIE_INTR2_CPU_BASE]	= 0x4300,
};

static const int pcie_offsets_bcm7712[] = {
	[RGR1_SW_INIT_1]	= 0x9210,
	[EXT_CFG_INDEX]		= 0x9000,
	[EXT_CFG_DATA]		= 0x8000,
	[PCIE_HARD_DEBUG]	= 0x4304,
	[PCIE_INTR2_CPU_BASE]	= 0x4400,
};

static const struct pcie_cfg_data generic_cfg = {
	.offsets	= pcie_offsets,
	.soc_base	= GENERIC,
	.perst_set	= brcm_pcie_perst_set_generic,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm2711_cfg = {
	.offsets	= pcie_offsets,
	.soc_base	= BCM2711,
	.perst_set	= brcm_pcie_perst_set_generic,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm2712_cfg = {
	.offsets	= pcie_offsets_bcm7712,
	.soc_base	= BCM7712,
	.perst_set	= brcm_pcie_perst_set_7278,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.post_setup	= brcm_pcie_post_setup_bcm2712,
	.num_inbound_wins = 10,
};

static const struct pcie_cfg_data bcm4908_cfg = {
	.offsets	= pcie_offsets,
	.soc_base	= BCM4908,
	.perst_set	= brcm_pcie_perst_set_4908,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm7278_cfg = {
	.offsets	= pcie_offsets_bcm7278,
	.soc_base	= BCM7278,
	.perst_set	= brcm_pcie_perst_set_7278,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_7278,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm7425_cfg = {
	.offsets	= pcie_offsets_bcm7425,
	.soc_base	= BCM7425,
	.perst_set	= brcm_pcie_perst_set_generic,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm7435_cfg = {
	.offsets	= pcie_offsets,
	.soc_base	= BCM7435,
	.perst_set	= brcm_pcie_perst_set_generic,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm7216_cfg = {
	.offsets	= pcie_offsets_bcm7278,
	.soc_base	= BCM7278,
	.perst_set	= brcm_pcie_perst_set_7278,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_7278,
	.has_phy	= true,
	.num_inbound_wins = 3,
};

static const struct pcie_cfg_data bcm7712_cfg = {
	.offsets	= pcie_offsets_bcm7712,
	.perst_set	= brcm_pcie_perst_set_7278,
	.bridge_sw_init_set = brcm_pcie_bridge_sw_init_set_generic,
	.soc_base	= BCM7712,
	.num_inbound_wins = 10,
};

static const struct of_device_id brcm_pcie_match[] = {
	{ .compatible = "brcm,bcm2711-pcie", .data = &bcm2711_cfg },
	{ .compatible = "brcm,bcm2712-pcie", .data = &bcm2712_cfg },
	{ .compatible = "brcm,bcm4908-pcie", .data = &bcm4908_cfg },
	{ .compatible = "brcm,bcm7211-pcie", .data = &generic_cfg },
	{ .compatible = "brcm,bcm7216-pcie", .data = &bcm7216_cfg },
	{ .compatible = "brcm,bcm7278-pcie", .data = &bcm7278_cfg },
	{ .compatible = "brcm,bcm7425-pcie", .data = &bcm7425_cfg },
	{ .compatible = "brcm,bcm7435-pcie", .data = &bcm7435_cfg },
	{ .compatible = "brcm,bcm7445-pcie", .data = &generic_cfg },
	{ .compatible = "brcm,bcm7712-pcie", .data = &bcm7712_cfg },
	{},
};

static int brcm_pci_config_read(struct pci_bus *bus, unsigned int devfn,
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

static int brcm_pci_config_write(struct pci_bus *bus, unsigned int devfn,
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

static int brcm_pci_config_read32(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 *val)
{
	void __iomem *addr;

	addr = brcm7425_pcie_map_bus(bus, devfn, where & ~0x3);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*val = readl(addr);

	if (size <= 2)
		*val = (*val >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);

	return PCIBIOS_SUCCESSFUL;
}

static int brcm_pci_config_write32(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 val)
{
	void __iomem *addr;
	u32 mask, tmp;

	addr = brcm7425_pcie_map_bus(bus, devfn, where & ~0x3);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 4) {
		writel(val, addr);
		return PCIBIOS_SUCCESSFUL;
	}

	mask = ~(((1 << (size * 8)) - 1) << ((where & 0x3) * 8));
	tmp = readl(addr) & mask;
	tmp |= val << ((where & 0x3) * 8);
	writel(tmp, addr);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops brcm_pcie_ops = {
	.read = brcm_pci_config_read,
	.write = brcm_pci_config_write,
};

static struct pci_ops brcm7425_pcie_ops = {
	.read = brcm_pci_config_read32,
	.write = brcm_pci_config_write32,
};

static int brcm_pcie_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
    const struct of_device_id *match = of_match_node(brcm_pcie_match, np);
	const struct pcie_cfg_data *data = (struct pcie_cfg_data *)match->data;;
	struct brcm_pcie *pcie;
	struct pci_controller *pci;
    struct io_resource *iores;
	int ret;

    iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);

	pcie = xzalloc(sizeof(*pcie));
    if (!pcie)
        return -ENOMEM;
		
	pcie->dev = dev;
	pci_controller_init(&pcie->pci);
    pci = &pcie->pci;
    pci->parent = dev;
	pcie->np = np;
	pcie->cfg = data;

	pcie->base = dev_platform_ioremap_resource(dev, 0);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	pcie->clk = clk_get_optional(dev, "sw_pcie");
	if (IS_ERR(pcie->clk))
		return PTR_ERR(pcie->clk);

	ret = of_pci_get_max_link_speed(np);
	pcie->gen = (ret < 0) ? 0 : ret;

	pcie->ssc = of_property_read_bool(np, "brcm,enable-ssc");

	pcie->rescal = reset_control_get_optional(dev, "rescal");
	if (IS_ERR(pcie->rescal))
		return PTR_ERR(pcie->rescal);

	pcie->perst_reset = reset_control_get_optional(dev, "perst");
	if (IS_ERR(pcie->perst_reset))
		return PTR_ERR(pcie->perst_reset);

	pcie->bridge_reset = reset_control_get_optional(dev, "bridge");
	if (IS_ERR(pcie->bridge_reset))
		return PTR_ERR(pcie->bridge_reset);

	pcie->swinit_reset = reset_control_get_optional(dev, "swinit");
	if (IS_ERR(pcie->swinit_reset))
		return PTR_ERR(pcie->swinit_reset);

	ret = clk_prepare_enable(pcie->clk);
	if (ret) {
        pr_err("could not enable clock: %d\n", ret);
		return -ENODEV;
    }

	pcie->cfg->bridge_sw_init_set(pcie, 0);

	if (pcie->swinit_reset) {
		ret = reset_control_assert(pcie->swinit_reset);
		if (ret) {
			clk_disable_unprepare(pcie->clk);
            pr_err("could not assert reset 'swinit': %d\n", ret);
			return -EINVAL;
		}

		/* HW team recommends 1us for proper sync and propagation of reset */
		udelay(1);

		ret = reset_control_deassert(pcie->swinit_reset);
		if (ret) {
			clk_disable_unprepare(pcie->clk);
			pr_err("could not de-assert reset 'swinit': %d\n", ret);
            return -ENODEV;
		}
	}

	ret = reset_control_reset(pcie->rescal);
	if (ret) {
		clk_disable_unprepare(pcie->clk);
		pr_err("failed to deassert 'rescal' %d\n", ret);
		return ret;
	}

	ret = brcm_phy_start(pcie);
	if (ret) {
		reset_control_rearm(pcie->rescal);
		clk_disable_unprepare(pcie->clk);
		return ret;
	}

	ret = brcm_pcie_setup(pcie);
	if (ret)
		goto fail;

	pcie->hw_rev = readl(pcie->base + PCIE_MISC_REVISION);
	if (pcie->cfg->soc_base == BCM4908 &&
	    pcie->hw_rev >= BRCM_PCIE_HW_REV_3_20) {
		pr_err("hardware revision with unsupported PERST# setup\n");
		ret = -ENODEV;
		goto fail;
	}

	pci->ops = &brcm_pcie_ops;

	ret = register_pci_controller(pci);
	if (!ret && !brcm_pcie_link_up(pcie))
		ret = -ENODEV;

	if (ret)
		return ret;

	return 0;

fail:
	return ret;
}

MODULE_DEVICE_TABLE(of, brcm_pcie_match);

static struct driver brcm_pcie_driver = {
	.probe = brcm_pcie_probe,
    .of_compatible = DRV_OF_COMPAT(brcm_pcie_match),
    .name = "brcm-pcie",
};
device_platform_driver(brcm_pcie_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broadcom STB PCIe RC driver");
MODULE_AUTHOR("Broadcom");
MODULE_SOFTDEP("pre: irq_bcm2712_mip");