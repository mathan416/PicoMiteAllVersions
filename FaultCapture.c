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
#define FLASH_FAULT_MAGIC 0x5043464cu
#define HFSR_MARKER 0x20000000u
uint32_t loader_fault_stack[256] __attribute__((aligned(8)));
void __not_in_flash_func(loader_fault_record)(uint32_t *frame, uint32_t exc_return) {
    uint32_t cfsr = m33_hw->cfsr;
    uint32_t pc = 0;
    /* Core registers precede the optional FP frame. DCRS=0 adds ten
     * security context words before the basic frame (ARMv8-M). */
    unsigned index = (exc_return & 32u) ? 6u : 16u;
    uintptr_t sp = (uintptr_t)frame;
    // STKOF or an incomplete exception frame must not be dereferenced.
    if (!(cfsr & 0x00101010u) && !(sp & 3u) && sp >= SRAM_BASE &&
        sp <= SRAM_END - (index + 1u) * sizeof(uint32_t)) pc = frame[index];
    watchdog_hw->scratch[1] = cfsr ? cfsr : HFSR_MARKER | m33_hw->hfsr;
    watchdog_hw->scratch[2] = (uint32_t)sp;
    watchdog_hw->scratch[3] = pc;
    watchdog_hw->scratch[0] = FAULT_MAGIC;
    watchdog_enable(1, false);
    while (1) tight_loop_contents();
}
void __attribute__((naked)) __not_in_flash_func(loader_hardfault_capture)(void) {
    __asm volatile (
        "tst lr, #64\n"
        "beq 1f\n"
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b 2f\n"
        "1: tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp_ns\n"
        "mrsne r0, psp_ns\n"
        "2: mov r1, lr\n"
        "movs r2, #0\n"
        "msr msplim, r2\n"
        "ldr r2, =loader_fault_stack\n"
        "add.w r2, r2, #1024\n"
        "msr msp, r2\n"
        "b loader_fault_record\n"
    );
}
void __not_in_flash_func(loader_flash_failure)(int result, uint32_t offset, uint32_t count) {
    watchdog_hw->scratch[1] = (uint32_t)result;
    watchdog_hw->scratch[2] = offset;
    watchdog_hw->scratch[3] = count;
    watchdog_hw->scratch[0] = FLASH_FAULT_MAGIC;
    watchdog_enable(1, false);
    while (1) tight_loop_contents();
}
void loader_report_previous_fault(void) {
    uint32_t magic = watchdog_hw->scratch[0];
    if (magic != FAULT_MAGIC && magic != FLASH_FAULT_MAGIC) return;
    uint32_t cfsr=watchdog_hw->scratch[1], sp=watchdog_hw->scratch[2], pc=watchdog_hw->scratch[3];
    watchdog_hw->scratch[0] = 0;
    if (magic == FLASH_FAULT_MAGIC) {
        MMPrintString("Previous flash error="); PIntH(cfsr);
        MMPrintString(" offset="); PIntH(sp); MMPrintString(" bytes="); PIntH(pc); PRet();
    } else {
        MMPrintString(cfsr & HFSR_MARKER ? "Previous fault HFSR=" : "Previous fault CFSR=");
        PIntH(cfsr & ~HFSR_MARKER);
        MMPrintString(" SP="); PIntH(sp); MMPrintString(" PC="); PIntH(pc); PRet();
        if (cfsr & 0x00100000u) MMPrintString("Hardware stack limit exceeded\r\n");
    }
    Option.Autorun = 0; // RAM only: keep a failing autorun from hiding this report.
}
