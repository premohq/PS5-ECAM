// SPDX-License-Identifier: GPL-3.0-or-later
#include "mods.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ps5mods
{
	namespace
	{
		std::mutex s_mutex;
		Status s_status; // under s_mutex
		std::thread s_thread;
		std::atomic<bool> s_stop{false};

		// the folders whose files take the game's place (GraphicPack2::LoadReplacedFiles): Cemu looks
		// for them in lower case, which a drive formatted on Windows may not have kept
		constexpr const char* kReplaced[] = {"content", "aoc", "code"};

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		// folder's entry named name in any case ("Content", "RULES.TXT"); empty when it has none
		fs::path Child(const fs::path& folder, const std::string& name)
		{
			std::error_code ec;
			for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
				if (Lower(it->path().filename().string()) == name)
					return it->path();
			return {};
		}

		// "00050000101C9400" or "00050000-101C9400": a game's title ID; else 0
		uint64_t TitleIdOf(const std::string& name)
		{
			std::string hex;
			for (char c : name)
				if (c != '-' && c != ' ')
					hex += c;
			if (hex.size() != 16 || !std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit((unsigned char)c) != 0; }))
				return 0;
			return std::strtoull(hex.c_str(), nullptr, 16);
		}

		struct Mod
		{
			fs::path from;		  // its folder on the drive
			fs::path to;		  // in graphicPacks/mods
			fs::path rules;		  // its rules.txt; empty: one is written
			uint64_t titleId = 0; // for the one written
			std::string game;	  // and the game's name, when known
			uint64_t bytes = 0;
		};

		// The mods in a drive's folder: each folder with a rules.txt, or with content/, aoc/ or code/
		// (looked into no further); the folders around them only hold them
		void Find(const fs::path& source, uint64_t titleId, const std::string& name, std::vector<Mod>& mods, int& skipped)
		{
			std::error_code ec;
			for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
			{
				std::error_code entryEc;
				if (!it->is_directory(entryEc))
					continue;
				const fs::path folder = it->path();
				const std::string folderName = folder.filename().string();
				// Cemu's own community packs, when a whole graphicPacks folder was copied: the app keeps
				// those up to date itself (Settings > Online)
				if (Lower(folderName) == "downloadedgraphicpacks" || folderName.empty() || folderName[0] == '.')
				{
					it.disable_recursion_pending();
					continue;
				}
				const fs::path rules = Child(folder, "rules.txt");
				bool replaces = false;
				for (const char* replaced : kReplaced)
					replaces = replaces || !Child(folder, replaced).empty();
				if (rules.empty() && !replaces)
					continue;
				it.disable_recursion_pending();
				Mod mod;
				mod.from = folder;
				mod.to = fs::path(Folder()) / folder.lexically_relative(source);
				mod.rules = rules;
				if (rules.empty())
				{
					// the game: the mod's folder's name, or the folder it is in, else the game it was added from
					mod.titleId = TitleIdOf(folderName);
					if (!mod.titleId && folder.parent_path() != source)
						mod.titleId = TitleIdOf(folder.parent_path().filename().string());
					if (!mod.titleId)
						mod.titleId = titleId;
					if (!mod.titleId)
					{
						ps5log::Line("[mods] {} left out: no rules.txt, and no game (put it in a folder named by the title ID)", folder.string());
						skipped++;
						continue;
					}
					if (mod.titleId == titleId)
						mod.game = name;
				}
				for (fs::recursive_directory_iterator file(folder, entryEc), last; !entryEc && file != last; file.increment(entryEc))
				{
					std::error_code sizeEc;
					if (file->is_regular_file(sizeEc))
						mod.bytes += file->file_size(sizeEc);
				}
				mods.push_back(std::move(mod));
			}
			if (ec)
				ps5log::Line("[mods] {} could not be read through: {}", source.string(), ec.message());
		}

		void Set(Status::State state, const std::string& message = {})
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.message = message;
		}

		// One file, in large pieces: each write to /data costs the console a while. A piece is one read
		// and one write of the system's: the console's C library reads and writes an unbuffered FILE a
		// character (or BUFSIZ) at a time, whatever fread and fwrite are asked for (#96)
		bool CopyFile(const fs::path& from, const fs::path& to, std::vector<char>& buffer)
		{
			const int in = open(from.c_str(), O_RDONLY);
			if (in < 0)
				return false;
			const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
			if (out < 0)
			{
				close(in);
				return false;
			}
			bool ok = true;
			while (ok && !s_stop)
			{
				const ssize_t got = read(in, buffer.data(), buffer.size());
				if (got < 0 && errno == EINTR)
					continue;
				ok = got >= 0;
				if (got <= 0)
					break;
				for (ssize_t written = 0; ok && written < got;)
				{
					const ssize_t put = write(out, buffer.data() + written, (size_t)(got - written));
					if (put < 0 && errno == EINTR)
						continue;
					ok = put > 0;
					written += ok ? put : 0;
				}
				std::lock_guard lock(s_mutex);
				s_status.copied += (uint64_t)got;
			}
			close(in);
			return close(out) == 0 && ok && !s_stop;
		}

		// The rules.txt of a mod that came without one: its files take the game's place, nothing more
		std::string Rules(const Mod& mod)
		{
			std::string name = mod.from.filename().string();
			std::replace(name.begin(), name.end(), '"', '\'');
			std::string game = mod.game.empty() ? fmt::format("{:016X}", mod.titleId) : mod.game;
			std::replace(game.begin(), game.end(), '"', '\'');
			std::replace(game.begin(), game.end(), '/', '-');
			return fmt::format("[Definition]\n"
							   "titleIds = {:016X}\n"
							   "name = {}\n"
							   "path = \"{}/Mods/{}\"\n"
							   "description = Added from a USB drive by PS5-ECAM: its files take the game's place.\n"
							   "version = 6\n",
				mod.titleId, name, game, name);
		}

		// A mod copied beside its place (rules.txt last, so a copy cut short is never a pack Cemu
		// loads), then put in place of the copy there before
		bool Copy(const Mod& mod, std::vector<char>& buffer, std::string& error)
		{
			const fs::path fresh = mod.to.string() + ".new";
			std::error_code ec;
			fs::remove_all(fresh, ec);
			if (!fs::create_directories(fresh, ec) && ec)
			{
				error = ps5lang::TrF("{0} could not be made", fresh.string());
				return false;
			}
			for (fs::recursive_directory_iterator it(mod.from, ec), end; !ec && it != end; it.increment(ec))
			{
				if (s_stop)
					break;
				const fs::path relative = it->path().lexically_relative(mod.from);
				if (relative.empty() || *relative.begin() == "..")
					continue;
				// content/, aoc/ and code/ as Cemu spells them
				fs::path out = fresh;
				bool top = true;
				for (const fs::path& part : relative)
				{
					std::string piece = part.string();
					if (top)
						for (const char* replaced : kReplaced)
							if (Lower(piece) == replaced)
								piece = replaced;
					out /= piece;
					top = false;
				}
				std::error_code entryEc;
				if (it->is_directory(entryEc))
				{
					fs::create_directories(out, entryEc);
					continue;
				}
				if (!it->is_regular_file(entryEc) || (relative == relative.filename() && Lower(relative.string()) == "rules.txt"))
					continue;
				if (!CopyFile(it->path(), out, buffer) && !s_stop)
				{
					fs::remove_all(fresh, ec);
					error = ps5lang::TrF("{0} could not be copied: is the PS5's storage full?", relative.string());
					return false;
				}
			}
			if (s_stop)
			{
				fs::remove_all(fresh, ec);
				error = ps5lang::Tr("Stopped for a game");
				return false;
			}
			if (ec)
			{
				fs::remove_all(fresh, ec);
				error = ps5lang::TrF("{0} could not be read", mod.from.string());
				return false;
			}
			bool rulesOk = false;
			if (!mod.rules.empty())
				rulesOk = CopyFile(mod.rules, fresh / "rules.txt", buffer);
			else
			{
				std::ofstream file(fresh / "rules.txt", std::ios::binary | std::ios::trunc);
				file << Rules(mod);
				rulesOk = (bool)file;
			}
			if (!rulesOk)
			{
				fs::remove_all(fresh, ec);
				error = ps5lang::TrF("{0} could not be written", "rules.txt");
				return false;
			}
			fs::remove_all(mod.to, ec);
			fs::rename(fresh, mod.to, ec);
			if (ec)
			{
				fs::remove_all(fresh, ec);
				error = ps5lang::TrF("{0} could not be written", mod.to.string());
				return false;
			}
			return true;
		}

		void Run(std::vector<std::string> folders, uint64_t titleId, std::string name)
		{
			std::vector<Mod> mods;
			int skipped = 0;
			for (const std::string& folder : folders)
				Find(folder, titleId, name, mods, skipped);
			uint64_t total = 0;
			for (const Mod& mod : mods)
				total += mod.bytes;
			{
				std::lock_guard lock(s_mutex);
				s_status.total = total;
				s_status.skipped = skipped;
			}
			std::string from;
			for (const std::string& folder : folders)
				from += (from.empty() ? "" : ", ") + folder;
			ps5log::Line("[mods] {} mod(s) to copy from {} ({} MB){}", mods.size(), from, total >> 20,
				skipped ? fmt::format(", {} left out", skipped) : std::string());
			std::vector<char> buffer(4 << 20);
			int copied = 0;
			std::string error;
			for (const Mod& mod : mods)
			{
				{
					std::lock_guard lock(s_mutex);
					s_status.current = mod.from.filename().string();
				}
				if (!Copy(mod, buffer, error))
				{
					ps5log::Line("[mods] {} not copied: {}", mod.from.string(), error);
					break;
				}
				copied++;
				ps5log::Line("[mods] {} copied to {}{}", mod.from.string(), mod.to.string(),
					mod.rules.empty() ? fmt::format(" (a rules.txt written for {:016X})", mod.titleId) : std::string());
			}
			std::lock_guard lock(s_mutex);
			s_status.mods = copied;
			s_status.state = error.empty() ? Status::State::Done : Status::State::Failed;
			s_status.message = error;
			s_status.finished++;
		}
	}

	std::string Folder()
	{
		return ps5paths::CemuRoot() + "/graphicPacks/mods";
	}

	std::vector<std::string> Folders(const std::vector<std::string>& drives)
	{
		std::vector<std::string> folders;
		for (const std::string& drive : drives)
			for (const char* folder : {"ps5cemu/mods", "PS5CEMU/mods", "ps5cemu/Mods", "PS5CEMU/Mods", "mods", "Mods"})
			{
				std::error_code ec;
				const fs::path path = fs::path(drive) / folder;
				if (fs::is_directory(path, ec))
				{
					folders.push_back(path.string());
					break; // one a drive: a drive that ignores case finds the same folder by each name
				}
			}
		return folders;
	}

	void Start(const std::vector<std::string>& drives, uint64_t titleId, const std::string& name)
	{
		{
			std::lock_guard lock(s_mutex);
			if (s_status.state == Status::State::Copying)
				return;
			const uint32_t finished = s_status.finished;
			s_status = {};
			s_status.finished = finished;
			s_status.state = Status::State::Copying;
		}
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
		s_thread = std::thread(Run, Folders(drives), titleId, name);
	}

	Status GetStatus()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	void Stop()
	{
		s_stop = true;
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
	}
}
