// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_transfer.h"
#include "standalone/ryujinx_saves.h"
#include "../app/own_status.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <set>

namespace ps5eden::transfer
{
	namespace
	{
		namespace fs = std::filesystem;
		struct Budget { uint64_t bytes = 0; size_t files = 0; };
		constexpr uint64_t kBytes = 4ull << 30;
		void CheckPath(const fs::path& path)
		{
			// Check ancestors too: copying through a symlinked account folder is not safe.
			for (auto at = fs::absolute(path).lexically_normal(); !at.empty(); at = at.parent_path())
			{
				std::error_code error;
				// stat's where lstat is refused, as on the console's /data (app/own_status.h)
				const auto status = ps5files::OwnStatus(at, error);
				if (!error && fs::is_symlink(status)) throw std::runtime_error("A save path contains a symbolic link: " + path.string());
				if (error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("Cannot inspect save path", at, error);
				if (at == at.root_path()) break;
			}
		}
		// fs::remove_all without its lstat, which the console's /data refuses: what is in a folder, then it
		void RemoveAll(const fs::path& path, std::error_code& error)
		{
			const auto status = ps5files::OwnStatus(path, error);
			if (error == std::errc::no_such_file_or_directory) { error.clear(); return; }
			if (!error && fs::is_directory(status))
			{
				const auto entries = Eden::RyujinxSaves::ListFolder(path, error);
				for (size_t i = 0; i < entries.size() && !error; i++) RemoveAll(entries[i].path(), error);
			}
			if (!error) fs::remove(path, error);
		}
		bool Within(const fs::path& child, const fs::path& parent)
		{
			const auto relative = fs::absolute(child).lexically_normal().lexically_relative(fs::absolute(parent).lexically_normal());
			return !relative.empty() && *relative.begin() != "..";
		}
		void Copy(const fs::path& from, const fs::path& to, Budget& budget, unsigned depth = 0)
		{
			if (depth > 32) throw std::runtime_error("Save directory is too deep.");
			CheckPath(from);
			if (!fs::is_directory(from)) throw std::runtime_error("Save source is not a directory.");
			fs::create_directories(to);
			std::error_code error;
			const auto entries = Eden::RyujinxSaves::ListFolder(from, error);
			if (error) throw fs::filesystem_error("Cannot list save", from, error);
			for (const auto& entry : entries)
			{
				const auto status = ps5files::OwnStatus(entry.path(), error);
				if (error) throw fs::filesystem_error("Cannot inspect save", entry.path(), error);
				if (fs::is_symlink(status)) throw std::runtime_error("Symbolic links are not imported/exported.");
				const auto target = to / entry.path().filename();
				if (fs::is_directory(status)) Copy(entry.path(), target, budget, depth + 1);
				else if (fs::is_regular_file(status))
				{
					const auto size = entry.file_size();
					if (++budget.files > 100000 || size > kBytes - budget.bytes) throw std::runtime_error("Save exceeds the transfer limit (100000 files / 4 GiB).");
					budget.bytes += size;
					fs::copy_file(entry.path(), target);
					if (fs::file_size(target) != size) throw std::runtime_error("Save changed or a copy was incomplete.");
				}
				else throw std::runtime_error("Save contains an unsupported file type.");
			}
		}
		fs::path Unique(const fs::path& parent)
		{
			CheckPath(parent);
			fs::create_directories(parent);
			const auto time = std::chrono::system_clock::now().time_since_epoch();
			const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
			for (int i = 0; i < 1000; i++)
			{
				const fs::path path = parent / (std::to_string(stamp) + "-" + std::to_string(i));
				std::error_code error;
				if (fs::create_directory(path, error)) return path;
				if (error && error != std::errc::file_exists) throw fs::filesystem_error("Cannot stage save", path, error);
			}
			throw std::runtime_error("Cannot allocate a unique save-transfer directory.");
		}
	}

