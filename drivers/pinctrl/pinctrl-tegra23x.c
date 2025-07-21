// SPDX-License-Identifier: GPL-2.0+
/*
 * Pinctrl data for the NVIDIA tegra23x pinmux
 * implemented from linux pinctrl-tegra234
 *
 * Copyright (c) 2025 Anis Chali <anis.chali@ametek.com>
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <malloc.h>
#include <pinctrl.h>
#include <linux/err.h>


struct pinctrl_tegra23x_drvdata;

struct pinctrl_tegra23x {
	struct {
		u32 __iomem *ctrl;
		u32 __iomem *mux;
	} regs;
	struct pinctrl_device pinctrl;
	const struct pinctrl_tegra23x_drvdata *drvdata;
};

struct tegra_pingroup {
	const char *name;
	const char *funcs[4];
	u32 reg;
};

struct tegra_drive_pingroup {
	const char *name;
	u32 reg;
	u32 drvdn_bit:5;
	u32 drvup_bit:5;
	u32 slwr_bit:5;
	u32 slwf_bit:5;
	u32 drvtype_bit:5;
	u32 schmitt_bit:5;
	u32 drvdn_width:6;
	u32 drvup_width:6;
	u32 slwr_width:6;
	u32 slwf_width:6;
};

struct pinctrl_tegra23x_drvdata {
	const struct tegra_pingroup *pingrps;
	const unsigned int num_pingrps;
	const struct tegra_drive_pingroup *drvgrps;
	const unsigned int num_drvgrps;
};

#define PG(pg_name, f0, f1, f2, f3, offset)		\
	{						\
		.name = #pg_name,			\
		.funcs = { #f0, #f1, #f2, #f3, },	\
		.reg = offset				\
	}

#define DRV_PG(pg_name, r, drvdn_b, drvdn_w, drvup_b, drvup_w, \
				slwr_b, slwr_w, slwf_b, slwf_w, schmitt_b) \
	{									\
		.name = "drive_" #pg_name,		\
		.reg = r,						\
		.drvdn_bit = drvdn_b,			\
		.drvup_bit = drvup_b,			\
		.slwr_bit = slwr_b,				\
		.slwf_bit = slwf_b,				\
		.drvtype_bit = 13,				\
		.schmitt_bit = schmitt_b, 		\
		.drvdn_width = drvdn_w,			\
		.drvup_width = drvup_w,			\
		.slwr_width = slwr_w,			\
		.slwf_width = slwf_w,			\
	}

	#define DRV_PINGROUP_ENTRY_Y(r, drvdn_b, drvdn_w, drvup_b,	\
			     drvup_w, slwr_b, slwr_w, slwf_b,	\
			     slwf_w, bank)			\
		.drv_reg = DRV_PINGROUP_Y(r),			\
		.drv_bank = bank,				\
		.drvdn_bit = drvdn_b,				\
		.drvdn_width = drvdn_w,				\
		.drvup_bit = drvup_b,				\
		.drvup_width = drvup_w,				\
		.slwr_bit = slwr_b,				\
		.slwr_width = slwr_w,				\
		.slwf_bit = slwf_b,				\
		.slwf_width = slwf_w

static const struct tegra_pingroup tegra234_pin_groups[] = {
	PG(soc_gpio08_pb0,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x5008),
	PG(soc_gpio36_pm5,	eth0,		rsvd1,		dca,		rsvd3,		0x10000),
	PG(soc_gpio53_pm6,	eth0,		rsvd1,		dca,		rsvd3,		0x10008),
	PG(soc_gpio55_pm4,	eth2,		rsvd1,		rsvd2,		rsvd3,		0x10010),
	PG(soc_gpio38_pm7,	eth1,		rsvd1,		rsvd2,		rsvd3,		0x10018),
	PG(soc_gpio39_pn1,	gp,		rsvd1,		rsvd2,		rsvd3,		0x10020),
	PG(soc_gpio40_pn2,	eth1,		rsvd1,		rsvd2,		rsvd3,		0x10028),
	PG(dp_aux_ch0_hpd_pm0,	dp,		rsvd1,		rsvd2,		rsvd3,		0x10030),
	PG(dp_aux_ch1_hpd_pm1,	eth3,		rsvd1,		rsvd2,		rsvd3,		0x10038),
	PG(dp_aux_ch2_hpd_pm2,	eth3,		rsvd1,		displayb,	rsvd3,		0x10040),
	PG(dp_aux_ch3_hpd_pm3,	eth2,		rsvd1,		displaya,	rsvd3,		0x10048),
	PG(dp_aux_ch1_p_pn3,	i2c4,		rsvd1,		rsvd2,		rsvd3,		0x10050),
	PG(dp_aux_ch1_n_pn4,	i2c4,		rsvd1,		rsvd2,		rsvd3,		0x10058),
	PG(dp_aux_ch2_p_pn5,	i2c7,		rsvd1,		rsvd2,		rsvd3,		0x10060),
	PG(dp_aux_ch2_n_pn6,	i2c7,		rsvd1,		rsvd2,		rsvd3,		0x10068),
	PG(dp_aux_ch3_p_pn7,	i2c9,		rsvd1,		rsvd2,		rsvd3,		0x10070),
	PG(dp_aux_ch3_n_pn0,	i2c9,		rsvd1,		rsvd2,		rsvd3,		0x10078),
	PG(eqos_td3_pe4,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15000),
	PG(eqos_td2_pe3,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15008),
	PG(eqos_td1_pe2,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15010),
	PG(eqos_td0_pe1,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15018),
	PG(eqos_rd3_pf1,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15020),
	PG(eqos_rd2_pf0,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15028),
	PG(eqos_rd1_pe7,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15030),
	PG(eqos_sma_mdio_pf4,	eqos,		rsvd1,		rsvd2,		rsvd3,		0x15038),
	PG(eqos_rd0_pe6,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15040),
	PG(eqos_sma_mdc_pf5,	eqos,		rsvd1,		rsvd2,		rsvd3,		0x15048),
	PG(eqos_comp,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15050),
	PG(eqos_txc_pe0,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15058),
	PG(eqos_rxc_pf3,		eqos,		rsvd1,		rsvd2,		rsvd3,		0x15060),
	PG(eqos_tx_ctl_pe5,	eqos,		rsvd1,		rsvd2,		rsvd3,		0x15068),
	PG(eqos_rx_ctl_pf2,	eqos,		rsvd1,		rsvd2,		rsvd3,		0x15070),
	PG(pex_l2_clkreq_n_pk4,	pe2,		rsvd1,		rsvd2,		rsvd3,		0x7000),
	PG(pex_wake_n_pl2,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x7008),
	PG(pex_l1_clkreq_n_pk2,	pe1,		rsvd1,		rsvd2,		rsvd3,		0x7010),
	PG(pex_l1_rst_n_pk3,	pe1,		rsvd1,		rsvd2,		rsvd3,		0x7018),
	PG(pex_l0_clkreq_n_pk0,	pe0,		rsvd1,		rsvd2,		rsvd3,		0x7020),
	PG(pex_l0_rst_n_pk1,	pe0,		rsvd1,		rsvd2,		rsvd3,		0x7028),
	PG(pex_l2_rst_n_pk5,	pe2,		rsvd1,		rsvd2,		rsvd3,		0x7030),
	PG(pex_l3_clkreq_n_pk6,	pe3,		rsvd1,		rsvd2,		rsvd3,		0x7038),
	PG(pex_l3_rst_n_pk7,	pe3,		rsvd1,		rsvd2,		rsvd3,		0x7040),
	PG(pex_l4_clkreq_n_pl0,	pe4,		rsvd1,		rsvd2,		rsvd3,		0x7048),
	PG(pex_l4_rst_n_pl1,	pe4,		rsvd1,		rsvd2,		rsvd3,		0x7050),
	PG(soc_gpio34_pl3,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x7058),
	PG(pex_l5_clkreq_n_paf0,	pe5,		rsvd1,		rsvd2,		rsvd3,		0x14000),
	PG(pex_l5_rst_n_paf1,	pe5,		rsvd1,		rsvd2,		rsvd3,		0x14008),
	PG(pex_l6_clkreq_n_paf2,  pe6,		rsvd1,		rsvd2,		rsvd3,		0x14010),
	PG(pex_l6_rst_n_paf3,	pe6,		rsvd1,		rsvd2,		rsvd3,		0x14018),
	PG(pex_l10_clkreq_n_pag6,	pe10,		rsvd1,		rsvd2,		rsvd3,		0x19000),
	PG(pex_l10_rst_n_pag7,	pe10,		rsvd1,		rsvd2,		rsvd3,		0x19008),
	PG(pex_l7_clkreq_n_pag0,	pe7,		rsvd1,		rsvd2,		rsvd3,		0x19010),
	PG(pex_l7_rst_n_pag1,	pe7,		rsvd1,		rsvd2,		rsvd3,		0x19018),
	PG(pex_l8_clkreq_n_pag2,	pe8,		rsvd1,		rsvd2,		rsvd3,		0x19020),
	PG(pex_l8_rst_n_pag3,	pe8,		rsvd1,		rsvd2,		rsvd3,		0x19028),
	PG(pex_l9_clkreq_n_pag4,	pe9,		rsvd1,		rsvd2,		rsvd3,		0x19030),
	PG(pex_l9_rst_n_pag5,	pe9,		rsvd1,		rsvd2,		rsvd3,		0x19038),
	PG(qspi0_io3_pc5,		qspi0,		rsvd1,		rsvd2,		rsvd3,		0xb000),
	PG(qspi0_io2_pc4,		qspi0,		rsvd1,		rsvd2,		rsvd3,		0xb008),
	PG(qspi0_io1_pc3,		qspi0,		rsvd1,		rsvd2,		rsvd3,		0xb010),
	PG(qspi0_io0_pc2,		qspi0,		rsvd1,		rsvd2,		rsvd3,		0xb018),
	PG(qspi0_sck_pc0,		qspi0,		rsvd1,		rsvd2,		rsvd3,		0xb020),
	PG(qspi0_cs_n_pc1,	qspi0,		rsvd1,		rsvd2,		rsvd3,		0xb028),
	PG(qspi1_io3_pd3,		qspi1,		rsvd1,		rsvd2,		rsvd3,		0xb030),
	PG(qspi1_io2_pd2,		qspi1,		rsvd1,		rsvd2,		rsvd3,		0xb038),
	PG(qspi1_io1_pd1,		qspi1,		rsvd1,		rsvd2,		rsvd3,		0xb040),
	PG(qspi1_io0_pd0,		qspi1,		rsvd1,		rsvd2,		rsvd3,		0xb048),
	PG(qspi1_sck_pc6,		qspi1,		rsvd1,		rsvd2,		rsvd3,		0xb050),
	PG(qspi1_cs_n_pc7,	qspi1,		rsvd1,		rsvd2,		rsvd3,		0xb058),
	PG(qspi_comp,		qspi,		rsvd1,		rsvd2,		rsvd3,		0xb060),
	PG(sdmmc1_clk_pj0,	sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8000),
	PG(sdmmc1_cmd_pj1,	sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8008),
	PG(sdmmc1_comp,		sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8010),
	PG(sdmmc1_dat3_pj5,	sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8018),
	PG(sdmmc1_dat2_pj4,	sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8020),
	PG(sdmmc1_dat1_pj3,	sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8028),
	PG(sdmmc1_dat0_pj2,	sdmmc1,		rsvd1,		rsvd2,		rsvd3,		0x8030),
	PG(ufs0_rst_n_pae1,	ufs0,		rsvd1,		rsvd2,		rsvd3,		0x11000),
	PG(ufs0_ref_clk_pae0,	ufs0,		rsvd1,		rsvd2,		rsvd3,		0x11008),
	PG(spi3_miso_py1,		spi3,		rsvd1,		rsvd2,		rsvd3,		0xd000),
	PG(spi1_cs0_pz6,		spi1,		rsvd1,		rsvd2,		rsvd3,		0xd008),
	PG(spi3_cs0_py3,		spi3,		rsvd1,		rsvd2,		rsvd3,		0xd010),
	PG(spi1_miso_pz4,		spi1,		rsvd1,		rsvd2,		rsvd3,		0xd018),
	PG(spi3_cs1_py4,		spi3,		rsvd1,		rsvd2,		rsvd3,		0xd020),
	PG(spi1_sck_pz3,		spi1,		rsvd1,		rsvd2,		rsvd3,		0xd028),
	PG(spi3_sck_py0,		spi3,		rsvd1,		rsvd2,		rsvd3,		0xd030),
	PG(spi1_cs1_pz7,		spi1,		rsvd1,		rsvd2,		rsvd3,		0xd038),
	PG(spi1_mosi_pz5,		spi1,		rsvd1,		rsvd2,		rsvd3,		0xd040),
	PG(spi3_mosi_py2,		spi3,		rsvd1,		rsvd2,		rsvd3,		0xd048),
	PG(uart2_tx_px4,		uartb,		rsvd1,		rsvd2,		rsvd3,		0xd050),
	PG(uart2_rx_px5,		uartb,		rsvd1,		rsvd2,		rsvd3,		0xd058),
	PG(uart2_rts_px6,		uartb,		rsvd1,		rsvd2,		rsvd3,		0xd060),
	PG(uart2_cts_px7,		uartb,		rsvd1,		rsvd2,		rsvd3,		0xd068),
	PG(uart5_tx_py5,		uarte,		rsvd1,		rsvd2,		rsvd3,		0xd070),
	PG(uart5_rx_py6,		uarte,		rsvd1,		rsvd2,		rsvd3,		0xd078),
	PG(uart5_rts_py7,		uarte,		rsvd1,		rsvd2,		rsvd3,		0xd080),
	PG(uart5_cts_pz0,		uarte,		rsvd1,		rsvd2,		rsvd3,		0xd088),
	PG(gpu_pwr_req_px0,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0xd090),
	PG(gp_pwm3_px3,		gp,		rsvd1,		rsvd2,		rsvd3,		0xd098),
	PG(gp_pwm2_px2,		gp,		rsvd1,		rsvd2,		rsvd3,		0xd0a0),
	PG(cv_pwr_req_px1,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0xd0a8),
	PG(usb_vbus_en0_pz1,	usb,		rsvd1,		rsvd2,		rsvd3,		0xd0b0),
	PG(usb_vbus_en1_pz2,	usb,		rsvd1,		rsvd2,		rsvd3,		0xd0b8),
	PG(extperiph2_clk_pp1,	extperiph2,	rsvd1,		rsvd2,		rsvd3,		0x0000),
	PG(extperiph1_clk_pp0,	extperiph1,	rsvd1,		rsvd2,		rsvd3,		0x0008),
	PG(cam_i2c_sda_pp3,	i2c3,		vi0,		rsvd2,		vi1,		0x0010),
	PG(cam_i2c_scl_pp2,	i2c3,		vi0,		vi0_alt,	vi1,		0x0018),
	PG(soc_gpio23_pp4,	vi0,		vi0_alt,	vi1,		vi1_alt,	0x0020),
	PG(soc_gpio24_pp5,	vi0,		soc,		vi1,		vi1_alt,	0x0028),
	PG(soc_gpio25_pp6,	vi0,		i2s5,		vi1,		dmic1,		0x0030),
	PG(pwr_i2c_scl_pp7,	i2c5,		rsvd1,		rsvd2,		rsvd3,		0x0038),
	PG(pwr_i2c_sda_pq0,	i2c5,		rsvd1,		rsvd2,		rsvd3,		0x0040),
	PG(soc_gpio28_pq1,	vi0,		rsvd1,		vi1,		rsvd3,		0x0048),
	PG(soc_gpio29_pq2,	rsvd0,		nv,		rsvd2,		rsvd3,		0x0050),
	PG(soc_gpio30_pq3,	rsvd0,		wdt,		rsvd2,		rsvd3,		0x0058),
	PG(soc_gpio31_pq4,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x0060),
	PG(soc_gpio32_pq5,	rsvd0,		extperiph3,	dcb,		rsvd3,		0x0068),
	PG(soc_gpio33_pq6,	rsvd0,		extperiph4,	dcb,		rsvd3,		0x0070),
	PG(soc_gpio35_pq7,	rsvd0,		i2s5,		dmic1,		rsvd3,		0x0078),
	PG(soc_gpio37_pr0,	gp,		i2s5,		dmic4,		dspk1,		0x0080),
	PG(soc_gpio56_pr1,	rsvd0,		i2s5,		dmic4,		dspk1,		0x0088),
	PG(uart1_cts_pr5,		uarta,		rsvd1,		rsvd2,		rsvd3,		0x0090),
	PG(uart1_rts_pr4,		uarta,		rsvd1,		rsvd2,		rsvd3,		0x0098),
	PG(uart1_rx_pr3,		uarta,		rsvd1,		rsvd2,		rsvd3,		0x00a0),
	PG(uart1_tx_pr2,		uarta,		rsvd1,		rsvd2,		rsvd3,		0x00a8),
	PG(cpu_pwr_req_pi5,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x4000),
	PG(uart4_cts_ph6,		uartd,		rsvd1,		i2s7,		rsvd3,		0x4008),
	PG(uart4_rts_ph5,		uartd,		spi4,		rsvd2,		rsvd3,		0x4010),
	PG(uart4_rx_ph4,		uartd,		rsvd1,		i2s7,		rsvd3,		0x4018),
	PG(uart4_tx_ph3,		uartd,		spi4,		rsvd2,		rsvd3,		0x4020),
	PG(gen1_i2c_scl_pi3,	i2c1,		rsvd1,		rsvd2,		rsvd3,		0x4028),
	PG(gen1_i2c_sda_pi4,	i2c1,		rsvd1,		rsvd2,		rsvd3,		0x4030),
	PG(soc_gpio20_pg7,	rsvd0,		sdmmc1,		rsvd2,		rsvd3,		0x4038),
	PG(soc_gpio21_ph0,	rsvd0,		gp,		i2s7,		rsvd3,		0x4040),
	PG(soc_gpio22_ph1,	rsvd0,		rsvd1,		i2s7,		rsvd3,		0x4048),
	PG(soc_gpio13_pg0,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x4050),
	PG(soc_gpio14_pg1,	rsvd0,		spi4,		rsvd2,		rsvd3,		0x4058),
	PG(soc_gpio15_pg2,	rsvd0,		spi4,		rsvd2,		rsvd3,		0x4060),
	PG(soc_gpio16_pg3,	rsvd0,		spi4,		rsvd2,		rsvd3,		0x4068),
	PG(soc_gpio17_pg4,	rsvd0,		ccla,		rsvd2,		rsvd3,		0x4070),
	PG(soc_gpio18_pg5,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x4078),
	PG(soc_gpio19_pg6,	gp,		rsvd1,		rsvd2,		rsvd3,		0x4080),
	PG(soc_gpio41_ph7,	rsvd0,		i2s2,		rsvd2,		rsvd3,		0x4088),
	PG(soc_gpio42_pi0,	rsvd0,		i2s2,		rsvd2,		rsvd3,		0x4090),
	PG(soc_gpio43_pi1,	rsvd0,		i2s2,		rsvd2,		rsvd3,		0x4098),
	PG(soc_gpio44_pi2,	rsvd0,		i2s2,		rsvd2,		rsvd3,		0x40a0),
	PG(soc_gpio06_ph2,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x40a8),
	PG(soc_gpio07_pi6,	gp,		rsvd1,		rsvd2,		rsvd3,		0x40b0),
	PG(dap4_sclk_pa4,		i2s4,		rsvd1,		rsvd2,		rsvd3,		0x2000),
	PG(dap4_dout_pa5,		i2s4,		rsvd1,		rsvd2,		rsvd3,		0x2008),
	PG(dap4_din_pa6,		i2s4,		rsvd1,		rsvd2,		rsvd3,		0x2010),
	PG(dap4_fs_pa7,		i2s4,		rsvd1,		rsvd2,		rsvd3,		0x2018),
	PG(dap6_sclk_pa0,		i2s6,		rsvd1,		rsvd2,		rsvd3,		0x2020),
	PG(dap6_dout_pa1,		i2s6,		rsvd1,		rsvd2,		rsvd3,		0x2028),
	PG(dap6_din_pa2,		i2s6,		rsvd1,		rsvd2,		rsvd3,		0x2030),
	PG(dap6_fs_pa3,		i2s6,		rsvd1,		rsvd2,		rsvd3,		0x2038),
	PG(soc_gpio45_pad0,	rsvd0,		i2s1,		rsvd2,		rsvd3,		0x18000),
	PG(soc_gpio46_pad1,	rsvd0,		i2s1,		rsvd2,		rsvd3,		0x18008),
	PG(soc_gpio47_pad2,	rsvd0,		i2s1,		rsvd2,		rsvd3,		0x18010),
	PG(soc_gpio48_pad3,	rsvd0,		i2s1,		rsvd2,		rsvd3,		0x18018),
	PG(soc_gpio57_pac4,	rsvd0,		i2s8,		rsvd2,		sdmmc1,		0x18020),
	PG(soc_gpio58_pac5,	rsvd0,		i2s8,		rsvd2,		sdmmc1,		0x18028),
	PG(soc_gpio59_pac6,	aud,		i2s8,		rsvd2,		rsvd3,		0x18030),
	PG(soc_gpio60_pac7,	rsvd0,		i2s8,		nv,		igpu,		0x18038),
	PG(spi5_cs0_pac3,		spi5,		i2s3,		dmic2,		rsvd3,		0x18040),
	PG(spi5_miso_pac1,	spi5,		i2s3,		dspk0,		rsvd3,		0x18048),
	PG(spi5_mosi_pac2,	spi5,		i2s3,		dmic2,		rsvd3,		0x18050),
	PG(spi5_sck_pac0,		spi5,		i2s3,		dspk0,		rsvd3,		0x18058),
};

static const struct tegra_drive_pingroup tegra234_drive_groups[] = {
	DRV_PG(soc_gpio08_pb0, 0x500c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio36_pm5, 0x10004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio53_pm6, 0x1000c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio55_pm4, 0x10014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio38_pm7, 0x1001c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio39_pn1, 0x10024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio40_pn2, 0x1002c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch0_hpd_pm0, 0x10034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch1_hpd_pm1, 0x1003c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch2_hpd_pm2, 0x10044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch3_hpd_pm3, 0x1004c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch1_p_pn3, 0x10054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch1_n_pn4, 0x1005c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch2_p_pn5, 0x10064,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch2_n_pn6, 0x1006c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch3_p_pn7, 0x10074,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dp_aux_ch3_n_pn0, 0x1007c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l2_clkreq_n_pk4, 0x7004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_wake_n_pl2, 0x700c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l1_clkreq_n_pk2, 0x7014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l1_rst_n_pk3, 0x701c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l0_clkreq_n_pk0, 0x7024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l0_rst_n_pk1, 0x702c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l2_rst_n_pk5, 0x7034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l3_clkreq_n_pk6, 0x703c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l3_rst_n_pk7, 0x7044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l4_clkreq_n_pl0, 0x704c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l4_rst_n_pl1, 0x7054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio34_pl3, 0x705c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l5_clkreq_n_paf0, 0x14004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l5_rst_n_paf1, 0x1400c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l6_clkreq_n_paf2, 0x14014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l6_rst_n_paf3, 0x1401c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l10_clkreq_n_pag6, 0x19004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l10_rst_n_pag7, 0x1900c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l7_clkreq_n_pag0, 0x19014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l7_rst_n_pag1, 0x1901c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l8_clkreq_n_pag2, 0x19024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l8_rst_n_pag3, 0x1902c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l9_clkreq_n_pag4, 0x19034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pex_l9_rst_n_pag5, 0x1903c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sdmmc1_clk_pj0, 0x8004,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sdmmc1_cmd_pj1, 0x800c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sdmmc1_dat3_pj5, 0x801c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sdmmc1_dat2_pj4, 0x8024,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sdmmc1_dat1_pj3, 0x802c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sdmmc1_dat0_pj2, 0x8034,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(ufs0_rst_n_pae1, 0x11004,	12,	5,	24,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(ufs0_ref_clk_pae0, 0x1100c,	12,	5,	24,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi3_miso_py1, 0xd004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi1_cs0_pz6, 0xd00c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi3_cs0_py3, 0xd014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi1_miso_pz4, 0xd01c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi3_cs1_py4, 0xd024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi1_sck_pz3, 0xd02c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi3_sck_py0, 0xd034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi1_cs1_pz7, 0xd03c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi1_mosi_pz5, 0xd044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi3_mosi_py2, 0xd04c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart2_tx_px4, 0xd054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart2_rx_px5, 0xd05c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart2_rts_px6, 0xd064,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart2_cts_px7, 0xd06c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart5_tx_py5, 0xd074,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart5_rx_py6, 0xd07c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart5_rts_py7, 0xd084,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart5_cts_pz0, 0xd08c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gpu_pwr_req_px0, 0xd094,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gp_pwm3_px3, 0xd09c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gp_pwm2_px2, 0xd0a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(cv_pwr_req_px1, 0xd0ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(usb_vbus_en0_pz1, 0xd0b4,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(usb_vbus_en1_pz2, 0xd0bc,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(extperiph2_clk_pp1, 0x0004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(extperiph1_clk_pp0, 0x000c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(cam_i2c_sda_pp3, 0x0014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(cam_i2c_scl_pp2, 0x001c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio23_pp4, 0x0024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio24_pp5, 0x002c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio25_pp6, 0x0034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pwr_i2c_scl_pp7, 0x003c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(pwr_i2c_sda_pq0, 0x0044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio28_pq1, 0x004c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio29_pq2, 0x0054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio30_pq3, 0x005c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio31_pq4, 0x0064,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio32_pq5, 0x006c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio33_pq6, 0x0074,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio35_pq7, 0x007c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio37_pr0, 0x0084,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio56_pr1, 0x008c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart1_cts_pr5, 0x0094,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart1_rts_pr4, 0x009c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart1_rx_pr3, 0x00a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart1_tx_pr2, 0x00ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(cpu_pwr_req_pi5, 0x4004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart4_cts_ph6, 0x400c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart4_rts_ph5, 0x4014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart4_rx_ph4, 0x401c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart4_tx_ph3, 0x4024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gen1_i2c_scl_pi3, 0x402c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gen1_i2c_sda_pi4, 0x4034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio20_pg7, 0x403c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio21_ph0, 0x4044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio22_ph1, 0x404c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio13_pg0, 0x4054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio14_pg1, 0x405c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio15_pg2, 0x4064,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio16_pg3, 0x406c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio17_pg4, 0x4074,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio18_pg5, 0x407c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio19_pg6, 0x4084,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio41_ph7, 0x408c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio42_pi0, 0x4094,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio43_pi1, 0x409c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio44_pi2, 0x40a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio06_ph2, 0x40ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio07_pi6, 0x40b4,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap4_sclk_pa4, 0x2004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap4_dout_pa5, 0x200c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap4_din_pa6, 0x2014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap4_fs_pa7, 0x201c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap6_sclk_pa0, 0x2024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap6_dout_pa1, 0x202c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap6_din_pa2, 0x2034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(dap6_fs_pa3, 0x203c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio45_pad0, 0x18004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio46_pad1, 0x1800c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio47_pad2, 0x18014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio48_pad3, 0x1801c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio57_pac4, 0x18024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio58_pac5, 0x1802c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio59_pac6, 0x18034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio60_pac7, 0x1803c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi5_cs0_pac3, 0x18044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi5_miso_pac1, 0x1804c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi5_mosi_pac2, 0x18054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi5_sck_pac0, 0x1805c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(eqos_td3_pe4, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_td2_pe3, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_td1_pe2, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_td0_pe1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_rd3_pf1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_rd2_pf0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_rd1_pe7, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_sma_mdio_pf4, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_rd0_pe6, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_sma_mdc_pf5, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_comp, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	DRV_PG(eqos_txc_pe0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_rxc_pf3, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_tx_ctl_pe5, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(eqos_rx_ctl_pf2, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi0_io3_pc5, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi0_io2_pc4, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi0_io1_pc3, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi0_io0_pc2, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi0_sck_pc0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi0_cs_n_pc1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi1_io3_pd3, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi1_io2_pd2, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi1_io1_pd1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi1_io0_pd0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi1_sck_pc6, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi1_cs_n_pc7, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12),
	DRV_PG(qspi_comp, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	DRV_PG(sdmmc1_comp, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
};

__maybe_unused static const struct pinctrl_tegra23x_drvdata tegra234_drvdata = {
	.pingrps = tegra234_pin_groups,
	.num_pingrps = ARRAY_SIZE(tegra234_pin_groups),
	.drvgrps = tegra234_drive_groups,
	.num_drvgrps = ARRAY_SIZE(tegra234_drive_groups),
};

static const struct tegra_pingroup tegra234_aon_groups[] = {
	PG(touch_clk_pcc4,	gp,		touch,		rsvd2,		rsvd3,		0x2000),
	PG(uart3_rx_pcc6,		uartc,		uartj,		rsvd2,		rsvd3,		0x2008),
	PG(uart3_tx_pcc5,		uartc,		uartj,		rsvd2,		rsvd3,		0x2010),
	PG(gen8_i2c_sda_pdd2,	i2c8,		rsvd1,		rsvd2,		rsvd3,		0x2018),
	PG(gen8_i2c_scl_pdd1,	i2c8,		rsvd1,		rsvd2,		rsvd3,		0x2020),
	PG(spi2_mosi_pcc2,	spi2,		rsvd1,		rsvd2,		rsvd3,		0x2028),
	PG(gen2_i2c_scl_pcc7,	i2c2,		rsvd1,		rsvd2,		rsvd3,		0x2030),
	PG(spi2_cs0_pcc3,		spi2,		rsvd1,		rsvd2,		rsvd3,		0x2038),
	PG(gen2_i2c_sda_pdd0,	i2c2,		rsvd1,		rsvd2,		rsvd3,		0x2040),
	PG(spi2_sck_pcc0,		spi2,		rsvd1,		rsvd2,		rsvd3,		0x2048),
	PG(spi2_miso_pcc1,	spi2,		rsvd1,		rsvd2,		rsvd3,		0x2050),
	PG(can1_dout_paa2,	can1,		rsvd1,		rsvd2,		rsvd3,		0x3000),
	PG(can1_din_paa3,		can1,		rsvd1,		rsvd2,		rsvd3,		0x3008),
	PG(can0_dout_paa0,	can0,		rsvd1,		rsvd2,		rsvd3,		0x3010),
	PG(can0_din_paa1,		can0,		rsvd1,		rsvd2,		rsvd3,		0x3018),
	PG(can0_stb_paa4,		rsvd0,		wdt,		tsc,		tsc_alt,	0x3020),
	PG(can0_en_paa5,		rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x3028),
	PG(soc_gpio49_paa6,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x3030),
	PG(can0_err_paa7,		rsvd0,		tsc,		rsvd2,		tsc_alt,	0x3038),
	PG(can1_stb_pbb0,		rsvd0,		dmic3,		dmic5,		rsvd3,		0x3040),
	PG(can1_en_pbb1,		rsvd0,		dmic3,		dmic5,		rsvd3,		0x3048),
	PG(soc_gpio50_pbb2,	rsvd0,		tsc,		rsvd2,		tsc_alt,	0x3050),
	PG(can1_err_pbb3,		rsvd0,		tsc,		rsvd2,		tsc_alt,	0x3058),
	PG(sce_error_pee0,	sce,		rsvd1,		rsvd2,		rsvd3,		0x1010),
	PG(batt_oc_pee3,		soc,		rsvd1,		rsvd2,		rsvd3,		0x1020),
	PG(bootv_ctl_n_pee7,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x1028),
	PG(power_on_pee4,		rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x1038),
	PG(soc_gpio26_pee5,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x1040),
	PG(soc_gpio27_pee6,	rsvd0,		rsvd1,		rsvd2,		rsvd3,		0x1048),
	PG(ao_retention_n_pee2,	gpio,		led,		rsvd2,		istctrl,	0x1050),
	PG(vcomp_alert_pee1,	soc,		rsvd1,		rsvd2,		rsvd3,		0x1058),
	PG(hdmi_cec_pgg0,		hdmi,		rsvd1,		rsvd2,		rsvd3,		0x1060),
};

static const struct tegra_drive_pingroup tegra234_aon_drive_groups[] = {
	DRV_PG(touch_clk_pcc4, 0x2004,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart3_rx_pcc6, 0x200c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(uart3_tx_pcc5, 0x2014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gen8_i2c_sda_pdd2, 0x201c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gen8_i2c_scl_pdd1, 0x2024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi2_mosi_pcc2, 0x202c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gen2_i2c_scl_pcc7, 0x2034,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi2_cs0_pcc3, 0x203c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(gen2_i2c_sda_pdd0, 0x2044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi2_sck_pcc0, 0x204c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(spi2_miso_pcc1, 0x2054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can1_dout_paa2, 0x3004,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can1_din_paa3, 0x300c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can0_dout_paa0, 0x3014,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can0_din_paa1, 0x301c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can0_stb_paa4, 0x3024,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can0_en_paa5, 0x302c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio49_paa6, 0x3034,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can0_err_paa7, 0x303c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can1_stb_pbb0, 0x3044,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can1_en_pbb1, 0x304c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio50_pbb2, 0x3054,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(can1_err_pbb3, 0x305c,	28,	2,	30,	2,	-1,	-1,	-1,	-1, 12),
	DRV_PG(sce_error_pee0, 0x1014,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(batt_oc_pee3, 0x1024,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(bootv_ctl_n_pee7, 0x102c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(power_on_pee4, 0x103c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio26_pee5, 0x1044,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(soc_gpio27_pee6, 0x104c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(ao_retention_n_pee2, 0x1054,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(vcomp_alert_pee1, 0x105c,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12),
	DRV_PG(hdmi_cec_pgg0, 0x1064,	12,	5,	20,	5,	-1,	-1,	-1,	-1, 12)
};

__maybe_unused static const struct pinctrl_tegra23x_drvdata tegra124_aon_drvdata = {
	.pingrps = tegra234_aon_groups,
	.num_pingrps = ARRAY_SIZE(tegra234_aon_groups),
	.drvgrps = tegra234_aon_drive_groups,
	.num_drvgrps = ARRAY_SIZE(tegra234_aon_drive_groups),
};

static int pinctrl_tegra23x_set_drvstate(struct pinctrl_tegra23x *ctrl,
                                        struct device_node *np)
{
	const char *pins = NULL;
	const struct tegra_drive_pingroup *group = NULL;
	int schmitt = -1, pds = -1, pus = -1, srr = -1, srf = -1;
	int i;
	u32 __iomem *regaddr;
	u32 val;

	if (of_property_read_string(np, "nvidia,pins", &pins))
		return 0;

	for (i = 0; i < ctrl->drvdata->num_drvgrps; i++) {
		if (!strcmp(pins, ctrl->drvdata->drvgrps[i].name)) {
			group = &ctrl->drvdata->drvgrps[i];
			break;
		}
	}

	if (!group)
		return 0;

	regaddr = ctrl->regs.ctrl + (group->reg >> 2);

	of_property_read_u32_array(np, "nvidia,schmitt", &schmitt, 1);
	of_property_read_u32_array(np, "nvidia,pull-down-strength", &pds, 1);
	of_property_read_u32_array(np, "nvidia,pull-up-strength", &pus, 1);
	of_property_read_u32_array(np, "nvidia,slew-rate-rising", &srr, 1);
	of_property_read_u32_array(np, "nvidia,slew-rate-falling", &srf, 1);

	if (schmitt >= 0) {
		val = readl(regaddr);
		val &= ~(0x1 << group->schmitt_bit);
		val |= schmitt << group->schmitt_bit;
		writel(val, regaddr);
	}
	if (pds >= 0) {
		val = readl(regaddr);
		val &= ~(((1 << group->drvdn_width) - 1) << group->drvdn_bit);
		val |= pds << group->drvdn_bit;
		writel(val, regaddr);
	}
	if (pus >= 0) {
		val = readl(regaddr);
		val &= ~(((1 << group->drvup_width) - 1) << group->drvup_bit);
		val |= pus << group->drvup_bit;
		writel(val, regaddr);
	}
	if (srr >= 0) {
		val = readl(regaddr);
		val &= ~(((1 << group->slwr_width) - 1) << group->slwr_bit);
		val |= srr << group->slwr_bit;
		writel(val, regaddr);
	}
	if (srf >= 0) {
		val = readl(regaddr);
		val &= ~(((1 << group->slwf_width) - 1) << group->slwf_bit);
		val |= srf << group->slwf_bit;
		writel(val, regaddr);
	}

	return 1;
}

static void pinctrl_tegra23x_set_func(struct pinctrl_tegra23x *ctrl,
				     u32 reg, int func)
{
	u32 __iomem *regaddr = ctrl->regs.mux + (reg >> 2);
	u32 val;

	val = readl(regaddr);
	val &= ~(0x3);
	val |= func;
	writel(val, regaddr);
}

static void pinctrl_tegra23x_set_pull(struct pinctrl_tegra23x *ctrl,
				     u32 reg, int pull)
{
	u32 __iomem *regaddr = ctrl->regs.mux + (reg >> 2);
	u32 val;

	val = readl(regaddr);
	val &= ~(0x3 << 2);
	val |= pull << 2;
	writel(val, regaddr);
}

static void pinctrl_tegra23x_set_input(struct pinctrl_tegra23x *ctrl,
				      u32 reg, int input)
{
	u32 __iomem *regaddr = ctrl->regs.mux + (reg >> 2);
	u32 val;

	val = readl(regaddr);
	val &= ~(1 << 5);
	val |= input << 5;
	writel(val, regaddr);
}

static void pinctrl_tegra23x_set_tristate(struct pinctrl_tegra23x *ctrl,
					 u32 reg, int tristate)
{
	u32 __iomem *regaddr = ctrl->regs.mux + (reg >> 2);
	u32 val;

	val = readl(regaddr);
	val &= ~(1 << 4);
	val |= tristate << 4;
	writel(val, regaddr);
}

static void pinctrl_tegra23x_set_opendrain(struct pinctrl_tegra23x *ctrl,
					  u32 reg, int opendrain)
{
	u32 __iomem *regaddr = ctrl->regs.mux + (reg >> 2);
	u32 val;

	val = readl(regaddr);
	val &= ~(1 << 6);
	val |= opendrain << 6;
	writel(val, regaddr);
}

static void pinctrl_tegra23x_set_ioreset(struct pinctrl_tegra23x *ctrl,
					u32 reg, int ioreset)
{
	u32 __iomem *regaddr = ctrl->regs.mux + (reg >> 2);
	u32 val;

	val = readl(regaddr);
	val &= ~(1 << 8);
	val |= ioreset << 8;
	writel(val, regaddr);
}

static int pinctrl_tegra23x_set_state(struct pinctrl_device *pdev,
				     struct device_node *np)
{
	struct pinctrl_tegra23x *ctrl =
			container_of(pdev, struct pinctrl_tegra23x, pinctrl);
	struct device_node *childnode;
	int pull = -1, tri = -1, in = -1, od = -1, ior = -1, i, j, k;
	const char *pins, *func = NULL;
	const struct tegra_pingroup *group = NULL;

	/*
	 * At first look if the node we are pointed at has children,
	 * which we may want to visit.
	 */
	list_for_each_entry(childnode, &np->children, parent_list)
		pinctrl_tegra23x_set_state(pdev, childnode);

	/* read relevant state from devicetree */
	of_property_read_string(np, "nvidia,function", &func);
	of_property_read_u32_array(np, "nvidia,pull", &pull, 1);
	of_property_read_u32_array(np, "nvidia,tristate", &tri, 1);
	of_property_read_u32_array(np, "nvidia,enable-input", &in, 1);
	of_property_read_u32_array(np, "nvidia,open-drain", &od, 1);
	of_property_read_u32_array(np, "nvidia,io-reset", &ior, 1);

	/* iterate over all pingroups referenced in the dt node */
	for (i = 0; ; i++) {
		if (of_property_read_string_index(np, "nvidia,pins", i, &pins))
			break;

		for (j = 0; j < ctrl->drvdata->num_pingrps; j++) {
			if (!strcmp(pins, ctrl->drvdata->pingrps[j].name)) {
				group = &ctrl->drvdata->pingrps[j];
				break;
			}
		}
		/* if no matching pingroup is found */
		if (j == ctrl->drvdata->num_pingrps) {
			/* see if we can find a drivegroup */
			if (pinctrl_tegra23x_set_drvstate(ctrl, np))
				continue;

			/* nothing matching found, warn and bail out */
			dev_warn(ctrl->pinctrl.dev,
				 "invalid pingroup %s referenced in node %s\n",
				 pins, np->name);
			continue;
		}

		if (func) {
			for (k = 0; k < 4; k++) {
				if (!strcmp(func, group->funcs[k]))
					break;
			}
			if (k < 4)
				pinctrl_tegra23x_set_func(ctrl, group->reg, k);
			else
				dev_warn(ctrl->pinctrl.dev,
					 "invalid function %s for pingroup %s in node %s\n",
					 func, group->name, np->name);
		}

		if (pull >= 0)
			pinctrl_tegra23x_set_pull(ctrl, group->reg, pull);

		if (in >= 0)
			pinctrl_tegra23x_set_input(ctrl, group->reg, in);

		if (tri >= 0)
			pinctrl_tegra23x_set_tristate(ctrl, group->reg, tri);

		if (od >= 0)
			pinctrl_tegra23x_set_opendrain(ctrl, group->reg, od);

		if (ior >= 0)
			pinctrl_tegra23x_set_ioreset(ctrl, group->reg, ior);
	}

	return 0;
}

