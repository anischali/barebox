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

	putc_ll('>');
	/* On POR, we are running from read-only memory here. */

	fdt = __dtb_z_spacemit_k1_start + get_runtime_offset();

	barebox_riscv_machine_entry(0xC0000000, SZ_256M, fdt);
}


/* Need the its for barebox 

		barebox {
            description = "Barebox";
            data = /incbin/("/home/anicha1/sources/barebox/images/barebox-spacemit-k1.img");
            type = "standalone";
            arch = "riscv";
            os = "barebox";
            compression = "none";
            load = <0x0 0x00200000>;    
            hash-1 {
                algo = "crc32";
            };
        };
*/