// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Switch side's program finds a game's update and DLC (patches/eden 0018, headless/game_addons.h):
// a game's own folder in Eden's roms/ or in the launcher's games folder, else the files beside it.
#include "game_addons.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Eden::AddOns;
static int failures = 0;
static void Check(bool ok, const char* what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << '\n'; failures += !ok; }
static void Put(const fs::path& path) { fs::create_directories(path.parent_path()); std::ofstream(path) << "x"; }
static std::vector<std::string> Names(const std::vector<fs::path>& paths)
{
	std::vector<std::string> names;
	for (const fs::path& path : paths)
		names.push_back(path.filename().string());
	return names;
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const fs::path work = argv[1];
	constexpr std::uint64_t game = 0x0100ABCDEF124000, update = 0x0100ABCDEF124800, dlc = 0x0100ABCDEF125001, other = 0x0100000000010000;

	Check(TaggedTitle("Game [0100ABCDEF124800][v65536].nsp") == update && TaggedTitle("Game (USA).xci") == 0 &&
			  TaggedTitle("Game [v0][0100abcdef125001].nsp") == dlc && TaggedTitle("[0200ABCDEF124800].nsp") == 0,
		"a dump's title ID read from its name");
	Check(IsAddOnOf(update, game) && IsAddOnOf(dlc, game) && !IsAddOnOf(game, game) && !IsAddOnOf(game + 1, game) &&
			  !IsAddOnOf(other | 0x800, game),
		"the game's update and DLC are its add-ons; the game, its other programs and another game's are not");

	const std::string roms = "/data/eden/roms", usb = "/mnt/usb0/Switch";
	Check(GameFolder("/data/eden/roms/Zelda/Zelda.nsp", {roms, ""}) == "/data/eden/roms/Zelda" &&
			  GameFolder("/data/eden/roms/Zelda/dlc/Zelda.nsp", {roms + "/", ""}) == "/data/eden/roms/Zelda",
		"a game in its own folder in Eden's roms/, as before");
	Check(GameFolder("/data/eden/roms/Zelda.nsp", {roms, ""}).empty() && GameFolder("/data/eden/romsx/Zelda/Zelda.nsp", {roms, ""}).empty(),
		"a game directly in roms/, or in a folder only named like it, has no folder of its own");
	Check(GameFolder("/mnt/usb0/Switch/Zelda/Zelda.nsp", {roms, usb}) == "/mnt/usb0/Switch/Zelda" &&
			  GameFolder("/mnt/usb0/Switch/Zelda.nsp", {roms, usb}).empty(),
		"a game in its own folder in the launcher's games folder on a USB drive: its update and DLC were left out before");
	Check(GameFolder("/data/eden/roms/Switch/Zelda/Zelda.nsp", {roms, "/data/eden/roms/Switch"}) == "/data/eden/roms/Switch/Zelda" &&
			  GameFolder("/data/eden/roms/Zelda.nsp", {roms, "/data/eden"}).empty(),
		"the root closest to the game decides: never all of roms/ for a game in it");
	Check(GameFolder("/mnt/usb0/Zelda/Zelda.nsp", {roms, "/"}).empty() && GameFolder("/mnt/usb0/Zelda/Zelda.nsp", {roms, "relative"}).empty(),
		"no whole drive and no relative folder as a games folder");

	// a flat folder: the game, its update and DLC beside it, and other games' files
	const fs::path flat = work / "flat";
	for (const char* name : {"Zelda [0100ABCDEF124000][v0].nsp", "Zelda [0100ABCDEF124800][v65536].nsp", "Zelda DLC [0100ABCDEF125001].nsp",
			 "Zelda [0100ABCDEF124000].xci", "Mario [0100000000010000].nsp", "Mario [0100000000010800].nsp", "Untagged update.NSP",
			 "Zelda.png", ".Zelda [0100ABCDEF124800].nsp", "sub/Zelda [0100ABCDEF125002].nsp"})
		Put(flat / name);
	std::vector<fs::path> named, unnamed;
	Beside(flat / "Zelda [0100ABCDEF124000][v0].nsp", game, named, unnamed);
	Check(Names(named) == std::vector<std::string>{"Zelda DLC [0100ABCDEF125001].nsp", "Zelda [0100ABCDEF124800][v65536].nsp"},
		"beside the game: its update and DLC by their names, not its other copy, another game's files, hidden files or subfolders");
	Check(Names(unnamed) == std::vector<std::string>{"Untagged update.NSP"}, "a file whose name gives no title ID is read to tell");
	named.clear(), unnamed.clear();
	Beside(flat / "Zelda [0100ABCDEF124000][v0].nsp", 0, named, unnamed);
	Check(named.empty() && unnamed.empty(), "nothing for a game whose title ID is not known");
	Beside(work / "missing/Zelda.nsp", game, named, unnamed);
	Check(named.empty() && unnamed.empty(), "nothing for a folder that is not there");

	// the files PS5-ECAM gave the game (--set addon_files, #106)
	Check(ListedFiles("/mnt/usb0/Switch/Updates/Zelda.nsp|/mnt/usb0/Switch/DLC/Zelda DLC.XCI") ==
			  std::vector<std::string>{"/mnt/usb0/Switch/Updates/Zelda.nsp", "/mnt/usb0/Switch/DLC/Zelda DLC.XCI"},
		"the files PS5-ECAM gave, in its order");
	Check(ListedFiles("relative/Zelda.nsp|/mnt/usb0/Zelda.txt||/|/mnt/usb0/Zelda.nsp|/mnt/usb0/Zelda.nsp") ==
			  std::vector<std::string>{"/mnt/usb0/Zelda.nsp"},
		"only absolute NSP and XCI paths, each once");
	Check(ListedFiles("").empty() && ListedFiles("|").empty(), "none from an empty list");
	std::string many;
	for (int i = 0; i < 70; i++)
		many += "/mnt/usb0/DLC " + std::to_string(i) + ".nsp|";
	Check(ListedFiles(many).size() == 64 && ListedFiles("/" + std::string(1100, 'x') + ".nsp").empty(), "at most 64, each under 1024 characters");

	std::cout << (failures ? "FAILED\n" : "OK\n");
	return failures ? 1 : 0;
}
