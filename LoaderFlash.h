#ifndef PICOCALC_LOADER_FLASH_H
#define PICOCALC_LOADER_FLASH_H
#include <stdint.h>
#include <stddef.h>
uint32_t loader_flash_size(void);
void loader_flash_erase(uint32_t offset, size_t count);
void loader_flash_program(uint32_t offset, const uint8_t *data, size_t count);
#ifndef LOADER_FLASH_IMPLEMENTATION
#define flash_range_erase loader_flash_erase
#define flash_range_program loader_flash_program
#endif
#endif
