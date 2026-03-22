#ifndef __ASSEMBLY__

#include <asm/io.h>

/*
 * SpacemiT K1 - PXA/XScale style UART
 * Base: 0xD4017000 (uart0)
 * Registers are 32-bit aligned (shift=2)
 *
 * Offsets (shifted by 2):
 *   THR  @ 0x00  (TX holding register)
 *   IER  @ 0x04  (interrupt enable, bit7 = UUE)
 *   LSR  @ 0x14  (line status register)
 *
 * LSR bits:
 *   bit5 = THRE  (TX holding register empty)
 *   bit6 = TEMT  (TX empty, both THR and TSR empty)
 */

#define UART_THR    0x00
#define UART_IER    0x04
#define UART_LSR    0x14

#define UART_IER_UUE    (1 << 6)
#define UART_LSR_THRE   (1 << 5)

static inline void PUTC_LL(char ch)
{
	void __iomem *base = IOMEM(0xD4017000);

	while (!(readl(base + UART_LSR) & UART_LSR_THRE))
		;

	writel(ch, base + UART_THR);
}

static inline void k1_uart_enable(void)
{
	void __iomem *base = IOMEM(0xD4017000);

	writel(UART_IER_UUE, base + UART_IER);
}

#define debug_ll_init() k1_uart_enable()

#endif /* __ASSEMBLY__ */