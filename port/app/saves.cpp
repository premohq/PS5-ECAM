// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: every side's saves to a USB drive and back (saves.h).

#include "saves.h"
#ifndef PS5ECAM_RETRO_DATA
#define PS5ECAM_RETRO_DATA "/data"
#endif
#include "lang.h"
#include "paths.h"
#include "../eden/library.h"
#include "../eden/save_transfer.h"
#include "../ps5/log.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sys/stat.h>
#include <thread>

namespace fs = std::filesystem;

namespace ps5saves
{
	namespace
	{
		// the backups' folder on a USB drive: the app's name before 5.0.0, kept, so the backups made
		// before are found and new ones go beside them
		constexpr const char* kFolder = "PS5CEMU-HAR-saves";
		// The host tests use a disposable mount root; production keeps the console's /mnt paths.
#ifndef PS5CEMU_USB_ROOT
#define PS5CEMU_USB_ROOT "/mnt"
#endif
		std::string UsbDrive(int index) { return fmt::format("{}/usb{}", PS5CEMU_USB_ROOT, index); }
		bool SwitchTitle(const GameRef& game) { return game.titleId && (game.titleId >> 56) == 1; }
		bool SwitchSave(const GameRef& game, const std::string& relative)
		{
			if (!SwitchTitle(game)) return false;
			const fs::path path(relative);
			std::vector<std::string> parts;
			for (const auto& part : path) parts.push_back(part.string());
			if (parts.size() < 4 || parts[0] != "0000000000000000" || parts[1].size() != 32) return false;
			if (parts[1].find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
			const std::string wanted = fmt::format("{:016x}", game.titleId);
			std::string title = parts[2];
			std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return title == wanted;
		}

		// What a backup holds: its folder in the backup, where it comes from, and which of the files
		// there are saves (empty: all)
		struct Set
		{
			const char* name;
			std::string source;
			bool (*keep)(const std::string& relative);
		};

		// in azahar/sdmc/Nintendo 3DS: the games' saves (title/<high>/<low>/data) and extra data, never
		// the games and updates installed beside them (title/.../content)
		bool SdmcSave(const std::string& relative)
		{
			const std::string path = "/" + relative + "/";
			return (path.find("/title/") != std::string::npos && path.find("/data/") != std::string::npos) ||
				path.find("/extdata/") != std::string::npos;
		}

		std::vector<Set> Sets()
		{
			const std::string root = ps5paths::Root();
			const std::string retro = PS5ECAM_RETRO_DATA;
			return {
				{"wiiu/save", ps5paths::Mlc() + "/usr/save", nullptr},
				{"3ds/sdmc", ps5paths::Azahar() + "/sdmc/Nintendo 3DS", SdmcSave},
				{"3ds/nand", ps5paths::Azahar() + "/nand/data", nullptr},
				{"3ds/states", ps5paths::Azahar() + "/states", nullptr},
				// Backend names are part of the backup format: never restore one core's states into another.
				{"retro/porpoise/gc", retro + "/porpoise/saves/User/GC", nullptr},
				{"retro/porpoise/wii", retro + "/porpoise/saves/User/Wii", nullptr},
				{"retro/porpoise/states", retro + "/porpoise/states", nullptr},
				{"retro/n64xy/saves", retro + "/PS5N64/saves", nullptr},
				{"retro/nesyx/saves", retro + "/PS5NES/saves", nullptr},
				{"retro/mupen64plus/saves", retro + "/mupen64plus/saves", nullptr},
				{"retro/mupen64plus/states", retro + "/mupen64plus/states", nullptr},
				{"retro/fceumm/saves", retro + "/fceumm/saves", nullptr},
				{"retro/fceumm/states", retro + "/fceumm/states", nullptr},
				{"ds/saves", ps5paths::MelonDs() + "/saves", nullptr},
				{"ds/slot2-saves", ps5paths::MelonDs() + "/slot2-saves", nullptr},
				{"ds/states", ps5paths::MelonDs() + "/states", nullptr},
				// Eden's selected console directory, shared with its separate executable:
				// the games' saves, and the system save with its user profiles, whose IDs name the saves'
				// folders
				{"switch/save", ps5eden::DataFolder() + "/user/nand/user/save", nullptr},
				{"switch/profiles", ps5eden::DataFolder() + "/user/nand/system/save/8000000000000010", nullptr},
				// the settings, so a reinstall or another PS5 starts where this one was: the launcher's (every
				// side's, each game's own), Cemu's, its controller profiles and the themes. A file is a set too
				{"settings/ps5cemu.json", root + "/ps5cemu.json", nullptr},
				{"settings/settings.xml", ps5paths::CemuRoot() + "/settings.xml", nullptr},
				{"settings/controllerProfiles", ps5paths::CemuRoot() + "/controllerProfiles", nullptr},
				{"settings/themes", root + "/themes", nullptr},
			};
		}

		bool IsSettings(const std::string& set)
		{
			return set.rfind("settings/", 0) == 0;
		}

		// Where a file a restore replaces is kept: saves-before/<stamp> in the data folder, in its shape
		// there; Eden's, in /data/ps5cemu's whatever the data folder, as a rename does not cross drives
		fs::path KeptPath(const std::string& stamp, const fs::path& file)
		{
			// Each system keeps replaced saves on its own filesystem, including Eden on /data
			// while the launcher and other systems are on a USB drive.
			for (const std::string& root : {ps5paths::CemuRoot(), ps5paths::Azahar(), ps5paths::MelonDs(), ps5eden::DataFolder(), std::string(PS5ECAM_RETRO_DATA) + "/porpoise", std::string(PS5ECAM_RETRO_DATA) + "/PS5N64", std::string(PS5ECAM_RETRO_DATA) + "/PS5NES", std::string(PS5ECAM_RETRO_DATA) + "/mupen64plus", std::string(PS5ECAM_RETRO_DATA) + "/fceumm", ps5paths::Root()})
			{
				const fs::path relative = file.lexically_relative(root);
				if (!relative.empty() && *relative.begin() != "..")
					return fs::path(root) / "saves-before" / stamp / relative;
			}
			return fs::path(ps5paths::Root()) / "saves-before" / stamp / file.filename();
		}

		struct File
		{
			fs::path from, to;
			uint64_t size;
		};

		std::mutex s_mutex;
		Status s_status; // under s_mutex
		std::thread s_thread;

		int s_finished = 0; // under s_mutex

		void SetState(Status::State state, std::string message = {})
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.message = std::move(message);
			if (state == Status::State::Done || state == Status::State::Failed)
				s_status.finished = ++s_finished;
		}

