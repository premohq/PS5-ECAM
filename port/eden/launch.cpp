// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden.h"
#include "ingame.h"
#include "library.h"
#include "../app/lang.h"
#include "../app/netfs.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <sys/stat.h>

namespace ps5eden
{
	namespace
	{
		constexpr const char* kProgram = "eden/eboot.bin";
		// Eden's names for the settings' values (its settings_store.h)
		constexpr const char* kResolutions[] = {"0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x"};
		constexpr const char* kFilters[] = {"bilinear", "fsr", "bicubic", "nearest"};
		constexpr const char* kAstc[] = {"compressed", "full"};
		constexpr const char* kSpeedLimits[] = {"100", "150", "200", "300", "unlimited"};
		constexpr const char* kAntiAliasing[] = {"none", "fxaa", "smaa"};
		constexpr const char* kControllers[] = {"auto", "pro", "handheld", "dual_joycons", "left_joycon", "right_joycon"};
		constexpr const char* kOutputs[] = {"1080p", "1440p", "2160p"};
		constexpr const char* kMemoryLayouts[] = {"4Gb", "6Gb", "8Gb"};

		// the lines of Eden's trace a boot log takes, the last ones
		constexpr size_t kTraceLines = 60;

		void Set(std::vector<std::string>& args, const std::string& setting)
		{
			args.push_back("--set");
			args.push_back(setting);
		}

		// Eden's trace (its boot_trace.h, "[ProsperoEden diag] +123ms ..." lines), its last lines into
		// the boot log
		void LogTrace(const std::string& trace)
		{
			std::ifstream in(trace);
			std::deque<std::string> lines;
			for (std::string line; std::getline(in, line);)
			{
				lines.push_back(std::move(line));
				if (lines.size() > kTraceLines)
					lines.pop_front();
			}
			for (const std::string& line : lines)
				ps5log::Line("[switch] eden: {}", line);
		}
	}

	std::vector<std::string> GameArguments(const ps5emu::Game& game, const ps5settings::Launcher& forGame, bool exitAfterGame)
	{
		const ps5settings::N3ds& nx = forGame.nx;
		std::vector<std::string> args = {"--rom", game.path.string()};
		if (exitAfterGame)
			args.push_back("--exit-after-game");
		// where the side lists its games from: a game in a folder of its own there brings that folder's
		// update, DLC and mods, as one in Eden's roms/ does (patches/eden 0018)
		Set(args, "games_folder=" + ScanFolder(nx.gamesFolder));
		Set(args, std::string("resolution=") + kResolutions[std::clamp(nx.nxResolution, 0, 6)]);
		Set(args, std::string("upscaling_filter=") + kFilters[std::clamp(nx.nxFilter, 0, 3)]);
		Set(args, std::string("astc_textures=") + kAstc[std::clamp(nx.nxAstc, 0, 1)]);
		Set(args, std::string("console_mode=") + (nx.nxDocked ? "docked" : "handheld"));
		// ProsperoEden's frame generation: Lossless Scaling's when the player's own Lossless.dll is where
		// Eden loads it (#81: steadier than MihawkGen, with fewer artefacts on grass and water), else
		// MihawkGen, whose network it ships; and Eden's speed limit (patches/eden 0006)
		Set(args, std::string("frame_gen=") + (nx.nxFrameGeneration ? "1" : "0"));
		if (nx.nxFrameGeneration)
			Set(args, std::string("frame_gen_method=") + (LosslessFound() ? "lsfg" : "mihawkgen"));
		Set(args, std::string("speed_limit=") + kSpeedLimits[std::clamp(nx.nxSpeedLimit, 0, 4)]);
		Set(args, "volume=" + std::to_string(std::clamp(nx.volume, 0, 100)));
		Set(args, std::string("fps_overlay=") + (nx.performance ? "1" : "0"));
		Set(args, std::string("vibration=") + (forGame.rumble ? "1" : "0")); // Settings > Controls' Vibration, every side's
		// PS5-ECAM's own (patches/eden 0008): anti-aliasing, FSR's sharpening, the controller and the sticks' deadzone
		Set(args, std::string("anti_aliasing=") + kAntiAliasing[std::clamp(nx.nxAntiAliasing, 0, 2)]);
		Set(args, "fsr_sharpening=" + std::to_string(std::clamp(nx.nxSharpening, 0, 200)));
		Set(args, std::string("controller=") + kControllers[std::clamp(nx.nxController + 1, 0, 5)]);
		Set(args, "deadzone=" + std::to_string(std::clamp(nx.nxDeadzone, 0, 30)));
		// ProsperoEden's Settings the launcher has too (patches/eden 0009): the picture's output and refresh
		// rate, the Switch's language, the Performance switches and memory, and the buttons, the last two
		// packed into one value each (LoadExec carries the arguments as well as the request file)
		Set(args, std::string("output_resolution=") + kOutputs[std::clamp(nx.nxOutput, 0, 2)]);
		Set(args, std::string("refresh_rate=") + (nx.nxHighRefresh ? "120" : "60"));
		Set(args, std::string("language=") + kLanguages[LanguageFor(nx)].key);
		const std::pair<const char*, bool> switches[] = {{"async_shaders", nx.nxAsyncShaders}, {"fast_gpu", nx.nxFastGpu},
			{"unsafe_cpu", nx.nxUnsafeCpu}, {"unsafe_dma", nx.nxUnsafeDma}, {"reactive_flushing", nx.nxReactiveFlushing},
			{"skip_invalidation", nx.nxSkipInvalidation}, {"block_list", nx.nxBlockList}, {"compact_memory", nx.nxCompactMemory}};
		std::string performance = std::string("memory_layout:") + kMemoryLayouts[std::clamp(nx.nxMemory, 0, 2)];
		for (const auto& [name, on] : switches)
			performance += fmt::format(",{}:{}", name, on ? 1 : 0);
		Set(args, "performance=" + performance);
		Set(args, "mapping=" + MappingValue(nx));
		// the room Settings > Online > Multiplayer chose, joined before the game starts (ProsperoEden's rooms)
		if (!nx.nxRoomHost.empty())
		{
			Set(args, "room_host=" + nx.nxRoomHost);
			Set(args, "room_port=" + std::to_string(nx.nxRoomPort));
			Set(args, "room_name=" + nx.nxRoomName);
			Set(args, "room_nickname=" + Nickname(nx));
			if (!nx.nxRoomPassword.empty())
				Set(args, "room_password=" + nx.nxRoomPassword);
		}
		return args;
	}

