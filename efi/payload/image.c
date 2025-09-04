// SPDX-License-Identifier: GPL-2.0-only
/*
 * image.c - barebox EFI payload support
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
#include <image-fit.h>
#include <efi/efi-payload.h>
#include <efi/efi-device.h>

struct linux_kernel_header {
	/* first sector of the image */
	uint8_t code1[0x0020];
	uint16_t cl_magic;		/**< Magic number 0xA33F */
	uint16_t cl_offset;		/**< The offset of command line */
	uint8_t code2[0x01F1 - 0x0020 - 2 - 2];
	uint8_t setup_sects;		/**< The size of the setup in sectors */
	uint16_t root_flags;		/**< If the root is mounted readonly */
	uint16_t syssize;		/**< obsolete */
	uint16_t swap_dev;		/**< obsolete */
	uint16_t ram_size;		/**< obsolete */
	uint16_t vid_mode;		/**< Video mode control */
	uint16_t root_dev;		/**< Default root device number */
	uint16_t boot_flag;		/**< 0xAA55 magic number */

	/* second sector of the image */
	uint16_t jump;			/**< Jump instruction (this is code!) */
	uint32_t header;		/**< Magic signature "HdrS" */
	uint16_t version;		/**< Boot protocol version supported */
	uint32_t realmode_swtch;	/**< Boot loader hook */
	uint16_t start_sys;		/**< The load-low segment (obsolete) */
	uint16_t kernel_version;	/**< Points to kernel version string */
	uint8_t type_of_loader;		/**< Boot loader identifier */
	uint8_t loadflags;		/**< Boot protocol option flags */
	uint16_t setup_move_size;	/**< Move to high memory size */
	uint32_t code32_start;		/**< Boot loader hook */
	uint32_t ramdisk_image;		/**< initrd load address */
	uint32_t ramdisk_size;		/**< initrd size */
	uint32_t bootsect_kludge;	/**< obsolete */
	uint16_t heap_end_ptr;		/**< Free memory after setup end */
	uint8_t ext_loader_ver;		/**< boot loader's extension of the version number */
	uint8_t ext_loader_type;	/**< boot loader's extension of its type */
	uint32_t cmd_line_ptr;		/**< Points to the kernel command line */
	uint32_t initrd_addr_max;	/**< Highest address for initrd */
	uint32_t kernel_alignment;	/**< Alignment unit required by the kernel */
	uint8_t relocatable_kernel;	/** */
	uint8_t min_alignment;		/** */
	uint16_t xloadflags;		/** */
	uint32_t cmdline_size;		/** */
	uint32_t hardware_subarch;	/** */
	uint64_t hardware_subarch_data;	/** */
	uint32_t payload_offset;	/** */
	uint32_t payload_length;	/** */
	uint64_t setup_data;		/** */
	uint64_t pref_address;		/** */
	uint32_t init_size;		/** */
	uint32_t handover_offset;	/** */
} __attribute__ ((packed));

struct efi_mem_resource {
	efi_physical_addr_t base;
	size_t size;
} __attribute__ ((packed));

struct efi_image_data {
	struct image_data *data;

	efi_handle_t handle;
	struct efi_loaded_image *loaded_image;

	struct efi_mem_resource image_res;
	struct efi_mem_resource oftree_res;
	struct efi_mem_resource *initrd_res;
};


static void *efi_allocate_pages(efi_physical_addr_t *mem,
				size_t size,
				enum efi_allocate_type allocate_type,
				enum efi_memory_type mem_type)
{
	efi_status_t efiret;

	efiret = BS->allocate_pages(allocate_type, mem_type,
				    DIV_ROUND_UP(size, EFI_PAGE_SIZE), mem);
	if (EFI_ERROR(efiret)) {
		errno = efi_errno(efiret);
		return NULL;
	}

	return efi_phys_to_virt(*mem);
}

static void efi_free_pages(void *_mem, size_t size)
{
	efi_physical_addr_t mem = efi_virt_to_phys(_mem);

	if (mem_malloc_start() <= mem && mem < mem_malloc_end())
		free(_mem);
	else
		BS->free_pages(mem, DIV_ROUND_UP(size, EFI_PAGE_SIZE));
}