	namespace
	{
		struct Indexed { Kind kind; uint64_t id; std::string folder; };
		std::vector<Indexed> IndexedSaves(const std::string& root, uint64_t title)
		{
			if ((title >> 56) != 1) throw std::runtime_error("A real Switch title ID is required.");
			CheckPath(root);
			const auto data = Eden::RyujinxSaves::DataRoot(root);
			if (data.empty()) return {};
			const auto path = Eden::RyujinxSaves::IndexPath(data);
			CheckPath(path);
			// Read at most the declared bound, even if a removable file grows during the read.
			constexpr size_t limit = 16u << 20, header = 12, entrySize = 0x8C;
			const auto size = fs::file_size(path);
			if (size > limit) throw std::runtime_error("Ryujinx save index is too large.");
			std::vector<char> bytes((size_t)size);
			std::ifstream input(path, std::ios::binary);
			if (!input.read(bytes.data(), bytes.size()) || input.peek() != EOF || bytes.size() < header ||
				std::memcmp(bytes.data(), "IMKV", 4) || (bytes.size() - header) % entrySize)
				throw std::runtime_error("Ryujinx save index is unreadable.");
			std::vector<Indexed> result;
			std::set<uint64_t> seen;
			bool deviceSeen = false;
			for (size_t at = header; at < bytes.size(); at += entrySize)
			{
				const char* entry = bytes.data() + at;
				if (std::memcmp(entry, "IMEN", 4)) throw std::runtime_error("Ryujinx save index is unreadable.");
				uint64_t program = 0, id = 0;
				std::memcpy(&program, entry + 0x0C, 8); std::memcpy(&id, entry + 0x4C, 8);
				const auto kind = (unsigned char)entry[0x2C];
				if (program != title || (kind != 1 && kind != 3)) continue;
				if (!id) throw std::runtime_error("Ryujinx save index contains an unsupported zero save ID.");
				char name[17]; std::snprintf(name, sizeof(name), "%016llx", (unsigned long long)id);
				const auto folder = data / "bis/user/save" / name / "0";
				CheckPath(folder);
				if (!fs::is_directory(folder)) continue;
				if (!seen.insert(id).second || (kind == 3 && deviceSeen))
					throw std::runtime_error("Ryujinx save index has ambiguous duplicate saves.");
				deviceSeen = deviceSeen || kind == 3;
				result.push_back({kind == 1 ? Kind::Account : Kind::Device, id, folder.string()});
			}
			return result;
		}

