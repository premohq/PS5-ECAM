// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: installing games, updates and DLC into the MLC for the launcher (emulator.h).
//
// As Cemu's "Install game title, update or DLC" (src/gui/wxgui/GameUpdateWindow.cpp, ParseUpdate
// and ThreadWork): the title in a folder with code, content and meta is copied beside its place in
// the MLC (usr/title/0005000e/xxxxxxxx.partial for an update, 0005000c/... for DLC), and only a
// complete copy takes the place of what was there. Cemu lists only folders named as title IDs, so a
// copy cut short (a cancel, a failure, the app closed with the PS button) is never a title it
// starts. This file keeps Cemu's MPL-2.0 licence.

#include "emulator.h"
#include "lang.h"
#include "wiiu_archives.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"

#include "Cafe/TitleList/TitleId.h"
#include "Cafe/TitleList/ParsedMetaXml.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <unistd.h>

namespace ps5emu
{
	namespace
	{
		constexpr const char* kParts[] = {"content", "code", "meta"};

		std::mutex s_mutex;
		InstallStatus s_status; // under s_mutex
		std::atomic<bool> s_cancel{false};

		void SetStatus(InstallStatus::State state, std::string message = {})
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.message = std::move(message);
		}

		void AddCopied(uint64_t bytes)
		{
			std::lock_guard lock(s_mutex);
			s_status.copied += bytes;
		}

		// Where folders go to be deleted: inside the MLC, so a move there is a rename on the same drive
		fs::path TrashFolder()
		{
			return ActiveSettings::GetMlcPath("install-trash");
		}

		// Deletes what is in the trash, on a thread of its own (one at a time; what arrives meanwhile goes
		// with the next)
		void EmptyTrash()
		{
			static std::atomic<bool> s_emptying{false};
			if (s_emptying.exchange(true))
				return;
			std::thread([] {
				std::error_code ec;
				const uint64_t started = sceKernelGetProcessTime();
				const auto removed = fs::remove_all(TrashFolder(), ec);
				if (removed != 0 && removed != (std::uintmax_t)-1)
					ps5log::Line("[install] {} old files deleted in {} ms", removed, (sceKernelGetProcessTime() - started) / 1000);
				s_emptying = false;
			}).detach();
		}

		// A folder out of the way at once, deleted after: an update's or a DLC's thousands of files take
		// minutes to delete on /data, which an install spent at 0% (an earlier install's copy kept aside)
		// and a cancel spent "cancelling" (its partial copy), neither of which could be stopped (Discord)
		void DropLater(const fs::path& folder)
		{
			std::error_code ec;
			if (!fs::exists(folder, ec))
				return;
			static std::atomic<uint32_t> s_dropped{0};
			fs::create_directories(TrashFolder(), ec);
			fs::rename(folder, TrashFolder() / fmt::format("{}-{}", sceKernelGetProcessTime(), s_dropped++), ec);
			if (ec)
				fs::remove_all(folder, ec); // another drive: as before
			EmptyTrash();
		}

		std::string Gigabytes(uint64_t bytes)
		{
			return fmt::format("{:.1f} GB", bytes / 1e9);
		}

