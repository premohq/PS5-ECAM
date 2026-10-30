<p align="center">
  <img src="docs/banner.png" alt="PS5-ECAM: the Wii U, Nintendo 3DS, Nintendo DS and Nintendo Switch on PlayStation 5 homebrew, with Cemu, Azahar, melonDS and Eden" width="100%">
</p>

<p align="center">
  <a href="https://github.com/premohq/PS5-ECAM/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/premohq/PS5-ECAM?label=release&color=3b8cff"></a>
  <a href="https://discord.gg/grrqkv4xWw"><img alt="Discord: join the server" src="https://img.shields.io/badge/discord-join%20the%20server-5865F2?logo=discord&logoColor=white"></a>
  <a href="https://ko-fi.com/premohq"><img alt="Ko-fi: support PS5-ECAM" src="https://img.shields.io/badge/ko--fi-support%20PS5--ECAM-FF5E5B?logo=kofi&logoColor=white"></a>
  <img alt="Platform: PS5 homebrew" src="https://img.shields.io/badge/platform-PS5%20homebrew-1f5fd6">
  <img alt="Emulation: Cemu, Azahar, melonDS and Eden" src="https://img.shields.io/badge/emulation-Cemu%20%C2%B7%20Azahar%20%C2%B7%20melonDS%20%C2%B7%20Eden-5cd3ff">
  <img alt="Languages: 31" src="https://img.shields.io/badge/languages-31-62c454">
  <a href="LICENSE"><img alt="License: GPL-3.0-or-later" src="https://img.shields.io/badge/license-GPL--3.0--or--later-a9a6ff"></a>
</p>

<p align="center">
  <b>PS5-ECAM</b> plays your Wii U, Nintendo 3DS, Nintendo DS and Nintendo Switch games on a jailbroken PS5,<br>
  with <a href="https://github.com/cemu-project/Cemu">Cemu</a>, <a href="https://github.com/azahar-emu/azahar">Azahar</a>,
  <a href="https://github.com/melonDS-emu/melonDS">melonDS</a> and <a href="https://github.com/eden-emu/mirror">Eden</a> in one launcher made for the TV and the DualSense.<br>
  Made by its co-founders, <a href="https://github.com/premohq">premohq</a> and <a href="https://github.com/JMUtechnologies">JMUtechnologies</a>. It was called PS5CEMU-HAR until 5.0.0.
</p>

<p align="center">
  <a href="https://github.com/premohq/PS5-ECAM/raw/releases/PS5-ECAM-v5.1.0.zip"><b>Download 5.1.0</b></a> (ZIP) ·
  <a href="https://github.com/premohq/PS5-ECAM/releases/tag/v5.1.0">Release notes</a> ·
  <a href="https://premohq.github.io/PS5-ECAM/">Website</a> ·
  <a href="#install">Install</a> ·
  <a href="docs/GUIDE.md">Guide</a> ·
  <a href="docs/COMPATIBILITY.md">Compatibility</a> ·
  <a href="#credits">Credits</a> ·
  <a href="#support">Support</a>
</p>

---

## What's new in 5.1.0