	void LaunchGame(const ps5emu::Game& game, const ps5settings::Launcher& forGame, bool exitAfterGame, std::string& error)
	{
		if (!Available())
		{
			error = ps5lang::Tr("This build has no Switch emulator: eden/eboot.bin is not in the app's folder.");
			return;
		}
		// a network share (/net/...) is the launcher's own: Eden, a program of its own, cannot read it (#101)
		if (ps5netfs::is_net(game.path.string()))
		{
			ps5log::Line("[switch] {} is on a network share, which Eden cannot read: {}", game.name, game.path.string());
			error = ps5lang::Tr("Eden cannot read games on a network share. Copy this game to the PS5 or a USB drive to play it.");
			return;
		}
		ps5log::Line("[switch] starting {} in Eden ({})", game.name, game.path.string());
		if (forGame.nx.nxFrameGeneration)
			ps5log::Line("[switch] frame generation: {} {}", LosslessFound() ? "Lossless Scaling, from" : "MihawkGen, no Lossless.dll at", LosslessFile());
		ps5emu::LoadExecApp(kProgram, GameArguments(game, forGame, exitAfterGame), true, RequestFile());
		error = ps5lang::Tr("The console did not start Eden. The boot log says what it answered.");
	}

	void WriteSession(const Session& session)
	{
		// a line each, key=value, its line breaks taken out
		auto line = [](std::ofstream& out, const char* key, std::string value) {
			std::replace(value.begin(), value.end(), '\n', ' ');
			out << key << '=' << value << '\n';
		};
		const std::string file = ps5ingamenx::SessionFile(), staged = file + ".new";
		{
			std::ofstream out(staged, std::ios::trunc);
			line(out, "title", session.title);
			line(out, "details", session.details);
			line(out, "cover", session.cover);
			line(out, "language", ps5lang::Current());
			line(out, "font", ps5lang::GetMenuFont().path);
			line(out, "face", std::to_string(ps5lang::GetMenuFont().face));
			line(out, "exit", ps5emu::ExitAfterGame() ? "1" : "0"); // a home screen tile's game: its menu quits to the home screen
			line(out, "game", fmt::format("{:016x}", session.titleId)); // what the menu changes is kept under it (TakeResult)
			line(out, "root", ps5paths::Root());						 // the data folder, for its amiibo/
			if (!out)
			{
				ps5log::Line("[switch] {} could not be written: Eden shows its own loading screen", staged);
				return;
			}
		}
		std::rename(staged.c_str(), file.c_str());
	}

	std::string RequestFile()
	{
		return ps5paths::EdenRequestFile();
	}

	std::string Nickname(const ps5settings::N3ds& nx)
	{
		// ProsperoEden's own when none is chosen
		return nx.nxNickname.empty() ? std::string("PlayerPS5") : nx.nxNickname;
	}

