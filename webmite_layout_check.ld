/* Configuration.h: FLASH_TARGET_OFFSET = 1080 * 1024.
 * __flash_binary_end is an exclusive XIP address supplied by the Pico SDK.
 * Keep this assertion in sync if the reserved flash layout changes.
 */
ASSERT(__flash_binary_end <= 0x1010e000,
       "WebMite firmware overlaps saved options sector; do not flash")
