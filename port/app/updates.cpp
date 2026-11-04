// SPDX-License-Identifier: GPL-3.0-or-later
#include "updates.h"
#include "update_files.h"
#include "boxart.h"
#include "emulator.h"
#include "pack_updates.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/curl_socket.h"
#include "../ps5/net.h"

#include <curl/curl.h>
#include <rapidjson/document.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

// The PS5's network library: getaddrinfo finds nothing on the console, so each host's address comes
// from the PS5's resolver (ps5/net.h) and is given to curl
extern "C" int sceNetInit(void);

namespace fs = std::filesystem;

namespace ps5update
{
	namespace
	{
		// the repository by its name, then by its name before 5.0.0 (GitHub sends a renamed repository's
		// old address on to the new one): the first that answers is asked for the rest
		constexpr const char* kRepositories[] = {"premohq/PS5-ECAM", "premohq/PS5CEMU-HAR"};
		std::string s_repository = kRepositories[0]; // under s_mutex: the one that answered
		// the releases branch: each release's ZIP (PS5-ECAM-<tag>.zip, the PPSA99360 folder) and
		// SHA256SUMS for all of them
		std::string Files(const std::string& repository) { return "https://raw.githubusercontent.com/" + repository + "/releases/"; }
		constexpr const char* kHosts[] = {"api.github.com", "raw.githubusercontent.com"};
		constexpr uint64_t kMaxDownload = 512ull << 20;

		std::mutex s_mutex;
		Status s_status;			  // under s_mutex
		bool s_dismissed = false;	  // under s_mutex
		bool s_restartFailed = false; // under s_mutex: Restart came back, so the installed one can wait
		std::thread s_thread;
		std::atomic<bool> s_stop{false};
		std::atomic<bool> s_cancelled{false}; // Cancel: what the thread finds after is not shown
		// the ZIP's transfer is under way: only its figures are the sheet's (SHA256SUMS' 1357 bytes showed
		// as "0 of 0 MB" before it, #78)
		std::atomic<bool> s_counting{false};
		std::atomic<bool> s_running{false};	  // s_thread has not ended

		void Set(Status::State state, const std::string& message = {})
		{
			std::lock_guard lock(s_mutex);
			if (s_cancelled)
				return; // the sheet is closed, the release offered again
			s_status.state = state;
			if (!message.empty() || state == Status::State::Failed)
				s_status.message = message;
		}

		void Fail(const std::string& message)
		{
			ps5log::Line("[update] {}", message);
			Set(Status::State::Failed, message);
		}

		// "2.0.0d", or "v2.0.0d": numbers, then a letter (none is before "a")
		using Version = std::tuple<int, int, int, int>;
		bool Parse(std::string text, Version& out)
		{
			if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
				text.erase(0, 1);
			int major = 0, minor = 0, patch = 0, used = 0;
			if (std::sscanf(text.c_str(), "%d.%d.%d%n", &major, &minor, &patch, &used) != 3)
				return false;
			const std::string rest = text.substr(used);
			const int letter = rest.empty() ? 0 : std::tolower((unsigned char)rest[0]) - 'a' + 1;
			out = {major, minor, patch, letter};
			return true;
		}

		// "host:443:address" for each host the PS5's resolver finds: asked once a session (ps5/net.h), so the
		// install after the check asks again for nothing, where it could wait on the resolver for good (#73)
		curl_slist* Resolve()
		{
			curl_slist* list = nullptr;
			for (const char* host : kHosts)
			{
				if (s_stop)
					break; // a game is starting: the next host is not asked for
				list = ps5net::AddResolve(list, host, 443);
			}
			return list;
		}

		int Progress(void*, curl_off_t total, curl_off_t received, curl_off_t, curl_off_t)
		{
			if (!s_cancelled && s_counting)
			{
				std::lock_guard lock(s_mutex);
				s_status.total = total > 0 ? (uint64_t)total : s_status.total;
				s_status.received = (uint64_t)received;
			}
			return s_stop ? 1 : 0; // a game is starting: cut short
		}

