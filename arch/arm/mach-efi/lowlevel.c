#include <common.h>
#include <asm/barebox-arm-head.h>
#include <asm/system.h>
#include <mach/efi/lowlevel.h>


void efi_lowlevel_init(void) {
    arm_cpu_lowlevel_init();
}
