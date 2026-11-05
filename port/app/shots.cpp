// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Orbit's pictures (shots.h).
#include "shots.h"

#include "log.h"
#include "paths.h"
#include "../retro/registry.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <curl/curl.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ps5shots
{
	namespace
	{
		constexpr size_t kMost = 6 * 1024 * 1024;

		// libretro-thumbnails' folder for a side, or nothing where it has none
		const char* Folder(int side)
		{
			if (side == 0)
				return "Nintendo - Wii U";
			if (side == 1)
				return "Nintendo - Nintendo 3DS";
			if (side == 2)
				return "Nintendo - Nintendo DS";
			if (!ps5retro::IsRetro(side))
				return nullptr; // the Switch and the Switch 2
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

		// its names as libretro's folders have them: these characters are underscores there
		std::string Clean(std::string name)
		{
			for (char& c : name)
				if (std::strchr("&*/:`<>?\\|\"", c))
					c = '_';
			return name;
		}

		struct Job
		{
			std::string file, folder;
			std::vector<std::string> names; // to try, in order
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
			auto* out = static_cast<std::vector<uint8_t>*>(user);
			const size_t bytes = size * count;
			if (out->size() + bytes > kMost)
				return 0;
			out->insert(out->end(), data, data + bytes);
			return bytes;
		}

		int Progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
		{
			return s_stop ? 1 : 0;
		}

		bool Get(CURL* curl, const std::string& folder, const std::string& name, std::vector<uint8_t>& out)
		{
			out.clear();
			char* escapedFolder = curl_easy_escape(curl, folder.c_str(), 0);
			char* escapedName = curl_easy_escape(curl, name.c_str(), 0);
			const std::string url = std::string("https://thumbnails.libretro.com/") + (escapedFolder ? escapedFolder : "") + "/Named_Snaps/" + (escapedName ? escapedName : "") + ".png";
			curl_free(escapedFolder);
			curl_free(escapedName);
			static const std::string ca = ps5paths::Assets() + "/cacert.pem";
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Receive);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Playtendoh/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &Progress);
			long status = 0;
			if (curl_easy_perform(curl) != CURLE_OK)
				return false;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			static constexpr uint8_t kPng[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
			return status == 200 && out.size() > 8 && std::memcmp(out.data(), kPng, 8) == 0;
		}

		void Work()
		{
			CURL* curl = curl_easy_init();
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
				std::vector<uint8_t> picture;
				bool found = false;
				for (const std::string& name : job.names)
					if (!s_stop && (found = Get(curl, job.folder, name, picture)))
						break;
				if (s_stop)
					break;
				std::error_code ec;
				fs::create_directories(fs::path(job.file).parent_path(), ec);
				// found, or an empty file so that it is asked for once
				std::ofstream(job.file, std::ios::binary).write(reinterpret_cast<const char*>(picture.data()), found ? (std::streamsize)picture.size() : 0);
				std::lock_guard lock(s_mutex);
				s_known[job.file] = found;
				ps5log::Line("[shots] {}: {}", job.names.front(), found ? "a screenshot" : "none");
			}
			if (curl)
				curl_easy_cleanup(curl);
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
		if (!stbi_write_png(path.c_str(), (int)width, (int)height, 4, rgba.data(), (int)width * 4))
			ps5log::Line("[shots] {} could not be written", path);
	}

	std::string Snap(int side, const ps5emu::Game& game)
	{
		const char* folder = Folder(side);
		if (!folder)
			return {};
		const std::string stem = game.path.stem().string();
		const std::string title = Clean(game.name.empty() ? stem : game.name);
		const std::string file = fmt::format("{}/snaps/{}/{}.png", ps5paths::Covers(), folder, Clean(stem));
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
		// a full name as libretro's has it (its region in brackets), else the game's in its regions
		Job job{file, folder, {}};
		if (stem.find('(') != std::string::npos)
			job.names.push_back(Clean(stem));
		for (const char* region : {" (USA)", " (World)", " (Europe)", " (Japan)"})
			job.names.push_back(title + region);
		// the last one asked for: the game in focus now, not those the row passed on its way
		s_job = std::move(job);
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
		}
		s_wake.notify_all();
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
	}
}
