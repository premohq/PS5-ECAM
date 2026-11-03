// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: which sides play on a libretro core in the app (port/libretro/game.h), for the launcher's
// files, which need nothing else of the frontend (no libretro.h).
#pragma once

namespace ps5libretro
{
	// A side whose core (ps5retro::kSystems' core) this build links, and that has no program of its
	// own in this copy of the app: its games play in the app, on the core. A side whose program (PR
	// #87's or #91's) is packaged keeps it until its core is measured on a console (docs/ROADMAP.md).
	bool Plays(int side);
	// Its core's name as players read it ("Mesen", "ParaLLEl-N64"); empty for a side with none
	const char* CoreTitle(int side);
}
