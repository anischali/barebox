// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/sizes.h>
#include <stdio.h>
#include <asm/common.h>
#include <efi/protocol/text.h>
#include <efi/payload.h>
#include <efi/mode.h>
#include <pbl.h>
#include <pbl/handoff-data.h>
#include <efi/guid.h>

asmlinkage void __efistub_efi_pe_entry(void *image, struct efi_system_table *sys_table);

static inline void * efi_fdt_find(struct efi_system_table *efi_sys_table)
{
	struct efi_config_table *ect;

	for_each_efi_config_table(ect) {
		struct fdt_header *oftree;
		u32 magic;

		
		if (efi_guidcmp(ect->guid, EFI_DEVICE_TREE_GUID))
			continue;

		oftree = (void *)ect->table;
		magic = be32_to_cpu(oftree->magic);

		if (magic != FDT_MAGIC) {
			pr_err("table has invalid magic 0x%08x\n", magic);
			return ERR_PTR(-EILSEQ);
		}

		return oftree;
	}

	return NULL;
}

/*
 * Put these in the data section so that they survive the clearing of the
 * BSS segment.
 */
static __attribute__ ((section(".data"))) bool is_efi_payload;

bool efi_is_payload(void)
{
	return is_efi_payload;
}

static void efi_putc(void *ctx, int ch)
{
	struct efi_system_table *sys_table = ctx;
	wchar_t ws[2] = { ch, L'\0' };

	sys_table->con_out->output_string(sys_table->con_out, ws);
}

void __efistub_efi_pe_entry(void *image, struct efi_system_table *sys_table)
{
	void *mem;
	static struct barebox_efi_data efidata;
	void *fdt;

#ifdef DEBUG
	sys_table->con_out->output_string(sys_table->con_out, L"\nbarebox\n");
#endif
	pbl_set_putc(efi_putc, sys_table);

	is_efi_payload = true;
	efidata.image = image;
	efidata.sys_table = sys_table;

	handoff_data_add(HANDOFF_DATA_EFI, &efidata, sizeof(efidata));

	mem = efi_earlymem_alloc(sys_table, SZ_16M, EFI_BOOT_SERVICES_CODE);

	fdt = efi_fdt_find(sys_table);
	if (IS_ERR_OR_NULL(fdt)) {
		pr_err("no valid FDT found\n");
		fdt = NULL;
	}

	barebox_pbl_entry((uintptr_t)mem, SZ_16M, fdt);
}