		// An Eden user folder (standalone ProsperoEden's, kStandaloneUser): each player's saves in
		// nand/user/save/0000000000000000/<player ID, 32 hex digits>/<title ID>, the game's device save
		// under the player ID of zeros, and the players' names in Eden's player list (extras.cpp reads
		// the same: a 0x10-byte header, then eight 0xC8-byte players, each its ID's low and high halves
		// and its name at 0x28)
		constexpr const char* kPlayers = "nand/user/save/0000000000000000";
		bool PlayerId(const std::string& name)
		{
			return name.size() == 32 && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
		}
		bool NoPlayer(const std::string& id) { return id.find_first_not_of('0') == std::string::npos; }
		// A player's own folder of an Eden user folder, as a choice's folder names it
		bool PlayerFolder(const fs::path& folder)
		{
			return PlayerId(folder.filename().string()) && folder.parent_path().filename() == "0000000000000000" &&
				   folder.parent_path().parent_path().filename() == "save";
		}
		// The game's save in a player's folder when it holds anything: its title ID as Eden writes it, or in small letters
		fs::path GameIn(const fs::path& player, uint64_t title)
		{
			for (const char* format : {"%016llX", "%016llx"})
			{
				char name[17];
				std::snprintf(name, sizeof(name), format, (unsigned long long)title);
				const fs::path game = player / name;
				CheckPath(game);
				std::error_code error;
				if (fs::is_directory(game, error) && !Eden::RyujinxSaves::ListFolder(game, error).empty() && !error) return game;
			}
			return {};
		}
		std::string PlayerName(const fs::path& user, std::string id)
		{
			std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return (char)std::toupper(c); });
			std::array<unsigned char, 0x10 + 8 * 0xC8> data{};
			std::ifstream file(user / "nand/system/save/8000000000000010/su/avators/profiles.dat", std::ios::binary);
			if (!file.read((char*)data.data(), data.size())) return {};
			for (size_t i = 0; i < 8; i++)
			{
				const auto* entry = data.data() + 0x10 + i * 0xC8;
				uint64_t low = 0, high = 0;
				std::memcpy(&low, entry, 8); std::memcpy(&high, entry + 8, 8);
				char entryId[33];
				std::snprintf(entryId, sizeof(entryId), "%016llX%016llX", (unsigned long long)high, (unsigned long long)low);
				if (id != entryId) continue;
				const char* name = (const char*)entry + 0x28;
				return std::string(name, strnlen(name, 32));
			}
			return {};
		}
		// A choice for each player with the game's save, by name; the device save alone when no player has one
		std::vector<Choice> PlayerChoices(const fs::path& user, uint64_t title)
		{
			const fs::path players = user / kPlayers;
			CheckPath(players);
			std::error_code error;
			auto entries = Eden::RyujinxSaves::ListFolder(players, error);
			if (error) throw fs::filesystem_error("Cannot list Eden's saves", players, error);
			std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
			std::vector<Choice> choices;
			std::string device;
			for (const auto& entry : entries)
			{
				const std::string id = entry.path().filename().string();
				if (!PlayerId(id) || GameIn(entry.path(), title).empty()) continue;
				if (NoPlayer(id)) { device = entry.path().string(); continue; }
				const std::string name = PlayerName(user, id);
				choices.push_back({0, name.empty() ? "Player " + id : name, entry.path().string()});
			}
			if (choices.empty() && !device.empty()) choices.push_back({0, "Device save", device});
			return choices;
		}
	}

	std::vector<Choice> Choices(const std::string& root, uint64_t title, std::string& error)
	{
		try
		{
			const auto indexed = IndexedSaves(root, title);
			std::vector<Choice> choices;
			for (const auto& save : indexed)
				if (save.kind == Kind::Account)
				{
					char label[48]; std::snprintf(label, sizeof(label), "Account save %016llX", (unsigned long long)save.id);
					choices.push_back({save.id, label, {}});
				}
			if (choices.empty() && !indexed.empty()) choices.push_back({0, "Device save", {}});
			if (choices.empty() && !Eden::RyujinxSaves::DataRoot(root).empty())
			{ error = "No Ryujinx saves found for this game."; return {}; }
			if (choices.empty() && fs::is_directory(fs::path(root) / kPlayers))
			{
				choices = PlayerChoices(root, title);
				if (choices.empty()) error = "No saves found for this game.";
				else error.clear();
				return choices;
			}
			if (choices.empty() && !Discover(root, title, error).empty()) choices.push_back({0, "Plain folder", {}});
			if (!choices.empty()) error.clear();
			return choices;
		}
		catch (const std::exception& failure) { error = failure.what(); return {}; }
	}

	std::vector<Source> Discover(const std::string& root, uint64_t title, std::string& error, uint64_t accountSaveId)
	{
		try
		{
			const auto indexed = IndexedSaves(root, title);
			std::vector<Source> result;
			if (!Eden::RyujinxSaves::DataRoot(root).empty())
			{
				size_t accounts = 0; bool found = false;
				for (const auto& save : indexed) if (save.kind == Kind::Account) accounts++;
				if (!accountSaveId && accounts > 1) throw std::runtime_error("Choose a Ryujinx account save before importing.");
				for (const auto& save : indexed)
					if (save.kind == Kind::Device || !accountSaveId || save.id == accountSaveId)
					{ result.push_back({save.kind, save.folder}); if (save.kind == Kind::Account) found = true; }
				if (accountSaveId && !found) throw std::runtime_error("The selected Ryujinx account save is no longer available.");
			}
			else if (PlayerFolder(root))
			{
				// one player's save of an Eden user folder, and the game's device save beside it
				if (accountSaveId) throw std::runtime_error("The selected Ryujinx index is no longer available.");
				const fs::path player(root);
				CheckPath(player);
				if (const auto game = GameIn(player, title); !game.empty() && !NoPlayer(player.filename().string()))
					result.push_back({Kind::Account, game.string()});
				if (const auto game = GameIn(player.parent_path() / std::string(32, '0'), title); !game.empty())
					result.push_back({Kind::Device, game.string()});
			}
			else if (fs::is_directory(fs::path(root) / kPlayers))
			{
				// an Eden user folder: its one player with a save, else a choice first
				if (accountSaveId) throw std::runtime_error("The selected Ryujinx index is no longer available.");
				const auto choices = PlayerChoices(root, title);
				if (choices.size() > 1) throw std::runtime_error("Choose which player's save to take before importing.");
				if (choices.size() == 1) return Discover(choices[0].folder, title, error);
			}
			else
			{
				if (accountSaveId) throw std::runtime_error("The selected Ryujinx index is no longer available.");
				for (const auto& save : Eden::RyujinxSaves::FindFolderSaves(root, title, error))
				{
					CheckPath(save.folder);
					result.push_back({save.kind == Eden::RyujinxSaves::Kind::Account ? Kind::Account : Kind::Device, save.folder.string()});
				}
			}
			if (!result.empty()) error.clear();
			else if (error.empty()) error = "No saves found for this game.";
			return result;
		}
		catch (const std::exception& failure) { error = failure.what(); return {}; }
	}

	bool Import(const std::vector<Source>& sources, const std::string& account, const std::string& device,
		const std::string& recovery, std::string& kept, std::string& error)
	{
		struct Move { fs::path staged, target, before; bool old = false, installed = false; };
		std::vector<Move> moves;
		fs::path work;
		kept.clear();
		try
		{
			if (sources.empty() || sources.size() > 2) throw std::runtime_error("Choose one account and/or device save.");
			CheckPath(account); CheckPath(device); CheckPath(recovery);
			if (Within(account, device) || Within(device, account) || Within(recovery, account) || Within(recovery, device))
				throw std::runtime_error("Save destinations and recovery directory overlap.");
			for (const auto& source : sources)
			{
				if (Within(account, source.folder) || Within(device, source.folder) || Within(source.folder, account) || Within(source.folder, device) ||
					Within(recovery, source.folder) || Within(source.folder, recovery)) throw std::runtime_error("Source overlaps a destination or recovery directory.");
			}
			work = Unique(recovery);
			Budget budget;
			for (const auto& source : sources)
			{
				const bool accountSave = source.kind == Kind::Account;
				const fs::path target = accountSave ? account : device;
				if (std::any_of(moves.begin(), moves.end(), [&](const auto& move) { return move.target == target; })) throw std::runtime_error("Duplicate save type.");
				const auto name = accountSave ? "account" : "device";
				Move move{work / (std::string(name) + ".staged"), target, work / (std::string(name) + ".before")};
				const size_t before = budget.files;
				Copy(source.folder, move.staged, budget);
				if (budget.files == before) throw std::runtime_error("Empty saves are not imported.");
				moves.push_back(std::move(move));
			}
			// Nothing in the destination is changed until every source is staged successfully.
			for (auto& move : moves)
			{
				CheckPath(move.target);
				fs::create_directories(move.target.parent_path());
				if (fs::exists(move.target)) { fs::rename(move.target, move.before); move.old = true; }
				fs::rename(move.staged, move.target); move.installed = true;
			}
			kept = work.string();
			error.clear();
			return true;
		}
		catch (const std::exception& failure)
		{
			error = failure.what();
			bool rollback = true;
			for (auto it = moves.rbegin(); it != moves.rend(); ++it)
			{
				std::error_code ec;
				if (it->installed) RemoveAll(it->target, ec);
				if (ec) rollback = false;
				if (it->old) { ec.clear(); fs::rename(it->before, it->target, ec); if (ec) rollback = false; }
			}
			if (!work.empty())
			{
				if (rollback) { std::error_code ec; RemoveAll(work, ec); }
				else { kept = work.string(); error += " Recovery is incomplete; retain " + kept; }
			}
			return false;
		}
	}

	bool Export(const std::string& account, const std::string& device, const std::string& target, std::string& error)
	{
		fs::path work;
		try
		{
			CheckPath(target);
			if (fs::exists(target)) throw std::runtime_error("Export destination already exists.");
			if (Within(target, account) || Within(target, device) || Within(account, target) || Within(device, target)) throw std::runtime_error("Export and source overlap.");
			work = Unique(fs::path(target).parent_path());
			Budget budget;
			for (const auto& entry : {std::pair{account, "account"}, std::pair{device, "device"}})
				if (fs::is_directory(entry.first)) Copy(entry.first, work / entry.second, budget);
			if (!budget.files) throw std::runtime_error("No save files to export.");
			fs::rename(work, target);
			error.clear();
			return true;
		}
		catch (const std::exception& failure)
		{
			error = failure.what();
			if (!work.empty()) { std::error_code ec; RemoveAll(work, ec); }
			return false;
		}
	}
}
