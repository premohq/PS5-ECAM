// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "../app/own_status.h"
#if defined(__PROSPERO__)
#include "../ps5/log.h"
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <fstream>
#include <cstdio>
#include <rapidjson/document.h>
#include <memory>
#include <map>
#include <thread>
namespace ps5retro
{
	namespace fs = std::filesystem;
	namespace
	{
		std::string Extension(const fs::path& path)
		{
			auto ext = path.extension().string();
			for (char& c : ext) c = char(std::tolower((unsigned char)c));
			return ext;
		}

		bool DiscMatches(std::string_view side, const fs::path& path)
		{
			if (side != "gamecube" && side != "wii") return true;
			const auto ext = Extension(path);
			// Compressed containers need the native disc reader. Inspect only raw ISO/GCM
			// headers here, without unpacking, renaming or copying the user's discs.
			if (ext != ".iso" && ext != ".gcm") return true;
			unsigned char header[0x20]{};
			std::ifstream in(path, std::ios::binary);
			if (!in.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
			const bool wii = header[0x18] == 0x5d && header[0x19] == 0x1c && header[0x1a] == 0x9e && header[0x1b] == 0xa3;
			const bool gc = header[0x1c] == 0xc2 && header[0x1d] == 0x33 && header[0x1e] == 0x9f && header[0x1f] == 0x3d;
			return side == "wii" ? wii : gc && !wii;
		}
	}

	bool HasExtension(std::string_view side, std::string name)
	{
		for (auto& c : name) c = char(std::tolower((unsigned char)c));
		const auto dot = name.find_last_of('.');
		if (dot == std::string::npos) return false;
		const auto ext = name.substr(dot);
		if (side == "gb") return ext == ".gb";
		if (side == "gbc") return ext == ".gbc";
		if (side == "gba") return ext == ".gba" || ext == ".zip" || ext == ".7z";
		if (side == "nes") return ext == ".nes" || ext == ".unf" || ext == ".unif" || ext == ".fds" || ext == ".zip" || ext == ".7z";
		if (side == "snes") return ext == ".sfc" || ext == ".smc" || ext == ".swc" || ext == ".fig" || ext == ".bs" || ext == ".st";
		if (side == "vb") return ext == ".vb" || ext == ".vboy";
		if (side == "pokemini") return ext == ".min";
		if (side == "n64") return ext == ".z64" || ext == ".n64" || ext == ".v64" || ext == ".zip" || ext == ".rom";
		if (side == "gamecube" || side == "wii")
			return ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".ciso" || ext == ".gcz" || ext == ".wia" || (side == "wii" && (ext == ".wbfs" || ext == ".wad" || ext == ".dol" || ext == ".elf"));
		return false;
	}

