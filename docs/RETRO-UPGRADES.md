# N64/NES standalone upgrades in PR #87 (AI-assisted)

This is source integration, not a PS5 compatibility or speed certification. The original N64XY and
NESYX backends remain the defaults. Two additional **optional** source-built runtimes are selectable
in the side's Settings > Emulator settings > Emulator (`ps5retro::kAlternatives`). A missing selected runtime produces an error; ECAM does
not silently select another emulator or convert saves. All five runtime overlays need PS5 build
and hardware qualification before this draft is merge-ready.

## What is implemented in ECAM

- Persistent, independent N64/NES backend selection, without renumbering the existing eight sides.
- Direct ROM and native-library/settings entry for both alternatives; normal quit returns to ECAM.
- The same bounded argv-loss bridge, with C++17 compatibility for the new donor build systems.
  Library-only requests clear stale forwarded-ROM environment variables. A request that cannot
  be consumed is refused rather than replayed.
- Alternative build/staging targets, immutable source pins, copied source/component notices,
  a separately MIT-licensed shared bridge, and overlay digest checks for prepared-source reuse.
- Alternatives built for ECAM title PPSA99360. Their source-built helper-only payloads use ports
  9136 (Mupen64Plus) and 9137 (FCEUmm), avoiding the donors' original helper ports. Actual HEN/
  title behavior remains untested. No standalone installer is staged or executed.
- Mupen64Plus's standalone self-updater is disabled in the ECAM overlay, including its settings
  action: embedded runtimes must be updated with ECAM, never by installing a standalone release.
- Read-only artwork reuse: adjacent images, adjacent covers folders, donor covers/cache folders
  and N64XY/NESYX path-hash serial filenames. Imported metadata can point to images contained
  within its library root. No artwork download is triggered by this resolver.
- Bounded offline gamelist.xml import to `.ecam-library.json`, imported display names and images,
  optional streaming raw-file CRC32 calculation, and a size/mtime CRC cache. CRC/title database
  input is optional and user-supplied. XML entities, external paths and symlink ROMs are refused.
  ROMs and saves are never rewritten. CRC refresh is available for files whose timestamps were
  preserved; raw file CRCs are not canonical cross-byte-order/header ROM identities.
- Empty/missing ROM checks, N64 byte-order/header validation, NES/UNIF header validation, archive
  signatures/size checks, backend-specific `.rom`/`.7z` routing, and explicit FDS BIOS location/
  size guidance. The native loaders still validate/decompress archive members and complete ROMs.
  An FDS image inside an archive receives BIOS validation from the native loader.
- Runtime diagnostics show selected program, package availability and native data/log root.
- Existing USB whole-backup/restore includes Porpoise GC/Wii data and separate N64XY, NESYX,
  Mupen64Plus and FCEUmm save/state sets. ROMs, BIOS and HD packs are excluded. Restore retains
  replaced files in the owning backend's saves-before folder. The existing global Restore from
  USB workflow restores these sets; per-game save/state management is still native.
- Mupen64Plus performance-log conversion to CSV/JSON with VI/s, speed, CPU/RSP/RDP/VI/idle shares,
  audio queue and underruns. VI/s is not unique rendered FPS. The tool consumes collected logs;
  it neither runs a benchmark nor asserts an improvement.

## Capabilities available through optional native frontends

These are features retained from the exact donor sources, not newly implemented ECAM menus or
independently PS5-tested claims. Open **Emulator settings and saves** after selecting the backend.

| Optional runtime | Retained native capabilities |
|---|---|
| Mupen64Plus PS5 | x86-64 dynarec and interpreter choice; paraLLEl-RDP GPU and Angrylion software rendering; 1x/2x/4x/8x internal resolution; accurate/fast GPU sync; HLE/LLE audio; emulated CPU overclock; configurable software render workers; VI filtering/overscan; Controller/Rumble Pak selection; dead zones/stick inversion; global and per-game controller layouts; ten state slots and resume prompt; fast-forward; GameShark database/custom cheats and RAM search; native timing overlay; cover/name lookup and shelf/grid/list views |
| NES FCEUmm PS5 | In-memory optional Mesen database header corrections; FDS/VS support; region selection; palettes/NTSC filters; CRT shaders/scaling/smoothing/scanlines; HD packs; expansion-audio quality/low-pass controls; turbo/remapping/disk controls; sprite limit/RAM-start options; rewind/fast-forward; ten state slots; CRC/name/cover lookup and caches; SMB/NFS network shares; ZIP/7z loaders; internal/USB/network tabs; localization and logs |

