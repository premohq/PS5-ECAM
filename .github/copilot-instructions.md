Read AGENTS.md and CONTRIBUTING.md at the repository root before suggesting changes; they are the
project's direction. In short: PS5-ECAM is one app in which every Nintendo system works the same way.
New systems are libretro cores from their official upstream repositories, pinned in tools/deps.json,
run by the app's own libretro frontend (port/libretro); no new standalone programs or personal forks.
Start from the newest PS5-ECAM branch and rebase before a pull request. Side counts come from
ps5retro::kSystems, never hard-coded. Files through ps5files::OwnStatus. Strings through Tr/TrF/TrP,
translated in every port/lang/*.po. Match the surrounding code, keep each change to one topic, and
never claim console test results that were not reported.
