// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: where things live on the console.
//
//   /data/homebrew/PPSA99360/        the app (eboot.bin, sce_sys, assets) as ShadowMountPlus mounts it
//   /app0/                           the same, read-only, from inside the app's sandbox (AppDir)
//   /data/cemu/                      Wii U games, MLC, keys, packs and Cemu settings
//   /data/azahar/                    3DS games, SD/NAND storage, keys and settings
//   /data/melonds/                   DS games, BIOS/firmware, saves and cheats
//   /data/eden/                      Switch games, keys, firmware, add-ons and user storage
//   /data/ps5cemu/                   shared launcher settings, logs, covers and downloads
// An install from before keeps all of them in /data/ps5cemu, used there with nothing moved, until it is
// given a folder for each system: renamed into them as the app starts (app/reorganize.h), or copied
// into them on a computer (tools/separate-systems.py).

#pragma once

#include "system_layout.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <sys/stat.h>
#include <vector>

namespace ps5paths
{
	constexpr const char* kTitleId = "PPSA99360";
	constexpr const char* kInstallDir = "/data/homebrew/PPSA99360";
	constexpr const char* kMountedEboot = "/data/homebrew/PPSA99360/eboot.bin";
	// where the PS5 mounts the app it runs, whatever ShadowMountPlus mounted it from (a folder on
	// /data, an extended storage or USB drive, or an image)
	constexpr const char* kSystemMount = "/system_ex/app/PPSA99360";
	// ShadowMountPlus's record of that source: mount.lnk holds the folder's path, mount_img.lnk is
	// there for an image
	constexpr const char* kMountRecord = ECAM_MOUNT_RECORD;
	constexpr const char* kImageMountRecord = "/user/app/PPSA99360/mount_img.lnk";

// where PS5-ECAM writes everything, unless DecideRoot finds a folder of its own on the app's
// drive: on the console /data/ps5cemu; the launcher's preview on a PC (tools/preview-shell.sh) gives
// a folder of its own
#ifndef PS5CEMU_DATA
#define PS5CEMU_DATA "/data/ps5cemu"
#endif
	constexpr const char* kDefaultRoot = PS5CEMU_DATA;
	// the data folder's name at the top of the app's drive (DecideRoot): the app's name before 5.0.0,
	// kept, as /data/ps5cemu is, so the data people have there stays theirs
	constexpr const char* kDriveRoot = ecam_layout::kDriveData;

	namespace detail
	{
		inline std::string& RootSlot()
		{
			static std::string root = kDefaultRoot;
			return root;
		}
	}

	namespace detail
	{
		struct SystemRoots
		{
			bool decided = false;
			std::string azahar, melonds, eden;
		};
		inline SystemRoots& SelectedSystems() { static SystemRoots roots; return roots; }
	}

