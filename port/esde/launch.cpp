// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: ES-DE started from the launcher, and a game it started handed back to it (esde.h, handoff.h).
#include "esde.h"
#include "handoff.h"
#include "../app/catalog.h"
#include "../app/boxart.h"
#include "../app/emulator.h"
#include "../app/paths.h"
#include "../app/own_status.h"
#include "../azahar/library.h"
#include "../eden/library.h"
#include "../eglantine/launch.h"
#include "../frontend/launcher.h"
#include "../frontend/settings.h"
#include "../melonds/library.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/pad.h"
#include "../retro/library.h"
#include "../retro/launch.h"
#include "../ui/images.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

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
			case ps5launcher::System::Switch2:
				return settings.nx2.gamesFolder;
			default:
				return ps5retro::IsRetro((int)side) ? settings.retro[(int)side - ps5retro::kFirst].gamesFolder : std::string();
			}
		}

		std::string Cover(int side, const ps5settings::Launcher& settings, const ps5emu::Game& game)
		{
			switch ((ps5launcher::System)side)
			{
			case ps5launcher::System::WiiU:
				return ps5boxart::Path(ps5boxart::System::WiiU, game.gameId);
			case ps5launcher::System::N3ds:
				if (std::string cover = ps5boxart::Path(ps5boxart::System::N3ds, game.gameId); !cover.empty())
					return cover;
				return ps5azahar::CoverPath(game.titleId);
			case ps5launcher::System::Nds:
				if (std::string cover = ps5boxart::Path(ps5boxart::System::Nds, game.gameId); !cover.empty())
					return cover;
				return ps5melonds::CoverPath(game.titleId);
			case ps5launcher::System::Switch:
				if (std::string own = ps5eden::BoxArt(game); !own.empty())
					return own;
				return settings.nx.nxCovers == 1 ? ps5eden::CoverPath(game) : ps5boxart::Path(ps5boxart::System::Switch, game.gameId);
			case ps5launcher::System::Switch2:
				return ps5eden::BoxArt(game);
			default:
				return ps5retro::CoverPath(side, game);
			}
		}

		// ES-DE finds downloaded art by matching the ROM's path without its extension. Keep the
		// directory tree and never replace a file ES-DE or the user already put there.
		bool WriteCover(const std::string& home, const std::string& system, const std::string& folder,
			const std::string& gamePath, const std::string& cover)
		{
			if (cover.empty() || gamePath.empty() || folder.empty())
				return false;
			const fs::path rom = fs::path(gamePath).lexically_normal();
			const fs::path root = fs::path(folder).lexically_normal();
			const fs::path relative = rom.lexically_relative(root);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const fs::path& part : relative)
				if (part == ".." || part == ".")
					return false;
			fs::path target = fs::path(home) / "ES-DE" / "downloaded_media" / system / "covers" / relative;
			target.replace_extension(".png");
			std::error_code ec;
			for (const char* extension : {".png", ".jpg", ".jpeg", ".webp"})
			{
				fs::path existing = target;
				existing.replace_extension(extension);
				const auto status = ps5files::OwnStatus(existing, ec);
				if (ec || fs::exists(status))
					return false;
			}
			fs::create_directories(target.parent_path(), ec);
			if (ec)
				return false;
			std::vector<uint8_t> rgba;
			int width = 0, height = 0;
			if (!ui::DecodeFile(cover, rgba, width, height) || width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
				size_t(width) * size_t(height) > 16 * 1024 * 1024 || rgba.size() != size_t(width) * size_t(height) * 4)
				return false;
			std::vector<uint8_t> png;
			auto append = [](void* context, void* bytes, int size) {
				auto& out = *static_cast<std::vector<uint8_t>*>(context);
				const auto* begin = static_cast<const uint8_t*>(bytes);
				out.insert(out.end(), begin, begin + size);
			};
			if (!stbi_write_png_to_func(append, &png, width, height, 4, rgba.data(), width * 4) || png.empty())
				return false;
			const fs::path staged = target.string() + ".new";
			{
				std::ofstream out(staged, std::ios::binary | std::ios::trunc);
				if (!out) return false;
				out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
				if (!out)
				{
					out.close();
					fs::remove(staged, ec);
					return false;
				}
			}
			fs::create_hard_link(staged, target, ec); // atomic no-replace: never clobber ES-DE's artwork
			if (ec)
			{
				fs::remove(staged, ec);
				return false;
			}
			fs::remove(staged, ec);
			return true;
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
				if (side == ps5launcher::System::Switch2 && !ps5eglantine::Available())
					continue;
				if (ps5retro::IsRetro(index) && !ps5retro::Available(index, settings.retro[index - ps5retro::kFirst].retroBackend))
					continue;
				System system{index, GamesFolder(settings, side), {}};
				for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)index))
				{
					const std::string path = entry.game.path.string();
					if (path.empty() || path[0] != '/')
						continue; // Artic Base's, from the 3DS on the network, is no file
					Game game;
					game.path = path;
					game.name = entry.game.name;
					game.titleId = entry.game.titleId;
					game.favourite = entry.favourite;
					game.hidden = entry.hidden;
					game.lastPlayed = entry.lastPlayed;
					game.minutesPlayed = entry.minutesPlayed;
					game.cover = Cover(index, settings, entry.game);
					system.games.push_back(std::move(game));
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
		auto systems = FromCatalogue(settings);
		int covers = 0;
		for (const System& system : systems)
		{
			const char* name = SystemName(system.side);
			if (!name)
				continue;
			for (const Game& game : system.games)
				if (WriteCover(home, name, system.folder, game.path, game.cover))
					covers++;
		}
		if (covers) ps5log::Line("[es-de] {} launcher covers copied into downloaded_media", covers);
		const Written written = WriteLibrary(home, Reference(), ps5paths::Eboot(), systems);
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
		ps5log::Line("[es-de] ES-DE did not start: Playtendoh's own launcher instead");
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