static struct pinctrl_ops pinctrl_tegra23x_ops = {
	.set_state = pinctrl_tegra23x_set_state,
};

static int pinctrl_tegra23x_probe(struct device *dev)
{
	struct resource *iores;
	struct pinctrl_tegra23x *ctrl;
	int i, ret;
	u32 **regs;

	ctrl = xzalloc(sizeof(*ctrl));

	/*
	 * Tegra pincontrol is split out into four independent memory ranges:
	 * tristate control, function mux, pullup/down control, pad control
	 * (from lowest to highest hardware address).
	 * We are only interested in the first three for now.
	 */
	regs = (u32 **)&ctrl->regs;
	for (i = 0; i <= 1; i++) {
		iores = dev_request_mem_resource(dev, i);
		if (IS_ERR(iores)) {
			dev_err(dev, "Could not get iomem region %d\n", i);
			return PTR_ERR(iores);
		}
		regs[i] = IOMEM(iores->start);
	}

	ctrl->drvdata = device_get_match_data(dev);

	ctrl->pinctrl.dev = dev;
	ctrl->pinctrl.ops = &pinctrl_tegra23x_ops;

	ret = pinctrl_register(&ctrl->pinctrl);
	if (ret) {
		free(ctrl);
		return ret;
	}

	of_pinctrl_select_state(dev->of_node, "boot");

	return 0;
}

static __maybe_unused struct of_device_id pinctrl_tegra23x_dt_ids[] = {
	{
#ifdef CONFIG_ARCH_TEGRA_234_SOC
		.compatible = "nvidia,tegra234-pinmux",
		.data = &tegra234_drvdata,
	}, {
		.compatible = "nvidia,tegra234-aon-pinmux",
		.data = &tegra124_aon_drvdata,
	}, {
#endif
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, pinctrl_tegra23x_dt_ids);

static struct driver pinctrl_tegra23x_driver = {
	.name		= "pinctrl-tegra23x",
	.probe		= pinctrl_tegra23x_probe,
	.of_compatible	= DRV_OF_COMPAT(pinctrl_tegra23x_dt_ids),
};

core_platform_driver(pinctrl_tegra23x_driver);
