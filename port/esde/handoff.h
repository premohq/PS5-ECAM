// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM and EmulationStation (ES-DE): how a game goes from one program to the other and back. ES-DE
// runs as a program of its own in the app's folder (es-de/es-de.bin, tools/build-esde.sh), started by
// PS5-ECAM through LoadExec as Eden is; this header is compiled into both (port/esde/esde.h, and
// port/esde/ps5/ in ES-DE's program), so there is one reading of these files:
//
//   1. ES-DE asks for a game: it writes game.txt in its home (the system it was chosen from, the game's
//      path, when) and starts PS5-ECAM with --rom <path> --frontend es-de;
//   2. PS5-ECAM plays it as a home screen tile's game, on the side its file is for, and marks game.txt
//      taken (one LoadExec dropped the arguments of: a game.txt not taken yet is that game's, to start);
//   3. when the game has ended (or did not start), PS5-ECAM takes game.txt, writes result.txt (the same,
//      with how long it ran and why it stopped, when it did) and starts ES-DE again;
//   4. ES-DE, starting, takes result.txt and opens that system's game list on that game.
//
// ES-DE's home is <PS5-ECAM's data folder>/es-de (its ES-DE/ folder: settings, game lists, media), which
// PS5-ECAM names to it with --ecam-root. A file is written whole or not at all (staged and renamed).

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ecam_esde
{
	struct Game
	{
		std::string system;	   // ES-DE's system the game was chosen in (a collection's too: "favorites")
		std::string path;	   // the game, a full path
		long long started = 0; // seconds since 1970, as ES-DE handed it over
		int taken = 0;		   // 1 once PS5-ECAM has started it
		long long seconds = 0; // a result's: how long it ran
		int status = 0;		   // a result's: 0, or -1 when it did not start or stopped with an error
		std::string error;	   // a result's: why, in English (ES-DE's interface is, on the PS5)
	};

	inline std::string GameFile(const std::string& home) { return home + "/game.txt"; }
	inline std::string ResultFile(const std::string& home) { return home + "/result.txt"; }

	// One line: a value's line breaks become spaces
	inline std::string OneLine(std::string value)
	{
		for (char& c : value)
			if (c == '\n' || c == '\r')
				c = ' ';
		return value;
	}

	inline bool Write(const std::string& file, const Game& game)
	{
		const std::string staged = file + ".new";
		std::FILE* out = std::fopen(staged.c_str(), "wb");
		if (!out)
			return false;
		const bool written = std::fprintf(out, "system=%s\npath=%s\nstarted=%lld\ntaken=%d\nseconds=%lld\nstatus=%d\nerror=%s\n",
								 OneLine(game.system).c_str(), OneLine(game.path).c_str(), game.started, game.taken, game.seconds,
								 game.status, OneLine(game.error).c_str()) > 0;
		if (std::fclose(out) != 0 || !written || std::rename(staged.c_str(), file.c_str()) != 0)
		{
			std::remove(staged.c_str());
			return false;
		}
		return true;
	}

	// The file's game; false when there is none, or it names no game
	inline bool Read(const std::string& file, Game& game)
	{
		std::FILE* in = std::fopen(file.c_str(), "rb");
		if (!in)
			return false;
		game = {};
		char line[4096];
		while (std::fgets(line, sizeof(line), in))
		{
			std::string text(line);
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			const size_t equals = text.find('=');
			if (equals == std::string::npos)
				continue;
			const std::string key = text.substr(0, equals), value = text.substr(equals + 1);
			if (key == "system")
				game.system = value;
			else if (key == "path")
				game.path = value;
			else if (key == "started")
				game.started = std::strtoll(value.c_str(), nullptr, 10);
			else if (key == "taken")
				game.taken = (int)std::strtol(value.c_str(), nullptr, 10);
			else if (key == "seconds")
				game.seconds = std::strtoll(value.c_str(), nullptr, 10);
			else if (key == "status")
				game.status = (int)std::strtol(value.c_str(), nullptr, 10);
			else if (key == "error")
				game.error = value;
		}
		std::fclose(in);
		return !game.path.empty() && game.path[0] == '/';
	}

	// Read, then removed: a game is handed over once
	inline bool Take(const std::string& file, Game& game)
	{
		const bool found = Read(file, game);
		std::remove(file.c_str());
		return found;
	}
}
