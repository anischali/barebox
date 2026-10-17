// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Anis Chali
 */

#include <common.h>
#include <linux/sizes.h>
#include <firmware.h>
#include <linux/sizes.h>
#include <io.h>
#include <debug_ll.h>
#include <asm/sections.h>
#include <asm/mmu.h>
#include <asm/csr.h>
#include <asm/system.h>

#include <soc/spacemit/k1x.h>

#include "lowlevel.h"

static uint32_t adjust_cpu_freq(uint64_t cluster, uint32_t freq)
{
	uint32_t freq_act=freq, val;

	// switch cpu clock source 
	val = readl((void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));
	val &= ~(0x07 | BIT(13));
	switch(freq) {
	case 1600000:
		val |= 0x07;
		break;

	case 1228000:
		val |= 0x04;
		break;

	case 819000:
		val |= 0x01;
		break;

	case 614000:
	default:
		freq_act = 614000;
		val |= 0x00;
		break;
	}
	writel(val, (void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));

	// set cluster frequency change request, and wait done
	val = readl((void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));
	val |= BIT(12);
	writel(val, (void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4));
	while(readl((void __iomem *)(K1X_APMU_BASE + 0x38c + cluster*4)) & BIT(12));

	return freq_act;
}

static void raise_cpu_frequency(void)
{
	uint32_t val, cpu_freq;

	writel(0x2dffff, (void __iomem *)0xd4051024);

	//enable CLK_1228M 
	val = readl((void __iomem *)(K1X_MPMU_BASE + 0x1024));
	val |= BIT(16) | BIT(15) | BIT(14) | BIT(13);
	writel(val, (void __iomem *)(K1X_MPMU_BASE + 0x1024));

	// enable PLL3(3200Mhz)
	val = readl((void __iomem *)(K1X_APB_SPARE_BASE + 0x12C));
	val |= BIT(31);
	writel(val, (void __iomem *)(K1X_APB_SPARE_BASE + 0x12C));
	// enable PLL3_DIV2
	val = readl((void __iomem *)(K1X_APB_SPARE_BASE + 0x128));
	val |= BIT(1);
	writel(val, (void __iomem *)(K1X_APB_SPARE_BASE + 0x128));

	cpu_freq = adjust_cpu_freq(0, 1228000);
	pr_info("adjust cluster-0 frequency to %u ...	[done]\n", cpu_freq);

	cpu_freq = adjust_cpu_freq(1, 614000);
	pr_info("adjust cluster-1 frequency to %u ...	[done]\n", cpu_freq);
}


static void spacemit_init_hw(void) {

	raise_cpu_frequency();
}

void __noreturn noinline spacemit_k1x_start(void)
{
	relocate_to_current_adr();
	setup_c();
	debug_ll_init();
	
	pr_info("Successful bringup!\n");

	spacemit_init_hw();

	pr_info("Successful bringup!\n");

	while (1)
		asm volatile("wfi");
}
