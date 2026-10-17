/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <linux/sizes.h>
#include <soc/spacemit/k1x.h>

/*
 * On-chip SRAM is 256K at 0xC0800000; the BootROM loads the FSBL 4K in,
 * at K1X_SRAM_BASE. Anything above 0xC0840000 does not hold data.
 */
#define STACK_TOP	(K1X_SRAM_BASE - SZ_4K + SZ_256K)
#define GLOBAL_PTR	(STACK_TOP - SZ_16K)

	/* .text_head_entry* is placed first by pbl.lds, name must match the image */
	.section .text_head_entry_start_pbl_spacemit_k1x, "ax"
	.option norvc
	.option norelax

SYM_FUNC_START(start_pbl_spacemit_k1x)
	/* barebox RISC-V image header, see asm/barebox-riscv-head.h */
	nop
	j	1f
	.balign	8
	.dword	0			/* load offset */
	.dword	_barebox_image_size
	.dword	0			/* flags */
	.word	0			/* version */
	.word	0
	.dword	0
	.asciz	"barebox"
	.balign	8
	.ascii	"RSCV"
	.word	0
1:
	auipc	s0, 0			/* load PC to s0 */
	csrw	mie, zero		/* disable interrupts */

	csrr	t0, mhartid		/* park all harts except hart 0 */
	bnez	t0, .Lpark

	lla	t0, mtrap_entry		/* install mtrap */
	csrw	mtvec, t0

	li	sp, STACK_TOP
    li  gp, GLOBAL_PTR

	csrw	satp, zero		/* disable MMU */
	sfence.vma

	tail	spacemit_k1x_start

.Lpark:
	wfi
	j	.Lpark
SYM_FUNC_END(start_pbl_spacemit_k1x)
