// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Sascha Hauer <s.hauer@pengutronix.de>
 */
#include <linux/sizes.h>
#include <common.h>
#include <malloc.h>
#include <efi.h>
#include <efi/efi-util.h>
#include <string.h>
#include <errno.h>

struct alloc_header {
	size_t size;     /* requested size */
	int from_efi;    /* 0 = memalign, 1 = EFI pages */
};

void malloc_stats(void)
{
	/* optional: implement stats if needed */
}

static void *efi_malloc(size_t size) {
	efi_status_t efiret;
	efi_physical_addr_t mem;
	size_t pages = DIV_ROUND_UP(size, EFI_PAGE_SIZE);

	efiret = BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES,
				    EFI_LOADER_DATA, pages, &mem);
	if (EFI_ERROR(efiret)) {
		errno = efi_errno(efiret);
		return NULL;
	}

	return efi_phys_to_virt(mem);
}

static void efi_free(void *ptr, size_t size) {
	efi_physical_addr_t phys = efi_virt_to_phys(ptr);
	size_t pages = DIV_ROUND_UP(size, EFI_PAGE_SIZE);
	BS->free_pages(phys, pages);
}

void *memalign(size_t alignment, size_t bytes)
{
	void *mem = NULL;

	if (!bytes)
		return ZERO_SIZE_PTR;

	if (alignment <= MALLOC_MAX_SIZE && bytes <= MALLOC_MAX_SIZE)
		mem = sbrk(bytes + alignment);
	if (!mem) {
		errno = ENOMEM;
		return NULL;
	}

	return PTR_ALIGN(mem, alignment);
}

void *malloc(size_t size)
{
	struct alloc_header *hdr;
	void *mem;

	if (!size)
		return ZERO_SIZE_PTR;

	/* if asked size is bigger than 1MB allocate from efi*/
	if (size >= SZ_1M)
		goto efi_alloc;

	/* try memalign first, to use boot service memory */
	mem = memalign(CONFIG_MALLOC_ALIGNMENT, size + sizeof(*hdr));
	if (mem && !errno) {
		hdr = (struct alloc_header *)mem;
		hdr->from_efi = 0;
		goto return_ptr;
	}

efi_alloc:
	/* if no memory left from boot services fallback here*/
	mem = efi_malloc(size + sizeof(*hdr));
	if (!mem)
		return NULL;

	hdr = (struct alloc_header *)mem;
	hdr->from_efi = 1;
		
return_ptr:
	hdr->size = size;
	return (void *)(hdr + 1);
}

void free(void *ptr)
{
	struct alloc_header *hdr;

	if (!ptr)
		return;

	hdr = (struct alloc_header *)ptr - 1;

	if (hdr->from_efi)
		efi_free(hdr, hdr->size + sizeof(*hdr));
}

size_t malloc_usable_size(void *mem)
{
	struct alloc_header *hdr;

	if (!mem)
		return 0;

	hdr = (struct alloc_header *)mem - 1;
	return hdr->size;
}

void *realloc(void *ptr, size_t size)
{
	struct alloc_header *hdr;
	void *n_mem;
	size_t old_sz;

	if (!ptr)
		return malloc(size);

	if (!size) {
		free(ptr);
		return NULL;
	}

	hdr = (struct alloc_header *)ptr - 1;
	old_sz = hdr->size;

	n_mem = malloc(size);
	if (!n_mem) {
		errno = ENOMEM;
		return NULL;
	}

	memcpy(n_mem, ptr, (old_sz < size) ? old_sz : size);
	free(ptr);

	return n_mem;
}