		std::string Stamp()
		{
			char stamp[32] = "saves";
			const std::time_t now = std::time(nullptr);
			if (const std::tm* local = std::localtime(&now))
				std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", local);
			return stamp;
		}

		bool Copy(const File& file)
		{
			std::error_code ec;
			fs::create_directories(file.to.parent_path(), ec);
			std::FILE* in = std::fopen(file.from.string().c_str(), "rb");
			if (!in)
				return false;
			std::FILE* out = std::fopen(file.to.string().c_str(), "wb");
			if (!out)
			{
				std::fclose(in);
				return false;
			}
			std::vector<char> buffer(1 << 20);
			bool ok = true;
			for (;;)
			{
				const size_t read = std::fread(buffer.data(), 1, buffer.size(), in);
				if (read == 0)
				{
					ok = !std::ferror(in);
					break;
				}
				if (std::fwrite(buffer.data(), 1, read, out) != read)
				{
					ok = false;
					break;
				}
				std::lock_guard lock(s_mutex);
				s_status.copied += read;
			}
			std::fclose(in);
			return std::fclose(out) == 0 && ok;
		}

		// -- one game's (6.15) ---------------------------------------------------------------------------

		std::string Hex8(uint32_t value)
		{
			return fmt::format("{:08x}", value);
		}