		// A file in pieces, so the progress moves within the large .app files and a cancel is quick. A
		// piece is one read and one write of the system's: the console's C library reads and writes an
		// unbuffered FILE a character (or BUFSIZ) at a time, whatever fread and fwrite are asked for,
		// which held every install at 0% for hours (#96). why: the system's reason, when it fails.
		bool CopyInPieces(const fs::path& from, const fs::path& to, std::vector<char>& buffer, std::string& why)
		{
			const int in = open(_pathToUtf8(from).c_str(), O_RDONLY);
			if (in < 0)
			{
				why = std::strerror(errno);
				return false;
			}
			const int out = open(_pathToUtf8(to).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
			if (out < 0)
			{
				why = std::strerror(errno);
				close(in);
				return false;
			}
			bool ok = true;
			while (ok && !s_cancel)
			{
				const ssize_t got = read(in, buffer.data(), buffer.size());
				if (got < 0 && errno == EINTR)
					continue;
				if (got < 0)
				{
					why = std::strerror(errno);
					ok = false;
				}
				if (got <= 0)
					break;
				for (ssize_t written = 0; ok && written < got;)
				{
					const ssize_t put = write(out, buffer.data() + written, (size_t)(got - written));
					if (put < 0 && errno == EINTR)
						continue;
					if (put <= 0)
					{
						why = std::strerror(put < 0 ? errno : ENOSPC);
						ok = false;
					}
					else
						written += put;
				}
				if (ok)
					AddCopied((uint64_t)got);
			}
			close(in);
			if (close(out) != 0 && ok)
			{
				why = std::strerror(errno);
				ok = false;
			}
			return ok && !s_cancel;
		}

		// How many files are copied at once: an update or DLC is thousands of small files, each of which
		// would otherwise wait for the one before (opening, closing, the drive's latency), and a USB drive
		// and the PS5's storage are kept busy together
		constexpr int kCopiers = 4;
		constexpr size_t kPiece = 8 << 20;

		struct FileToCopy
		{
			fs::path relative;
			uint64_t size;
		};

		void Work(fs::path source, fs::path target, std::string title)
		{
			std::error_code ec;
			// the folder read once: every folder to make, every file and its size
			std::vector<fs::path> folders;
			std::vector<FileToCopy> files;
			uint64_t total = 0;
			// what an earlier install left to delete goes now, beside this one
			EmptyTrash();
			const uint64_t reading = sceKernelGetProcessTime();
			for (const char* part : kParts)
			{
				if (!fs::is_directory(source / part, ec))
					continue;
				folders.push_back(part);
				for (auto it = fs::recursive_directory_iterator(source / part, ec); !ec && it != fs::recursive_directory_iterator() && !s_cancel;
					 it.increment(ec))
				{
					// an entry's errors apart from the walk's: one size not read used to end the walk, and the
					// files found until then were installed as if they were all of it
					std::error_code entryEc;
					const fs::path relative = it->path().lexically_relative(source);
					if (it->is_directory(entryEc))
						folders.push_back(relative);
					else if (it->is_regular_file(entryEc))
					{
						const uint64_t size = it->file_size(entryEc);
						files.push_back({relative, entryEc ? 0 : size});
						total += entryEc ? 0 : size;
					}
				}
				// a folder not read through is not installed in part
				if (ec)
				{
					const std::string failure = fmt::format("{} could not be read through: {}", part, ec.message());
					ps5log::Line("[install] {}: {}", title, failure);
					SetStatus(InstallStatus::State::Failed, failure);
					return;
				}
			}
			// a cancel while the folder was read: nothing has changed yet
			if (s_cancel)
			{
				ps5log::Line("[install] {}: cancelled while its folder was read", title);
				SetStatus(InstallStatus::State::Cancelled);
				return;
			}
			ps5log::Line("[install] {}: {} files and {} folders read in {} ms", title, files.size(), folders.size(),
				(sceKernelGetProcessTime() - reading) / 1000);
			// the large ones first, so the last to finish is a small one and the copiers end together
			std::stable_sort(files.begin(), files.end(), [](const FileToCopy& a, const FileToCopy& b) { return a.size > b.size; });
			{
				std::lock_guard lock(s_mutex);
				s_status.total = total;
			}
			const fs::space_info space = fs::space(ActiveSettings::GetMlcPath(), ec);
			if (!ec && space.available <= total)
			{
				// tr: {0} and {1} are sizes (2.4 GB)
				SetStatus(InstallStatus::State::Failed, ps5lang::TrF("There is not enough space: it needs {0}, and {1} is free.",
					Gigabytes(total), Gigabytes(space.available)));
				return;
			}

			// the copy goes beside the title's place (xxxxxxxx.partial) and takes it only when complete
			fs::path staging = target, backup = target;
			staging.replace_extension(".partial");
			backup.replace_extension(".backup");
			DropLater(staging); // an earlier install's, left by a stop the app did not live through
			// what an earlier version set aside while it copied into the title's place: back where that
			// copy is not there, else dropped, as this one replaces both
			if (!fs::exists(target, ec))
				fs::rename(backup, target, ec);
			DropLater(backup);

			const uint64_t started = sceKernelGetProcessTime();
			std::string failure;
			for (const fs::path& folder : folders)
			{
				if (s_cancel)
					break;
				fs::create_directories(staging / folder, ec);
			}
			ps5log::Line("[install] {}: {} folders made in {} ms, {} MB to copy", title, folders.size(), (sceKernelGetProcessTime() - started) / 1000,
				total / 1000000);
			std::atomic<size_t> next{0};
			std::mutex failureMutex;
			auto copier = [&] {
				std::vector<char> buffer(kPiece);
				std::string why;
				for (size_t i = next++; i < files.size() && !s_cancel; i = next++)
				{
					if (!CopyInPieces(source / files[i].relative, staging / files[i].relative, buffer, why) && !s_cancel)
					{
						std::lock_guard lock(failureMutex);
						if (failure.empty())
							failure = fmt::format("A file could not be copied: {} ({})", _pathToUtf8(files[i].relative), why);
						s_cancel = true; // the others stop too
					}
				}
			};
			std::vector<std::thread> copiers;
			for (int i = 1; i < kCopiers; i++)
				copiers.emplace_back(copier);
			copier();
			for (std::thread& thread : copiers)
				thread.join();
			const bool cancelled = s_cancel && failure.empty();

			if (cancelled || !failure.empty())
			{
				// the partial copy out of the way at once; what was installed never moved
				DropLater(staging);
				ps5log::Line("[install] {}: {}", title, cancelled ? "cancelled" : failure);
				SetStatus(cancelled ? InstallStatus::State::Cancelled : InstallStatus::State::Failed, failure);
				return;
			}
			// the complete copy in the title's place: what was there aside, then dropped
			if (fs::exists(target, ec))
			{
				fs::rename(target, backup, ec);
				if (ec)
				{
					ps5log::Line("[install] {}: {} could not be moved aside: {}", title, _pathToUtf8(target), ec.message());
					SetStatus(InstallStatus::State::Failed, ps5lang::TrF("What is installed there now could not be moved aside: {0}", ec.message()));
					DropLater(staging);
					return;
				}
			}
			fs::rename(staging, target, ec);
			if (ec)
			{
				failure = fmt::format("The copy could not take its place: {}", ec.message());
				fs::rename(backup, target, ec); // what was there, back
				DropLater(staging);
				ps5log::Line("[install] {}: {}", title, failure);
				SetStatus(InstallStatus::State::Failed, failure);
				return;
			}
			DropLater(backup);
			const uint64_t ms = std::max<uint64_t>(1, (sceKernelGetProcessTime() - started) / 1000);
			ps5log::Line("[install] {} installed in {}: {} files, {} MB in {:.1f} s ({} MB/s)", title, _pathToUtf8(target), files.size(), total / 1000000,
				ms / 1000.0, total / 1000 / ms);
			SetStatus(InstallStatus::State::Done);
		}

		// -- what can be installed, on the drives (6.12) ------------------------------------------------

		std::mutex s_findMutex;
		std::vector<Installable> s_found; // under s_findMutex
		std::string s_findWhere;		  // the folder being looked in, under s_findMutex
		std::thread s_finder;
		std::atomic<bool> s_finding{false}, s_stopFinding{false};

		uint64_t ReadBig(const std::vector<uint8_t>& data, size_t at, int bytes)
		{
			uint64_t value = 0;
			for (int i = 0; i < bytes && at + i < data.size(); i++)
				value = value << 8 | data[at + i];
			return value;
		}

		InstallCandidate::Kind KindOf(uint64_t titleId)
		{
			switch ((uint32_t)(titleId >> 32))
			{
			case 0x00050000:
			case 0x00050002: return InstallCandidate::Kind::Game;
			case 0x0005000E: return InstallCandidate::Kind::Update;
			case 0x0005000C: return InstallCandidate::Kind::Dlc;
			default: return InstallCandidate::Kind::System;
			}
		}

		uint64_t FolderBytes(const fs::path& folder)
		{
			std::error_code ec;
			uint64_t bytes = 0;
			for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator() && !s_stopFinding; it.increment(ec))
				if (it->is_regular_file(ec))
					bytes += it->file_size(ec);
			return bytes;
		}