	// The data folder: kDefaultRoot until main has decided it (DecideRoot), then the one decided
	inline const std::string& Root() { return detail::RootSlot(); }
	inline std::string CemuFolder() { return ecam_layout::Sibling(Root(), "cemu"); }
	inline std::string Azahar() { const auto& s = detail::SelectedSystems(); return s.decided ? s.azahar : ecam_layout::System(Root(), "azahar"); }
	inline std::string MelonDs() { const auto& s = detail::SelectedSystems(); return s.decided ? s.melonds : ecam_layout::System(Root(), "melonds"); }
	// Eden's separate process shares this exact selection algorithm (ecam_layout::Eden): on /data,
	// or beside the data folder on the drive the app is installed on. Before DecideSystemRoots, /data's.
	inline std::string Eden() { const auto& s = detail::SelectedSystems(); return s.decided ? s.eden : ecam_layout::System(kDefaultRoot, "eden"); }
	inline std::string EdenRequestFile() { return Eden() + "/launch-request.txt"; }
	inline std::string& EdenSessionOverride() { static std::string path; return path; }
	inline bool SetEdenSessionFile(std::string path)
	{
		if (path.empty() || path.front() != '/' || path.size() > 4096 || path.find_first_of("\r\n\\") != std::string::npos)
			return false;
		for (std::size_t start = 1; start <= path.size();)
		{
			const std::size_t end = path.find('/', start);
			const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (part.empty() || part == "." || part == "..") return false;
			if (end == std::string::npos) break;
			start = end + 1;
		}
		if (path.size() < 16 || path.compare(path.size() - 16, 16, "ecam-session.txt") != 0)
			return false;
		EdenSessionOverride() = std::move(path);
		return true;
	}
	inline std::string EdenSessionFile() { return EdenSessionOverride().empty() ? Eden() + "/ecam-session.txt" : EdenSessionOverride(); }
	inline std::string EdenResultFile()
	{
		const std::string& session = EdenSessionOverride();
		return session.empty() ? Eden() + "/ecam-result.txt" : session.substr(0, session.size() - 16) + "ecam-result.txt";
	}
	// EglantinePS5's data folder, beside the Switch side's default one (/data/eglantine): the program
	// the Switch 2's games start in (patches/eglantine) takes its launch request from there
	inline std::string Eglantine() { return ecam_layout::Sibling(kDefaultRoot, "eglantine"); }
	inline std::string EglantineRequestFile() { return Eglantine() + "/launch-request.txt"; }
	inline bool Present(const std::string& path) { return ecam_layout::Present(path); }
	namespace detail
	{
		inline std::string FindCemuRoot(const std::string& root) { return ecam_layout::Cemu(root); }
	}
	inline const std::string& CemuRoot()
	{
		// Used only after DecideRoot and elevation. Cemu's own paths are fixed for the process.
		static const std::string folder = detail::FindCemuRoot(Root());
		return folder;
	}
	// Main calls this after elevation and DecideRoot, before any thread. Early settings reads
	// can discover defaults without fixing a choice while the process is still in its sandbox.
	inline void DecideSystemRoots()
	{
		auto& s = detail::SelectedSystems();
		if (s.decided) return;
		s.azahar = ecam_layout::System(Root(), "azahar");
		s.melonds = ecam_layout::System(Root(), "melonds");
		// the Switch's beside the others on the app's drive, unless the console's has its files already;
		// chosen there, it is marked, so a file put in the console's later does not take the Switch
		// side, and its saves, back to it
		const std::string drive = ecam_layout::DriveData(kMountRecord);
		s.eden = ecam_layout::Eden(kDefaultRoot, drive);
		if (!drive.empty() && s.eden == ecam_layout::Sibling(drive, "eden"))
			ecam_layout::Activate(s.eden);
		(void)CemuRoot();
		s.decided = true;
	}
	inline std::string Mlc() { return CemuRoot() + "/mlc01"; }
	inline std::string Games() { return CemuFolder() + "/games"; }
	inline std::string CemuCache() { return CemuRoot() + "/cache"; }
	inline std::string DiscKeys() { return CemuRoot() + "/keys.txt"; }

	inline std::vector<std::string> GameFolders(const std::vector<std::string>& configured)
	{
		std::vector<std::string> folders;
		auto add = [&](const std::string& folder) {
			if (std::find(folders.begin(), folders.end(), folder) == folders.end())
				folders.push_back(folder);
		};
		const std::string legacy = Root() + "/games";
		for (const std::string& folder : configured.empty() ? std::vector<std::string>{Games()} : configured)
		{
			if (folder == Games() || folder == legacy || folder == Root() + "/cemu/games")
			{
				add(Games());
				if (Present(legacy) && !ecam_layout::Activated(CemuFolder()))
					add(legacy);
				if (Present(Root() + "/cemu/games") && !ecam_layout::Activated(CemuFolder()))
					add(Root() + "/cemu/games");
			}
			else
				add(folder); // custom local, USB and network paths remain explicit choices
		}
		return folders;
	}
	// Native input and storage directories, made at startup without inventing unsupported content.
	inline std::vector<std::string> SystemDirectories()
	{
		std::vector<std::string> directories{Games()};
		auto add = [&](const std::string& base, std::initializer_list<const char*> names) {
			for (const char* name : names) directories.push_back(base + "/" + name);
		};
		add(CemuRoot(), {"updates", "dlc", "mlc01", "graphicPacks", "controllerProfiles"});
		add(Azahar(), {"games", "updates", "dlc", "sysdata", "nand", "sdmc", "cheats", "load"});
		add(MelonDs(), {"games", "bios", "saves", "states", "cheats"});
		add(Eden(), {"roms", "updates", "keys", "firmware", "mods", "user", "config"});
		return directories;
	}
	inline std::string Cache() { return Root() + "/cache"; }
	inline std::string RadvCache() { return Root() + "/cache/radv"; }
	inline std::string Logs() { return Root() + "/logs"; }
	inline std::string LauncherSettings() { return Root() + "/ps5cemu.json"; }
	inline std::string Covers() { return Root() + "/covers"; }

	// Whether path is folder or inside it. (No starts_with: melonDS's own build, whose frontend has this
	// header, is C++17.)
	inline bool Within(const std::string& path, const std::string& folder)
	{
		return path.compare(0, folder.size(), folder) == 0 && (path.size() == folder.size() || path[folder.size()] == '/');
	}

