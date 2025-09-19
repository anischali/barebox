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
	size_t size; /* requested size */
};

void *efi_malloc(size_t size)
{
	efi_status_t efiret;
	efi_physical_addr_t mem;
	struct alloc_header *hdr;
	size_t pages;

	if (!size)
		return ZERO_SIZE_PTR;

	pages = DIV_ROUND_UP(size, EFI_PAGE_SIZE);
	efiret = BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
				    pages, &mem);
	if (EFI_ERROR(efiret)) {
		errno = efi_errno(efiret);
		return NULL;
	}

	hdr = (struct alloc_header *)efi_phys_to_virt(mem);
	hdr->size = size;
	return (void *)(hdr + 1);
}

void efi_free(void *ptr)
{
	efi_physical_addr_t phys;
	struct alloc_header *hdr;

	if (!ptr)
		return;

	hdr = (struct alloc_header *)ptr - 1;
	phys = efi_virt_to_phys(hdr);
	BS->free_pages(phys, DIV_ROUND_UP(hdr->size, EFI_PAGE_SIZE));
}

void *efi_realloc(void *ptr, size_t size)
{
	struct alloc_header *hdr;
	void *n_mem;
	size_t old_sz;

	if (!ptr)
		return efi_malloc(size);

	if (!size) {
		efi_free(ptr);
		return NULL;
	}

	hdr = (struct alloc_header *)ptr - 1;
	old_sz = hdr->size;

	n_mem = efi_malloc(size);
	if (!n_mem) {
		errno = ENOMEM;
		return NULL;
	}

	memcpy(n_mem, ptr, (old_sz < size) ? old_sz : size);
	efi_free(ptr);

	return n_mem;
}
