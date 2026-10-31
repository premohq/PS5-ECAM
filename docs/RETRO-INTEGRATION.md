# Porpoise, N64 and NES integration (AI-assisted)

This contribution adds four ECAM library systems backed by three source-built, bundled native
runtimes. It follows the existing Eden process-handoff architecture: the launcher releases its
graphics resources and loads a program inside the ECAM package. No separately installed emulator
app is required. Existing standalone data folders are used, without moving or converting saves.

See [RETRO-UPGRADES.md](RETRO-UPGRADES.md) for the optional Mupen64Plus/FCEUmm runtimes, artwork/metadata, validation, diagnostics and USB backup upgrades.

Status: implementation submitted for source review. Host tests and source-overlay checks are
available. A full PS5 cross-build and console launch/return qualification are still outstanding.

## Architecture

| ECAM system | Backend | Bundled executable | Default games folder | Native data |
|---|---|---|---|---|
| GameCube | Porpoise / Dolphin libretro | retro/porpoise/eboot.bin | /data/porpoise/games/GameCube | /data/porpoise |
| Wii | Porpoise / Dolphin libretro | retro/porpoise/eboot.bin | /data/porpoise/games/Wii | /data/porpoise |
| N64 | PS5N64X / ParaLLEl-N64 | retro/n64/eboot.bin | /data/PS5N64/games | /data/PS5N64 |
| NES | PS5NESX / FCEUmm | retro/nes/eboot.bin | /data/PS5NES/games | /data/PS5NES |

The original Wii U/3DS/DS/Switch indices stay 0–3. New indices 4–7 extend the catalogue, library
sorting/filter arrays, visibility mask and per-system library settings. Existing four-entry
settings arrays load with zero-initialized new slots. Existing visibility choices are preserved;
enable the new systems in Settings > Launcher > Sides after packaging their runtimes.

Each additional system has a game-folder picker and explicit rescan. Local and USB directories
are searched up to four levels, with a 100000-entry ceiling, without following directory symlinks.
Stable synthetic IDs are namespaced separately from Switch titles. Discovery does not open or
rewrite save files. Missing runtimes and inaccessible game roots are reported before launch.

### Discovery formats

- GameCube/Wii: ISO, GCM, RVZ, CISO, GCZ and WIA; Wii additionally WBFS, WAD, DOL and ELF. A raw ISO or
  GCM is listed on the side its disc header names (Wii or GameCube), and refused on the other.
- N64: Z64, N64, V64 and ZIP; ROM additionally requires the optional Mupen64Plus backend.
- NES: NES, UNF, UNIF, FDS and ZIP; 7z additionally requires the optional FCEUmm backend.

The configured GameCube and Wii roots are separate. The scanner reads a raw disc's header, but not
the contents of a compressed container (RVZ, CISO, GCZ, WIA): keep those in separate folders to avoid
mixed lists. No files are reorganized automatically. Archive member selection, decompression and full content validation remain the
native NES/N64 loader's responsibility; ECAM performs basic format and size preflight checks. BIOS/system files required by a game remain user-provided.

### Game and settings handoff

ECAM sends the exact selected ROM path and an explicit parent eboot return path. The existing
LoadExec helper also writes a one-use request beside the runtime, because PS5 LoadExec can drop
argv. The runtime bridge checks a bounded request, preserves spaces without invoking a shell,
and consumes it before use. It checks known app aliases and internal/extended/USB install roots.

The source overlay adds forwarding to the NES/N64 native frontends and extends Porpoise's
existing forwarding support. A forwarded NES/N64 game skips the donor library; leaving the game
runs its normal teardown and returns to ECAM. Porpoise forwards into Dolphin and returns after
the game finishes, fails, or requests app exit. During Porpoise shutdown the parent path is copied
to static storage before its allocator is released.

Settings > Emulator settings opens the bundled native frontend with a library-only request.
That frontend supplies its existing emulator options, controller setup and save-state shortcuts;
closing it returns to ECAM. The core's settings and save identity algorithms are not changed. Optional alternative backends keep their own data and state formats.
ECAM does not offer its Wii U/3DS/DS/Switch save or home-tile actions for these additional systems.

## Source provenance and licensing

Exact donor revisions are in tools/retro/sources.json:

