// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../app/emulator.h"
#include "../frontend/settings.h"

namespace ps5eglantine
{
	bool Available();
	bool KeysFound();
	bool FirmwareFound();
	std::string DataFolder();
	void LaunchGame(const ps5emu::Game& game, const ps5settings::Launcher& settings, std::string& error);
}
