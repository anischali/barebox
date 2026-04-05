// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <asm/barebox-riscv.h>
#include <debug_ll.h>
#include <asm/riscv_nmon.h>

ENTRY_FUNCTION(start_spacemit_k1, a0, a1, a2)
{
	extern char __dtb_z_spacemit_k1_start[];
	void *fdt;

	debug_ll_init();
    
    if (a1 && fdt_check_header((void *)a1) == 0) {
        fdt = (void *)a1;
    } else {
        fdt = __dtb_z_spacemit_k1_start + get_runtime_offset();
    }


    puthex_ll((unsigned long)fdt);
    putc_ll('\n');

	barebox_riscv_supervisor_entry(0xC0020000, SZ_1G, a0, fdt);
}