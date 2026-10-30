// SPDX-License-Identifier: GPL-3.0-or-later
// A game folder that holds its game as an ExeFS and a RomFS (port/eden/exefs.h): found in a folder by
// its main, its title ID from main.npdm, and a Switch 2 title told from a Switch one by its ACID's place.
#include "eden/exefs.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
static int failures = 0;
static void Check(bool ok, const char* what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << '\n'; failures += !ok; }

// An npdm as a dump has it: its header, then its ACI0, then its ACID after a signature and a key of
// 0x200 bytes (a Switch title) or 0x300 (a Switch 2 title)
static std::string NpdmFile(uint64_t titleId, bool switch2)
{
	const uint32_t aci = 0x80, acid = 0x100, signature = switch2 ? 0x300 : 0x200;
	std::string data(acid + signature + 0x40, '\0');
	std::memcpy(data.data(), "META", 4);
	std::memcpy(data.data() + 0x70, &aci, 4);
	std::memcpy(data.data() + 0x78, &acid, 4);
	std::memcpy(data.data() + aci, "ACI0", 4);
	std::memcpy(data.data() + aci + 0x10, &titleId, 8);
	std::memcpy(data.data() + acid + signature, "ACID", 4);
	return data;
}

static void Put(const fs::path& file, const std::string& text)
{
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary) << text;
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const fs::path root = argv[1];
	using namespace ps5eden::exefs;

	Check(IsMain("/data/eden/roms/Pokopia/exefs/main") && IsMain("Pokopia/ExeFS/main") && !IsMain("exefs/main") && !IsMain("/exefs/main") &&
			  !IsMain("Pokopia/exefs/rtld") && !IsMain("Pokopia/main") && !IsMain("Game.nsp"),
		"a game folder's ExeFS main, exefs/ in any case, and nothing else");

	Put(root / "Pokopia/exefs/main", "code");
	Put(root / "Pokopia/exefs/main.npdm", NpdmFile(0x0400A3A01EE4C000, true));
	Put(root / "Pokopia/romfs/data.bin", "data");
	Put(root / "Dumped Switch Game/EXEFS/main", "code");
	Put(root / "Dumped Switch Game/EXEFS/main.npdm", NpdmFile(0x0100000000010000, false));
	Put(root / "No npdm/exefs/main", "code");
	Put(root / "Not a game/readme.txt", "text");
	Put(root / "Broken/exefs/main", "code");
	Put(root / "Broken/exefs/main.npdm", "META but nothing after it");

	const fs::path pokopia = MainIn(root / "Pokopia"), dumped = MainIn(root / "Dumped Switch Game");
	Check(pokopia == root / "Pokopia/exefs/main" && dumped == root / "Dumped Switch Game/EXEFS/main" && IsMain(pokopia) && IsMain(dumped),
		"a folder's ExeFS main found, whatever the case of exefs/");
	Check(MainIn(root / "No npdm").empty() && MainIn(root / "Not a game").empty() && MainIn(root / "Missing").empty(),
		"a folder without main and main.npdm in its exefs/ holds no game");

	Npdm two, one, broken;
	Check(Read(pokopia, two) && two.switch2 && two.titleId == 0x0400A3A01EE4C000, "a Switch 2 title: its ID, and its ACID 0x300 bytes in");
	Check(Read(dumped, one) && !one.switch2 && one.titleId == 0x0100000000010000, "a Switch title dumped as an ExeFS: its ID, and its ACID 0x200 bytes in");
	Check(!Read(MainIn(root / "Broken"), broken) && !Read(root / "No npdm/exefs/main", broken), "a short or missing npdm is not read");

	std::string truncated = NpdmFile(0x0400380020056000, true);
	truncated.resize(0x100 + 0x300 + 2);
	Put(root / "Cut short/exefs/main", "code");
	Put(root / "Cut short/exefs/main.npdm", truncated);
	Check(!Read(root / "Cut short/exefs/main", broken), "an npdm cut short in its ACID is not read past its end");
	return failures ? 1 : 0;
}
