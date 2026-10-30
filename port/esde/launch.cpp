// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: ES-DE started from the launcher, and a game it started handed back to it (esde.h, handoff.h).
#include "esde.h"
#include "handoff.h"
#include "../app/catalog.h"
#include "../app/emulator.h"
#include "../app/paths.h"
#include "../eden/library.h"
#include "../frontend/launcher.h"
#include "../frontend/settings.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/pad.h"
#include "../retro/launch.h"

#include <algorithm>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace ps5esde
{
	namespace
	{
		// ES-DE's own systems, in its folder: each system's full name, platform and theme
		std::string Reference()
		{
			return ps5paths::AppDir() + "/es-de/resources/systems/unix/es_systems.xml";
		}

		// What LoadExec may drop: ES-DE's arguments, also in a file in its folder (port/esde/ps5/main_ecam.cpp)
		std::string ArgumentsFile()
		{
			return ps5paths::AppDir() + "/es-de/ecam-arguments.txt";
		}

		std::string GamesFolder(const ps5settings::Launcher& settings, ps5launcher::System side)
		{
			switch (side)
			{
			case ps5launcher::System::WiiU:
				return settings.gamesFolder;
			case ps5launcher::System::N3ds:
				return settings.n3ds.gamesFolder;
			case ps5launcher::System::Nds:
				return settings.nds.gamesFolder;
			case ps5launcher::System::Switch:
				return ps5eden::ScanFolder(settings.nx.gamesFolder);
			default:
				return ps5retro::IsRetro((int)side) ? settings.retro[(int)side - ps5retro::kFirst].gamesFolder : std::string();
			}
		}

		// The sides the bar shows whose emulator the app has, each with the games its library listed last
		std::vector<System> FromCatalogue(const ps5settings::Launcher& settings)
		{
			ps5catalog::Load();
			std::vector<System> systems;
			for (int index = 0; index < ps5launcher::kSideCount; index++)
			{
				const auto side = (ps5launcher::System)index;
				if (!(settings.ui.sides & (1 << index)) || !SystemName(index))
					continue;
				if (side == ps5launcher::System::Switch && !ps5eden::Available())
					continue;
				if (ps5retro::IsRetro(index) && !ps5retro::Available(index, settings.retro[index - ps5retro::kFirst].retroBackend))
					continue;
				System system{index, GamesFolder(settings, side), {}};
				for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)index))
				{
					const std::string path = entry.game.path.string();
					if (path.empty() || path[0] != '/')
						continue; // Artic Base's, from the 3DS on the network, is no file
					system.games.push_back({path, entry.game.name, entry.game.titleId, entry.favourite, entry.hidden, entry.lastPlayed,
						entry.minutesPlayed});
				}
				systems.push_back(std::move(system));
			}
			return systems;
		}
	}

	bool Available()
	{
		std::error_code ec;
		return fs::is_regular_file(ps5paths::AppDir() + "/" + kProgram, ec);
	}

	std::string Home()
	{
		return ps5paths::Root() + "/es-de";
	}

	bool OwnLauncherHeld()
	{
		// the controller answers a moment after the pad is opened: a quarter of a second of looking
		for (int i = 0; i < 25; i++)
		{
			ps5pad::Data data{};
			if (ps5pad::Read(0, data) && (data.buttons & ps5pad::kL1))
				return true;
			sceKernelUsleep(10000);
		}
		return false;
	}

	void Start(const ps5settings::Launcher& settings)
	{
		const std::string home = Home();
		std::error_code ec;
		fs::create_directories(home, ec);
		const Written written = WriteLibrary(home, Reference(), ps5paths::Eboot(), FromCatalogue(settings));
		ps5log::Line("[es-de] {} systems, {} games written for ES-DE in {}{}", written.systems, written.games, home,
			written.error.empty() ? std::string() : "; " + written.error);
		// favourites ES-DE changed since its last start, into the launcher's library too
		if (!written.fromEsde.empty())
		{
			for (const Favourite& favourite : written.fromEsde)
				ps5catalog::SetFavourite((ps5catalog::System)favourite.side, favourite.titleId, favourite.on);
			ps5catalog::Save();
			ps5log::Line("[es-de] {} favourites changed in ES-DE, kept in the library", written.fromEsde.size());
		}
		// it comes back here (--return) when it is quit
		ps5emu::LoadExecApp(kProgram, {"--ecam-root", ps5paths::Root()}, true, ArgumentsFile());
		ps5log::Line("[es-de] ES-DE did not start: PS5-ECAM's own launcher instead");
	}

	std::string TakeRequest()
	{
		const std::string file = ecam_esde::GameFile(Home());
		ecam_esde::Game game;
		if (!ecam_esde::Read(file, game) || game.taken)
			return {};
		game.taken = 1;
		ecam_esde::Write(file, game);
		return game.path;
	}

	bool GameFromFrontend()
	{
		ecam_esde::Game game;
		return ecam_esde::Read(ecam_esde::GameFile(Home()), game) && game.taken;
	}

	void BackToFrontend(const ps5settings::Launcher& settings, const std::string& error)
	{
		const std::string home = Home();
		ecam_esde::Game game;
		if (ecam_esde::Take(ecam_esde::GameFile(home), game))
		{
			const long long now = (long long)std::time(nullptr);
			game.seconds = game.started > 0 && now > game.started ? now - game.started : 0;
			game.status = error.empty() ? 0 : -1;
			game.error = error;
			if (!ecam_esde::Write(ecam_esde::ResultFile(home), game))
				ps5log::Line("[es-de] the game's result could not be written in {}", home);
			ps5log::Line("[es-de] back to ES-DE after {} ({} s){}", game.path, game.seconds, error.empty() ? std::string() : ": " + error);
		}
		Start(settings);
	}
}