		// A folder that is a title: unpacked (code, content, meta), or packed (title.tmd and .app files,
		// which Cemu reads as they are)
		bool Look(const fs::path& folder, const std::string& drive)
		{
			std::error_code ec;
			Installable found;
			found.folder = _pathToUtf8(folder);
			found.drive = drive;
			if (fs::is_directory(folder / "meta", ec) && fs::is_directory(folder / "content", ec))
			{
				const InstallCandidate candidate = InspectInstall(found.folder);
				if (candidate.kind == InstallCandidate::Kind::None || candidate.kind == InstallCandidate::Kind::System)
					return true; // a title, but nothing to install (already where it goes): not looked into
				found.kind = candidate.kind;
				found.name = candidate.name;
				found.titleId = candidate.titleId;
				found.version = candidate.version;
				found.installedVersion = candidate.installedVersion;
			}
			else if (fs::is_regular_file(folder / "title.tmd", ec))
			{
				std::ifstream file(folder / "title.tmd", std::ios::binary);
				std::vector<uint8_t> tmd(0x1E0);
				file.read((char*)tmd.data(), tmd.size());
				if (file.gcount() < (std::streamsize)tmd.size())
					return true;
				found.titleId = ReadBig(tmd, 0x18C, 8);
				found.version = (uint16_t)ReadBig(tmd, 0x1DC, 2);
				found.kind = KindOf(found.titleId);
				found.packed = true;
				if (found.kind == InstallCandidate::Kind::System)
					return true;
			}
			else
				return false;
			found.baseTitleId = found.titleId & 0x00000000FFFFFFFFull | 0x0005000000000000ull;
			std::lock_guard lock(s_findMutex);
			s_found.push_back(found);
			return true;
		}

