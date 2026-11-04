# Compatibility

How games run in Playtendoh, from reports on real consoles. It only lists what has been reported:
a game that isn't here hasn't been reported yet, which doesn't mean it doesn't work. To add a game
or correct one, [open an issue](https://github.com/premohq/PS5-ECAM/issues/new/choose) with the
game's region and version, the app version, and how far you played.

| Status | Meaning |
|---|---|
| **Playable** | Can be played through with no known problem that gets in the way |
| **Issues** | Runs, with problems: drawing errors, slowdowns, or a crash in one place |
| **Crashes** | Starts, but crashes or freezes early, before it can really be played |
| **Won't start** | Doesn't get as far as the game |

On the Wii U side, games run much slower without JIT memory (Cemu's interpreter instead of its
recompiler), so the status assumes the app got JIT memory (see the README's
[Install](../README.md#install) step 3).

## Wii U (Cemu)

| Game | Region / version | Status | Notes | Reports | App version |
|---|---|---|---|---|---|
| The Legend of Zelda: Breath of the Wild | v1.5.0 (v208) | Playable | Runs at about 60 fps at 1440p. Up to 2.0.0 C it froze and closed when Magnesis or other runes, or the Sheikah scope (R3), highlighted objects; fixed after 2.0.0 C (a newer Cemu) | [#10](https://github.com/premohq/PS5-ECAM/issues/10) | after 2.0.0 C |
| Fast Racing Neo | | Issues | About half the textures are missing during races | [#13](https://github.com/premohq/PS5-ECAM/issues/13) | |
| Skylanders Giants | | Crashes | Crashes before it boots on 3.5.0 and earlier: every game started with Pro Controllers for players 2 to 4, and Giants only takes the GamePad at boot. 4.0.0 gives players 2 to 4 a controller only while their DualSense is signed in; not yet confirmed on a console | [#29](https://github.com/premohq/PS5-ECAM/issues/29) | 3.5.0 |
| The Legend of Zelda: The Wind Waker HD | EUR, v0 | Crashes | Crashes after making its save, before the title menu, with every setting at default and no graphic packs | [#32](https://github.com/premohq/PS5-ECAM/issues/32) | 3.5.0 |
| Batman: Arkham Origins | USA, v16 | Issues | Plays with its **60 FPS graphic pack turned off**; with it on, it glitches and crashes once gameplay loads. Up to 2.0.0 C it crashed there either way (Cemu ran out of Vulkan descriptors) | [#15](https://github.com/premohq/PS5-ECAM/issues/15) | after 2.0.0 C |

## Nintendo 3DS (Azahar)

| Game | Region / version | Status | Notes | Reports | App version |
|---|---|---|---|---|---|
| The Legend of Zelda: Majora's Mask 3D | USA, Rev 1 | Playable | Save states work from the in-game menu | | after 2.0.0 C |
| Pokémon X | EUR | Issues | Moving black dots on outlined 3D models (the Poké Ball and Professor Sycamore in the introduction, then the characters), at any internal resolution. 4.0.0's Outline fix removes them and is on by default for X, Y, Omega Ruby and Alpha Sapphire; it costs some speed | [#23](https://github.com/premohq/PS5-ECAM/issues/23) | 3.0.0 |

A CIA that is only partly decrypted (the CIA decrypted, the game
inside it still encrypted) is refused by Azahar on any console; that is about the dump, not the
game ([#14](https://github.com/premohq/PS5-ECAM/issues/14)).
