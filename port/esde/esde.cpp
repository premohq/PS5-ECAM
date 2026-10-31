// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: ES-DE's systems and game lists from the launcher's catalogue (esde.h). After the
// library_ps5.cpp of Mihawk's ES-DE port, which writes them from its own app's library.
#include "esde.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace ps5esde
{
	namespace
	{
		// ES-DE's systems (its resources/systems/unix/es_systems.xml), in the launcher's sides' order:
		// Wii U, 3DS, DS, Switch, then ps5retro::kSystems' (GameCube, Wii, N64, NES, GB, GBC, GBA, SNES,
		// Virtual Boy, Pokemon mini). The extensions are each side's (ES-DE scans for none here: its
		// systems file wants them all the same). Every theme the app ships has art for each of these
		// names (tools/check-esde-themes.py, run by tools/build-esde.sh): a new one is checked there.
		struct Known
		{
			const char* name;
			const char* fullname;
			const char* extensions;
		};
		constexpr Known kSystems[] = {
			{"wiiu", "Nintendo Wii U", ".wua .wud .wux .rpx .zip .7z .rar .WUA .WUD .WUX .RPX .ZIP .7Z .RAR"},
			{"n3ds", "Nintendo 3DS", ".3ds .cci .cxi .3dsx .cia .z3ds .zip .7z .rar .3DS .CCI .CXI .3DSX .CIA .Z3DS .ZIP .7Z .RAR"},
			{"nds", "Nintendo DS", ".nds .srl .zip .7z .rar .NDS .SRL .ZIP .7Z .RAR"},
			{"switch", "Nintendo Switch", ".nsp .xci .NSP .XCI"},
			{"gc", "Nintendo GameCube", ".iso .gcm .rvz .ciso .gcz .wia .ISO .GCM .RVZ .CISO .GCZ .WIA"},
			{"wii", "Nintendo Wii", ".iso .gcm .rvz .ciso .gcz .wia .wbfs .wad .dol .elf .ISO .GCM .RVZ .CISO .GCZ .WIA .WBFS .WAD .DOL .ELF"},
			{"n64", "Nintendo 64", ".z64 .n64 .v64 .zip .rom .Z64 .N64 .V64 .ZIP .ROM"},
			{"nes", "Nintendo Entertainment System", ".nes .unf .unif .fds .zip .7z .NES .UNF .UNIF .FDS .ZIP .7Z"},
			{"gb", "Nintendo Game Boy", ".gb .GB"},
			{"gbc", "Nintendo Game Boy Color", ".gbc .GBC"},
			{"gba", "Nintendo Game Boy Advance", ".gba .zip .7z .GBA .ZIP .7Z"},
			{"snes", "Nintendo SNES (Super Nintendo)", ".sfc .smc .swc .fig .bs .st .SFC .SMC .SWC .FIG .BS .ST"},
			{"virtualboy", "Nintendo Virtual Boy", ".vb .vboy .VB .VBOY"},
			{"pokemini", "Nintendo Pokemon Mini", ".min .MIN"},
		};
		constexpr int kKnown = (int)(sizeof(kSystems) / sizeof(kSystems[0]));

		struct Reference
		{
			std::string fullname, platform, theme;
		};

		// ES-DE's own systems file: name -> full name, platform, theme
		std::map<std::string, Reference> ReadReference(const std::string& path)
		{
			std::map<std::string, Reference> systems;
			pugi::xml_document document;
			if (path.empty() || !document.load_file(path.c_str()))
				return systems;
			for (pugi::xml_node system : document.child("systemList").children("system"))
				systems[system.child_value("name")] = {system.child_value("fullname"), system.child_value("platform"), system.child_value("theme")};
			return systems;
		}

		void SetChild(pugi::xml_node node, const char* name, const std::string& value)
		{
			pugi::xml_node child = node.child(name);
			if (!child)
				child = node.append_child(name);
			child.text().set(value.c_str());
		}

		// The catalogue's seconds since 1970 as ES-DE keeps a time: "YYYYMMDDTHHMMSS", local
		std::string EsdeTime(int64_t seconds)
		{
			if (seconds <= 0)
				return {};
			const std::time_t when = (std::time_t)seconds;
			std::tm local{};
			if (!localtime_r(&when, &local))
				return {};
			char text[64]; // "YYYYMMDDTHHMMSS" for any year the fields could hold
			std::snprintf(text, sizeof(text), "%04d%02d%02dT%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
				local.tm_min, local.tm_sec);
			return text;
		}

		// A game list's path, full: "./x" is relative to the system's folder
		std::string Absolute(const std::string& folder, const std::string& path)
		{
			if (path.rfind("./", 0) == 0)
				return (folder == "/" ? std::string() : folder) + "/" + path.substr(2);
			return path;
		}

		// The favourites as the last write left them, by game path: whichever of the launcher and ES-DE
		// changed one since is the one kept
		std::set<std::string> ReadLines(const std::string& path)
		{
			std::set<std::string> lines;
			if (std::FILE* in = std::fopen(path.c_str(), "rb"))
			{
				char line[4096];
				while (std::fgets(line, sizeof(line), in))
				{
					std::string text(line);
					while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
						text.pop_back();
					if (!text.empty())
						lines.insert(text);
				}
				std::fclose(in);
			}
			return lines;
		}

		bool WriteLines(const std::string& path, const std::set<std::string>& lines)
		{
			const std::string staged = path + ".new";
			std::FILE* out = std::fopen(staged.c_str(), "wb");
			if (!out)
				return false;
			for (const std::string& line : lines)
				std::fprintf(out, "%s\n", line.c_str());
			if (std::fclose(out) != 0 || std::rename(staged.c_str(), path.c_str()) != 0)
			{
				std::remove(staged.c_str());
				return false;
			}
			return true;
		}

		bool Save(const pugi::xml_document& document, const std::string& path)
		{
			const std::string staged = path + ".new";
			if (!document.save_file(staged.c_str(), "    "))
				return false;
			if (std::rename(staged.c_str(), path.c_str()) != 0)
			{
				std::remove(staged.c_str());
				return false;
			}
			return true;
		}
	}

	const char* SystemName(int side)
	{
		return side >= 0 && side < kKnown ? kSystems[side].name : nullptr;
	}

	int SideNamed(std::string_view name)
	{
		for (int side = 0; side < kKnown; side++)
			if (name == kSystems[side].name)
				return side;
		return -1;
	}

	std::string CommonFolder(const std::vector<std::string>& paths, const std::string& fallback)
	{
		if (paths.empty())
			return fallback;
		fs::path common = fs::path(paths.front()).parent_path();
		for (const std::string& path : paths)
		{
			const fs::path folder = fs::path(path).parent_path();
			fs::path shared;
			auto a = common.begin(), b = folder.begin();
			for (; a != common.end() && b != folder.end() && *a == *b; ++a, ++b)
				shared /= *a;
			common = shared;
		}
		const std::string folder = common.string();
		return folder.empty() ? "/" : folder;
	}

	Written WriteLibrary(const std::string& home, const std::string& reference, const std::string& eboot, const std::vector<System>& systems)
	{
		Written written;
		const std::string data = home + "/ES-DE";
		std::error_code ec;
		fs::create_directories(data + "/custom_systems", ec);
		fs::create_directories(data + "/gamelists", ec);
		const std::map<std::string, Reference> known = ReadReference(reference);
		const std::string favouritesFile = data + "/ecam-favorites.txt";
		const std::set<std::string> wereFavourites = ReadLines(favouritesFile);
		std::set<std::string> favourites;

		pugi::xml_document systemsFile;
		systemsFile.append_child(pugi::node_comment)
			.set_value(" Written by PS5-ECAM before each start of ES-DE, from its library (port/esde/esde.cpp): changes here do not last. ");
		pugi::xml_node list = systemsFile.append_child("systemList");
		for (const System& system : systems)
		{
			const char* name = SystemName(system.side);
			if (!name)
				continue;
			std::vector<std::string> paths;
			for (const Game& game : system.games)
				paths.push_back(game.path);
			const std::string folder = CommonFolder(paths, system.folder);
			const auto own = known.find(name);
			pugi::xml_node entry = list.append_child("system");
			entry.append_child("name").text().set(name);
			entry.append_child("fullname").text().set(
				own != known.end() && !own->second.fullname.empty() ? own->second.fullname.c_str() : kSystems[system.side].fullname);
			entry.append_child("path").text().set(folder.c_str());
			entry.append_child("extension").text().set(kSystems[system.side].extensions);
			pugi::xml_node command = entry.append_child("command");
			command.append_attribute("label").set_value("PS5-ECAM");
			command.text().set((eboot + " --rom %ROM%").c_str());
			entry.append_child("platform").text().set(
				own != known.end() && !own->second.platform.empty() ? own->second.platform.c_str() : name);
			entry.append_child("theme").text().set(own != known.end() && !own->second.theme.empty() ? own->second.theme.c_str() : name);
			written.systems++;

			// its game list: the side's games, each keeping what ES-DE knows of it
			const std::string listFolder = data + "/gamelists/" + name, listFile = listFolder + "/gamelist.xml";
			fs::create_directories(listFolder, ec);
			pugi::xml_document previous;
			previous.load_file(listFile.c_str());
			std::map<std::string, pugi::xml_node> knownGames;
			for (pugi::xml_node game : previous.child("gameList").children("game"))
				knownGames[Absolute(folder, game.child_value("path"))] = game;
			pugi::xml_document games;
			pugi::xml_node root = games.append_child("gameList");
			for (pugi::xml_node node : previous.child("gameList").children("folder"))
				root.append_copy(node);
			const std::string prefix = folder == "/" ? "/" : folder + "/";
			for (const Game& game : system.games)
			{
				if (game.path.rfind(prefix, 0) != 0)
					continue; // CommonFolder holds them all; a path it does not hold is not a game's
				const std::string relative = "./" + game.path.substr(prefix.size());
				pugi::xml_node node;
				if (const auto found = knownGames.find(game.path); found != knownGames.end())
				{
					node = root.append_copy(found->second);
					node.child("path").text().set(relative.c_str());
				}
				else
				{
					node = root.append_child("game");
					node.append_child("path").text().set(relative.c_str());
				}
				if (std::string(node.child_value("name")).empty())
					SetChild(node, "name", game.name);
				// the favourite: the launcher's when it changed it since the last write, else ES-DE's when
				// ES-DE did (said back to the launcher), else as it was
				const bool was = wereFavourites.count(game.path) != 0;
				const bool esde = std::string(node.child_value("favorite")) == "true";
				bool favourite = was;
				if (game.favourite != was)
					favourite = game.favourite;
				else if (esde != was)
				{
					favourite = esde;
					if (game.titleId)
						written.fromEsde.push_back({system.side, game.titleId, esde});
				}
				if (favourite)
				{
					SetChild(node, "favorite", "true");
					favourites.insert(game.path);
				}
				else if (node.child("favorite"))
					node.remove_child("favorite");
				if (game.hidden)
					SetChild(node, "hidden", "true");
				else if (node.child("hidden"))
					node.remove_child("hidden");
				// the later last play, the longer play time
				const std::string played = EsdeTime(game.lastPlayed), before = node.child_value("lastplayed");
				if (!played.empty() && (before.size() != played.size() || played > before))
					SetChild(node, "lastplayed", played);
				const unsigned long long seconds = (unsigned long long)game.minutesPlayed * 60;
				if (seconds > std::strtoull(node.child_value("playtime"), nullptr, 10))
					SetChild(node, "playtime", std::to_string(seconds));
				written.games++;
			}
			if (!Save(games, listFile) && written.error.empty())
				written.error = "could not write " + listFile;
		}
		if (!Save(systemsFile, data + "/custom_systems/es_systems.xml") && written.error.empty())
			written.error = "could not write " + data + "/custom_systems/es_systems.xml";
		if (!WriteLines(favouritesFile, favourites) && written.error.empty())
			written.error = "could not write " + favouritesFile;
		return written;
	}
}
