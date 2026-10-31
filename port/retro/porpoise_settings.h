// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Porpoise-X's settings (premohq/Porpoise-X, src/ui_settings.cpp), for PS5-ECAM's own Settings
// to show on the GameCube and Wii sides: the files Porpoise reads as PS5-ECAM starts a game in it
// (launch.cpp), written in Porpoise's own words, so PS5-ECAM uses nothing of Porpoise's UI and Porpoise
// needs nothing of PS5-ECAM's.
//   <data>/settings.ini                every game's: "key = value" lines, Porpoise's settings table
//   <data>/game-settings/<key>.ini     a game's own, with only the keys it changes; <key> its disc's ID
//                                      (Porpoise's Library::settings_key_of), else its file's name
// Writing one changes the keys given and keeps every other line as Porpoise wrote it: its folders, its
// presets and button sets, its own menus' settings.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ps5porpoise
{
	// Porpoise's folder (ps5retro::kSystems' data for the GameCube and the Wii)
	std::string Data();
	std::string SettingsFile();
	// A game's own file, for the disc at gamePath
	std::string GameFile(const std::string& gamePath);
	// The disc's ID as Porpoise keys a game's settings ("GALE01"): a GameCube or Wii disc image's own
	// (.iso, .gcm), a WBFS's, a WIA's or RVZ's, a CISO's; else the file's name
	std::string GameKey(const std::string& gamePath);

	// The file's "key = value" settings (empty when there is none)
	std::map<std::string, std::string> Read(const std::string& file);
	// values over the file's (made with Porpoise's header when new), the rest kept
	bool Write(const std::string& file, const std::map<std::string, std::string>& values, std::string& error);
	// keys taken out of a game's own file, so the game has every game's again; the file removed once empty
	bool Remove(const std::string& file, const std::vector<std::string>& keys, std::string& error);
}
