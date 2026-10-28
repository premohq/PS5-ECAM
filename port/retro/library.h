// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "registry.h"
#include "../app/emulator.h"
#include <vector>
namespace ps5retro
{
	// Read-only discovery; roots are explicit and files are never migrated or renamed.
	std::vector<ps5emu::Game> Scan(const std::string& side, const std::string& folder, std::string& error);
	std::string CoverPath(int side, const ps5emu::Game& game);
	bool ValidateRom(int side, const std::string& path, std::string& error, int backend = 0);
	void StartScan(int side, const std::string& folder);
	bool Scanning(int side);
	std::vector<ps5emu::Game> ListGames(int side);
	std::string ScanError(int side);
	void FinishScans();
}
