# SD-loader erase fix for saved options

UF2 Loader v2.4.1 rounds RP2350 erase lengths with `(size + 0x1fff) & -4096`,
erasing an extra sector. The SD8 UF2 has 0x10da00 bytes of application payload;
the old loader erases through offset 0x10f000, including WebMite saved options
at 0x10e000. Correct rounding erases through 0x10e000 exclusively.

Upstream fixed the same calculation in commit:
https://github.com/pelrun/uf2loader/commit/543fba49bedfce861ecb8a37683b5e76fe866848

The adjacent uf2loader-erasefix1.patch backports the formula to v2.4.1, adds
zero/overflow rejection, marks the menu 2.4.1-erasefix1, and includes an actual
function ASan/UBSan regression test. The original source fails that test at
the options-boundary assertion; the patched source passes.

Apply the patch in a pelrun/uf2loader checkout based on commit
1a0b1c4b144a72445e44e4c545ec4163bc873685, then run
`python3 tests/check_erase_rounding.py`. Build only the `ui` target for pico2.
The tested local source commit is 5b884d3 (full source is in the user's
UF2Loader-EraseFix project directory). Existing PicoCalc SDK 2.1.1 and
Arm GNU 14.2.Rel1 were used; no stage3 or partition-table update is required.

Only replace the SD root's BOOT2350.UF2. It is a RAM-loaded menu, not the
flash-resident bootloader_pico2.uf2. The installed artifact is 89600 bytes,
SHA256 2ffe37ce181812ebb35570f7f7b15a16ffa0b46ec8fadce58f11df3815c4c38b.
All UF2 blocks were verified as RP2350 ARM secure-family RAM targets from
0x20000000 to 0x2000af00, with valid numbering, headers and payload matching
the linked binary. Old card/package menus were backed up before replacement.

This fix preserves the sector after a compatible application image. It
cannot restore already-erased credentials or preserve data inside a different
image's occupied sectors. Hardware verification remains pending: boot the new
menu, configure Wi-Fi once if needed, use that menu to flash the same SD8
image again, and confirm WEB CONNECT still sees saved configuration. No A:
formatting or BOOTSEL bootloader reinstall is needed for this fix.

Hardware follow-up: the user confirmed the configured Wi-Fi details survived
both an ordinary power cycle and an SD8 reflash through the patched menu.
The loader settings-preservation test has therefore passed on their unit.
