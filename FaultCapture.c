/* Preserve a Cortex-M33 fault without filesystem writes or network callbacks.
 * Watchdog scratch 0..3 are loader commands only when scratch0 has the
 * loader's different magic; SDK reboot uses scratch4..7.
 */
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/structs/m33.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "FaultCapture.h"
#define FAULT_MAGIC 0x50434654u
uint32_t loader_fault_stack[256] __attribute__((aligned(8)));
void __not_in_flash_func(loader_fault_record)(uint32_t *frame, uint32_t exc_return) {
    uint32_t cfsr = m33_hw->cfsr;
    uint32_t pc = 0;
    unsigned index = (exc_return & 16u) ? 6u : 24u;
    uintptr_t sp = (uintptr_t)frame;
    // STKOF or an incomplete exception frame must not be dereferenced.
    if (!(cfsr & 0x00101010u) && sp >= SRAM_BASE &&
        sp <= SRAM_END - (index + 1u) * sizeof(uint32_t)) pc = frame[index];
    watchdog_hw->scratch[1] = cfsr;
    watchdog_hw->scratch[2] = (uint32_t)sp;
    watchdog_hw->scratch[3] = pc;
    watchdog_hw->scratch[0] = FAULT_MAGIC;
    watchdog_enable(1, false);
    while (1) tight_loop_contents();
}
void __attribute__((naked)) __not_in_flash_func(loader_hardfault_capture)(void) {
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "movs r2, #0\n"
        "msr msplim, r2\n"
        "ldr r2, =loader_fault_stack\n"
        "add.w r2, r2, #1024\n"
        "msr msp, r2\n"
        "b loader_fault_record\n"
    );
}
void loader_report_previous_fault(void) {
    if (watchdog_hw->scratch[0] != FAULT_MAGIC) return;
    uint32_t cfsr=watchdog_hw->scratch[1], sp=watchdog_hw->scratch[2], pc=watchdog_hw->scratch[3];
    watchdog_hw->scratch[0] = 0;
    MMPrintString("Previous fault CFSR="); PIntH(cfsr);
    MMPrintString(" SP="); PIntH(sp); MMPrintString(" PC="); PIntH(pc); PRet();
    if (cfsr & 0x00100000u) MMPrintString("Hardware stack limit exceeded\r\n");
    Option.Autorun = 0; // RAM only: keep a failing autorun from hiding this report.
}
