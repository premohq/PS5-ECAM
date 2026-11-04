# Four separate system folders

The four systems are `WiiU`, `N3ds`, `Nds`, and `Switch` in `port/app/catalog.h`.
Their saved catalogue keys remain `wiiu`, `3ds`, `ds`, and `switch`. Their backends are Cemu,
Azahar, melonDS, and Eden.

New installations use four **top-level sibling directories**, outside the launcher's folder:

```text
/data/
├── cemu/                    Wii U
│   ├── games/               WUA, WUD/WUX and unpacked games
│   ├── updates/             unpacked update sources
│   ├── dlc/                 unpacked DLC sources
│   ├── keys.txt             your disc keys, one per line
│   ├── mlc01/               installed games, updates, DLC, Wii U system files and saves
│   ├── settings.xml, controllerProfiles/
│   └── graphicPacks/, reshade/, cache/, log.txt
├── azahar/                  Nintendo 3DS
│   ├── games/               decrypted ROMs
│   ├── updates/, dlc/       decrypted CIA sources, installed through Install a CIA file
│   ├── sysdata/             optional aes_keys.txt and other supported system dumps
│   ├── sdmc/                installed CIA games, updates, DLC and saves
│   ├── nand/                native 3DS system storage
│   └── cheats/, load/       native cheats, texture packs and mods
├── melonds/                 Nintendo DS
│   ├── games/               NDS ROMs
│   ├── bios/                optional bios7.bin, bios9.bin and firmware.bin
│   └── saves/, states/, cheats/
├── eden/                    Nintendo Switch
│   ├── roms/                NSP/XCI games; a game may have its own add-ons beside it
│   ├── updates/             NSP/XCI updates and DLC
│   ├── keys/                your prod.keys and title.keys
│   ├── firmware/            your firmware's NCA files
│   ├── user/                native NAND, saves and profiles
│   └── config/, mods/, covers/, logs/
└── ps5cemu/                 shared launcher settings, logs, covers, amiibo and downloads
```

The launcher creates the input/storage directories at startup. `keys.txt` is created with a
comment only if it does not exist. No keys, games, BIOS or firmware are supplied. An empty
3DS `aes_keys.txt` is deliberately not created: that would override working built-in keys.

Each backend keeps its required formats. Wii U system files belong in the MLC; 3DS titles and
system files use `sdmc`, `nand` and `sysdata`; DS firmware belongs in `bios`; Switch firmware
belongs in `firmware`. The current DS backend has no update/DLC installer and needs no key
folder. Switch updates and DLC need no install: Eden reads them where they are, in `updates`, in a
`dlc` folder if you make one, in a game's own folder, or beside the game, in any games folder
(`patches/eden/0018`). Staging folders do not automatically install content: use the existing
Wii U installer or 3DS CIA installer.

## Existing installations are preserved

Without a verified migration marker, existing system storage is used in place:

| Backend | Legacy location |
|---|---|
| Cemu | `/data/ps5cemu` when it contains `settings.xml`, `mlc01`, `keys.txt`, `controllerProfiles`, `graphicPacks` or `reshade`; a nested `ps5cemu/cemu` is also discovered |
| Azahar | `/data/ps5cemu/azahar` |
| melonDS | `/data/ps5cemu/melonds` |
| Eden | `/data/ps5cemu/eden` |

Legacy storage wins an unactivated conflict. Unreadable entries and dangling links are not
treated as missing installations. Custom game folders and genuinely custom MLC/game-files
settings remain explicit choices. Former default paths are mapped to the selected system root
after migration, including Cemu's saved MLC path and Eden's saved `game_files` default.

Before migration, both old and new default Wii U game folders are discoverable. After verified
activation, only the new default is scanned, so retained original copies do not reappear as
duplicate content. Catalogue IDs and save-backup archive keys are unchanged. Old save backups
can restore into the separate layout. Files replaced by restore are kept under the owning
system's `saves-before/<time>` on the same filesystem.

Eden's separate executable is updated too (`patches/eden/0010-...`). It uses exactly the same
selection code as the launcher, resolving after elevation, so its launch requests, keys,
firmware, saves, settings and diagnostics agree on `/data/eden` or the discovered legacy root
(or, with the app's data on its drive, that drive's `eden`, `patches/eden/0019`).
The launcher no longer points at one folder while Eden uses another.

## Separating an install from before, on the PS5

At a start of the app, an install from before is asked once: **A folder for each system**,
**Reorganize** or **Later**. **Later** asks again a week on; **Settings > Game files > A folder for
each system** (hold Cross) does the same at any time. Playtendoh then starts again, and moves the files
as it starts, before anything opens them (`port/app/reorganize.h`):

- Each system's files are **renamed** into its folder: Cemu's files at the top of `/data/ps5cemu`
  (`settings.xml`, `mlc01`, `games`, `keys.txt`, `graphicPacks`, its shader cache and the rest), and
  the `azahar`, `melonds` and `eden` folders whole. On the same drive a rename moves a folder whatever
  its size, so it takes a moment, needs no free space, and nothing is copied or deleted.
