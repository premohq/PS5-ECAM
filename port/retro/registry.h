// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the systems after the first four sides (Wii U, 3DS, DS, Switch), the launcher's sides 4
// on, in this order. Each runs on a libretro core linked into the app, with the app's in-game menu
// (port/libretro; core: its name there), or on a program bundled beside it (runtime, started as Eden
// is: port/retro/launch.cpp). Every count of sides comes from this list: a new system is a row here,
// a ps5launcher::System and its names (frontend/launcher.h), and its file types (HasExtension).
#pragma once
#include <array>
#include <string>
#include <string_view>
namespace ps5retro
{
	struct System { const char* key; const char* name; const char* runtime; const char* games; const char* data; const char* core = ""; };
	inline constexpr std::array<System, 10> kSystems{{
		{"gamecube", "GameCube", "retro/porpoise/eboot.bin", "/data/porpoise/games/GameCube", "/data/porpoise"},
		{"wii", "Wii", "retro/porpoise/eboot.bin", "/data/porpoise/games/Wii", "/data/porpoise"},
		{"n64", "Nintendo 64", "retro/n64/eboot.bin", "/data/PS5N64/games", "/data/PS5N64"},
		{"nes", "NES", "retro/nes/eboot.bin", "/data/PS5NES/games", "/data/PS5NES", "mesen"},
		{"gb", "Game Boy", "retro/gbc/eboot.bin", "/data/PS5GBC/games", "/data/PS5GBC", "mgba"},
		{"gbc", "Game Boy Color", "retro/gbc/eboot.bin", "/data/PS5GBC/games", "/data/PS5GBC", "mgba"},
		{"gba", "Game Boy Advance", "retro/gba/eboot.bin", "/data/PS5GBA/games", "/data/PS5GBA", "mgba"},
		{"snes", "Super Nintendo", "", "/data/PS5SNES/games", "/data/PS5SNES", "mesens"},
		{"vb", "Virtual Boy", "", "/data/PS5VB/games", "/data/PS5VB", "beetlevb"},
		{"pokemini", "Pokémon mini", "", "/data/PS5PokeMini/games", "/data/PS5PokeMini", "pokemini"},
	}};
	// the launcher's sides: the first four, then these
	inline constexpr int kFirst = 4;
	inline constexpr int kSideCount = kFirst + (int)kSystems.size();
	inline const System* Find(std::string_view key) { for (const auto& s : kSystems) if (s.key == key) return &s; return nullptr; }
	inline bool IsRetro(int side) { return side >= kFirst && side < kSideCount; }
	inline const System& Of(int side) { return kSystems.at(size_t(side - kFirst)); }
	struct Backend { const char* name; const char* runtime; const char* data; };
	inline Backend BackendFor(int side, int backend = 0)
	{
		if (side == 6 && backend == 1) return {"Mupen64Plus (experimental)", "retro/mupen64plus/eboot.bin", "/data/mupen64plus"};
		if (side == 7 && backend == 1) return {"FCEUmm PS5 (experimental)", "retro/fceumm/eboot.bin", "/data/fceumm"};
		const auto& s = Of(side); return {s.name, s.runtime, s.data};
	}
	bool HasExtension(std::string_view side, std::string name);
}
