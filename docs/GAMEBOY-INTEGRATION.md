# GB, GBC and GBA integration (AI-assisted)

This contribution extends the bundled-runtime framework in PR #87. It adds Game Boy, Game Boy
Color and Game Boy Advance as three separate ECAM libraries backed by two source-pinned native
applications. It is a separate contribution; PR #87 is not changed by this branch.

Status: source integration and host checks. Native PS5 builds and firmware 4.50 qualification are
outstanding. No emulator compatibility, frame rate, installable binary or console test is claimed.

## Systems and persistent identity

| ECAM ID | Library key | Native runtime | Bundled program | Default game folder | Native data |
|---|---|---|---|---|---|
| 8 | gb | GBCXY / mGBA GB core | retro/gbc/eboot.bin | /data/PS5GBC/games | /data/PS5GBC |
| 9 | gbc | GBCXY / mGBA GBC core | retro/gbc/eboot.bin | /data/PS5GBC/games | /data/PS5GBC |
| 10 | gba | GBAXY / mGBA GBA core | retro/gba/eboot.bin | /data/PS5GBA/games | /data/PS5GBA |

IDs 0–7 retain their previous meaning. The catalogue, scanner slots, side bar, visibility mask,
last-look state and library sort/filter arrays now have eleven entries. Old four- and eight-side
settings keep their previous values; the new sides stay hidden until enabled under Launcher >
Sides. Each handheld has an independent ECAM game folder, favourites, catalogue and recent list.
The same GB/GBC folder can contain both systems: loose `.gb` and `.gbc` files go to separate lists.

If an existing installation uses `/data/PS5GBC/roms` or `/data/PS5GBA/roms`, select that folder in
Game files. ECAM does not move ROMs or change the native apps' scan configuration. GB/GBC mode
selection is left to mGBA; this integration does not force a hardware model from an extension.

## Source pins

- Yahballon/PS5GBCX at `66188f43df8c5f59f023557011e6fac5028b9b90`:
  https://github.com/Yahballon/PS5GBCX
- Yahballon/PS5GBAX at `197c62e08cd709299785fa07c8c0addbb844c51d`:
  https://github.com/Yahballon/PS5GBAX

Both donors use mGBA 0.10.5 in their respective GB/GBC-only and GBA-only configurations. Their
cores, cartridge identity, serialization, SRAM/RTC behavior and emulator options are unchanged.
These are source pins, not independently tested claims about the donor release binaries.

## Discovery, artwork and validation

- Local/USB discovery retains the bounded, non-symlink-recursive scanner from PR #87.
- GB: `.gb`; GBC: `.gbc`; GBA: `.gba`, `.zip`, `.7z`, case-insensitive.
- GB/GBC archives remain accessible through the native frontend. ECAM does not put ambiguous ZIP
  containers into both handheld libraries or guess their system from an archive filename.
- Loose handheld files receive empty/readability, minimum-header-size and 32 MiB ceiling checks.
  GBA archive files have a 64 MiB ceiling and basic ZIP/7z signature checks. The donor validates the
  complete cartridge and decompressed archive, including its own single-ROM/size restrictions.
- No proprietary BIOS, copyrighted cartridge logo, commercial test ROM or user save is embedded
  in ECAM's tests. Test files exercise boundaries and do not represent playable cartridges.
- Existing local/donor artwork lookup supports GBCXY's `GBC-<path hash>` and GBAXY's
  `GBA-<path hash>` cache names, adjacent artwork and offline metadata indexes.
- `tools/retro-library.py` accepts `.gb`, `.gbc` and `.gba` for offline names/art and optional CRCs.

## Game, settings and return handoff

ECAM forwards the exact selected file and parent eboot through the shared request/argv bridge.
GB/GBC select the shared `retro/gbc` runtime; GBA selects `retro/gba`. The generated startup overlay
reads the one-use request **after** the donor's filesystem-access attempt, so a persistent request
outside the sandbox can be consumed before library initialization.

A direct request starts `ShelfRuntime` with that ROM. Normal game exit follows the donor's
teardown and returns to ECAM. A failed direct start also exits the frontend rather than dropping
into another game list; its reason is written to the native log. Library-only entry opens the
native settings/library frontend; closing it returns to ECAM. The shared bridge clears stale
forwarded-ROM environment state for settings-only launches.

The donor's UI assets remain embedded and are prepared in its writable native data folder; no
separately installed GBCXY/GBAXY is needed. Native runtime imports, nested libc and display teardown
still require a complete PS5 build and hardware testing.

Native controls, scaling, palettes/model choices where available, shortcuts, fast-forward,
settings, saves and save-state behavior remain in the selected runtime's frontend. This change
provides access to those menus; it does not claim every desktop mGBA feature is exposed by them.

