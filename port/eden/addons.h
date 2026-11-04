// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a Switch game's update and DLC, as its page and the library show them (roadmap N3): the
// update and DLC files the scan read (in the games folder, and in Eden's updates/ and dlc/), each
// given to its game by the base title ID they share, as Eden pairs them when the game starts
// (patches/eden 0016). A game's title ID ends in 000; its update's is the game's + 0x800, a DLC's the
// game's + 0x1000 + its number.

#pragma once

#include "../app/emulator.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace ps5eden::addons
{
	struct File
	{
		uint64_t titleId = 0;
		bool update = false;  // an update, else DLC
		std::string version;  // an update's, as the game shows it ("1.2.0"); empty when not read
		std::string path;	  // where it is, when Eden would not find it by itself (outside its updates/ and dlc/)
	};

	// The game a title is of (Eden's FileSys::GetBaseTitleID)
	inline constexpr uint64_t BaseOf(uint64_t title) { return title & 0xFFFFFFFFFFFFE000ull; }
	// What a title ID says it is, for a file only its name tells of: an update (800, the game's other
	// programs' 801...), else DLC (1001...); neither for a game's own
	inline constexpr bool IsUpdateId(uint64_t title) { return (title & 0x1FFF) >= 0x800 && (title & 0x1FFF) < 0x1000; }
	inline constexpr bool IsDlcId(uint64_t title) { return (title & 0x1FFF) > 0x1000; }

	// Whether version a comes before b: their numbers compared one by one, so 1.10.0 is after 1.9.2
	inline bool Older(const std::string& a, const std::string& b)
	{
		size_t i = 0, j = 0;
		while (i < a.size() || j < b.size())
		{
			const size_t ai = a.find('.', i), bj = b.find('.', j);
			const std::string x = i < a.size() ? a.substr(i, ai == std::string::npos ? std::string::npos : ai - i) : "";
			const std::string y = j < b.size() ? b.substr(j, bj == std::string::npos ? std::string::npos : bj - j) : "";
			const unsigned long long nx = std::strtoull(x.c_str(), nullptr, 10), ny = std::strtoull(y.c_str(), nullptr, 10);
			if (nx != ny)
				return nx < ny;
			if (x != y)
				return x < y;
			i = ai == std::string::npos ? a.size() : ai + 1;
			j = bj == std::string::npos ? b.size() : bj + 1;
		}
		return false;
	}

	// Each game's update and DLC from the files found: whether it has an update and the newest's
	// version, and how many DLC (each title once, in however many files). A game whose title ID was made
	// from its path (no keys and none in its name) has none.
	inline void Pair(std::vector<ps5emu::Game>& games, const std::vector<File>& files, bool (*madeId)(uint64_t))
	{
		for (ps5emu::Game& game : games)
		{
			game.hasUpdate = false;
			game.versionName.clear();
			game.dlcCount = 0;
			if (!game.titleId || madeId(game.titleId))
				continue;
			const uint64_t base = BaseOf(game.titleId);
			std::set<uint64_t> dlc;
			for (const File& file : files)
			{
				if (BaseOf(file.titleId) != base)
					continue;
				if (!file.update)
				{
					dlc.insert(file.titleId);
					continue;
				}
				game.hasUpdate = true;
				if (!file.version.empty() && (game.versionName.empty() || Older(game.versionName, file.version)))
					game.versionName = file.version;
			}
			game.dlcCount = (uint32_t)dlc.size();
		}
	}

	// The files of a game's update and DLC among those found, updates first, for Eden to take wherever
	// they are (patches/eden 0024, #106): together at most maxLength characters with a '|' between
	// them, as the launch hands them over. A path with a '|' in it is left out (FAT and exFAT names
	// cannot have one).
	inline std::vector<std::string> FilesOf(uint64_t titleId, const std::vector<File>& files, size_t maxLength)
	{
		std::vector<std::string> paths;
		if (!titleId)
			return paths;
		size_t length = 0;
		for (const bool update : {true, false})
			for (const File& file : files)
			{
				if (file.update != update || file.path.empty() || BaseOf(file.titleId) != BaseOf(titleId) ||
					file.path.find('|') != std::string::npos || std::find(paths.begin(), paths.end(), file.path) != paths.end())
					continue;
				if (length + file.path.size() + !paths.empty() > maxLength)
					continue;
				length += file.path.size() + !paths.empty();
				paths.push_back(file.path);
			}
		return paths;
	}
}
