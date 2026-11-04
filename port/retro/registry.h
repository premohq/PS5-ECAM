// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the systems after the first four sides (Wii U, 3DS, DS, Switch), the launcher's sides 4
// on, in this order. Each runs on a libretro core linked into the app, with the app's in-game menu
// (port/libretro; core: its name there), or on a program bundled beside it (runtime, started as Eden
// is: port/retro/launch.cpp). Every count of sides comes from this list: a new system is a row here,
// a ps5launcher::System and its names (frontend/launcher.h), and its file types (HasExtension).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace ps5retro
{
	// colour: the side's own (0xRRGGBB), as the first four have theirs (ui/tokens.h): the launcher's accent
	// on its side, its tile and its controller in the mark, and its in-game menu's
	struct System { const char* key; const char* name; const char* runtime; const char* games; const char* data; const char* core = ""; uint32_t colour = 0xbca375; };
	inline constexpr std::array<System, 10> kSystems{{
		{"gamecube", "GameCube", "retro/porpoise/eboot.bin", "/data/porpoise/games/GameCube", "/data/porpoise", "", 0x6f6cff}, // indigo
		{"wii", "Wii", "retro/porpoise/eboot.bin", "/data/porpoise/games/Wii", "/data/porpoise", "", 0x3fcff0},                // the Wii Menu's sky
		{"n64", "Nintendo 64", "retro/n64/eboot.bin", "/data/PS5N64/games", "/data/PS5N64", "paralleln64", 0x2bc994},          // jungle green
		{"nes", "NES", "retro/nes/eboot.bin", "/data/PS5NES/games", "/data/PS5NES", "mesen", 0xff7043},                        // the pad's red
		{"gb", "Game Boy", "retro/gbc/eboot.bin", "/data/PS5GBC/games", "/data/PS5GBC", "mgba", 0xa8bf3a},                     // the screen's pea green
		{"gbc", "Game Boy Color", "retro/gbc/eboot.bin", "/data/PS5GBC/games", "/data/PS5GBC", "mgba", 0xff5ca8},              // berry
		{"gba", "Game Boy Advance", "retro/gba/eboot.bin", "/data/PS5GBA/games", "/data/PS5GBA", "mgba", 0xb98fff},            // violet
		{"snes", "Super Nintendo", "", "/data/PS5SNES/games", "/data/PS5SNES", "mesens", 0xdb70f0},                            // the buttons' lavender
		{"vb", "Virtual Boy", "", "/data/PS5VB/games", "/data/PS5VB", "beetlevb", 0xe02130},                                   // its red on black
		{"pokemini", "Pokémon mini", "", "/data/PS5PokeMini/games", "/data/PS5PokeMini", "pokemini", 0xffe14d},                // electric yellow
	}};
	// the launcher's sides: the first four, then these
	inline constexpr int kFirst = 4;
	inline constexpr int kSideCount = kFirst + (int)kSystems.size();
	inline const System* Find(std::string_view key) { for (const auto& s : kSystems) if (s.key == key) return &s; return nullptr; }
	inline bool IsRetro(int side) { return side >= kFirst && side < kSideCount; }
	// a side's place in kSystems, and in what is kept for each of them (ps5settings::Launcher's retro)
	inline size_t Slot(int side) { return size_t(side - kFirst); }
	inline const System& Of(int side) { return kSystems.at(Slot(side)); }
	inline bool Is(int side, std::string_view key) { return IsRetro(side) && Of(side).key == key; }
	struct Backend { const char* name; const char* runtime; const char* data; };
	// the sides with a second emulator, chosen in the side's Settings (backend 1): a program bundled
	// beside the app too, with its own settings and saves; first, the name of the side's own program
	struct Alternative { const char* key; const char* first; Backend backend; };
	inline constexpr Alternative kAlternatives[] = {
		{"n64", "N64XY", {"Mupen64Plus (experimental)", "retro/mupen64plus/eboot.bin", "/data/mupen64plus"}},
		{"nes", "NESYX", {"FCEUmm PS5 (experimental)", "retro/fceumm/eboot.bin", "/data/fceumm"}},
	};
	inline const Alternative* AlternativeOf(int side)
	{
		for (const auto& a : kAlternatives) if (Is(side, a.key)) return &a;
		return nullptr;
	}
	inline Backend BackendFor(int side, int backend = 0)
	{
		if (const Alternative* other = AlternativeOf(side); other && backend == 1) return other->backend;
		const auto& s = Of(side); return {s.name, s.runtime, s.data};
	}
	bool HasExtension(std::string_view side, std::string name);
}
