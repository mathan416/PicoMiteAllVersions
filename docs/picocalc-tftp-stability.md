# SD6: bounded A: flash access and recovery

SD6 is partially hardware-tested, not a confirmed complete repair. SD4 is retained as
recovery. No loader reinstall, partition-table change or full-flash erase is
required. Existing A: contents must be backed up before explicitly formatting
an oversized filesystem. Applications and the private HA token are on B:.

## Evidence

SD4 A: TFTP upload resets; B: transfers and tricorder scans succeed. SD5
reserves a verified 16 KiB stack instead of SD4's unintended 2 KiB. On SD5,
32-byte and empty-file uploads and downloads passed byte-for-byte. A 512-byte
upload then timed out and the board restarted. The photo reported CFSR=0,
SP=20081BB8, PC=0, with no STKOF indication. A stack overflow is therefore
not established as the cause of this failure.

The running SD5 firmware reports 16769024 for MM.INFO(OPTION FLASH SIZE):
16 MB minus the loader's 8 KB. Pico 2 W has 4 MB; the correct application
capacity is 4186112. The historical CMake 16 MB override leaked into the
loader adapter, which mistook a compile maximum for physical capacity.
LittleFS could allocate blocks beyond the mapped application partition.
The earlier mock used a 4 MB compile constant and missed this integration bug.

The user-supplied Raspberry Pi forum thread 391477 describes a related
address-translation problem with a separate data partition. Our loader maps
one application partition containing both executable and filesystem, so a
separate untranslated data-read window is not needed here. The invalid
capacity instead permitted reads/writes outside that existing mapping.

## Changes

- Query ROM partition 0 location/flags; require the loader's 8 KB application
  start, matching QMI mapping and an end within the Pico 2 W's 4 MB flash.
  Cache the validated capacity. Load the partition table if not resident;
  isolate the rare 4 KB workarea in a non-inlined helper. Invalid tables fail
  closed. SD builds no longer compile with the legacy 16 MB override.
- LittleFS callbacks reject reads, programming and erases outside real flash
  before touching XIP or invoking ROM. Legacy oversized geometry rejects all
  programming/erases, including metadata and boot-count writes.
- If mounting at the corrected size fails with a geometry mismatch, try the
  exact prior SD4/SD5 geometry for read-only recovery. In-range files remain
  available for backup; inaccessible blocks return I/O errors.
- Failed mounts no longer cause automatic formatting. Only an entirely erased
  filesystem region can initialize automatically. Nonblank incompatible or
  corrupt data is preserved. Public LittleFS calls reject unmounted state
  instead of entering partially deinitialized state. This guard is scoped to
  the SD build's single filesystem instance.
- Explicit A:/FORMAT recomputes block_count from the corrected option size,
  permitting a backed-up legacy volume to be recreated. SD6 does not perform
  that operation automatically.
- Fault decoding now handles the basic core frame before optional FP state,
  DCRS security-context words, and secure/nonsecure stack selection. If CFSR
  is zero, report HFSR instead. ROM flash failures preserve the result code,
  offset and byte count under a separate watchdog magic. Reporting is once
  only; autorun is suppressed in RAM. No settings or filesystem fault writes.

The existing 16 KiB stack, startup IRQ/keyboard fix, filesystem start, saved
options record, partition table and TFTP implementation remain in place.

## Validation

ASan/UBSan host tests exercise the actual loader adapter with a 16 MB compile
maximum and a 4 MB ROM partition; the actual LittleFS block callbacks; and the
actual LittleFS implementation/recovery helper. Cases include boundary and
misaligned requests, ROM errors, loader preservation, legacy inline-file
recovery with byte-for-byte media preservation, damaged nonblank volumes,
erased roots with remaining data, blank initialization and API calls after
failed mounts. Fault tests cover basic, FP and security-context frames,
incomplete frames, HFSR and ROM flash-error reporting.

All prior network, options, TCP status, linker-layout, keyboard and startup-IRQ
regressions pass. The built ELF retains a 16384-byte stack and the link-time
saved-options boundary assertion passes. UF2 structure/address checks are
recorded in the packaged build manifest. Hardware validation is pending.

## Hardware sequence

1. Copy the SD6_TEST UF2 into pico2-apps/ on the existing SD card and select it
   from the loader (hold Up at power-on). Keep SD4 as recovery; do not replace
   BOOT2350.UF2 or erase/repartition flash.
2. Confirm cursor/keyboard and network. If needed enable Telnet/TFTP. Check
   PRINT MM.INFO(OPTION FLASH SIZE): expected 4186112.
3. If A: reports read-only recovery, list and back up its recoverable contents
   to B:/the Mac. Do not attempt A: uploads yet. Inspect the backup before
   explicitly recreating A: with DRIVE "A:/FORMAT".
4. With a writable A:, one console idle at the BASIC prompt and status
   messages suppressed, upload/download unique files of 32, 512, 513 and
   32768 bytes and compare every byte. Stop at the first failure. No concurrent
   BASIC filesystem commands during transfers.
5. Check overwrite, missing-file, client cancellation and reconnection;
   retest B: as a control. Power-cycle, verify saved Wi-Fi/options, loader
   reentry and the B: tricorder. Record any Previous fault/flash error report.

## SD6 hardware results

