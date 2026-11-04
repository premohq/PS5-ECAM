// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the launcher's themes, skins over its one layout. A theme sets the colours the kit draws
// with (tokens.h's, by tokens.json's names), how round its corners are, and what moves behind the
// menus; every screen, button and setting is the same in each. The built-in ones are below; more can
// be made as JSON files in /data/ps5cemu/themes, each starting from a built-in one:
//
//   {
//     "name": "Sunset",
//     "base": "midnight",                 a built-in theme's id; its colours are the starting point
//     "colours": {"wiiu": "#ff7a59", "ink-0": "#120806"},   tokens.json's names, #rrggbb or #rrggbbaa
//     "corners": 0.5,                     0 square to 1.5 rounder; 1 is the design's
//     "motif": "side"                     side (each side's own), none or eden
//   }
//
// Only the launcher has themes: the in-game menus keep their own look, and nothing of a theme runs
// in a game.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ui
{
	// What moves behind the menus
	enum class Motif
	{
		Side, // each side's own: bubbles (Wii U), waves (3DS), pixels (DS), bars in pairs (Switch), the later consoles' (frontend/shell/consoles.cpp)
		None, // still: the colours and the game's picture only
		Eden, // the side's light drifting, its own motif faint, motes rising (ProsperoEden 1.001's look)
	};

	struct Theme
	{
		std::string id;			 // "midnight"; a user's: "user:<file name>"
		std::string name;		 // as the list shows it
		std::string description; // one line
		std::map<std::string, uint32_t> colours; // over tokens.json's, by its names ("ink-0", "wiiu")
		float corners = 1.0f;
		Motif motif = Motif::Side;
		bool light = false; // a light ground with dark text: what is drawn on a picture stays white
	};

	// The built-in themes, the default (Link Jack, id "") first
	const std::vector<Theme>& BuiltInThemes();
	// The themes in a folder's .json files (/data/ps5cemu/themes), by file name; one that cannot be read
	// is left out, and why goes in error (one line per file)
	std::vector<Theme> UserThemes(const std::string& folder, std::string* error = nullptr);
	// Every theme the list offers: the built-in ones, then the folder's
	std::vector<Theme> AllThemes(const std::string& folder);
	// The theme by its id among AllThemes, else the default
	Theme FindTheme(const std::string& id, const std::string& folder);

	// The theme's colours into tokens.h's, its corners and motif made the kit's
	void ApplyTheme(const Theme& theme);
	const Theme& CurrentTheme();
	Motif CurrentMotif();
	bool LightTheme();

	// How round the kit draws corners: every shape's radius times this (Canvas)
	float CornerScale();
}