		CURL* Session(curl_slist* resolve, const char* url)
		{
			CURL* curl = curl_easy_init();
			if (!curl)
				return nullptr;
			ps5curl::Setup(curl);
			static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store of its own
			curl_easy_setopt(curl, CURLOPT_URL, url);
			if (resolve)
				curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "PS5-ECAM/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
			// a transfer that stalls (under 1 KB/s for 30 s) fails, where it hung the update sheet for good (#72)
			curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
			curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			return curl;
		}

		size_t Collect(char* data, size_t size, size_t count, void* out)
		{
			auto* body = static_cast<std::string*>(out);
			if (body->size() > 1024 * 1024)
				return 0; // a release's details and the sums are a few kilobytes
			body->append(data, size * count);
			return size * count;
		}

		// A small text from GitHub; false when it did not come
		bool Fetch(curl_slist* resolve, const std::string& url, std::string& body, long timeout, const char* accept = nullptr)
		{
			CURL* curl = Session(resolve, url.c_str());
			if (!curl)
				return false;
			curl_slist* headers = ps5curl::Headers(accept ? curl_slist_append(nullptr, accept) : nullptr);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
			const CURLcode result = curl_easy_perform(curl);
			long http = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
			curl_easy_cleanup(curl);
			curl_slist_free_all(headers);
			if (result != CURLE_OK || http != 200)
				ps5log::Line("[update] {}: {} (HTTP {})", url, s_stop ? "stopped for a game" : curl_easy_strerror(result), http);
			return result == CURLE_OK && http == 200;
		}

		struct Download
		{
			std::ofstream file;
			uint64_t written = 0;
		};

		size_t Write(char* data, size_t size, size_t count, void* user)
		{
			auto* download = static_cast<Download*>(user);
			const size_t bytes = size * count;
			if (download->written + bytes > kMaxDownload)
				return 0;
			download->file.write(data, (std::streamsize)bytes);
			download->written += bytes;
			return download->file ? bytes : 0;
		}

		// SHA-256 (FIPS 180-4), to check the download against the release's SHA256SUMS
		class Sha256
		{
		public:
			void Add(const uint8_t* data, size_t size)
			{
				m_length += size;
				while (size > 0)
				{
					const size_t take = std::min(size, 64 - m_used);
					std::memcpy(m_block.data() + m_used, data, take);
					m_used += take, data += take, size -= take;
					if (m_used == 64)
					{
						Compress(m_block.data());
						m_used = 0;
					}
				}
			}

			std::string Hex()
			{
				const uint64_t bits = m_length * 8;
				const uint8_t one = 0x80, zero = 0;
				Add(&one, 1);
				while (m_used != 56)
					Add(&zero, 1);
				for (int i = 7; i >= 0; i--)
				{
					const uint8_t byte = (uint8_t)(bits >> (i * 8));
					Add(&byte, 1);
				}
				std::string hex;
				for (const uint32_t word : m_state)
					hex += fmt::format("{:08x}", word);
				return hex;
			}

		private:
			static uint32_t Rotate(uint32_t value, int by) { return (value >> by) | (value << (32 - by)); }

			void Compress(const uint8_t* block)
			{
				static constexpr uint32_t kRounds[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
					0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1,
					0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
					0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354,
					0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585,
					0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee,
					0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
				uint32_t w[64];
				for (int i = 0; i < 16; i++)
					w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
				for (int i = 16; i < 64; i++)
				{
					const uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
					const uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
					w[i] = w[i - 16] + s0 + w[i - 7] + s1;
				}
				uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3], e = m_state[4], f = m_state[5], g = m_state[6],
						 h = m_state[7];
				for (int i = 0; i < 64; i++)
				{
					const uint32_t t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + kRounds[i] + w[i];
					const uint32_t t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
					h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
				}
				m_state[0] += a, m_state[1] += b, m_state[2] += c, m_state[3] += d;
				m_state[4] += e, m_state[5] += f, m_state[6] += g, m_state[7] += h;
			}

