// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: which sides play on a libretro core in the app (port/libretro/game.h), for the launcher's
// files, which need nothing else of the frontend (no libretro.h).
#pragma once

namespace ps5libretro
{
	// A side whose core (ps5retro::kSystems' core) this build links, and that has no program of its
	// own beside the app: its games play in the app, on the core. The sides with a program (PR #87's
	// and #91's) keep it until their core is measured on a console (docs/ROADMAP.md).
	bool Plays(int side);
}