- Before anything moves, every file is checked: if one is already where it would go, or the folder
  would be on another drive, nothing moves and the row says what is in the way.
- `reorganize.txt` in the launcher's folder says what is under way, so the start after a power cut
  finishes it. A system whose move fails part way is put back whole where it was, and keeps working
  there; the next start says which, and `boot.log`'s `[reorganize]` lines say why.
- Each system moved gets its `.ecam-separated-v2` marker. Paths saved before follow: the games
  folders in the settings, Cemu's game folders and MLC, home screen tiles made before (a tile's game
  is looked for where it moved).

The same row then puts them back (hold Cross), with the same checks: **Go back** to a version from
before (Settings > Online) needs that first, as that version looks for the files in `/data/ps5cemu`,
and is not offered until then.

## Or: back up, verify and copy, on a computer

For a full verified backup and copies instead of moves, `tools/separate-systems.py` defaults to a
**read-only plan**. Run it on a computer with a mounted
or copied console data tree. Python does not need to run on the PS5. Close the launcher and
Eden before taking the source snapshot and keep them closed through transfer and verification.

```sh
python3 tools/separate-systems.py --legacy-root /mounted/data/ps5cemu
```

To apply, name a new independent backup directory:

```sh
python3 tools/separate-systems.py --legacy-root /mounted/data/ps5cemu \
  --apply --backup /backup-drive/ECAM-before-separation
```

The tool:

1. Rejects symlinks, special files, conflicting populated destinations, mixed Cemu layouts and
   backups overlapping the source or destination. Empty directory scaffolding from startup
   can be reused; existing files are never overwritten.
2. Makes a full independent backup of the shared legacy root, not just saves. If Eden lives
   outside it, backs that up too. Verifies paths, empty directories, sizes and SHA-256 hashes,
   and writes `manifest.json` to the backup.
3. Copies Cemu's content into sibling `cemu`, and the complete Azahar, melonDS and Eden trees
   into sibling `azahar`, `melonds`, and `eden`. Verifies each copy and prepares native input
   folders. Launcher settings and shared assets stay in `ps5cemu`. Rebuildable Cemu title-list
   metadata is omitted to prevent stale paths. Transient Eden launch/session/result files are
   backed up but not copied into the active targets, so stale requests are not replayed. Custom
   paths are not rewritten in place.
4. Checks that the source did not change while copying. Only after all copies are verified,
   writes `.ecam-separated-v2` with the exact version token into each target. The launcher
   and Eden recognize these as explicit activation of the verified copies.

**No original file or directory is moved or deleted.** A failed run keeps originals, backup
and partial copies for inspection; never retry by deleting originals. A full independent
backup and the separate copies require additional storage space.

When staging on a computer, transfer all four complete sibling directories back to their
corresponding locations under `/data`, including their hidden activation markers. Verify the
transferred content against the staged copies or hashes before starting the app. The tool
verifies the local copies; it cannot verify a later FTP transfer. `boot.log` records all four
selected roots. Verify a game, its add-ons, keys/firmware and save on every system before
retiring any old copy.

For rollback, close both programs, back up any newer data from the separate folders, then
rename their activation markers to a different name. The original legacy storage is selected
again at next launch. Retain the old and new saves and the independent backup for comparison.
Do not merge or overwrite them automatically.

## Launcher data on an app drive

When `PS5CEMU-HAR-data` is used on a USB or extended-storage drive, the three in-process systems
use **siblings** such as `/mnt/usb0/cemu`, `/mnt/usb0/azahar` and `/mnt/usb0/melonds`. The shared
launcher folder stays `/mnt/usb0/PS5CEMU-HAR-data`. The Switch's is `/mnt/usb0/eden` too
(`patches/eden/0019`), unless the console's `/data/eden` (or the discovered `/data/ps5cemu/eden`
legacy directory) holds files: Switch keys and saves already there stay in use, and only the empty
folders earlier versions made at every start do not count. Both programs choose with the same code
(`ecam_layout::Eden`), from ShadowMountPlus's record of where the app is mounted from, and the
launcher marks the drive's `eden` with `.ecam-separated-v2` when it chooses it, so a file put on the
console later does not take the Switch side back. Its existing custom game-files setting and
standalone ProsperoEden discovery continue to work.

**Reorganize** works the same there: the three move within the app's drive, and the Switch's within
`/data` when it is there. For the copying tool in this arrangement, provide `--eden-legacy` pointing to a mounted/copied
`/data/ps5cemu/eden` tree in addition to `--legacy-root` for the app-drive launcher tree. The
Eden target is a sibling of that console tree's `ps5cemu` directory. Back up and transfer each
volume to its corresponding location; do not copy an entire `/data/ps5cemu` and delete it on
the assumption that it still owns all four systems.
