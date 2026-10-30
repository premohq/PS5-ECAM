// SPDX-License-Identifier: GPL-3.0-or-later
// EmulationStation as the other frontend (port/esde): ES-DE's systems and game lists from the launcher's
// library, as ES-DE reads them (each system with the four fields it requires, each game within its
// system's folder and there), what ES-DE knows of a game kept, favourites from whichever side changed
// them, and the files a game goes over and back in.
#include "esde/esde.h"
#include "esde/handoff.h"
#include "app/paths.h"
#include "frontend/settings.h"

#include <pugixml.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
static int failures = 0;
static void Check(bool ok, const char* what)
{
	std::cout << (ok ? "PASS  " : "FAIL  ") << what << '\n';
	failures += !ok;
}
static void Put(const fs::path& file, const std::string& text = "game")
{
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary) << text;
}
static pugi::xml_node GameNamed(pugi::xml_document& document, const std::string& relative)
{
	for (pugi::xml_node game : document.child("gameList").children("game"))
		if (relative == game.child_value("path"))
			return game;
	return {};
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const fs::path root = argv[1];
	fs::create_directories(root);

	// the sides' systems, ES-DE's names for them (its themes' art)
	bool named = true;
	for (int side = 0; side < 14; side++)
		named = named && ps5esde::SystemName(side) && ps5esde::SideNamed(ps5esde::SystemName(side)) == side;
	Check(named && !ps5esde::SystemName(14) && ps5esde::SideNamed("psx") == -1, "each of the fourteen sides has an ES-DE system of its own");
	Check(std::string(ps5esde::SystemName(0)) == "wiiu" && std::string(ps5esde::SystemName(3)) == "switch" &&
			  std::string(ps5esde::SystemName(4)) == "gc" && std::string(ps5esde::SystemName(12)) == "virtualboy",
		"the names ES-DE's themes know: wiiu, switch, gc, virtualboy");

	// a system's folder: the deepest that holds every game, as ES-DE reads a game only within it
	Check(ps5esde::CommonFolder({"/data/a/b/x.nds", "/data/a/c/y.nds"}, "/f") == "/data/a" &&
			  ps5esde::CommonFolder({"/data/a/b/x.nds"}, "/f") == "/data/a/b" && ps5esde::CommonFolder({}, "/f") == "/f" &&
			  ps5esde::CommonFolder({"/data/x.nds", "/mnt/usb0/y.nds"}, "/f") == "/",
		"a system's folder holds every game it lists");

	// ES-DE's own systems file, for full names, platforms and themes
	const std::string reference = (root / "es_systems.xml").string();
	Put(reference, "<systemList><system><name>nds</name><fullname>Nintendo DS (ES-DE)</fullname><platform>nds</platform>"
				   "<theme>nds</theme></system></systemList>");

	const fs::path games = root / "games", home = root / "home";
	Put(games / "ds/Mario Kart DS.nds");
	Put(games / "ds/more/Kirby.nds");
	Put(games / "ds/Old.nds");
	Put(games / "switch/Game.nsp");
	const std::string ds = (games / "ds").string();
	// what ES-DE already knows: a scraped description and a play count for one game, a folder entry,
	// and a game the launcher no longer lists
	Put(home / "ES-DE/gamelists/nds/gamelist.xml",
		"<gameList><folder><path>./more</path><name>More</name></folder>"
		"<game><path>./Mario Kart DS.nds</path><name>Mario Kart DS</name><desc>Racing.</desc><playcount>3</playcount>"
		"<lastplayed>20991231T000000</lastplayed></game>"
		"<game><path>./Gone.nds</path><name>Gone</name></game></gameList>");
	std::vector<ps5esde::System> systems(2);
	systems[0].side = 2;
	systems[0].folder = ds;
	systems[0].games = {{(games / "ds/Mario Kart DS.nds").string(), "Mario Kart DS", 0x0101, true, false, 1600000000, 90},
		{(games / "ds/more/Kirby.nds").string(), "Kirby", 0x0102, false, true, 0, 0}, {(games / "ds/Old.nds").string(), "Old", 0x0103, false, false, 0, 0}};
	systems[1].side = 3;
	systems[1].folder = (games / "switch").string();
	systems[1].games = {{(games / "switch/Game.nsp").string(), "Game", 0x0100000000010000, false, false, 0, 0}};
	const std::string eboot = "/data/homebrew/PPSA99360/eboot.bin";
	const ps5esde::Written first = ps5esde::WriteLibrary(home.string(), reference, eboot, systems);
	Check(first.error.empty() && first.systems == 2 && first.games == 4, "both systems and their four games written");

	pugi::xml_document systemsFile;
	Check(systemsFile.load_file((home / "ES-DE/custom_systems/es_systems.xml").string().c_str()), "ES-DE's custom systems file is XML");
	bool complete = true;
	int count = 0;
	for (pugi::xml_node system : systemsFile.child("systemList").children("system"))
	{
		count++;
		for (const char* field : {"name", "fullname", "path", "extension", "command"})
			complete = complete && !std::string(system.child_value(field)).empty();
	}
	Check(count == 2 && complete, "every system has the name, full name, path, extensions and command ES-DE requires");
	pugi::xml_node nds = systemsFile.child("systemList").find_child_by_attribute("system", "", "");
	for (pugi::xml_node system : systemsFile.child("systemList").children("system"))
		if (std::string(system.child_value("name")) == "nds")
			nds = system;
	Check(std::string(nds.child_value("fullname")) == "Nintendo DS (ES-DE)" && std::string(nds.child_value("theme")) == "nds" &&
			  std::string(nds.child_value("path")) == ds,
		"a system's full name and theme from ES-DE's own file, its folder the games'");
	Check(std::string(nds.child_value("command")) == eboot + " --rom %ROM%" && std::string(nds.child("command").attribute("label").value()) == "PS5-ECAM",
		"its command names PS5-ECAM's eboot, which ES-DE looks for, with the game");

	pugi::xml_document list;
	Check(list.load_file((home / "ES-DE/gamelists/nds/gamelist.xml").string().c_str()), "the DS's game list is XML");
	bool within = true;
	for (pugi::xml_node game : list.child("gameList").children("game"))
	{
		const std::string path = game.child_value("path");
		within = within && path.rfind("./", 0) == 0 && fs::exists(fs::path(ds) / path.substr(2));
	}
	Check(within, "each game's path is within its system's folder, and there");
	const pugi::xml_node mario = GameNamed(list, "./Mario Kart DS.nds");
	Check(std::string(mario.child_value("desc")) == "Racing." && std::string(mario.child_value("playcount")) == "3",
		"what ES-DE knows of a game (its scraped description, its play count) is kept");
	Check(std::string(mario.child_value("lastplayed")) == "20991231T000000" && std::string(mario.child_value("playtime")) == "5400",
		"the later last play and the longer play time");
	Check(std::string(mario.child_value("favorite")) == "true" && std::string(GameNamed(list, "./more/Kirby.nds").child_value("hidden")) == "true",
		"the library's favourites and hidden games");
	Check(!GameNamed(list, "./Gone.nds") && list.child("gameList").child("folder"), "a game the library no longer has is left out, folders kept");
	Check(std::string(GameNamed(list, "./Old.nds").child_value("name")) == "Old", "a game new to ES-DE has the library's name");

	// favourites: ES-DE's change goes back to the library; the library's goes to ES-DE
	{
		pugi::xml_document edited;
		edited.load_file((home / "ES-DE/gamelists/nds/gamelist.xml").string().c_str());
		GameNamed(edited, "./Old.nds").append_child("favorite").text().set("true");
		edited.save_file((home / "ES-DE/gamelists/nds/gamelist.xml").string().c_str());
	}
	systems[0].games[0].favourite = false; // the library took Mario Kart's away
	const ps5esde::Written second = ps5esde::WriteLibrary(home.string(), reference, eboot, systems);
	list.load_file((home / "ES-DE/gamelists/nds/gamelist.xml").string().c_str());
	Check(second.fromEsde.size() == 1 && second.fromEsde[0].side == 2 && second.fromEsde[0].titleId == 0x0103 && second.fromEsde[0].on,
		"a favourite set in ES-DE is said back to the library");
	Check(std::string(GameNamed(list, "./Old.nds").child_value("favorite")) == "true" && !GameNamed(list, "./Mario Kart DS.nds").child("favorite"),
		"a favourite kept from ES-DE, one taken away in the library gone from ES-DE");
	systems[0].games[2].favourite = true; // the library as it now is
	const ps5esde::Written third = ps5esde::WriteLibrary(home.string(), reference, eboot, systems);
	Check(third.fromEsde.empty(), "nothing changed since: nothing said back");

	// a missing reference: ES-DE's names all the same
	const ps5esde::Written bare = ps5esde::WriteLibrary((root / "bare").string(), (root / "missing.xml").string(), eboot, systems);
	pugi::xml_document bareSystems;
	bareSystems.load_file((root / "bare/ES-DE/custom_systems/es_systems.xml").string().c_str());
	Check(bare.error.empty() && std::string(bareSystems.child("systemList").child("system").child_value("fullname")) == "Nintendo DS" &&
			  std::string(bareSystems.child("systemList").child("system").child_value("theme")) == "nds",
		"without ES-DE's own file, its system names and themes all the same");

	// the files a game goes over and back in
	ecam_esde::Game request;
	request.system = "favorites";
	request.path = "/data/games/A game\nwith a line.nds";
	request.started = 1700000000;
	const std::string file = (root / "game.txt").string();
	ecam_esde::Game read;
	Check(ecam_esde::Write(file, request) && ecam_esde::Read(file, read) && read.system == "favorites" &&
			  read.path == "/data/games/A game with a line.nds" && read.started == 1700000000 && !read.taken,
		"a request is read back as written, on one line");
	read.taken = 1;
	read.seconds = 125;
	read.status = -1;
	read.error = "It is not in the library";
	ecam_esde::Game taken;
	Check(ecam_esde::Write(file, read) && ecam_esde::Take(file, taken) && taken.taken == 1 && taken.seconds == 125 && taken.status == -1 &&
			  taken.error == "It is not in the library" && !fs::exists(file),
		"a result is taken once");
	Put(file, "system=nds\npath=relative.nds\n");
	Check(!ecam_esde::Read(file, taken), "a game without a full path is no request");

	// the setting, in the data folder's settings file
	fs::create_directories(ps5paths::Root());
	ps5settings::Launcher settings;
	settings.ui.emulationStation = true;
	Check(ps5settings::Save(settings) && ps5settings::Load().ui.emulationStation, "Start in EmulationStation is kept");
	return failures ? 1 : 0;
}
