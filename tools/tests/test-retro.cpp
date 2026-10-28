// SPDX-License-Identifier: GPL-3.0-or-later
#include "retro/library.h"
#include "retro/launch.h"
#include "retro/runtime/ecam_bridge.h"
#include "frontend/launcher.h"
#include "app/paths.h"
#include "app/catalog.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
static int failures = 0;
static std::string called;
static std::vector<std::string> handed;
namespace ps5emu { void LoadExecApp(const std::string& program, const std::vector<std::string>& args, bool, const std::string&) { called = program; handed = args; } }
static void Check(bool ok, const char* text) { std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; failures += !ok; }
static void Put(const fs::path& file, const std::string& text = "fixture") { fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary) << text; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	fs::create_directories(argv[1]); const auto root = fs::canonical(argv[1]);
	std::string error;
	Check(ps5launcher::kSideCount == 8, "eight stable side indices");
	for (int i = 0; i < 8; i++) Check((int)ps5launcher::SideNamed(ps5launcher::SideName((ps5launcher::System)i)) == i, "side names round-trip without changing existing indices");
	Put(root / "roms/A.NES"); Put(root / "roms/B.z64"); Put(root / "roms/C.iso"); Put(root / "roms/D.wad"); Put(root / "roms/not-a-game.txt");
	Put(root / "roms/nested/E.unif"); Put(root / "roms/one/two/three/four/five/too-deep.nes"); fs::create_directory_symlink(root / "roms", root / "roms/loop");
	const auto nes = ps5retro::Scan("nes", (root / "roms").string(), error);
	Check(nes.size() == 2 && error.empty(), "NES scan accepts upper-case/nested formats and ignores symlinks");
	Check(ps5retro::Scan("n64", (root / "roms").string(), error).size() == 1, "N64 scan isolates its ROM formats");
	Check(ps5retro::Scan("gamecube", (root / "roms").string(), error).size() == 1, "GameCube scan excludes Wii channels");
	Check(ps5retro::Scan("wii", (root / "roms").string(), error).size() == 2, "Wii scan includes channels");
	Check(ps5retro::Scan("invalid", (root / "roms").string(), error).empty() && !error.empty(), "unknown systems are refused");
	const auto again = ps5retro::Scan("nes", (root / "roms").string(), error);
	Check(nes[0].titleId == again[0].titleId && (nes[0].titleId >> 56) == 0x60, "stable IDs use a separate synthetic namespace");
	ps5retro::StartScan(7, (root / "roms").string()); ps5retro::FinishScans();
	Check(ps5retro::ListGames(7).size() == 2 && !ps5retro::Scanning(7), "background scanner joins before handoff");
	ps5paths::detail::AppDirSlot() = (root / "app").string();
	Check(!ps5retro::Available(7) && !ps5retro::Launch(7, nes[0].path.string(), error), "missing runtime fails before handoff");
	Put(root / "app/retro/nes/eboot.bin");
	ps5retro::Launch(7, nes[0].path.string(), error);
	Check(called == "retro/nes/eboot.bin" && handed[0] == "--rom" && handed[1] == nes[0].path.string(), "handoff selects bundled runtime and exact ROM path");
	Check(ps5retro::LaunchArguments(7, "/game\n--return", "/app/eboot.bin").empty(), "line-injection ROM paths refused");
	ps5retro::LaunchLibrary(7, error);
	Check(called == "retro/nes/eboot.bin" && handed[0] == "--ecam-library", "native settings frontend has an explicit return handoff");
	Check(ecam_bridge::Parse({"--ecam-library", "--return", "/app/eboot.bin"}).valid, "library-only request does not require a fake ROM");
	Check(ps5retro::LaunchArguments(7, "/game.nes", "/app/eboot.bin\n--rom").empty(), "return-path line injection refused");
	ecam_bridge::request = ecam_bridge::Parse({"--ecam-library", "--return", "/app/eboot.bin"});
	const char* retained = ecam_bridge::ReturnProgram(); ecam_bridge::request.returnProgram.clear();
	Check(std::string(retained) == "/app/eboot.bin", "return path survives runtime allocator cleanup");
	const auto parsed = ecam_bridge::Parse({"--rom", "/games/a game.nes", "--return", "/app/eboot.bin"});
	Check(parsed.valid && parsed.rom == "/games/a game.nes", "bridge preserves spaces without shell parsing");
	Check(!ecam_bridge::Parse({"--rom", "relative.nes", "--return", "/app/eboot.bin"}).valid, "bridge requires explicit absolute paths");
	Put(root / "request", "--rom\n/games/a game.nes\n--return\n/app/eboot.bin\n");
	Check(ecam_bridge::Read((root / "request").string()).valid && !fs::exists(root / "request"), "argc-loss request is consumed once");
	Put(root / "request", "--rom\nrelative.nes\n");
	Check(!ecam_bridge::Read((root / "request").string()).valid, "incomplete request does not start another game");
	ps5settings::Launcher settings;
	settings.ui.sides = 255; settings.ui.librarySort[7] = 3; settings.retro[3].gamesFolder = (root / "roms").string();
	fs::create_directories(ps5paths::Root());
	Check(ps5settings::Save(settings), "eight-side settings serialize");
	const auto loaded = ps5settings::Load();
	Check(loaded.ui.sides == 255 && loaded.ui.librarySort[7] == 3 && loaded.retro[3].gamesFolder == settings.retro[3].gamesFolder, "new side settings persist independently");
	Put(ps5paths::LauncherSettings(), "{\"ui\":{\"sides\":15,\"librarySort\":[1,2,3,0]},\"n3ds\":{\"gamesFolder\":\"/legacy/3ds\"}}");
	const auto legacy = ps5settings::Load();
	Check(legacy.ui.sides == 15 && legacy.ui.librarySort[2] == 3 && legacy.ui.librarySort[7] == 0 && legacy.n3ds.gamesFolder == "/legacy/3ds", "old four-side settings retain original values and zero-fill new array slots");
	ps5catalog::Replace(ps5catalog::System::Nes, nes); ps5catalog::SetFavourite(ps5catalog::System::Nes, nes[0].titleId, true);
	Check(ps5catalog::Save() && ps5catalog::Games(ps5catalog::System::Nes).size() == 2 && ps5catalog::Games(ps5catalog::System::WiiU).empty(), "new catalogue is separate from Wii U saves/library");
	return failures ? 1 : 0;
}