A folder for each system, everything on a USB drive, and the Switch's updates and DLC wherever they are.
The details are in the [release notes](https://github.com/premohq/PS5-ECAM/releases/tag/v5.1.0).

- **A folder for each system:** `/data/cemu`, `/data/azahar`, `/data/melonds` and `/data/eden`. An install
  from before keeps working where it is, and **Reorganize** moves it in a moment, and back
  ([System folders](docs/SYSTEM-FOLDERS.md)).
- **Everything on your USB drive:** with the app on a drive, every system's files go beside it there, the
  Switch's too (its keys and saves on the console stay in use). An older copy of the app left in
  `/data/homebrew` is no longer started or updated in place of the one you run.
- **Switch:** updates and DLC are found beside a game in any games folder, a USB drive's too, and its page
  shows the update's version and DLC; frame generation with Lossless Scaling and your own `Lossless.dll`
  ([#81](https://github.com/premohq/PS5-ECAM/issues/81)); mods, cheats, players, and saves to USB and back (Ryujinx's too), from JMUtechnologies
  ([#86](https://github.com/premohq/PS5-ECAM/pull/86)); a game with a mods folder starts ([#77](https://github.com/premohq/PS5-ECAM/issues/77)); fixes from ProsperoEden's open pull
  requests (several updates of one game, a full audio buffer, Metal Gear Solid: Master Collection).
- **Updates in a minute** ([#78](https://github.com/premohq/PS5-ECAM/issues/78), [#73](https://github.com/premohq/PS5-ECAM/issues/73), [#72](https://github.com/premohq/PS5-ECAM/issues/72)): Check for updates answers at once, and
  an update installs in about a minute and can be cancelled.
- **Library:** 3DS and DS games come back to it without the app starting over; each game's play time, and
  **Settings > Play time** with each side's time and the most played games; **Hide from the Library** and
  **Surprise me** in a game's Options and Sort; Library and Settings beside the players and the clock; one
  layout, Light by default; graphic packs downloaded when Cemu first needs them, so the app is smaller
  ([#74](https://github.com/premohq/PS5-ECAM/issues/74)); network shares looked through only when you ask ([#75](https://github.com/premohq/PS5-ECAM/issues/75)); Azahar's compressed 3DS
  files with their names and box art.
- **Settings:** fewer pages down the left, and related rows behind one row (›), every setting kept.
- **Remove its update or DLC** from a Wii U ([#58](https://github.com/premohq/PS5-ECAM/issues/58)) or 3DS ([#80](https://github.com/premohq/PS5-ECAM/issues/80)) game's Options menu.
- **DS:** the right stick as the stylus; from [#86](https://github.com/premohq/PS5-ECAM/pull/86), a GBA game in Slot-2 for Pal Park, a recorded microphone, touch calibration.
- **Wii U:** each player's DualSense light bar in a colour of its own. **Switch:** Saves in a game's Options.
- **Fixes:** stripes on empty screens ([#76](https://github.com/premohq/PS5-ECAM/issues/76)); one label size in Cyrillic ([#79](https://github.com/premohq/PS5-ECAM/issues/79)); a renamed
  Wii U account's name is the one games show; a Switch shader the driver refuses no longer crashes the game
  (This War of Mine); the Swedish menus reviewed by yeager ([#83](https://github.com/premohq/PS5-ECAM/pull/83)).

Changes in earlier versions are in their [release notes](https://github.com/premohq/PS5-ECAM/releases).

## Support

PS5-ECAM is free. If it brings your games back, you can support it:

- **Ko-fi:** [ko-fi.com/premohq](https://ko-fi.com/premohq), for a one-off tip or the
  [$3 a month Early Access tier](https://ko-fi.com/premohq/tiers): highly experimental builds, builds of
  ECAM for other platforms, and new features before anyone else.
- **PayPal:** [paypal.me/empthe](https://www.paypal.com/paypalme/empthe), or scan the code (the same one
  as in the app's **Settings > About**):

<a href="https://www.paypal.com/paypalme/empthe"><img src="docs/support-qr.svg" width="120" alt="QR code for paypal.me/empthe"></a>

## Features

| | Wii U | Nintendo 3DS | Nintendo DS | Nintendo Switch |
|---|---|---|---|---|
| **Emulator** | Cemu, x64 recompiler, Vulkan | Azahar, dynarmic recompiler, Vulkan | melonDS, ARM recompiler | Eden (ProsperoEden), dynarmic, Vulkan |
| **Plays** | `.wua`, `.wud`, `.wux`, unpacked games (also in `.zip`, `.7z` and `.rar` archives) | `.3ds`, `.cci`, `.cxi`, `.app`, `.3dsx`, `.elf`, compressed `.z*`, also in `.zip`, `.7z` and `.rar` archives; installed `.cia` | `.nds`, also in `.zip`, `.7z` and `.rar` archives | `.nsp`, `.xci`, each alone or in a folder with its update and DLC |
| **Picture** | 4K output, 120 Hz, FSR 1, graphic packs, ReShade, frame generation | up to 10x resolution, texture filters, layouts, borders, red-cyan 3D, frame generation | sharp, smooth or square pixels, layouts, borders | up to 4x resolution, FSR 1, anti-aliasing, docked or handheld, frame generation |
| **Extras** | mods, accounts, amiibo, Skylanders portals, updates and DLC | save states, cheats, amiibo, mods, cameras, Artic Base | save states, cheats, RetroAchievements | amiibo, online rooms, mods, cheats, updates and DLC (from their folders) |

- **One launcher for all four:** box art and game info from GameTDB, search, sorting and favourites,
  a page for each game, settings for each game, and an in-game menu on every system.
- **Made for the DualSense:** every button remappable, motion controls, rumble, the touchpad as the
  touch screen, and up to four players on the Wii U.
- **Updates itself:** a new release installs from inside the app.

## Install

1. **A PS5 that runs homebrew.** Any firmware up to 13.60, with etaHEN, OnionHEN, or an ELF loader on
   port 9021. See the [firmware and setup guide](docs/HEN-SETUP.md).
2. **Copy the app.** Download the [5.1.0 ZIP](https://github.com/premohq/PS5-ECAM/raw/releases/PS5-ECAM-v5.1.0.zip) ([release notes](https://github.com/premohq/PS5-ECAM/releases/tag/v5.1.0)),
   extract it and copy the `PPSA99360` folder to `/data/homebrew/PPSA99360`.
3. **Let your HEN in.** etaHEN: add `PPSA99360` to its app jailbreak list. OnionHEN: add `PPSA99360`
   at the end of `exact_title_ids` in `config.ini`, with no comma after it. Any other: have an ELF
   loader on port 9021.
4. **Add your games** (below), and start PS5-ECAM from the home screen. The Setup check shows
   what's ready and what to do about the rest.

Each system keeps its files in a folder of its own: `/data/cemu`, `/data/azahar`, `/data/melonds`
and `/data/eden`, with the launcher's settings, covers and logs in `/data/ps5cemu`.

Updating, from PS5CEMU-HAR too: the app offers new releases itself, and the title ID stays
`PPSA99360`. An install from before keeps everything where it is, in `/data/ps5cemu`, and goes on
working. The app offers to give each system its folder (**Reorganize**, or later from **Settings >
Game files**): the files are moved in a moment, never copied or deleted, and can be put back.
[System folders](docs/SYSTEM-FOLDERS.md) has the details.

Installed on a USB drive or the extended storage? A `PS5CEMU-HAR-data` folder at the top of that drive
(its name from before 5.0.0, kept so nothing moves) keeps the launcher's files there instead of in
`/data`, with `cemu`, `azahar`, `melonds` and `eden` beside it (the Switch's stay in `/data/eden` when
they are there already).
[The guide](docs/GUIDE.md#everything-on-the-apps-drive) says how.

## Your games

| | Games folder | Also needs |
|---|---|---|
| **Wii U** | `/data/cemu/games` | disc keys in `/data/cemu/keys.txt` for encrypted `.wud` and `.wux` |
| **3DS** | `/data/azahar/games` | nothing: games must be decrypted dumps; `.cia` files install from Settings |
| **DS** | `/data/melonds/games` | nothing: your own BIOS and firmware in `/data/melonds/bios` are optional |
| **Switch** | `/data/eden/roms` | your Switch's `prod.keys` (and `title.keys`) in `/data/eden/keys`, and its firmware in `/data/eden/firmware` |

An install from before has these in `/data/ps5cemu` (`games`, `keys.txt`, `azahar/games`, and so on)
until it is separated. [System folders](docs/SYSTEM-FOLDERS.md) has each system's whole folder.

Any other folder, a USB drive or a network share (not for Switch games) works too: **Settings > Game
files** on each system. No games, keys or firmware are included. Use dumps of games and hardware you own.

## Controls

| | |
|---|---|
| **L1 / R1** | Library and Settings |
| **Touchpad click** | The next system |
| **Cross / Circle** | Play or open / back |
| **Options** | A game's menu: game settings, saves, graphic packs, mods, home screen tile |
| **Touchpad click + Options** | The in-game menu |
| **Touchpad** | The touch screen (GamePad, 3DS and DS) |

Every shortcut, the in-game menu and the rest are in the [guide](docs/GUIDE.md).

## Documentation

- [Guide](docs/GUIDE.md): the launcher, each system, controls, saves, mods, the remote touch screen and where files are kept
- [Firmware and setup](docs/HEN-SETUP.md) · [Mods](docs/GUIDE.md#mods) · [Compatibility](docs/COMPATIBILITY.md)
- [Building from source](docs/BUILDING.md) · [Driver performance](docs/DRIVER-PERFORMANCE.md) · [Roadmap](docs/ROADMAP.md)
- Problems: **Settings > Diagnostics > Copy logs to USB**, then [open an issue](https://github.com/premohq/PS5-ECAM/issues/new/choose)

## Building

`make release` on Ubuntu 24.04 (WSL works) downloads every dependency at its pinned version, builds
the four emulators (Eden as a program of its own, `eden/eboot.bin`) and the launcher, and writes the
app to `build/app/PPSA99360` and a ZIP to `dist/`. See [docs/BUILDING.md](docs/BUILDING.md).

## Join the Discord

<a href="https://discord.gg/grrqkv4xWw"><img alt="Join the PS5-ECAM Discord server" src="https://img.shields.io/badge/Discord-Join%20the%20server-5865F2?style=for-the-badge&logo=discord&logoColor=white"></a>

The server at [discord.gg/grrqkv4xWw](https://discord.gg/grrqkv4xWw) is the quickest way to get help and to hear about new versions:

- **Help with your setup:** ask in the support forum, one post per problem, tagged by system.
- **What runs, and how well:** tested-games has a post per game with its speed, its glitches and the settings that work.
- **New versions first:** every release is posted in announcements the moment it's published.
- **The developer and other players:** suggest features, share your setup and hear what's coming.

Bugs still go in [GitHub issues](https://github.com/premohq/PS5-ECAM/issues/new/choose), with the log from *Copy logs to USB*.

## Credits

### Founders

- **[premohq](https://github.com/premohq)**, co-founder, who started the app as PS5CEMU-HAR
- **[JMUtechnologies](https://github.com/JMUtechnologies)**, co-founder: a folder for each system (the
  Wii U's, 3DS's, DS's and Switch's files in folders of their own, Eden's side of it, an install from
  before still found, and the PC tool that separates one); the Switch side's mods, cheats, ReShade
  presets and players, and its saves to and from a USB drive, Ryujinx's too; the DS's GBA cartridge in
  Slot-2, its recorded microphone and touch screen calibration; and the tools that compare
  performance captures ([#86](https://github.com/premohq/PS5-ECAM/pull/86), [#84](https://github.com/premohq/PS5-ECAM/pull/84))

### Contributors

Big thanks to the people who sent code and fixes to PS5-ECAM itself. Want to join them? [CONTRIBUTING.md](CONTRIBUTING.md)
says where the app is going and how to fit in.


- **[dolphin78](https://github.com/dolphin78)**, with a massive shoutout: Artic Setup works on the PS5
  because of him ([#70](https://github.com/premohq/PS5-ECAM/pull/70)). He found and fixed why the 3DS's system update failed and why a
  New 3DS's setup crashed, and tested it all on his own PS5 with a New 3DS XL. He also made crash
  reports survive a crash on the 3DS side ([#70](https://github.com/premohq/PS5-ECAM/pull/70)), fixed Artic Base's connections ([#45](https://github.com/premohq/PS5-ECAM/issues/45)), and
  came up with the remote touch screen and its first prototype ([#38](https://github.com/premohq/PS5-ECAM/issues/38))
- **[Mafiacoding](https://github.com/Mafiacoding)** for archived games: the ZIP support for the Wii U,
  3DS and DS sides that 5.0.0's archives build on ([#53](https://github.com/premohq/PS5-ECAM/pull/53), [#50](https://github.com/premohq/PS5-ECAM/issues/50))
- **[3lmerx503](https://github.com/3lmerx503)** for finding and testing the rumble fix ([#41](https://github.com/premohq/PS5-ECAM/issues/41))
- **[yeager](https://github.com/yeager)** (Daniel Nylander) for his review of the Swedish translation
  ([#85](https://github.com/premohq/PS5-ECAM/pull/85))

### Built on

- [Cemu](https://github.com/cemu-project/Cemu) by the Cemu team and contributors (MPL-2.0)
- [Azahar](https://github.com/azahar-emu/azahar) by the Azahar team and the Citra contributors
  before them (GPL-2.0-or-later), with dynarmic by MerryMage and contributors
- [melonDS](https://github.com/melonDS-emu/melonDS) by Arisotura and the melonDS team (GPL-3.0),
  with its FreeBIOS replacements
- [Eden](https://github.com/eden-emu/mirror), the Switch emulator, by the Eden team (GPL-3.0-or-later),
  and [ProsperoEden](https://github.com/Swordpdf/ProsperoEden), its PS5 port and launcher, by **Sword**
  ([Swordpdf](https://github.com/Swordpdf)), **BlackBearReloaded**, **bawsdeep**, **Rockiu0** and its
  contributors (GPL-3.0-or-later; the Switch side runs it with `patches/eden`)
- [eden-nx2](https://git.eden-emu.dev/Takoroka/eden-nx2) by **Takoroka**, Eden with experimental
  support for Switch 2 games (GPL-3.0-or-later), the Switch side's Eden now, and
  [Nightshade](https://github.com/Oliv3rmoon/Nightshade) by **Oliv3rmoon**, whose port of it to the PS5
  the Switch side's follows (`patches/eden` 0021 and 0022)
- **Mihawk** for RADV on PS5 ([PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa),
  [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), the payload SDK fork and its platform layer)
  and the PS5 ports of Azahar and dynarmic ([PS5_Azahar](https://github.com/mihawk-99/PS5_Azahar)),
  with contributions from **mpereiraesaa**
- **BlackBearReloaded** for ProsperoEden, which the launcher's layout, artwork, Eden theme and software
  renderer build on, and the
  PS5 Native App Boilerplate for the app runtime, packaging and sandbox elevation (its repository went
  with their account: [drakmor's copy](https://github.com/drakmor/ps5-native-app-boilerplate) is the
  one the build fetches)
- **heydemoura** for the [PS5 Forwarder Format](https://github.com/heydemoura/ps5-forwarder-format)
  the home screen tiles use
- **John Törnblom** for the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk), and
  **pacbrew** for their PS5 libraries
- [libsmb2](https://github.com/sahlberg/libsmb2) and [libnfs](https://github.com/sahlberg/libnfs) by
  Ronnie Sahlberg for the network shares, and [rcheevos](https://github.com/RetroAchievements/rcheevos)
  by RetroAchievements
- **Swordpdf** for the etaHEN jailbreak request from PS5SX2
- **Dimok** for the Wii U Homebrew Launcher's background, and the authors of the 3DS Homebrew
  Launcher, which inspired the 3DS side's
- The [Lexend](https://github.com/googlefonts/lexend) authors for the launcher's font (SIL OFL 1.1)
- [GameTDB](https://www.gametdb.com/) for box art and game information, and the authors of the
  [Cemu community graphic packs](https://github.com/cemu-project/cemu_graphic_packs)

## License

The app's own code is GPL-3.0-or-later (see [LICENSE](LICENSE)). Files derived from Cemu keep Cemu's
MPL-2.0, as their headers say; Azahar is GPL-2.0-or-later, melonDS GPL-3.0, and Eden and
ProsperoEden GPL-3.0-or-later (their notices are in the app's `eden/` folder), and the app that
includes them is distributed under GPL-3.0-or-later. Other third-party components keep their own
licenses.

PS5-ECAM is an independent homebrew project, not affiliated with or endorsed by Sony Interactive
Entertainment, Nintendo, or the Cemu, Azahar, melonDS, Eden or ProsperoEden projects. "PlayStation"
and "PS5" are trademarks of Sony Interactive Entertainment Inc.; "Wii U", "Nintendo 3DS", "Nintendo
DS" and "Nintendo Switch" are trademarks of Nintendo. No firmware, keys, games or SDK material is included. It is provided as is,
without warranty, for use with hardware and games you own.
