# Contributing to PS5-ECAM

Thank you for helping. PS5-ECAM has grown fast because people sent code, fixes and console test
reports, and every one of them is welcome. This page is the project's direction and the few habits
that let many people's work fit together as one app. It is written for people and for the AI
assistants they work with alike ([AGENTS.md](AGENTS.md) points assistants here).

If something here does not fit what you want to build, open an issue or ask the owner
([premohq](https://github.com/premohq)) first. A short conversation saves a long pull request.

## The vision: one app, every Nintendo system, one way of working

PS5-ECAM is **one app** that plays every Nintendo system there is an emulator for, the obscure ones
and the arcade boards too ([ROADMAP.md, Every Nintendo system](docs/ROADMAP.md#every-nintendo-system)).
What makes it worth using over a folder of separate homebrew apps is that **every system looks and
works the same**:

- its side in the carousel, its library with box art, its game menu;
- the same in-game menu (Touchpad click + Options): its screens, picture, volume, controls, save
  states, back to the library;
- its settings in PS5-ECAM's Settings, with the same few pages as the other sides;
- its saves where Settings > Saves finds them, its strings in every language.

So the question for any change is: **does this make the systems feel more like one app?**

### How new systems come in: libretro cores behind PS5-ECAM's own UI

- New systems run on **libretro cores linked into the app**, through PS5-ECAM's own libretro
  frontend (`port/libretro`, built by `tools/build-libretro.py`). The core does the emulation; the
  app does everything the player sees.
- A system is **a row in `ps5retro::kSystems`** (`port/retro/registry.h`), its `ps5launcher::System`
  and names (`port/frontend/launcher.h`), and its file types (`HasExtension`). Every count of sides
  comes from that list.
- Each system gets **the best emulator there is for it**: the most accurate, actively maintained
  one, from its **official upstream repository** (the libretro organisation's or the emulator
  project's own), **pinned by commit in `tools/deps.json`**. The roadmap's tables name the first
  choices (Mesen for the NES, Mesen-S/bsnes for the SNES, mGBA, SameBoy, Mupen64Plus-Next with
  ParaLLEl-RDP, Beetle VB, PokeMini...). A personal port or fork is not a substitute for upstream,
  however well it runs on its author's console.
- A second core for a system is welcome **as a choice**, once it is measured to be faster on the
  console.

### The programs bundled beside the app today

The GameCube and Wii (Porpoise, Dolphin) and the N64, NES, Game Boy, Game Boy Color and Game Boy
Advance donor runtimes (PR #87, PR #91: `retro/*/eboot.bin`, `tools/retro/sources.json`) run as
programs of their own, started as Eden is. They were the quickest way to get those systems in, and
they work. They are **transitional**:

- the N64, NES, GB, GBC and GBA move onto the libretro frontend one at a time, as their best core
  runs there; the program it replaces stays a choice until then;
- please **don't add more standalone programs**, and don't build new features around one (its own
  folder layouts, title IDs or settings files): build them in PS5-ECAM's frontend, where every
  system gets them;
- **Porpoise stays** for the GameCube and Wii. Changes to Porpoise itself go to
  [premohq/Porpoise-X](https://github.com/premohq/Porpoise-X), not as patches kept in this repository.

The Wii U (Cemu), 3DS (Azahar), DS (melonDS) and Switch (Eden) sides are the app's own ports and
stay as they are.

### EmulationStation, a frontend over the same systems

The app can open on EmulationStation (ES-DE, `port/esde`) instead of its launcher, for people who like
its menus and themes. It is **a frontend, not another set of emulators**: it lists the launcher's
libraries and every game it starts plays in PS5-ECAM (`port/esde/handoff.h`). A system or a feature is
added to the app, and EmulationStation shows it; nothing is added to EmulationStation alone.

## Working together

- **Start from the newest `PS5-ECAM` branch, and rebase before you open a pull request.** The owner
  works on this branch every day; work on an old base re-does or undoes what changed since. If your
  pull request has been open a while, rebase it before pushing more to it.
- **One topic a pull request**, small enough to read. A rewrite of the launcher's files mixed into a
  feature cannot be reviewed.
- **Check [ROADMAP.md](docs/ROADMAP.md) first.** If what you want is there, it says how it is meant
  to go. If it is not, open an issue before a large change or a new system.
- **Match the code around you**: its tabs, its naming, its comments' voice. Read a few neighbouring
  functions before writing one.

## The habits the code relies on

- **Side counts and indices come from `ps5retro::kSystems`.** Never write 11, 7, `side == 10` or an
  array of a fixed number of sides; use `ps5launcher::kSideCount`, `ps5retro::kFirst`,
  `ps5retro::IsRetro`, `ps5retro::Of(side).key`.
- **Files through `ps5files::OwnStatus`** (`port/app/own_status.h`), never `symlink_status` or
  `lstat` alone: the console's `/data` refuses `lstat` to an app without a HEN. Host tests run with
  it refused (`tools/tests/refuse-lstat.c`).
- **Settings stay few.** Settings was cut down to fewer pages with related rows behind one row; add a
  row where it belongs, not to every side's page.
- **Every string a player reads goes through `Tr`, `TrF` or `TrP`**, then
  `python3 -B tools/lang.py update`, with its translation in every `port/lang/*.po`, and
  `python3 -B tools/lang.py check` passing.
- **Performance changes stay off until measured on a console**, and per-game profiles need the
  measurement that justifies them.
- **No personal setup in the repository**: no LAN addresses, local paths or scripts for one machine,
  and no generated reports, audits or logs as documents.

## Testing and honesty about it

- Run the host tests for what you touched (`.github/workflows/tests.yml` lists them all; most are
  `bash tools/tests/test-*.sh`) and keep CI green.
- Say exactly what was tested and where: "builds", "host tests pass", "ran on my PS5 (firmware
  x.xx): game, what worked, what did not". A build passing is not a game running; never write that a
  test passed that was not run.
- A pull request with a console test report is the most useful kind there is. Thank you for those.

## Commits

Commit messages follow the history's style: `Area: what changed, in plain words`, then a body that
says why. If an assistant helped, say so if you like, but the message is still about the change.
