// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the pictures Orbit puts behind the game in focus. First a game's own moment, the picture
// each save state leaves beside its file (<state file>.png: what you were doing when you saved); then a
// screenshot of the game from libretro's thumbnails (thumbnails.libretro.com), which have the Wii U, the
// 3DS, the DS, the GameCube, the Wii and the classics, though not the Switch. GameTDB has none: only
// covers, discs and backs. Screenshots are cached in covers/snaps, with an empty file for one not found.

#pragma once

#include "emulator.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ps5shots
{
	// The picture beside a save state just saved: pixels 0xAARRGGBB, pitch in pixels. What goes wrong
	// costs only the picture: it is logged, and the state is saved all the same
	void SaveStatePicture(const std::string& statePath, const uint32_t* pixels, unsigned width, unsigned height, size_t pitch);
	// The file of a game's screenshot, or nothing until it is there. Asking for one not looked for yet
	// fetches it on a thread of its own, the last one asked for first (side: the launcher's)
	std::string Snap(int side, const ps5emu::Game& game);
	// Before a game starts: nothing goes on behind it
	void Stop();
}
