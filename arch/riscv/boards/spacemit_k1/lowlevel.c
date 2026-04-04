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
    
    if (a1 != 0xffffffff) {
        fdt = (void *)a1;
    } else {
        fdt = __dtb_z_spacemit_k1_start + get_runtime_offset();
    }

	barebox_riscv_supervisor_entry(0x80000000, SZ_128M, a0, fdt);
}