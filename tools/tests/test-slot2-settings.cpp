// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/settings.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
int main()
{
	using namespace ps5settings;
	std::filesystem::create_directories(ps5paths::Root());
	Launcher settings;
	Check(!settings.nds.dsSlot2, "new Slot-2 feature defaults off");
	const uint64_t title = 0x1234;
	settings.games[GameKey("ds", title)]["dsSlot2"] = 1;
	settings.games[GameKey("ds", title)]["dsMicSource"] = 2;
	settings.games[GameKey("ds", title)]["dsTouchX"] = 10;
	settings.games[GameKey("ds", title)]["dsTouchScale"] = 75;
	Check(Save(settings), "settings serialize");
	auto loaded = Load();
	Check(!loaded.nds.dsSlot2 && ForGame(loaded, "ds", title).nds.dsSlot2, "game-specific opt-in survives save/load");
	Check(!ForGame(loaded, "ds", title + 1).nds.dsSlot2, "other DS games retain default");
	Check(!ForGame(loaded, "3ds", title).n3ds.dsSlot2 && !ForGame(loaded, "switch", title).nx.dsSlot2, "other systems do not inherit DS opt-in");
	int value = 0;
	Check(Value(ForGame(loaded, "ds", title), "ds", "dsSlot2", value) && value == 1, "per-game setting adapter resolves Slot-2");
	Check(loaded.nds.dsMicSource == 0 && ForGame(loaded, "ds", title).nds.dsMicSource == 2, "recorded microphone is opt-in per DS game and persists");
	Check(Value(ForGame(loaded, "ds", title), "ds", "dsMicSource", value) && value == 2, "microphone setting adapter resolves recorded source");
	Check(ForGame(loaded, "3ds", title).n3ds.dsMicSource == 0, "3DS does not inherit DS microphone source");
	Check(ForGame(loaded, "ds", title).nds.dsTouchX == 10 && ForGame(loaded, "ds", title).nds.dsTouchScale == 75 && loaded.nds.dsTouchScale == 100, "touch calibration persists per game with identity defaults elsewhere");
	loaded.nx2.nxBlockList = false;
	loaded.games[GameKey("switch2", title)]["nxBlockList"] = 1;
	Check(Save(loaded), "Switch 2 precompilation setting serializes");
	auto switch2 = Load();
	Check(!switch2.nx2.nxBlockList && ForGame(switch2, "switch2", title).nx2.nxBlockList,
		"Switch 2 game profile can override precompilation without changing the side default");
	const auto& switch2Settings = GameSettingNames("switch2");
	Check(std::find(switch2Settings.begin(), switch2Settings.end(), "nxBlockList") != switch2Settings.end(),
		"Switch 2 exposes precompilation in its per-game settings list");
	Check(Value(ForGame(switch2, "switch2", title), "switch2", "nxBlockList", value) && value == 1,
		"Switch 2 precompilation profile value resolves through the shared settings adapter");
	switch2.nx2.nxDeadzone = 8;
	switch2.nx2.performance = false;
	switch2.games[GameKey("switch2", title)]["nxDeadzone"] = 20;
	switch2.games[GameKey("switch2", title)]["performance"] = 1;
	Check(Save(switch2), "Switch 2 deadzone and HUD profiles serialize");
	const auto controls = Load();
	const auto perGame = ForGame(controls, "switch2", title);
	Check(perGame.nx2.nxDeadzone == 20 && perGame.nx2.performance && controls.nx2.nxDeadzone == 8 && !controls.nx2.performance,
		"Switch 2 title profile overrides controls and HUD without changing side defaults");
	Check(Value(perGame, "switch2", "nxDeadzone", value) && value == 20 &&
		  Value(perGame, "switch2", "performance", value) && value == 1,
		"Switch 2 deadzone and HUD resolve through the shared per-game settings adapter");
	return failures ? 1 : 0;
}