	Result TakeResult()
	{
		Result result;
		const std::string file = ps5ingamenx::ResultFile();
		std::ifstream in(file);
		if (!in)
			return result;
		result.found = true;
		struct stat info;
		if (stat(file.c_str(), &info) == 0)
			result.written = info.st_mtime;
		for (std::string line; std::getline(in, line);)
		{
			const size_t equals = line.find('=');
			if (equals == std::string::npos)
				continue;
			const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
			const int number = std::atoi(value.c_str());
			auto& s = result.settings;
			if (key == "state")
				result.state = value;
			else if (key == "error")
				result.error = value;
			else if (key == "game")
				result.titleId = std::strtoull(value.c_str(), nullptr, 16);
			else if (key == "changed")
				result.changed = value == "1";
			else if (key == "filter")
				s.filter = number;
			else if (key == "antiAliasing")
				s.antiAliasing = number;
			else if (key == "sharpening")
				s.sharpening = number;
			else if (key == "frameGeneration")
				s.frameGeneration = number != 0;
			else if (key == "overlay")
				s.overlay = number != 0;
			else if (key == "speedLimit")
				s.speedLimit = number;
			else if (key == "volume")
				s.volume = number;
			else if (key == "docked")
				s.docked = number != 0;
			else if (key == "controller")
				s.controller = number;
			else if (key == "vibration")
				s.vibration = number != 0;
			else if (key == "deadzone")
				s.deadzone = number;
		}
		in.close();
		std::remove(file.c_str());
		ps5log::Line("[switch] Eden's result: {}{}{}", result.state, result.error.empty() ? "" : ": ", result.error);
		return result;
	}

	std::string ResultMessage(const Result& result)
	{
		using ps5lang::Tr;
		using ps5lang::TrF;
		if (result.state == "running")
			// Eden's program stopped without saying so: a crash, or the console closed it
			return TrF("The Switch game stopped: Eden closed while it ran. Its logs are in {0}.", DataFolder() + "/logs");
		if (result.state != "failed" || result.error.empty())
			return {};
		// a room that could not be joined: "room:" its name, a tab, Eden's words
		if (result.error.rfind("room:", 0) == 0)
		{
			const size_t tab = result.error.find('\t');
			const std::string name = result.error.substr(5, tab == std::string::npos ? std::string::npos : tab - 5);
			const std::string why = tab == std::string::npos ? std::string() : result.error.substr(tab + 1);
			// tr: a Switch game's room (Settings > Online > Multiplayer) could not be joined: its name ({0}) and why ({1}, in English)
			return TrF("The room {0} could not be joined: {1} Choose another in Settings > Online > Multiplayer, or play offline.", name, why);
		}
		if (result.error.rfind("Forwarded game not found", 0) == 0)
			return Tr("Eden could not find the game's file. Is the drive with it plugged in?");
		// tr: why a Switch game stopped or did not start, in Eden's own words (English)
		return TrF("The Switch game did not start or stopped: {0}", result.error);
	}

	void KeepMenuChanges(const Result& result)
	{
		if (!result.changed)
			return;
		// into the game's own settings when it has some, else the side's, as the other sides' menus do
		const ps5settings::Launcher all = ps5settings::Load();
		const ps5settings::Launcher before = ps5settings::ForGame(all, "switch", result.titleId);
		ps5settings::Launcher after = before;
		ps5settings::N3ds& nx = after.nx;
		const Result::Settings& s = result.settings;
		nx.nxFilter = std::clamp(s.filter, 0, 3);
		nx.nxAntiAliasing = std::clamp(s.antiAliasing, 0, 2);
		nx.nxSharpening = std::clamp(s.sharpening, 0, 200);
		nx.nxFrameGeneration = s.frameGeneration;
		nx.performance = s.overlay;
		nx.nxSpeedLimit = std::clamp(s.speedLimit, 0, 4);
		nx.volume = std::clamp(s.volume, 0, 100);
		nx.nxDocked = s.docked;
		nx.nxController = std::clamp(s.controller, -1, 4);
		nx.nxDeadzone = std::clamp(s.deadzone, 0, 30);
		after.rumble = s.vibration;
		ps5settings::SaveChanges("switch", result.titleId, before, after);
		ps5log::Line("[switch] the in-game menu's changes kept");
	}

	std::string TakeStaleRequest()
	{
		const std::string request = RequestFile();
		struct stat asked{};
		if (stat(request.c_str(), &asked) != 0)
			return {};
		std::remove(request.c_str());
		// Eden starts its trace in the app's sandbox before it asks for /data, so a trace newer than the
		// request is that launch's: Eden ran, but could not read what it was started with
		const std::string trace = ps5paths::SandboxDownload() + "/boot-trace.txt";
		struct stat traced{};
		if (stat(trace.c_str(), &traced) == 0 && traced.st_mtime >= asked.st_mtime)
		{
			ps5log::Line("[switch] Eden started but did not take its launch request ({}): it reads it from /data. Its trace, {}:", request,
				trace);
			LogTrace(trace);
			return {};
		}
		ps5log::Line("[switch] Eden never took its launch request ({}), and has no trace since: the console did not run "
					 "eden/eboot.bin and started this app again instead (boot.prev.log has the LoadExec it was asked for)",
			request);
		return ps5lang::Tr("The console did not start Eden. The boot log says what it answered.");
	}
}
