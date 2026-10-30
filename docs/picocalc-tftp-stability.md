# SD5: A: TFTP fault investigation

Observed on clean SD4: A: TFTP upload triggers Invalid address - resetting;
B: transfers and checksum verification succeed. Reproduced with status output
suppressed and no concurrent BASIC filesystem commands. Partition-aware
filesystem boot-count writes and a live SD-based tricorder scan succeeded.

## Confirmed build defect

CMake requests PICO_CORE0_STACK_SIZE=0x4000, but SDK 2.1.1 crt0.S uses
PICO_STACK_SIZE. SD4 ELF __StackBottom=0x20081800 and __StackTop=0x20082000:
only 2048 bytes. SDK newlib installs the M33 MSPLIM guard at __StackBottom.
A TFTP callback adds lwIP/CYW43 stack depth to littlefs and ROM-flash operations,
which may explain why this path faults while B: and ordinary A: writes do not.
That causal attribution is a hypothesis until SD5 hardware tests/fault capture.

SD5 reserves 16 KiB using PICO_STACK_SIZE and a pinned SDK 2.1.1 linker script.
The stack is 0x2007e000..0x20082000. The C heap ends at 0x2007e000; assertions
exclude overlap with heap, static data, scratch sections or a core1 stack.
WebMite remains single-core. The flash partition map, filesystem offsets,
TFTP callbacks and saved-option record layout are unchanged for this test.
The previous working SD4 firmware is retained for recovery.

## Fault capture

The HardFault entry stub first selects the interrupted MSP/PSP, then switches
to a separate 1 KiB emergency stack with the stack limit cleared. CFSR, original
SP and (when the exception frame is valid) fault PC are written to watchdog
scratch 0..3 under a magic distinct from UF2 Loader's command magic. SDK reboot
uses scratch4..7. It triggers a watchdog restart without filesystem access,
network callbacks or saved-option writes. On the next boot the registers are
printed once, and autorun is disabled in RAM so the report is readable.
Incomplete/overflowed exception frames are not dereferenced. STKOF is reported
as Hardware stack limit exceeded. No normal-startup diagnostic chatter is added.

## Validation

- check_stack_layout.py rejects the SD4 2 KiB ELF and accepts the linked SD5
  16 KiB reservation, heap/BSS separation and SRAM top.
- check_fault_capture.py exercises actual record/report code under ASan/UBSan,
  including ordinary/extended frames, invalid/overflowed frame and once-only
  reporting. ARM disassembly confirms no stack push before emergency switch.
- Startup-IRQ, saved-options and flash-adapter regression checks pass.
- Full SD5 build and saved-options flash layout assertion pass.

## Hardware test sequence

1. Select SD5 from the existing loader; confirm cursor, keyboard and Wi-Fi.
2. Keep one console session open and use OPTION WEB MESSAGES OFF (RAM only).
3. Leave BASIC at its prompt; do not run simultaneous filesystem commands.
4. Upload a new 32-byte test file to A:, then read back and compare bytes.
5. Repeat with 512 bytes, 513 bytes and a multi-block file, using unique test
   filenames. Verify data before deleting only these test files.
6. Test missing-file, repeated overwrite, client cancellation and reconnect;
   confirm normal protocol errors, not crashes. Retest B: as a control.
7. Power cycle; verify Wi-Fi/options and loader menu reentry, then tricorder.
8. If it faults, capture the Previous fault line before another restart.

This is a test candidate, not a hardware-validated fix. No repartitioning or
formatting is part of SD5. Do not change partitioning without contrary evidence.
