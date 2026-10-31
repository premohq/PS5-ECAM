// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: EmulationStation's games, played by PS5-ECAM (port/esde/handoff.h). In place of the
// game_ps5.cpp of Mihawk's ES-DE port, which hands them to its own app; the same two calls, which his
// ES-DE patch (patches/0001, FileData::launchGame and ViewController::goToStart) makes.
//
// A game ES-DE starts: its game lists are saved and its display released (the patch), game.txt says which
// game, from which system, and PS5-ECAM starts with --rom <path> --frontend es-de, on the side its file is
// for. When the game has ended PS5-ECAM writes result.txt and starts ES-DE again; main_ecam.cpp takes
// that result before ES-DE starts, and ES-DE's start asks for it here.
#include "ecam_frontend.h"
#include "handoff.h"

#include <cstdio>
#include <ctime>

extern "C" int sceSystemServiceLoadExec(const char* path, const char* const* argv);

namespace
{
	ecam_esde::Game returned;
	bool hasReturn = false;
	bool startedFromGame = false;
}

extern "C" int ecam_esde_take_result(void)
{
	hasReturn = startedFromGame = ecam_esde::Take(ecam_esde::ResultFile(ecam_frontend::Home()), returned);
	return hasReturn;
}

// This start is the one after a game (Mihawk's capture_ps5.cpp asks, for its armed captures)
extern "C" int ps5_esde_started_from_game(void)
{
	return startedFromGame;
}

extern "C" int ps5_esde_launch_game(const char* command, const char* system, const char* path, char* error, size_t errorSize)
{
	(void)command; // "<PS5-ECAM's eboot> --rom %ROM%" (port/esde/esde.cpp): the path is the one given
	ecam_esde::Game game;
	game.system = system ? system : "";
	game.path = path ? path : "";
	game.started = (long long)std::time(nullptr);
	const std::string request = ecam_esde::GameFile(ecam_frontend::Home());
	if (game.path.empty() || game.path[0] != '/')
		std::snprintf(error, errorSize, "%s", "the game has no full path");
	else if (!ecam_esde::Write(request, game))
		std::snprintf(error, errorSize, "could not write %s", request.c_str());
	else
	{
		const char* argv[] = {"--rom", game.path.c_str(), "--frontend", "es-de", nullptr};
		const int result = sceSystemServiceLoadExec(ecam_frontend::Eboot().c_str(), argv);
		// it does not come back when PS5-ECAM starts
		std::remove(request.c_str());
		std::snprintf(error, errorSize, "PS5-ECAM did not start (LoadExec %#x)", (unsigned)result);
	}
	return -1;
}

extern "C" int ps5_esde_game_return(char* system, size_t systemSize, char* path, size_t pathSize, long* seconds, int* status,
	char* error, size_t errorSize)
{
	if (!hasReturn)
		return 0;
	hasReturn = false; // once
	std::snprintf(system, systemSize, "%s", returned.system.c_str());
	std::snprintf(path, pathSize, "%s", returned.path.c_str());
	*seconds = (long)returned.seconds;
	*status = returned.status;
	std::snprintf(error, errorSize, "%s", returned.error.c_str());
	return 1;
}