	// A path in kDefaultRoot, in Root() instead when the data is elsewhere: a folder setting saved
	// while the data was in /data follows the data to the drive it was moved to (#52)
	inline std::string Relocated(const std::string& path)
	{
		if (Root() != kDefaultRoot)
			for (const char* system : {"cemu", "azahar", "melonds"})
			{
				const std::string old = ecam_layout::Sibling(kDefaultRoot, system);
				if (Within(path, old))
					return ecam_layout::Sibling(Root(), system) + path.substr(old.size());
			}
		const size_t length = std::strlen(kDefaultRoot);
		if (Root() == kDefaultRoot || path.compare(0, length, kDefaultRoot) != 0 || (path.size() > length && path[length] != '/'))
			return path;
		return Root() + path.substr(length);
	}

	// Cemu's own files and folders, at the top of the data folder in an install from before: what
	// app/reorganize.h moves into CemuFolder(). (title_list_cache.xml stays: Cemu makes it again from
	// where the games are now.)
	inline constexpr const char* kCemuItems[] = {"settings.xml", "network_services.xml", "controllerProfiles", "gameProfiles",
		"graphicPacks", "reshade", "mlc01", "keys.txt", "otp.bin", "seeprom.bin", "shareddata.bin", "sdcard", "games", "updates", "dlc",
		"log.txt", "screenshots", "dump", "crashdump", "icons", "cafeLibs", "memorySearcher", "debugger", "resources", "cache/shaderCache"};

	// A path saved before the systems had folders of their own (a home screen tile's game, a games
	// folder setting), where app/reorganize.h moved it; as it is while it is still there
	inline std::string Moved(const std::string& path)
	{
		if (path.empty() || path[0] != '/' || Present(path))
			return path;
		std::string moved;
		auto under = [&](const std::string& old, const std::string& now) {
			if (!Within(path, old))
				return false;
			moved = now + path.substr(old.size());
			return true;
		};
		if (ecam_layout::Activated(CemuFolder()))
			for (const char* item : kCemuItems)
				if (under(Root() + "/" + item, CemuFolder() + "/" + item))
					return moved;
		for (const char* system : {"azahar", "melonds"})
			if (ecam_layout::Activated(ecam_layout::Sibling(Root(), system)) && under(Root() + "/" + system, ecam_layout::Sibling(Root(), system)))
				return moved;
		const std::string eden = ecam_layout::Sibling(kDefaultRoot, "eden");
		if (ecam_layout::Activated(eden) && under(std::string(kDefaultRoot) + "/eden", eden))
			return moved;
		return path;
	}

	// The folder ShadowMountPlus mounted the running app from, by its record (kMountRecord), when it
	// holds the app; empty without one. image: whether the app was mounted from an image instead,
	// whose files can't be replaced one by one.
	inline std::string MountSource(bool* image = nullptr)
	{
		struct stat info{};
		if (image)
			*image = stat(kImageMountRecord, &info) == 0;
		return ecam_layout::MountSource(kMountRecord); // as Eden's program reads it
	}

	// The drives whose homebrew folders the app is installed in (#40 tried /mnt/ext1/homebrew)
	inline constexpr const char* kInstallDrives[] = {"/mnt/ext0", "/mnt/ext1", "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3"};

	// The folders the app may be installed in, the usual one first: /data/homebrew's and the drives'
	inline std::vector<std::string> InstallFolders()
	{
		std::vector<std::string> folders{kInstallDir};
		for (const char* drive : kInstallDrives)
			folders.push_back(std::string(drive) + "/homebrew/" + kTitleId);
		return folders;
	}

	// The running app's eboot.bin as this process sees it: /app0's while it is there, else the PS5's
	// mount of the app; empty when it sees neither
	inline std::string RunningEboot()
	{
		struct stat info{};
		for (const std::string& eboot : {std::string("/app0/eboot.bin"), std::string(kSystemMount) + "/eboot.bin"})
			if (stat(eboot.c_str(), &info) == 0 && S_ISREG(info.st_mode))
				return eboot;
		return {};
	}

	// Whether folder holds the copy of the app that is running: its eboot.bin the same as running's,
	// by size and time. True when running cannot be read, as nothing then tells two copies apart. An
	// older copy left in /data/homebrew beside an app on a drive is not it: a restart or an update
	// there started or updated that old copy instead (#72, "the PS5 started the old one").
	inline bool RunsFrom(const std::string& folder, const std::string& running = RunningEboot())
	{
		struct stat mine{}, there{};
		if (running.empty() || stat(running.c_str(), &mine) != 0)
			return true;
		return stat((folder + "/eboot.bin").c_str(), &there) == 0 && there.st_size == mine.st_size && there.st_mtime == mine.st_mtime;
	}

