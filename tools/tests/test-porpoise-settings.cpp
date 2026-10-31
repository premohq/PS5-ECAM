// SPDX-License-Identifier: GPL-3.0-or-later
// Porpoise's settings files as PS5-ECAM writes them (port/retro/porpoise_settings.h). Prints PASS
// lines; exits 1 on a failure.
#include "retro/porpoise_settings.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool ok, const char* what)
{
	std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
	failures += !ok;
}

static std::string Text(const fs::path& file)
{
	std::ifstream in(file);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

static void Disc(const fs::path& file, const char* magic, size_t at, const char* id)
{
	std::string data(at + 0x20, '\0');
	if (magic)
		std::memcpy(data.data(), magic, 4);
	std::memcpy(data.data() + at, id, 6);
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary) << data;
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const fs::path root = argv[1];
	fs::remove_all(root);
	std::string error;

	Check(ps5porpoise::Data() == "/data/porpoise" && ps5porpoise::SettingsFile() == "/data/porpoise/settings.ini", "Porpoise's folder and file");

	// a settings.ini as Porpoise writes it: its own lines kept, ours changed in place or added
	const fs::path ini = root / "settings.ini";
	fs::create_directories(root);
	std::ofstream(ini) << "# Porpoise settings (written by the Settings screen)\nsettings_version = 18\nresolution = 3\nui_theme = 7\n"
						  "layout1 = 0 1 2 3\nfolder = /mnt/usb0/games\nvolume = 10\n";
	Check(ps5porpoise::Write(ini.string(), {{"resolution", "5"}, {"volume", "4"}, {"wii_pointer", "2"}}, error), "written");
	const std::string text = Text(ini);
	Check(text.find("resolution = 5\n") != std::string::npos && text.find("volume = 4\n") != std::string::npos &&
		text.find("resolution = 3") == std::string::npos, "a key changed where it was");
	Check(text.find("ui_theme = 7\n") != std::string::npos && text.find("layout1 = 0 1 2 3\n") != std::string::npos &&
		text.find("folder = /mnt/usb0/games\n") != std::string::npos && text.find("settings_version = 18\n") != std::string::npos,
		"Porpoise's own lines kept: its theme, presets, folders, version");
	Check(text.rfind("wii_pointer = 2\n") == text.size() - std::strlen("wii_pointer = 2\n"), "a key it did not have added at the end");
	const auto values = ps5porpoise::Read(ini.string());
	Check(values.at("resolution") == "5" && values.at("ui_theme") == "7" && !values.count("# Porpoise settings (written by the Settings screen)"),
		"read back as Porpoise reads it");
	Check(!fs::exists(root / "settings.ini.tmp"), "written beside it, then put in its place");

	// a game's own file: made with the header, its keys taken out again, removed once empty
	const fs::path game = root / "game-settings/GALE01.ini";
	Check(ps5porpoise::Write(game.string(), {{"resolution", "2"}, {"cpu_clock", "7"}}, error) &&
		Text(game).rfind("# This game's own settings", 0) == 0, "a game's own file, with its header");
	Check(ps5porpoise::Remove(game.string(), {"resolution"}, error) && ps5porpoise::Read(game.string()).size() == 1, "a key back to every game's");
	Check(ps5porpoise::Remove(game.string(), {"cpu_clock"}, error) && !fs::exists(game), "the file gone once it has nothing of its own");

	// a game's key: its disc's ID, in each image format Porpoise reads, else its file's name
	Disc(root / "games/Melee.iso", nullptr, 0, "GALE01");
	Disc(root / "games/Mario Galaxy.wbfs", "WBFS", 0x200, "RMGE01");
	Disc(root / "games/Twilight.rvz", "RVZ\x01", 0x58, "RZDE01");
	Disc(root / "games/Sunshine.ciso", "CISO", 0x8000, "GMSE01");
	Disc(root / "games/broken.iso", nullptr, 0, "ab\x01\x02zz");
	Check(ps5porpoise::GameKey((root / "games/Melee.iso").string()) == "GALE01" && ps5porpoise::GameKey((root / "games/Mario Galaxy.wbfs").string()) == "RMGE01" &&
		ps5porpoise::GameKey((root / "games/Twilight.rvz").string()) == "RZDE01" && ps5porpoise::GameKey((root / "games/Sunshine.ciso").string()) == "GMSE01",
		"a disc's ID from an ISO, a WBFS, an RVZ and a CISO");
	Check(ps5porpoise::GameKey((root / "games/broken.iso").string()) == "broken.iso" && ps5porpoise::GameKey((root / "missing.gcz").string()) == "missing.gcz",
		"no ID: the file's name, as Porpoise falls back");
	Check(ps5porpoise::GameFile((root / "games/Melee.iso").string()) == "/data/porpoise/game-settings/GALE01.ini", "a game's own file, by its ID");
	return failures ? 1 : 0;
}