		bool Skipped(const std::string& name)
		{
			// the other systems' folders too: the Switch's eden is on the app's drive with the rest now
			static const char* const kSkipped[] = {"mlc01", "covers", "logs", "shaderCache", "cache", "graphicPacks", "azahar", "melonds", "eden",
				"controllerProfiles", "gameProfiles", "themes", "figures", "amiibo", "saves-before", "PS5CEMU-HAR-saves", "PS5-ECAM-logs", "PS5CEMU-HAR-logs",
				"System Volume Information", "$RECYCLE.BIN", "homebrew", "etaHEN"};
			if (name.empty() || name[0] == '.')
				return true;
			for (const char* skipped : kSkipped)
				if (name == skipped)
					return true;
			return false;
		}

		void Find(const fs::path& folder, const std::string& drive, int depth)
		{
			if (s_stopFinding)
				return;
			{
				std::lock_guard lock(s_findMutex);
				s_findWhere = _pathToUtf8(folder);
			}
			if (Look(folder, drive) || depth == 0)
				return;
			std::error_code ec;
			std::vector<fs::path> children;
			for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
				if (it->is_directory(ec) && !Skipped(_pathToUtf8(it->path().filename())))
					children.push_back(it->path());
			std::sort(children.begin(), children.end());
			for (const fs::path& child : children)
				Find(child, drive, depth - 1);
		}
	}

	void StartFindingInstallables(const std::vector<FindRoot>& roots)
	{
		StopFindingInstallables();
		{
			std::lock_guard lock(s_findMutex);
			s_found.clear();
			s_findWhere.clear();
		}
		s_stopFinding = false;
		s_finding = true;
		s_finder = std::thread([roots] {
			std::set<std::string> looked;
			for (const FindRoot& root : roots)
			{
				std::error_code ec;
				const fs::path folder = _utf8ToPath(root.folder);
				if (!fs::is_directory(folder, ec) || !looked.insert(_pathToUtf8(fs::weakly_canonical(folder, ec))).second)
					continue;
				Find(folder, root.drive, root.depth);
			}
			// then how large each is, the list already shown
			std::vector<std::string> folders;
			{
				std::lock_guard lock(s_findMutex);
				for (const Installable& found : s_found)
					folders.push_back(found.folder);
			}
			for (const std::string& folder : folders)
			{
				if (s_stopFinding)
					break;
				const uint64_t bytes = FolderBytes(_utf8ToPath(folder));
				std::lock_guard lock(s_findMutex);
				for (Installable& found : s_found)
					if (found.folder == folder)
						found.bytes = bytes;
			}
			{
				std::lock_guard lock(s_findMutex);
				s_findWhere.clear();
				ps5log::Line("[install] {} updates, DLC and games found to install", s_found.size());
			}
			s_finding = false;
		});
	}

	bool FindingInstallables(std::string* where)
	{
		if (where)
		{
			std::lock_guard lock(s_findMutex);
			*where = s_findWhere;
		}
		return s_finding;
	}

	std::vector<Installable> FoundInstallables()
	{
		std::lock_guard lock(s_findMutex);
		return s_found;
	}

	void RefreshInstalledVersions(std::vector<Installable>& found)
	{
		for (Installable& item : found)
			if (!item.packed)
				item.installedVersion = InspectInstall(item.folder).installedVersion;
	}

	void StopFindingInstallables()
	{
		s_stopFinding = true;
		if (s_finder.joinable())
			s_finder.join();
		s_finding = false;
	}

	InstallCandidate InspectInstall(const std::string& folder)
	{
		InstallCandidate candidate;
		const fs::path path = _utf8ToPath(folder);
		std::error_code ec;
		if (!fs::is_directory(path / "meta", ec) || !fs::is_directory(path / "content", ec))
		{
			candidate.note = fs::exists(path / "title.tmd", ec) ?
				ps5lang::Tr("This one is not unpacked (title.tmd and .app files). Cemu reads it from the game files folder as it is: no install needed.") :
				ps5lang::Tr("There is no title here: a game, update or DLC to install is a folder with code, content and meta.");
			return candidate;
		}
		TitleInfo title(path);
		if (!title.IsValid())
		{
			candidate.note = ps5lang::Tr("Its meta folder does not describe a Wii U title (meta.xml).");
			return candidate;
		}
		// its own name in the menus' language, its English one where it has none
		ParsedMetaXml* meta = title.GetMetaInfo();
		candidate.name = meta ? meta->GetLongName((CafeConsoleLanguage)ps5lang::NintendoLanguage()) : title.GetMetaTitleName();
		candidate.titleId = title.GetAppTitleId();
		candidate.version = title.GetAppTitleVersion();
		const TitleIdParser parser(candidate.titleId);
		switch (parser.GetType())
		{
		case TitleIdParser::TITLE_TYPE::BASE_TITLE:
		case TitleIdParser::TITLE_TYPE::BASE_TITLE_DEMO: candidate.kind = InstallCandidate::Kind::Game; break;
		case TitleIdParser::TITLE_TYPE::BASE_TITLE_UPDATE: candidate.kind = InstallCandidate::Kind::Update; break;
		case TitleIdParser::TITLE_TYPE::AOC: candidate.kind = InstallCandidate::Kind::Dlc; break;
		default: candidate.kind = InstallCandidate::Kind::System; break;
		}
		const fs::path target = ActiveSettings::GetMlcPath(title.GetInstallPath());
		if (fs::equivalent(target, path, ec))
		{
			candidate.kind = InstallCandidate::Kind::None;
			candidate.note = ps5lang::Tr("This is where it is installed already.");
			return candidate;
		}
		if (fs::exists(target, ec))
		{
			const TitleInfo installed(target);
			if (installed.IsValid())
				candidate.installedVersion = installed.GetAppTitleVersion();
		}
		return candidate;
	}

	bool StartInstall(const std::string& folder, std::string& error)
	{
		if (GetInstallStatus().state == InstallStatus::State::Running)
		{
			error = ps5lang::Tr("An install is already running.");
			return false;
		}
		const InstallCandidate candidate = InspectInstall(folder);
		if (candidate.kind == InstallCandidate::Kind::None)
		{
			error = candidate.note;
			return false;
		}
		const fs::path source = _utf8ToPath(folder);
		const fs::path target = ActiveSettings::GetMlcPath(TitleInfo(source).GetInstallPath());
		{
			std::lock_guard lock(s_mutex);
			s_status = {};
			s_status.state = InstallStatus::State::Running;
		}
		s_cancel = false;
		const std::string title = fmt::format("{} ({:016x} v{})", candidate.name, candidate.titleId, candidate.version);
		ps5log::Line("[install] {} from {}", title, folder);
		// on its own: what it does shows in GetInstallStatus, and only one runs at a time
		std::thread(Work, source, target, title).detach();
		return true;
	}

	InstallStatus GetInstallStatus()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	void CancelInstall()
	{
		s_cancel = true;
	}

	namespace
	{
		// Where a title of kind (0005000e an update, 0005000c DLC) is installed in mlc01: as Cemu installs
		// it (TitleInfo::GetInstallPath, small letters), or in capitals as some tools copy it in
		std::vector<fs::path> InstalledFolders(uint64_t titleId, uint32_t kind)
		{
			std::vector<fs::path> folders;
			std::error_code ec;
			const uint32_t low = (uint32_t)titleId;
			for (const std::string& path : {fmt::format("usr/title/{:08x}/{:08x}", kind, low), fmt::format("usr/title/{:08X}/{:08X}", kind, low)})
			{
				const fs::path folder = ActiveSettings::GetMlcPath(path);
				if (fs::is_directory(folder, ec) && std::none_of(folders.begin(), folders.end(), [&](const fs::path& found) { return fs::equivalent(found, folder, ec); }))
					folders.push_back(folder);
			}
			return folders;
		}

		constexpr uint32_t kUpdateKind = 0x0005000E, kDlcKind = 0x0005000C;
	}

	void InstalledAddOns(uint64_t titleId, bool& update, bool& dlc)
	{
		update = !InstalledFolders(titleId, kUpdateKind).empty();
		dlc = !InstalledFolders(titleId, kDlcKind).empty();
	}

	bool RemoveInstalled(uint64_t titleId, bool update, bool dlc, std::string& error)
	{
		if (GetInstallStatus().state == InstallStatus::State::Running)
		{
			error = ps5lang::Tr("An install is already running.");
			return false;
		}
		bool ok = true;
		for (const uint32_t kind : {kUpdateKind, kDlcKind})
		{
			if (kind == kUpdateKind ? !update : !dlc)
				continue;
			for (const fs::path& folder : InstalledFolders(titleId, kind))
			{
				// out of the way at once (a rename within mlc01), its thousands of files deleted after
				DropLater(folder);
				std::error_code ec;
				const bool stayed = fs::exists(folder, ec);
				ps5log::Line("[install] {:016x}: {} {}", titleId, _pathToUtf8(folder), stayed ? "could not be removed" : "removed");
				if (stayed)
				{
					error = _pathToUtf8(folder);
					ok = false;
				}
			}
		}
		return ok;
	}

	void Rescan()
	{
		CafeTitleList::Refresh();
		archives::StartScan(GetConfig().game_paths);
	}
}
