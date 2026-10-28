// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <thread>
namespace ps5retro
{
	namespace fs = std::filesystem;
	bool HasExtension(std::string_view side, std::string name)
	{
		for (auto& c : name) c = char(std::tolower((unsigned char)c));
		const auto dot = name.find_last_of('.');
		if (dot == std::string::npos) return false;
		const auto ext = name.substr(dot);
		if (side == "nes") return ext == ".nes" || ext == ".unf" || ext == ".unif" || ext == ".fds" || ext == ".zip";
		if (side == "n64") return ext == ".z64" || ext == ".n64" || ext == ".v64" || ext == ".zip";
		if (side == "gamecube" || side == "wii")
			return ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".ciso" || ext == ".gcz" || ext == ".wbfs" || ext == ".wia" || (side == "wii" && (ext == ".wad" || ext == ".dol" || ext == ".elf"));
		return false;
	}
	std::vector<ps5emu::Game> Scan(const std::string& side, const std::string& folder, std::string& error)
	{
		std::vector<ps5emu::Game> result;
		if (!Find(side)) { error = "Unknown emulator system."; return result; }
		std::error_code ec;
		if (!fs::is_directory(folder, ec)) { error = "The games folder is not available: " + folder; return result; }
		fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
		size_t inspected = 0;
		for (; !ec && it != end; it.increment(ec))
		{
			if (++inspected > 100000) { error = "The scan reached its 100000-entry limit."; break; }
			if (it.depth() >= 4) it.disable_recursion_pending();
			std::error_code statusError;
			const auto status = it->symlink_status(statusError);
			if (statusError || !fs::is_regular_file(status) || !HasExtension(side, it->path().filename().string())) continue;
			ps5emu::Game game;
			game.path = it->path(); game.name = game.path.stem().string();
			// Stable identity in a dedicated namespace. Include system to keep GC/Wii lists independent.
			uint64_t hash = 14695981039346656037ull;
			for (unsigned char c : side + ":" + fs::absolute(game.path).lexically_normal().string()) { hash ^= c; hash *= 1099511628211ull; }
			game.titleId = 0x6000000000000000ull | (hash & 0x00ffffffffffffffull);
			result.push_back(std::move(game));
		}
		if (ec) error = "The games folder could not be fully scanned: " + ec.message();
		else if (inspected <= 100000) error.clear();
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
		return result;
	}
	namespace
	{
		struct State { std::thread worker; std::atomic_bool scanning{false}; std::mutex mutex; std::vector<ps5emu::Game> games; std::string error; ~State() { if (worker.joinable()) worker.join(); } };
		State states[4];
		State& StateFor(int side) { return states[size_t(side - 4)]; }
	}
	void StartScan(int side, const std::string& folder)
	{
		if (!IsRetro(side)) return;
		auto& state = StateFor(side);
		if (state.scanning) return;
		if (state.worker.joinable()) state.worker.join();
		state.scanning = true;
		try { state.worker = std::thread([&state, side, folder] {
			std::string error; std::vector<ps5emu::Game> games;
			try { games = Scan(Of(side).key, folder, error); } catch (const std::exception& e) { error = e.what(); }
			{ std::lock_guard lock(state.mutex); state.games = std::move(games); state.error = std::move(error); }
			state.scanning = false;
		}); } catch (...) { state.scanning = false; throw; }
	}
	bool Scanning(int side) { return IsRetro(side) && StateFor(side).scanning; }
	std::vector<ps5emu::Game> ListGames(int side) { if (!IsRetro(side)) return {}; auto& s = StateFor(side); std::lock_guard lock(s.mutex); return s.games; }
	std::string ScanError(int side) { if (!IsRetro(side)) return {}; auto& s = StateFor(side); std::lock_guard lock(s.mutex); return s.error; }
	void FinishScans() { for (auto& s : states) if (s.worker.joinable()) s.worker.join(); }
}
