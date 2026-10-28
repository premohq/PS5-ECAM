// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "registry.h"
#include <vector>
namespace ps5retro
{
	std::vector<std::string> LaunchArguments(int side, const std::string& rom, const std::string& returnProgram);
	bool Available(int side, int backend = 0);
	std::string Diagnostics(int side, int backend = 0);
	bool LaunchLibrary(int side, std::string& error, int backend = 0);
	bool Launch(int side, const std::string& rom, std::string& error, int backend = 0);
}
