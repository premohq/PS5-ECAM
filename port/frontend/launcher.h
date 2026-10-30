// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: what the launcher (shell.h) and main_ps5.cpp share: the system sides, what the launcher
// shows of their state, and the game it returns.

#pragma once

#include "../app/emulator.h"
#include "../app/reorganize.h"
#include "../retro/registry.h"
#include "settings.h"

#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace ps5launcher
{
	enum class System
	{
		WiiU,	// Cemu
		N3ds,	// Azahar
		Nds,	// melonDS
		Switch, // Eden, in a program of its own (eden/eboot.bin: port/eden)
		// the others, in ps5retro::kSystems' order
		GameCube, Wii, N64, Nes, Gb, Gbc, Gba, Snes, VirtualBoy, PokemonMini,
	};
	// the sides, in the bar's order (their numbers)
	constexpr int kSideCount = ps5retro::kSideCount;
	static_assert((int)System::PokemonMini + 1 == kSideCount, "a ps5retro::kSystems row for each System after the Switch");

	// The side's name in ps5cemu.json ("wiiu", "3ds", "ds", "switch", ps5retro::kSystems' keys) and the one players read
	inline const char* SideName(System side)
	{
		if (ps5retro::IsRetro((int)side))
			return ps5retro::Of((int)side).key;
		return side == System::N3ds ? "3ds" : side == System::Nds ? "ds" : side == System::Switch ? "switch" : "wiiu";
	}
	inline System SideNamed(const std::string& name)
	{
		for (int side = ps5retro::kFirst; side < kSideCount; side++)
			if (name == ps5retro::Of(side).key)
				return (System)side;
		return name == "3ds" ? System::N3ds : name == "ds" ? System::Nds : name == "switch" ? System::Switch : System::WiiU;
	}
	inline const char* SideTitle(System side)
	{
		static constexpr const char* kRetroTitles[] = {"GameCube", "Wii", "N64", "NES", "GB", "GBC", "GBA", "SNES", "Virtual Boy", "Pokémon mini"};
		static_assert(std::size(kRetroTitles) == ps5retro::kSystems.size());
		if (ps5retro::IsRetro((int)side))
			return kRetroTitles[(int)side - ps5retro::kFirst];
		return side == System::N3ds ? "3DS" : side == System::Nds ? "DS" : side == System::Switch ? "Switch" : "Wii U";
	}

	struct Status
	{
		bool coreReady = false;	 // Cemu started: its library can open
		bool coreStarting = false; // Cemu's library loading beside the launcher (ps5emu::StartCore), which polls it
		std::string notice;		 // a problem to show on the Wii U side's Library (empty: none)
		std::string notice3ds;	 // and on the 3DS side's
		std::string noticeDs;	 // and on the DS side's
		std::string noticeRetro[ps5retro::kSystems.size()]; // and on the other sides', in ps5retro::kSystems' order
		std::string noticeSwitch; // and on the Switch side's
		std::string launchError; // why the last Wii U game did not start, said once (empty: it did)
		std::string launchError3ds; // and the last 3DS game
		std::string launchErrorDs;  // and the last DS game
		std::string launchErrorSwitch; // and the last Switch game
		std::vector<std::string> diagnostics; // the lines Settings > Diagnostics shows
		// the session before this one ended in a crash: its first [crash] line (boot.prev.log), said once
		// as the launcher starts; empty: it did not
		std::string lastCrash;
		// a home screen tile's game (--rom, app/forwarders.h): started as soon as its side can, on that
		// side, by its path (or its file's name); empty: none
		std::string autoLaunch;
		System autoLaunchSide = System::WiiU;
		bool autoLaunchResume = false; // and it goes on from its last quit's state
		bool autoLaunchFrontend = false; // and it is EmulationStation's: one that cannot start goes back to it (port/esde/esde.h)
		// per side (Wii U, 3DS, DS, Switch): its list is its last look's, its games folder a network share's
		// that this session has not looked through (Settings > Game files, #75)
		bool lastLook[kSideCount] = {};
		// what this start did of a folder for each system (app/reorganize.h): said once, as the launcher starts
		ps5reorganize::Outcome reorganized;
	};

	struct Choice
	{
		System system;
		ps5emu::Game game;
		// an archived game's unpacked copy, which its emulator starts (app/game_archives.h); empty: the
		// game's own path
		std::string launchPath;
		// Resume where you left off: the 3DS or DS game's state from its last quit loaded as it starts
		// (ps5saves::ResumeState)
		bool resume = false;
	};
}