			std::array<uint32_t, 8> m_state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
			std::array<uint8_t, 64> m_block{};
			size_t m_used = 0;
			uint64_t m_length = 0;
		};

		std::string FileSha256(const fs::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			Sha256 sha;
			std::vector<char> buffer(1 << 20);
			while (file)
			{
				file.read(buffer.data(), (std::streamsize)buffer.size());
				sha.Add(reinterpret_cast<const uint8_t*>(buffer.data()), (size_t)file.gcount());
			}
			return sha.Hex();
		}

		// The release an update replaced: its files as they were, and version.txt (PreviousVersion)
		fs::path PreviousFolder()
		{
			return fs::path(ps5paths::Root()) / "previous-version";
		}

		// The release an update installed and where, for the next start to see whether it is that one
		// (TookInstalled): a restart that starts the old one again would otherwise offer it again, and
		// again (#72)
		fs::path InstalledRecord()
		{
			return fs::path(ps5paths::Root()) / "update-installed.txt";
		}

		// At a fresh start: false when an update installed a release newer than this one, which then
		// says so (the sheet shows why, and the release is not offered again this session)
		bool TookInstalled()
		{
			std::string version, folder;
			{
				std::ifstream in(InstalledRecord());
				if (!in)
					return true;
				std::getline(in, version);
				std::getline(in, folder);
			}
			std::error_code ec;
			fs::remove(InstalledRecord(), ec);
			Version installed, mine;
			if (!Parse(version, installed) || !Parse(PS5CEMU_VERSION, mine) || installed <= mine)
			{
				ps5log::Line("[update] {} is the release the last update installed", Readable(PS5CEMU_VERSION));
				return true;
			}
			ps5log::Line("[update] the last update installed {} in {}, but this is {} from {}: the PS5 started the old one", Readable(version),
				folder, Readable(PS5CEMU_VERSION), ps5paths::AppDir());
			std::lock_guard lock(s_mutex);
			s_status.state = Status::State::Failed;
			s_status.latest = version;
			s_status.installing = true; // the sheet shows it
			// tr: at a start after an update: {0} the release installed, {1} the folder, {2} this release
			s_status.message = ps5lang::TrF("{0} was installed in {1}, but the PS5 started {2} again. Close Playtendoh and start it from the "
											"home screen; if it is still {2}, copy the release's PPSA99360 folder over by hand.",
				Readable(version), folder, Readable(PS5CEMU_VERSION));
			return false;
		}

		// A file kept as it is, in the same shape under to: a second name for it where the drive has
		// them (no space taken), else a copy
		bool Keep(const fs::path& file, const fs::path& to)
		{
			std::error_code ec;
			fs::create_directories(to.parent_path(), ec);
			fs::remove(to, ec);
			fs::create_hard_link(file, to, ec);
			if (ec)
			{
				ec.clear();
				fs::copy_file(file, to, fs::copy_options::overwrite_existing, ec);
			}
			return !ec;
		}

		// Files and folders earlier releases put in the app that this one does not have: the community
		// graphic packs (pack_updates.h downloads them, #74), the opening picture of the launcher's Eden
		// layout, the 3DS borders as TGA (PNG now), ProsperoEden's launcher's files in eden/ui (the Switch side
		// never opens that launcher, tools/build-eden.sh)
		constexpr const char* kRetired[] = {"assets/graphicPacks", "assets/ui/opening.jpg", "assets/borders/aurora.tga",
			"assets/borders/midnight.tga", "assets/borders/ps5cemu-har.tga", "assets/borders/shell.tga", "assets/borders/waves.tga",
			"eden/ui"};

