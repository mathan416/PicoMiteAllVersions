# PicoCalc Pico 2 W: netfix2 SD-loader candidate

The USB-tested netfix2 binary is not compatible as-is with UF2 Loader v2.4.1.
It performs physical-address flash operations and sizes its flash filesystem
using the entire chip. UF2 Loader puts the app in a partition starting 8 KiB
into flash and translates its runtime addresses. Raw writes target the wrong
physical sectors, potentially overwriting the loader or corrupting files.

This isolated branch adds an opt-in `PICOCALC_UF2LOADER=ON` configuration for
WEBRP2350. Default builds retain netfix2 flash behavior and version.

## Implementation

- LoaderFlash.h redirects all firmware flash-range erase/program calls to a
  common implementation; the SDK sources themselves are not macro-renamed.
- ROM flash operations use the secure, runtime-address API and XIP_BASE plus
  the application's offset, allowing RP2350 address translation and partition
  permission checks to apply. Flush the ROM XIP cache after successful writes.
- Require exact page/sector alignment, validate bounds without integer overflow,
  and stop visibly with SDK panic on rejected operations/ROM errors. Never round
  an erase upward or read past a program buffer to pad it.
- Determine usable flash from the actual QMI runtime mapping, and clamp loaded
  options and new default options to that capacity. For loader v2.4.1 on the
  4 MiB Pico 2 W, it is 4 MiB minus 8 KiB. The option record stays page-padded.
- PICO_FLASH_ASSUME_CORE1_SAFE is set only for this WebMite configuration, which
  does not launch core1 (the binary has no multicore launch symbols). The SDK's
  flash-safe-execute path uses IRQ exclusion instead of waiting for a nonexistent
  core1 lockout handler. Do not reuse this assumption for a multicore build.
- Compile FileIO.c and the adapter with -Os to retain space before saved options.

## Sources and differences

Reference: https://github.com/pelrun/uf2loader/tree/v2.4.1
Maintainer's port: https://github.com/pelrun/PicoMiteAllVersions/tree/58009cf
Compatibility discussion: https://github.com/pelrun/uf2loader/issues/6

The maintainer's port establishes the runtime-ROM API, translated flash capacity
and explicit cache flush. This patch keeps netfix2, uses the SDK's unused-core
configuration instead of adding a dummy core1 thread, and does not copy the old
port's length-rounding behavior (which could erase extra sectors or overread).

## Build and validation

Use the same SDK 2.1.1, four documented SDK source replacements, and Arm GNU
14.2.rel1 as netfix2. Configure with `-DPICOCALC_UF2LOADER=ON` in addition to
the ordinary CMake arguments. Existing saved-options linker assertion applies.

Run tests/webmite_network/check_loader_flash.py (real shim, translated mock ROM,
ASan/UBSan); also run the network, options storage, TCP status and layout tests.
Check the final ELF has loader_flash_* and no flash_range_erase/program symbols.
Check UF2 ARM-S family, ordered contiguous blocks, runtime partition bounds,
saved-option boundary, and the RP2350-E10 dummy block which loader 2.4.1 skips.

This establishes source/build/format compatibility, not a successful hardware
boot. No unit was flashed during preparation. First hardware tests must check
boot, A: file write/read, BASIC RUN/save, options survival, and reentry to the
loader menu. A successful menu boot alone does not test runtime flash writes.

## Migration

Back up A: files (especially ha-token.txt) and record Wi-Fi settings before
installing the loader. Loader relocation shifts physical options/filesystem
locations by 8 KiB. This is not an in-place data migration; expect to restore
files and configure Wi-Fi once. Switching to other firmware can overwrite the
shared application partition. The SD card's ordinary files remain separate.
Keep the original USB netfix2 and a private backup for recovery.

## SD2 keyboard follow-up

User hardware test of SD1: loader menu keyboard works, BASIC keyboard does not.
PicoCalc LoadOptions does force CONFIG_I2C and physical pins 9/10 (GP6/GP7),
which select I2C1. That bus's CheckI2CKeyboard path requests register 9 then
reads immediately; loader v2.4.1 explicitly waits 16 ms between the operations.
SD2 matches that I2C1 response gap and retries the startup probe three times
with a settling delay. This is a timing hypothesis supported by source, not
yet a confirmed hardware fix. No controller reset command is sent.

Also fix a separate definite bug on the I2C0 split path: initialize buff and
return from the request-only phase before decoding uninitialized data.

check_keyboard_init.py tests real startup code with a timing-sensitive mock,
transient write/read failures and bounded permanent failure, and compiles the
actual I2C1 transaction statements to enforce the 16 ms response gap. Tests
pass with ASan/UBSan. The saved-option layout and loader reservation are unchanged.

## SD3 diagnostic follow-up

User reports SD2 still has no keyboard input and no blinking cursor. The
keyboard-only timing hypothesis is not established as the cause. SD3-diag
adds direct-to-display startup checkpoints, bypassing network console output:
filesystem boot update, Wi-Fi architecture initialization, WebConnect, IRQ
mask and 30 ms timer progress at prompt entry, then the first web/routine/input
poll. ROM flash failures print their return code, offset and length before
the existing fail-stop panic. It makes no further speculative runtime repair.
Read the last [SD3] line on screen to locate the stall. SD3 is diagnostic,
not a validated keyboard or startup fix. It uses the same saved-data layout.
