// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: Orbit's pictures (shots.h).
#include "shots.h"

#include "paths.h"
#include "../ps5/curl_socket.h"
#include "../ps5/log.h"
#include "../ps5/net.h"
#include "../retro/registry.h"

// stb's PNG writer, static to this file, for a save state's picture
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include <curl/curl.h>
#include <fmt/format.h>
#include <zlib.h>

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ps5shots
{
	namespace
	{
		constexpr const char* kLibretro = "thumbnails.libretro.com";
		constexpr const char* kEshop = "img-eshop.cdn.nintendo.net";
		constexpr size_t kMost = 12 * 1024 * 1024; // a picture, or the longest folder's list (the NES's)

		bool IsSwitch(int side) { return side == 3 || side == ps5retro::kSideCount; } // the Switch and the Switch 2

		// libretro-thumbnails' folder for a side, or nothing where it has none. VS. System's games are the
		// NES's, the Super Game Boy's the Game Boy's and the iQue Player's the N64's. Game & Watch has none,
		// nor has MAME's folder: its save states' pictures are all it has
		const char* Folder(int side)
		{
			if (side == 0)
				return "Nintendo - Wii U";
			if (side == 1)
				return "Nintendo - Nintendo 3DS";
			if (side == 2)
				return "Nintendo - Nintendo DS";
			if (!ps5retro::IsRetro(side))
				return nullptr;
			static constexpr std::pair<std::string_view, const char*> kFolders[] = {{"gamecube", "Nintendo - GameCube"}, {"wii", "Nintendo - Wii"},
				{"n64", "Nintendo - Nintendo 64"}, {"ique", "Nintendo - Nintendo 64"}, {"nes", "Nintendo - Nintendo Entertainment System"},
				{"vs", "Nintendo - Nintendo Entertainment System"}, {"fds", "Nintendo - Family Computer Disk System"}, {"gb", "Nintendo - Game Boy"},
				{"sgb", "Nintendo - Game Boy"}, {"gbc", "Nintendo - Game Boy Color"}, {"gba", "Nintendo - Game Boy Advance"},
				{"snes", "Nintendo - Super Nintendo Entertainment System"}, {"satellaview", "Nintendo - Satellaview"}, {"vb", "Nintendo - Virtual Boy"},
				{"pokemini", "Nintendo - Pokemon Mini"}};
			for (const auto& [key, folder] : kFolders)
				if (key == ps5retro::Of(side).key)
					return folder;
			return nullptr;
		}

		// what a file's name is said by: the characters libretro's folders cannot have are underscores
		std::string Clean(std::string name)
		{
			for (char& c : name)
				if (std::strchr("&*/:`<>?\\|\"", c))
					c = '_';
			return name;
		}

		// a game's name to compare: letters and digits, without what is in brackets (its region, its languages)
		std::string Key(std::string_view name)
		{
			std::string key;
			int depth = 0;
			for (const char c : name)
			{
				if (c == '(' || c == '[')
					depth++;
				else if ((c == ')' || c == ']') && depth > 0)
					depth--;
				else if (!depth && (unsigned char)c < 0x80 && std::isalnum((unsigned char)c))
					key += (char)std::tolower((unsigned char)c);
			}
			return key;
		}

		struct Job
		{
			std::string file, folder, stem, title;
			uint64_t titleId = 0;
		};

		std::mutex s_mutex;
		std::condition_variable s_wake;
		std::thread s_thread;
		std::map<std::string, bool> s_known; // by file: true once it is there, false while asked for or not found
		bool s_pending = false;
		Job s_job;
		std::atomic<bool> s_stop{false};

		size_t Receive(char* data, size_t size, size_t count, void* user)
		{
			auto* out = static_cast<std::string*>(user);
			const size_t bytes = size * count;
			if (out->size() + bytes > kMost)
				return 0;
			out->append(data, bytes);
			return bytes;
		}

		int Progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
		{
			return s_stop ? 1 : 0;
		}

		// the HTTP status, or -1 when there was no answer
		long Get(CURL* curl, const std::string& url, std::string& out)
		{
			out.clear();
			static const std::string ca = ps5paths::Assets() + "/cacert.pem";
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Receive);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Playtendoh/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &Progress);
			if (curl_easy_perform(curl) != CURLE_OK)
				return -1;
			long status = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			return status;
		}

		std::string Escape(CURL* curl, const std::string& text)
		{
			char* escaped = curl_easy_escape(curl, text.c_str(), (int)text.size());
			const std::string out = escaped ? escaped : "";
			curl_free(escaped);
			return out;
		}

		std::string Url(CURL* curl, const std::string& folder, const std::string& name)
		{
			return fmt::format("https://{}/{}/Named_Snaps/{}", kLibretro, Escape(curl, folder), name.empty() ? "" : Escape(curl, name) + ".png");
		}

		// a folder's names, from its list, fetched once and kept beside its pictures
		const std::vector<std::string>& Names(CURL* curl, const std::string& folder)
		{
			static std::map<std::string, std::vector<std::string>> lists;
			if (const auto known = lists.find(folder); known != lists.end())
				return known->second;
			const std::string file = fmt::format("{}/snaps/{}/list.html", ps5paths::Covers(), folder);
			std::string page;
			{
				std::ifstream in(file, std::ios::binary);
				std::stringstream text;
				text << in.rdbuf();
				page = text.str();
			}
			if (page.empty() && Get(curl, Url(curl, folder, ""), page) == 200)
			{
				std::error_code ec;
				fs::create_directories(fs::path(file).parent_path(), ec);
				std::ofstream(file, std::ios::binary).write(page.data(), (std::streamsize)page.size());
			}
			std::vector<std::string>& names = lists[folder];
			static constexpr std::string_view kHref = "href=\"";
			for (size_t at = page.find(kHref); at != std::string::npos; at = page.find(kHref, at + 1))
			{
				const size_t start = at + kHref.size(), end = page.find('"', start);
				if (end == std::string::npos || end - start < 5 || page.compare(end - 4, 4, ".png") != 0)
					continue;
				int size = 0;
				char* plain = curl_easy_unescape(curl, page.c_str() + start, (int)(end - start - 4), &size);
				if (plain)
					names.emplace_back(plain, (size_t)size);
				curl_free(plain);
			}
			return names;
		}

		// the name in the folder's list: the file's own, else the game's, its USA release first
		std::string Best(const std::vector<std::string>& names, const Job& job)
		{
			const std::string stem = Clean(job.stem);
			for (const std::string& name : names)
				if (name == stem)
					return name;
			const std::string wanted = Key(job.title), wantedStem = Key(job.stem);
			std::string best;
			int bestRank = 100;
			for (const std::string& name : names)
			{
				const std::string key = Key(name);
				if (key != wanted && key != wantedStem)
					continue;
				const int rank = name.find("(USA") != std::string::npos ? 0 : name.find("(World") != std::string::npos ? 1 :
					name.find("Europe") != std::string::npos ? 2 : name.find("Japan") != std::string::npos ? 3 : 4;
				if (rank < bestRank)
				{
					bestRank = rank;
					best = name;
				}
			}
			return best;
		}

		// the Switch's and Switch 2's eShop screenshots (tools/render-switch-shots.py): by title ID, else by name
		struct Eshop
		{
			std::map<uint64_t, std::string> byId;
			std::map<std::string, std::string> byName;
		};

		const Eshop& EshopTable()
		{
			static Eshop table;
			static bool read = false;
			if (read)
				return table;
			read = true;
			const std::string path = ps5paths::Assets() + "/gametdb/switch-shots.tsv.gz";
			gzFile file = gzopen(path.c_str(), "rb");
			if (!file)
				return table;
			std::string text;
			std::vector<char> buffer(1 << 16);
			for (int count; (count = gzread(file, buffer.data(), (unsigned)buffer.size())) > 0;)
				text.append(buffer.data(), (size_t)count);
			gzclose(file);
			std::istringstream lines(text);
			for (std::string line; std::getline(lines, line);)
			{
				const size_t first = line.find('\t'), second = line.find('\t', first + 1);
				if (first == std::string::npos || second == std::string::npos)
					continue;
				const std::string hash = line.substr(first + 1, second - first - 1);
				table.byId[std::strtoull(line.c_str(), nullptr, 16)] = hash;
				table.byName.emplace(line.substr(second + 1), hash);
			}
			return table;
		}

		// the picture of a job: its status, and the bytes when it is 200; 404 for a game no one has
		long FetchPicture(CURL* curl, const Job& job, std::string& picture)
		{
			if (job.folder.empty())
			{
				const Eshop& table = EshopTable();
				const auto byId = table.byId.find(job.titleId);
				const auto byName = table.byName.find(Key(job.title));
				const std::string* hash = byId != table.byId.end() ? &byId->second : byName != table.byName.end() ? &byName->second : nullptr;
				return hash ? Get(curl, fmt::format("https://{}/i/{}.jpg", kEshop, *hash), picture) : 404;
			}
			const std::vector<std::string>& names = Names(curl, job.folder);
			if (names.empty())
				return -1; // its list did not come: tried again at the next start
			const std::string name = Best(names, job);
			return name.empty() ? 404 : Get(curl, Url(curl, job.folder, name), picture);
		}

		void Work()
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolved = ps5net::AddResolve(ps5net::AddResolve(nullptr, kLibretro, 443), kEshop, 443);
			static curl_slist* const headers = ps5curl::Headers();
			CURL* curl = curl_easy_init();
			if (curl)
			{
				ps5curl::Setup(curl);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				if (resolved)
					curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);
			}
			while (curl)
			{
				Job job;
				{
					std::unique_lock lock(s_mutex);
					s_wake.wait(lock, [] { return s_pending || s_stop; });
					if (s_stop)
						break;
					job = std::move(s_job);
					s_pending = false;
				}
				std::string picture;
				const long status = FetchPicture(curl, job, picture);
				if (s_stop)
					break;
				const bool found = status == 200 && picture.size() > 32;
				// found, or an empty file so that a game no one has is asked for once; no answer at all
				// leaves nothing, to be asked for at the next start
				if (found || status == 404)
				{
					std::error_code ec;
					fs::create_directories(fs::path(job.file).parent_path(), ec);
					std::ofstream(job.file, std::ios::binary).write(picture.data(), found ? (std::streamsize)picture.size() : 0);
				}
				std::lock_guard lock(s_mutex);
				s_known[job.file] = found;
				ps5log::Line("[shots] {}: {}", job.title, found ? "a screenshot" : status == 404 ? "none" : "no answer");
			}
			if (curl)
				curl_easy_cleanup(curl);
			if (resolved)
				curl_slist_free_all(resolved);
		}

		void WriteTo(void* context, void* data, int size)
		{
			static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
		}
	}

	void SaveStatePicture(const std::string& statePath, const uint32_t* pixels, unsigned width, unsigned height, size_t pitch)
	{
		if (!pixels || width == 0 || height == 0)
			return;
		std::vector<uint8_t> rgba(size_t(width) * height * 4);
		uint8_t* out = rgba.data();
		for (unsigned y = 0; y < height; y++)
			for (unsigned x = 0; x < width; x++, out += 4)
			{
				const uint32_t pixel = pixels[y * pitch + x];
				out[0] = pixel >> 16 & 0xff;
				out[1] = pixel >> 8 & 0xff;
				out[2] = pixel & 0xff;
				out[3] = 0xff;
			}
		const std::string path = statePath + ".png";
		std::ofstream file(path, std::ios::binary);
		if (!file || !stbi_write_png_to_func(WriteTo, &file, (int)width, (int)height, 4, rgba.data(), (int)width * 4))
			ps5log::Line("[shots] {} could not be written", path);
	}

	std::string Snap(int side, const ps5emu::Game& game)
	{
		const bool eshop = IsSwitch(side);
		const char* folder = Folder(side);
		if (!eshop && !folder)
			return {};
		const std::string stem = game.path.stem().string();
		const std::string file = eshop ? fmt::format("{}/snaps/Switch/{:016X}.jpg", ps5paths::Covers(), game.titleId) :
										 fmt::format("{}/snaps/{}/{}.png", ps5paths::Covers(), folder, Clean(stem));
		std::lock_guard lock(s_mutex);
		if (const auto known = s_known.find(file); known != s_known.end())
			return known->second ? file : std::string();
		std::error_code ec;
		const auto size = fs::file_size(file, ec);
		if (!ec)
		{
			s_known[file] = size > 0;
			return size > 0 ? file : std::string();
		}
		// the last one asked for, the game in focus now: not those the row passed on its way
		s_job = Job{file, eshop ? std::string() : std::string(folder), stem, game.name.empty() ? stem : game.name, game.titleId};
		s_pending = true;
		s_stop = false;
		if (!s_thread.joinable())
			s_thread = std::thread(Work);
		s_wake.notify_one();
		return {};
	}

	void Stop()
	{
		{
			std::lock_guard lock(s_mutex);
			s_stop = true;
			s_pending = false;
			// what it was asked for and did not finish is asked for again
			for (auto known = s_known.begin(); known != s_known.end();)
				known = known->second ? std::next(known) : s_known.erase(known);
		}
		s_wake.notify_all();
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
	}
}