static int efi_load_file_image(const char *file,
			       struct efi_loaded_image **loaded_image,
			       efi_handle_t *h)
{
	efi_physical_addr_t mem;
	void *exe;
	char *buf;
	size_t size;
	efi_handle_t handle;
	efi_status_t efiret = EFI_SUCCESS;
	int ret;

	buf = read_file(file, &size);
	if (!buf)
		return -ENOMEM;

	exe = efi_allocate_pages(&mem, size, EFI_ALLOCATE_ANY_PAGES,
				 EFI_LOADER_CODE);
	if (!exe) {
		pr_err("Failed to allocate pages for image\n");
		ret = -ENOMEM;
		goto free_buf;
	}

	memcpy(exe, buf, size);

	efiret = BS->load_image(false, efi_parent_image, efi_device_path, exe,
				size, &handle);
	if (EFI_ERROR(efiret)) {
		ret = -efi_errno(efiret);
		pr_err("failed to LoadImage: %s\n", efi_strerror(efiret));
		goto free_mem;
	}

	efiret = BS->open_protocol(handle, &efi_loaded_image_protocol_guid,
				   (void **)loaded_image, efi_parent_image,
				   NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(efiret)) {
		ret = -efi_errno(efiret);
		pr_err("failed to OpenProtocol: %s\n", efi_strerror(efiret));
		BS->unload_image(handle);
		goto free_mem;
	}

	*h = handle;
	free(buf);

	return 0;

free_mem:
	efi_free_pages(exe, size);
free_buf:
	free(buf);

	return ret;
}

static bool is_linux_image(enum filetype filetype, const void *base)
{
	const struct linux_kernel_header *hdr = base;

	if (IS_ENABLED(CONFIG_X86) &&
	    hdr->boot_flag == 0xAA55 && hdr->header == 0x53726448)
		return true;

	if (IS_ENABLED(CONFIG_ARM64) &&
	    filetype == filetype_arm64_efi_linux_image)
		return true;

	return false;
}

static int efi_execute_image(efi_handle_t handle,
			     struct efi_loaded_image *loaded_image,
			     enum filetype filetype)
{
	efi_status_t efiret;
	const char *options;
	bool is_driver;

	is_driver =
		(loaded_image->image_code_type == EFI_BOOT_SERVICES_CODE) ||
		(loaded_image->image_code_type == EFI_RUNTIME_SERVICES_CODE);

	if (is_linux_image(filetype, loaded_image->image_base)) {
		pr_debug("Linux kernel detected. Adding bootargs.");
		options = linux_bootargs_get();
		pr_info("add linux options '%s'\n", options);
		if (options) {
			loaded_image->load_options =
				xstrdup_char_to_wchar(options);
			loaded_image->load_options_size =
				(strlen(options) + 1) * sizeof(wchar_t);
		}
		shutdown_barebox();
	}

	efi_pause_devices();

	efiret = BS->start_image(handle, NULL, NULL);
	if (EFI_ERROR(efiret))
		pr_err("failed to StartImage: %s\n", efi_strerror(efiret));

	efi_continue_devices();

	if (!is_driver)
		BS->unload_image(handle);

	efi_connect_all();
	efi_register_devices();

	return -efi_errno(efiret);
}

typedef void (*handover_fn)(void *image, struct efi_system_table *table,
			    struct linux_kernel_header *header);

