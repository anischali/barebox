// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "efi-fdt: " fmt

#include <common.h>
#include <init.h>
#include <libfile.h>
#include <efi.h>
#include <efi/efi-payload.h>
#include <efi/efi-device.h>

static int efi_fdt_save(void) {
	struct efi_config_table *ect;
	struct fdt_header *oftree;
	u32 magic, size;
	int ret;

	for_each_efi_config_table(ect) {

		if (efi_guidcmp(ect->guid, EFI_DEVICE_TREE_GUID))
			continue;

		oftree = (void *)ect->table;
		magic = be32_to_cpu(oftree->magic);

		if (magic != FDT_MAGIC) {
			pr_err("table has invalid magic 0x%08x\n", magic);
			return -EILSEQ;
		}

		size = be32_to_cpu(oftree->totalsize);
		ret = write_file("/efi.dtb", oftree, size);
		if (ret) {
			pr_err("error saving /efi.dtb: %pe\n", ERR_PTR(ret));
			return ret;
		}

		return 0;
	}
	
	return 0;
}
late_efi_initcall(efi_fdt_save);

static int efi_fdt_memory_probe(void)
{
	struct efi_config_table *ect;
	struct fdt_header *oftree;
	struct device_node *root, *memory;
	u32 magic, size;

	for_each_efi_config_table(ect) {

		if (efi_guidcmp(ect->guid, EFI_DEVICE_TREE_GUID))
			continue;

		oftree = (void *)ect->table;
		magic = be32_to_cpu(oftree->magic);

		if (magic != FDT_MAGIC) {
			pr_err("table has invalid magic 0x%08x\n", magic);
			return -EILSEQ;
		}

		size = be32_to_cpu(oftree->totalsize);
		root = of_unflatten_dtb(oftree, size);
		memory = root;
		while (1) {
			memory = of_find_node_by_type(memory, "memory");
			if (!memory)
				break;

			pr_info("add memory: %p\n", memory);
			of_add_memory(memory, true);
		}

		return 0;
	}

	return 0;
}
core_efi_initcall(efi_fdt_memory_probe);
