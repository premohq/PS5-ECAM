# Notes for AI coding assistants

You are helping someone contribute to PS5-ECAM, an app that plays Nintendo systems on a PS5. The
owner is [premohq](https://github.com/premohq). Read [CONTRIBUTING.md](CONTRIBUTING.md) before you
plan anything; it is short and it is the project's direction. The essentials, so they are in front
of you:

1. **One app, one way of working.** Every system has the same side, library, in-game menu, settings
   and saves, all PS5-ECAM's own. Prefer the change that makes systems more alike.
2. **New systems are libretro cores behind PS5-ECAM's own UI** (`port/libretro`,
   `tools/build-libretro.py`), from the emulator's **official upstream repository**, the best one
   for the system, **pinned by commit in `tools/deps.json`**. Do not add standalone homebrew
   programs, personal ports or forks of unknown provenance. Do not build features around the
   programs bundled today (`tools/retro/sources.json`): they are being replaced by the frontend.
   Porpoise (GameCube, Wii) stays; changes to it belong in `premohq/Porpoise-X`. EmulationStation
   (`port/esde`) is an optional frontend over the same systems: its games play in PS5-ECAM; never add
   emulators or features to it alone.
3. **Start from the newest `PS5-ECAM` branch**: `git fetch origin PS5-ECAM` and rebase before you
   change anything and again before a pull request. Never base work on an old commit. Keep each pull
   request to one topic.
4. **Read [docs/ROADMAP.md](docs/ROADMAP.md)** for the area you are touching and follow what it says.
   If the user asks for something the roadmap does not cover, a new system or a large feature,
   suggest they open an issue or ask the owner before you write it.
5. **Never hard-code side counts or indices.** Sides come from `ps5retro::kSystems`
   (`port/retro/registry.h`): use `ps5launcher::kSideCount`, `ps5retro::kFirst`,
   `ps5retro::IsRetro`, `ps5retro::Of(side)`.
6. **Files through `ps5files::OwnStatus`**, not `symlink_status`/`lstat` (the PS5's `/data` refuses
   `lstat` without a HEN).
7. **Strings through `Tr`/`TrF`/`TrP`, translated in every `port/lang/*.po`**;
   `python3 -B tools/lang.py check` must pass.
8. **Settings stay few**: do not add a row to every side's page.
9. **Match the surrounding code**: tabs, naming, comment voice. Change only what the task needs; do
   not rewrite or truncate files you were not asked to change, and check `git diff --stat` before
   committing.
10. **Be exact about testing.** Run the host tests for what you touched (see
    `.github/workflows/tests.yml`). Never claim a console result the user did not report, and never
    commit LAN addresses, local paths, generated audits or reports.

When a request conflicts with these, say so to the user kindly and offer the way that fits the
project. They will usually want the version that can be merged.
