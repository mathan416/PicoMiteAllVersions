#ifndef PICOCALC_FAULT_CAPTURE_H
#define PICOCALC_FAULT_CAPTURE_H
#include <stdint.h>
void loader_flash_failure(int result, uint32_t offset, uint32_t count);
void loader_hardfault_capture(void);
void loader_report_previous_fault(void);
#endif