## Filesystem access and saves

The standalone-title binary Lapy helpers and their baseline-package requirement are removed from
the generated runtime builds. They target PPSA99210/PPSA99209, whereas ECAM runs as PPSA99360.
An active HEN must grant the ECAM title/process the required filesystem access. The retained
cooperative elevation client may recognize existing access or a compatible resident service.
There is no new ECAM helper implementation or promise that an original-title helper will work.
Validate access after LoadExec on firmware 4.50 before testing against existing save data.

Native paths remain `/data/PS5GBC/saves`, `/data/PS5GBC/states`, `/data/PS5GBA/saves` and
`/data/PS5GBA/states`. No migration, renaming or conversion occurs. GB/GBC share the native backend
and storage intentionally, while ECAM catalogue IDs remain separated by system. Matching folder
names alone do not prove save/state compatibility with an unknown installed standalone version.

USB whole-backup/restore includes each native save/state directory once, excludes ROMs, and keeps
replaced saves in that backend's `saves-before` folder. The existing global Restore from USB flow
handles these sets. Per-game ECAM save editing, cross-core conversion and unified state-slot UI
are not part of this change. Donor fallback data folders, custom native root overrides and files
outside the documented default roots are not automatically backed up.

## Build and validation

```sh
# Source-only preparation; leaves the original donor clones unchanged.
ECAM_GBC_SOURCE=/path/to/PS5GBCX ECAM_GBA_SOURCE=/path/to/PS5GBAX \
  python3 tools/build-retro.py --prepare-only gbc gba
# With reviewed sources, Clang/LLVM 18 and prepared PS5 SDK/Vulkan/network dependencies:
ECAM_GBC_SOURCE=/path/to/PS5GBCX ECAM_GBA_SOURCE=/path/to/PS5GBAX \
  python3 tools/build-retro.py --reuse-prepared gbc gba
# Or, into fresh generated worktrees:
make gameboy
make package
```

Set the donors' `PS5GBC_DEPS` / `PS5GBA_DEPS` and SDK/libnfs/libz locations as their BUILDING.md
requires. Stage outputs are `build/retro/gbc` and `build/retro/gba`; the normal package script includes
them only when source provenance is present. It preserves component notices, the MIT bridge notice
and source hashes, and omits standalone metadata/helpers. No downloaded release eboot is substituted.
Preparation, building and packaging do not install, merge or publish a release.

```sh
make test-retro
ASAN_OPTIONS=detect_leaks=0 ECAM_TEST_SANITIZE=1 make test-retro
CPLUS_INCLUDE_PATH=/path/to/fmt/include bash tools/tests/test-saves.sh
python3 tools/tests/test-retro-overlays.py /path/to/donor-parent --systems gbc gba
# With no local path, the overlay checker retrieves immutable source files for all seven donors.
python3 tools/tests/test-retro-overlays.py
```

The host integration suite covers stable IDs, format separation, scanner shutdown, boundary
validation, exact executable/ROM routing, settings-only requests, four/eight/eleven-side settings
and separate GB/GBC catalogues. Backup tests cover native save/state separation, ROM exclusion,
restore destination and retention of replaced files. Source-overlay checks cover idempotence,
launch/return anchors, elevation/request ordering, packaging license inputs and untouched originals.

In the cloud test environment, LeakSanitizer cannot inspect process tasks; leak detection is
therefore disabled explicitly for that run. AddressSanitizer and UndefinedBehaviorSanitizer remain
enabled. Syntax checks and host tests do not cross-link or execute PS5 code.

## Owner qualification before merge

1. Finish both native builds and ECAM linking; inspect package imports, nested libraries, notices,
   assets and ECAM-title filesystem access. Verify there is no original-title binary helper.
2. Load old four/eight-side settings; enable the three new sides and check eleven-side bar/settings,
   folder selection, sorting, catalogue persistence, favourite/recent identity and artwork.
3. Test real GB, GBC and GBA games on firmware 4.50 from internal and USB storage, including spaces,
   non-ASCII names, malformed/truncated files and supported GBA archives.
4. On disposable copies, test battery saves, RTC cartridges, save states, save/load shortcuts and
   USB backup/restore. Verify GB/GBC shared native storage causes no cross-game collisions.
5. Test settings-only entry/exit, direct launch, failed starts, argv loss, repeated transitions,
   audio/controller/display behavior and return to the correct ECAM section.
6. Regress the original eight systems. Record actual console results separately from host checks.

Link trading/multiplayer, sensor mapping, camera/printer, native ECAM option/state editors,
automatic archive classification for GB/GBC, and actual firmware compatibility are not asserted.
