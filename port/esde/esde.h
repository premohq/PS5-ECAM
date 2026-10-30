// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: EmulationStation (ES-DE) as the app's other frontend (Settings > Launcher > Start in
// EmulationStation). ES-DE is a program of its own in the app's folder (es-de/es-de.bin, built by
// tools/build-esde.sh from Mihawk's ES-DE port in PS5 RetroArch), drawing with OpenGL where the launcher
// draws with Vulkan, so the two never share a process: PS5-ECAM starts it through LoadExec as it starts
// Eden. ES-DE shows the same systems and games as the launcher, and every game it starts plays here, as
// a home screen tile's does, with the same emulators, settings, saves and in-game menus
// (port/esde/handoff.h says how a game goes over and back).
//
// Before each start of ES-DE its systems and game lists are written from the launcher's catalogue
// (app/catalog.h): the sides the bar shows, each as the system ES-DE has for it (its themes' art), with
// exactly the games the side lists, and ES-DE runs with --gamelist-only. A game ES-DE knows keeps what
// ES-DE knows of it (scraped details, play count); its favourite follows whichever of the two changed it
// last, and its last play and play time the later and the longer.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ps5settings
{
	struct Launcher;
}

namespace ps5esde
{
	constexpr const char* kProgram = "es-de/es-de.bin";

	// ES-DE's name for a side's system ("wiiu", "n3ds", "nds", "switch", "gc"... "virtualboy"), whose
	// themes have its art; nullptr for no side
	const char* SystemName(int side);
	int SideNamed(std::string_view name); // -1: none

	// What ES-DE is given of a game, and of a side
	struct Game
	{
		std::string path, name;
		uint64_t titleId = 0;
		bool favourite = false, hidden = false;
		int64_t lastPlayed = 0; // seconds since 1970; 0: never
		uint32_t minutesPlayed = 0;
	};
	struct System
	{
		int side = 0;
		std::string folder; // the side's games folder
		std::vector<Game> games;
	};
	// A favourite changed in ES-DE since the last start, for the launcher's catalogue
	struct Favourite
	{
		int side = 0;
		uint64_t titleId = 0;
		bool on = false;
	};
	struct Written
	{
		int systems = 0, games = 0;
		std::vector<Favourite> fromEsde;
		std::string error; // empty: all of it written
	};

	// The deepest folder holding every one of the paths (ES-DE reads a game only within its system's
	// folder); fallback when there are none
	std::string CommonFolder(const std::vector<std::string>& paths, const std::string& fallback);

	// ES-DE's custom_systems/es_systems.xml and gamelists/<system>/gamelist.xml in home's ES-DE/, from the
	// systems given. reference: ES-DE's own systems file (resources/systems/unix/es_systems.xml in its
	// folder), for each system's full name, platform and theme; eboot: PS5-ECAM's, which each system's
	// command names (ES-DE looks for the command's program before a game starts).
	Written WriteLibrary(const std::string& home, const std::string& reference, const std::string& eboot,
		const std::vector<System>& systems);

	// -- the launcher's side (port/esde/launch.cpp) ----------------------------------------------------

	// Whether the app has ES-DE (es-de/es-de.bin in its folder)
	bool Available();
	// ES-DE's home: <data folder>/es-de
	std::string Home();
	// L1 held as the app starts: PS5-ECAM's own launcher this time, whatever the frontend is
	bool OwnLauncherHeld();
	// ES-DE, with its systems and game lists written first. Returns only when it did not start
	void Start(const ps5settings::Launcher& settings);
	// The game ES-DE asked for, as the app starts: marked taken, its path when it had not been (the
	// arguments LoadExec dropped); empty when there is none
	std::string TakeRequest();
	// A game ES-DE started is the one this session is for (game.txt in its home, taken)
	bool GameFromFrontend();
	// The game ES-DE started has ended, or did not start (error, in English): its result for ES-DE, which
	// starts again. Returns only when it did not
	void BackToFrontend(const ps5settings::Launcher& settings, const std::string& error = {});
}
