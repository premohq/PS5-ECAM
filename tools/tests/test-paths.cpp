// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/paths.h"
#include "frontend/settings.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
namespace fs = std::filesystem;
static void Check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
    std::cout << "PASS  " << what << '\n';
}
static void Put(const fs::path& p, const std::string& value = "original") {
    fs::create_directories(p.parent_path()); std::ofstream(p) << value;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const fs::path base = argv[1], shared = base / "ps5cemu";
    ps5paths::detail::RootSlot() = shared.string();
    ps5paths::DecideSystemRoots();
    Check(ps5paths::CemuRoot() == (base / "cemu").string(), "Cemu is a sibling of ps5cemu");
    Check(ps5paths::Azahar() == (base / "azahar").string(), "Azahar is a sibling of ps5cemu");
    Check(ps5paths::MelonDs() == (base / "melonds").string(), "melonDS is a sibling of ps5cemu");
    Check(ecam_layout::System(shared.string(), "eden") == (base / "eden").string(), "Eden layout selects a sibling of ps5cemu");
    Check(ps5paths::Eden() == ecam_layout::Sibling(ps5paths::kDefaultRoot, "eden"), "launcher Eden default uses the separate console directory");
    Check(ps5paths::Mlc() == (base / "cemu/mlc01").string() && ps5paths::DiscKeys() == (base / "cemu/keys.txt").string(), "Wii U content and keys live in the Cemu directory");
    ps5settings::Launcher settings;
    Check(settings.gamesFolder == (base / "cemu/games").string() && settings.n3ds.gamesFolder == (base / "azahar/games").string() && settings.nds.gamesFolder == (base / "melonds/games").string(), "all in-process game defaults are separate");
    const auto directories = ps5paths::SystemDirectories();
    auto has = [&](const fs::path& p) { return std::find(directories.begin(), directories.end(), p.string()) != directories.end(); };
    Check(has(base / "cemu/updates") && has(base / "cemu/dlc") && has(base / "cemu/mlc01"), "Wii U gets add-on sources and native system storage");
    Check(has(base / "azahar/sysdata") && has(base / "azahar/nand") && has(base / "azahar/sdmc") && has(base / "azahar/updates"), "3DS gets key/system storage and add-on sources");
    Check(has(base / "melonds/bios") && has(base / "melonds/saves") && !has(base / "melonds/dlc"), "DS gets BIOS/firmware and saves without unsupported DLC folders");
    Check(has(fs::path(ps5paths::Eden()) / "keys") && has(fs::path(ps5paths::Eden()) / "firmware") && has(fs::path(ps5paths::Eden()) / "updates"), "Switch gets keys, firmware and add-on directories");
    Check(!fs::exists(shared) && !fs::exists(base / "cemu"), "discovery creates no files");
    Put(shared / "games/Game.wua");
    const std::vector<std::string> both{(base / "cemu/games").string(), (shared / "games").string()};
    Check(ps5paths::GameFolders({}) == both && ps5paths::GameFolders({(shared / "games").string()}) == both, "old and new default games remain discoverable before migration");
    Check(ps5paths::GameFolders(both) == both, "scan roots are not duplicated");
    const std::vector<std::string> custom{"/mnt/usb0/My Games", "/network/NAS/library", shared.string() + "-other/games"};
    Check(ps5paths::GameFolders(custom) == custom, "custom paths remain unchanged");
    for (const char* marker : {"settings.xml", "mlc01", "keys.txt", "controllerProfiles", "graphicPacks", "reshade"}) {
        Put(shared / marker);
        Check(ecam_layout::Cemu(shared.string()) == shared.string(), "legacy Cemu state wins unactivated conflicts");
        Check(fs::file_size(shared / marker) == 8, "legacy data remains untouched");
        fs::remove(shared / marker);
    }
    for (const char* name : {"azahar", "melonds", "eden"}) {
        Put(shared / name / "games/game", name);
        Check(ecam_layout::System(shared.string(), name) == (shared / name).string(), "each legacy backend remains discoverable");
        Put(base / name / ecam_layout::kMarker, "invalid");
        Check(ecam_layout::System(shared.string(), name) == (shared / name).string(), "invalid migration marker cannot hide legacy data");
        Put(base / name / ecam_layout::kMarker, ecam_layout::kVersion);
        Check(ecam_layout::System(shared.string(), name) == (base / name).string(), "verified migration activates the separate backend folder");
        Check(fs::exists(shared / name / "games/game"), "activation leaves original system content in place");
    }
    Put(shared / "mlc01/save", "saved data");
    Put(base / "cemu" / ecam_layout::kMarker, ecam_layout::kVersion);
    Check(ecam_layout::Cemu(shared.string()) == (base / "cemu").string(), "verified migration activates separate Cemu storage");
    Check(ps5paths::GameFolders({(shared / "games").string()}) == std::vector<std::string>{(base / "cemu/games").string()}, "activated copies do not scan the retained backup originals");
    Check(ps5paths::CemuRoot() == (base / "cemu").string() && ps5paths::Azahar() == (base / "azahar").string(), "all selected roots remain fixed for the running session");
    const std::string drive = (base / "usb/PS5CEMU-HAR-data").string();
    Check(ecam_layout::Cemu(drive) == (base / "usb/cemu").string() && ecam_layout::System(drive, "azahar") == (base / "usb/azahar").string(), "app-drive system folders are siblings of its launcher data");
    ps5paths::detail::RootSlot() = drive;
    Check(ps5paths::Relocated(ecam_layout::Sibling(ps5paths::kDefaultRoot, "cemu") + "/mlc01") == ecam_layout::Sibling(drive, "cemu") + "/mlc01", "new Cemu default MLC paths follow a copied install onto the app drive");
    Check(ps5paths::Relocated(ecam_layout::Sibling(ps5paths::kDefaultRoot, "azahar") + "/games") == ecam_layout::Sibling(drive, "azahar") + "/games", "new 3DS default paths follow the app drive");
    Check(ps5paths::Relocated(ecam_layout::Sibling(ps5paths::kDefaultRoot, "melonds") + "/games") == ecam_layout::Sibling(drive, "melonds") + "/games", "new DS default paths follow the app drive");
    const auto edenDefault = ecam_layout::Sibling(ps5paths::kDefaultRoot, "eden") + "/roms";
    Check(ps5paths::Relocated(edenDefault) == edenDefault, "relocation leaves the Switch's paths to its own choice (ecam_layout::Eden), which both programs make");
    Check(ps5paths::Relocated(std::string(ps5paths::kDefaultRoot) + "-other/games") == std::string(ps5paths::kDefaultRoot) + "-other/games", "root relocation respects path boundaries");

    // the copy of the app that runs, told from an older one left in /data/homebrew beside it
    const fs::path running = base / "usb0/homebrew/PPSA99360", stale = base / "homebrew/PPSA99360", same = base / "ext0/homebrew/PPSA99360";
    Put(running / "eboot.bin", "release 5.1.0");
    Put(stale / "eboot.bin", "release 5.0");
    fs::create_directories(same);
    fs::copy_file(running / "eboot.bin", same / "eboot.bin");
    fs::last_write_time(same / "eboot.bin", fs::last_write_time(running / "eboot.bin"));
    fs::last_write_time(stale / "eboot.bin", fs::last_write_time(running / "eboot.bin") - std::chrono::hours(48));
    const std::string eboot = (running / "eboot.bin").string();
    Check(ps5paths::RunsFrom(running.string(), eboot) && ps5paths::RunsFrom(same.string(), eboot), "the running copy, and a copy of the same files, are the app's");
    Check(!ps5paths::RunsFrom(stale.string(), eboot) && !ps5paths::RunsFrom((base / "missing").string(), eboot), "an older copy, or none, is not the running app's");
    Check(ps5paths::RunsFrom(stale.string(), "") && ps5paths::RunsFrom(stale.string(), (base / "unreadable/eboot.bin").string()), "without the running app's eboot, nothing tells the copies apart");
    const auto installs = ps5paths::InstallFolders();
    Check(installs.front() == ps5paths::kInstallDir && std::find(installs.begin(), installs.end(), "/mnt/usb0/homebrew/PPSA99360") != installs.end() &&
              std::find(installs.begin(), installs.end(), "/mnt/ext1/homebrew/PPSA99360") != installs.end(), "the usual install folder first, then the drives'");
}
