// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string>
#include <string_view>
namespace ps5retro
{
	struct System { const char* key; const char* name; const char* runtime; const char* games; const char* data; };
	inline constexpr std::array<System, 4> kSystems{{
		{"gamecube", "GameCube", "retro/porpoise/eboot.bin", "/data/porpoise/games/GameCube", "/data/porpoise"},
		{"wii", "Wii", "retro/porpoise/eboot.bin", "/data/porpoise/games/Wii", "/data/porpoise"},
		{"n64", "Nintendo 64", "retro/n64/eboot.bin", "/data/PS5N64/games", "/data/PS5N64"},
		{"nes", "NES", "retro/nes/eboot.bin", "/data/PS5NES/games", "/data/PS5NES"}
	}};
	inline const System* Find(std::string_view key) { for (const auto& s : kSystems) if (s.key == key) return &s; return nullptr; }
	inline bool IsRetro(int side) { return side >= 4 && side < 8; }
	inline const System& Of(int side) { return kSystems.at(size_t(side - 4)); }
	bool HasExtension(std::string_view side, std::string name);
}