The new FCEUmm donor's README explicitly reports no console testing. Its host tests and source
features must not be presented as verified PS5 operation. Mupen64Plus's performance reports are
upstream reports; ECAM's packaging, title and firmware 4.50 still need their own measurements.

## Exact additional donors and build

- https://github.com/TheRealRetro/mupen64plus-ps5 at `a915117cad9f93d9cda2ec78915366bb29b90883`
- https://github.com/MisterTemaki/fceummPS5 at `0e43fe1d17ce488e0a2eab6fe66a2c7a69a74b99`

```sh
# Optional alternatives; does not replace the default three runtime targets.
python3 tools/build-retro.py --prepare-only mupen64plus fceumm
# Inspect generated sources, then use a prepared PS5 SDK and clang/lld 18:
python3 tools/build-retro.py --reuse-prepared mupen64plus fceumm
# Or build into fresh generated worktrees:
make retro-upgrades
```

Mupen64Plus needs its documented PS5 Vulkan/RADV dependencies for GPU rendering. Set
`ECAM_N64_VULKAN=0` for its CPU-renderer build; this does not provide the GPU options.
`PS5_PAYLOAD_SDK` and donor-specific dependency requirements still apply. Source checkout overrides
are `ECAM_MUPEN64PLUS_SOURCE` and `ECAM_FCEUMM_SOURCE`. Old prepared worktrees from an earlier overlay
must be moved aside and regenerated; `--reuse-prepared` checks the pin and current overlay digest.
Source pinning is not a promise of reproducible dependency toolchains or an ABI-compatible binary.

```sh
# Run beside a locally organized ROM library; images stay in place.
python3 tools/retro-library.py /path/to/games --gamelist /path/to/games/gamelist.xml
python3 tools/retro-library.py /path/to/games --gamelist /path/to/games/gamelist.xml --crc --crc-database /path/to/crc-titles.json
# Copy the generated sidecar and referenced images with that library to the PS5, then rescan.
# Database format: {"lowercase-raw-file-crc32": "Display title"}
python3 tools/retro-performance.py /path/to/boot.log --format csv
```

## Remaining work and deliberately unsupported combinations

- Complete five native runtime builds, ECAM linking/package inspection and firmware 4.50 tests.
- Validate helpers, executable memory, SDK/libc loading, graphics/audio/controller behavior, repeated
  launch/return, crash/failure behavior and all original systems. No FPS gain is claimed yet.
- ECAM-native per-game emulator option editors, unified state-slot thumbnails/resume UI, automatic
  cover downloads and an ECAM SMB/NFS scanner bridge are not implemented. Equivalent native
  frontend functionality is retained where the selected donor supports it.
- No cross-core save-state conversion, automatic save migration, native network-settings sharing,
  or imported video previews. The native runtime menus own those settings and state formats.
- Native settings are not automatically synchronized with ECAM options for the original systems.
- Detailed ECAM play duration across process handoffs, automatic GC/Wii compressed-image
  classification, dedicated new-system artwork, Transfer Pak/64DD/netplay, and arbitrary-install-
  alias qualification are not added by this change.
- pNES/Nestopia, pNES5, EmuC0re, PSN64 and NATIVE-EMUS-PS5 are research references, not additional
  bundled backends. Their different environments/accuracy tradeoffs do not establish an upgrade
  over the two selected alternatives. No unsupported feature is enabled just because a core on
  another platform implements it.

## Validation

`make test-retro` runs the integration boundary suite and importer/performance tests.
`ECAM_TEST_SANITIZE=1 bash tools/tests/test-retro.sh` enables ASan/UBSan.
`python3 tools/tests/test-retro-overlays.py ../work` checks all five exact donor checkouts without
changing them. With no path it retrieves only the tested source files from immutable commit URLs,
which the CI workflow also checks. Existing `make test-contribution` includes USB restore tests
that verify backend separation, ROM exclusion and retention of replaced files.

The alternative entry points were checked with host C++17 syntax compilation; ECAM frontend changes
were checked with host C++20 compilation. None of these checks emulate a PS5 or boot an N64/NES game.