		// path (in app) moved under keep, as it was, or deleted when keep is empty
		void Retire(const fs::path& app, const fs::path& path, const fs::path& keep)
		{
			std::error_code ec;
			if (!fs::exists(app / path, ec))
				return;
			if (!keep.empty())
			{
				std::vector<fs::path> files;
				if (fs::is_directory(app / path, ec))
				{
					for (auto it = fs::recursive_directory_iterator(app / path, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
						if (it->is_regular_file(ec))
							files.push_back(it->path().lexically_relative(app));
				}
				else
					files.push_back(path);
				for (const fs::path& file : files)
					Keep(app / file, keep / file);
			}
			const auto removed = fs::remove_all(app / path, ec);
			ps5log::Line("[update] {} is no longer the app's: {} file(s) {}", path.string(), (int64_t)removed, keep.empty() ? "deleted" : "kept for going back");
		}

		// The folder the app runs from, which the update replaces the files of: the one ShadowMountPlus
		// mounted it from (on a USB drive, say, with an older copy left in /data/homebrew), else of the
		// usual install folder and the drives' the one holding the copy running, else where the app was
		// found. Empty for an app mounted from an image, and for one whose copy is in none of those while
		// /data/homebrew holds another: that older copy is not updated in its place.
		fs::path AppFolder()
		{
			bool image = false;
			const std::string source = ps5paths::MountSource(&image);
			if (!source.empty())
				return source;
			if (image)
				return {};
			std::error_code ec;
			for (const std::string& folder : ps5paths::InstallFolders())
				if (fs::exists(folder + "/eboot.bin", ec) && ps5paths::RunsFrom(folder))
					return folder;
			return fs::exists(ps5paths::kMountedEboot, ec) ? fs::path() : fs::path(ps5paths::AppDir());
		}

		double Seconds(std::chrono::steady_clock::time_point since)
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
		}

		void RunCheck(curl_slist* resolve)
		{
			Set(Status::State::Checking);
			std::string body;
			rapidjson::Document json;
			bool answered = false;
			for (const char* repository : kRepositories)
			{
				body.clear();
				if (Fetch(resolve, std::string("https://api.github.com/repos/") + repository + "/releases/latest", body, 10,
						"Accept: application/vnd.github+json") &&
					!json.Parse(body.c_str()).HasParseError() && json.IsObject() && json.HasMember("tag_name") && json["tag_name"].IsString())
				{
					std::lock_guard lock(s_mutex);
					s_repository = repository;
					answered = true;
					break;
				}
			}
			if (!answered)
			{
				// tr: why an update did not finish
				Fail(ps5lang::Tr("GitHub did not answer with a release"));
				return;
			}
			const std::string latest = json["tag_name"].GetString();
			Version mine, theirs;
			if (!Parse(PS5CEMU_VERSION, mine) || !Parse(latest, theirs))
			{
				Fail(ps5lang::TrF("The latest release's version ({0}) is not one this can read", latest));
				return;
			}
			{
				std::lock_guard lock(s_mutex);
				s_status.latest = latest;
			}
			if (theirs <= mine)
			{
				ps5log::Line("[update] {} is the latest release", Readable(PS5CEMU_VERSION));
				Set(Status::State::UpToDate);
				return;
			}
			ps5log::Line("[update] {} is out (this is {})", Readable(latest), Readable(PS5CEMU_VERSION));
			Set(Status::State::Available);
		}

		void RunInstall(curl_slist* resolve)
		{
			const std::string latest = GetStatus().latest;
			std::string repository;
			{
				std::lock_guard lock(s_mutex);
				repository = s_repository;
			}
			const std::string releases = Files(repository);
			// the release's ZIP by the app's name, else by its name before 5.0.0
			std::string zipName = fmt::format("PS5-ECAM-{}.zip", latest);
			const std::string oldName = fmt::format("PS5CEMU-HAR-{}.zip", latest);
			// the release's checksum, from the sums beside it
			std::string sums, expected;
			if (!Fetch(resolve, releases + "SHA256SUMS", sums, 20))
			{
				Fail(ps5lang::Tr("The release's checksums could not be downloaded"));
				return;
			}
			for (size_t start = 0; start < sums.size();)
			{
				size_t end = sums.find('\n', start);
				if (end == std::string::npos)
					end = sums.size();
				const std::string line = sums.substr(start, end - start);
				if (line.size() > 66 && line.find(zipName) != std::string::npos)
					expected = line.substr(0, 64);
				start = end + 1;
			}
			if (expected.size() != 64)
				for (size_t start = 0; start < sums.size();)
				{
					size_t end = sums.find('\n', start);
					if (end == std::string::npos)
						end = sums.size();
					const std::string line = sums.substr(start, end - start);
					if (line.size() > 66 && line.find(oldName) != std::string::npos)
					{
						expected = line.substr(0, 64);
						zipName = oldName;
					}
					start = end + 1;
				}
			if (expected.size() != 64)
			{
				Fail(ps5lang::TrF("The releases branch has no checksum for {0}", zipName));
				return;
			}

			ps5log::Line("[update] downloading {}", zipName);
			Set(Status::State::Downloading);
			const fs::path archive = fs::path(ps5paths::Cache()) / "app-update.zip";
			std::error_code ec;
			fs::create_directories(archive.parent_path(), ec);
			Download download;
			download.file.open(archive, std::ios::binary | std::ios::trunc);
			CURL* curl = Session(resolve, (releases + zipName).c_str());
			if (!curl)
			{
				Fail(ps5lang::Tr("The download could not start"));
				return;
			}
			curl_slist* headers = ps5curl::Headers();
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
			s_counting = true;
			const CURLcode result = curl_easy_perform(curl);
			s_counting = false;
			long http = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
			curl_easy_cleanup(curl);
			curl_slist_free_all(headers);
			download.file.close();
			if (result != CURLE_OK || http != 200 || download.written == 0)
			{
				fs::remove(archive, ec);
				ps5log::Line("[update] the download of {} failed ({}, HTTP {})", zipName, s_stop ? "stopped for a game" : curl_easy_strerror(result), http);
				Set(Status::State::Failed, s_stop ? ps5lang::Tr("Stopped for a game") : ps5lang::Tr("The download did not finish: is the PS5 online?"));
				return;
			}

			Set(Status::State::Verifying);
			const std::string actual = FileSha256(archive);
			if (actual != expected)
			{
				fs::remove(archive, ec);
				Fail(ps5lang::TrF("The download is damaged (SHA-256 {0}, not {1})", actual.substr(0, 12), expected.substr(0, 12)));
				return;
			}

			// unpacked beside the app's folder (the same drive, so each file then moves into place)
			Set(Status::State::Installing);
			const fs::path app = AppFolder();
			if (app.empty())
			{
				fs::remove(archive, ec);
				Fail(ps5lang::Tr("Playtendoh runs from an image, whose files can't be replaced: install the new release by hand"));
				return;
			}
			ps5log::Line("[update] installing in {}", app.string());
			const fs::path staging = app.string() + ".update";
			RemoveAll(staging);
			std::vector<fs::path> files;
			std::string error;
			const auto unpacking = std::chrono::steady_clock::now();
			if (!Unpack(archive, staging, files, error, s_stop))
			{
				RemoveAll(staging);
				fs::remove(archive, ec);
				Fail(ps5lang::TrF("It could not be unpacked: {0}", error));
				return;
			}
			ps5log::Line("[update] {} files unpacked in {:.1f} s", files.size(), Seconds(unpacking));
			const auto moving = std::chrono::steady_clock::now();
			// the program last, so that until then the app is the old one whole
			std::stable_partition(files.begin(), files.end(), [](const fs::path& file) { return file != "eboot.bin"; });
			// what each file replaces kept as it is, for Settings > Online's Go back (PreviousVersion), until
			// the next update: a file this release does not have stays in the app as it is anyway
			const fs::path previous = PreviousFolder();
			RemoveAll(previous);
			bool kept = InParallel(files.size(), [&](int, size_t i) {
				std::error_code missing;
				return !fs::exists(app / files[i], missing) || Keep(app / files[i], previous / files[i]);
			});
			if (kept)
				kept = static_cast<bool>(std::ofstream(previous / "version.txt") << PS5CEMU_VERSION << '\n');
			if (!kept)
			{
				ps5log::Line("[update] the release being replaced could not be kept: no going back to it");
				RemoveAll(previous);
			}
			MakeFolders(app, files);
			std::mutex failure;
			std::string failed; // under failure: the first file that could not be moved, and why
			auto move = [&](int, size_t i) {
				std::error_code why;
				fs::rename(staging / files[i], app / files[i], why);
				if (!why)
					return true;
				std::lock_guard lock(failure);
				if (failed.empty())
					failed = ps5lang::TrF("{0} could not be replaced ({1}): copy the release's PPSA99360 folder over by hand", files[i].string(), why.message());
				return false;
			};
			// all but the program at once, then the program (files' last) on its own
			if (!InParallel(files.size() - 1, move) || !move(0, files.size() - 1))
			{
				Fail(failed);
				return;
			}
			ps5log::Line("[update] the files kept and moved into place in {:.1f} s", Seconds(moving));
			// what earlier releases carried and this one does not: into the kept release with the rest, for Go
			// back, rather than left in the app for good
			for (const char* retired : kRetired)
				Retire(app, retired, kept ? previous : fs::path());
			fs::remove_all(staging, ec);
			fs::remove(archive, ec);
			std::ofstream(InstalledRecord(), std::ios::trunc) << latest << '\n' << app.string() << '\n';
			ps5log::Line("[update] {} installed in {} ({} files, {} MB)", Readable(latest), app.string(), files.size(), download.written >> 20);
			Set(Status::State::Ready);
		}

		void Run(bool install)
		{
			struct Running
			{
				~Running() { s_running = false; }
			} running;
			sceNetInit();
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolve = Resolve();
			if (!resolve)
			{
				ps5log::Line("[update] GitHub could not be found: no update check");
				Set(Status::State::Failed, ps5lang::Tr("GitHub could not be found: is the PS5 online?"));
				return;
			}
			if (install)
				RunInstall(resolve);
			else
				RunCheck(resolve);
			curl_slist_free_all(resolve);
		}

		bool Busy(Status::State state)
		{
			return state == Status::State::Checking || state == Status::State::Downloading || state == Status::State::Verifying ||
				state == Status::State::Installing;
		}

		void Launch(bool install)
		{
			if (s_thread.joinable())
				s_thread.join();
			s_stop = false;
			s_cancelled = false;
			s_running = true;
			s_thread = std::thread(Run, install);
		}
	}

