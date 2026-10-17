/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SPACEMIT_K1_LOWLEVEL_H
#define __SPACEMIT_K1_LOWLEVEL_H

#include <linux/compiler.h>

/* FSBL C entry, called from entry.S */
void __noreturn spacemit_k1x_start(void);

#endif /* __SPACEMIT_K1_LOWLEVEL_H */