	namespace
	{
		// Share immutable parsed sidecars between scanner and artwork lookups. Avoid rereading one
		// multi-megabyte index for every game; changed size/mtime invalidates it at the next lookup.
		std::shared_ptr<const rapidjson::Document> Metadata(const fs::path& path)
		{
			struct Cached { uintmax_t size; fs::file_time_type changed; std::shared_ptr<rapidjson::Document> doc; };
			static std::mutex mutex; static std::map<std::string, Cached> cache;
			std::error_code ec;
			if (!fs::is_regular_file(ps5files::OwnStatus(path, ec)) || ec) return {};
			const auto size = fs::file_size(path, ec); if (ec || size > 8 * 1024 * 1024) return {};
			const auto changed = fs::last_write_time(path, ec); if (ec) return {};
			std::lock_guard lock(mutex); const auto key = path.string(); const auto found = cache.find(key);
			if (found != cache.end() && found->second.size == size && found->second.changed == changed) return found->second.doc;
			std::ifstream stream(path, std::ios::binary); std::string text(size_t(size), '\0');
			stream.read(text.data(), std::streamsize(text.size())); if (stream.gcount() != std::streamsize(size)) return {};
			auto doc = std::make_shared<rapidjson::Document>(); doc->Parse(text.data(), text.size());
			if (doc->HasParseError() || !doc->IsObject() || !doc->HasMember("games") || !(*doc)["games"].IsObject()) return {};
			if (cache.size() >= 4 && found == cache.end()) cache.erase(cache.begin());
			cache.insert_or_assign(key, Cached{size, changed, doc}); return doc;
		}
	}
	std::string CoverPath(int side, const ps5emu::Game& game)
	{
		if (!IsRetro(side)) return {};
		// Look through ancestor sidecars up to the scanner's depth. Artwork is restricted to that root.
		for (auto root = game.path.parent_path(); !root.empty(); root = root.parent_path())
		{
			const auto relativeRom = game.path.lexically_relative(root);
			if (std::distance(relativeRom.begin(), relativeRom.end()) > 5) break;
			std::error_code ec; const auto metadata = Metadata(root / ".ecam-library.json");
			if (metadata)
			{
				const auto& doc = *metadata;
				const auto key = game.path.lexically_relative(root).generic_string();
				if (doc["games"].HasMember(key.c_str()))
				{
					const auto& entry = doc["games"][key.c_str()];
					if (entry.IsObject() && entry.HasMember("image") && entry["image"].IsString())
					{
						const auto image = fs::weakly_canonical(root / entry["image"].GetString(), ec);
						const auto relative = image.lexically_relative(fs::weakly_canonical(root, ec)).generic_string();
						const auto ext = image.extension().string();
						if (!ec && !relative.empty() && relative != ".." && relative.rfind("../", 0) != 0 && (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") && fs::is_regular_file(image, ec) && !ec && fs::file_size(image, ec) <= 16 * 1024 * 1024 && !ec) return image.string();
					}
				}
			}
			if (root == root.parent_path()) break;
		}
		const auto stem = game.path.stem().string();
		std::vector<fs::path> folders{game.path.parent_path(), game.path.parent_path() / "covers"};
		for (int backend = 0; backend < 2; backend++)
		{
			const fs::path data = BackendFor(side, backend).data;
			folders.push_back(data / "covers"); folders.push_back(data / "cache");
			if (side == 7) folders.push_back(data / "covers" / ((game.path.extension() == ".fds" || game.path.extension() == ".FDS") ? "FDS" : "NES"));
		}
		std::vector<std::string> names{stem, game.name};
		if (side >= 6)
		{
			uint32_t hash = 2166136261u; for (unsigned char c : game.path.string()) { hash ^= c; hash *= 16777619u; }
			char serial[20]; std::snprintf(serial, sizeof(serial), "%s-%08X", side == 6 ? "N64" : side == 7 ? "NES" : side == 10 ? "GBA" : "GBC", hash); names.push_back(serial);
		}
		for (const auto& folder : folders)
			for (const auto& name : names)
			{
				if (name.empty() || name.find_first_of("/\\") != std::string::npos || name == "." || name == "..") continue;
				for (const char* ext : {".png", ".jpg", ".jpeg", ".tga"})
				{
					const auto file = folder / (name + ext); std::error_code ec;
					if (fs::is_regular_file(ps5files::OwnStatus(file, ec)) && !ec && fs::file_size(file, ec) <= 16 * 1024 * 1024 && !ec) return file.string();
				}
			}
		return {};
	}
	bool ValidateRom(int side, const std::string& path, std::string& error, int backend)
	{
		if (!IsRetro(side) || !HasExtension(Of(side).key, path)) { error = "Unsupported game format."; return false; }
		std::error_code ec;
		const auto size = fs::file_size(path, ec);
		if (ec || size == 0) { error = "The game file is empty or cannot be read."; return false; }
		if (side < 6)
		{
			if (!DiscMatches(Of(side).key, path)) { error = "The disc header is unreadable or does not match the selected system."; return false; }
			error.clear(); return true;
		}
		std::string ext = fs::path(path).extension().string();
		for (char& c : ext) c = char(std::tolower((unsigned char)c));
		if (side >= 8)
		{
			const uint64_t maximum = (ext == ".zip" || ext == ".7z") ? 64ull * 1024 * 1024 : 32ull * 1024 * 1024;
			if (size > maximum) { error = "The handheld game exceeds this runtime's file-size limit."; return false; }
			if (ext == ".gb" || ext == ".gbc" || ext == ".gba")
			{
				const uint64_t minimum = side == 10 ? 0xc0 : 0x150;
				if (size < minimum) { error = "The handheld cartridge header is truncated."; return false; }
				std::ifstream in(path, std::ios::binary); if (!in) { error = "The handheld game cannot be read."; return false; }
				error.clear(); return true; // The native mGBA loader validates complete cartridge contents.
			}
		}
		if (side < 8 && backend == 0 && (ext == ".7z" || ext == ".rom")) { error = "This format requires the optional alternative runtime. Select it under Emulator runtime."; return false; }
		const uint64_t limit = side == 6 ? (backend == 1 ? 252ull : 64ull) * 1024 * 1024 : 64ull * 1024 * 1024;
		if (size > limit && ext != ".zip" && ext != ".7z") { error = "The ROM exceeds this backend's supported size limit."; return false; }
		unsigned char h[16]{}; std::ifstream in(path, std::ios::binary); in.read(reinterpret_cast<char*>(h), sizeof(h));
		if (ext == ".zip")
		{
			if (size < 22 || size > 512ull * 1024 * 1024 || h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4) { error = "Invalid or oversized ZIP archive."; return false; }
			// The native loader selects members, limits decompression and validates the contents.
		}
		else if (ext == ".7z")
		{
			if (size < 32 || size > 512ull * 1024 * 1024 || h[0] != '7' || h[1] != 'z' || h[2] != 0xbc || h[3] != 0xaf || h[4] != 0x27 || h[5] != 0x1c) { error = "Invalid or oversized 7-Zip archive."; return false; }
		}
		else if (side == 6)
		{
			const bool magic = (h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40) ||
				(h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) || (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80);
			if (size < 64 || !magic) { error = "The N64 cartridge header is invalid or truncated."; return false; }
		}
		else if (ext == ".nes" && (size < 16 || h[0] != 'N' || h[1] != 'E' || h[2] != 'S' || h[3] != 0x1a))
		{ error = "The NES cartridge header is invalid or truncated."; return false; }
		else if ((ext == ".unf" || ext == ".unif") && (size < 32 || std::string(reinterpret_cast<char*>(h), 4) != "UNIF"))
		{ error = "The UNIF cartridge header is invalid or truncated."; return false; }
		else if (ext == ".fds")
		{
			const fs::path data = BackendFor(side, backend).data;
			const fs::path bios = backend == 1 ? data / "bios/disksys.rom" : data / "disksys.rom";
			if (!fs::is_regular_file(bios, ec) || fs::file_size(bios, ec) != 8192 || ec) { error = "Famicom Disk System requires your 8192-byte BIOS at " + bios.string(); return false; }
		}
		error.clear(); return true;
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
			const auto status = ps5files::OwnStatus(it->path(), statusError);
			if (statusError || !fs::is_regular_file(status) || !HasExtension(side, it->path().filename().string()) || !DiscMatches(side, it->path())) continue;
			ps5emu::Game game;
			game.path = it->path(); game.name = game.path.stem().string();
			game.format = game.path.extension().string().substr(1); for (char& c : game.format) c = char(std::toupper((unsigned char)c));
			// Stable identity in a dedicated namespace. Include system to keep GC/Wii lists independent.
			uint64_t hash = 14695981039346656037ull;
			for (unsigned char c : side + ":" + fs::absolute(game.path).lexically_normal().string()) { hash ^= c; hash *= 1099511628211ull; }
			game.titleId = 0x6000000000000000ull | (hash & 0x00ffffffffffffffull);
			result.push_back(std::move(game));
		}
		if (ec) error = "The games folder could not be fully scanned: " + ec.message();
		else if (inspected <= 100000) error.clear();
#if defined(__PROSPERO__)
		ps5log::Line("[retro] {} scan {}: {} entries, {} games; {}", side, folder, inspected, result.size(), error);
#endif
		const auto metadata = Metadata(fs::path(folder) / ".ecam-library.json");
		if (metadata)
		{
			const auto& doc = *metadata;
			if (doc.HasMember("games"))
				for (auto& game : result)
				{
					const auto key = game.path.lexically_relative(folder).generic_string(); const auto entry = doc["games"].FindMember(key.c_str());
					if (entry == doc["games"].MemberEnd() || !entry->value.IsObject()) continue;
					const auto name = entry->value.FindMember("name");
					if (name != entry->value.MemberEnd() && name->value.IsString() && name->value.GetStringLength() > 0 && name->value.GetStringLength() <= 512)
						game.name.assign(name->value.GetString(), name->value.GetStringLength());
				}
		}
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
		return result;
	}

	int IdentifySystem(const std::string& path)
	{
		const auto ext = Extension(path);
		if (ext == ".zip" || ext == ".7z" || ext == ".elf") return -1;
		for (int side = kFirst; side < kSideCount; side++)
			if (HasExtension(Of(side).key, path) && DiscMatches(Of(side).key, path))
			{
				// Compressed GC/Wii containers cannot be assigned by extension alone.
				if (side < 6 && ext != ".iso" && ext != ".gcm" && ext != ".wbfs" && ext != ".wad" && ext != ".dol") return -1;
				return side;
			}
		return -1;
	}

	namespace
	{
		struct State
		{
			std::thread worker; std::atomic_bool scanning{false}; std::mutex mutex;
			std::vector<ps5emu::Game> games; std::string error, nextFolder;
			bool pending = false; // nextFolder asked for while a scan ran
			~State() { if (worker.joinable()) worker.join(); }
		};
		State states[kSystems.size()];
		State& StateFor(int side) { return states[size_t(side - kFirst)]; }
	}
	void StartScan(int side, const std::string& folder)
	{
		if (!IsRetro(side)) return;
		auto& state = StateFor(side);
		{
			std::lock_guard lock(state.mutex);
			if (state.scanning) { state.nextFolder = folder; state.pending = true; return; }
		}
		if (state.worker.joinable()) state.worker.join();
		state.scanning = true;
		try { state.worker = std::thread([&state, side, folder] {
			std::string current = folder;
			for (;;)
			{
				std::string error; std::vector<ps5emu::Game> games;
				try { games = Scan(Of(side).key, current, error); } catch (const std::exception& e) { error = e.what(); }
				std::lock_guard lock(state.mutex);
				if (state.pending)
				{
					current = std::move(state.nextFolder); state.pending = false;
					continue; // another folder was chosen while this one was scanned: only the newest one's games are kept
				}
				state.games = std::move(games); state.error = std::move(error); state.scanning = false; return;
			}
		}); } catch (...) { state.scanning = false; throw; }
	}
	bool Scanning(int side) { return IsRetro(side) && StateFor(side).scanning; }
	std::vector<ps5emu::Game> ListGames(int side) { if (!IsRetro(side)) return {}; auto& s = StateFor(side); std::lock_guard lock(s.mutex); return s.games; }
	std::string ScanError(int side) { if (!IsRetro(side)) return {}; auto& s = StateFor(side); std::lock_guard lock(s.mutex); return s.error; }
	void FinishScans() { for (auto& s : states) if (s.worker.joinable()) s.worker.join(); }
}