		std::vector<std::string> Parts(const std::string& relative)
		{
			std::vector<std::string> parts;
			size_t start = 0;
			while (start <= relative.size())
			{
				const size_t end = relative.find('/', start);
				parts.push_back(relative.substr(start, end == std::string::npos ? std::string::npos : end - start));
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return parts;
		}

		// the 3DS's extra data folder of a game: its title's unique ID
		std::string ExtDataId(uint64_t titleId)
		{
			return Hex8((uint32_t)(titleId >> 8) & 0xFFFFF);
		}

		std::string DsStem(const GameRef& game)
		{
			std::string stem = fs::path(game.file).stem().string();
			std::string extension = fs::path(game.file).extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if ((extension == ".gz" || extension == ".bz2" || extension == ".xz" || extension == ".zst") && fs::path(stem).extension() == ".tar")
				stem = fs::path(stem).stem().string();
			return stem;
		}

		// Whether a file of a set (by its path in the set) is one of this game's saves
		bool OfGame(const GameRef& game, const std::string& set, const std::string& relative)
		{
			if (game.side == 3) return set == "switch/save" && SwitchSave(game, relative);
			if (game.side < 0 || game.side > 2) return false;
			const std::vector<std::string> parts = Parts(relative);
			const std::string high = Hex8((uint32_t)(game.titleId >> 32)), low = Hex8((uint32_t)game.titleId);
			if (game.side == 0)
				return set == "wiiu/save" && parts.size() > 2 && parts[0] == high && parts[1] == low;
			if (game.side == 1)
			{
				if (set == "3ds/states")
					return relative.rfind(fmt::format("{:016X}.", game.titleId), 0) == 0;
				if (set != "3ds/sdmc" || parts.size() < 6)
					return false;
				// <id0>/<id1>/title/<high>/<low>/data/..., or <id0>/<id1>/extdata/00000000/<unique ID>/...
				return (parts[2] == "title" && parts[3] == high && parts[4] == low && parts[5] == "data") ||
					(parts[2] == "extdata" && parts[3] == "00000000" && parts[4] == ExtDataId(game.titleId));
			}
			const std::string stem = DsStem(game);
			if (set == "ds/saves" || set == "ds/slot2-saves")
				return relative == stem + ".sav";
			if (set == "ds/states")
				return relative.rfind(stem + ".ml", 0) == 0 && relative.size() == stem.size() + 4;
			return false;
		}

		// Each set's files: from the saves to the backup (or, restoring, back); with a game, its alone
		std::vector<File> List(const fs::path& backup, bool restoring, const GameRef* game = nullptr)
		{
			std::vector<File> files;
			std::error_code ec;
			for (const auto& set : Sets())
			{
				const fs::path from = restoring ? backup / set.name : fs::path(set.source);
				const fs::path to = restoring ? fs::path(set.source) : backup / set.name;
				if (game && IsSettings(set.name))
					continue; // a game's backup is its saves alone
				if (fs::is_regular_file(from, ec))
				{
					files.push_back({from, to, fs::file_size(from, ec)});
					continue;
				}
				if (!fs::is_directory(from, ec))
					continue;
				for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
				{
					if (it->is_symlink(ec) || !it->is_regular_file(ec))
						continue;
					const std::string relative = it->path().lexically_relative(from).generic_string();
					if (set.keep && !set.keep(relative))
						continue;
					if (game && !OfGame(*game, set.name, relative))
						continue;
					files.push_back({it->path(), to / relative, it->file_size(ec)});
				}
			}
			return files;
		}

		void Run(fs::path backup, bool restoring, std::optional<GameRef> game)
		{
			std::vector<File> files = List(backup, restoring, game ? &*game : nullptr);
			uint64_t total = 0;
			bool settings = false;
			for (const File& file : files)
			{
				total += file.size;
				settings = settings || (restoring && IsSettings(file.from.lexically_relative(backup).generic_string()));
			}
			{
				std::lock_guard lock(s_mutex);
				s_status.total = total;
				s_status.files = (int)files.size();
			}
			std::error_code ec;
			// restoring: what is there now moves aside first, into saves-before, in the same shape
			const std::string stamp = Stamp();
			const fs::path aside = fs::path(ps5paths::Root()) / "saves-before" / stamp;
			int moved = 0;
			for (const File& file : files)
			{
				if (restoring && fs::exists(file.to, ec))
				{
					const fs::path kept = KeptPath(stamp, file.to);
					if (fs::exists(kept, ec))
					{
						SetState(Status::State::Failed, ps5lang::Tr("A save moved aside a moment ago is in the way: try again in a moment."));
						return;
					}
					fs::create_directories(kept.parent_path(), ec);
					fs::rename(file.to, kept, ec);
					if (ec)
					{
						SetState(Status::State::Failed, ps5lang::TrF("A save could not be moved aside: {0}", file.to.string()));
						return;
					}
					moved++;
				}
				if (!Copy(file))
				{
					SetState(Status::State::Failed, ps5lang::TrF("A file could not be copied: {0}", file.from.filename().string()));
					return;
				}
			}
			if (!restoring)
			{
				// a game's backup says whose it is (FindBackups lists it as any other)
				std::ofstream json(backup / "backup.json");
				json << fmt::format("{{\"version\": \"{}\", \"date\": \"{}\", \"files\": {}, \"bytes\": {}{}}}\n", PS5CEMU_VERSION,
					backup.filename().string(), files.size(), total, game ? fmt::format(", \"game\": \"{:016x}\"", game->titleId) : std::string());
			}
			ps5log::Line("[saves] {} {} files, {} KB {} {}{}", restoring ? "restored" : "backed up", files.size(), total / 1000, restoring ? "from" : "to",
				backup.string(), restoring && moved ? fmt::format(" ({} kept aside in {})", moved, aside.string()) : std::string());
			{
				std::lock_guard lock(s_mutex);
				s_status.settingsRestored = restoring && settings;
			}
			SetState(Status::State::Done);
		}

		bool Begin(const fs::path& backup, bool restoring, std::string& error, std::optional<GameRef> game = std::nullopt)
		{
			if (game && (game->side < 0 || game->side > 3 || (game->side == 3 && !SwitchTitle(*game))))
			{
				error = ps5lang::Tr("This game has no title ID to find its saves by.");
				return false;
			}
			if (Current().state == Status::State::Running)
			{
				error = ps5lang::Tr("A backup is already running.");
				return false;
			}
			if (s_thread.joinable())
				s_thread.join();
			{
				std::lock_guard lock(s_mutex);
				s_status = {};
				s_status.finished = s_finished;
				s_status.state = Status::State::Running;
				s_status.restoring = restoring;
				s_status.where = backup.string();
			}
			s_thread = std::thread(Run, backup, restoring, std::move(game));
			return true;
		}

		// The folder for a new backup on the first USB drive that takes one
		bool NewBackupFolder(fs::path& folder, std::string& error)
		{
			const std::string stamp = Stamp();
			for (int drive = 0; drive < 8; drive++)
			{
				std::error_code ec;
				const fs::path root = UsbDrive(drive);
				if (!fs::is_directory(root, ec))
					continue;
				fs::create_directories(root / kFolder, ec);
				if (ec) continue;
				for (int suffix = 0; suffix < 1000; suffix++)
				{
					folder = root / kFolder / (stamp + (suffix ? fmt::format("-{:03d}", suffix) : ""));
					if (fs::create_directory(folder, ec)) return true;
					if (ec && ec != std::errc::file_exists) break;
					ec.clear();
				}
			}
			error = ps5lang::Tr("No USB drive found: plug one in, formatted exFAT or FAT32.");
			return false;
		}

		// A folder's (or a file's) files: how many, their bytes, the newest one's time
		void Measure(const fs::path& path, GameSave& save)
		{
			std::error_code ec;
			auto add = [&](const fs::directory_entry& entry) {
				if (entry.is_symlink(ec) || !entry.is_regular_file(ec))
					return;
				save.files++;
				save.bytes += entry.file_size(ec);
				struct stat info{};
				if (stat(entry.path().c_str(), &info) == 0)
					save.changed = std::max<int64_t>(save.changed, (int64_t)info.st_mtime);
			};
			if (fs::is_directory(path, ec))
			{
				for (auto it = fs::recursive_directory_iterator(path, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
					add(*it);
			}
			else
				add(fs::directory_entry(path, ec));
		}
	}

	std::string ResumeState(const GameRef& game)
	{
		if (game.side == 1)
			return fmt::format("{}/states/{:016X}.{:02d}.cst", ps5paths::Azahar(), game.titleId, kResumeSlot);
		if (game.side != 2)
			return {};
		// named by the game's file, an archive's by the archive (a .tar.gz's without its .tar), as melonDS's are
		std::string stem = DsStem(game);
		std::string inner = fs::path(stem).extension().string();
		std::transform(inner.begin(), inner.end(), inner.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (inner == ".tar")
			stem = fs::path(stem).stem().string();
		return fmt::format("{}/states/{}.ml{}", ps5paths::MelonDs(), stem, kResumeSlot);
	}

	std::vector<GameSave> FindGameSaves(const GameRef& game)
	{
		std::vector<GameSave> saves;
		std::error_code ec;
		auto keep = [&](GameSave save) {
			for (const std::string& path : save.paths)
				Measure(path, save);
			if (save.files > 0)
				saves.push_back(std::move(save));
		};
		if (game.side == 0)
		{
			// mlc01/usr/save/<high>/<low>/user/<account> and user/common
			const fs::path user = fs::path(ps5paths::Mlc()) / "usr/save" / Hex8((uint32_t)(game.titleId >> 32)) / Hex8((uint32_t)game.titleId) / "user";
			std::vector<GameSave> accounts;
			for (auto it = fs::directory_iterator(user, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
			{
				if (!it->is_directory(ec))
					continue;
				const std::string name = it->path().filename().string();
				GameSave save;
				save.paths = {it->path().string()};
				if (name == "common")
					save.kind = GameSave::Kind::Common;
				else if (name.size() == 8 && name.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
				{
					save.kind = GameSave::Kind::Account;
					save.account = (uint32_t)std::strtoul(name.c_str(), nullptr, 16);
				}
				else
					continue;
				keep(std::move(save));
			}
			// the accounts in their order, the one every account shares last
			std::sort(saves.begin(), saves.end(), [](const GameSave& a, const GameSave& b) {
				return a.kind != b.kind ? a.kind == GameSave::Kind::Account : a.account < b.account;
			});
		}
		else if (game.side == 1)
		{
			// azahar/sdmc/Nintendo 3DS/<id0>/<id1>/title/<high>/<low>/data, and extdata/00000000/<unique ID>
			const fs::path sdmc = fs::path(ps5paths::Azahar()) / "sdmc/Nintendo 3DS";
			GameSave data, extra;
			data.kind = GameSave::Kind::Data;
			extra.kind = GameSave::Kind::ExtData;
			for (auto id0 = fs::directory_iterator(sdmc, ec); !ec && id0 != fs::directory_iterator(); id0.increment(ec))
			{
				std::error_code inner;
				for (auto id1 = fs::directory_iterator(id0->path(), inner); !inner && id1 != fs::directory_iterator(); id1.increment(inner))
				{
					const fs::path title = id1->path() / "title" / Hex8((uint32_t)(game.titleId >> 32)) / Hex8((uint32_t)game.titleId) / "data";
					const fs::path ext = id1->path() / "extdata" / "00000000" / ExtDataId(game.titleId);
					std::error_code check;
					if (fs::is_directory(title, check))
						data.paths.push_back(title.string());
					if (fs::is_directory(ext, check))
						extra.paths.push_back(ext.string());
				}
			}
			keep(std::move(data));
			keep(std::move(extra));
			GameSave states;
			states.kind = GameSave::Kind::States;
			const std::string prefix = fmt::format("{:016X}.", game.titleId);
			for (auto it = fs::directory_iterator(fs::path(ps5paths::Azahar()) / "states", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
				if (it->path().filename().string().rfind(prefix, 0) == 0)
					states.paths.push_back(it->path().string());
			keep(std::move(states));
		}
		else if (game.side == 3 && SwitchTitle(game))
		{
			const fs::path base = fs::path(ps5eden::DataFolder()) / "user/nand/user/save";
			GameSave save;
			for (auto user = fs::directory_iterator(base / "0000000000000000", ec); !ec && user != fs::directory_iterator(); user.increment(ec))
			{
				std::error_code inner;
				if (!user->is_directory(inner) || user->is_symlink(inner)) continue;
				for (auto title = fs::directory_iterator(user->path(), inner); !inner && title != fs::directory_iterator(); title.increment(inner))
				{
					const std::string relative = title->path().lexically_relative(base).generic_string() + "/save";
					if (title->is_directory(inner) && !title->is_symlink(inner) && SwitchSave(game, relative)) save.paths.push_back(title->path().string());
				}
			}
			keep(std::move(save));
		}
		else if (game.side == 2)
		{
			// melonds/saves/<the game's file>.sav, and melonds/states/<the game's file>.ml<slot>
			const std::string stem = DsStem(game);
			GameSave save;
			save.kind = GameSave::Kind::Data;
			save.paths = {(fs::path(ps5paths::MelonDs()) / "saves" / (stem + ".sav")).string()};
			keep(std::move(save));
			GameSave slot2;
			slot2.kind = GameSave::Kind::Slot2;
			slot2.paths = {(fs::path(ps5paths::MelonDs()) / "slot2-saves" / (stem + ".sav")).string()};
			keep(std::move(slot2));
			GameSave states;
			states.kind = GameSave::Kind::States;
			for (auto it = fs::directory_iterator(fs::path(ps5paths::MelonDs()) / "states", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
			{
				const std::string name = it->path().filename().string();
				if (name.rfind(stem + ".ml", 0) == 0 && name.size() == stem.size() + 4)
					states.paths.push_back(it->path().string());
			}
			keep(std::move(states));
		}
		return saves;
	}

	std::string MoveAside(const GameSave& save, std::string& error)
	{
		if (Current().state == Status::State::Running)
		{
			error = ps5lang::Tr("A backup is already running.");
			return {};
		}
		std::error_code ec;
		const std::string stamp = Stamp();
		const fs::path aside = save.paths.empty() ? fs::path(ps5paths::Root()) / "saves-before" / stamp : KeptPath(stamp, save.paths.front()).parent_path();
		for (const std::string& path : save.paths)
		{
			const fs::path kept = KeptPath(stamp, path);
			if (fs::exists(kept, ec))
			{
				error = ps5lang::Tr("A save moved aside a moment ago is in the way: try again in a moment.");
				return {};
			}
			fs::create_directories(kept.parent_path(), ec);
			fs::rename(path, kept, ec);
			if (ec)
			{
				error = ps5lang::TrF("A save could not be moved aside: {0}", path);
				ps5log::Line("[saves] moving {} aside: {}", path, ec.message());
				return {};
			}
		}
		ps5log::Line("[saves] {} moved aside to {}", save.paths.empty() ? std::string() : save.paths.front(), aside.string());
		return aside.string();
	}

	bool StartGameBackup(const GameRef& game, std::string& error)
	{
		fs::path folder;
		return NewBackupFolder(folder, error) && Begin(folder, false, error, game);
	}

	std::vector<Backup> FindGameBackups(const GameRef& game)
	{
		std::vector<Backup> found;
		const std::string high = Hex8((uint32_t)(game.titleId >> 32)), low = Hex8((uint32_t)game.titleId);
		for (const Backup& backup : FindBackups())
		{
			std::error_code ec;
			const fs::path folder = backup.folder;
			bool has = false;
			if (game.side == 0)
				has = fs::is_directory(folder / "wiiu/save" / high / low, ec);
			else if (game.side == 1)
			{
				const std::string prefix = fmt::format("{:016X}.", game.titleId);
				for (auto it = fs::directory_iterator(folder / "3ds/states", ec); !has && !ec && it != fs::directory_iterator(); it.increment(ec))
					has = it->path().filename().string().rfind(prefix, 0) == 0;
				std::error_code sdmcEc;
				for (auto id0 = fs::directory_iterator(folder / "3ds/sdmc", sdmcEc); !has && !sdmcEc && id0 != fs::directory_iterator(); id0.increment(sdmcEc))
				{
					std::error_code inner;
					for (auto id1 = fs::directory_iterator(id0->path(), inner); !has && !inner && id1 != fs::directory_iterator(); id1.increment(inner))
					{
						std::error_code check;
						has = fs::is_directory(id1->path() / "title" / high / low / "data", check) ||
							fs::is_directory(id1->path() / "extdata" / "00000000" / ExtDataId(game.titleId), check);
					}
				}
			}
			else if (game.side == 3)
			{
				const fs::path saves = folder / "switch/save";
				for (auto it = fs::recursive_directory_iterator(saves, ec); !has && !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
					if (it->is_regular_file(ec) && !it->is_symlink(ec))
						has = SwitchSave(game, it->path().lexically_relative(saves).generic_string());
			}
			else if (game.side == 2)
			{
				const std::string stem = DsStem(game);
				has = fs::exists(folder / "ds/saves" / (stem + ".sav"), ec) || fs::exists(folder / "ds/slot2-saves" / (stem + ".sav"), ec);
				for (int slot = 1; !has && slot <= 9; slot++)
					has = fs::exists(folder / "ds/states" / fmt::format("{}.ml{}", stem, slot), ec);
			}
			if (has)
				found.push_back(backup);
		}
		return found;
	}

	bool StartGameRestore(const Backup& backup, const GameRef& game, std::string& error)
	{
		std::error_code ec;
		if (!fs::is_directory(backup.folder, ec))
		{
			error = ps5lang::Tr("That backup is not there any more.");
			return false;
		}
		return Begin(backup.folder, true, error, game);
	}

	std::vector<Backup> FindBackups()
	{
		std::vector<Backup> backups;
		std::error_code ec;
		std::vector<std::string> drives;
		for (int i = 0; i < 8; i++)
			drives.push_back(UsbDrive(i));
		drives.push_back(std::string(PS5CEMU_USB_ROOT) + "/ext0");
		drives.push_back(std::string(PS5CEMU_USB_ROOT) + "/ext1");
		for (const std::string& drive : drives)
		{
			const fs::path folder = fs::path(drive) / kFolder;
			if (!fs::is_directory(folder, ec))
				continue;
			for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
			{
				if (!it->is_directory(ec) || !fs::exists(it->path() / "backup.json", ec))
					continue;
				Backup backup;
				backup.folder = it->path().string();
				backup.drive = drive;
				const std::string name = it->path().filename().string(); // 2026-10-12 214000
				backup.when = name.size() >= 17 ? name.substr(0, 13) + ":" + name.substr(13, 2) : name;
				std::ifstream json(it->path() / "backup.json");
				const std::string text((std::istreambuf_iterator<char>(json)), std::istreambuf_iterator<char>());
				auto number = [&](const char* key) {
					const size_t at = text.find(key);
					return at == std::string::npos ? 0ull : std::strtoull(text.c_str() + at + std::strlen(key), nullptr, 10);
				};
				backup.files = (int)number("\"files\": ");
				backup.bytes = number("\"bytes\": ");
				backups.push_back(backup);
			}
		}
		std::sort(backups.begin(), backups.end(), [](const Backup& a, const Backup& b) {
			return fs::path(a.folder).filename().string() > fs::path(b.folder).filename().string();
		});
		return backups;
	}

	bool StartBackup(std::string& error)
	{
		fs::path folder;
		return NewBackupFolder(folder, error) && Begin(folder, false, error);
	}

	bool StartRestore(const Backup& backup, std::string& error)
	{
		std::error_code ec;
		if (!fs::is_directory(backup.folder, ec))
		{
			error = ps5lang::Tr("That backup is not there any more.");
			return false;
		}
		return Begin(backup.folder, true, error);
	}

	namespace
	{
		bool StartTransfer(const GameRef& game, const std::string& profile, const std::string& source, bool importing, std::string& error, uint64_t accountSaveId = 0)
		{
			if (game.side != 3 || !SwitchTitle(game) || profile.size() != 32 ||
				profile.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
			{ error = ps5lang::Tr("Choose a player first, for a game with a title ID."); return false; }
			if (Current().state == Status::State::Running) { error = ps5lang::Tr("A backup is already running."); return false; }
			if (s_thread.joinable()) s_thread.join();
			{
				std::lock_guard lock(s_mutex);
				s_status = {}; s_status.finished = s_finished;
				s_status.state = Status::State::Running; s_status.restoring = importing;
			}
			s_thread = std::thread([game, profile, source, importing, accountSaveId] {
				try
				{
					const std::string title = fmt::format("{:016X}", game.titleId);
					const fs::path base = fs::path(ps5eden::DataFolder()) / "user/nand/user/save/0000000000000000";
					const std::string account = (base / profile / title).string();
					const std::string device = (base / std::string(32, '0') / title).string();
					std::string why, where;
					bool done = false;
					if (importing)
					{
						const auto sources = ps5eden::transfer::Discover(source, game.titleId, why, accountSaveId);
						if (!sources.empty()) done = ps5eden::transfer::Import(sources, account, device,
							ps5eden::DataFolder() + "/save-transfer-recovery", where, why);
					}
					else
					{
						for (int drive = 0; drive < 8 && !done; drive++)
						{
							std::error_code ec;
							const fs::path usb = UsbDrive(drive);
							if (!fs::is_directory(usb, ec)) continue;
							const fs::path exports = usb / "PS5-ECAM-exports";
							fs::create_directories(exports, ec);
							if (ec) continue;
							for (int suffix = 0; suffix < 1000; suffix++)
							{
								const fs::path folder = exports / fmt::format("{}-{:03d}", Stamp(), suffix);
								if (!fs::create_directory(folder, ec)) { if (ec && ec != std::errc::file_exists) break; ec.clear(); continue; }
								where = (folder / title).string();
								done = ps5eden::transfer::Export(account, device, where, why);
								break;
							}
						}
						if (!done && why.empty()) why = ps5lang::Tr("No USB drive found: plug one in, formatted exFAT or FAT32.");
					}
					{
						std::lock_guard lock(s_mutex); s_status.where = where;
					}
					SetState(done ? Status::State::Done : Status::State::Failed, done ? where : why);
				}
				catch (const std::exception& failure) { SetState(Status::State::Failed, failure.what()); }
			});
			return true;
		}
	}
	bool StartSwitchImport(const GameRef& game, const std::string& profile, const std::string& sourceRoot, std::string& error, uint64_t accountSaveId)
	{
		return StartTransfer(game, profile, sourceRoot, true, error, accountSaveId);
	}
	bool StartSwitchExport(const GameRef& game, const std::string& profile, std::string& error)
	{
		return StartTransfer(game, profile, {}, false, error);
	}

	Status Current()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	void Finish()
	{
		if (s_thread.joinable())
			s_thread.join();
	}
}
