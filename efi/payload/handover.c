// SPDX-License-Identifier: GPL-2.0-only
/*
 * handover.c - legacy x86 EFI handover protocol
 *
 * Copyright (c) 2014 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#include <clock.h>
#include <common.h>
#include <linux/sizes.h>
#include <linux/ktime.h>
#include <memory.h>
#include <command.h>
#include <magicvar.h>
#include <init.h>
#include <driver.h>
#include <io.h>
#include <efi.h>
#include <malloc.h>
#include <string.h>
#include <linux/err.h>
#include <boot.h>
#include <bootm.h>
#include <fs.h>
#include <libfile.h>
#include <binfmt.h>
#include <wchar.h>
#include <efi/efi-payload.h>
#include <efi/efi-device.h>

#include "image.h"
#include "setup_header.h"

static int efi_do_execute_image(enum filetype filetype, const char *file)
{
	efi_handle_t handle;
	struct efi_loaded_image *loaded_image;
	int ret;

	ret = efi_load_image(file, &loaded_image, &handle);
	if (ret)
		return ret;

	return efi_execute_image(handle, loaded_image, filetype);
}

typedef void(*handover_fn)(void *image, struct efi_system_table *table,
			   struct x86_setup_header *header);

static inline void linux_efi_handover(efi_handle_t handle,
				      struct x86_setup_header *header)
{
	handover_fn handover;
	uintptr_t addr;

	addr = header->code32_start + header->handover_offset;
	if (IS_ENABLED(CONFIG_X86_64))
		addr += 512;

	handover = efi_phys_to_virt(addr);
	handover(handle, efi_sys_table, header);
}

static int do_bootm_efi(struct image_data *data)
{
	void *tmp;
	void *initrd = NULL;
	size_t size;
	efi_handle_t handle;
	int ret;
	const char *options;
	struct efi_loaded_image *loaded_image;
	struct x86_setup_header *image_header, *boot_header;

	ret = efi_load_image(data->os_file, &loaded_image, &handle);
	if (ret)
		return ret;

	image_header = (struct x86_setup_header *)loaded_image->image_base;

	if (!is_x86_setup_header(image_header) ||
	    image_header->version < 0x20b ||
	    !image_header->relocatable_kernel) {
		pr_err("Not a valid kernel image!\n");
		BS->unload_image(handle);
		return -EINVAL;
	}

	boot_header = xmalloc(0x4000);
	memset(boot_header, 0, 0x4000);
	memcpy(boot_header, image_header, sizeof(*image_header));

	/* Refer to Linux kernel commit a27e292b8a54
	 * ("Documentation/x86/boot: Reserve type_of_loader=13 for barebox")
	 */
	boot_header->type_of_loader = 0x13;

	if (data->initrd_file) {
		tmp = read_file(data->initrd_file, &size);
		initrd = xmemalign(PAGE_SIZE, PAGE_ALIGN(size));
		memcpy(initrd, tmp, size);
		memset(initrd + size, 0, PAGE_ALIGN(size) - size);
		free(tmp);
		boot_header->ramdisk_image = efi_virt_to_phys(initrd);
		boot_header->ramdisk_size = PAGE_ALIGN(size);
	}

	options = linux_bootargs_get();
	if (options) {
		boot_header->cmd_line_ptr = efi_virt_to_phys(options);
		boot_header->cmdline_size = strlen(options);
	}

	boot_header->code32_start = efi_virt_to_phys(loaded_image->image_base +
			(image_header->setup_sects+1) * 512);

	if (bootm_verbose(data)) {
		printf("\nStarting kernel at 0x%p", loaded_image->image_base);
		if (data->initrd_file)
			printf(", initrd at 0x%08x",
			       boot_header->ramdisk_image);
		printf("...\n");
	}

	if (data->dryrun) {
		BS->unload_image(handle);
		free(boot_header);
		free(initrd);
		return 0;
	}

	efi_set_variable_usec("LoaderTimeExecUSec", &efi_systemd_vendor_guid,
			      ktime_to_us(ktime_get()));

	shutdown_barebox();
	linux_efi_handover(handle, boot_header);

	return 0;
}

static struct image_handler efi_handle_tr = {
	.name = "EFI Application",
	.bootm = do_bootm_efi,
	.filetype = filetype_exe,
};

static int efi_execute(struct binfmt_hook *b, char *file, int argc, char **argv)
{
	return efi_do_execute_image(b->type, file);
}

static struct binfmt_hook binfmt_efi_hook = {
	.type = filetype_exe,
	.hook = efi_execute,
};

static int do_bootm_mbr(struct image_data *data)
{
	/* On x86, Linux kernel images have a MBR magic at the end of
	 * the first 512 byte sector and a PE magic if they're EFI-stubbed.
	 * The PE magic has precedence over the MBR, so if we arrive in
	 * this boot handler, the kernel has no EFI stub.
	 *
	 * Print a descriptive error message instead of "no image handler
	 * found for image type MBR sector".
	 */
	pr_err("Can't boot MBR sector: Is CONFIG_EFI_STUB disabled in your Linux kernel config?\n");
	return -ENOSYS;
}

static struct image_handler non_efi_handle_linux_x86 = {
	.name = "non-EFI x86 Linux Image",
	.bootm = do_bootm_mbr,
	.filetype = filetype_mbr,
};

static struct binfmt_hook binfmt_arm64_efi_hook = {
	.type = filetype_arm64_efi_linux_image,
	.hook = efi_execute,
};

static int efi_register_image_handler(void)
{
	register_image_handler(&efi_handle_tr);
	binfmt_register(&binfmt_efi_hook);

	if (IS_ENABLED(CONFIG_X86))
		register_image_handler(&non_efi_handle_linux_x86);

	if (IS_ENABLED(CONFIG_ARM64))
		binfmt_register(&binfmt_arm64_efi_hook);

	return 0;
}
late_efi_initcall(efi_register_image_handler);
