# Roadmap

What PS5-ECAM is working towards: first the rebuild and stability work still open (the launcher
rewrite of October 2026 and the issues around it), then a release that gives each emulator a folder of
its own, then the longer list of ideas to pick from. Written 23 October 2026.

## Now: finishing the rebuild

The launcher is one layout now (themes are skins over it), a 3DS or DS game comes back to the library
without the app starting over, the graphic packs download on demand, a network share's library is
kept between looks, and every network call has a timeout. What is left:

### To check on a console

None of the October rebuild has run on a PS5 yet: the PC preview draws the launcher, and the rest was
checked by reading and compiling. Before a release:

1. **3DS and DS back to the library:** quit a game, play another of the same side, quit again; then a
   Wii U game after a 3DS one (that one starts the app over, `--rom`), and Resume where you left off
   after it. The boot log says `back to the library in this process` each time.
2. **Screen gap:** the quick action on the 3DS and DS, in every layout and turned like a book; the
   touch screen still under the cursor.
3. **Graphic packs:** downloaded on the first Wii U start without them (`[packs]` in the boot log),
   and the packs a game has on still on after.
4. **An update from 5.0.1:** it installs, and `assets/graphicPacks` and the TGA borders are gone from
   the app's folder afterwards (moved into `previous-version`).
5. **Network shares:** a side on a share opens at once with "As found last time"; Look for games
   now finds a game added since.
