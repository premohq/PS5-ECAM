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
namespace ps5lang { const char* Tr(const char* english) { return english; } std::string Tr(const std::string& english) { return english; } }
static void Check(bool ok, const char* text) { std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; failures += !ok; }
static void Put(const fs::path& file, const std::string& text = "fixture") { fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary) << text; }
static std::string DiscHeader(bool wii)
{
	std::string header(0x20, '\0');
	header.replace(wii ? 0x18 : 0x1c, 4, wii ? std::string("\x5d\x1c\x9e\xa3", 4) : std::string("\xc2\x33\x9f\x3d", 4));
	return header;
}
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	fs::create_directories(argv[1]); const auto root = fs::canonical(argv[1]);
	std::string error;
	Check(ps5launcher::kSideCount == 14, "fourteen stable side indices");
	for (int i = 0; i < ps5launcher::kSideCount; i++) Check((int)ps5launcher::SideNamed(ps5launcher::SideName((ps5launcher::System)i)) == i, "side names round-trip without changing existing indices");
	Put(root / "roms/A.NES", std::string("NES\x1a", 4) + std::string(12, '\0')); Put(root / "roms/B.z64"); Put(root / "roms/C.iso", DiscHeader(false)); Put(root / "roms/D.wad"); Put(root / "roms/not-a-game.txt");
	Put(root / "roms/nested/E.unif"); Put(root / "roms/one/two/three/four/five/too-deep.nes"); fs::create_directory_symlink(root / "roms", root / "roms/loop");
	const auto nes = ps5retro::Scan("nes", (root / "roms").string(), error);
	Check(nes.size() == 2 && error.empty(), "NES scan accepts upper-case/nested formats and ignores symlinks");
	Check(ps5retro::Scan("n64", (root / "roms").string(), error).size() == 1, "N64 scan isolates its ROM formats");
	Check(ps5retro::Scan("gamecube", (root / "roms").string(), error).size() == 1, "GameCube scan excludes Wii channels");
	Check(ps5retro::Scan("wii", (root / "roms").string(), error).size() == 1, "Wii scan excludes GameCube discs and includes channels");
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
	Check(ps5retro::LaunchArguments(7, "/game.nes", "relative/eboot.bin").empty(), "relative return path refused before handoff");
	Check(ps5retro::LaunchArguments(7, "/game.nes", "/app/not-eboot.bin").empty(), "return path must name the ECAM executable");
	ecam_bridge::request = ecam_bridge::Parse({"--ecam-library", "--return", "/app/eboot.bin"});
	const char* retained = ecam_bridge::ReturnProgram(); ecam_bridge::request.returnProgram.clear();
	Check(std::string(retained) == "/app/eboot.bin", "return path survives runtime allocator cleanup");
	const auto parsed = ecam_bridge::Parse({"--rom", "/games/a game.nes", "--return", "/app/eboot.bin"});
	Check(parsed.valid && parsed.rom == "/games/a game.nes", "bridge preserves spaces without shell parsing");
	Check(!ecam_bridge::Parse({"--rom", "relative.nes", "--return", "/app/eboot.bin"}).valid, "bridge requires explicit absolute paths");
	Put(root / "request", "--rom\n/games/a game.nes\n--return\n/app/eboot.bin\n");
	Check(ecam_bridge::Read((root / "request").string()).valid && !fs::exists(root / "request"), "argc-loss request is consumed once");
	Check(!ecam_bridge::Read((root / "request").string()).valid, "consumed request cannot be replayed");
	Put(root / "linked-request-target", "--rom\n/games/a game.nes\n--return\n/app/eboot.bin\n");
	fs::create_symlink(root / "linked-request-target", root / "linked-request");
	Check(!ecam_bridge::Read((root / "linked-request").string()).valid && fs::exists(root / "linked-request-target"), "request symlink is refused and its target is preserved");
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
	Put(root / "roms/empty.nes", "");
	Check(!ps5retro::ValidateRom(7, (root / "roms/empty.nes").string(), error) && !error.empty(), "empty ROM fails preflight");
	Check(!ps5retro::ValidateRom(6, (root / "roms/B.z64").string(), error), "invalid N64 header fails preflight");
	Put(root / "roms/B.z64", std::string("\x80\x37\x12\x40", 4) + std::string(60, '\0'));
	Check(ps5retro::ValidateRom(6, (root / "roms/B.z64").string(), error), "valid N64 byte-order magic passes preflight");
	Check(!ps5retro::ValidateRom(7, (root / "roms/missing.fds").string(), error), "missing disk fails preflight");
	Put(root / "roms/fake.zip", "bad archive");
	Check(!ps5retro::ValidateRom(7, (root / "roms/fake.zip").string(), error), "invalid ZIP fails preflight");
	Put(root / "roms/A.png");
	Check(ps5retro::CoverPath(7, nes[0]) == (root / "roms/A.png").string(), "local artwork reused without a download");
	Put(root / "roms/.ecam-library.json", "{\"version\":1,\"games\":{\"A.NES\":{\"name\":\"Imported title\",\"image\":\"art/manual.png\"}}}");
	Put(root / "roms/art/manual.png");
	const auto imported = ps5retro::Scan("nes", (root / "roms").string(), error);
	bool foundName = false; for (const auto& g : imported) if (g.path.filename() == "A.NES") foundName = g.name == "Imported title" && g.titleId == nes[0].titleId;
	Check(foundName, "offline metadata changes title without changing save/library identity");
	Check(ps5retro::CoverPath(7, nes[0]) == (root / "roms/art/manual.png").string(), "gamelist artwork resolved inside library root");
	Put(root / "roms/.ecam-library.json", "{\"games\":{\"A.NES\":{\"image\":\"../../outside.png\"}}}");
	Put(root / "outside.png");
	Check(ps5retro::CoverPath(7, nes[0]) == (root / "roms/A.png").string(), "artwork traversal refused in favour of local fallback");
	Check(!ps5retro::Available(6, 1), "optional backend never silently falls back");
	Put(root / "app/retro/mupen64plus/eboot.bin");
	ps5retro::Launch(6, (root / "roms/B.z64").string(), error, 1);
	Check(called == "retro/mupen64plus/eboot.bin", "explicit alternative N64 handoff uses its own request folder");
	ps5retro::LaunchLibrary(6, error, 1);
	Check(called == "retro/mupen64plus/eboot.bin" && handed[0] == "--ecam-library", "alternative native settings accessible");
	settings.retro[2].retroBackend = 1; ps5settings::Save(settings);
	Check(ps5settings::Load().retro[2].retroBackend == 1 && ps5settings::Load().retro[3].retroBackend == 0, "backend selection persists independently");
	Put(root / "roms/new.7z", std::string("7z\xbc\xaf\x27\x1c", 6) + std::string(26, '\0'));
	Check(!ps5retro::ValidateRom(7, (root / "roms/new.7z").string(), error, 0) && ps5retro::ValidateRom(7, (root / "roms/new.7z").string(), error, 1), "7z accepted only by the supporting NES backend");
	Check(ps5retro::HasExtension("n64", "hack.rom"), "alternative N64 .rom files discoverable");
	setenv("PS5ECAM_ROM", "/games/stale.nes", 1);
	char arg0[] = "eboot.bin"; char arg1[] = "--ecam-library"; char arg2[] = "--return"; char arg3[] = "/app/eboot.bin"; char* args[] = {arg0, arg1, arg2, arg3};
	ecam_bridge::Init(4, args, "test-no-runtime");
	Check(std::getenv("PS5ECAM_ROM") == nullptr, "library-only handoff clears stale forwarded-game environment");
	Check(ps5retro::Diagnostics(6, 1).find("/data/mupen64plus") != std::string::npos, "diagnostics identify backend-specific data");
	Check((int)ps5launcher::System::Nes == 7 && (int)ps5launcher::System::Gb == 8 && (int)ps5launcher::System::Gba == 10, "handheld sides append without renumbering existing systems");
	Put(root / "handhelds/Pocket.GB", std::string(0x150, '\0')); Put(root / "handhelds/Color.gbc", std::string(0x150, '\0')); Put(root / "handhelds/Advance.gba", std::string(0xc0, '\0'));
	Put(root / "handhelds/collection.zip", "fixture");
	const auto gb = ps5retro::Scan("gb", (root / "handhelds").string(), error);
	const auto gbc = ps5retro::Scan("gbc", (root / "handhelds").string(), error);
	const auto gba = ps5retro::Scan("gba", (root / "handhelds").string(), error);
	Check(gb.size() == 1 && gb[0].path.extension() == ".GB", "GB list accepts case-insensitive loose GB ROMs only");
	Check(gbc.size() == 1 && gbc[0].path.extension() == ".gbc", "GBC list isolates color games without guessing ZIP contents");
	Check(gba.size() == 2, "GBA list accepts GBA and dedicated archives");
	Check(gb[0].titleId != gbc[0].titleId, "shared GB/GBC data root has distinct catalogue identities");
	Check(ps5retro::ValidateRom(8, gb[0].path.string(), error) && ps5retro::ValidateRom(9, gbc[0].path.string(), error), "basic complete GB/GBC headers pass preflight");
	Check(ps5retro::ValidateRom(10, (root / "handhelds/Advance.gba").string(), error), "basic complete GBA header passes preflight");
	Put(root / "short.gb", "short"); Check(!ps5retro::ValidateRom(8, (root / "short.gb").string(), error), "truncated handheld header refused");
	Put(root / "oversized.gba", "x"); fs::resize_file(root / "oversized.gba", 32ull * 1024 * 1024 + 1);
	Check(!ps5retro::ValidateRom(10, (root / "oversized.gba").string(), error), "oversized GBA ROM refused before handoff");
	ps5retro::StartScan(8, (root / "handhelds").string()); ps5retro::StartScan(9, (root / "handhelds").string()); ps5retro::StartScan(10, (root / "handhelds").string()); ps5retro::FinishScans();
	Check(ps5retro::ListGames(8).size() == 1 && ps5retro::ListGames(9).size() == 1 && ps5retro::ListGames(10).size() == 2, "three independent handheld scanner slots join cleanly");
	Put(root / "app/retro/gbc/eboot.bin"); Put(root / "app/retro/gba/eboot.bin");
	ps5retro::Launch(8, gb[0].path.string(), error); Check(called == "retro/gbc/eboot.bin" && handed[1] == gb[0].path.string(), "GB forwards exact ROM to bundled GBCXY");
	ps5retro::Launch(9, gbc[0].path.string(), error); Check(called == "retro/gbc/eboot.bin" && handed[1] == gbc[0].path.string(), "GBC forwards exact ROM to shared GBCXY");
	ps5retro::Launch(10, (root / "handhelds/Advance.gba").string(), error); Check(called == "retro/gba/eboot.bin", "GBA selects its own bundled GBAXY");
	ps5retro::LaunchLibrary(10, error); Check(called == "retro/gba/eboot.bin" && handed[0] == "--ecam-library", "GBA native settings retain ECAM return handoff");
	settings.ui.sides = 2047; settings.ui.librarySort[10] = 2; settings.retro[6].gamesFolder = "/games/gba"; settings.retro[4].gamesFolder = "/games/gb"; ps5settings::Save(settings);
	const auto eleven = ps5settings::Load();
	Check(eleven.ui.sides == 2047 && eleven.ui.librarySort[10] == 2 && eleven.retro[6].gamesFolder == "/games/gba" && eleven.retro[4].gamesFolder == "/games/gb", "eleven-side visibility and independent handheld settings persist");
	Put(ps5paths::LauncherSettings(), "{\"ui\":{\"sides\":255,\"librarySort\":[1,2,3,0,1,2,3,1]},\"n64\":{\"retroBackend\":1}}");
	const auto eight = ps5settings::Load();
	Check(eight.ui.sides == 255 && eight.ui.librarySort[7] == 1 && eight.ui.librarySort[10] == 0 && eight.retro[2].retroBackend == 1, "eight-side PR87 settings retain values and leave new sides hidden");
	ps5catalog::Replace(ps5catalog::System::Gb, gb); ps5catalog::Replace(ps5catalog::System::Gbc, gbc);
	Check(ps5catalog::Save() && ps5catalog::Games(ps5catalog::System::Gb).size() == 1 && ps5catalog::Games(ps5catalog::System::Gbc).size() == 1, "GB and GBC catalogues persist separately");
	Put(root / "mixed-discs/GameCube.ISO", DiscHeader(false));
	Put(root / "mixed-discs/Wii.iso", DiscHeader(true));
	Put(root / "mixed-discs/Other.iso", std::string(0x20, '\0'));
	Put(root / "mixed-discs/Truncated.gcm", "short");
	Put(root / "mixed-discs/Wii.wbfs");
	const auto gcDiscs = ps5retro::Scan("gamecube", (root / "mixed-discs").string(), error);
	const auto wiiDiscs = ps5retro::Scan("wii", (root / "mixed-discs").string(), error);
	Check(gcDiscs.size() == 1 && gcDiscs.front().path.filename() == "GameCube.ISO", "shared folder isolates GameCube ISOs by header and excludes WBFS");
	Check(wiiDiscs.size() == 2, "shared folder isolates Wii ISOs and retains Wii WBFS");
	Check(ps5retro::ValidateRom(4, (root / "mixed-discs/GameCube.ISO").string(), error) && ps5retro::ValidateRom(5, (root / "mixed-discs/Wii.iso").string(), error), "matching disc headers pass preflight");
	Check(!ps5retro::ValidateRom(4, (root / "mixed-discs/Wii.iso").string(), error) && !ps5retro::ValidateRom(5, (root / "mixed-discs/GameCube.ISO").string(), error), "wrong-system disc fails before runtime handoff");
	Check(!ps5retro::ValidateRom(4, (root / "mixed-discs/Other.iso").string(), error) && !ps5retro::ValidateRom(5, (root / "mixed-discs/Truncated.gcm").string(), error), "foreign and truncated raw discs fail preflight");
	Check(fs::file_size(root / "mixed-discs/GameCube.ISO") == 0x20 && fs::file_size(root / "mixed-discs/Wii.iso") == 0x20, "disc discovery does not modify the original files");
	Check(ps5retro::IdentifySystem("/games/Game.Z64") == 6 && ps5retro::IdentifySystem("/games/Game.nes") == 7 &&
		ps5retro::IdentifySystem("/games/Game.gb") == 8 && ps5retro::IdentifySystem("/games/Game.gbc") == 9 && ps5retro::IdentifySystem("/games/Game.gba") == 10,
		"direct launch identifies loose retro ROM systems instead of routing them to Wii U");
	Check(ps5retro::IdentifySystem("/games/Game.sfc") == (int)ps5launcher::System::Snes && ps5retro::IdentifySystem("/games/Game.vb") == (int)ps5launcher::System::VirtualBoy &&
		ps5retro::IdentifySystem("/games/Game.min") == (int)ps5launcher::System::PokemonMini,
		"direct launch identifies the SNES, Virtual Boy and Pokemon mini sides too");
	Check(ps5retro::IdentifySystem((root / "mixed-discs/GameCube.ISO").string()) == 4 && ps5retro::IdentifySystem((root / "mixed-discs/Wii.iso").string()) == 5,
		"direct disc launch identifies GameCube and Wii from headers");
	Check(ps5retro::IdentifySystem("/games/Unknown.zip") == -1 && ps5retro::IdentifySystem("/games/Unknown.elf") == -1,
		"ambiguous archives and ELF files are left to existing explicit routing");
	Put(root / "pokemini/Game.min");
	ps5retro::StartScan((int)ps5launcher::System::PokemonMini, (root / "pokemini").string());
	ps5retro::FinishScans();
	Check(ps5retro::ListGames((int)ps5launcher::System::PokemonMini).size() == 1, "the last side has a scanner slot of its own");
	for (int i = 0; i < 200; ++i) Put(root / "busy-scan" / (std::to_string(i) + ".nes"));
	Put(root / "latest-scan/Latest.nes");
	ps5retro::StartScan(7, (root / "busy-scan").string());
	ps5retro::StartScan(7, (root / "latest-scan").string());
	ps5retro::FinishScans();
	const auto latest = ps5retro::ListGames(7);
	Check(latest.size() == 1 && latest.front().name == "Latest", "a folder change during scanning queues the newest request instead of dropping it");
	// the sides by their keys in ps5retro::kSystems, not by number
	const int n64 = (int)ps5launcher::System::N64, nesSide = (int)ps5launcher::System::Nes, snes = (int)ps5launcher::System::Snes;
	Check(ps5retro::Slot(ps5retro::kFirst) == 0 && ps5retro::Of(snes).key == std::string("snes") && ps5retro::Is(n64, "n64") && !ps5retro::Is(0, "n64"),
		"a retro side's slot and key come from the registry");
	int alternatives = 0;
	for (int side = ps5retro::kFirst; side < ps5launcher::kSideCount; side++) alternatives += ps5retro::AlternativeOf(side) != nullptr;
	Check(alternatives == 2 && ps5retro::AlternativeOf(n64) && ps5retro::AlternativeOf(nesSide) && std::string(ps5retro::BackendFor(nesSide, 1).runtime) == "retro/fceumm/eboot.bin" &&
		std::string(ps5retro::BackendFor(snes, 1).runtime) == ps5retro::Of(snes).runtime, "only the N64 and the NES have a second emulator; another side's backend 1 is its own");
	Put(root / "carts/Game.sfc", std::string(0x8000, '\0')); Put(root / "carts/Game.vb", std::string(0x1000, '\0')); Put(root / "carts/Game.min", std::string(0x1000, '\0'));
	Check(ps5retro::ValidateRom(snes, (root / "carts/Game.sfc").string(), error) && ps5retro::ValidateRom((int)ps5launcher::System::VirtualBoy, (root / "carts/Game.vb").string(), error) &&
		ps5retro::ValidateRom((int)ps5launcher::System::PokemonMini, (root / "carts/Game.min").string(), error), "SNES, Virtual Boy and Pokemon mini games pass preflight");
	Put(root / "carts/huge.sfc", "x"); fs::resize_file(root / "carts/huge.sfc", 32ull * 1024 * 1024 + 1);
	Check(!ps5retro::ValidateRom(snes, (root / "carts/huge.sfc").string(), error) && error.find("larger") != std::string::npos, "an oversized SNES game is refused, saying why");
	Check(!ps5retro::ValidateRom(nesSide, (root / "roms/fake.zip").string(), error) && error == "This ZIP archive is damaged or too large.", "a refused game's reason is the app's words");
	Check(!ps5retro::Available(snes) && !ps5retro::Launch(snes, (root / "carts/Game.sfc").string(), error) && !ps5retro::LaunchLibrary(snes, error),
		"a side with no program and no core linked is not offered, and nothing is started for it");
	Check(ps5retro::Diagnostics(n64, 0).find("not in this copy of PS5-ECAM") != std::string::npos && ps5retro::Diagnostics(n64, 1).find("not in this copy") == std::string::npos,
		"diagnostics say whether this copy has the emulator");
	return failures ? 1 : 0;
}
