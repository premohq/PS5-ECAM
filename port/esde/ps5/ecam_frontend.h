// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: what ES-DE's program knows of the app that started it (main_ecam.cpp sets it, game_ecam.cpp
// reads it): its eboot, which plays the games and is gone back to when ES-DE is quit, and ES-DE's home in
// its data folder (port/esde/handoff.h).
#pragma once

#include <string>

namespace ecam_frontend
{
	inline std::string& Eboot()
	{
		static std::string eboot = "/app0/eboot.bin";
		return eboot;
	}
	inline std::string& Home()
	{
		static std::string home = "/data/ps5cemu/es-de";
		return home;
	}
}
