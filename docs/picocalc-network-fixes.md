# PicoCalc WebMite network fixes

This branch starts at `V6.00.03_PicoCalc` (`200d7d3`) and identifies candidate
builds as `6.00.03-netfix1`. It targets the Pico 2 W inside a PicoCalc.

## Confirmed defects addressed

- `TelnetPutC()` used `sizeof(Telnetbuff-4)`, the size of a pointer (four bytes
  on ARM32), instead of the buffer size minus four. This created excessive
  small writes. The reusable transmit buffer was passed to `tcp_write()`
  without `TCP_WRITE_FLAG_COPY`, allowing queued data to change underneath
  lwIP. Server and client writes now transfer ownership by copying.
- A normal FIN (`p == NULL` in the receive callback) did not release the
  console slot. Error callbacks attempted to close a PCB lwIP had already
  freed. Bookkeeping cleanup is now separate from closing a live PCB.
- Accepting connections indexed `client_pcb[MaxPcb]` when the table was full.
  Every slot array has exactly `MaxPcb` elements. Full tables are now rejected.
- A second Telnet connection replaced the shared current-console index. An
  older connection's error could then close the newer console. Only one
  Telnet client is now admitted, matching the single BASIC console.
- Negotiation at the start of a TCP packet caused the entire packet to be
  discarded, including any typed command following it. A small incremental
  parser now retains state across packets, handles chained pbufs, escaped
  IAC, subnegotiation, and interrupt-process/break commands.
- Output polling can run a disconnect callback. The send path now checks that
  it still owns the connection before accessing the PCB again.
- Telnet option output encoded the option byte as 255 instead of its value;
  option output now checks buffer boundaries and emits the requested value.

The release defects were reproduced offline with extracted source functions.
Regression tests compile the actual changed functions with lwIP API stubs and
AddressSanitizer/UndefinedBehaviorSanitizer. They cover buffer ownership, 40
reconnections, table capacity, second-console rejection, dead-PCB cleanup,
close failure/abort propagation, every packet split of negotiation plus input, chained pbufs,
control commands, and disconnect during transmit polling.

## Saved Wi-Fi settings: still under investigation

These fixes address confirmed memory-safety and connection defects. They do
**not** establish the cause of reported Wi-Fi settings loss after reboot.
Neither the BASIC applications nor these changes explicitly reset Wi-Fi.

The firmware has two automatic `ResetAllFlash()` startup paths in `PicoMite.c`:
invalid saved options and an unexpected USB clock. `ResetAllFlash()` calls
`ResetOptions(true)`, which zeroes the options structure and saves defaults.
That clears Wi-Fi and Telnet settings. `SaveOptions()` is a single-sector
flash erase followed by programming, with no redundant committed record.
Interruption of that operation can therefore make a later boot reset options.
We have not captured a failing device's option sector or reset reason, so the
actual trigger remains unproven. A network timeout alone is not evidence of
flash erasure.

`RUN "file.bas"` is also not flash-free: this interpreter clears the saved
BASIC-variable region and writes tokenised program flash before executing the
first BASIC statement. This distinction matters when isolating startup faults.

The supplied release UF2 matched GitHub's asset SHA-256 exactly:
`2e3e62c1bdb6f0904c08632a6defda7c34055b429df9dbc69d9c271319624a79`.
Its application ends at exclusive address `0x1010cf00`, below the saved-options
sector at `0x1010e000`. Thus there is no image/options overlap in that original
asset. The new linker assertion prevents a future WebMite build from growing
into the saved-options sector. It does not change the flash layout or provide
atomic option storage. Size optimisation is applied to the WebMite networking
translation units to retain the original flash layout with the modern compiler.
Keep the assertion in sync with `Configuration.h` if
that layout is intentionally changed.

## Build and checks

Follow the repository README: SDK **2.1.1** requires its `gpio.c`, `gpio.h`,
`float_sci_m33_vfp.S`, and `flash.c` replaced with this repository's versions.
Use a modern Arm GNU toolchain. Local validation uses Arm GNU 14.2.rel1;
the older GCC 9 rejects declarations following labels in existing source.

```sh
python3 tests/webmite_network/run_tests.py
python3 tests/webmite_network/check_layout_guard.py
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

The layout test requires `arm-none-eabi-gcc` and `arm-none-eabi-ld` in PATH, or
`ARM_CC` and `ARM_LD` set to their paths. Host tests require Python 3 and Clang
with ASan/UBSan. CI runs both test suites and the PicoCalc WebMite build.

## Local build result

The size-optimised candidate builds with SDK 2.1.1 (the four documented SDK
replacements) and Arm GNU 14.2.rel1. In this local build its ELF flash end is
`0x1010de68`, leaving 408 bytes before the options sector; the padded UF2
application ends at `0x1010df00`. The linker guard is mandatory because that
margin is small and compiler/build differences change image size. An earlier
unoptimised attempt was rejected by the guard rather than packaged as safe.
The unmodified release tag also exceeded the boundary in an independent Arm
14.2 rebuild (`0x1010e3d8`), and the guard rejected it. This demonstrates a
compiler/build-dependent layout risk, not overlap in the original published
UF2, and not proof of the reported device failure's cause.
No binary is published as a tested firmware release by this branch.

## Hardware validation required

A successful host test or compile is not an on-device validation. No candidate
has been flashed as part of this investigation, and no fix for the Wi-Fi wipe
is claimed. Before any flash, back up programs and options through a local
console; option backups can contain credentials and must stay private.

The next controlled hardware check should record firmware version, reset cause,
and saved options before loading any BASIC program, then compare them after a
minimal load/run and cold reboot. Use USB serial first so networking can be
isolated. If needed, inspect a private flash dump of the options sector; never
attach that dump, a token file, or unredacted options backup to a public issue.
Retest Telnet reconnects, a refused second console, larger TFTP transfers, and
Home Assistant reads separately. The PicoCalc STM32 keyboard-controller
firmware is outside this patch's scope; there is no evidence yet that it needs
updating.
