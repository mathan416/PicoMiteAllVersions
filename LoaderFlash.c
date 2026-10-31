/* RP2350 runtime-address flash access for UF2 Loader v2.4.1.
 * Based on the loader maintainer's documented ROM API and XIP-cache fix.
 * Unlike the historical shim, never round erase lengths or overread buffers.
 */
#define LOADER_FLASH_IMPLEMENTATION
#include "LoaderFlash.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include "FaultCapture.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/structs/qmi.h"
#if !PICO_RP2350
#error This compatibility build is only for RP2350
#endif
/* The legacy interpreter build defines a 16 MB maximum. It is not the
 * physical device size or the loader's application partition capacity. */
#include "boot/picobin.h"
static uint32_t loader_partition_capacity;
static uint32_t loader_partition_base;
static int loader_partition_info(uint32_t info[3]) {
    return rom_get_partition_table_info(info, 3,
        PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION);
}
static int __attribute__((noinline)) loader_partition_load(uint32_t info[3]) {
    uint8_t workarea[4096] __attribute__((aligned(4)));
    int result = rom_load_partition_table(workarea, sizeof workarea, false);
    return result < 0 ? result : loader_partition_info(info);
}
uint32_t loader_flash_size(void) {
    uint32_t base = ((qmi_hw->atrans[0] & QMI_ATRANS0_BASE_BITS)
                     >> QMI_ATRANS0_BASE_LSB) * FLASH_SECTOR_SIZE;
    if (loader_partition_capacity && base == loader_partition_base)
        return loader_partition_capacity;
    uint32_t info[3];
    int result = loader_partition_info(info);
    if (result < 0) result = loader_partition_load(info);
    if (result != 3 || !(info[0] & PT_INFO_PARTITION_LOCATION_AND_FLAGS))
        panic("Application partition table unavailable");
    uint32_t first = (info[1] & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                     >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last = (info[1] & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)
                    >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    /* This SD image is specifically for the 4 MB Pico 2 W. Fail closed if
     * a table or mapping would expose the loader or nonexistent flash. */
    uint32_t end = (last + 1u) * FLASH_SECTOR_SIZE;
    if (first != 2u || base != first * FLASH_SECTOR_SIZE || last < first ||
        end > 4u * 1024u * 1024u)
        panic("Invalid application flash partition");
    loader_partition_base = base;
    loader_partition_capacity = end - base;
    return loader_partition_capacity;
}
int loader_flash_contains(uint32_t offset, size_t count) {
    uint32_t capacity = loader_flash_size();
    return offset <= capacity && count <= capacity - offset;
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
    if (result != PICO_OK) loader_flash_failure(result, offset, (uint32_t)count);
    rom_flash_flush_cache();
}
void loader_flash_erase(uint32_t offset, size_t count) {
    loader_flash_op(offset, count, NULL, CFLASH_OP_VALUE_ERASE, FLASH_SECTOR_SIZE);
}
void loader_flash_program(uint32_t offset, const uint8_t *data, size_t count) {
    loader_flash_op(offset, count, (uint8_t *)data, CFLASH_OP_VALUE_PROGRAM, FLASH_PAGE_SIZE);
}