The unit booted SD6, mounted the oversized A: filesystem for read-only recovery,
and reported 4186112 bytes for MM.INFO(OPTION FLASH SIZE). The B: tricorder
was confirmed working by the user. All five legacy A: files were downloaded
and verified into a persistent local backup: bootcount (4 bytes), one known
32-byte payload and three empty files. The last empty-file download needed a
retry after TFTP remained busy; BASIC stayed responsive.

With explicit user approval, DRIVE "A:/FORMAT" returned normally. Unique
32-, 512- and 513-byte test files then uploaded and downloaded with exact byte
comparisons. No CPU restart occurred during these transfers. This clears the
previous SD5 512-byte crash case for the tested runs.

The next 32768-byte upload was rejected as busy before block 0. A separate
retry was also busy. BASIC still answered PRINT 607. A controlled CPU RESTART
was then sent to clear the session and test reconnection; the board did not
answer subsequent Telnet or TFTP probes at 10.0.2.107. The 32 KiB upload has
NOT passed, and saved-Wi-Fi/reconnect behavior after that command is NOT yet
confirmed. User screen evidence is requested. Keep this PR in draft: stale
TFTP-session cleanup and post-restart connectivity remain unresolved.

## SD6 follow-up and SD7 candidate

The user reported that the controlled software restart reached BASIC but
printed a Wi-Fi connection failure. A subsequent physical restart restored
10.0.2.107 without re-entering credentials: settings were retained, not wiped.
The standalone 32768-byte upload/download then passed exact comparison.
Three overwrites (1024, 17, 0 bytes) also passed with a client-side final-ACK
retry probe, each under two seconds. A subsequent empty upload still returned
No connection, so this is not complete protocol validation.

Confirmed protocol defects in the bundled tftp.c: closing a completed upload
forgets its final DATA block, so a lost final ACK followed by a valid DATA
retry gets No connection. A repeated initial WRQ/RRQ after a lost initial
reply is rejected as busy. SD7 caches only the final completed upload's peer,
block and length for a bounded 20-second ACK dally, without holding the active
slot or reopening/writing the file. Same-peer repeated initial requests resend
their initial reply. Short packet headers are checked/copied before access;
a failed write closes only once and is never cached as completed.

The actual receive handler is tested with lost final/initial ACKs, full and
short DATA blocks, expiry including timer wrap, an unrelated active transfer,
wrong peers/blocks/lengths, write failure and truncated packets under ASan/UBSan.
SD7 also prints the SDK result/link status on Wi-Fi failure and makes WEB
CONNECT retry when the link is down (zero), as well as negative failures,
using saved credentials without resetting settings. This is diagnostics and
recovery, not a claim to have fixed the intermittent startup failure.

The Mac helper now repeats the final download ACK during its bounded dally;
a local mock explicitly drops the first ACK and verifies recovery and exact
file contents. Both client and server directions need hardware retesting on
SD7. SD7 is host-tested only until loaded on the unit. SD6 remains available.

### Disconnected poll servicing

Further review found ProcessWeb skipped cyw43_arch_poll after a failed
connection (WIFIconnected=0), and returned before polling when TCPstate was
null. This build uses PICO_CYW43_ARCH_POLL, so those paths stop servicing the
poll-driven driver/lwIP work in the normal interpreter loop. SD7 now runs
its bounded polling block before connection/server gating; server processing
remains gated, and startupcomplete still prevents uninitialized driver calls.

check_wifi_poll.py executes the actual ProcessWeb function under ASan/UBSan
with startup incomplete, failed association with no server, connected with
no server, a normal idle server and a later disconnection. All pass. This
fix prevents a software servicing gap; it does not establish why the original
30-second association attempt sometimes fails or promise automatic rejoining.
The updated SD7 candidate must still be tested on hardware.

## SD8: explicit WEB CONNECT feedback

The user installed SD7, but WEB CONNECT produced no success message, leading
them to use OPTION WIFI again. Review confirms the command silently returned
when WIFIconnected was true and the Wi-Fi association status was positive;
that status does not establish that TCP/IP has an address. Our earlier claim
that the user's retry had succeeded was premature.

SD8 explicitly reports Already connected plus the IP only when the saved
runtime flag and cyw43_tcpip_link_status==CYW43_LINK_UP agree. Otherwise it
calls WebConnect, which reports attempt/success/failure. Empty saved SSID
reports that Wi-Fi is not configured. Existing supplied-credential behavior
is retained. No credential values are printed, and the no-argument command
adds no settings writes or CPU restart.

check_web_connect.py exercises the actual command branch under ASan/UBSan:
no configuration, fully connected, associated-only, no-IP, down, failure,
stale runtime flag, and explicitly supplied credentials. All pass. SD8
retains the SD7 TFTP/polling and SD6 partition fixes. Hardware validation of
this command feedback remains pending; no assumption of reconnect success
should be made from an IP string alone.

Repeated successful WebConnect calls also reopened TCP/TFTP/UDP listeners
without tearing down their previous instances. SD8 initializes those services
once per boot and reuses them across association retries. The real WebConnect
routine passes a separate sanitizer test with failure -> success -> failure
-> success: attempts and messages occur, while each listener is opened only
once. This is tested for the normal successful initialization path; broader
listener-allocation/error-path hardening remains separate work.