- premohq/Porpoise-X: 3a4b194c5a0b889ac2c0a7e9e5b0dea143fa5296 (its ps5-ecam branch: 4.0, the bridge in it, none of its menus' sounds, downloads, updates, home screen tiles or first start when PS5-ECAM starts it)
- Yahballon/PS5N64X: 28c55d854f5e451da9ffd5a78c694415accad172
- Yahballon/PS5NESX: 58168b79fe0e2cce7d42b8b8109d74a4283a2b41

The preparation tool applies narrow integration changes in separate pinned worktrees. Existing
donor checkouts and their local changes are not modified. Emulator cores, CPU/rendering choices,
controller bindings and native save serialization are retained. Original source notices and
packaged component licenses remain with each runtime. ECAM-SOURCE.json records the pin and
hashes of modified source files; ECAM's license is included as an additional notice.

NES/N64's standalone packaging reused signed Lapy helpers for their original title IDs. Those
helpers are neither required nor copied by the ECAM overlay. Filesystem access must be qualified
under ECAM's title and the active HEN/elevation implementation. Do not reuse a helper for another
title as proof that this path works. Emulator runtime libc/SDK compatibility also requires a
complete linked build and console validation.

## Build workflow

The native donor builds require their documented prepared PS5 dependencies and clang-18. Follow
the pinned donors' build documentation; the ECAM helper does not provision a Linux environment
or silently replace compiler versions. Porpoise requires PS5_VULKAN_DIR; NES/N64 require
PS5NES_DEPS / PS5N64_DEPS and their SDK, libnfs and zlib inputs.

```sh
# Prepare exact source worktrees and inspect the overlays without cross-compiling:
python3 tools/build-retro.py --prepare-only porpoise n64 nes

# Build those reviewed worktrees once the native dependencies are available:
python3 tools/build-retro.py --reuse-prepared porpoise n64 nes

# Or build directly into fresh worktrees:
make retro

# Build/package ECAM with any staged build/retro/<runtime>/ directories:
make package
```

Existing worktrees and staged runtime directories are never silently replaced. Move previous
outputs aside before rebuilding, or use --reuse-prepared after review. Source checkouts may be
supplied via ECAM_PORPOISE_SOURCE, ECAM_N64_SOURCE and ECAM_NES_SOURCE. Only source-built runtime
folders with ECAM-SOURCE.json are staged by packaging. No downloaded release eboots are substituted.
All staging occurs before the package permission/inspection step. Packaging and preparation do
not install, execute, merge, publish a release or contact a console.

## Host validation

```sh
make test-retro
make test-contribution
python3 tools/tests/test-retro-overlays.py /path/to/donor-checkouts
python3 -B tools/lang.py check
```

The overlay checker expects Porpoise-X, PS5N64X and PS5NESX folders at their exact pins. It applies
patches twice to temporary copies, checks forwarding/return anchors and Python build syntax, and
verifies that the donor source files remain unchanged. The C++ host suite checks side identity,
format isolation, bounded discovery behavior, scanner shutdown, settings compatibility, missing
runtimes, exact handoff paths, request parsing/consumption and parent-path lifetime.

Host fixtures contain synthetic filenames and data; no commercial ROMs or BIOS files are included.
Host checks do not boot the PS5 runtimes or validate emulated audio/video/controller behavior.

## Owner qualification before merge

1. Complete all three source builds and the ECAM build. Check runtime imports, libc/SDK versions,
   asset lookup under the ECAM title, packaged paths, notices and provenance.
2. Enable all eight systems; check the bar, switching, filters, favourites, folder pickers,
   rescanning and a previous four-system settings file. Verify the original systems still launch.
3. On disposable copies, launch one game in each new system from internal and USB storage.
   Test spaces/non-ASCII names, nested directories, missing files, invalid ZIPs and containers.
4. Confirm direct-game start and return to the same ECAM system after normal quit and failed
   boot. Explicitly test argv loss, internal/extended/USB app locations and one-use requests.
5. Check native settings entry/exit, controller mappings and shortcuts, audio, video and Wii
   input. Confirm existing memory-card, SRAM and state files are still accessible and persist.
6. Confirm HEN/elevation under PPSA99360; no standalone-title Lapy helper should be used.
7. Repeat game/settings/ECAM transitions and watch memory, renderer teardown, logs and save errors.

## Current boundaries

- Source integration is implemented; a complete cross-build and PS5 runtime testing are pending.
- Existing standalone data is reused directly. There is no automatic migration or save conversion.
  Use copies for qualification; compatibility with an unknown installed standalone version is not
  established by matching its data-folder name.
- Local/donor artwork reuse, offline metadata import and whole-system USB save backup/restore are included in the upgrade. Automatic cover retrieval, detailed play-duration reporting and ECAM-native per-game/state editors remain outstanding. Native runtime frontends retain their own functionality. New system tiles currently use the shared fallback rendering.
- ECAM's new scanner supports local/USB paths. Native network browsing remains in the bundled
  native frontend; an ECAM NFS/SMB scanner bridge is not implemented.
- Arbitrary app-install locations outside the bridge's known aliases, nested runtime libc loading,
  title elevation, Porpoise asset roots and recovery from a runtime crash need console evidence.
- Porpoise's GameCube/Wii support is still subject to its upstream compatibility limits. No
  performance or compatibility improvement is claimed by adding the library/handoff integration.
