/* RP2350 runtime-address flash access for UF2 Loader v2.4.1.
 * Based on the loader maintainer's documented ROM API and XIP-cache fix.
 * Unlike the historical shim, never round erase lengths or overread buffers.
 */
#define LOADER_FLASH_IMPLEMENTATION
#include "LoaderFlash.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/structs/qmi.h"
#if !PICO_RP2350
#error This compatibility build is only for RP2350
#endif
uint32_t loader_flash_size(void) {
    uint32_t base = ((qmi_hw->atrans[0] & QMI_ATRANS0_BASE_BITS)
                     >> QMI_ATRANS0_BASE_LSB) * FLASH_SECTOR_SIZE;
    if (base >= PICO_FLASH_SIZE_BYTES) panic("Invalid application flash mapping");
    return PICO_FLASH_SIZE_BYTES - base;
}
static void loader_flash_op(uint32_t offset, size_t count, uint8_t *data,
                            uint32_t operation, uint32_t alignment) {
    uint32_t capacity = loader_flash_size();
    if (offset % alignment || !count || count % alignment ||
        offset > capacity || count > capacity - offset)
        panic("Invalid application flash operation");
    cflash_flags_t flags = {
        (operation << CFLASH_OP_LSB) |
        (CFLASH_SECLEVEL_VALUE_SECURE << CFLASH_SECLEVEL_LSB) |
        (CFLASH_ASPACE_VALUE_RUNTIME << CFLASH_ASPACE_LSB)
    };
    int result = rom_flash_op(flags, XIP_BASE + offset, count, data);
    if (result != PICO_OK) panic("Application flash failed: %d", result);
    rom_flash_flush_cache();
}
void loader_flash_erase(uint32_t offset, size_t count) {
    loader_flash_op(offset, count, NULL, CFLASH_OP_VALUE_ERASE, FLASH_SECTOR_SIZE);
}
void loader_flash_program(uint32_t offset, const uint8_t *data, size_t count) {
    loader_flash_op(offset, count, (uint8_t *)data, CFLASH_OP_VALUE_PROGRAM, FLASH_PAGE_SIZE);
}
