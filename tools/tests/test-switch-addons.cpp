// SPDX-License-Identifier: GPL-3.0-or-later
// A Switch game's update and DLC as its page shows them (port/eden/addons.h): paired by base title ID,
// the newest update's version, each DLC once.
#include "eden/addons.h"
#include <iostream>

using namespace ps5eden::addons;
static int failures = 0;
static void Check(bool ok, const char* what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << '\n'; failures += !ok; }
static bool Made(uint64_t title) { return (title >> 56) == 0x5A; }
static ps5emu::Game Game(uint64_t titleId)
{
	ps5emu::Game game;
	game.titleId = titleId;
	return game;
}

int main()
{
	Check(Older("1.9.2", "1.10.0") && !Older("1.10.0", "1.9.2") && Older("1.2", "1.2.1") && !Older("2.0.0", "2.0.0") && Older("1.0.0", "1.0.0b"),
		"versions compared number by number");
	Check(IsUpdateId(0x0100ABCDEF124800) && !IsUpdateId(0x0100ABCDEF124000) && IsDlcId(0x0100ABCDEF125001) && !IsDlcId(0x0100ABCDEF124800) &&
			  !IsDlcId(0x0100ABCDEF124000),
		"a name's title ID tells an update from DLC and from the game");

	constexpr uint64_t zelda = 0x0100ABCDEF124000, mario = 0x0100000000010000;
	std::vector<ps5emu::Game> games{Game(zelda), Game(mario), Game(0x5A00000012345678), Game(0x0100777700002000)};
	games[1].hasUpdate = true; // what an earlier scan said is replaced
	games[1].dlcCount = 3;
	const std::vector<File> files{
		{0x0100ABCDEF124800, true, "1.2.0", ""},
		{0x0100ABCDEF124800, true, "1.10.1", ""}, // a newer update beside the older
		{0x0100ABCDEF124800, true, "", ""},		  // one whose version could not be read
		{0x0100ABCDEF125001, false, "", ""},
		{0x0100ABCDEF125002, false, "", ""},
		{0x0100ABCDEF125002, false, "", ""}, // the same DLC in an NSP and an XCI
		{0x0100000000011001, false, "", ""},  // another game's (0x...10000 + 0x1000 + 1)
	};
	Pair(games, files, Made);
	Check(games[0].hasUpdate && games[0].versionName == "1.10.1" && games[0].dlcCount == 2, "the newest update's version, and each DLC once");
	Check(!games[1].hasUpdate && games[1].versionName.empty() && games[1].dlcCount == 1, "another game's DLC its own; an earlier scan's facts replaced");
	Check(!games[2].hasUpdate && games[2].dlcCount == 0, "nothing for a game whose title ID was made from its path");
	Check(!games[3].hasUpdate && games[3].dlcCount == 0, "nothing for a game with none");
	std::vector<ps5emu::Game> unread{Game(zelda)};
	Pair(unread, {{0x0100ABCDEF124800, true, "", ""}}, Made);
	Check(unread[0].hasUpdate && unread[0].versionName.empty(), "an update known by its name alone: there, its version not shown");

	// the files Eden is handed (#106): a game's, updates first, each once, within the length
	const std::vector<File> placed{
		{0x0100ABCDEF125001, false, "", "/mnt/usb0/Switch/DLC/Zelda DLC.nsp"},
		{0x0100ABCDEF124800, true, "1.2.0", "/mnt/usb0/Switch/Updates/Zelda v1.2.0.nsp"},
		{0x0100ABCDEF124800, true, "1.2.0", "/mnt/usb0/Switch/Updates/Zelda v1.2.0.nsp"}, // read twice
		{0x0100ABCDEF124800, true, "1.0.1", ""},										  // in Eden's updates/
		{0x0100ABCDEF125002, false, "", "/mnt/usb0/Switch/DLC/Zel|da.nsp"},
		{0x0100000000011001, false, "", "/mnt/usb0/Switch/DLC/Mario DLC.nsp"},
	};
	Check(FilesOf(zelda, placed, 1000) ==
			  std::vector<std::string>{"/mnt/usb0/Switch/Updates/Zelda v1.2.0.nsp", "/mnt/usb0/Switch/DLC/Zelda DLC.nsp"},
		"a game's own files for Eden, updates first, each once; none in its own folders or with a '|'");
	Check(FilesOf(zelda + 0x800, placed, 1000).size() == 2, "found by the game's base title ID");
	Check(FilesOf(zelda, placed, 45) == std::vector<std::string>{"/mnt/usb0/Switch/Updates/Zelda v1.2.0.nsp"}, "as many as fit the length");
	Check(FilesOf(0, placed, 1000).empty() && FilesOf(0x0100777700002000, placed, 1000).empty(), "none for a game with none");

	std::cout << (failures ? "FAILED\n" : "OK\n");
	return failures ? 1 : 0;
}