static inline void linux_efi_handover(efi_handle_t handle,
				      struct linux_kernel_header *header)
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
	struct linux_kernel_header *image_header, *boot_header;

	ret = efi_load_file_image(data->os_file, &loaded_image, &handle);
	if (ret)
		return ret;

	image_header = (struct linux_kernel_header *)loaded_image->image_base;

	if (image_header->boot_flag != 0xAA55 ||
	    image_header->header != 0x53726448 ||
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

	boot_header->code32_start =
		efi_virt_to_phys(loaded_image->image_base +
				 (image_header->setup_sects + 1) * 512);

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

static bool ramdisk_is_fit(struct image_data *data)
{
	struct stat st;

	if (bootm_signed_images_are_forced())
		return true;

	if (data->initrd_file) {
		if (!stat(data->initrd_file, &st) && st.st_size > 0)
			return false;
	}

	return data->os_fit ? fit_has_image(data->os_fit,
			data->fit_config, "ramdisk") > 0 : false;
}

static bool fdt_is_fit(struct image_data *data)
{
	struct stat st;

	if (bootm_signed_images_are_forced())
		return true;

	if (data->oftree_file) {
		if (!stat(data->oftree_file, &st) && st.st_size > 0)
			return false;
	}

	return data->os_fit ? fit_has_image(data->os_fit,
			data->fit_config, "fdt") > 0 : false;
}

static int efi_load_os(struct efi_image_data *e)
{
	efi_status_t efiret = EFI_SUCCESS;
	efi_physical_addr_t mem;
	size_t image_size = 0;
	void *image = NULL;
	void *vmem = NULL;
	int ret = 0;

	if (!e->data->os_fit)
		return efi_load_file_image(e->data->os_file,
			&e->loaded_image, &e->handle);

	image = (void *)e->data->fit_kernel;
	image_size = e->data->fit_kernel_size;

	if (image_size <= 0 || !image)
		return -EINVAL;

	vmem = efi_allocate_pages(&mem, image_size, EFI_ALLOCATE_ANY_PAGES,
				 EFI_LOADER_CODE);
	if (!vmem) {
		pr_err("Failed to allocate pages for image\n");
		return -ENOMEM;
	}

	memcpy(vmem, image, image_size);

	efiret = BS->load_image(false, efi_parent_image, efi_device_path, image,
				image_size, &e->handle);
	if (EFI_ERROR(efiret)) {
		ret = -efi_errno(efiret);
		pr_err("failed to LoadImage: %s\n", efi_strerror(efiret));
		goto out_mem;
	};

	efiret = BS->open_protocol(e->handle, &efi_loaded_image_protocol_guid,
				   (void **)&e->loaded_image, efi_parent_image,
				   NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(efiret)) {
		ret = -efi_errno(efiret);
		pr_err("failed to OpenProtocol: %s\n", efi_strerror(efiret));
		goto out_unload;
	}

	e->image_res.base = mem;
	e->image_res.size = image_size;

	return 0;

out_mem:
	efi_free_pages(vmem, image_size);
out_unload:
	BS->unload_image(e->handle);
	return ret;
}

static void efi_unload_os(struct efi_image_data *e)
{
	BS->close_protocol(e->handle, &efi_loaded_image_protocol_guid,
				 efi_parent_image, NULL);

	BS->unload_image(e->handle);
	efi_free_pages(efi_phys_to_virt(e->image_res.base),
				 e->image_res.size);
}

static int efi_load_ramdisk(struct efi_image_data *e)
{
	void *vmem, *tmp = NULL;
	efi_physical_addr_t mem;
	efi_status_t efiret = EFI_SUCCESS;
	const void *initrd;
	unsigned long initrd_size;
	bool from_fit;
	int ret;

	from_fit = ramdisk_is_fit(e->data);

	if (from_fit) {
		ret = fit_open_image(e->data->os_fit, e->data->fit_config,
				     "ramdisk", &initrd, &initrd_size);
		if (ret) {
			pr_err("Cannot open ramdisk image in FIT image: %pe\n",
			       ERR_PTR(ret));
			return ret;
		}
	}

	if (!from_fit) {
		if (!e->data->initrd_file)
			return 0;

		pr_info("Loading ramdisk from '%s'\n", e->data->initrd_file);
		tmp = read_file(e->data->initrd_file, &initrd_size);
		if (!tmp || initrd_size <= 0) {
			pr_err("Failed to read initrd from file: %s\n",
				e->data->initrd_file);
			return -EINVAL;
		}
		initrd = tmp;
	}

	efiret = BS->allocate_pool(EFI_LOADER_DATA,
			sizeof(struct efi_mem_resource),
			(void **)&e->initrd_res);
	if (EFI_ERROR(efiret) || !e->initrd_res) {
		ret = -efi_errno(efiret);
		pr_err("Failed to allocate initrd %s/n", efi_strerror(efiret));
		goto free_mem;
	}

	vmem = efi_allocate_pages(&mem, initrd_size,
				 EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA);
	if (!vmem) {
		pr_err("Failed to allocate pages for initrd data\n");
		ret = -ENOMEM;
		goto free_pool;
	}

	memcpy(vmem, (void *)initrd, initrd_size);
	e->initrd_res->base = (uint64_t)mem;
	e->initrd_res->size = (uint64_t)initrd_size;

	if (IS_ENABLED(CONFIG_EFI_INITRD_INSTALL)) {
		efiret = BS->install_configuration_table(
			&efi_linux_initrd_media_guid,
			(void *)e->initrd_res);
		if (EFI_ERROR(efiret)) {
			ret = -efi_errno(efiret);
			pr_err("Failed to install INITRD %s/n",
					efi_strerror(efiret));
			goto free_pages;
		}
	} else {
		ret = efi_initrd_register(vmem, initrd_size);
		if (ret) {
			pr_err("Failed to register INITRD %s/n",
				strerror(efiret));
			goto free_pages;
		}
	}

	if (!from_fit && tmp)
		free(tmp);

	return 0;

free_pages:
	efi_free_pages(vmem, initrd_size);
free_pool:
	BS->free_pool(e->initrd_res);
free_mem:
	if (!from_fit && tmp)
		free(tmp);

	return ret;
}

static void efi_unload_ramdisk(struct efi_image_data *e)
{

	if (IS_ENABLED(CONFIG_EFI_INITRD_INSTALL))
		BS->install_configuration_table(
			&efi_linux_initrd_media_guid, NULL);
	else
		efi_initrd_unregister();

	efi_free_pages(efi_phys_to_virt(e->initrd_res->base),
				 e->initrd_res->size);

	BS->free_pool(e->initrd_res);
	e->initrd_res = NULL;
}

static int efi_load_fdt(struct efi_image_data *e)
{
	efi_status_t efiret = EFI_SUCCESS;
	efi_physical_addr_t mem;
	void *vmem, *tmp = NULL;
	const void *of_tree;
	unsigned long of_size;
	bool from_fit;
	int ret;

	if (IS_ENABLED(CONFIG_EFI_FDT_FORCE))
		return 0;

	from_fit = fdt_is_fit(e->data);
	if (from_fit) {
		ret = fit_open_image(e->data->os_fit, e->data->fit_config,
				     "fdt", &of_tree, &of_size);
		if (ret) {
			pr_err("Cannot open FDT image in FIT image: %pe\n",
			       ERR_PTR(ret));
			return ret;
		}
	}

	if (!from_fit) {
		if (!e->data->oftree_file)
			return 0;

		pr_info("Loading devicetree from '%s'\n", e->data->oftree_file);
		tmp = read_file(e->data->oftree_file, &of_size);
		if (!tmp || of_size <= 0) {
			pr_err("Failed to read initrd from file: %s\n",
				e->data->initrd_file);
			return -EINVAL;
		}
		of_tree = tmp;
	}

	vmem = efi_allocate_pages(&mem, SZ_128K,
				 EFI_ALLOCATE_ANY_PAGES,
				 EFI_ACPI_RECLAIM_MEMORY);
	if (!vmem) {
		pr_err("Failed to allocate pages for FDT\n");
		ret = -ENOMEM;
		goto free_file;
	}

	memcpy(vmem, of_tree, of_size);

	efiret = BS->install_configuration_table(&efi_fdt_guid,
			(void *)mem);
	if (EFI_ERROR(efiret)) {
		pr_err("Failed to install FDT %s/n", efi_strerror(efiret));
		ret = -efi_errno(efiret);
		goto free_mem;
	}

	e->oftree_res.base = mem;
	e->oftree_res.size = SZ_128K;

	if (!from_fit)
		free(tmp);

	return 0;

free_mem:
	efi_free_pages(vmem, SZ_128K);
free_file:
	if (!from_fit)
		free(tmp);

	return ret;
}

static void efi_unload_fdt(struct efi_image_data *e)
{
	BS->install_configuration_table(&efi_fdt_guid, NULL);

	efi_free_pages(efi_phys_to_virt(e->oftree_res.base),
				 e->oftree_res.size);
}

static int do_bootm_efi_stub(struct image_data *data)
{
	struct efi_image_data e = {.data = data};
	enum filetype type;
	int ret = 0;

	ret = efi_load_os(&e);
	if (ret)
		return ret;

	ret = efi_load_fdt(&e);
	if (ret)
		goto unload_os;

	ret = efi_load_ramdisk(&e);
	if (ret)
		goto unload_oftree;

	type = file_detect_type(e.loaded_image->image_base, PAGE_SIZE);
	ret = efi_execute_image(e.handle, e.loaded_image, type);
	if (ret)
		goto unload_ramdisk;

	return 0;

unload_ramdisk:
	if (e.initrd_res)
		efi_unload_ramdisk(&e);
unload_oftree:
	efi_unload_fdt(&e);
unload_os:
	efi_unload_os(&e);
	return ret;
}

static struct image_handler efi_handle_tr = {
	.name = "EFI Application",
	.bootm = do_bootm_efi,
	.filetype = filetype_exe,
};

static struct image_handler efi_arm64_handle_tr = {
	.name = "EFI ARM64 Linux kernel",
	.bootm = do_bootm_efi_stub,
	.filetype = filetype_arm64_efi_linux_image,
};

static int efi_execute(struct binfmt_hook *b, char *file, int argc, char **argv)
{
	int ret;
	efi_handle_t handle;
	struct efi_loaded_image *loaded_image;

	ret = efi_load_file_image(file, &loaded_image, &handle);
	if (ret)
		return ret;

	return efi_execute_image(handle, loaded_image, b->type);
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

	if (IS_ENABLED(CONFIG_ARM64)) {
		register_image_handler(&efi_arm64_handle_tr);
		binfmt_register(&binfmt_arm64_efi_hook);
	}

	return 0;
}
late_efi_initcall(efi_register_image_handler);
