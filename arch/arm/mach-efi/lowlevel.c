#include <common.h>
#include <asm/barebox-arm-head.h>
#include <asm/system.h>
#include <mach/efi/lowlevel.h>


void efi_lowlevel_init(void) {

#ifdef CONFIG_ARMV8_SWITCH_EL
    if (current_el() == 2)
        armv8_switch_to_el1();
#endif
}