	// The app's own folder. The sandbox mounts it as /app0, but a process the HEN has jailbroken
	// sees the console's root, which has no /app0: there the app is read where the PS5 mounts it,
	// which follows ShadowMountPlus wherever it found the app (an install on a USB drive with an older
	// copy left in /data/homebrew included), else where it is usually installed, or where the sandbox
	// mounts it from (as ProsperoEden's storage_paths.h finds its own).
	inline std::string FindAppDir()
	{
		auto holdsApp = [](const std::string& folder) {
			struct stat info{};
			return stat((folder + "/eboot.bin").c_str(), &info) == 0 && S_ISREG(info.st_mode);
		};
		for (const char* candidate : {"/app0", kSystemMount, kInstallDir, "/mnt/sandbox/PPSA99360_000/app0"})
			if (holdsApp(candidate))
				return std::string(candidate);
		// none of those: the folder ShadowMountPlus recorded (MountSource checks it holds the app),
		// then the homebrew folders on the drives people install to
		if (std::string source = MountSource(); !source.empty())
			return source;
		for (const char* drive : kInstallDrives)
			if (std::string folder = std::string(drive) + "/homebrew/" + kTitleId; holdsApp(folder))
				return folder;
		return std::string(kInstallDir);
	}

	namespace detail
	{
		inline std::string& AppDirSlot()
		{
			static std::string directory;
			return directory;
		}
	}

	// FindAppDir's answer, decided on first use (after ps5privilege::Acquire) and again by RefindAppDir
	inline const std::string& AppDir()
	{
		std::string& directory = detail::AppDirSlot();
		if (directory.empty())
			directory = FindAppDir();
		return directory;
	}

	// The app's folder looked for again, by the elevation helper's callers, before any thread starts:
	// the helper takes the process out of the sandbox, to the console's root, where the /app0 found
	// before is gone (3.5.0 then read its launcher's font from there, and on a console whose drives
	// the HEN leaves out the launcher never started, #34). Whether it moved.
	inline bool RefindAppDir()
	{
		std::string& directory = detail::AppDirSlot();
		if (directory.empty())
			return false;
		std::string found = FindAppDir();
		if (found == directory)
			return false;
		directory = std::move(found);
		return true;
	}

	// The app's own download0, which its sandbox mounts as /download0 and a process the HEN or the
	// elevation helper has taken out of it sees at /mnt/sandbox/PPSA99360_000/download0: the Switch
	// side's program keeps its trace there until it reaches /data (patches/eden)
	inline std::string SandboxDownload()
	{
		struct stat info{};
		if (stat("/download0", &info) == 0 && S_ISDIR(info.st_mode))
			return "/download0";
		return std::string("/mnt/sandbox/") + kTitleId + "_000/download0";
	}

	// The USB drive or extended storage a path is on ("/mnt/usb0" for "/mnt/usb0/homebrew/PPSA99360");
	// empty for any other place
	inline std::string DriveOf(const std::string& path)
	{
		return ecam_layout::DriveOf(path); // as Eden's program finds the app's drive
	}

	// The data folder, decided once by main, when the process sees the console's drives (after the
	// elevation helper): a PS5CEMU-HAR-data folder at the top of the drive the app is installed on
	// (where ShadowMountPlus mounted it from, else the app's folder) holds everything, and nothing is
	// made in /data (#52). A first start on a drive, with no /data/ps5cemu yet, makes that folder; an
	// app that has kept its data in /data goes on doing so until the folder is made (the guide's
	// "Everything on the app's drive" says how to move it). An app in /data, or mounted from an
	// image, keeps kDefaultRoot. Not a drive's ps5cemu folder: that one holds Wii U mods (app/mods.h),
	// and the app's backups and logs on a drive are PS5CEMU-HAR-saves and PS5-ECAM-logs-... already.
	inline const std::string& DecideRoot()
	{
		std::string& root = detail::RootSlot();
		std::string drive = DriveOf(MountSource());
		if (drive.empty())
			drive = DriveOf(AppDir());
		if (drive.empty())
			return root;
		const std::string folder = drive + "/" + kDriveRoot;
		struct stat info{};
		const bool there = stat(folder.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
		const bool inData = stat(kDefaultRoot, &info) == 0;
		if (there || (!inData && mkdir(folder.c_str(), 0777) == 0))
			root = folder;
		return root;
	}

	inline std::string Eboot() { return AppDir() + "/eboot.bin"; }
	inline std::string Assets() { return AppDir() + "/assets"; }
	// Cemu's read-only data (gameProfiles, resources)
	inline std::string CemuData() { return Assets() + "/cemu"; }
}