	void Start()
	{
		{
			std::lock_guard lock(s_mutex);
			if (s_status.state != Status::State::Idle)
				return;
		}
		if (!TookInstalled())
			return;
		{
			std::lock_guard lock(s_mutex);
			s_status.state = Status::State::Checking;
		}
		Launch(false);
	}

	void Check()
	{
		{
			std::lock_guard lock(s_mutex);
			// (a cancelled download's thread still ending: asked again once it has)
			if (Busy(s_status.state) || s_status.state == Status::State::Ready || s_running)
				return;
			s_status = {};
			s_status.state = Status::State::Checking;
			s_dismissed = false;
		}
		Launch(false);
	}

	void Install()
	{
		{
			std::lock_guard lock(s_mutex);
			if (s_status.state != Status::State::Available || s_running)
				return;
			s_status.state = Status::State::Downloading;
			s_status.installing = true;
			s_status.received = s_status.total = 0;
		}
		Launch(true);
	}

	Status GetStatus()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	std::string PreviousVersion()
	{
		std::ifstream file(PreviousFolder() / "version.txt");
		std::string version;
		std::getline(file, version);
		return version;
	}

	bool GoBack(std::string& error)
	{
		const fs::path previous = PreviousFolder(), app = AppFolder();
		const std::string version = PreviousVersion();
		if (version.empty() || app.empty())
		{
			error = ps5lang::Tr("No earlier version is kept here.");
			return false;
		}
		Stop(); // no update installing meanwhile
		std::vector<fs::path> files;
		std::error_code ec;
		for (auto it = fs::recursive_directory_iterator(previous, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
			if (it->is_regular_file(ec) && it->path().filename() != "version.txt")
				files.push_back(it->path().lexically_relative(previous));
		// the program last, as an update puts it, so a failure part-way leaves this one's to start
		std::stable_partition(files.begin(), files.end(), [](const fs::path& file) { return file != "eboot.bin"; });
		for (const fs::path& file : files)
		{
			fs::create_directories((app / file).parent_path(), ec); // a folder that release had and this one has not
			fs::rename(previous / file, app / file, ec);
			if (ec)
			{
				ec.clear();
				fs::copy_file(previous / file, app / file, fs::copy_options::overwrite_existing, ec);
			}
			if (ec)
			{
				ps5log::Line("[update] going back to {}: {} could not be put back ({})", version, file.string(), ec.message());
				error = ps5lang::TrF("{0} could not be put back ({1}): copy that release's PPSA99360 folder over by hand", file.string(), ec.message());
				return false;
			}
		}
		fs::remove_all(previous, ec);
		fs::remove(InstalledRecord(), ec); // the release gone back from is not looked for at the next start
		ps5log::Line("[update] back to {} ({} files): starting it", version, files.size());
		return true;
	}

	bool Cancel()
	{
		std::lock_guard lock(s_mutex);
		// not once its files are being put in place (Installing), nor after
		if (s_status.state != Status::State::Downloading && s_status.state != Status::State::Verifying)
			return false;
		s_cancelled = true;
		s_stop = true; // the download stops at its next progress call, the unpacking at its next file
		ps5log::Line("[update] the download of {} cancelled", Readable(s_status.latest));
		s_status.state = Status::State::Available;
		s_status.installing = false;
		s_status.received = s_status.total = 0;
		s_status.message.clear();
		s_dismissed = true;
		return true;
	}

	bool Prompting()
	{
		std::lock_guard lock(s_mutex);
		if (s_status.installing)
			return true;
		return s_status.state == Status::State::Available && !s_dismissed;
	}

	void Dismiss()
	{
		std::lock_guard lock(s_mutex);
		if (Busy(s_status.state) || (s_status.state == Status::State::Ready && !s_restartFailed))
			return; // an install under way, or done, is not put off (unless the PS5 would not start it)
		s_dismissed = true;
		s_status.installing = false;
	}

	void Restart()
	{
		// nothing else on the network or writing (LoadExecApp stops the rest)
		ps5packs::Stop();
		// as the library's own restart: /app0 first, then each folder the app can start from, those that
		// once took it to the home screen left out (#48, #68). The single folder this used to ask did that
		// on some setups, and its "Restart now" did nothing again at each press (#72)
		ps5log::Line("[update] starting Playtendoh again for the release installed");
		ps5emu::LoadExecApp("eboot.bin", {}, false);
		std::lock_guard lock(s_mutex);
		s_restartFailed = true;
		// tr: an update is installed, but the app could not start itself again to run it
		s_status.message = ps5lang::Tr("The PS5 did not start Playtendoh again. Close it and start it from the home screen to finish the update.");
	}

	void Stop()
	{
		s_stop = true;
		if (!s_thread.joinable())
			return;
		// A game never waits on the check: a step of it can still be waiting on the network (its 31 s
		// into the app were curl_easy_cleanup's wait on a kept-alive connection, #78, before every request
		// asked for it to be closed), and a game chosen meanwhile sat on a black screen. Waited on
		// briefly, a check still running is left to end by itself: it stops at its next step (s_stop),
		// before any download or file write.
		int waited = 0;
		for (; s_running && waited < 25; waited++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (s_running)
		{
			ps5log::Line("[update] the check is still waiting on the network: left to end by itself");
			s_thread.detach();
		}
		else
			s_thread.join();
	}

	void WaitForCheck(int maxMs)
	{
		s_stop = true;
		int waited = 0;
		for (; s_running && waited < maxMs; waited += 10)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (s_running)
			ps5log::Line("[update] the check still runs after {} ms: the app starts over regardless", waited);
		else if (waited)
			ps5log::Line("[update] waited {} ms for the check to end before the app starts over", waited);
	}
}
