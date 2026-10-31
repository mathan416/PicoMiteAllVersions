# SD9: TFTP error-path hardening

SD8's normal transfers, live tricorder and saved-Wi-Fi reflashes were verified
on hardware. This validated build closes the remaining TFTP exception/cleanup paths.
It retains the same partition, saved-options sector and 16 KiB stack. Use the
already patched 2.4.1-erasefix1 menu; no format or bootloader reinstall is needed.

## File callbacks

The old callbacks called BasicFileOpen, ErrorCheck, FileClose and BASIC heap
allocation inside lwIP. Ordinary missing files or storage errors could longjmp
out of the polling stack, bypassing packet/file/timer cleanup. FAT read/write
return codes and short writes were not reliably checked, and pbuf chains were
handled as contiguous memory. Client error messages were printed as unbounded
C strings and their files could be closed twice.

TFTP now owns one fixed file object, one 256-byte LittleFS cache and a fixed
path buffer. It uses non-throwing filesystem APIs, leaves BASIC's drive/file
numbers/error policy untouched, and does not allocate from BASIC's heap or
print from callbacks. File paths are normalized without escaping the selected
filesystem root. A: does not require an SD card; B: config/mount failures return
an ordinary protocol error. A compile guard rejects allocating FatFS LFN mode.

All FAT return codes and short writes are checked. Final DATA, including an
empty exact-block terminator, is acknowledged only after successful filesystem
sync. LittleFS timestamps use atomic file attributes in the supplied file
configuration. Close is idempotent; the protocol alone owns closing. Interrupted
uploads can leave a partial target file, as with ordinary TFTP; this change does
not promise atomic replacement. Firmware images should still be staged under
.part and verified before being renamed/selectable, or copied directly to SD.

## Protocol

Requests validate filename/mode lengths and terminators before opening files
or scheduling timers. Only octet (binary) mode is supported; unsupported modes
and option extensions get an explicit error rather than silently wrong text
conversion. Failed/malformed opens leave no timer. Reinitializing a bound
server does not leak a UDP PCB. Allocation failures and invalid read counts
release retained state, and write failure or short results get no success ACK.

A stack-only payload view handles split headers and chained data while keeping
the receive packet intact for one release. Client errors are consumed with a
bounded view. Duplicate old ACKs are ignored rather than causing extra DATA
or a protocol error; active requests are repeated only for the same filename
and direction. Existing final-ACK dally remains independent of the active slot.
Timeout counters use unsigned wrap-safe arithmetic. Invalid traffic does not
extend an active transfer's last-valid-packet time.

## Validation and release gate

Run `python3 tests/webmite_network/check_tftp_errors.py` (the previous
check_tftp_retries.py entry point delegates to this broader suite). It compiles
the COMPLETE actual MMtftp.c and tftp.c with actual LittleFS, mocked FAT/UDP and
fault injection under ASan/UBSan. BASIC heap calls abort the test. Coverage:

- Repeated missing-file and invalid-path requests, followed by valid transfers.
- Filename/mode boundary lengths, absent NULs, unsupported mode/options,
  truncated/oversized datagrams, bad chains and 5000 deterministic malformed packets.
- Split headers/data, duplicates, competing peers, cancellation, timeout,
  dally expiry and timer wrap; no buffer/file/timer/UDP leaks.
- Real LittleFS full-volume, program/read/sync errors and successful reuse.
- FAT read/write/sync errors, short writes, missing/unconfigured card, and
  exact drive/path routing with unchanged interpreter filesystem state.
- Network allocation/bind failures, final-ACK loss and immediate subsequent use.

All earlier networking, flash/LittleFS, options, keyboard/IRQ, Wi-Fi service and
linker/stack regression checks pass. The complete SD9 image fits below saved
options. The hardware gate below has now passed: missing files, malformed requests,
cancellation, timeout, ordinary transfers, console responsiveness and the
tricorder. Saved settings previously survived the patched-loader reflash, and
SD9 reconnected using that configuration.
Full-storage and injected physical I/O failures are tested on mocks, not by
filling or removing the user's live SD card. The working SD8 remains recovery.

## Hardware validation: passed

The running version string was independently read from flash as
6.00.03-netfix2-sd9 before any error probes. The bounded live test script is
`tests/hardware/verify_sd9_errors.py`; it requires a confirmed SD9 installation,
uses unique files only, stops at the first unexpected response and records its
created files for explicit cleanup.

The unit passed 20 alternating A:/B: missing-file requests, each followed by a
healthy read. Malformed names/modes/terminators, oversized DATA, invalid ACK
lengths, unknown operations and truncated packets recovered cleanly. A repeated
initial WRQ, competing peer and cancellation with a non-NUL-terminated client
error all worked. An abandoned upload expired and the slot was usable again.
A: overwrite/readback sizes 0, 32, 512, 513 and 32768 matched exactly; 32 KiB
took 7.41 seconds. A B: 513-byte transfer passed afterward. No BASIC error or
CPU restart occurred; WEB CONNECT explicitly reported the current IP.

An additional B: transfer succeeded with all ten BASIC file handles occupied.
Those handles remained writable and closed normally; readback retained both
before/after records. All 16 new test files were removed. Installed tricorder
source matched the Mac copy (14964 bytes). Live basement and atmosphere/weather
scans returned valid values with sound enabled and MM.ERRNO=0. The tricorder
was returned to its weather page; the console is closed after final checks.

SD9 is the validated stable build for this tested Pico 2 W/PicoCalc workflow.
The stable-named UF2 is byte-identical to SD9_TEST already on the unit; no extra
reflash is needed. SD8 is retained for rollback. Physical storage I/O/full/OOM
faults were injected on the host rather than induced on the user's live media.
Interrupted uploads can still leave partial target files, as documented above.
