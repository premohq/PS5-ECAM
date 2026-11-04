// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarders.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"
#include "../ui/images.h"

#include "psfwd.h"

// stb's PNG writer, static to this file, for the tile's icon0.png
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ps5forwarders
{
	namespace
	{
		constexpr const char* kRoot = PSFWD_DEFAULT_ROOT; // /data/homebrew
		constexpr int kIconSize = 512;					  // the format's icon0.png
		constexpr int kElfldrPort = 9021;				  // the console's payload loader
		constexpr int kInstallPolls = 100;				  // 10 s for the helper to move a tile in

		std::mutex s_mutex;
		Status s_status; // under s_mutex
		std::thread s_thread;
		std::atomic<bool> s_stop{false};

		bool Exists(const std::string& path)
		{
			struct stat info{};
			return stat(path.c_str(), &info) == 0;
		}

		// Where a tile is made when /data/homebrew is out of the app's reach (port/payloads). In
		// /data/ps5cemu, where the helper looks, even with the app's data on its drive (#52): a tile
		// goes into /data/homebrew either way
		std::string Pending()
		{
			return std::string(ps5paths::kDefaultRoot) + "/tiles/pending";
		}

		// Where the tile helper is asked to take a tile away: a file named by its title ID
		std::string Removals()
		{
			return std::string(ps5paths::kDefaultRoot) + "/tiles/remove";
		}

		// Whether the app may make folders in folder: without a HEN its sandbox may read /data/homebrew,
		// which is root's, but not make folders there
		bool CanMakeFolders(const char* folder)
		{
			const std::string probe = fmt::format("{}/.ps5cemu-probe-{}", folder, getpid());
			if (mkdir(probe.c_str(), 0777) != 0)
				return false;
			rmdir(probe.c_str());
			return true;
		}

		// A payload sent to elfldr, which runs it as root once the connection is closed
		bool SendToElfldr(const std::string& path, std::string& error)
		{
			std::vector<char> elf;
			if (FILE* file = std::fopen(path.c_str(), "rb"))
			{
				char buffer[64 * 1024];
				for (size_t read; (read = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
					elf.insert(elf.end(), buffer, buffer + read);
				std::fclose(file);
			}
			if (elf.empty())
			{
				error = fmt::format("{} is missing", path);
				return false;
			}
			const int fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd < 0)
			{
				error = fmt::format("no socket (errno {})", errno);
				return false;
			}
			timeval timeout{5, 0};
			setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_port = htons(kElfldrPort);
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
			{
				error = fmt::format("nothing answers on 127.0.0.1:{} (errno {})", kElfldrPort, errno);
				close(fd);
				return false;
			}
			size_t sent = 0;
			while (sent < elf.size())
			{
				const ssize_t n = write(fd, elf.data() + sent, elf.size() - sent);
				if (n <= 0)
					break;
				sent += (size_t)n;
			}
			shutdown(fd, SHUT_WR); // elfldr runs what it got once the sending side closes
			char sink[256];
			while (read(fd, sink, sizeof(sink)) > 0)
			{
			}
			close(fd);
			if (sent < elf.size())
			{
				error = fmt::format("elfldr took {} of {} bytes", sent, elf.size());
				return false;
			}
			return true;
		}

		// tile-installer.elf, which elfldr runs as root, answering for the tile titleId in folder: one made
		// in Pending() moved into /data/homebrew, or one asked for in Removals() taken away. Empty when it
		// is done, else why not
		std::string RunHelper(const std::string& folder, const std::string& titleId)
		{
			const std::string result = folder + "/" + titleId + ".result";
			std::remove(result.c_str());
			std::string error;
			if (!SendToElfldr(ps5paths::AppDir() + "/tile-installer.elf", error))
			{
				ps5log::Line("[tiles] the tile helper could not be sent to elfldr: {}", error);
				return ps5lang::Tr("elfldr does not answer on port 9021. Home screen tiles need it, to be put in /data/homebrew and to "
								   "start their games: load elfldr, then try again.");
			}
			for (int poll = 0; poll < kInstallPolls && !s_stop; poll++)
			{
				std::ifstream file(result);
				std::string line;
				if (file && std::getline(file, line) && !line.empty())
				{
					file.close();
					std::remove(result.c_str());
					ps5log::Line("[tiles] the tile helper: {}", line);
					if (line == "ok")
						return {};
					// "error <errno> <what>": what, in the helper's words
					size_t what = line.find(' ', 6);
					return ps5lang::TrF("It could not be put in /data/homebrew: {0}", what == std::string::npos ? line : line.substr(what + 1));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			ps5log::Line("[tiles] the tile helper did not answer in {} s", kInstallPolls / 10);
			return ps5lang::Tr("elfldr took the tile helper, but the tile did not reach /data/homebrew.");
		}

		// The tile made for this game before: a forwarder in /data/homebrew that starts PS5-ECAM
		// with the same --rom
		std::string Earlier(const std::string& rom)
		{
			DIR* dir = opendir(kRoot);
			if (!dir)
				return {};
			std::string found;
			while (const dirent* entry = readdir(dir))
			{
				if (!psfwd_valid_title_id(entry->d_name))
					continue;
				// 5 KB a read: off the stack of a thread of the app's, and its own for each caller (the menu
				// asks whether a game has a tile while one is being made)
				auto info = std::make_unique<psfwd_info>();
				if (!psfwd_read((std::string(kRoot) + "/" + entry->d_name).c_str(), info.get()) || std::strcmp(info->target, ps5paths::kTitleId) != 0)
					continue;
				for (int i = 0; i + 1 < info->argc; i++)
					if (std::strcmp(info->argv[i], "--rom") == 0 && rom == info->argv[i + 1])
						found = info->title_id;
				if (!found.empty())
					break;
			}
			closedir(dir);
			return found;
		}

		// A free ID from the format's range that no app on the console has: PS5-ECAM's own is in
		// that range, and an app installed on a drive has no folder in /data/homebrew
		std::string NewTitleId()
		{
			for (int attempt = 0; attempt < 64; attempt++)
			{
				char id[PSFWD_TITLE_ID_SIZE] = {};
				if (!psfwd_new_title_id(kRoot, id))
					return {};
				if (std::strcmp(id, ps5paths::kTitleId) != 0 && !Exists(std::string("/user/app/") + id) &&
					!Exists(std::string("/system_ex/app/") + id))
					return id;
			}
			return {};
		}

		void Append(void* context, void* data, int size)
		{
			auto* out = static_cast<std::vector<uint8_t>*>(context);
			out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
		}

		// The tile's picture: the cover made square as a PNG, else the app's own icon
		std::vector<uint8_t> Icon(const std::string& cover)
		{
			std::vector<uint8_t> png;
			std::vector<uint8_t> rgba;
			if (!cover.empty() && ui::SquarePicture(cover, kIconSize, rgba))
			{
				std::vector<uint8_t> rgb((size_t)kIconSize * kIconSize * 3);
				for (size_t i = 0, o = 0; i < rgba.size(); i += 4, o += 3)
				{
					rgb[o] = rgba[i];
					rgb[o + 1] = rgba[i + 1];
					rgb[o + 2] = rgba[i + 2];
				}
				if (stbi_write_png_to_func(Append, &png, kIconSize, kIconSize, 3, rgb.data(), kIconSize * 3) && !png.empty())
					return png;
				png.clear();
			}
			if (FILE* file = std::fopen((ps5paths::AppDir() + "/sce_sys/icon0.png").c_str(), "rb"))
			{
				uint8_t buffer[64 * 1024];
				for (size_t read; (read = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
					png.insert(png.end(), buffer, buffer + read);
				std::fclose(file);
			}
			return png;
		}

		void Finish(Status::State state, const std::string& titleId, bool replaced, const std::string& message, bool removed = false)
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.titleId = titleId;
			s_status.folder = titleId.empty() ? std::string() : std::string(kRoot) + "/" + titleId;
			s_status.replaced = replaced;
			s_status.removed = removed;
			s_status.message = message;
			s_status.finished++;
		}

		void RunRemove(Request request)
		{
			const std::string titleId = Earlier(request.rom);
			if (titleId.empty())
			{
				Finish(Status::State::Failed, {}, false, ps5lang::Tr("It has no home screen tile."), true);
				return;
			}
			if (CanMakeFolders(kRoot))
			{
				if (!psfwd_remove(kRoot, titleId.c_str()))
				{
					ps5log::Line("[tiles] {}/{} not removed (errno {})", kRoot, titleId, errno);
					Finish(Status::State::Failed, titleId, false, ps5lang::TrF("{0} could not be removed", std::string(kRoot) + "/" + titleId), true);
					return;
				}
			}
			else
			{
				// the tile helper takes it away, as root: asked for by a file named by the title ID
				std::error_code ec;
				std::filesystem::create_directories(Removals(), ec);
				const std::string ask = Removals() + "/" + titleId;
				std::ofstream(ask, std::ios::trunc) << request.rom << "\n";
				const std::string why = RunHelper(Removals(), titleId);
				std::remove(ask.c_str());
				if (!why.empty())
				{
					Finish(Status::State::Failed, titleId, false, why, true);
					return;
				}
			}
			ps5log::Line("[tiles] {}/{} removed: {}", kRoot, titleId, request.name);
			Finish(Status::State::Done, titleId, false, {}, true);
		}

		void Run(Request request)
		{
			const std::vector<uint8_t> icon = Icon(request.cover);
			if (icon.empty())
			{
				Finish(Status::State::Failed, {}, false, ps5lang::Tr("The game has no picture for its tile"));
				return;
			}
			std::string titleId = Earlier(request.rom);
			const bool replaced = !titleId.empty();
			if (titleId.empty())
				titleId = NewTitleId();
			if (titleId.empty())
			{
				Finish(Status::State::Failed, {}, false, ps5lang::Tr("No free title ID for a tile"));
				return;
			}
			const char* argv[] = {"--rom", request.rom.c_str(), "--exit-after-game"};
			psfwd_spec spec{};
			spec.title_id = titleId.c_str();
			spec.name = request.name.c_str();
			spec.target = ps5paths::kTitleId;
			spec.argc = 3;
			spec.argv = argv;
			spec.creator = "Playtendoh " PS5CEMU_VERSION;
			spec.icon0_png = {icon.data(), icon.size()};
			char error[256] = {};
			const std::string templateDir = ps5paths::Assets() + "/forwarder";
			// made in /data/homebrew where the app may make folders there (a HEN's jailbreak); else in its
			// own folder, then moved in by the tile helper, as root
			const bool direct = CanMakeFolders(kRoot);
			const std::string root = direct ? std::string(kRoot) : Pending();
			if (!direct)
			{
				std::error_code ec;
				std::filesystem::create_directories(root, ec);
				std::filesystem::remove_all(root + "/" + titleId, ec);
			}
			if (!psfwd_write(root.c_str(), templateDir.c_str(), &spec, error, sizeof(error)))
			{
				ps5log::Line("[tiles] {}'s tile not made: {}", request.name, error);
				Finish(Status::State::Failed, titleId, replaced, error);
				return;
			}
			if (!direct)
			{
				ps5log::Line("[tiles] {} cannot be written here: the tile made in {}, for the tile helper to move", kRoot, root);
				const std::string why = RunHelper(Pending(), titleId);
				if (!why.empty())
				{
					std::error_code ec;
					std::filesystem::remove_all(root + "/" + titleId, ec);
					Finish(Status::State::Failed, titleId, replaced, why);
					return;
				}
			}
			ps5log::Line("[tiles] {}/{}: {} ({}; --rom {}, a {} KB picture)", kRoot, titleId, request.name, replaced ? "made again" : "new",
				request.rom, icon.size() >> 10);
			Finish(Status::State::Done, titleId, replaced, {});
		}
	}

	namespace
	{
		void Begin(void (*run)(Request), const Request& request)
		{
			{
				std::lock_guard lock(s_mutex);
				if (s_status.state == Status::State::Making)
					return;
				const uint32_t finished = s_status.finished;
				s_status = {};
				s_status.finished = finished;
				s_status.state = Status::State::Making;
			}
			if (s_thread.joinable())
				s_thread.join();
			s_stop = false;
			s_thread = std::thread(run, request);
		}
	}

	void Start(const Request& request)
	{
		Begin(Run, request);
	}

	bool HasTile(const std::string& rom)
	{
		return !Earlier(rom).empty();
	}

	void StartRemove(const Request& request)
	{
		Begin(RunRemove, request);
	}

	Status GetStatus()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	void Stop()
	{
		s_stop = true; // a wait for the tile helper ends
		if (s_thread.joinable())
			s_thread.join();
	}
}