6. **3DS borders** (now PNG) show in a game.
7. **Update timing (#78):** Check for updates answers in about a second; an update takes a minute or
   two, not 20 to 30, with no "0 of 0 MB" step; the boot log's `files unpacked in` and `moved into
   place in` give the install's own times (32.5 s and 17.7 s one at a time). Go back in Settings >
   Online still puts the release before back. Box art, the graphic packs, RetroAchievements and the
   Switch rooms still download.
8. **Play time:** a game played a few minutes shows them in the library and on its page, on each
   side; for the Switch, after the app comes back from Eden (`played for` in the boot log). Then
   **Settings > Play time** adds them up.
9. **Hide a game and Surprise me:** hide a game from its Options menu, start the app again: still
   hidden, found by Search and (with Settings > Launcher > Library filters on) the Hidden filter; Back
   in the Library. Surprise me from Sort, on a side with a filter in use.
10. **The DS's stylus stick:** Settings > Screens > Touch > The right stick as the stylus, then Metroid Prime
   Hunters: the aim follows the stick, and lets go with it. Off, the right stick's buttons work again.
11. **Light bars:** a Wii U game with two to four players, each DualSense in its own colour, and
   again after one is turned off and on. For L8, the boot log's `[pad] P1: device data` lines, with
   the battery's level as the PS5's menu shows it, to find which byte it is.
12. **The bar:** Library and Settings beside P1 and the clock, on every side and with four players.
13. **Switch updates and DLC** (`patches/eden/0018`): a game with its update and a DLC in a folder of
   its own on a USB drive chosen as the games folder; one loose in `roms/` with them beside it; one
   whose update has no title ID in its name. Each shows the update's version on its title screen and
   has its DLC, and Eden's log says `Update and DLC beside the game` for the loose ones.
14. **Everything on a USB drive** (`patches/eden/0019`): a first start of the app installed on a drive,
    with no `/data/ps5cemu` and no Switch files on the console: `boot.log`'s `[paths]` line names
    `<drive>/eden`, a Switch game starts with keys, firmware and its update there, its save is kept
    there, and it is still there after the app starts again. Then an install from before on a drive
    whose Switch files are in `/data/eden`: they stay in use.
15. **An older copy left in `/data/homebrew`** with the app mounted from a USB drive (an image too):
    leaving a Wii U game, and an update, start and update the copy on the drive.

### Stability

- **The Wii U side still starts the app over** after a game: Cemu keeps VideoOut (its GPU thread owns
  the swapchain until the process ends). Find whether `LatteThread_Exit` can hand VideoOut back, as
  Azahar and melonDS do, so the Wii U side comes back in place too; until then the restart stays,
  with its known ways of failing (#60, #68).
- **Updates and downloads that waited for minutes** ([#78](https://github.com/premohq/PS5-ECAM/issues/78),
  the real cause of [#73](https://github.com/premohq/PS5-ECAM/issues/73) and #72's "update loop"):
  dolphin78 timed every step on a console. Each request finished in a second, then
  `curl_easy_cleanup` sat on the idle keep-alive connection until the server dropped it (probably
  curl's `fcntl` non-blocking, which the PS5's sockets ignore). **Done** with their fix, onto this
  branch's cancel and stall timeout: every request now sends `Connection: close`, from one place
  (`ps5curl::Headers` in `ps5/curl_socket.h`) that the update, the graphic packs, box art,
  RetroAchievements and the Switch rooms all take it from; only the ZIP moves the dialog's figures
  (no more "0 of 0 MB" for `SHA256SUMS`); the comments that blamed the resolver are corrected. The
  install's remaining minute (1,852 files unpacked, kept for Go back and moved into place one at a
  time) is now shared out among four threads, as `port/app/install.cpp` copies a game, with
  `eboot.bin` still moved last; there are fewer files too, with no graphic packs.
- **Switch games with a mods folder do not start** ([#77](https://github.com/premohq/PS5-ECAM/issues/77)):
  **done**, `patches/eden/0011`: upstream ProsperoEden's PR #288 (Oliv3rmoon), confirmed on a PS5 by
  its reporter. A game folder's mods were laid over the game's DLC and update too; Mario Kart 8 Deluxe
  with its DLC crashed as it booted. Also the likely cause of the Discord's MK8 Deluxe loop after its
  title screen: check with Teaz's and meundies' logs.
- **Switch updates and DLC not taken:** Eden read a game's update and DLC from `updates/` and from
  the game's own folder only when that folder was in its `roms/`. A games folder chosen in Settings >
  Game files (a USB drive's), and update and DLC files beside a game, were left out, though the
  library hid them as the game's and the guide said a game's folder brings them. **Done**,
  `patches/eden/0018`: the launcher says where it lists its games from (`--set games_folder`), a game
  in a folder of its own there brings that folder's update, DLC and mods as `roms/<folder>/` does, a
  game without one takes the files beside it that are its own (by the title ID in their names, else
  by what they hold), and `dlc/` is read as `updates/` is. `tools/tests/test-eden-addons.sh` checks
  the rules; check on a console (above).
- **The Switch's files never on the app's drive:** with the app on a USB drive or the extended
  storage, the Wii U's, 3DS's and DS's folders went beside the launcher's data there, but the
  Switch's keys, firmware, updates, saves and shader cache stayed in `/data/eden`, and an `eden`
  folder made on the drive was never read. **Done**, `patches/eden/0019`: both programs choose the
  Switch's folder with the same code (`ecam_layout::Eden`), on the drive unless the console's holds
  files already, and the launcher marks it there. `test-eden-paths.sh` checks both agree on a drive.
- **An older copy in `/data/homebrew` started or updated instead of the one on a drive:** with no
  ShadowMountPlus record of the app's folder (an image), a restart tried `/data/homebrew` first and
  an update replaced its files. **Done**: both take a copy only when its `eboot.bin` is the running
  app's (`ps5paths::RunsFrom`); an update with none found says to install by hand.
- **Upstream Switch fixes** (open PRs on Swordpdf/ProsperoEden, none tested on a PS5 by its author):
  taken as `patches/eden/0012` to `0014` (`21b8d87`): #287 (several updates of one game: the newest's
  version and control data), #289 (a full audio ring drops its oldest sound instead of stopping the
  game) and #298 (ExecuteProgram, for Metal Gear Solid: Master Collection's launcher). Check each on a
  console. Not taken yet: #285 (NSZ and XCZ games), #290 (a forwarded game moved into a game folder);
  take each once it is merged or tested on a console.
- **Switch games on 8.60** ([#72](https://github.com/premohq/PS5-ECAM/issues/72)): one report that
  they did not start on an earlier version; no log yet.
- **Wii U unpacked games found but not listed** (Discord: meundies 5 games in the folder, 2 listed;
  Sanji, Sx): no log yet. `patches/cemu/0046` makes Cemu's log say why each title it leaves out was
  refused; a likely cause is `Code`/`Content`/`Meta` named with capitals, which the scan accepts and
  the PS5's storage then does not find.
- **Wii U account names in games** (Discord, Lycnus): Rename now writes the name into the Mii data
  games read (`patches/cemu/0045`). Tekken Tag Tournament 2 showed `player1:default`, Cemu's first
  account's name, even with a new account: check on a console which account it ran on.
- **The theme launchers are gone** (`d079b49`): Classic's 2.0 layout, Eden's rail, Phantom and
  Terminal were whole launchers of their own in 4.0.0 to 5.0.1, at the owner's request (October 13);
  the rebuild made one layout with colour skins and dropped Phantom and Terminal. The owner's call:
  keep one layout, or bring them back over it.

### On GitHub

**The open issues on 30 October 2026**, each with what is left:

- **Close now**: **#98** (3DS frame generation "broken": its reporter had turned it off, and says so),
  **#78** (updates' `Connection: close`: shipped in 5.1.0, as its last comment says).
- **Close with 5.1.1**: **#93** (`840cbb4`: the touchpad's shortcuts with a Pro Controller. LEGO City
  Undercover still takes no input with the Pro Controller profile: it reads only the GamePad, so it wants
  the GamePad profile), **#94** (`4afd4c2`, Cemu patch 0047), **#96** (`b171274`: installs finish;
  the reporter's logs ask why they took longer than before).
- **Waiting on a console**: **#97** (`patches/eden` 0020, the repeated-fault count: a run past ten
  minutes), **#92** (Tears of the Kingdom in the Depths, reported on the Wii U side's form but on the
  Switch's Eden: its Eden logs).
- **Answered in code, the rest planned**: **#101** (Switch games on a share: `af8aeb4` says so and stops
  offering shares; playing them is [N0](#n0)), **#100** (the DS's 3D above its resolution: [D5](#d5)),
  **#99** (Lossless Scaling on the other sides: [F1](#f1)).
- **Requests to keep open**: #95 (the second screen's transparency), #88 (DolphinBar), #82 (Dolphin:
  GameCube and Wii run through Porpoise now, PR #87), #81, #72; #90 (3DS games' errors) waits on its
  reporter's answer.

- **PR #70** (Artic Setup: Azahar patches 0030 to 0032, fmt's prints to the boot log,
  `symbolize-crash.py --ps5`) and **PR #71** (Cyrillic and Greek from SST-Roman, #69) are already in
  the code: close both, thanking dolphin78.
- **#76**: dolphin78 traced the stripes on empty screens to the grain's float hash, the cause fixed in
  `4f9b6f9`; this branch also gives each list's labels one size. Close it with the release.
- **#81** (Lossless Scaling frame generation on the Switch, with the player's own `Lossless.dll`): done
  on this branch; close it with the release (5.1.0, `Makefile`). What closed #58 asked for, a Wii U update
  or DLC removed from the game's Options, is done too.
- **#74, #75**: done on this branch (packs downloaded when missing; a share's games kept between
  looks). Close them with the release.
- **#77** (Switch mods folder, `patches/eden/0011`) and **#80** (3DS: Remove its update or DLC, in a
  game's Options): done on this branch; close them with the release. **#79** (Cyrillic labels of
  different sizes) is #76's: the labels' one size; check on a console, then close it with #76.
- **PR #83 and PR #85** (yeager, the Swedish review; #85 is #83 again with his GitHub address): taken
  as `4ee7ff1`, his commit. Close both with the release, thanking him; he is in the README's
  contributors.
- **PR #86 and PR #84** (JMUtechnologies, co-founder): #86's 102 commits taken as `2aef5d4`, his
  commit, which has #84's tools too; then `9f7fb68` (a DS game starts without its Slot-2 cartridge
  or recorded microphone), `ba89451` (Eden's mod readers as patched), `855b9ea` (the system
  folders), `550808f` (its texts in every language). Close both with the release, thanking him. To
  check on a console: a Switch game's Mods and Cheats (one off, a 30/60 FPS group), Picture effects
  for the side and a game, Players (choose, new, rename, remove, then a game on each), a game's save
  to USB and back, a Ryujinx save; on the DS, Pal Park with a GBA game in `slot2`, Slot-2 on with a
  game that has none, the microphone's three choices (a missing WAV too), touch calibration, and a
  capture with `capture-frames`.

### Every Nintendo system

The owner, 29 October 2026: **PS5-ECAM's scope is every Nintendo system there is an emulator for,
the obscure ones too, and Nintendo's arcade hardware.** The side switch is a carousel now, so the bar
takes any number of systems.

**How: libretro cores behind PS5-ECAM's own UI.** One libretro frontend of the app's runs every
core, and each system looks and works as the others do: its side in the carousel, its library with
box art, the game menu, the in-game menu (its screens, picture, volume, controls, save states, back
to the library) and its settings, all PS5-ECAM's. Adding a system is then mostly a core and a
library entry (its file types, its box art's source, its default folder), so more or less every
emulator libretro has comes in the same way. What the frontend has to do once, for all of them:

- **The core's calls:** `retro_load_game`, `retro_run` once a frame, the video it hands over
  (software frames, and a Vulkan context for the cores that render with one: ParaLLEl-RDP for the
  N64, Dolphin), the audio batches at the core's rate, `retro_get_memory_data` for the save RAM
  written to the system's saves folder, `retro_serialize` for the in-game menu's save-state slots,
  and the core options as rows of the system's settings.
- **The DualSense** mapped to the RetroPad per system, with the touchpad's shortcuts as on the other
  sides (Touchpad click + Options for the in-game menu).
- **Where a core lives:** linked into one runtime program started as Eden is (`retro/eboot.bin`,
  LoadExec, back to the launcher when the game ends), or loaded as a module if the PS5 lets the app
  load one (the payload SDK has a `dlfcn.c`: to check). A program per few cores is the fallback.
- **Its own core per system first, the best one** (below), and a second core as a choice where one
  is faster.

**Where it stands:** the cores are built (`tools/build-libretro.py`: Mesen, Mesen-S, mGBA, Beetle VB,
PokeMini) and the in-game menu has a core's game (`ps5ingame3ds::Console::Retro`), the screens its
picture (`ps5dsscreens::Frame::picture`). `port/libretro/session.cpp` makes the core's calls: its
environment, its picture in any of the three formats, its sound, four RetroPads, its options (v0 to
v2, in the launcher's language where the core has it), the save RAM (`<saves>/<game>.srm`, written once
it holds still) and the save states with Undo; `cores.cpp` finds the cores the app links
(`tools/tests/test-libretro.sh` runs it with a core of its own). `port/libretro/game.cpp` is the game's
loop on the console, as the DS side's: the picture through the DS side's screens, the sound through
AudioOut at 48 kHz, the DualSense as each player's RetroPad with the side's controls, the in-game menu
(save states, Undo, Resume where you left off, volume, filter, border, A and B, back to the library) and
the play time. A side with a core and no program of its own (the SNES, the Virtual Boy, the Pokémon
mini) plays on it in the app once `tools/build-libretro.py` has built the cores, which
`tools/build-cemu.sh` then links (`build/libretro/objects.txt`). **To check on a console first:** a
game on each of the three, its save after a quit, a state, Resume, the sound's pace (`[perfretro]` in
the boot log). Then the NES on Mesen as a choice beside its program, and the others in turn.

**The classic systems already added** (JMUtechnologies, PR #87 and PR #91) are to run on the best
emulator there is for each, replaced where they do not, maybe as libretro cores. This is the N64, the
NES, the Game Boy, the Game Boy Color and the Game Boy Advance. Not GameCube and Wii (Dolphin, through
Porpoise), and not the four sides the app began with (Wii U, 3DS, DS, Switch).

| System | Runs now (RETRO-INTEGRATION.md, GAMEBOY-INTEGRATION.md) | The best to try |
|---|---|---|
| N64 | PS5N64X (ParaLLEl-N64), Mupen64Plus as the other choice | Mupen64Plus-Next with ParaLLEl-RDP and ParaLLEl-RSP, ParaLLEl-N64's successor |
| NES | PS5NESX (FCEUmm), FCEUmm as the other choice | Mesen, the most accurate; Nestopia UE next |
| GB, GBC | PS5GBCX (mGBA 0.10.5's GB core) | SameBoy, the most accurate; Gambatte next |
| GBA | PS5GBAX (mGBA 0.10.5) | mGBA stays, at its newest |

**Still to add**, with the emulators to look at first (to check, each, for what it runs today):

| System | Emulators |
|---|---|
| SNES / Super Famicom, with Satellaview and Sufami Turbo | bsnes (the most accurate), Mesen 2, Snes9x for speed |
| Famicom Disk System | Mesen, which the NES's choice may cover already |
| Super Game Boy | bsnes; SameBoy's SGB mode |
| Nintendo 64DD | ares, Mupen64Plus-Next |
| Virtual Boy | Beetle VB (Mednafen) |
| Pokémon mini | PokeMini |
| Game & Watch | MAME's handhelds; the gw-libretro simulator |
| e-Reader | mGBA, with the GBA |
| iQue Player, Color TV-Game | MAME, where it runs them |
| Arcade: Donkey Kong, Donkey Kong Jr., Mario Bros., Popeye, Punch-Out!!, Radar Scope, Sheriff, Space Fever and the rest of Nintendo's own boards | MAME |
| Arcade: VS. System and VS. UniSystem, PlayChoice-10, Nintendo Super System | Mesen and FCEUmm (VS.), MAME |
| Arcade: Aleck64 (N64-based), Triforce (GameCube-based) | MAME (Aleck64); Dolphin's Triforce work, if any is current |

- **The donor apps give way to the frontend**: PR #87's and PR #91's systems move onto it, one at a
  time, as their best core runs there. Porpoise already runs Dolphin as a libretro core, so its
  frontend code is the place to start from (GameCube and Wii stay on Dolphin).
- Each core measured on the console, speed and accuracy, before it becomes a system's default
  (performance changes stay off until measured). The one it replaces stays a choice until then.
- Saves keep working: a core that writes them differently converts them once, the originals kept
  aside.
- None of PR #87's and PR #91's runtimes has been built for the PS5 or run on a console yet, so the
  first step is to build and qualify them, whichever cores they end up with.
- **Room for more systems in the code:** a system is a `ps5launcher::System` and a place in
  `kBarOrder` (`frontend/launcher.h`, `shell/shell_internal.h`); arrays sized by the side count
  (the catalogue's and the settings' lists, `lastLook`, `noticeRetro`) grow with it. Which sides
  are shown is a bit each in `ui.sides`, read with an upper limit (2047 for 11 sides) that grows too,
  and an `int` holds 31 of them: past that it needs a wider mask or a list of names.

### GameCube and Wii: Porpoise X as Eden is

The owner, 31 October 2026: Porpoise X (premohq/Porpoise-X) goes the way ProsperoEdenX went for the
Switch side: its UI taken out, PS5-ECAM's in its place, every one of its settings and features kept.
Its work is on its `ps5-ecam` branch (as ProsperoEdenX's `ps5`), which `tools/retro/sources.json` pins;
its `main` stays upstream's (4.0 now).

1. **Started by PS5-ECAM, back to it.** **Done:** `c855be7` (the bridge in Porpoise X itself, no longer
   applied by `tools/retro/prepare.py`, which finds it there and changes nothing).
2. **None of its own screens** when PS5-ECAM starts it: no library, settings, memory cards, setup or
   update check; its UI's assets (fonts, images, sounds, themes) neither loaded nor packaged, only what
   the emulation reads (Dolphin's `Sys`, `recommended.ini`, the per-game profiles). **Started:** `3b36a08` (no menu
   music, sounds, jingle or cover downloads; Porpoise checks for updates only when its own Settings
   asks). Its fonts and theme stay until step 4's menu replaces its in-game one, which draws with them.
3. **Its settings in PS5-ECAM's Settings**, the GameCube and Wii sides' and a game's own: every row of
   its `add_game_rows` (Video, Enhancements, Graphics, Audio, Controls, System, Wii Remote), with its
   controller layouts and Wii Remote setup, written to its own files by `port/retro/porpoise_settings.cpp`
   (**done:** the files), in every language. Its menus' own (themes, library views, menu music) are
   PS5-ECAM's launcher's.
4. **PS5-ECAM's loading screen and in-game menu** drawn by Porpoise's program over its frames, as Eden's
   (`port/eden/ingame.cpp` built into it): a `port/porpoise/ingame.cpp` with the session file the
   launcher writes (`ps5eden::WriteSession`'s), recording into Porpoise's present pass
   (`src/porpoise_vk.cpp`), its rows Porpoise's in-game actions (save states, the game's settings,
   controllers and the Wii Remote's pointer, fast forward, screenshot, cheats, back to the library).
5. **To check on a console** at each step: a GameCube and a Wii game started, played, quit back to the
   library; a setting changed in PS5-ECAM seen in the game.

### EmulationStation, the other frontend

**Started, 30 October 2026, not built yet.** ES-DE as a choice beside the launcher (Settings > Launcher >
Start in EmulationStation, when the app has it): Mihawk's PS5 port of ES-DE 3.5.0 (`esde-port` in
`tools/deps.json`: his patches/0001 and his PS5 glue), with PS5-ECAM's glue in place of his
(`port/esde/ps5`), only what PS5-ECAM's ES-DE uses linked into the program, and `patches/esde` (ES-DE's
scraper in its menu, its notes and log lines saying what PS5-ECAM does). The
launcher writes ES-DE's systems and game lists from its library before each start (`port/esde/esde.cpp`),
and every game ES-DE starts plays here and goes back to it (`port/esde/handoff.h`). Checked on the host:
the files ES-DE reads (`tools/tests/test-esde.sh`, and against ES-DE's own systems file), the handoff,
both patches applying to ES-DE 3.5.0, the glue compiling. Next:

- **`make esde` the first time**: `tools/build-esde.sh` builds it over the public PS5 OpenGL SDK 1.0.2
  (darkxex's copy of BlackBearReloaded's), where Mihawk builds it over his own OpenGL tree: the first
  build says whether that holds, then a console: the start from the launcher, a game from each kind of
  side (the Wii U's and the 3DS's in the app, the Switch's and the other sides' programs) and back,
  L1 at the start, Quit.
- **Its media from the launcher's covers**, so ES-DE has box art before its scraper runs.

### Switch 2

**Started, 30 October 2026: experimental.** The Switch side's Eden is Takoroka's eden-nx2 (Eden master
with Switch 2 support) at `50fa86bbbc`, ported as Oliv3rmoon's Nightshade did (`feature/nx2`): the pin
and the derivations moved (patches/eden 0021), and a game folder's ExeFS and RomFS play, a Switch 2
title by its npdm, with eden-nx2's session settings (0022). Six emulated cores: what ProsperoEdenX counts
per core has a place for each. The launcher lists such a folder as a Switch game, `Switch 2` its
format (port/eden/exefs.h). Nothing has run on a console yet. Next:

- **A first console run** with a tester's own dump (Pokopia, Splatoon Raiders): the logs say how far.
- **A side of its own** in the carousel, once a game plays: the Switch's Eden with Switch 2 games
  only, its own settings page and in-game menu, as every system has.
- **Its own words in the library** (an "experimental" warning on the game's page), in every language.
- **eden-nx2's macro compiler** with the console's executable memory (Nightshade's next step), and
  sparse images in the console's RADV for the largest textures.

### Lighter

- **Eden's `ui/` folder** (about 26 MB, 62 files in 5.0.1's ZIP): **done**, none of it is packaged now.
  Only ProsperoEden's own launcher reads it (`frontend.cpp`: its font, art, sounds and translations);
  PS5-ECAM's loading screen and in-game menu take PS5-ECAM's fonts, Eden's own overlay text is baked
  into its shaders, and the keyboard is the PS5's. That launcher was still reached by a start of
  `eden/eboot.bin` without PS5-ECAM's launch request (unreadable without filesystem access, or the
  crash restart when PS5-ECAM's eboot is refused): it goes back to PS5-ECAM now, which says why
  (`patches/eden/0015`), rather than to a launcher with no font. An update from 5.0.1 moves `eden/ui`
  into the kept release, for Go back. To check on a console: a Switch game, its in-game menu and its
  keyboard; a game whose file is gone (back to PS5-ECAM, as before); an update from 5.0.1 (`eden/ui is
  no longer the app's` in the boot log), then Go back to 5.0.1.
- **`sce_sys/pic1.dds`** is a copy of `pic0.dds` (8 MB): check whether the console needs it.
- **Package size in the README** for the next release, and its notes.

### Cleaner code

- `shell_internal.h` is still one class for the whole launcher, and `shell/settings.cpp` about 2,700
  lines: split the settings pages into their own files, and the Library's, the hub's and the pages'
  state out of the class.
- Host tests for what was checked by hand: `ps5net::Resolve`'s cache; run the preview's scripts in CI
  (they need lavapipe). The catalogue's round trip (archive fields, play time), the update's unpacking
  and the DS screens' places have theirs now (`test-catalog.sh`, `test-update.sh`, `test-screens.sh`).

## Next: a folder for each emulator

A release of its own, once the rebuild has shipped and run on consoles, so that a known-good version
is there underneath it. **Effort:** M, most of it the paths saved in files and testing on each kind of
drive.

### Where it stands

The paths are in the code now, on this branch, with a layout that differs from the plan below: the
four folders sit beside `/data/ps5cemu` rather than inside a new `/data/PS5-ECAM`, and the
launcher's own files stay in `/data/ps5cemu`.

- **Done**, by JMUtechnologies (`3cb8349`, his patch; he is in the README's contributors):
  `/data/cemu`, `/data/azahar`, `/data/melonds` and `/data/eden` for a new install, each with
  its own games, updates, DLC, keys and firmware or BIOS folders (`port/app/system_layout.h`,
  `ps5paths::DecideSystemRoots`, the `[paths]` line in the boot log). An install from before is found
  where it is and nothing moves. Eden's program picks its folder with the same code
  (`patches/eden/0010`), so the launch request, menu and saves agree. Cemu's saved MLC path, the old
  default game folders and Eden's `game_files` follow the folder in use. Backups keep their format.
  On an app drive, `cemu`, `azahar` and `melonds` go beside `PS5CEMU-HAR-data`.
- **Done: Reorganize, in the app** (`port/app/reorganize.h`), as planned below but into the folders
  beside `ps5cemu`: a prompt at a fresh start (**Reorganize** or **Later**, asked again a week on) and
  **Settings > Game files > A folder for each system**. The moves are renames made as the app starts
  again, before anything opens the files; everything is checked first (a file in the way, another
  drive), `reorganize.txt` lets the start after a power cut finish, and a system whose move fails goes
  back whole. The same row puts them back, and **Go back** waits for that. Old paths follow: the games
  folder settings, Cemu's game folders and MLC, and home screen tiles (`ps5paths::Moved`).
- **Also there:** `tools/separate-systems.py`, which copies instead, on a computer, with a full backup
  checked by hashes first; the app takes its copies by the same markers.
- **Still to do:** the `PS5-ECAM` names below (the data folder, `PS5CEMU-HAR-data`, `ps5cemu.json`,
  the macros). **Go back** is held back after Reorganize even to a version that has the folders too:
  once one is out, compare versions there. Whether the folders stay beside `ps5cemu` or move inside a
  `/data/PS5-ECAM` is still the owner's choice.
- **Checked on a PC only:** host tests (`test-reorganize.sh`, `test-paths.sh`, `test-saves-paths.sh`,
  `test-eden-paths.sh`, `test-separate-systems.py`), the launcher preview's prompt and row, and a
  compile of every port source and Eden's changed files with the PS5 build's flags. To check on a
  console: Reorganize on `/data`, on a USB drive and on extended storage; Later, then the Settings row;
  a tile made before; Put back, then Go back; a power cut halfway (pull the plug during the start).

### What changes

The data folder is named after the app, and each emulator's files are in a folder named after it.
Today Cemu's lie at the top of `/data/ps5cemu`, among the launcher's:

```
/data/PS5-ECAM/
  cemu/      settings.xml, mlc01/ (installed games and saves), games/, keys.txt, graphicPacks/,
             controllerProfiles/
  azahar/    as now
  melonds/   as now
  eden/      as now
  launcher/  its settings (ps5cemu.json now), library.json, covers/, themes/, music/, network shares
  amiibo/  cache/  logs/      shared by more than one side
```

The other old names go too: `PS5CEMU-HAR-data` on a USB drive or extended storage, `ps5cemu.json`, and
in the code the `PS5CEMU_` macros and the `ps5cemu` names.

**What stays:** the app's title ID, `PPSA99360`. Every home screen tile starts the app by it, so a new
one would break them all, and a title ID can only be four letters and five digits anyway. The name on
the home screen is already PS5-ECAM. With the same app, an update needs no new install and leaves no
old app to delete: the in-app update replaces the files in place as it does now.

### How it goes

1. The update installs as any other.
2. At the first start of the new version, a prompt explains the change: **Reorganize** or **Not now**.
3. **Reorganize** moves the folders. They are on the same drive, so each move is a rename, not a copy:
   about 20 of them, under a second whatever the data's size (300 GB of Wii U games moves as fast as
   one file), with no free space needed. It works the same for data on a USB drive or extended storage.
4. **Not now** keeps the old layout working, and asks again later; Settings has the same button.

### What needs care

- **Paths saved in files:** Cemu's `settings.xml` (its MLC path and game folders), Azahar's config,
  Eden's `prosperoeden.json` (`game_files`), the launcher's `library.json`, and the caches keyed by path
  (`archives.bin`, `cache/switch-games.txt`, Eden's `covers/`). Each is rewritten from the old place to
  the new. `ps5paths::Relocated` (`port/app/paths.h`) already does this for data moved to a USB drive
  (#52): the same idea, for more folders.
- **Home screen tiles:** each one stores its game's full path (`--rom`, `port/app/forwarders.h`). A
  game in `/data/ps5cemu/games` would no longer be there, so the new version translates an old path when
  a tile starts it: the tiles keep working, and nobody remakes them.
- **Eden's program has its folder built in:** `kDataDir` is `/data/ps5cemu/eden` in
  `patches/eden/0003`. It changes there, and Eden is rebuilt (`tools/build-eden.sh`) and shipped in the
  same update.
- **Go back to the previous version** (Settings > Online, `ps5update::GoBack`): the release before would
  look for `/data/ps5cemu`, find nothing and start as if new (nothing lost, but it looks lost). Go back
  moves the folders back first, or is not offered once they have moved.
- **A power cut halfway through:** each move is written down before it is made, so the next start
  finishes the reorganizing or puts everything back.
- **Backups** (`port/app/saves.h`) are stored by system, not by folder, so a backup from before
  restores into the new places; only its `settings/` names the old `ps5cemu.json`.
- **Saves** live inside these folders and move with them: nothing to convert.
- **To check on a console:** reorganizing on `/data`, on a USB drive and on extended storage; tiles made
  before; Go back after reorganizing; Not now, then Reorganize from Settings; an update from the
  release before straight to this one.

## Ideas

A list of things PS5-ECAM could do next: fixes, quality of life, new features and a few long shots.
Written 22 October 2026, against the code at `184f41f` (5.0.1 in progress). Nothing here is planned or
promised; it is a list to pick from.

Each idea was checked against the code first, so none of them is already in the app (what was checked
and left out is at the end). Where an idea comes from another emulator or launcher, it says which.

**Effort:** S is a day or two, M about a week, L more than that or needs research first.

### Progress

Each idea done is marked **Done** where it is listed below, with its commit.

| Idea | Commit | What it changed |
|---|---|---|
| [S1](#s1) | [`bfb579e`](https://github.com/premohq/PS5-ECAM/commit/bfb579e) | An unreadable `ps5cemu.json` is kept as `ps5cemu.json.broken`, and the launcher says so once |
| [S6](#s6) | [`5b870ea`](https://github.com/premohq/PS5-ECAM/commit/5b870ea) | **Back up to USB** takes the Switch's saves and Eden's user profiles, and restores them |
| [S5](#s5) | [`f5fe945`](https://github.com/premohq/PS5-ECAM/commit/f5fe945) | GitHub Actions runs the string table check and the PC tests at every push and pull request |
| [G1](#g1) | [`d70b44a`](https://github.com/premohq/PS5-ECAM/commit/d70b44a) | Touchpad click + Square saves a state, + Triangle held loads it (3DS and DS) |
| [P1](#p1) | [`fdec151`](https://github.com/premohq/PS5-ECAM/commit/fdec151) | **Add files to the PS5** on the remote touch screen's page: games, Switch keys and firmware, amiibo and music from a phone's browser |
| [S2](#s2) | [`431f28c`](https://github.com/premohq/PS5-ECAM/commit/431f28c) | The start after a crash says so once, with the crash's first line and Copy logs to USB |
| [G9](#g9) | [`7224443`](https://github.com/premohq/PS5-ECAM/commit/7224443) | **Fast forward, on and off** in Controls > Buttons (3DS and DS) |
| [G4](#g4) | [`18838c8`](https://github.com/premohq/PS5-ECAM/commit/18838c8) | **Undo the last load** in the in-game menu's Save states (3DS and DS) |
| [T1](#t1) | [`f76c8ca`](https://github.com/premohq/PS5-ECAM/commit/f76c8ca) | The 3DS's cameras see the newest picture in `azahar/camera` (or sent from a phone), for QR codes |
| [L7](#l7) | [`1e1515c`](https://github.com/premohq/PS5-ECAM/commit/1e1515c) | The launcher dims after five minutes with no button pressed; the waking press does nothing else |
| [S7](#s7) | [`25e7f57`](https://github.com/premohq/PS5-ECAM/commit/25e7f57) | Backups hold the settings too (each game's own, Cemu's, controller profiles, themes); a restore starts the app again to use them |
| [S8](#s8) | [`39c2657`](https://github.com/premohq/PS5-ECAM/commit/39c2657) | An update keeps the version it replaces; **Go back to** in Settings > Online; **Skip this version** on the prompt |
| [I4](#i4) | [`507f825`](https://github.com/premohq/PS5-ECAM/commit/507f825) | **3D for red and cyan glasses** in the 3DS side's Graphics (and each game's) |
| [G3](#g3) | [`074a280`](https://github.com/premohq/PS5-ECAM/commit/074a280) | Quit to the library saves a state; **Resume where you left off** in the game's Options menu (3DS and DS) |
| [N2](#n2) | `2aef5d4` | A Switch game's **Mods** and **Cheats**, each on or off, and **Picture effects** (JMUtechnologies, #86) |
| [N4](#n4) | `2aef5d4` | A Switch game's **Saves**: its backup and restore, to a USB drive and back, Ryujinx's too; **Players** (JMUtechnologies, #86) |
| [D1](#d1) | `2aef5d4`, `9f7fb68` | **A GBA cartridge in Slot-2** for the DS, off by default; the DS's microphone from a recording, and touch calibration (JMUtechnologies, #86) |
| [L2](#l2) | [`7d7a7ca`](https://github.com/premohq/PS5-ECAM/commit/7d7a7ca) | **Hide from the Library** in a game's Options menu; Search and a **Hidden** filter still find it |
| [L3](#l3) | [`7d7a7ca`](https://github.com/premohq/PS5-ECAM/commit/7d7a7ca) | **Surprise me**, Sort's last choice: a game of the list in focus, by chance |
| [L9](#l9) | [`d40e252`](https://github.com/premohq/PS5-ECAM/commit/d40e252) | **Settings > Play time**: each side's time, all of them together, and the ten most played games |
| [G6](#g6) | [`76ec5d8`](https://github.com/premohq/PS5-ECAM/commit/76ec5d8) | **The right stick as the stylus** on the DS (Settings > System), off by default, for Metroid Prime Hunters |
| [H3](#h3) | [`5226870`](https://github.com/premohq/PS5-ECAM/commit/5226870) | In a Wii U game, each player's light bar in the PS5's colour for their number |

Being worked on separately (not here): the Switch side's in-game menu, its settings and button
remapping ([N1](#n1)), its rooms ([I2](#i2)), ReShade on the Wii U side ([I1](#i1)), and the box art
downloader.

### Start here

The ten with the most value for the work, in order:

1. [S1](#s1) A broken `ps5cemu.json` is kept, not wiped
2. [N1](#n1) Buttons and controllers for the Switch side
3. [S2](#s2) Say so when the last session crashed
4. [G1](#g1) Quick save and quick load hotkeys
5. [G3](#g3) Resume where you left off
6. [S6](#s6) Switch saves in the USB backups
7. [P1](#p1) Add games, keys and saves from a phone or computer's browser
8. [N2](#n2) Mods and cheats for Switch games
9. [S5](#s5) CI on every push
10. [I1](#i1) Turn on the ReShade code that is already in the tree

### Stability and fixes

<a id="s1"></a>**S1. A broken `ps5cemu.json` is kept, not wiped** (S) · **Done:** [`bfb579e`](https://github.com/premohq/PS5-ECAM/commit/bfb579e)
`ps5settings::Load` returns the defaults when the file does not parse (`port/frontend/settings.cpp:200`),
and the next `Save` writes over it. Many settings are "only in ps5cemu.json", so people do edit it by
hand, and one missing comma then loses every game's own settings, the accounts by PS5 user, the theme
and the rest. Move it to `ps5cemu.json.broken`, start on the defaults, and say so once.

<a id="s2"></a>**S2. Say so when the last session crashed** (S to M) · **Done:** [`431f28c`](https://github.com/premohq/PS5-ECAM/commit/431f28c), without the QR code for now
`ps5/crash.cpp` writes `[crash]` lines to the boot log, but the next start says nothing. When
`boot.prev.log` ends in a crash, offer **Copy logs to USB** and a QR code (`ui/qr.cpp` makes them already)
for a GitHub issue filled in with the version, firmware, HEN, game and the crash's first lines. Dolphin
and PCSX2 both tell you after a crash; most reports here need the logs anyway.

<a id="s3"></a>**S3. A safe start for a game that keeps crashing** (M)
After two crashes in a row with the same game, offer to start it once with nothing extra: no graphic
packs or mods, no cheats, the side's default settings and the experiments off. Several closed issues were
this: #15 (Batman's 60 FPS pack), #20 (3DS cheats), #24 (a graphic pack with `content/`).

<a id="s4"></a>**S4. Close out the A/B experiments** (M)
`port/frontend/settings.h` still has flags "on in 4.0.0's test builds, for the A/B that decides it"
(`idleSpinUs`, `schedulerSpinUs`, `idleEventsUs`, `isolateCores`, `tscClocks`, `driverClockTsc`,
`asyncSubmit`, `gpuWaitSleepUs`) and others off "until a console has measured it" (`fastTls`,
`heapThreadCache`). A hidden **Diagnostics > Benchmark** could play a game for a minute each way and
write a `[bench]` summary from the `[perf]` lines, so Discord testers can send numbers. Then settle the
defaults and delete the paths that lost.

<a id="s5"></a>**S5. CI on every push** (S to M) · **Done:** [`f5fe945`](https://github.com/premohq/PS5-ECAM/commit/f5fe945)
`.github` has only the issue templates. A GitHub Actions job on Ubuntu 24.04 could run what already runs
on a PC: `tools/tests` (archives, keyboard, music, netfs, stream), `tools/reshade-check.sh`,
`tools/lang.py check` and the launcher preview's screenshots. The PS5 build itself can stay manual.

<a id="s6"></a>**S6. Switch saves in the USB backups** (S) · **Done:** [`5b870ea`](https://github.com/premohq/PS5-ECAM/commit/5b870ea)
**Back up to USB** (`port/app/saves.cpp`) takes the Wii U's, 3DS's and DS's saves, but not Eden's
(`eden/user/nand/user/save`). The guide says it does not "yet".

<a id="s7"></a>**S7. Settings in the backups too** (S) · **Done:** [`25e7f57`](https://github.com/premohq/PS5-ECAM/commit/25e7f57)
Add `ps5cemu.json` (each game's own settings), Cemu's `controllerProfiles/` and `themes/` to a backup, so
a reinstall or a second PS5 starts where you were. They are small.

<a id="s8"></a>**S8. Updates you can undo** (M) · **Done:** [`39c2657`](https://github.com/premohq/PS5-ECAM/commit/39c2657), all but the release notes before installing
`port/app/updates.cpp` moves the new files over the old ones. Keep what it replaces in a `previous/`
folder and offer **Settings > Online > Go back to 5.0.0**. While there: **Skip this version**, and the
release notes shown before **Update now** (What's new only shows after). Dolphin has update tracks for
the same reason.

<a id="s9"></a>**S9. Pause when the PS menu opens** (M)
Nothing checks the PS5's system state (no `sceSystemServiceGetStatus` anywhere), so a game runs on under
the PS menu and into rest mode. At least: pause 3DS and DS games and make an automatic save state
([G3](#g3)) when the PS menu comes up or the console goes to rest mode, as most PS5 games pause there.

<a id="s10"></a>**S10. A real pause behind the Wii U and Switch menus** (L)
A known issue in the guide: a Wii U game runs on behind its in-game menu, and now a Switch game does too.
Cemu has no pause, so its cores would have to be held at the scheduler. On the Switch side, find out first
why ProsperoEden's port keeps `system.Pause()` out.

<a id="s11"></a>**S11. A compatibility list for every side, kept fresh** (S to M)
`port/app/compatibility.cpp` reads two tables, the Wii U's and the 3DS's; the DS and Switch have none.
Fetch the list at start (as the box art is) so it changes without an app update, and after a game ends,
offer a QR code for a "how it ran" report. RPCS3 shows its compatibility list in its game list the same
way.

### Quality of life: the launcher and library

<a id="l1"></a>**L1. Your own collections** (M)
The Library's filters are All, Recently added, Favourites and Graphic packs on. Add named collections
("Couch co-op", "Finish next") and ones made from GameTDB's genre and players, which the app already has
(`port/app/gameinfo.h`). ES-DE has custom collections and Daijishou has playlists.

<a id="l2"></a>**L2. Hide a game** (S) · **Done:** [`7d7a7ca`](https://github.com/premohq/PS5-ECAM/commit/7d7a7ca)
For demos, homebrew and the clutter of a big folder. The game menu has no way to hide one. ES-DE has a
hidden flag.

<a id="l3"></a>**L3. Surprise me** (S) · **Done:** [`7d7a7ca`](https://github.com/premohq/PS5-ECAM/commit/7d7a7ca), from the list in view (no collections yet)
A random game from the side (or a collection). ES-DE and Daijishou both have one.

<a id="l4"></a>**L4. More cover styles** (S to M)
`port/app/boxart.cpp` only fetches GameTDB's 2D `coverHQ` and `cover`. GameTDB also has 3D boxes, discs
and cartridges, and full covers (front, spine and back). A theme or a setting could use them.

<a id="l5"></a>**L5. Game descriptions in your language** (M)
The app carries GameTDB's English databases only (`port/app/gameinfo.h`). GameTDB has German, French,
Spanish, Italian, Japanese and others. Download the one for the menus' language when it is chosen, to
keep the app's ZIP small.

<a id="l6"></a>**L6. Delete and uninstall from the game menu** (S to M) · **Done** for installed updates and DLC: the 3DS's (#80) and the Wii U's (`ps5emu::RemoveInstalled`); deleting a game not yet
The game menu has no delete. An installed 3DS CIA (and its update and DLC), or a Wii U update in
`mlc01`, can only be removed by FTP today (#58 was about that). Hold Cross to delete, with the saves kept
unless asked.

<a id="l7"></a>**L7. A screensaver for OLED TVs** (S) · **Done:** [`1e1515c`](https://github.com/premohq/PS5-ECAM/commit/1e1515c), as a dim (no slideshow yet)
Nothing happens when the launcher sits idle. After a few minutes, dim it or show covers slowly. Midnight
is made for OLED screens, but a still launcher burns in all the same. ES-DE has dim, slideshow and video
screensavers.

<a id="l8"></a>**L8. The time and the DualSense's battery in a corner** (S) · **Half done:** the time is in the bar's corner; for the battery, [`821a6e3`](https://github.com/premohq/PS5-ECAM/commit/821a6e3) logs the pad state's undescribed bytes, to find it on a console
As the PS5's home screen and Daijishou show them.

<a id="l9"></a>**L9. Play time you can look at** (S) · **Done:** [`d40e252`](https://github.com/premohq/PS5-ECAM/commit/d40e252), without this week's (the catalogue keeps totals only)
Each game's minutes are counted already (`port/app/catalog.h`). A page with totals by side, the most
played, and this week's, as Playnite and Steam have.

<a id="l10"></a>**L10. Saves for each PS5 user on the 3DS, DS and Switch** (M)
The Wii U side can give each PS5 user a Wii U account of their own (`accountPerUser`). The other sides
share one set of saves. Eden has user profiles of its own to map them to.

<a id="l11"></a>**L11. Who's player 2** (S to M)
On the Wii U, players 2 to 4 are the other signed-in users' DualSenses, with no way to choose who is
who. A sheet before
a game (or in the in-game menu) where each player presses Cross to take their place, as the Switch's
"Change grip/order" does. It matters for Nintendo Land and Mario Party 10.

<a id="l12"></a>**L12. Get themes** (M)
Themes are JSON files in `/data/ps5cemu/themes`. A page that lists a community themes repository on
GitHub and downloads one, as ES-DE's theme downloader does.

### Quality of life: in a game

<a id="g1"></a>**G1. Quick save and quick load hotkeys** (S) · **Done:** [`d70b44a`](https://github.com/premohq/PS5-ECAM/commit/d70b44a)
The guide's shortcuts are touchpad click + Options, L1 and R1; Square and Triangle look free. Touchpad click + Square
saves to the current slot and + Triangle loads it (held, as loading in the menu is), on the 3DS and DS.
RetroArch, DuckStation and DraStic all have this.

<a id="g2"></a>**G2. Pictures and times on the save state slots** (M)
The slots show a number and whether they are empty. A small picture of the game and when it was made, as
RetroArch, Delta and DuckStation show them. The frame can be read as the remote screen reads it.

<a id="g3"></a>**G3. Resume where you left off** (M) · **Done:** [`074a280`](https://github.com/premohq/PS5-ECAM/commit/074a280)
On **Quit to the library**, a 3DS or DS game makes an automatic save state, and its page offers
**Resume** beside **Play**. RetroArch's auto save state and Delta's resume work this way.

<a id="g4"></a>**G4. Undo the last load** (S) · **Done:** [`18838c8`](https://github.com/premohq/PS5-ECAM/commit/18838c8)
Keep the state from just before a load, so loading the wrong slot is not the end of an hour. RetroArch
and DuckStation have "Undo load state".

<a id="g5"></a>**G5. Touch hotspots** (M)
A DualSense button that touches one fixed point of the touch screen: a game's map, its item menu, its
"Y" button on the bottom screen. Citra and Azahar call it "touch from button".

<a id="g6"></a>**G6. The right stick as the stylus** (S to M) · **Done:** [`76ec5d8`](https://github.com/premohq/PS5-ECAM/commit/76ec5d8), on the DS (not the 3DS yet)
On the DS the right stick can only be given to buttons (`port/melonds/input.cpp`). As a stylus it makes
Metroid Prime Hunters playable, and on the 3DS it does the same for Kid Icarus: Uprising. DraStic does
this.

<a id="g7"></a>**G7. Buttons for each game** (M)
A game can have its own picture, sound and speed (`GameSettingNames`), but not its own buttons, turbo or
A/B. Dolphin and Cemu on a PC both have per-game controller profiles.

<a id="g8"></a>**G8. Screenshots** (M)
A quick action or a hotkey that saves a PNG to `/data/ps5cemu/screenshots/<game>/`, a gallery on the
game's page, and one picked as the game's background.

<a id="g9"></a>**G9. Fast forward as a toggle** (S) · **Done:** [`7224443`](https://github.com/premohq/PS5-ECAM/commit/7224443)
Today it works only while held. An option to press once for on and again for off, as RetroArch offers.

### The Switch side

Since ProsperoEden's launcher is no longer opened from the app (`1579f25`), these went with it.

<a id="n0"></a>**N0. Games on a network share** (M, #101)
The other sides play from an SMB or NFS share because their emulators run in the app, whose file
calls go through `port/ps5/netfs_files.cpp`. Eden is a program of its own, so a `/net/...` game is out
of its reach: the Switch side's folder picker no longer offers shares, and starting such a game says
to copy it to the PS5 or a USB drive. To play from a share: link the same netfs layer into Eden's
program (`headless/ecam.cmake` already builds PS5-ECAM sources into it), or have the launcher copy the
game to the PS5 before Eden starts, with its progress shown. Either needs a console to try on.

<a id="n1"></a>**N1. Buttons and controllers** (M)
The Switch side's **Controls** page has only Vibration (`port/frontend/shell/settings.cpp`, the
`!IsSwitch()` checks). Needed: the button map, A/B on Circle or Cross like the other sides, the
controller type (Pro Controller, Joy-Con pair, handheld), and players 2 to 4.

<a id="n2"></a>**N2. Mods and cheats** (M) · **Done:** `2aef5d4` (JMUtechnologies, #86), with Eden's ReShade looks
`/data/ps5cemu/eden/mods` is there, but nothing in the app lists or turns on a mod or a cheat now. A
game's menu could list them with a switch each, as the Add-ons tab of Eden on a PC does.

<a id="n3"></a>**N3. Updates and DLC, shown** (S to M) · **Done** for the Switch (`port/eden/addons.h`): the update's version as the game shows it and the DLC count, on its page and in Recently played
The library pairs a game with the update and DLC beside it, but does not show it. Show which version
will run and which DLC is there, on the game's page.

<a id="n4"></a>**N4. Saves on the Switch side** (S) · **Done:** `2aef5d4` (JMUtechnologies, #86), but for JKSV's and Checkpoint's layouts
The game menu leaves **Saves** out for Switch games (`OpenGameMenu`, `!IsSwitch()`). Back up and restore
one game's save, and take one from a JKSV or Checkpoint dump made on a real Switch.

<a id="n5"></a>**N5. Switch games from a network share** (L)
Eden cannot read shares, so the Switch side refuses them. Either Eden reads through the app's `/net`
layer (`port/ps5/netfs_files.cpp`), or the share's game gets a **Copy to the PS5** action.

### New features for each system

<a id="w1"></a>**W1. Wii U online with Pretendo** (L)
Cemu on a PC plays online through Pretendo Network with your own Wii U's `otp.bin`, `seeprom.bin` and
account files: Mario Kart 8, Splatoon, Smash. Nothing in the app wires it up. Needs a check of Cemu's
network and TLS paths on the console.

<a id="w2"></a>**W2. Import saves from USB** (S to M)
The guide explains how to copy saves from Cemu or Citra on a PC by FTP, path by path. A game's **Saves**
could import them from a USB drive itself, in the PC emulators' layouts, SaveMii's for the Wii U and
Checkpoint's for the 3DS. Apollo Save Tool does this on the PS4.

<a id="d1"></a>**D1. The GBA slot** (M) · **Done:** `2aef5d4`, `9f7fb68` (JMUtechnologies, #86)
melonDS can put a GBA game and its save in the DS's slot 2: Pokémon's Pal Park, and the DS games that
read a GBA cartridge for extras. The guide lists the GBA slot as not there.

<a id="d2"></a>**D2. DSi games** (L)
melonDS plays DSi games with a DSi's own BIOS, firmware and NAND. The app refuses them today
(`port/melonds/core.cpp`: "DSi games are not supported yet").

<a id="d3"></a>**D3. DS Wi-Fi through Wiimmfi** (L)
melonDS emulates the DS's wireless, and Wiimmfi still runs Nintendo WFC for Pokémon's GTS and Mario
Kart DS. The guide lists the DS's wireless as not there.

<a id="d4"></a>**D4. The DS's LCD look** (S to M)
The DS's screen filters are Sharp, Smooth and Square pixels. Add an LCD grid, the DS screen's own
colours, and frame blending for games that flicker sprites to fake transparency, as RetroArch's handheld
shaders do.

<a id="f1"></a>**F1. Lossless Scaling's frame generation on the other sides** (L, #99)
The Switch side generates frames with the player's own `Lossless.dll` (#81), else MihawkGen; the 3DS,
DS and Wii U sides have MihawkGen alone (`port/cemu/FrameGenPS5.cpp`, Cemu patch 0042, Azahar patch
0033). Eden's LSFG is a module of its own (`video_core/frame_gen/lossless_dll.h`, which reads the DLL's
shaders, and `renderer_vulkan/present/frame_gen.cpp`, the passes), GPL-3.0 as the app is: built into the
app once, beside MihawkGen in `FrameGenPS5`, the Vulkan sides would choose it as the Switch does
(`Lossless.dll` where the Switch side reads it). The request's order: the 3DS first, then the DS (which
needs D5's Vulkan picture of its screens to have frames to give it), the Wii U last.

<a id="d5"></a>**D5. The DS's 3D above its own resolution** (L, #100)
Mario Kart DS and the other 3D games drawn at 2x to 4x, chosen as the 3DS side's internal resolution
is (the request asks for that same selector). The DS side draws its 3D with melonDS's software renderer
(`port/melonds/core.cpp`, `SoftRenderer`), which has the DS's 256x192 alone. melonDS scales its 3D only
in its OpenGL renderer (`GLRenderer`, its scale factor) or its compute renderer (OpenGL 4.3): either
needs an OpenGL context on the console beside the screens' Vulkan picture (`port/melonds/screens.cpp`),
and its frames handed over to it, as Eden's OpenGL build does with the PS5's OpenGL SDK. To measure on a
console first: the software renderer is what runs everything today.

<a id="t1"></a>**T1. The 3DS camera from a picture** (M) · **Done:** [`f76c8ca`](https://github.com/premohq/PS5-ECAM/commit/f76c8ca)
Azahar's cameras are set to `blank` (`port/azahar/core.cpp:429`). Azahar can show a still image as the
camera instead: a picture from USB, or one sent from the phone page ([P1](#p1)), for the QR codes
of Pokémon Sun and Moon, Miis, and Animal Crossing's designs.

<a id="t2"></a>**T2. RetroAchievements hardcore mode** (M)
The DS's achievements are softcore only (`port/app/achievements.h`). Hardcore turns off states, rewind,
cheats and slow speeds, and adds leaderboards. Rich presence, and the badges' pictures in the menu, can
come with it. RetroArch, DuckStation and PCSX2 all offer it.

### The DualSense

<a id="h1"></a>**H1. The DualSense's microphone as the game's** (L, needs a test first)
The DS side has "R3 held to blow". The DualSense has a microphone: Nintendogs, Spirit Tracks' pan
flute, and the 3DS's and GamePad's microphones. First check that a HEN lets homebrew open it.

<a id="h2"></a>**H2. Adaptive triggers for ZL and ZR** (S to M)
A light stop halfway down, or resistance, set for each side or game, for racing games and shooters.
Check first that the payload SDK reaches `scePadSetTriggerEffect`.

<a id="h3"></a>**H3. A light bar that says something** (S) · **Done:** [`5226870`](https://github.com/premohq/PS5-ECAM/commit/5226870), as the player's number on the Wii U (no achievement flash or cover colour yet)
`ui/feedback.cpp` lights it in the side's colour. In a game it could show the player's number (Wii U,
players 1 to 4), flash on an achievement, or take the cover's colour.

### A phone or computer as a companion

The remote touch screen's HTTP server (`port/stream`) already reaches every phone on the network. It can
do more.

<a id="p1"></a>**P1. Add games, keys and saves from a browser** (M) · **Done:** [`fdec151`](https://github.com/premohq/PS5-ECAM/commit/fdec151), without the PIN or saves: the same network switch as the touch screen, and fixed folders
An upload page, behind a PIN shown on the TV, that puts each file where it goes: a game in its side's
folder, `prod.keys` in `eden/keys`, a CIA to install. No FTP client and no USB drive. Provenance on iOS
has a built-in web server for the same thing.

<a id="p2"></a>**P2. A phone as players 2 to 4** (M)
A page of touch buttons as a controller, for Wii U party games with more players than DualSenses, as
AirConsole does.

<a id="p3"></a>**P3. A phone's motion for the GamePad** (S to M)
The GamePad's gyroscope from the phone's own, for aiming in Splatoon from the phone's screen. Browsers
may only give motion over HTTPS, so check that first.

<a id="p4"></a>**P4. The library and the logs on the phone** (S to M)
A page listing your games, and the latest `boot.log` to attach to an issue from the phone. Goes with
[S2](#s2).

### Bigger and stranger

<a id="i1"></a>**I1. Turn on the ReShade code that is already in the tree** (M)
`port/cemu/ReShadeEffects.cpp` and `ReShadeRuntime.cpp`, about 2,200 lines, compile ReShade `.fx`
effects to Vulkan and run them on a picture, and `tools/reshade-check` tests them on a PC, but nothing in
the app calls them. A **Picture effects** page (CRT, sharpening, colour) for the Wii U first, then the
3DS and DS screens, with presets per game. RetroArch's shaders and ReShade on a PC are what people know.

<a id="i2"></a>**I2. Switch rooms over the internet** (L)
Eden emulates the Switch's local wireless through rooms. ProsperoEden's launcher had them (the guide
listed "rooms" until `184f41f`). Hosting and joining a room by a code, from the app, would let two PS5s,
or a PS5 and a PC, play local wireless games together.

<a id="i3"></a>**I3. DS local play between two PS5s** (L)
melonDS 1.0 has local multiplayer over a LAN. Two PS5s on one network as two DSes: Mario Kart DS,
Pokémon trades and battles.

<a id="i4"></a>**I4. The 3DS's 3D** (S to M) · **Done:** [`507f825`](https://github.com/premohq/PS5-ECAM/commit/507f825), red and cyan only (side by side for 3D TVs not yet)
Azahar can draw the 3DS's stereo picture as red and cyan anaglyph, or side by side for a 3D TV. Cheap
glasses, and the 3D slider back.

<a id="i5"></a>**I5. "Where you left off" cards** (M)
With [G2](#g2), [G3](#g3) and [G8](#g8): the launcher's home row shows the last picture of each game,
how long ago, and its play time, and Cross resumes it, like the PS5's own activity cards. The library
would look like your games, not their boxes.

<a id="i6"></a>**I6. Cheats you can download** (M)
3DS cheats need a hand-made `<title ID>.txt` and DS cheats a `.mch`. A **Get cheats** action could fetch
them by title ID from a public cheat collection, or read an R4 `usrcheat.dat` for the DS. RetroArch has a
cheat downloader and Apollo Save Tool has online patches.

### Checked and left out

These came up and are already in the app: play time, turbo buttons, themes and your own themes, favourites
and sorting, rewind and fast forward on the DS, five save state slots on the 3DS and DS, RetroAchievements
on the DS, the community graphic packs' updater, frame generation, FSR 1, home screen tiles, network
shares, games in archives, Wii U accounts for each PS5 user, Skylanders portals and the rest of the USB
devices, the GamePad's sound on the DualSense's speaker, Artic Base and Artic Setup, the remote touch
screen, and atomic writes of the settings and catalog files.

Left out on purpose: Wii U save states (Cemu has none), PS5 trophies (out of reach for homebrew), the
3DS's rewind (Azahar has none, and its states are large), and a real Wii U GamePad (#44, closed as not
planned).
