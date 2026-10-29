// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Settings: what the side you are on can change, found at a
// glance. One short list of pages with icons down the left, the side's own and the few every side
// shares, and nothing of another side; the page's rows across the rest of the screen, each a control
// (a toggle, a slider, pips, segments, swatches), the focused one with a line saying what it does, its
// longer help behind Triangle. Game settings are the same pages, scoped to one game: only what a game
// can have its own of, each value its side's (Default) until changed, Square back to Default.

#include "shell_internal.h"
#include "../actions.h"
#include "../sound.h"
#include "../../app/achievements.h"
#include "../../app/boxart.h"
#include "../../app/mods.h"
#include "../../app/pack_updates.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../app/usb_devices.h"
#include "../../azahar/azahar.h"
#include "../../azahar/controls.h"
#include "../../azahar/library.h"
#include "../../eden/eden.h"
#include "../../eden/library.h"
#include "../../eden/rooms.h"
#include "../../eden/save_transfer.h"
#include "../../melonds/library.h"
#include "../../melonds/melonds.h"
#include "../../ps5/privilege.h"
#include "../../ps5/display.h"
#include "../../ps5/log.h"
#include "../../ps5/pad.h"
#include "../../stream/stream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		// tr: the 3DS borders' names (artwork around the screens)
		constexpr const char* kBorderThemes[] = {TrMarkC("border", "None"), TrMarkC("border", "Midnight"), TrMarkC("border", "Waves"),
			TrMarkC("border", "Aurora"), TrMarkC("border", "Shell"), "PS5-ECAM"};
		constexpr const char* kRegions[] = {TrMark("Automatic"), TrMark("Japan"), TrMark("USA"), TrMark("Europe"), TrMark("Australia"),
			TrMark("China"), TrMark("Korea"), TrMark("Taiwan")};
		// tr: the languages the emulated 3DS can be set to
		constexpr const char* kLanguages[] = {TrMark("Automatic"), TrMark("Japanese"), TrMark("English"), TrMark("French"), TrMark("German"),
			TrMark("Italian"), TrMark("Spanish"), TrMark("Chinese (simplified)"), TrMark("Korean"), TrMark("Dutch"), TrMark("Portuguese"),
			TrMark("Russian"), TrMark("Chinese (traditional)")};
		constexpr const char* kResolutions[] = {"1x (400 × 240)", "2x (800 × 480)", "3x (1200 × 720)", "4x (1600 × 960)", "5x (2000 × 1200)",
			"6x (2400 × 1440)", "7x (2800 × 1680)", "8x (3200 × 1920)", "9x (3600 × 2160)", "10x (4000 × 2400)"};
		// tr: the 3DS's screen layouts
		constexpr const char* kLayouts[] = {TrMark("Top above bottom"), TrMark("Top screen only"), TrMark("Large top screen"), TrMark("Side by side")};
		// tr: texture filters: None, then the filters' own names
		constexpr const char* kTextureFilters[] = {TrMarkC("texture filter", "None"), "Anime4K", TrMark("Bicubic"), "ScaleForce", "xBRZ", "MMPX"};
		// the ones the list offers, by Azahar's number: Anime4K is its OpenGL renderer's only (the PS5 runs
		// its Vulkan one), so it is left out, and a setting that has it shows as None
		constexpr int kOfferedFilters[] = {0, 2, 3, 4, 5};

		int FilterIndex(int filter)
		{
			for (int i = 0; i < (int)std::size(kOfferedFilters); i++)
				if (kOfferedFilters[i] == filter)
					return i;
			return 0;
		}
		// tr: how a game's picture is scaled: the methods' names
		constexpr const char* kUpscaleFilters[] = {TrMark("Bilinear"), TrMark("Bicubic"), TrMark("Bicubic Hermite"), TrMark("Nearest neighbour"), TrMark("AMD FSR 1")};
		constexpr const char* kMusicNames[] = {TrMark("Setup theme"), TrMark("Off")};

		// The launcher music's choices, as ps5cemu.json names them: its own piece, each of the player's
		// files (ps5sound::MusicFolder), all of them shuffled when there are two or more, none. The folder
		// is read again at most every 3 s: the rows are built each frame Settings draws them.
		std::vector<std::string> MusicValues(std::vector<std::string>* names)
		{
			static std::vector<std::string> s_files;
			static auto s_listed = std::chrono::steady_clock::time_point{};
			const auto now = std::chrono::steady_clock::now();
			if (now - s_listed > std::chrono::seconds(3))
			{
				s_files = ps5sound::MusicFiles();
				s_listed = now;
			}
			std::vector<std::string> values{"setup"};
			if (names)
				names->assign({Tr(kMusicNames[0])});
			for (const std::string& file : s_files)
			{
				values.push_back("file:" + file);
				if (names)
					names->push_back(std::filesystem::path(file).stem().string());
			}
			if (s_files.size() > 1)
			{
				values.push_back("shuffle");
				if (names)
					// tr: Launcher music's choice that plays every file of the player's, in a random order
					names->push_back(Tr("Shuffle your music"));
			}
			values.push_back("off");
			if (names)
				names->push_back(Tr(kMusicNames[1]));
			return values;
		}
		constexpr int kHolds[] = {400, 600, 800, 1000, 1200, 1500};
		constexpr int kClocks[] = {25, 50, 75, 100, 125, 150, 200, 300, 400}; // the 3DS's CPU clock, as its in-game menu has them
		// tr: how the DS's screens are scaled to the TV: the filters' names
		constexpr const char* kDsFilters[] = {TrMark("Sharp"), TrMark("Smooth"), TrMark("Square pixels")};
		// tr: the DS's two screens turned together, a quarter turn at a time, for the games held on their side:
		// upright, a quarter turn clockwise, upside down, a quarter turn counterclockwise
		constexpr const char* kDsRotations[] = {TrMark("Upright"), TrMark("90° right"), TrMark("Upside down"), TrMark("90° left")};
		// the Switch's internal resolutions (Eden's), times the game's own 720p handheld or 1080p docked
		constexpr const char* kNxResolutions[] = {"0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x"};
		// Eden's upscaling filters, in its order (bilinear, AMD FSR, bicubic, nearest), by kUpscaleFilters' names
		constexpr int kNxFilters[] = {0, 4, 1, 3};
		// AMD FSR 1's sharpening on the Switch side, Eden's slider: each step halves it, shown as the share
		// left of the sharpest
		constexpr int kNxSharpening[] = {0, 25, 50, 100, 200};
		// tr: the Switch's controller types, as a game sees the DualSense (Automatic: what the game takes)
		constexpr const char* kNxControllers[] = {TrMark("Automatic"), TrMark("Pro Controller"), TrMark("Handheld"), TrMark("Joy-Con pair"),
			TrMark("Left Joy-Con"), TrMark("Right Joy-Con")};

		int NxSharpeningIndex(int value)
		{
			int best = 0;
			for (int i = 1; i < (int)std::size(kNxSharpening); i++)
				if (std::abs(kNxSharpening[i] - value) < std::abs(kNxSharpening[best] - value))
					best = i;
			return best;
		}

		// whether a room's address is one Eden takes: a name or an IPv4 address
		bool RoomHost(const std::string& host)
		{
			return !host.empty() && host.size() <= 253 && std::all_of(host.begin(), host.end(), [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
			});
		}

		// the DualSense buttons a turbo can be on, by the names the settings keep them by (ps5pad::ButtonNamed)
		constexpr const char* kTurboInputs[] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Create"};

		std::vector<std::string> TurboOptions(const std::string& turbo, int& index)
		{
			// tr: the turbo button's first choice: no button is one
			std::vector<std::string> options{TrC("turbo", "None")};
			index = 0;
			for (const char* input : kTurboInputs)
			{
				if (turbo == input)
					index = (int)options.size();
				options.push_back(ButtonName(input));
			}
			return options;
		}

		// the built-in themes' names and lines (ui/theme.cpp), for the translations
		[[maybe_unused]] constexpr const char* kThemeWords[] = {TrMark("Light"), TrMark("Link Jack"), TrMark("Classic (3.0)"), TrMark("Midnight"),
			TrMark("Eden"), TrMark("Frosted glass over each side's colours."),
			TrMark("3.0.0's launcher: deep blue panels, flatter, a little squarer."),
			TrMark("True black behind everything and nothing moving: for OLED screens."),
			TrMark("Dark frosted glass and rounder corners, the side's light drifting and motes of light rising."),
			TrMark("A light background with dark text.")};

		int ClockIndex(int clock)
		{
			for (int i = 0; i < (int)std::size(kClocks); i++)
				if (kClocks[i] >= clock)
					return i;
			return (int)std::size(kClocks) - 1;
		}

		// a table of names, in the language
		template<size_t N>
		std::vector<std::string> Options(const char* const (&list)[N], const char* context = nullptr)
		{
			std::vector<std::string> options;
			for (const char* name : list)
				options.push_back(context ? TrC(context, name) : Tr(name));
			return options;
		}

		std::vector<std::string> TextureFilterNames()
		{
			std::vector<std::string> names;
			for (int filter : kOfferedFilters)
				names.push_back(filter == 0 ? TrC("texture filter", kTextureFilters[filter]) : Tr(kTextureFilters[filter]));
			return names;
		}

		using ps5lang::Percent;
	}

	std::vector<SettingsPage> Shell::SettingsPages() const
	{
		// the side's pages; the shared ones on every side
		std::vector<SettingsPage> pages;
		// the menus' language first, with a globe, which reads in any language
		pages.push_back({"ui-language", Tr("Language"), Tr("The language of the menus: the PS5's own, unless you choose another."), Icon::Globe});
		const char* every = IsWiiU() ? Tr("For every Wii U game.") :
			Is3ds()					 ? Tr("For every 3DS game.") :
			IsSwitch()				 ? Tr("For every Switch game.") :
									   Tr("For every DS game.");
		pages.push_back({"graphics", Tr("Graphics"), every, Icon::Monitor});
		if (Is3ds() || IsDs())
			pages.push_back({"screens", Tr("Screens"), every, Icon::Screens});
		// the remote touch screen is on the 3DS's and the DS's Controls, behind a row (on the Wii U's System; a
		// Switch game runs in Eden's program, which has none)
		pages.push_back({"controls", Tr("Controls"), IsWiiU() ? Tr("What each player's DualSense is to the Wii U.") :
				Is3ds()										  ? Tr("The DualSense as the 3DS, and the remote touch screen.") :
				IsSwitch()									  ? Tr("The DualSense as the Switch.") :
																Tr("The DualSense as the DS, and the remote touch screen."),
			Icon::Pad});
		// the emulated console and the sound, one page (the owner's, 2026-10-27: fewer pages down the left); the
		// Wii U's has its USB portals and the remote touch screen behind rows
		pages.push_back({"system", Tr("System"), IsWiiU() ? Tr("The sound, Cemu's USB portals and the remote touch screen.") :
													Is3ds() ? Tr("The emulated 3DS's region, language and speed, and the sound.") :
													IsSwitch() ? Tr("The emulated Switch, docked or handheld, its speed and language, and the sound.") :
																 Tr("The emulated DS's language, BIOS, microphone and Slot-2, and the sound."),
			Icon::Chip});
		// ProsperoEden's Settings > Performance, as its launcher has it
		if (IsSwitch())
			pages.push_back({"performance", Tr("Performance"), Tr("Speed against accuracy, and the emulated Switch's memory."), Icon::Pulse});
		pages.push_back({"files", Tr("Game files"), IsWiiU() ? Tr("Where your Wii U games are, and installs into the Wii U's storage.") :
										Is3ds()		 ? Tr("Where your 3DS games are, and installs into the 3DS's storage.") :
										IsSwitch()	 ? Tr("Where your Switch games are, with its keys and firmware.") :
													   Tr("Where your DS games are, and where their saves go."),
			Icon::Folder});
		// the Wii U's accounts and every side's saves (6.13); a handheld has one player, so its page is saves alone.
		// A Switch game's saves are Eden's, in its own folder, each of Eden's players with their own
		pages.push_back({"saves",
			IsWiiU() ? Tr("Accounts and saves") :
				// tr: Settings' page of the Switch side's players (Eden's user profiles, the people) and every side's saves
				IsSwitch() ? Tr("Players and saves") :
							 Tr("Saves"),
			IsWiiU()	 ? Tr("Each account has its own saves for every game. Backups hold every side's saves.") :
				IsSwitch() ? std::string(Tr("Who plays, each with saves of their own.")) + " " + Tr("Backups hold every side's saves.") :
							 Tr("Backups hold every side's saves."),
			Icon::Save});
		// with the Switch's rooms (port/eden/rooms.h) and the 3DS's Artic Base, what goes over the network
		pages.push_back({"online", Tr("Online"), IsWiiU() ? Tr("Box art, graphic packs and PS5-ECAM's updates.") :
										   IsDs()	 ? Tr("Box art, RetroAchievements and PS5-ECAM's updates.") :
										   Is3ds()	 ? Tr("Box art, Artic Base and PS5-ECAM's updates.") :
													   Tr("Box art, multiplayer rooms and PS5-ECAM's updates."),
			Icon::Globe});
		pages.push_back({"launcher", Tr("Launcher"), Tr("Theme, sides, language and text."), Icon::Sparkle});
		pages.push_back({"playtime", Tr("Play time"), Tr("How long you have played, each side and the most played games."), Icon::Play});
		pages.push_back({"diagnostics", Tr("Diagnostics"), Tr("The Setup check, logs, caches and this session's details."), Icon::Pulse});
		// the version always in sight, whatever the credits' length in a language
		pages.push_back({"about", TrC("app", "About"), std::string(Tr("Who made what.")) + " · " + Tr("Version") + " " + ps5update::Readable(PS5CEMU_VERSION),
			Icon::Info});
		if (IsRetro())
		{
			std::vector<SettingsPage> selected;
			if (InGameSettings()) return {{"runtime", Tr("Emulator settings"), Tr("Controls, settings and saves are managed by the bundled emulator."), Icon::Info}};
			selected.push_back({"runtime", Tr("Emulator settings"), Tr("Controls, settings and saves are managed by the bundled emulator."), Icon::Info});
			for (const auto& page : pages) if (page.id == "files" || page.id == "launcher" || page.id == "playtime" || page.id == "diagnostics" || page.id == "about") selected.push_back(page);
			return selected;
		}
		if (!InGameSettings())
			return pages;
		// a game's settings: only the pages with something a game can have its own of (SideRows' rows
		// with a setting: Graphics, Screens, System and the Switch's Performance; the Switch's Picture effects,
		// Eden's own, are behind a row on its Graphics)
		std::vector<SettingsPage> game;
		for (const SettingsPage& page : pages)
			if (page.id == "graphics" || page.id == "screens" || page.id == "system" || page.id == "performance")
				game.push_back({page.id, page.title, TrF("For {0} only. Its other settings follow its side's.", m_gameName), page.icon});
		// its saves alone (6.15), whatever settings it has of its own (a Switch game's are Eden's)
		game.push_back({"gamesaves", Tr("Saves"), TrF("{0}'s saves only. Settings has every side's.", m_gameName), Icon::Save});
		if (IsSwitch())
		{
			game.push_back({"nxmods", Tr("Mods"), Tr("Its mods, each on or off."), Icon::Folder});
			game.push_back({"nxcheats", Tr("Cheats"), Tr("Its cheats, each on or off."), Icon::Folder});
		}
		return game;
	}

	void Shell::SettingsOpen(const std::string& page, bool onRows, int row)
	{
		m_switchExtrasKey.clear();
		if (InGameSettings())
			GameSettingsClose();
		m_page = page;
		m_subPage.clear();
		m_onRows = onRows;
		m_settingRow = row;
		m_onBar = false;
		m_tab = ScreenId::Settings;
		m_holdProgress = 0;
		m_message.clear();
		Show(ScreenId::Settings);
	}

	std::string Shell::PacksStatus() const
	{
		using State = ps5packs::Status::State;
		const auto status = ps5packs::GetStatus();
		const std::string installed = ps5packs::InstalledVersion();
		switch (status.state)
		{
		case State::Checking: return Tr("Checking GitHub…");
		case State::Downloading:
			return status.total ? TrF("Downloading: {0}", Percent((int)(status.received * 100 / status.total))) :
								  TrF("Downloading: {0} MB", status.received >> 20);
		case State::Installing: return Tr("Installing…");
		case State::UpToDate: return TrF("Up to date: {0}", installed);
		case State::Done: return TrF("Installed: {0}", status.version);
		case State::Failed: return status.message;
		case State::Idle: break;
		}
		return installed.empty() ? std::string(Tr("Cross to download")) : installed;
	}

	std::string Shell::AppUpdateStatus()
	{
		using State = ps5update::Status::State;
		const auto status = ps5update::GetStatus();
		switch (status.state)
		{
		case State::Checking: return Tr("Checking GitHub…");
		case State::UpToDate: return TrF("Up to date: {0}", ps5update::Readable(PS5CEMU_VERSION));
		// tr: {0} is a version, as in 3.5.0
		case State::Available: return TrF("{0}: Cross to install", ps5update::Readable(status.latest));
		case State::Downloading:
			return status.total ? TrF("Downloading: {0}", Percent((int)(status.received * 100 / status.total))) : std::string(Tr("Downloading…"));
		case State::Verifying: return Tr("Checking the download…");
		case State::Installing: return Tr("Installing…");
		case State::Ready: return Tr("Installed: Cross to restart");
		case State::Failed: return status.message;
		case State::Idle: break;
		}
		return Tr("Cross to check");
	}

	void Shell::PollPacks()
	{
		if (ps5packs::GetStatus().state == ps5packs::Status::State::Done && !m_packsReloaded && m_status.coreReady && IsWiiU())
		{
			m_packsReloaded = true;
			ps5emu::ReloadGraphicPacks();
		}
	}

	std::string Shell::ModsStatus() const
	{
		using State = ps5mods::Status::State;
		const auto status = ps5mods::GetStatus();
		switch (status.state)
		{
		case State::Copying:
			// tr: {0} is a mod's name, {1} how much of all the mods is copied, as in 40%
			return status.total ? TrF("Copying {0}: {1}", status.current, Percent((int)(status.copied * 100 / status.total))) :
								  std::string(Tr("Looking for mods…"));
		case State::Done: return status.mods ? TrP(status.mods, "{0} mod added", "{0} mods added") : std::string(Tr("No mods found"));
		case State::Failed: return status.message;
		case State::Idle: break;
		}
		return Tr("Cross to look on your drives");
	}

	void Shell::AddMods(uint64_t titleId, const std::string& name)
	{
		const std::vector<std::string> drives = ConnectedDrives();
		if (ps5mods::Folders(drives).empty())
		{
			m_feedback.Play(ui::Cue::Denied);
			OpenHelp(Tr("No mods on your drives"),
				Tr("Copy each mod's folder into a ps5cemu/mods folder on a USB drive and plug it in. A mod is a Cemu graphic pack (a "
				   "folder with a rules.txt), or a folder with content and aoc folders in it for the files it replaces. The guide on "
				   "GitHub, docs/GUIDE.md, has more."));
			return;
		}
		ps5mods::Start(drives, titleId, name);
		m_modsToastAt = m_now;
		m_feedback.Play(ui::Cue::Select);
		Toast(Tr("Copying mods from your drives…"));
	}

	void Shell::PollMods()
	{
		const ps5mods::Status status = ps5mods::GetStatus();
		if (status.state == ps5mods::Status::State::Copying)
		{
			// a large mod takes minutes: how far it is, now and then
			if (m_now - m_modsToastAt > 8)
			{
				m_modsToastAt = m_now;
				Toast(ModsStatus());
			}
			return;
		}
		if (status.finished == m_modsFinished)
			return;
		m_modsFinished = status.finished;
		// the mods copied, failure or not, are Cemu's packs from now
		if (status.mods > 0 && m_status.coreReady)
			ps5emu::ReloadGraphicPacks();
		const std::string skipped = status.skipped ?
			"\n\n" + TrP(status.skipped, "{0} folder had no rules.txt and no game: put a mod like that in a folder named by its game's title ID, "
										 "or add it from its game's menu.",
				"{0} folders had no rules.txt and no game: put a mod like that in a folder named by its game's title ID, or add it from "
				"its game's menu.") :
			std::string();
		if (status.state == ps5mods::Status::State::Failed)
		{
			m_feedback.Play(ui::Cue::Denied);
			OpenHelp(Tr("The mods could not all be copied"),
				status.message + (status.mods ? "\n\n" + TrP(status.mods, "{0} mod was added.", "{0} mods were added.") : std::string()) + skipped);
			return;
		}
		if (status.mods == 0)
		{
			m_feedback.Play(ui::Cue::Denied);
			OpenHelp(Tr("No mods found"), Tr("The ps5cemu/mods folders on your drives hold no folder with a rules.txt, nor with content, aoc "
											 "or code folders.") + skipped);
			return;
		}
		if (!skipped.empty())
			OpenHelp(TrP(status.mods, "{0} mod added", "{0} mods added"), Tr("Turn them on in their games' Graphic packs.") + skipped);
		else
			Toast(TrP(status.mods, "{0} mod added: turn it on in its game's Graphic packs", "{0} mods added: turn them on in their games' Graphic packs"));
	}

	// -- the rows -------------------------------------------------------------------------------------

	std::vector<Row> Shell::SideRows(const std::string& page, const ps5settings::Launcher& settings)
	{
		using Kind = Row::Kind;
		if (IsRetro() && (page == "files" || page == "runtime" || page == "graphics" || page == "system" || page == "screens" || page == "gamesaves"))
		{
			std::vector<Row> rows;
			const int backend = settings.retro[SideIndex() - 4].retroBackend;
			if ((SideIndex() == 6 || SideIndex() == 7) && page == "runtime" && !InGameSettings())
			{
				Row runtime; runtime.id = "retro-backend"; runtime.kind = Kind::Choice; runtime.label = Tr("Emulator runtime");
				runtime.options = SideIndex() == 6 ? std::vector<std::string>{"N64XY", "Mupen64Plus (experimental)"} : std::vector<std::string>{"NESYX", "FCEUmm PS5 (experimental)"};
				runtime.index = backend; runtime.value = runtime.options[backend];
				runtime.description = Tr("Changes the next launch. Each emulator keeps separate settings and saves. Build and package the selected runtime first."); rows.push_back(runtime);
			}
			if (page == "files")
			{
				Row folder; folder.id = "folder"; folder.kind = Kind::Link; folder.label = Tr("Games folder"); folder.value = GamesFolder(m_side); folder.description = Tr("Select this system's games folder. Existing games and saves stay in place."); rows.push_back(folder);
				Row scan; scan.id = "rescan"; scan.kind = Kind::Action; scan.label = Tr("Look for games now"); rows.push_back(scan);
			}
			else
			{
				Row info; info.id = "retro-library"; info.kind = Kind::Action; info.dimmed = !ps5retro::Available(SideIndex(), backend); info.label = Tr("Open emulator settings and saves");
				info.description = Tr("The bundled emulator keeps its own settings, controls and save formats. Open its native frontend to change settings. Close that frontend to return to ECAM. Existing standalone data is retained."); rows.push_back(info);
			}
			if (page == "gamesaves") { Row backup; backup.id = "backup"; backup.kind = Kind::Action; backup.label = Tr("Back up to USB"); backup.description = Tr("Backs up all systems, including both N64 and NES backends. Save states remain specific to their emulator."); rows.push_back(backup); }
			Row diagnostic; diagnostic.id = "retro-diagnostics"; diagnostic.kind = Kind::Action; diagnostic.label = Tr("Emulator diagnostics"); rows.push_back(diagnostic);
			return rows;
		}
		const ps5settings::N3ds& hand = m_side == System::Nds ? settings.nds : m_side == System::Switch ? settings.nx : settings.n3ds;
		std::vector<Row> rows;
		std::string group; // the › page the rows made now are on (sub)
		auto toggle = [&](const char* id, const std::string& label, bool on, const std::string& description, const std::string& help) {
			Row row;
			row.kind = Kind::Toggle;
			row.id = id;
			row.label = label;
			row.on = on;
			row.value = on ? Tr("On") : Tr("Off");
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		auto choice = [&](const char* id, const std::string& label, std::vector<std::string> options, int index, const std::string& description,
						  const std::string& help, Kind kind = Kind::Choice) {
			Row row;
			row.kind = kind;
			row.id = id;
			row.label = label;
			row.options = std::move(options);
			row.index = std::clamp(index, 0, (int)row.options.size() - 1);
			row.value = row.options[row.index];
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		auto slider = [&](const char* id, const std::string& label, int value, int most, const std::string& description, const std::string& help) {
			Row row;
			row.kind = Kind::Slider;
			row.id = id;
			row.label = label;
			row.fraction = (float)value / most;
			row.value = Percent(value);
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		auto action = [&](Kind kind, const char* id, const std::string& label, const std::string& value, const std::string& description,
						  const std::string& help) {
			Row row;
			row.kind = kind;
			row.id = id;
			row.label = label;
			row.value = value;
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		// a › row: the rows made after it, until the next or endSub, are a page of their own behind it,
		// which Cross opens (SubPageRows); value: what they come to, in a word or two
		auto sub = [&](const char* page, const std::string& label, const std::string& value, const std::string& description, const std::string& help) {
			group.clear();
			Row* row = action(Kind::Link, "sub", label, value, description, help);
			row->opens = page;
			group = page;
			return row;
		};
		auto endSub = [&] { group.clear(); };
		// a Wii U row Cemu's core must be up for
		auto cemu = [&](Row* row) {
			row->needsCore = true;
			if (!m_status.coreReady)
			{
				row->dimmed = true;
				row->description = m_status.coreStarting ? Tr("Cemu is still starting. One moment.") : Tr("Cemu did not start, so this cannot be changed.");
			}
			return row;
		};
		const std::string side = ps5launcher::SideName(m_side);
		auto own = [&](Row* row, const char* setting) { row->setting = setting; };

		if (page == "ui-language")
		{
			// Cross, Left or Right opens the list
			int index = 0;
			choice("ui-language", Tr("Language"), LanguageOptions(index), index, Tr("The menus' language, and the games' names where they have one in it."),
				Tr("PS5-ECAM speaks the PS5's own language unless you choose another here; the first start's Setup check has the same "
				   "choice. A game's name follows it where the game has one in that language, as on a console set to it; otherwise it is "
				   "the English one. Descriptions come from GameTDB in English, and graphic packs from their authors, as they wrote them."));
		}
		else if (page == "graphics" && IsWiiU())
		{
			own(choice("upscaling", Tr("Upscaling to 4K"), Options(kUpscaleFilters), settings.upscaleFilter, Tr("How the picture is scaled to the TV."),
					Tr("Bicubic is sharp, Bicubic Hermite a little softer, Bilinear softer still; Nearest neighbour keeps pixels square. AMD FSR 1 keeps "
					   "edges clean for a game drawn below 4K, for a little more work by the GPU than Bicubic.")),
				"upscaleFilter");
			// the display's rate and what a game's frames do in it
			std::string rate = settings.highFrameRate ? "120 Hz" : "60 Hz";
			if (settings.framePacing > 1)
				rate += " · " + ps5display::FramePacingName(settings.framePacing, settings.highFrameRate);
			if (settings.frameGeneration)
				rate += std::string(" · ") + Tr("Frame generation");
			// tr: Settings > Graphics: a row that opens the frame rate's settings (120 Hz output, frame pacing, frame generation)
			sub("framerate", Tr("Frame rate"), rate, Tr("120 Hz output, frame pacing and frame generation."),
				Tr("The display's refresh rate, a game held to an even frame rate, and a picture made between each two frames. Triangle on "
				   "each says more."));
			own(toggle("highframerate", Tr("120 Hz output"), settings.highFrameRate, Tr("For displays that take 120 Hz."),
					Tr("The 119.88 Hz mode, on displays that support it. Games still run at their own speed, and a frame that misses a refresh "
					"waits 8 ms for the next instead of 17.")),
				"highFrameRate");
			std::vector<std::string> pacing;
			for (int i = 1; i <= 3; i++)
				pacing.push_back(ps5display::FramePacingName(i, settings.highFrameRate));
			own(choice("framepacing", Tr("Frame pacing"), pacing, settings.framePacing - 1, Tr("Holds a game to an even frame rate."),
					Tr("Each frame stays on screen for at least two or three refreshes, so a game that cannot hold the display's rate runs at an "
					"even one: 60 or 40 fps with 120 Hz output, 30 or 20 without. 60 fps with 120 Hz output suits a game at 4K that sometimes "
					"drops under 60; 30 fps at 60 Hz suits one at 8K. Also in the in-game menu."),
					Kind::Segmented),
				"framePacing");
			own(toggle("framegeneration", Tr("Frame generation"), settings.frameGeneration, Tr("A game held to 30 fps shown at 60."),
					Tr("MihawkGen makes a picture between each two frames of a game that takes two refreshes a frame or more, so a game at 30 fps "
					   "is shown at 60, or at 60 with 120 Hz output. A game that runs faster is shown as it is. It costs the GPU about 2 ms a "
					   "picture at 4K, and adds a little delay. Also in the in-game menu.")),
				"frameGeneration");
			endSub();
			// ReShade's preset, as a PC's ReShade makes it (port/cemu/ReShadePS5.h)
			own(toggle("reshade", Tr("ReShade"), settings.reshade, TrF("Your ReShade preset and effects, from {0}.", ps5paths::CemuRoot() + "/reshade"),
					TrF("Runs a ReShade preset made on a PC over the game's picture, before the GamePad's screen and the menus. Put "
						"ReShadePreset.ini (or presets/<title ID>.ini for one game) with the Shaders and Textures folders it uses in {0}. "
						"Compiled as the game starts; the boot log says what runs. Also in the in-game menu.",
						ps5paths::CemuRoot() + "/reshade")),
				"reshade");
			own(toggle("overlay", Tr("Performance overlay"), settings.overlay, Tr("Frame rate, CPU and memory in a corner."),
					Tr("Frames per second, CPU and memory use in the top left corner, as Cemu shows them. Also in the in-game menu.")),
				"overlay");
			// tr: Settings > Graphics: a row that opens the shaders' settings
			sub("shaders", Tr("Shaders"), "", Tr("How new shaders are built, and Cemu's notes about them."),
				Tr("Async shader compile, and the notes Cemu shows while a game builds its shaders. Triangle on each says more."));
			own(toggle("async", Tr("Async shader compile"), settings.asyncShaders, Tr("No stutter while new shaders build."),
					Tr("On, a new shader is built while the game carries on, so it does not stutter, but some things may be missing for a moment. "
					"Off, the game waits for each one: stutter, but nothing drawn wrong.")),
				"asyncShaders");
			toggle("shadernotices", Tr("Shader notices"), settings.shaderNotices, Tr("Cemu's \"compiled shaders\" notes over a game."),
				Tr("The notes Cemu shows in a corner while a game builds its shaders and pipelines (\"Loaded 1200 shaders\"). Off hides "
				   "them; the shaders are built either way. Also in the in-game menu."));
			endSub();
		}
		else if (page == "graphics" && Is3ds())
		{
			own(choice("resolution", Tr("Internal resolution"), Options(kResolutions), hand.resolution - 1, Tr("Higher is sharper and asks more of the GPU."),
					Tr("How large the 3DS's 3D scenes are drawn before they are scaled to the TV. Higher is sharper and asks more of the GPU, and "
					"each time a game reads a picture back the wait grows with it."),
					Kind::Stepper),
				"resolution");
			std::string textures = TextureFilterNames()[FilterIndex(hand.textureFilter)];
			if (hand.customTextures)
				textures += std::string(" · ") + Tr("Custom textures");
			// tr: Settings > Graphics: a row that opens the textures' settings (the texture filter, custom textures)
			sub("textures", Tr("Textures"), textures, Tr("The texture filter, and texture packs."),
				Tr("A filter that smooths the game's textures as they are scaled up, and texture packs of your own. Triangle on each says more."));
			own(choice("filter", Tr("Texture filter"), TextureFilterNames(), FilterIndex(hand.textureFilter), Tr("Smooths textures as they are scaled up."),
					Tr("Smooths the game's textures as they are scaled up; None keeps them as the 3DS draws them. A filter redraws every texture "
					"at the internal resolution: at high resolutions it is the costliest setting. If a game stutters, try None first.")),
				"textureFilter");
			own(toggle("customtextures", Tr("Custom textures"), hand.customTextures, Tr("Texture packs from azahar/load/textures."),
					// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
					TrF("Texture packs in {0}/azahar/load/textures/<title ID>, as the desktop Azahar loads them.", ecam_layout::Parent(ps5paths::Azahar()))),
				"customTextures");
			// tr: Settings > Graphics' Shaders row, its value: where the 3DS's vertex shaders run, on the GPU (Hardware) or on the CPU (Software)
			sub("shaders", Tr("Shaders"), hand.hwShaders ? TrC("shaders", "Hardware") : TrC("shaders", "Software"),
				Tr("Where the 3DS's shaders run, and how new ones are built."),
				Tr("Hardware shaders, the outline fix for them, and async shader compile. Triangle on each says more."));
			own(toggle("hwshaders", Tr("Hardware shaders"), hand.hwShaders, Tr("Off: 3D drawn exactly, on the CPU. Off for Pokémon."),
					Tr("On, the 3DS's vertex shaders run on the PS5's GPU, the fastest way. Off, they run on the CPU as Azahar's software "
					   "shaders: slower, but a model and the outline drawn around it are computed alike, and its lighting is corrected per "
					   "triangle. Off by default for Pokémon X, Y, Omega Ruby, Alpha Sapphire, Sun, Moon, Ultra Sun and Ultra Moon, whose "
					   "outlines, lighting and shadows glitch otherwise. Applies to the next game.")),
				"hwShaders");
			own(toggle("outlinefix", Tr("Outline fix"), hand.invariantPosition, Tr("Fixes moving black dots on outlined 3D models."),
					Tr("Computes every 3D position the same way twice, as Azahar does on Apple's GPUs. For a game that shows moving black dots "
					   "on its outlined models with hardware shaders on; the Pokémon games are drawn right with Hardware shaders off "
					   "instead. Applies to the next game.")),
				"invariantPosition");
			// off by default, as desktop Azahar: on, a draw whose shader is still building is skipped (Alpha
			// Sapphire's first cutscene drew its Pokemon as their dark outlines alone)
			own(toggle("async-3ds", Tr("Async shader compile"), hand.asyncShaders, Tr("No stutter while new shaders build."),
					Tr("On, a new shader is built while the game carries on, so it does not stutter, but some things may be missing for a moment. "
					"Off, the game waits for each one: stutter, but nothing drawn wrong.")),
				"asyncShaderCompilation");
			endSub();
			own(toggle("framegeneration-3ds", Tr("Frame generation"), hand.frameGeneration, Tr("A game held to 30 fps shown at 60."),
					Tr("MihawkGen makes a picture between each two frames of a game that takes two refreshes a frame or more, so a game at 30 fps "
					   "is shown at 60, or at 60 with 120 Hz output. A game that runs faster is shown as it is. It costs the GPU about 2 ms a "
					   "picture at 4K, and adds a little delay. Also in the in-game menu.")),
				"frameGeneration");
			own(toggle("anaglyph", Tr("3D for red and cyan glasses"), hand.anaglyph, Tr("The 3DS's 3D, in red and cyan."),
					Tr("The 3DS's 3D screen as both eyes' pictures in one, red for the left and cyan for the right, to see in 3D with red and "
					   "cyan glasses, as Azahar's anaglyph mode makes it, the 3D slider all the way up. Colours change with it. Applies to "
					   "the next game.")),
				"anaglyph");
			toggle("performance-hand", Tr("Performance overlay"), hand.performance, Tr("The frame rate and the emulation's speed, in the top left corner."),
				Tr("The frame rate and the emulation's speed, in the top left corner."));
		}
		else if (page == "graphics" && IsSwitch())
		{
			own(choice("nx-resolution", Tr("Internal resolution"), Options(kNxResolutions), hand.nxResolution, Tr("Higher is sharper and asks more of the GPU."),
					Tr("How large Eden draws a game before it is scaled to the TV, against the game's own picture: 1x is 1080p docked and 720p "
					   "handheld. Higher is sharper and asks more of the GPU; 3x and 4x need the graphics memory for it. Applies to the next game."),
					Kind::Stepper),
				"nxResolution");
			std::vector<std::string> filters;
			for (int filter : kNxFilters)
				filters.push_back(Tr(kUpscaleFilters[filter]));
			own(choice("nx-filter", Tr("Upscaling to 4K"), filters, hand.nxFilter, Tr("How the picture is scaled to the TV."),
					Tr("Bilinear is soft, Bicubic sharper; Nearest neighbour keeps pixels square. AMD FSR 1 keeps edges clean for a game drawn "
					   "below 4K, for a little more work by the GPU.")),
				"nxFilter");
			// FSR's sharpening, anti-aliasing and Eden's ReShade looks (switch_extras.cpp's, which SettingRows adds
			// behind this row), what is done over the picture
			const std::vector<std::string> antiAliasing{Tr("Off"), "FXAA", "SMAA"};
			// tr: Settings > Graphics on the Switch side: a row that opens FSR sharpening, anti-aliasing and Eden's ReShade looks
			sub("nxpost", Tr("Picture effects"), antiAliasing[std::clamp(hand.nxAntiAliasing, 0, 2)], Tr("Sharpening, anti-aliasing and ReShade's looks."),
				Tr("FSR sharpening, while AMD FSR 1 scales the picture; Eden's anti-aliasing; and a ReShade look over the picture, Eden's own "
				   "or one of yours. Triangle on each says more."));
			// FSR 1's own setting, while it is the filter
			if (hand.nxFilter == 1)
			{
				std::vector<std::string> levels;
				for (int value : kNxSharpening)
					levels.push_back(Percent((int)std::lround(100.0 * std::pow(2.0, -value / 100.0))));
				own(choice("nx-sharpening", Tr("FSR sharpening"), levels, NxSharpeningIndex(hand.nxSharpening),
						Tr("How much AMD FSR 1 sharpens the picture."),
						Tr("How much AMD FSR 1 sharpens the picture as it scales it to the TV: 100% is the most, and each step down halves it. "
						   "Also in the in-game menu, where it changes at once.")),
					"nxSharpening");
			}
			own(choice("nx-aa", Tr("Anti-aliasing"), antiAliasing, hand.nxAntiAliasing, Tr("Smooths the jagged edges of the picture."),
					Tr("Eden's anti-aliasing, over the game's picture: FXAA smooths edges lightly, SMAA more cleanly, each for a little more "
					   "work by the GPU. Off, as Eden has it. Also in the in-game menu, where it changes at once."),
					Kind::Segmented),
				"nxAntiAliasing");
			endSub();
			own(choice("nx-astc", Tr("ASTC textures"), {Tr("Compressed"), Tr("Full quality")}, hand.nxAstc,
					Tr("Compressed saves graphics memory; full quality looks best."),
					Tr("Many Switch games keep their textures in ASTC, which the PS5's GPU cannot read: Eden converts them. Compressed keeps them "
					   "small, as BC3; full quality keeps them as they are, at four times the graphics memory, for a game whose textures look "
					   "blocky. Applies to the next game."),
					Kind::Segmented),
				"nxAstc");
			// ProsperoEden's Settings > Video: the picture Eden hands the console, and the TV's refresh rate
			const char* outputs[] = {"1080p", "1440p", "2160p"};
			std::string output = std::string(outputs[std::clamp(hand.nxOutput, 0, 2)]) + (hand.nxHighRefresh ? " · 120 Hz" : " · 60 Hz");
			if (hand.nxFrameGeneration)
				output += std::string(" · ") + Tr("Frame generation");
			// tr: Settings > Graphics: a row that opens what Eden sends to the TV (output resolution, 120 Hz, frame generation)
			sub("output", Tr("Output to the TV"), output, Tr("The picture's size, 120 Hz and frame generation."),
				Tr("The size of the picture Eden sends to the TV, whether it is sent at 120 Hz, and ProsperoEden's frame generation. Each "
				   "applies to the next game."));
			own(choice("nx-output", Tr("Output resolution"), {"1080p", "1440p", "2160p"}, std::clamp(hand.nxOutput, 0, 2),
					Tr("The size of the picture Eden sends to the TV. Applies to the next game."),
					Tr("The size of the picture Eden sends to the TV. Applies to the next game."), Kind::Segmented),
				"nxOutput");
			own(toggle("nx-120hz", Tr("120 Hz"), hand.nxHighRefresh, Tr("For a TV that takes 120 Hz. Applies to the next game."),
					Tr("For a TV that takes 120 Hz. Applies to the next game.")),
				"nxHighRefresh");
			// ProsperoEden's own: Lossless Scaling's with the player's own Lossless.dll, else MihawkGen (#81)
			own(toggle("nx-framegeneration", Tr("Frame generation"), hand.nxFrameGeneration, Tr("A game held to 30 fps shown at 60."),
					// tr: {0} is where Lossless.dll goes: /data/eden/user/lossless/Lossless.dll, or the same on the app's drive
					TrF("ProsperoEden's own frame generation: a picture made between each two frames, so a game at 30 fps is shown at 60. "
						"It is Lossless Scaling's when your own copy of its Lossless.dll is at {0}, else MihawkGen's. It asks more of the "
						"GPU and adds a little delay. Applies to the next game.",
						ps5eden::LosslessFile())),
				"nxFrameGeneration");
			endSub();
			toggle("performance-hand", Tr("Performance overlay"), hand.performance, Tr("The frame rate and the emulation's speed, in the top left corner."),
				Tr("The frame rate and the emulation's speed, in the top left corner."));
		}
		else if (page == "graphics")
		{
			own(choice("dsfilter", Tr("Screen filter"), Options(kDsFilters), hand.dsFilter, Tr("How the DS's screens are scaled."),
					Tr("How the DS's 256 x 192 screens are scaled to the TV. Sharp keeps every pixel square and even at any size; Smooth blurs "
					"them together; Square pixels takes the nearest, uneven where the size is not a whole number. The in-game menu changes it "
					"too."),
					Kind::Segmented),
				"dsFilter");
			const bool executable = ps5privilege::Current().executable;
			own(toggle("dsjit", Tr("Recompiler"), hand.dsJit,
					executable ? Tr("melonDS's JIT: much faster than its interpreter.") : Tr("No executable memory: the slower interpreter runs."),
					Tr("Runs the DS's two CPUs on melonDS's recompiler, in the executable memory the PS5 gives (Diagnostics says whether it "
					"does), much faster than its interpreter. Off: the interpreter, slower, for a game the recompiler gets wrong. Applies to "
					"the next game.")),
				"dsJit");
			toggle("performance-hand", Tr("Performance overlay"), hand.performance, Tr("The frame rate and the emulation's speed, in the top left corner."),
				Tr("The frame rate and the emulation's speed, in the top left corner."));
		}
		else if (page == "screens")
		{
			own(choice("layout", Tr("Screen layout"), Options(kLayouts), hand.layout, Tr("How the two screens share the TV."),
					Tr("One above the other, the top one alone, the top one large with the bottom one beside it, or the two side by side. In a "
					"game, touchpad click + R1 goes to the next.")),
				"layout");
			if (IsDs() || Is3ds())
				own(choice("dsrotation", Tr("Screen rotation"), Options(kDsRotations), hand.dsRotation,
						Tr("For the games held on their side, like a book."),
						IsDs() ? Tr("Turns the DS's two screens together, a quarter turn at a time, for the games played with the DS held on its side, "
									"like a book (Hotel Dusk). The touchpad moves the cursor over the bottom screen as the TV shows it. The in-game menu "
									"changes it too.") :
								 Tr("Turns the 3DS's two screens together, a quarter turn at a time, for the games played with the 3DS held on its "
									"side, like a book. The touchpad moves the cursor over the bottom screen as the TV shows it. The in-game menu "
									"changes it too.")),
					"dsRotation");
			if (IsDs() || Is3ds())
				own(toggle("screengap", Tr("Screen gap"), hand.screenGap, Tr("The two screens held apart, as the hinge holds them."),
						Tr("A gap between the two screens wherever both show: one above the other, side by side, or turned like a book. "
						   "A game can have its own; the in-game menu's quick actions change it too.")),
					"screenGap");
			if (IsDs() || Is3ds())
				own(toggle("hiddentouch", Tr("Touch without the bottom screen"), hand.hiddenTouch,
						Tr("The touchpad touches it while only the top screen shows."),
						Tr("With the top screen alone on the TV, the touchpad still touches the bottom screen: its cursor moves over the top "
						   "screen as if the bottom one were there, for the games that ask for a touch now and then (Star Fox 64 3D). Off, "
						   "the touchpad touches only a bottom screen that is shown. A game can have its own; the in-game menu changes it too.")),
					"hiddenTouch");
			own(choice("border", Tr("Border"), Options(kBorderThemes), hand.border, Tr("Artwork around the screens."),
					Tr("Artwork around the screens, never over them. It follows every layout, and the in-game menu changes it too."),
					Kind::Swatches),
				"border");
			// the DS's touch, where its cursor lands and the right stick as the stylus (on System's page before
			// System took the sound)
			if (IsDs())
			{
				std::vector<std::string> touchOffsets, touchScales;
				for (int value = -20; value <= 20; value++) touchOffsets.push_back(fmt::format("{}%", value));
				for (int value = 50; value <= 150; value++) touchScales.push_back(fmt::format("{}%", value));
				const auto touchHelp = Tr("Moves and scales where the touchpad's cursor lands on the DS's screens, for a game where it is off: 0% and 100% leave it as it is. The remote touch screen is not changed. Applies to the next game.");
				std::string touch;
				if (hand.dsTouchX != 0 || hand.dsTouchY != 0 || hand.dsTouchScale != 100)
					touch = fmt::format("{}% · {}% · {}%", hand.dsTouchX, hand.dsTouchY, hand.dsTouchScale);
				if (hand.dsStylus)
					// tr: Settings > Screens' Touch row, its value: the right stick is the stylus
					touch += (touch.empty() ? "" : " · ") + std::string(Tr("Stick as stylus"));
				// tr: Settings > Screens: a row that opens the touch screen's settings (where the touchpad's cursor lands, the right stick as the stylus)
				sub("touch", Tr("Touch"), touch.empty() ? std::string(Tr("Default")) : touch,
					Tr("Where the touchpad's cursor lands, and the right stick as the stylus."),
					Tr("Moves and scales where the touchpad's cursor lands on the DS's screens, for a game where it is off, and lets the right "
					   "stick drag a touch on the bottom screen. Triangle on each says more."));
				own(choice("dstouchx", Tr("Touch, left or right"), touchOffsets, hand.dsTouchX + 20, touchHelp, touchHelp), "dsTouchX");
				own(choice("dstouchy", Tr("Touch, up or down"), touchOffsets, hand.dsTouchY + 20, touchHelp, touchHelp), "dsTouchY");
				own(choice("dstouchscale", Tr("Touch, scale"), touchScales, hand.dsTouchScale - 50, touchHelp, touchHelp), "dsTouchScale");
				// tr: a DS setting: the right stick as the touch screen's stylus (DraStic has it), for Metroid Prime Hunters' aiming
				own(toggle("dsstylus", Tr("The right stick as the stylus"), hand.dsStylus, Tr("Pushed, it drags a touch on the bottom screen."),
					Tr("Push the right stick and the bottom screen is touched in its middle, the touch dragged as you push and lifted when you let "
					   "go: Metroid Prime Hunters aims that way. The stick's directions do nothing else then. Applies to the next game.")),
					"dsStylus");
				endSub();
			}
		}
		else if (page == "controls")
		{
			toggle("rumble", Tr("Vibration"), settings.rumble, Tr("The DualSense's rumble, in games and in the launcher."),
				Tr("Off, the DualSense never rumbles, in games or in the launcher. The same on every side."));
			if (IsSwitch())
				slider("nx-deadzone", Tr("Stick deadzone"), hand.nxDeadzone, 30, Tr("How far a stick moves before it counts."),
					Tr("How far a stick moves before the game sees it, for both sticks. Raise it if a character or the camera drifts when you "
					   "let go of the stick; lower it for finer control. Also in the in-game menu."))
					->value = Percent(hand.nxDeadzone);
			// the turbo button is the app's own, which Eden's program does not have
			auto turbo = [&] {
				int index = 0;
				auto options = TurboOptions(IsWiiU() ? settings.turbo : hand.turbo, index);
				choice("turbo", Tr("Turbo button"), std::move(options), index, Tr("Hold it and it presses again and again by itself."),
					Tr("Choose a DualSense button: while you hold it in a game, it presses and lets go by itself about fifteen times a second, "
					   "for games that make you mash. The button keeps whatever it is mapped to. None turns it off. Every player on this side "
					   "has it."));
			};
			// a Switch game's buttons, as Eden maps them (port/eden/eden.h)
			if (IsSwitch())
			{
				const std::string set = TrP((int)ps5eden::ListMappings(hand).size(), "{0} button set", "{0} buttons set");
				// tr: Settings > Controls: a row that opens the buttons' settings (each button, the defaults)
				sub("buttons", Tr("Buttons"), set, Tr("Each button, and the defaults."),
					Tr("Which DualSense button presses each of the Switch's buttons, and the buttons and deadzone back as they were."));
				// tr: Settings > Controls > Buttons: the row that opens the list of every button and the DualSense button on it
				action(Kind::Link, "buttons", Tr("Each button"), set, Tr("Which DualSense button presses each button."),
					Tr("Which DualSense button presses each of the Switch's buttons. A is on Circle and B on Cross by default, where the Switch has "
					   "them; + is Options and - the touchpad's click. The D-pad and the sticks stay the Switch's own."));
				action(Kind::Hold, "resetcontrols", Tr("Reset to defaults"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Tr("Default buttons and deadzone."), Tr("Default buttons and deadzone."));
				endSub();
			}
			if (IsWiiU())
			{
				turbo();
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				{
					const auto controls = m_status.coreReady ? ps5emu::GetPlayerControls(player) : ps5emu::PlayerControls{};
					Row* row = action(Kind::Link, "player", TrF("Player {0}", player + 1), m_status.coreReady ? TypeName(controls.type) : "",
						controls.connected ? Tr("Cross: this player's controller, motion and buttons.") : Tr("No DualSense for this player yet."),
						Tr("What the game sees in this player's hands, and its motion, vibration, deadzones and buttons. Player 1 is the signed-in "
						"user who started the app; the other signed-in users' DualSenses are players 2 to 4."));
					row->index = player;
					row->dimmed = m_status.coreReady && !controls.connected;
					cemu(row);
				}
			}
			else if (!IsSwitch())
			{
				const bool aOnCircle = ps5azahar::MappedInput(hand, ps5azahar::Button::A) == ps5emu::PadInput::Circle;
				choice("abbuttons", Tr("A and B"), {Tr("A on Circle"), Tr("A on Cross")}, aOnCircle ? 0 : 1,
					Tr("Where A is: on Circle, as on the console, or on Cross."),
					Tr("A on Circle and B on Cross, where the console has them, or A on Cross and B on Circle, as PlayStation games confirm. X "
					   "and Y swap to match. Also in the in-game menu."),
					Kind::Segmented);
				if (Is3ds())
					toggle("motion", Tr("Motion controls"), hand.motion, Tr("The DualSense's motion as the 3DS's."),
						Tr("The DualSense's gyroscope and accelerometer as the 3DS's own, for the games that aim or steer by tilting it."));
				slider("deadzone", Tr("Stick deadzone"), hand.deadzone, 50, Tr("How far a stick moves before it counts."),
					Is3ds() ? Tr("How far a stick moves before the game sees it, for the circle pad and the C-stick. Raise it if something drifts "
							  "when you let go of the stick; lower it for finer control.") :
							  Tr("How far the left stick moves before it presses the DS's D-pad. Raise it if a direction is pressed when you let go "
							  "of the stick."))
					->value = Percent(hand.deadzone);
				int mapped = 0;
				if (Is3ds())
				{
					const auto mappings = ps5azahar::ListMappings(hand);
					mapped = (int)std::count_if(mappings.begin(), mappings.end(), [](const ps5emu::ButtonMapping& m) { return !m.input.empty(); });
				}
				else
					for (ps5azahar::Button button : ps5azahar::DsButtons())
						mapped += ps5azahar::MappedInput(hand, button) != ps5emu::PadInput::None;
				const std::string set = TrP(mapped, "{0} button set", "{0} buttons set");
				sub("buttons", Tr("Buttons"), set, Tr("Each button, the turbo button, and the defaults."),
					Tr("Which DualSense button presses each button, a button that presses again and again while you hold it, and the controls "
					   "back as they were."));
				action(Kind::Link, "buttons", Tr("Each button"), set, Tr("Which DualSense button presses each button."),
					Is3ds() ? Tr("Which DualSense button presses each of the 3DS's buttons. A is on Circle and B on Cross by default, where the 3DS has "
							  "them; the circle pad is the left stick, the C-stick the right one, and the touchpad the touch screen.") :
							  Tr("Which DualSense button presses each of the DS's buttons. A is on Circle and B on Cross by default, where the DS has "
							  "them; the left stick is a second D-pad, the touchpad the touch screen, and holding R3 blows into the microphone."));
				turbo();
				action(Kind::Hold, "resetcontrols", Tr("Reset to defaults"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Is3ds() ? Tr("Default buttons, motion and deadzone.") : Tr("Default buttons and deadzone."), Tr("The default buttons, motion and deadzone."));
				endSub();
			}
		}
		else if (page == "system" && Is3ds())
		{
			// Azahar looks for it in its own files, which only the 3DS side opens
			ps5emu::Game home;
			const bool homeMenu = m_prepared == System::N3ds && ps5azahar::HomeMenu(hand.region, home);
			own(choice("region", Tr("Region"), Options(kRegions), hand.region + 1, Tr("The emulated 3DS's region."),
					Tr("Automatic takes each game's own region. A game made for another region may refuse to start or show other languages. "
					"Applies to the next game.")),
				"region");
			own(choice("language", Tr("Language"), Options(kLanguages), hand.language + 1, Tr("The emulated 3DS's language."),
					Tr("The language games that follow the console's show their text in. Applies to the next game.")),
				"language");
			std::vector<std::string> clocks;
			for (int clock : kClocks)
				clocks.push_back(fmt::format("{}%", clock));
			own(choice("cpuclock", Tr("CPU clock"), clocks, ClockIndex(hand.cpuClock), Tr("The emulated 3DS's CPU speed."),
					Tr("How fast the emulated CPU runs, against a 3DS's: higher can steady a game that slows down where a 3DS would not, and asks "
					"more of the PS5; lower can speed up one held back by its own pacing. 100% is a 3DS. Also in the in-game menu.")),
				"cpuClock");
			Row* row = action(Kind::Action, "homemenu", Tr("Home Menu"), homeMenu ? Tr("Start") : Tr("Run Artic Setup first"),
				Tr("The 3DS Home Menu, from your console's files."),
				Tr("Starts the 3DS Home Menu, once Artic Base's setup has copied your own console's system files (Artic Base)."));
			row->dimmed = !homeMenu;
		}
		else if (page == "system" && IsSwitch())
		{
			// tr: the emulated Switch's console mode: in its dock, or handheld
			own(choice("nx-docked", Tr("Console mode"), {TrC("switch mode", "Docked"), TrC("switch mode", "Handheld")}, hand.nxDocked ? 0 : 1,
					Tr("Docked: the TV's picture. Handheld: the console's own screen's."),
					Tr("Docked, games draw for a TV, at their sharpest (1080p for most); handheld, for the console's own screen (720p), which "
					   "asks less of the GPU. A few games look or play differently in each. Applies to the next game."),
					Kind::Segmented),
				"nxDocked");
			std::vector<std::string> controllers;
			for (const char* name : kNxControllers)
				controllers.push_back(Tr(name));
			own(choice("nx-controller", Tr("Controller type"), controllers, hand.nxController + 1, Tr("What a game sees in your hands."),
					Tr("Automatic gives each game the controller it takes: a Pro Controller for most. A game that does not work with one may "
					   "with a Joy-Con pair; Handheld is player 1's alone, for games played on the console's own screen. Also in the in-game "
					   "menu.")),
				"nxController");
			// Eden's speed limit, as ProsperoEden's Settings > Performance has it
			std::vector<std::string> speeds;
			for (int percent : {100, 150, 200, 300})
				speeds.push_back(Percent(percent));
			speeds.push_back(TrC("speed limit", "None"));
			own(choice("nx-speedlimit", Tr("Speed limit"), speeds, std::clamp(hand.nxSpeedLimit, 0, 4), Tr("How fast the game may run."),
					Tr("As a share of the Switch's own speed. Above 100%, a game held to 30 fps can reach 60 where the PS5 has the time to spare, "
					   "and a game that keeps time by its frames plays faster. None runs as fast as the PS5 can. Applies to the next game.")),
				"nxSpeedLimit");
			// the system language games see, each named in its own language
			std::vector<std::string> languages{Tr("Automatic")};
			for (const auto& language : ps5eden::kLanguages)
				languages.push_back(language.name);
			own(choice("nx-language", Tr("Language"), languages, std::clamp(hand.nxLanguage + 1, 0, (int)languages.size() - 1),
					Tr("The language Switch games see. Automatic: the menus' own."),
					Tr("The language Switch games see. Automatic: the menus' own.")),
				"nxLanguage");
		}
		else if (page == "performance" && IsSwitch())
		{
			// ProsperoEden's Settings > Performance, each for every Switch game or one game's own
			own(choice("nx-memory", Tr("Memory"), {"4 GB", "6 GB", "8 GB"}, std::clamp(hand.nxMemory, 0, 2),
					Tr("4 GB is the Switch's own. More, for mods that ask for it. Applies to the next game."),
					Tr("4 GB is the Switch's own. More, for mods that ask for it. Applies to the next game."), Kind::Segmented),
				"nxMemory");
			const struct
			{
				const char* id;
				const char* label;
				const char* about;
				bool ps5settings::N3ds::* value;
				const char* field;
			} switches[] = {
				{"nx-compactmemory", TrMark("Compact memory"), TrMark("Keeps the game's mostly empty memory small, for the biggest games."),
					&ps5settings::N3ds::nxCompactMemory, "nxCompactMemory"},
				{"nx-blocklist", TrMark("Precompile code"), TrMark("Compiles the code earlier sessions ran before the game needs it: fewer hitches."),
					&ps5settings::N3ds::nxBlockList, "nxBlockList"},
				// the shortcuts, behind Accuracy
				{"nx-fastgpu", TrMark("Fast GPU"), TrMark("Eden's lowest GPU accuracy: faster, and some effects may be drawn wrong."),
					&ps5settings::N3ds::nxFastGpu, "nxFastGpu"},
				{"nx-unsafecpu", TrMark("Unsafe CPU"), TrMark("Quicker, inexact floating-point maths. Faster; a few games misbehave."),
					&ps5settings::N3ds::nxUnsafeCpu, "nxUnsafeCpu"},
				{"nx-unsafedma", TrMark("Unsafe DMA"), TrMark("Eden's unsafe DMA accuracy: faster, a few games show glitches."),
					&ps5settings::N3ds::nxUnsafeDma, "nxUnsafeDma"},
				{"nx-reactive", TrMark("Reactive flushing"), TrMark("Keeps effects that read back the GPU's work right. Off is faster; some break."),
					&ps5settings::N3ds::nxReactiveFlushing, "nxReactiveFlushing"},
				{"nx-skipinvalidation", TrMark("Skip cache invalidation"), TrMark("Fewer CPU cache invalidations: faster, rarely wrong."),
					&ps5settings::N3ds::nxSkipInvalidation, "nxSkipInvalidation"},
			};
			constexpr int kShortcuts = 2; // switches' first behind Accuracy
			own(toggle(switches[0].id, Tr(switches[0].label), hand.*(switches[0].value), Tr(switches[0].about), Tr(switches[0].about)), switches[0].field);
			own(toggle("nx-async", Tr("Async shader compile"), hand.nxAsyncShaders, Tr("No stutter while new shaders build."),
					Tr("On, a new shader is built while the game carries on, so it does not stutter, but some things may be missing for a moment. "
					"Off, the game waits for each one: stutter, but nothing drawn wrong.")),
				"nxAsyncShaders");
			own(toggle(switches[1].id, Tr(switches[1].label), hand.*(switches[1].value), Tr(switches[1].about), Tr(switches[1].about)), switches[1].field);
			// how many shortcuts differ from Eden's own choice (all off, Reactive flushing on)
			const ps5settings::N3ds defaults;
			int changed = 0;
			for (int i = kShortcuts; i < (int)std::size(switches); i++)
				changed += hand.*(switches[i].value) != defaults.*(switches[i].value);
			// tr: Settings > Performance: a row that opens Eden's accuracy shortcuts (Fast GPU, Unsafe CPU, ...); its value: how many are changed
			sub("accuracy", Tr("Accuracy"), changed ? TrP(changed, "{0} changed", "{0} changed") : std::string(Tr("Default")),
				Tr("Eden's shortcuts: faster, some less exact."),
				Tr("Fast GPU, Unsafe CPU, Unsafe DMA, Reactive flushing and Skip cache invalidation trade exactness for speed, for every Switch "
				   "game or one game's own. Triangle on each says more."));
			for (int i = kShortcuts; i < (int)std::size(switches); i++)
				own(toggle(switches[i].id, Tr(switches[i].label), hand.*(switches[i].value), Tr(switches[i].about), Tr(switches[i].about)), switches[i].field);
			endSub();
		}
		else if (page == "system" && IsDs())
		{
			own(choice("language", Tr("Language"), Options(kLanguages), hand.language + 1, Tr("The emulated DS's language."),
					Tr("The DS's own language, which many games follow. The DS has Japanese, English, French, German, Italian, Spanish and "
					"Chinese; any other language uses English. Applies to the next game.")),
				"language");
			const bool found = ps5melonds::OwnBiosFound();
			own(toggle("dsownbios", Tr("Your DS's BIOS"), hand.dsOwnBios, found ? Tr("Found in melonds/bios.") : Tr("Not found: melonDS's built-in BIOS runs the games."),
					// tr: {0} is the folder for a DS's own BIOS and firmware dumps
					TrF("bios7.bin, bios9.bin and firmware.bin, dumped from your own DS, in {0}: melonDS runs on them when they are there "
						"and this is on. Without them, its own replacements (FreeBIOS) run most games as well. Applies to the next game.",
						ps5melonds::Root() + "/bios")),
				"dsOwnBios");
			own(choice("dsmic", Tr("Microphone"), {Tr("Noise"), Tr("Silence"), Tr("A recording")}, hand.dsMicSource,
				Tr("What the DS hears while R3 is held. Applies to the next game."),
				// tr: {0} is melonDS's folder, /data/melonds or where the DS's files are; keep <the game's file name> as a placeholder
				TrF("A recording is a WAV file, 16-bit mono PCM at 8,000 to 48,000 Hz and up to 16 MB: {0}/microphone/<the game's file name>.wav for one game, {0}/microphone.wav for the others. It plays over and over while R3 is held, from its start each time. Nothing is recorded from a microphone.", ps5melonds::Root())), "dsMicSource");
			own(toggle("dsslot2", Tr("GBA cartridge in Slot-2"), hand.dsSlot2,
				Tr("For the DS games that read a GBA game in their second slot, as Pokémon's Pal Park does. It does not play GBA games."),
				// tr: {0} is melonDS's folder, /data/melonds or where the DS's files are
				TrF("Put the GBA game in {0}/slot2 with the DS game's file name, ending .gba, and its .sav beside it if it has one. The cartridge's save is then kept in {0}/slot2-saves, and the files you put there are never changed. A DS game without a cartridge there starts with the slot empty. Applies to the next game.", ps5melonds::Root())), "dsSlot2");
		}
		else if (page == "files")
		{
			const std::string& folder = GamesFolder(m_side);
			action(Kind::Link, "folder", Is3ds() ? Tr("3DS games") : IsDs() ? Tr("DS games") : IsSwitch() ? Tr("Switch games") : Tr("Wii U games"),
				ShortPath(IsSwitch() ? ps5eden::ScanFolder(folder) : folder, 34),
				Tr("Where your games are. Cross picks another."),
				// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
				IsWiiU() ? TrF("Your Wii U games: .wua, .wud, .wux, or folders with code, content and meta, alone or in .zip, .7z or .rar archives. "
							"Encrypted .wud and .wux need their keys in {0}/keys.txt.", ps5paths::CemuRoot()) :
				Is3ds()	 ? Tr("Your 3DS games: .3ds or .cci, .cxi, .3dsx and Azahar's compressed dumps, decrypted, alone or in .zip, .7z or .rar "
						   "archives, here or in the folders in it. CIA files are installed (Install a CIA file). Encrypted dumps do not start, "
						   "with keys or without: decrypt them on your 3DS first (GodMode9).") :
				IsSwitch() ? Tr("Your Switch games: .nsp and .xci files, alone or each in a folder of its own with its update and DLC.") :
						   Tr("Your DS games: .nds files, alone or in .zip, .7z or .rar archives, here or in the folders in it."));
			action(Kind::Action, "rescan", Tr("Look for games now"), m_scanning ? Tr("Looking…") : "", Tr("This side's folder, again."),
				Tr("Looks through this side's folder again: games added since appear, and games taken away go."));
			// #75: a large library on a share is a long look at every start
			if (!ps5netfs::shares().empty())
				toggle("lookonshares", Tr("Look on network shares at every start"), settings.lookOnShares,
					Tr("Off: the games found last time, at once."),
					Tr("A games folder on a network share is looked through each time its side opens, and the app starts again after "
					   "every game: with a large library that is a long wait. Off, the side shows the games it found there last time at "
					   "once; Look for games now looks again when you have added some. Folders on the PS5 or a USB drive are looked "
					   "through every time either way."));
			if (IsSwitch())
			{
				// Eden's own files: the keys and firmware it needs
				const std::string files = ps5eden::GameFilesFolder();
				// the two in one row: what is missing first, Cross saying where each goes
				const bool keys = ps5eden::KeysFound(), firmware = ps5eden::FirmwareFound();
				// tr: Settings > Game files' Keys and firmware row, its value when the firmware is missing and the keys are there
				const std::string found = keys && firmware ? std::string(Tr("Found")) : keys ? std::string(Tr("Firmware missing")) :
					// tr: Settings > Game files' Keys and firmware row, its value when prod.keys is missing and the firmware is there
					firmware ? std::string(Tr("Keys missing")) : std::string(Tr("Missing"));
				// tr: Settings > Game files: one row for the Switch's keys (prod.keys) and its firmware, which Eden needs
				action(Kind::Action, "nxkeys", Tr("Keys and firmware"), found,
					!keys			? Tr("prod.keys is missing: Eden cannot read Switch games without it") :
						!firmware	? Tr("No firmware yet: many games need it to start") :
									  Tr("Eden can read your games."),
					TrF("Eden reads Switch games with your own console's keys: prod.keys (and title.keys), dumped from your Switch, in "
						"{0}/keys. No keys come with PS5-ECAM.",
						files) +
						"\n\n" +
						TrF("Many Switch games need the console's own system files: its firmware's .nca files, dumped from your Switch, "
							"in {0}/firmware. No firmware comes with PS5-ECAM.",
							files));
			}
			else
			{
				// network shares (6.14): a folder on one can be this side's
				const size_t shares = ps5netfs::shares().size();
				action(Kind::Link, "shares", Tr("Network shares"), shares ? TrP((long long)shares, "{0} share", "{0} shares") : std::string(),
					Tr("Games on a computer or a NAS, over SMB or NFS."),
					Tr("A folder shared by a Windows, macOS or Linux computer or a NAS, read where it is: add it here, then choose a games folder "
					   "on it with the folder browser, where each share is a tab. Nothing is ever written to it; saves and installs stay on "
					   "the PS5. A cable does better than Wi-Fi for Wii U games."));
			}
			if (IsWiiU())
			{
				const bool keys = IsFile(ps5paths::DiscKeys());
				action(Kind::Action, "keys", Tr("Wii U disc keys"), keys ? std::string(Tr("keys.txt found")) : std::string(),
					keys ? Tr("Only encrypted .wud and .wux need them.") : Tr("keys.txt is missing: only encrypted .wud and .wux need it"),
					// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
					TrF("Encrypted .wud and .wux dumps need their disc keys in {0}/keys.txt, one a line. Decrypted dumps, .wua and "
						"folders need none.", ps5paths::CemuRoot()));
				// tr: Settings > Game files: a row that opens Updates and DLC and Mods from USB
				sub("installs", Tr("Updates, DLC and mods"), "", Tr("Installs from your drives, and Cemu mods."),
					Tr("Updates and DLC found on your drives, each installed into the Wii U's storage with one press, and Cemu mods copied from a "
					   "USB drive's ps5cemu/mods folder. Triangle on each says more."));
				cemu(action(Kind::Link, "install", Tr("Updates and DLC"), "", Tr("Found on your drives, each installed with one press."),
					Tr("Looks on every USB drive and extended storage plugged in, in the games folder and in /data for updates, DLC and games "
					   "(folders with code, content and meta), and lists them by game with what is installed now. Cross installs one, Square "
					   "every new one; each goes into the Wii U's storage (mlc01), as Cemu's Install game title, update or DLC does. Triangle "
					   "there chooses a folder anywhere else.")));
				cemu(action(Kind::Action, "mods", Tr("Mods from USB"), ModsStatus(), Tr("Cemu mods from the ps5cemu/mods folder on your drives."),
					Tr("Copies the mods in a USB drive's ps5cemu/mods folder into the app's graphic packs, where each shows on its game's "
					   "Graphic packs page, off until you turn it on. A mod is a Cemu graphic pack (a folder with a rules.txt), or, for one "
					   "shared without it, a folder with content and aoc folders, in a folder named by its game's title ID. A game's menu "
					   "has Add mods from USB as well, which gives such mods that game. The guide on GitHub, docs/GUIDE.md, has more.")));
				endSub();
			}
			else if (Is3ds())
				action(Kind::Link, "installcia", Tr("Install a CIA file"), "", Tr("A game, an update or DLC."),
					Tr("Installs a CIA into the 3DS's storage, as Azahar's Install CIA does: an update or DLC goes with its game, and a game joins "
					"the library. Circle cancels while it runs."));
			// an install from before: a folder for each system (app/reorganize.h), and back as before, which
			// Go back to an earlier version needs
			if (!m_layout.separate.empty() || !m_layout.separated.empty())
			{
				const bool back = m_layout.separate.empty();
				const bool blocked = !m_layout.blocked.empty();
				Row* row = action(Kind::Hold, back ? "putback" : "separate", Tr("A folder for each system"),
					blocked ? std::string(Tr("Cannot move")) : m_message.empty() ? std::string(Tr("Hold Cross")) : m_message,
					// tr: {0} is a file or folder already where one would be moved to
					blocked ? TrF("In the way: {0}", m_layout.blocked) :
					back	? Tr("Each system's files are in a folder of their own.") :
							  Tr("Your files from before into a folder for each system."),
					back ? TrF("Puts each system's files back into {0}, where they were before. Going back to an earlier version needs them "
							   "there, as it looks for them there. PS5-ECAM starts again to do it.",
							   ps5paths::Root()) :
						   // tr: {0} is the app's data folder, /data/ps5cemu; {1} lists the four folders: /data/cemu, /data/azahar, ...
						   TrF("Moves each system's files from {0} into a folder of its own: {1}. They are renamed on the same drive, never "
							   "copied, so it takes a moment whatever their size, and nothing is deleted. PS5-ECAM starts again to do it; a "
							   "system that cannot move stays as it was. This row puts them back.",
							   ps5paths::Root(), SystemFolders()));
				row->dimmed = blocked;
			}
		}
		else if (page == "saves")
		{
			if (IsWiiU())
			{
				const auto accounts = m_status.coreReady ? ps5emu::ListAccounts() : std::vector<ps5emu::WiiuAccount>{};
				const uint32_t current = m_status.coreReady ? ps5emu::CurrentAccount() : 0;
				std::vector<std::string> names;
				int index = 0;
				std::string currentName;
				for (int i = 0; i < (int)accounts.size(); i++)
				{
					names.push_back(accounts[i].name);
					if (accounts[i].id == current)
					{
						index = i;
						currentName = accounts[i].name;
					}
				}
				if (names.empty())
					names.push_back("-");
				Row* account = cemu(choice("account", Tr("Account games start on"), names, index,
					settings.accountPerUser ? Tr("Chosen by your PS5 user: One account for each PS5 user is on.") :
											  Tr("The next game starts on these saves. A game already running keeps its own."),
					Tr("Each Wii U account has its own saves of every game, as on a Wii U. The next game you start uses the one shown here; a game "
					   "already running keeps the one it started with.")));
				if (settings.accountPerUser)
					account->dimmed = true;
				cemu(toggle("accountperuser", Tr("One account for each PS5 user"), settings.accountPerUser,
					Tr("Whoever starts PS5-ECAM plays on their own saves."),
					Tr("The PS5 user who starts the app plays on a Wii U account of their own, made the first time with their PS5 name, so "
					   "brothers and sisters each keep their own saves just by starting the app from their own PS5 user.")));
				// tr: Settings > Accounts and saves: a row that opens New account, Rename and Delete an account
				sub("accounts", Tr("Manage accounts"), TrF("{0} of 12", (int)accounts.size()), Tr("New, renamed or deleted."),
					Tr("A new account, a new name for the one games start on, or another account deleted. Saves always stay in the Wii U's "
					   "storage."));
				// tr: how many Wii U accounts there are, of the twelve a Wii U has at most
				cemu(action(Kind::Link, "newaccount", Tr("New account"), TrF("{0} of 12", (int)accounts.size()),
					Tr("A new account starts with no saves."), Tr("Name it with the keyboard, ten letters at most. Games start on it from then on.")));
				cemu(action(Kind::Link, "renameaccount", TrF("Rename {0}", currentName), "", Tr("The account games start on."),
					Tr("The name games show for this account, ten letters at most. Its saves stay as they are.")));
				// deleting: Left and Right choose which, never the one games start on
				std::vector<std::string> others;
				for (const auto& a : accounts)
					if (a.id != current)
						others.push_back(a.name);
				int& which = m_holdChoice["deleteaccount"];
				which = others.empty() ? 0 : std::clamp(which, 0, (int)others.size() - 1);
				Row* remove = cemu(action(Kind::Hold, "deleteaccount", Tr("Delete an account"),
					others.empty() ? std::string(Tr("Only one")) : others[which] + "  ·  " + Tr("Hold Cross"),
					others.empty() ? Tr("There is only one account, and games start on it.") : Tr("Left and Right choose which. Its saves stay in the Wii U's storage."),
					Tr("The account is removed; its saves stay in the Wii U's storage (and in any backup), so nothing is lost. The account "
					   "games start on cannot be deleted: choose another first.")));
				remove->options = others;
				remove->index = which;
				if (others.empty())
					remove->dimmed = true;
				endSub();
			}
			// every side's saves, to a USB drive and back
			if (m_now - m_backupsAt > 3)
			{
				m_backups = ps5saves::FindBackups();
				m_backupsAt = m_now;
			}
			const ps5saves::Status saves = ps5saves::Current();
			const bool running = saves.state == ps5saves::Status::State::Running;
			if (saves.finished != m_savesFinished)
			{
				// a backup or restore has ended: said once, and what it changed read again
				m_savesFinished = saves.finished;
				m_backupsAt = -100;
				if (saves.state == ps5saves::Status::State::Done)
				{
					// tr: a backup or restore of the saves, finished ({0}: how many files)
					m_savesMessage = TrP(saves.files, "Done: {0} file", "Done: {0} files");
					if (saves.restoring && m_status.coreReady)
						ps5emu::ReloadAccounts();
					// the settings came back too: the launcher holds the old ones, so the app starts again on
					// the restored ones, before anything saves the old over them
					if (saves.restoring && saves.settingsRestored)
					{
						ps5log::Line("[saves] the settings were restored: starting over to use them");
						ps5emu::RestartToLibrary();
					}
				}
				else
					m_savesMessage = saves.message;
			}
			std::string backupValue = m_savesMessage;
			if (running && !saves.restoring)
				// tr: a backup of the saves, as it copies ({0}: a percentage)
				backupValue = TrF("Copying: {0}", Percent(saves.total ? (int)(saves.copied * 100 / saves.total) : 0));
			else if (backupValue.empty() && !m_backups.empty())
				// tr: the newest backup of the saves: when, and on which drive
				backupValue = TrF("Last: {0} · {1}", m_backups.front().when, DriveName(m_backups.front().drive));
			action(Kind::Action, "backup", Tr("Back up to USB"), backupValue, Tr("Every side's saves, save states and Wii U accounts."),
				Tr("Copies the Wii U's saves (every account's, with the accounts), the 3DS's saves, extra data and save states, the DS's saves "
				   "and save states, and the Switch's saves with its user profiles to PS5CEMU-HAR-saves on the first USB drive plugged in, in a "
				   "folder named by the date and time. Never the games, so a backup stays small. The settings go with them: the launcher's, each "
				   "game's own and the controllers'."));
			std::vector<std::string> backups;
			for (const auto& backup : m_backups)
				backups.push_back(backup.when + " · " + DriveName(backup.drive));
			int& chosen = m_holdChoice["restore"];
			chosen = backups.empty() ? 0 : std::clamp(chosen, 0, (int)backups.size() - 1);
			std::string restoreValue = backups.empty() ? std::string(Tr("No backups found")) : backups[chosen] + "  ·  " + Tr("Hold Cross");
			if (running && saves.restoring)
				restoreValue = TrF("Copying: {0}", Percent(saves.total ? (int)(saves.copied * 100 / saves.total) : 0));
			Row* restore = action(Kind::Hold, "restore", Tr("Restore from USB"), restoreValue,
				backups.empty() ? Tr("Back up first, or plug in the drive with your backups.") : Tr("Left and Right choose which backup, the newest first."),
				// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
				TrF("Puts the chosen backup's saves back where they came from. The saves there now are moved first to "
					"{0}/saves-before, so nothing is lost; saves the backup does not have stay as they are. Its settings come back too, and "
					"PS5-ECAM starts again to use them.",
					ps5paths::Root()));
			restore->options = backups;
			restore->index = chosen;
			if (backups.empty())
				restore->dimmed = true;
		}
		else if (page == "online")
		{
			// downloading the covers now: the missing ones, or every one again
			int missing = 0;
			for (const Game& game : m_games)
				missing += !game.entry.game.gameId.empty() && game.boxArt.empty();
			const size_t pending = ps5boxart::Pending();
			const std::string status = pending ? TrP((long long)pending, "Downloading: {0} left", "Downloading: {0} left") :
				ps5boxart::Answered() == 0 ? std::string(Tr("GameTDB did not answer: is the PS5 online?")) :
				missing				   ? TrP(missing, "{0} game without box art", "{0} games without box art") :
										 std::string(Tr("Every game has box art"));
			// tr: Settings > Online: a row that opens the box art's settings (GameTDB on or off, downloading the covers again)
			sub("boxart", Tr("Box art"), settings.boxArt || pending ? status : std::string(Tr("Off")), Tr("Covers for the library, from GameTDB."),
				Tr("Whether covers are downloaded from GameTDB (art.gametdb.com), and the missing ones or every one downloaded again. On the "
				   "Switch side, also whether a game shows its box art or its own icon."));
			// (a Switch game's ID is the game GameTDB has by its name: port/eden/library.h)
			toggle("boxart", Tr("Box art from GameTDB"), settings.boxArt, Tr("Covers for the library, downloaded once."),
				Tr("The first time a game shows up, its cover is downloaded from GameTDB (art.gametdb.com) by the ID on its box. Off: nothing "
				"more is downloaded."));
			// the Switch side's covers: GameTDB's box art, or each game's own icon, as the Switch's home menu has them
			if (IsSwitch())
				choice("nx-covers", Tr("Switch covers"), {Tr("Box art"), Tr("Game icons")}, hand.nxCovers, Tr("GameTDB's box art, or each game's own icon."),
					Tr("Box art is GameTDB's, found by each game's name. Game icons are the games' own, read from their files, as the Switch's "
					   "home menu shows them. A picture beside a game (Game.png or Game.jpg) is its cover either way."));
			action(Kind::Action, "boxartmissing", Tr("Download missing box art"), status, Tr("Tries again for every game without a cover."),
				Tr("Asks GameTDB again for the cover of every game on this side that has none, including games it had no cover for "
				   "before. Covers are downloaded in the background while you use the launcher."));
			action(Kind::Hold, "boxartall", Tr("Download all box art again"), m_message.empty() ? Tr("Hold Cross") : m_message,
				Tr("Replaces every cover on this side with a fresh download."),
				Tr("Downloads the cover of every game on this side again, replacing the ones you have. Useful when a cover is wrong or "
				   "looks broken."));
			endSub();
			// Artic Base's page (pages.cpp): a game played from a 3DS on the network
			if (Is3ds())
				action(Kind::Link, "artic", Tr("Artic Base"), settings.n3ds.articAddress, Tr("Play a game from your 3DS, and set up from it."),
					Tr("Artic Base plays a game from a 3DS on your network, its saves staying on the 3DS; Artic Setup copies a 3DS's system files "
					"into Azahar, for the Home Menu and the games that need them."));
			// ProsperoEden's rooms (port/eden/rooms.h), behind a row; the public list downloaded once, as their page
			// first shows, and again by Cross on its row
			if (IsSwitch())
			{
				const bool joined = !hand.nxRoomHost.empty();
				const std::string about = Tr("A room is a server where Switch games find each other for their local wireless play, as in desktop "
											 "Eden and ProsperoEden. The room chosen here is joined as each Switch game starts: then choose Local "
											 "Wireless (or local play) in the game. A room that cannot be joined brings you back here with why.");
				// tr: Settings > Online on the Switch side: a row that opens the rooms for the games' local wireless play
				sub("rooms", Tr("Multiplayer"), joined ? hand.nxRoomName : std::string(Tr("None: games play offline")),
					Tr("Rooms for the games' local wireless play, online."), about);
				if (m_subPage == "rooms" && !ps5rooms::Asked())
					ps5rooms::Refresh();
				const ps5rooms::Status rooms = ps5rooms::Get();
				Row* room = action(Kind::Action, "nx-room", Tr("Room"), joined ? hand.nxRoomName : std::string(Tr("None: games play offline")),
					joined ? Tr("Joined as a Switch game starts. Cross plays offline again.") : Tr("Choose a public room below, or a private one."),
					about);
				room->dimmed = !joined;
				action(Kind::Action, "nx-nickname", Tr("Nickname"), ps5eden::Nickname(hand), Tr("Your name in the room."),
					Tr("What the others in a room see of you: 4 to 20 letters, digits, spaces, dots, dashes or underscores, as Eden takes them."));
				const bool privateRoom = joined && hand.nxRoomName == hand.nxRoomHost;
				action(Kind::Action, "nx-private", Tr("Private room"),
					privateRoom ? fmt::format("{}:{}", hand.nxRoomHost, hand.nxRoomPort) : std::string(Tr("Cross to enter one")),
					Tr("A room by its address, port and password."),
					Tr("A room someone gave you the address of: its address (a name or an IPv4 address), its UDP port (24872 unless they "
					   "say otherwise) and its password, if it has one."));
				const std::string listed = rooms.busy ? std::string(Tr("Downloading…")) :
					!rooms.error.empty()			   ? rooms.error :
					rooms.rooms.empty()				   ? std::string(Tr("No public rooms right now")) :
														 TrP((long long)rooms.rooms.size(), "{0} room", "{0} rooms");
				action(Kind::Action, "nx-refresh", Tr("Public rooms"), listed, Tr("Cross downloads the list again."),
					Tr("ProsperoEden's list of public rooms: its own featured rooms first, then the others, the fullest first. A room for "
					   "another version of Eden cannot be joined.") + std::string("\n\n") + about);
				// the rooms themselves, ProsperoEden's own first (the most a page lists)
				for (size_t i = 0; i < rooms.rooms.size() && i < 80; i++)
				{
					const ps5rooms::Room& r = rooms.rooms[i];
					std::string value = fmt::format("{}/{}", r.players, r.capacity);
					if (r.password)
						value += std::string(" · ") + Tr("Password");
					if (r.featured)
						// tr: one of ProsperoEden's own rooms, which its list puts first
						value = std::string(Tr("Featured")) + " · " + value;
					if (!r.compatible)
						value = Tr("Another version");
					const bool chosen = joined && hand.nxRoomHost == r.address && hand.nxRoomPort == r.port;
					Row* row = action(Kind::Action, "nx-public", chosen ? TrF("{0} (chosen)", r.name) : r.name, value,
						r.game.empty() ? std::string(Tr("Any game")) : r.game, about);
					row->index = (int)i;
					row->dimmed = !r.compatible;
				}
				endSub();
			}
			if (IsWiiU())
				action(Kind::Action, "packs", Tr("Community graphic packs"), PacksStatus(), Tr("Cross checks GitHub for newer ones."),
					Tr("Cemu's community graphic packs, from GitHub's latest release when it is newer than the ones installed. They are not "
					   "in the app: the first time the Wii U side starts without them, they are downloaded in the background."));
			if (IsDs())
			{
				// RetroAchievements (6.16): only the DS has sets
				const ps5cheevos::Account account = ps5cheevos::Get();
				const char* about = Tr("Earn RetroAchievements in DS games with your account, as RetroArch does. Cross signs in with your user name "
									   "and password; only the sign-in's token is kept on the PS5, never the password. Softcore: save states, "
									   "cheats and speed stay as they are. RetroAchievements has no sets for the Wii U or the 3DS.");
				if (account.signedIn)
					// tr: the RetroAchievements account signed in: its name ({1}) and softcore points ({0})
					action(Kind::Hold, "cheevos", Tr("RetroAchievements"),
						TrP((long long)account.points, "{1} · {0} point", "{1} · {0} points", account.user) + "  ·  " + Tr("Hold Cross to sign out"),
						Tr("Signed in: DS games with a set earn achievements."), about);
				else
					action(Kind::Action, "cheevos", Tr("RetroAchievements"),
						account.busy ? std::string(Tr("Signing in…")) : !account.message.empty() ? account.message : std::string(Tr("Cross to sign in")),
						Tr("Achievements in DS games, with your RetroAchievements account."), about);
				Row* notices = toggle("cheevosnotices", Tr("Unlock notices"), account.notices, Tr("Each unlock as the PS5's notification."),
					Tr("An achievement earned, and the count as a game starts, shown as the PS5's own notifications. Off: they are only in the "
					   "in-game menu's Achievements."));
				if (!account.signedIn)
					notices->dimmed = true;
			}
			action(Kind::Action, "appupdate", Tr("PS5-ECAM updates"), AppUpdateStatus(), Tr("Cross checks GitHub for a newer version."),
				Tr("PS5-ECAM asks GitHub for its latest release each time it starts. Cross asks again, or installs the newer version it found: "
				   "the release's files are downloaded, checked and put in place of these, and the app starts again. Your games, saves and "
				   "settings stay as they are."));
			// the release the last update replaced, kept until the next (ps5update::PreviousVersion)
			if (const std::string previous = ps5update::PreviousVersion(); !previous.empty())
			{
				// tr: {0} is the version the last update replaced, as in 5.0.0
				Row* goback = action(Kind::Hold, "goback", TrF("Go back to {0}", ps5update::Readable(previous)), m_message.empty() ? Tr("Hold Cross") : m_message,
					Tr("The version before the last update, kept as it was."),
					Tr("The last update kept the version it replaced. Holding Cross puts its files back in place of these, and PS5-ECAM starts "
					   "again as that version. Your games, saves and settings stay as they are. The newer version is not offered at start "
					   "again; this page still finds it."));
				// a version from before a folder for each system looks for the files where they were (app/reorganize.h)
				if (!m_layout.separated.empty())
				{
					goback->dimmed = true;
					goback->description = Tr("Put each system's files back first: Settings > Game files > A folder for each system.");
				}
			}
		}
		else if (page == "launcher")
		{
			{
				// the themes: the built-in ones, then your own (themes/*.json)
				std::vector<std::string> names;
				int index = 0;
				for (int i = 0; i < (int)m_themes.size(); i++)
				{
					const ui::Theme& theme = m_themes[i];
					names.push_back(theme.id.rfind("user:", 0) == 0 ? theme.name : std::string(Tr(theme.name.c_str())));
					if (theme.id == ui::CurrentTheme().id) // (an id from before 5.1 is the one it now names)
						index = i;
				}
				const ui::Theme& current = m_themes.empty() ? ui::BuiltInThemes().front() : m_themes[std::min(index, (int)m_themes.size() - 1)];
				choice("theme", Tr("Theme"), names, index,
					current.id.rfind("user:", 0) == 0 ? current.description : std::string(Tr(current.description.c_str())),
					TrF("How the launcher looks: its colours, its corners and what moves behind the menus. Every theme has the same screens and "
						"buttons. Make your own as a JSON file in {0}/themes, starting from any of these: the text under this page says how. "
						"Only the launcher changes: the in-game menus and the games stay as they are.",
						ps5paths::Root()));
			}
			// the sides the bar offers: any mix, at least one, in its order
			std::string shown;
			for (System side : kBarOrder)
				if (SideShown(side))
					shown += (shown.empty() ? "" : " · ") + std::string(SideTitle(side));
			// tr: Settings > Launcher: a row that opens which sides the bar shows (Wii U, Switch, DS, 3DS)
			sub("sides", Tr("Sides"), shown, Tr("Which systems the bar shows."),
				Tr("The sides the bar offers, any mix of the four. A hidden side keeps its games and settings, and they come back when it is "
				   "turned on again. At least one side stays on."));
			for (int index = 4; index < kSides; index++)
			{
				const auto extra = (System)index;
				toggle((std::string("side-") + ps5launcher::SideName(extra)).c_str(), SideTitle(extra), SideShown(extra), Tr("Show this emulator in the system bar."), Tr("The runtime must be included when packaging ECAM."));
			}
			toggle("side-wiiu", Tr("Wii U side"), SideShown(System::WiiU), Tr("Cemu's games, in the bar."),
				Tr("Whether the bar shows the Wii U side (Cemu). Hidden, its games and settings are kept and come back when it is turned on "
				   "again. At least one side stays on."));
			// in the bar's order: Wii U, Switch, DS, 3DS
			toggle("side-switch", Tr("Switch side"), SideShown(System::Switch), Tr("Eden's games, in the bar."),
				Tr("Whether the bar shows the Switch side (Eden). Hidden, its games and settings are kept and come back when it is turned on "
				   "again. At least one side stays on."));
			toggle("side-ds", Tr("DS side"), SideShown(System::Nds), Tr("melonDS's games, in the bar."),
				Tr("Whether the bar shows the DS side (melonDS). Hidden, its games and settings are kept and come back when it is turned on "
				   "again. At least one side stays on."));
			toggle("side-3ds", Tr("3DS side"), SideShown(System::N3ds), Tr("Azahar's games, in the bar."),
				Tr("Whether the bar shows the 3DS side (Azahar). Hidden, its games and settings are kept and come back when it is turned on "
				   "again. At least one side stays on."));
			endSub();
			toggle("pictures", Tr("Game pictures behind menus"), settings.ui.gamePictures, Tr("The focused game's cover, softened, behind the screens."),
				Tr("The focused game's picture fills the screen behind the menus, softened and dimmed, so moving the focus changes the whole "
				"screen. Off: the game's colours only."));
			toggle("filters", Tr("Library filters"), settings.ui.libraryFilters, Tr("All, Recently added, Favourites: a row over the list."),
				Tr("A row of filters over the Library's list: All, Recently added (the last 30 days), Favourites and, on the Wii U side, "
				   "Graphic packs on, each with its count. Off: the list has every game, and Sort and Search are all it needs."));
			int hold = 2;
			for (int i = 0; i < 6; i++)
				if (kHolds[i] <= settings.ui.holdMs)
					hold = i;
			const std::vector<std::string> holds = Options({"0.4 s", "0.6 s", "0.8 s", "1.0 s", "1.2 s", "1.5 s"});
			// what is on, and the hold when it is not the 0.8 s it starts at
			std::string easier;
			for (const auto& [on, name] : {std::pair{settings.ui.largerText, Tr("Larger text")}, std::pair{settings.ui.highContrast, Tr("High contrast")},
					 std::pair{settings.ui.reduceMotion, Tr("Reduce motion")}})
				if (on)
					easier += (easier.empty() ? "" : " · ") + std::string(name);
			if (hold != 2)
				easier += (easier.empty() ? "" : " · ") + holds[hold];
			// tr: Settings > Launcher: a row that opens Larger text, High contrast, Reduce motion and Hold to confirm
			sub("accessibility", Tr("Accessibility"), easier.empty() ? std::string(Tr("Default")) : easier,
				Tr("Larger text, high contrast, less motion, the hold to confirm."),
				Tr("Text a step larger, opaque panels with full-strength text, fades in place of movement, and how long Cross is held for what "
				   "cannot be undone. Triangle on each says more."));
			toggle("largertext", Tr("Larger text"), settings.ui.largerText, Tr("Every size of text a step up."),
				Tr("Every style of text a step larger: captions from 20 to 23, reading text from 26 to 30."));
			toggle("highcontrast", Tr("High contrast"), settings.ui.highContrast, Tr("Opaque panels, full-strength text, a thicker ring."),
				Tr("Opaque panels instead of glass, all text at full strength, a thicker focus ring and no grain or pictures behind the menus."));
			toggle("reducemotion", Tr("Reduce motion"), settings.ui.reduceMotion, Tr("Fades in place of movement."),
				Tr("Springs become short fades; nothing drifts, breathes or rises behind the menus."));
			choice("holdms", Tr("Hold to confirm"), holds, hold, Tr("How long Cross is held for what cannot be undone."),
				Tr("Clearing caches, Artic Setup and resetting controls are held to confirm, never pressed twice. This is how long."), Kind::Stepper);
			endSub();
			action(Kind::Link, "makeityours", Tr("Make it yours"), "", Tr("The first-start choices again: sides, theme, buttons."),
				Tr("The page the first start opens on: the language, the sides, the theme, A and B, the music, vibration and the games' "
				   "pictures, on one page."));
		}
		else if (page == "diagnostics")
		{
			action(Kind::Link, "setupcheck", Tr("Setup check"), "", Tr("Storage, recompilers, games, keys and box art, checked."),
				Tr("Each check has a status, what it means and what to do. It is also shown on the first start and whenever a check fails as "
				"the app starts."));
			action(Kind::Action, "copylogs", Tr("Copy logs to USB"), m_diagnosticsDone[0], Tr("The logs a report needs, onto a USB drive."),
				Tr("Copies the last five sessions' logs and the settings into a dated folder on a USB drive, to attach to a report."));
			if (IsWiiU())
				action(Kind::Hold, "clearcaches", Tr("Clear shader caches"), m_diagnosticsDone[1].empty() ? Tr("Hold Cross") : m_diagnosticsDone[1],
					Tr("For a game that crashes on a bad cache."), Tr("Deletes Cemu's shader caches. Games build them again as they run: the first minutes stutter."));
			else if (Is3ds())
				action(Kind::Hold, "clearcaches", Tr("Clear shader caches"), m_diagnosticsDone[2].empty() ? Tr("Hold Cross") : m_diagnosticsDone[2],
					Tr("For a game that crashes on a bad cache."),
					Tr("Deletes Azahar's shader caches. Games build them again as they run: the first minutes stutter."));
			// the 3DS's and the Wii U's games unpacked from archives (app/game_archives.h): one cache for both
			if (IsWiiU() || Is3ds())
			{
				if (m_now - m_unpackedAt > 3)
				{
					m_unpackedBytes = ps5archive::UnpackedBytes();
					m_unpackedAt = m_now;
				}
				// tr: Settings > Diagnostics: the copies of games unpacked from their archives (.zip, .7z...), kept for their next start
				action(Kind::Hold, "clearunpacked", Tr("Unpacked games"),
					!m_unpackedDone.empty() ? m_unpackedDone :
					m_unpackedBytes			? SizeWords(m_unpackedBytes) + "  ·  " + Tr("Hold Cross") :
											  std::string(Tr("None to delete")),
					Tr("Games from archives, kept unpacked for their next start."),
					// tr: {0} is the folder they are kept in
					TrF("Deletes the copies of games unpacked from their archives, kept in {0}. Each is unpacked again the next time it "
						"starts; when room is needed, the copies used longest ago go by themselves.",
						ps5archive::UnpackedRoot()));
			}
		}
		// the rows of the pages that went into others (the owner's, 2026-10-27: fewer pages down the left), each
		// after the rows of the page it is on: the sound on System, after the emulated console's; the Wii U's
		// USB portals there too; and the remote touch screen on the 3DS's and the DS's Controls, the Wii U's
		// System
		if (page == "system")
		{
			own(slider("volume", IsWiiU() ? Tr("Wii U games' volume") : Is3ds() ? Tr("3DS games' volume") : IsSwitch() ? Tr("Switch games' volume") : Tr("DS games' volume"),
					IsWiiU() ? settings.volume : hand.volume, 100, Tr("The games' sound."), Tr("The games' sound on this side. Left and Right change it by 10%.")),
				"volume");
			if (IsWiiU())
				toggle("gamepadspeaker", Tr("GamePad speaker"), settings.gamePadSpeaker, Tr("The Wii U GamePad's own sound, on the DualSense."),
					Tr("The sounds games play on the Wii U GamePad's speaker, from player 1's DualSense speaker. Many games send their whole sound "
					"there too, so it is off unless you want it. Applies to the next game."));
			std::vector<std::string> musicNames;
			const std::vector<std::string> musicValues = MusicValues(&musicNames);
			const auto chosen = std::find(musicValues.begin(), musicValues.end(), settings.music);
			// a file no longer there plays the launcher's own piece: the row says so too
			const int music = chosen == musicValues.end() ? 0 : (int)(chosen - musicValues.begin());
			// tr: Settings > System: a row that opens the launcher's music and menu sounds
			sub("launchersound", Tr("Launcher sound"), musicNames[music], Tr("The menus' music and sounds."),
				Tr("The music that plays in the menus, how loud it is, and the sounds of moving and choosing. Every side has the same."));
			choice("music", Tr("Launcher music"), musicNames, music, Tr("Music that plays in the menus."),
				TrF("The launcher's own music, one of yours, or none. To use your own, put MP3 or WAV files in {0}; they appear here. Every "
					"side plays the same.",
					ps5sound::MusicFolder()));
			slider("musicvolume", Tr("Music volume"), settings.musicVolume, 100, Tr("How loud the menus' music is."),
				Tr("How loud the launcher's music is. Left and Right change it by 10%."));
			toggle("menusounds", Tr("Menu sounds"), settings.menuSounds, Tr("The sounds of moving and choosing."),
				Tr("The launcher's sounds as you move, choose and go back."));
			endSub();
		}
		if (page == "system" && IsWiiU())
		{
			int plugged = 0;
			for (ps5usb::Device device : ps5usb::kDevices)
				plugged += m_status.coreReady && ps5usb::Enabled(device);
			// tr: Settings > System on the Wii U side: a row that opens Cemu's emulated portals (Skylanders, Disney Infinity, LEGO Dimensions)
			sub("usb", Tr("USB devices"), plugged ? TrP(plugged, "{0} on", "{0} on") : std::string(Tr("Off")), Tr("Cemu's emulated portals."),
				PanelText("usb"));
			for (ps5usb::Device device : ps5usb::kDevices)
			{
				Row* row = toggle("usb", ps5usb::Name(device), m_status.coreReady && ps5usb::Enabled(device),
					device == ps5usb::Device::Skylanders ? Tr("For the Skylanders games.") :
					device == ps5usb::Device::Infinity	 ? Tr("For Disney Infinity 3.0.") :
														   Tr("For LEGO Dimensions."),
					TrF("Cemu's emulated portal, plugged in as a game starts. In the game, the menu's USB devices category puts figures on it: dumps "
						"in {0}. A real portal plugged into the PS5 is not supported.",
						ps5usb::Folder(device)));
				row->index = (int)device;
				cemu(row);
			}
			endSub();
		}
		// the remote touch screen is this program's: a Switch game runs in Eden's
		if ((page == "system" && IsWiiU()) || (page == "controls" && (Is3ds() || IsDs())))
		{
			const char* screen = IsWiiU() ? Tr("The GamePad's screen") : Is3ds() ? Tr("The bottom screen") : Tr("The touch screen");
			const std::string about = TrF("{0} in any browser on the PS5's network, a phone's, a tablet's or a computer's, with nothing to install, and its "
										  "touches sent back to the game. Scan the code shown here, or open the address. The DualSense stays the buttons, "
										  "and the sound stays on the PS5.\nWhile a browser shows it, the PS5 copies that screen up to 30 times a second, so "
										  "a game can run a little slower; with no browser, it costs nothing. Any device on your network can open the page, "
										  "so turn it off on a network you share.\nOne switch for every side (the GamePad's screen is player 1's), also in "
										  "a game's menu (touchpad click + Options). From dolphin78's idea and prototype (#38).",
				screen);
			// tr: a row that opens the remote touch screen's page: the game's second screen in a phone's browser, and its code to scan
			sub("remote", Tr("Remote touch screen"), settings.remoteScreen ? Tr("On") : Tr("Off"), TrF("{0} in a phone's or tablet's browser, with touch.", screen),
				about);
			toggle("remote", Tr("Remote touch screen"), settings.remoteScreen, TrF("{0} in a phone's or tablet's browser, with touch.", screen), about);
			endSub();
		}
		(void)side;
		return rows;
	}

	std::vector<Row> Shell::SettingRows(const std::string& page)
	{
		if (IsSwitch() && (page == "nxmods" || page == "nxcheats"))
			return SwitchExtrasRows(page);
		// the Switch's rows from Eden's own files (switch_extras.cpp), behind a row of the page they are on: its
		// ReShade looks behind Graphics' Picture effects, after FSR sharpening and anti-aliasing; its players
		// behind a row of their own, first on Players and saves
		auto withExtras = [&](std::vector<Row> rows) {
			if (!IsSwitch() || (page != "graphics" && (page != "saves" || InGameSettings())))
				return rows;
			const std::string group = page == "graphics" ? "nxpost" : "nxprofiles";
			std::vector<Row> added = SwitchExtrasRows(group);
			std::string look;
			for (Row& row : added)
			{
				row.group = group;
				// a game's own look is its own (game settings' chip on the row it is behind)
				row.fromSide = row.id != "nxextra:preset" || !(InGameSettings() && m_switchExtras.ownPreset);
				// (not Default, the side's, nor Off)
				if (row.id == "nxextra:preset" && row.index > 0 && row.value != Tr("Off"))
					look = row.value;
			}
			if (group == "nxpost")
			{
				// the look chosen, then the anti-aliasing
				for (Row& row : rows)
					if (row.opens == group && !look.empty())
						row.value = row.value == Tr("Off") ? look : look + " · " + row.value;
			}
			else
			{
				const auto& view = m_switchExtras;
				Row players;
				players.kind = Row::Kind::Link;
				players.id = "sub";
				players.opens = group;
				// tr: the Switch side's players, the people (Eden's user profiles), each with saves of their own; not a number of players
				players.label = TrC("switch", "Players");
				players.value = view.error.empty() && view.profile >= 0 && view.profile < (int)view.profiles.size() ? view.profiles[view.profile].name : "";
				players.description = Tr("Who plays, each with saves of their own.");
				players.help = Tr("Remembered for this PS5 user. Each player has saves of their own; none are moved or deleted.");
				rows.insert(rows.begin(), std::move(players));
			}
			rows.insert(rows.end(), std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
			return rows;
		};
		if (!InGameSettings())
			return SubPageRows(withExtras(SideRows(page, m_settings)));
		if (page == "gamesaves" && !IsRetro())
			return GameSaveRows();
		// a game's: its rows only, each its own value or the side's (Default), and the › rows they are behind
		const std::string side = ps5launcher::SideName(m_side);
		const auto found = m_settings.games.find(ps5settings::GameKey(side, m_gameTitle));
		std::vector<Row> rows;
		for (Row& row : SideRows(page, m_gameView))
		{
			if (row.setting.empty() && row.opens.empty())
				continue;
			row.fromSide = found == m_settings.games.end() || !found->second.count(row.setting);
			rows.push_back(std::move(row));
		}
		rows = SubPageRows(withExtras(std::move(rows)));
		if (!rows.empty() && m_subPage.empty())
		{
			Row reset;
			reset.kind = Row::Kind::Hold;
			reset.id = "resetgame";
			reset.label = Tr("Back to defaults");
			reset.value = m_message.empty() ? Tr("Hold Cross") : m_message;
			reset.description = Tr("This game goes back to its side's settings.");
			reset.help = Tr("Clears this game's own settings, so it uses its side's settings again, like every other game.");
			rows.push_back(reset);
		}
		return rows;
	}

	std::vector<Row> Shell::SubPageRows(const std::vector<Row>& rows) const
	{
		std::vector<Row> shown;
		for (const Row& row : rows)
		{
			if (row.group != m_subPage)
				continue;
			if (!row.opens.empty())
			{
				// a › row with nothing behind it goes (a game's settings, which have none of its rows); in a
				// game's, it says whether the game has its own of any
				bool any = false, fromSide = true;
				for (const Row& in : rows)
					if (in.group == row.opens)
					{
						any = true;
						fromSide = fromSide && in.fromSide;
					}
				if (!any)
					continue;
				shown.push_back(row);
				shown.back().fromSide = fromSide;
				continue;
			}
			shown.push_back(row);
		}
		return shown;
	}

	void Shell::OpenSubPage(const Row& row)
	{
		m_subPage = row.opens;
		m_subFrom = m_settingRow;
		m_subTitle = row.label;
		m_subLine = row.description;
		m_settingRow = 0;
		m_message.clear();
		m_feedback.Play(ui::Cue::Select);
	}

	void Shell::CloseSubPage()
	{
		m_subPage.clear();
		m_settingRow = m_subFrom;
		m_message.clear();
		m_feedback.Play(ui::Cue::Back);
	}

	std::string Shell::SaveLabel(const ps5saves::GameSave& save) const
	{
		using Kind = ps5saves::GameSave::Kind;
		switch (save.kind)
		{
		case Kind::Account:
			if (m_status.coreReady)
				for (const auto& account : ps5emu::ListAccounts())
					if (account.id == save.account)
						return account.name;
			// tr: a Wii U account by its number, when its name cannot be read
			return TrF("Account {0}", fmt::format("{:08x}", save.account));
		// tr: a Wii U game's save that all its accounts share
		case Kind::Common: return Tr("Every account's");
		case Kind::Data: return Tr("Save data");
		// tr: a 3DS game's extra data (its extdata), which some games keep beside their saves
		case Kind::ExtData: return Tr("Extra data");
		case Kind::States: return TrP((int)save.paths.size(), "{0} save state", "{0} save states");
		case Kind::Slot2: return Tr("The GBA cartridge's save");
		}
		return {};
	}

	// A game's saves (6.15): a row for each, held Cross to move it aside; this game's alone backed up
	// and restored
	std::vector<Row> Shell::GameSaveRows()
	{
		using Kind = Row::Kind;
		if (m_now - m_gameSavesAt > 3)
		{
			m_gameSaves = ps5saves::FindGameSaves(m_gameRef);
			m_gameBackups = ps5saves::FindGameBackups(m_gameRef);
			m_switchImports.clear();
			if (IsSwitch() && !ps5eden::IsMadeId(m_gameTitle))
				for (int drive = 0; drive < 8; drive++)
					for (const char* name : {"PS5-ECAM-imports", "Ryujinx"})
					{
						const std::string folder = fmt::format("/mnt/usb{}/{}", drive, name);
						std::string why;
						for (const auto& choice : ps5eden::transfer::Choices(folder, m_gameTitle, why))
							m_switchImports.push_back({folder, fmt::format("USB {} · {} · {}", drive, name, choice.label), choice.accountSaveId});
					}
			m_gameSavesAt = m_now;
		}
		const ps5saves::Status status = ps5saves::Current();
		const bool running = status.state == ps5saves::Status::State::Running;
		if (status.finished != m_savesFinished)
		{
			m_savesFinished = status.finished;
			m_gameSavesAt = -100;
			m_savesMessage = status.state == ps5saves::Status::State::Done ?
				(status.message.empty() ? TrP(status.files, "Done: {0} file", "Done: {0} files") : status.message) : status.message;
		}
		std::vector<Row> rows;
		for (size_t i = 0; i < m_gameSaves.size(); i++)
		{
			const ps5saves::GameSave& save = m_gameSaves[i];
			Row row;
			row.kind = Kind::Hold;
			row.id = fmt::format("gamesave:{}", i);
			row.label = SaveLabel(save);
			std::string when;
			const std::time_t changed = (std::time_t)save.changed;
			std::tm local{};
			if (save.changed > 0 && localtime_r(&changed, &local))
				when = ps5lang::ShortDateTime(local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
			row.value = SizeWords(save.bytes) + (when.empty() ? "" : " · " + when) + "  ·  " + Tr("Hold Cross");
			row.description = Tr("Held: moved to saves-before on the PS5, so it can still come back.");
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			row.help = TrF("Holding Cross moves these files to {0}/saves-before, in a folder named by the date and time, and the game "
						   "starts without them next time. Nothing is deleted: copy them back from there (or restore a backup) to have them again.",
				ps5paths::Root());
			rows.push_back(std::move(row));
		}
		if (m_gameSaves.empty())
		{
			Row none;
			none.kind = Kind::Action;
			none.id = "gamenosaves";
			none.label = Tr("No saves yet");
			none.description = Tr("They appear here once the game has saved.");
			none.dimmed = true;
			rows.push_back(std::move(none));
		}
		Row backup;
		backup.kind = Kind::Action;
		backup.id = "gamebackup";
		backup.label = Tr("Back up to USB");
		backup.value = m_savesMessage;
		if (running && !status.restoring)
			backup.value = TrF("Copying: {0}", Percent(status.total ? (int)(status.copied * 100 / status.total) : 0));
		else if (backup.value.empty() && !m_gameBackups.empty())
			backup.value = TrF("Last: {0} · {1}", m_gameBackups.front().when, DriveName(m_gameBackups.front().drive));
		backup.description = Tr("This game's saves, to the first USB drive.");
		backup.help = Tr("Copies this game's saves (every account's, on the Wii U) and its save states to PS5CEMU-HAR-saves on the first USB "
						 "drive plugged in, in a folder named by the date and time, the same way a backup of every side's saves is made.");
		if (m_gameSaves.empty())
			backup.dimmed = true;
		rows.push_back(std::move(backup));
		std::vector<std::string> backups;
		for (const auto& found : m_gameBackups)
			backups.push_back(found.when + " · " + DriveName(found.drive));
		int& chosen = m_holdChoice["gamerestore"];
		chosen = backups.empty() ? 0 : std::clamp(chosen, 0, (int)backups.size() - 1);
		Row restore;
		restore.kind = Kind::Hold;
		restore.id = "gamerestore";
		restore.label = Tr("Restore from USB");
		restore.value = backups.empty() ? std::string(Tr("No backups found")) : backups[chosen] + "  ·  " + Tr("Hold Cross");
		if (running && status.restoring)
			restore.value = TrF("Copying: {0}", Percent(status.total ? (int)(status.copied * 100 / status.total) : 0));
		restore.description = backups.empty() ? Tr("No backup on the drives plugged in has this game's saves.") : Tr("Left and Right choose which backup, the newest first.");
		// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
		restore.help = TrF("Puts this game's saves from the chosen backup back where they came from, and nothing else. Its saves there now are "
						   "moved first to {0}/saves-before, so nothing is lost.", ps5paths::Root());
		restore.options = backups;
		restore.index = chosen;
		if (backups.empty())
			restore.dimmed = true;
		rows.push_back(std::move(restore));
		if (IsSwitch() && !ps5eden::IsMadeId(m_gameTitle))
		{
			Row exportSave;
			exportSave.kind = Kind::Action; exportSave.id = "nxexport"; exportSave.label = Tr("Copy the save to USB");
			exportSave.description = Tr("The chosen player's save, in USB/PS5-ECAM-exports, as folders another emulator can take.");
			exportSave.value = m_savesMessage; exportSave.dimmed = running;
			rows.push_back(std::move(exportSave));
			Row importSave;
			importSave.kind = Kind::Hold; importSave.id = "nximport"; importSave.label = Tr("Take a save from USB");
			for (const auto& source : m_switchImports) importSave.options.push_back(source.label);
			int& chosen = m_holdChoice["nximport"];
			chosen = importSave.options.empty() ? 0 : std::clamp(chosen, 0, (int)importSave.options.size() - 1);
			importSave.index = chosen;
			importSave.value = importSave.options.empty() ? Tr("None on a USB drive") : importSave.options[chosen] + " · " + Tr("Hold Cross");
			importSave.description = Tr("From USB/PS5-ECAM-imports/<title ID>, or a copy of Ryujinx's folder in USB/Ryujinx, as the chosen player's save. The save it replaces is kept.");
			importSave.help = Tr("Choose the player in Settings > Players and saves > Players first. Every file is copied and checked before the save is replaced, and the save it replaces is kept in Eden's save-transfer-recovery folder.");
			importSave.dimmed = running || importSave.options.empty(); rows.push_back(std::move(importSave));
		}
		return rows;
	}

	namespace
	{
		// a play time as hours and minutes
		std::string Duration(uint64_t minutes)
		{
			// tr: a play time under an hour
			if (minutes < 60)
				return TrF("{0} min", minutes);
			// tr: a play time: hours, then minutes
			return TrF("{0} h {1} min", minutes / 60, minutes % 60);
		}
	}

	// Settings > Play time: each side's minutes and how many of its games were played, all sides',
	// and the most played games (the catalogue's counts: ps5catalog::Ended)
	std::string Shell::PlayTimeText() const
	{
		struct Played
		{
			std::string name;
			const char* side;
			uint32_t minutes;
		};
		const char* sides[kSides] = {"Wii U", "3DS", "DS", "Switch", "GameCube", "Wii", "N64", "NES", "GB", "GBC", "GBA", "SNES", "Virtual Boy", "Pokémon mini"};
		std::vector<Played> games;
		std::string text;
		uint64_t all = 0;
		for (int side = 0; side < kSides; side++)
		{
			uint64_t minutes = 0;
			int played = 0;
			for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)side))
				if (entry.minutesPlayed > 0)
				{
					minutes += entry.minutesPlayed;
					played++;
					games.push_back({entry.game.name, sides[side], entry.minutesPlayed});
				}
			all += minutes;
			if (played > 0)
				// tr: Settings > Play time, a side's line: {1} the side (Wii U, 3DS, DS, Switch), {2} its play time, {0} how many games
				text += TrP(played, "{1}: {2}, {0} game", "{1}: {2}, {0} games", sides[side], Duration(minutes)) + "\n";
		}
		if (games.empty())
			return Tr("Nothing played yet. A game's time counts from its start to its end, and shows here and on its page.");
		std::stable_sort(games.begin(), games.end(), [](const Played& a, const Played& b) { return a.minutes > b.minutes; });
		// tr: Settings > Play time: every side's play time added up
		text += TrF("In all: {0}", Duration(all)) + "\n\n" + Tr("Most played") + "\n";
		for (size_t i = 0; i < games.size() && i < 10; i++)
			text += fmt::format("{}. {}  ·  {}  ·  {}\n", i + 1, games[i].name, Duration(games[i].minutes), games[i].side);
		return text;
	}

	std::string Shell::PanelText(const std::string& page) const
	{
		if (InGameSettings())
			return {};
		if (page == "playtime")
			return PlayTimeText();
		if (page == "diagnostics")
		{
			std::string text;
			for (const auto& line : m_status.diagnostics)
				text += line + "\n";
			return text;
		}
		if (page == "launcher")
			return TrF("Your own theme: a file such as sunset.json in {0}/themes, read as the app starts, then listed under Theme by its name:\n"
					   "{{\"name\": \"Sunset\", \"base\": \"midnight\", \"colours\": {{\"wiiu\": \"#ff7a59\", \"ink-0\": \"#120806\"}}, "
					   "\"corners\": 0.5, \"motif\": \"none\"}}\n"
					   "base: a theme to start from (light, link-jack, classic, midnight, eden). colours: ink-0 to ink-2 (the background), text, "
					   "wiiu, n3ds, nds and nx (each side's), glass, line, good, warn, bad and focus, as #rrggbb. corners: 0 square to 1.5 round. "
					   "motif: side, none or eden.",
				ps5paths::Root());
		if (page == "files" && IsSwitch())
			// tr: {0} is Eden's data folder; {1} its game files folder (the same one unless another was chosen)
			return TrF("Saves: {0}/user/nand/user/save. Updates and DLC: beside a game, in its own folder, or in {1}/updates. Mods and "
					   "cheats: {1}/mods. Covers and names: {0}/covers, once Eden has read a game.",
				ps5eden::DataFolder(), ps5eden::GameFilesFolder());
		if (page == "files" && IsDs())
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			return TrF("Saves: {0}/melonds/saves (a .sav beside the game is taken the first time). Save states: melonds/states. Cheats: "
					   "melonds/cheats/<game file>.mch, melonDS's format. A GBA game for Slot-2: melonds/slot2/<game file>.gba. DSi games and the DS's wireless are not there.",
				ecam_layout::Parent(ps5paths::MelonDs()));
		if (page == "remote" && !m_settings.remoteScreen)
			return Tr("On, a QR code here opens the game's second screen in a phone's browser. Nothing to install; the phone must be on the "
				   "PS5's network.");
		if (page == "usb")
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			return TrF("Figure dumps go in {0}/figures: skylanders, infinity and dimensions. Switch a portal on here, start the game, then "
					   "put figures on it from the in-game menu (touchpad click + Options > USB devices).",
				ps5paths::Root());
		if (page == "about")
			// tr: premohq and JMUtechnologies are GitHub names: keep them as they are
			return std::string(Tr("PS5-ECAM is made by its co-founders, premohq and JMUtechnologies.")) + "\n" +
				   // tr: {0} is the app's version; {1} its data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on; {2} Eden's game files folder; {3} Cemu's selected data folder; {4} its default games folder; {5} Azahar's selected folder; {6} melonDS's selected folder
				   TrF("Cemu, the Wii U emulator, by the Cemu team and its contributors; RADV on the PS5 by Mihawk-99 and mpereiraesaa; the "
				   "community graphic packs' authors.\nAzahar, the 3DS emulator, by the Azahar team and the Citra contributors before them; "
				   "Mihawk-99's PS5 port of it and dynarmic.\nmelonDS, the DS emulator, by Arisotura and the melonDS team.\nEden, the Switch "
				   "emulator, by the Eden team; ProsperoEden, its PS5 port and launcher, by Sword, BlackBearReloaded, bawsdeep, Rockiu0 and its "
				   "contributors.\nBox art and game information: GameTDB. The font: Lexend. The remote touch screen: dolphin78's idea and "
				   "prototype.\nAn unofficial port, not affiliated with the Cemu, Azahar, melonDS or Eden teams, Nintendo or Sony.\n\nWii U "
				   "games: {4}, storage and saves: {3}/mlc01, keys: {3}/keys.txt\n3DS games: {5}/games, storage: "
				   "{5}/sdmc\nDS games: {6}/games, saves: {6}/saves\nSwitch games: {2}/roms, keys: {2}/keys, firmware: "
				   "{2}/firmware\n\nVersion {0}",
				ps5update::Readable(PS5CEMU_VERSION), ps5paths::Root(), ps5eden::GameFilesFolder(), ps5paths::CemuRoot(), ps5paths::Games(), ps5paths::Azahar(), ps5paths::MelonDs());
		return {};
	}

	// -- the menus' language ----------------------------------------------------------------------------

	std::vector<std::string> Shell::LanguageOptions(int& index) const
	{
		// tr: the first choice in the language list: the PS5's own ({0}, in its own words)
		std::vector<std::string> options{TrF("The PS5's language: {0}", ps5lang::NameOf(ps5lang::SystemLanguage()))};
		index = 0;
		for (const ps5lang::Language& language : ps5lang::Languages())
		{
			if (m_settings.ui.language == language.code)
				index = (int)options.size();
			options.push_back(language.name);
		}
		return options;
	}

	void Shell::OpenLanguagePicker(const std::string& kicker)
	{
		int index = 0;
		std::vector<std::string> options = LanguageOptions(index);
		OpenPicker(kicker, Tr("Language"), std::move(options), index, [this](int choice) { SetLanguage(choice); });
	}

	void Shell::SetLanguage(int index)
	{
		// the first choice is the PS5's own; the rest are ps5lang's, in its order
		const auto& languages = ps5lang::Languages();
		m_settings.ui.language = index > 0 && index <= (int)languages.size() ? languages[index - 1].code : "";
		SaveSettings();
		ApplyLanguage();
		Toast(TrF("Language: {0}", ps5lang::NameOf(ps5lang::Current())));
	}

	// -- changing a row ---------------------------------------------------------------------------------

	void Shell::SetRowIndex(ps5settings::Launcher& settings, const std::string& id, int index)
	{
		ps5settings::N3ds& hand = m_side == System::Nds ? settings.nds : m_side == System::Switch ? settings.nx : settings.n3ds;
		if (id == "retro-backend" && IsRetro()) settings.retro[SideIndex() - 4].retroBackend = std::clamp(index, 0, 1);
		else if (id == "music")
		{
			const std::vector<std::string> values = MusicValues(nullptr);
			settings.music = values[std::clamp(index, 0, (int)values.size() - 1)];
		}
		else if (id == "holdms")
			settings.ui.holdMs = kHolds[std::clamp(index, 0, 5)];
		else if (id == "upscaling")
			settings.upscaleFilter = std::clamp(index, 0, 4);
		else if (id == "framepacing")
			settings.framePacing = std::clamp(index + 1, 1, 3);
		else if (id == "resolution")
			hand.resolution = std::clamp(index + 1, 1, 10);
		else if (id == "filter")
			hand.textureFilter = kOfferedFilters[std::clamp(index, 0, (int)std::size(kOfferedFilters) - 1)];
		else if (id == "layout")
			hand.layout = std::clamp(index, 0, 3);
		else if (id == "border")
			hand.border = std::clamp(index, 0, 5);
		else if (id == "region")
			hand.region = std::clamp(index - 1, -1, 6);
		else if (id == "language")
			hand.language = std::clamp(index - 1, -1, 11);
		else if (id == "cpuclock")
			hand.cpuClock = kClocks[std::clamp(index, 0, (int)std::size(kClocks) - 1)];
		else if (id == "dstouchx") hand.dsTouchX = std::clamp(index - 20, -20, 20);
		else if (id == "dstouchy") hand.dsTouchY = std::clamp(index - 20, -20, 20);
		else if (id == "dstouchscale") hand.dsTouchScale = std::clamp(index + 50, 50, 150);
		else if (id == "dsmic")
			hand.dsMicSource = std::clamp(index, 0, 2);
		else if (id == "dsfilter")
			hand.dsFilter = std::clamp(index, 0, 2);
		else if (id == "dsrotation")
			hand.dsRotation = std::clamp(index, 0, 3);
		else if (id == "nx-resolution")
			hand.nxResolution = std::clamp(index, 0, 6);
		else if (id == "nx-filter")
			hand.nxFilter = std::clamp(index, 0, 3);
		else if (id == "nx-astc")
			hand.nxAstc = std::clamp(index, 0, 1);
		else if (id == "nx-docked")
			hand.nxDocked = index == 0;
		else if (id == "nx-speedlimit")
			hand.nxSpeedLimit = std::clamp(index, 0, 4);
		else if (id == "nx-covers")
			hand.nxCovers = std::clamp(index, 0, 1);
		else if (id == "nx-sharpening")
			hand.nxSharpening = kNxSharpening[std::clamp(index, 0, (int)std::size(kNxSharpening) - 1)];
		else if (id == "nx-aa")
			hand.nxAntiAliasing = std::clamp(index, 0, 2);
		else if (id == "nx-controller")
			hand.nxController = std::clamp(index - 1, -1, 4);
		else if (id == "nx-output")
			hand.nxOutput = std::clamp(index, 0, 2);
		else if (id == "nx-language")
			hand.nxLanguage = std::clamp(index - 1, -1, (int)std::size(ps5eden::kLanguages) - 1);
		else if (id == "nx-memory")
			hand.nxMemory = std::clamp(index, 0, 2);
		else if (id == "theme")
			settings.ui.theme = index >= 0 && index < (int)m_themes.size() ? m_themes[index].id : "";
		else if (id == "abeverywhere")
		{
			// A and B on every side: the 3DS's and the DS's now, the Wii U players' now when Cemu is up, else
			// as the Wii U side next opens (OpenSide)
			using ps5azahar::Button;
			using ps5emu::PadInput;
			const bool aOnCircle = index == 0;
			for (ps5settings::N3ds* handheld : {&settings.n3ds, &settings.nds})
			{
				const bool ds = handheld == &settings.nds;
				ps5azahar::SetMapping(*handheld, (size_t)Button::A, aOnCircle ? PadInput::Circle : PadInput::Cross, ds);
				ps5azahar::SetMapping(*handheld, (size_t)Button::B, aOnCircle ? PadInput::Cross : PadInput::Circle, ds);
				ps5azahar::SetMapping(*handheld, (size_t)Button::X, aOnCircle ? PadInput::Triangle : PadInput::Square, ds);
				ps5azahar::SetMapping(*handheld, (size_t)Button::Y, aOnCircle ? PadInput::Square : PadInput::Triangle, ds);
			}
			settings.wiiuFaceLayout = aOnCircle ? 1 : 2;
			if (m_status.coreReady)
			{
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
					ps5emu::SetFaceLayout(player, aOnCircle ? ps5emu::FaceLayout::Nintendo : ps5emu::FaceLayout::PlayStation);
				settings.wiiuFaceLayout = 0;
			}
		}
		else if (id == "turbo")
			(IsWiiU() ? settings.turbo : hand.turbo) = index <= 0 || index > (int)std::size(kTurboInputs) ? "" : kTurboInputs[index - 1];
		else if (id == "abbuttons")
		{
			using ps5azahar::Button;
			using ps5emu::PadInput;
			const bool aOnCircle = index == 0, ds = m_side == System::Nds;
			ps5azahar::SetMapping(hand, (size_t)Button::A, aOnCircle ? PadInput::Circle : PadInput::Cross, ds);
			ps5azahar::SetMapping(hand, (size_t)Button::B, aOnCircle ? PadInput::Cross : PadInput::Circle, ds);
			ps5azahar::SetMapping(hand, (size_t)Button::X, aOnCircle ? PadInput::Triangle : PadInput::Square, ds);
			ps5azahar::SetMapping(hand, (size_t)Button::Y, aOnCircle ? PadInput::Square : PadInput::Triangle, ds);
		}
	}

	bool Shell::ApplyRow(ps5settings::Launcher& settings, const Row& row, int step, bool cross)
	{
		ps5settings::N3ds& hand = m_side == System::Nds ? settings.nds : m_side == System::Switch ? settings.nx : settings.n3ds;
		const std::string& id = row.id;
		switch (row.kind)
		{
		case Row::Kind::Toggle:
		{
			if (!cross && step != 0 && ((step > 0) == row.on))
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			const bool on = !row.on;
			if (id.rfind("side-", 0) == 0)
			{
				const int bit = 1 << (int)ps5launcher::SideNamed(id.substr(5));
				const int sides = on ? settings.ui.sides | bit : settings.ui.sides & ~bit;
				if (sides == 0)
				{
					m_feedback.Play(ui::Cue::Denied);
					Toast(Tr("One side at least stays on"));
					return false;
				}
				settings.ui.sides = sides;
			}
			else if (id == "pictures")
				settings.ui.gamePictures = on;
			else if (id == "filters")
				settings.ui.libraryFilters = on;
			else if (id == "lookonshares")
				settings.lookOnShares = on;
			else if (id == "menusounds")
				settings.menuSounds = on;
			else if (id == "gamepadspeaker")
				settings.gamePadSpeaker = on;
			else if (id == "remote")
				settings.remoteScreen = on;
			else if (id == "rumble")
				settings.rumble = on;
			else if (id == "boxart")
				settings.boxArt = on;
			else if (id == "accountperuser")
				settings.accountPerUser = on;
			else if (id == "largertext")
				settings.ui.largerText = on;
			else if (id == "highcontrast")
				settings.ui.highContrast = on;
			else if (id == "reducemotion")
				settings.ui.reduceMotion = on;
			else if (id == "highframerate")
				settings.highFrameRate = on;
			else if (id == "framegeneration")
				settings.frameGeneration = on;
			else if (id == "framegeneration-3ds")
				hand.frameGeneration = on;
			else if (id == "overlay")
				settings.overlay = on;
			else if (id == "async")
				settings.asyncShaders = on;
			else if (id == "shadernotices")
				settings.shaderNotices = on;
			else if (id == "customtextures")
				hand.customTextures = on;
			else if (id == "async-3ds")
				hand.asyncShaders = on;
			else if (id == "outlinefix")
				hand.invariantPosition = on;
			else if (id == "hwshaders")
				hand.hwShaders = on;
			else if (id == "anaglyph")
				hand.anaglyph = on;
			else if (id == "motion")
				hand.motion = on;
			else if (id == "dsjit")
				hand.dsJit = on;
			else if (id == "dsslot2")
				hand.dsSlot2 = on;
			else if (id == "dsstylus")
				hand.dsStylus = on;
			else if (id == "performance-hand")
				hand.performance = on;
			else if (id == "nx-framegeneration")
				hand.nxFrameGeneration = on;
			else if (id == "reshade")
				settings.reshade = on;
			else if (id == "nx-120hz")
				hand.nxHighRefresh = on;
			else if (id == "nx-async")
				hand.nxAsyncShaders = on;
			else if (id == "nx-fastgpu")
				hand.nxFastGpu = on;
			else if (id == "nx-unsafecpu")
				hand.nxUnsafeCpu = on;
			else if (id == "nx-unsafedma")
				hand.nxUnsafeDma = on;
			else if (id == "nx-reactive")
				hand.nxReactiveFlushing = on;
			else if (id == "nx-skipinvalidation")
				hand.nxSkipInvalidation = on;
			else if (id == "nx-blocklist")
				hand.nxBlockList = on;
			else if (id == "nx-compactmemory")
				hand.nxCompactMemory = on;
			else if (id == "hiddentouch")
				hand.hiddenTouch = on;
			else if (id == "screengap")
				hand.screenGap = on;
			else if (id == "dsownbios")
				hand.dsOwnBios = on;
			else
				return false;
			m_feedback.Play(ui::Cue::Toggle);
			return true;
		}
		case Row::Kind::Choice:
		case Row::Kind::Stepper:
		case Row::Kind::Segmented:
		case Row::Kind::Swatches:
		{
			const int count = (int)row.options.size();
			// the language: its list, whichever way it is asked for (stepping would change every word at each press)
			if (row.kind == Row::Kind::Choice && (cross || id == "ui-language"))
			{
				// the dropdown: its choice is made where the row's value lives (the side's, or the game's)
				const Row chosen = row;
				OpenPicker(InGameSettings() ? m_gameName : SystemName(m_side), row.label, row.options, row.index,
					[this, chosen](int index) { ChooseIndex(chosen, index); });
				return false;
			}
			int index = row.index + (cross ? 1 : step);
			if (row.kind == Row::Kind::Choice || cross)
				index = (index + count) % count; // round
			else if (index < 0 || index >= count)
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			SetRowIndex(settings, id, index);
			m_feedback.Play(ui::Cue::Focus);
			return true;
		}
		case Row::Kind::Slider:
		{
			int* value = id == "musicvolume" ? &settings.musicVolume :
				id == "volume"				 ? (IsWiiU() ? &settings.volume : &hand.volume) :
				id == "deadzone"			 ? &hand.deadzone :
				id == "nx-deadzone"			 ? &hand.nxDeadzone :
											   nullptr;
			if (!value)
				return false;
			const int most = id == "deadzone" ? 50 : id == "nx-deadzone" ? 30 : 100, by = id == "deadzone" ? 5 : id == "nx-deadzone" ? 2 : 10;
			int next = *value + (cross ? by : step * by);
			if (cross && next > most)
				next = 0;
			next = std::clamp(next, 0, most);
			if (next == *value)
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			*value = next;
			m_feedback.Play(ui::Cue::Focus);
			return true;
		}
		default: return false;
		}
	}

	// What a changed setting changes at once: the launcher's sound, the DualSense's rumble, box art
	void Shell::UseOwnAccount()
	{
		if (!m_status.coreReady || !m_settings.accountPerUser)
			return;
		const int32_t user = ps5pad::UserId(0);
		if (user < 0)
			return;
		const uint32_t id = ps5emu::AccountForUser(fmt::format("{:#010x}", user), ps5pad::UserName(0), m_settings.accountsByUser);
		SaveSettings();
		for (const auto& account : ps5emu::ListAccounts())
			if (account.id == id)
				Toast(TrF("Games start on {0}'s saves", account.name));
	}

	void Shell::SettingsChanged(const std::string& id)
	{
		ps5sound::SetMusic(m_settings.music, m_settings.musicVolume);
		ps5sound::SetMenuSounds(m_settings.menuSounds);
		ps5pad::SetVibrationEnabled(m_settings.rumble);
		if (id == "theme")
		{
			ApplyTheme();
			m_feedback.SetLight(Accent());
		}
		if (id.rfind("side-", 0) == 0)
			SidesChanged();
		if (id == "filters")
		{
			m_libraryIndex = 0;
			LibraryRefresh(); // every game again, or the side's filter
		}
		if (id == "accountperuser" && m_settings.accountPerUser)
			UseOwnAccount();
		if (id == "remote")
			ps5stream::Configure(m_settings.remoteScreen);
		if (id == "boxart")
		{
			ps5boxart::SetEnabled(m_settings.boxArt);
			if (m_settings.boxArt)
				FetchBoxArt();
		}
		if (id == "nx-covers")
		{
			// each game's cover again, the other kind: box art fetched where it is missing
			for (Game& game : m_games)
				game.boxArt = SwitchCover(game.entry.game);
			FetchBoxArt();
		}
		SaveSettings();
	}

	// A game's own value, from its view of the settings after a row changed it
	void Shell::KeepGameSetting(const std::string& setting, const ps5settings::Launcher& view)
	{
		const std::string side = ps5launcher::SideName(m_side);
		int value = 0;
		if (setting.empty() || !ps5settings::Value(view, side, setting, value))
			return;
		m_settings.games[ps5settings::GameKey(side, m_gameTitle)][setting] = value;
		SaveSettings();
		m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
	}

	void Shell::ChooseIndex(const Row& row, int index)
	{
		if (row.id == "retro-backend" && IsRetro())
		{
			m_settings.retro[SideIndex() - 4].retroBackend = std::clamp(index, 0, 1); SaveSettings(); return;
		}
		if (row.id == "ui-language")
		{
			SetLanguage(index);
			return;
		}
		if (InGameSettings())
		{
			ps5settings::Launcher view = m_gameView;
			SetRowIndex(view, row.id, index);
			KeepGameSetting(row.setting, view);
			return;
		}
		SetRowIndex(m_settings, row.id, index);
		SettingsChanged(row.id);
	}

	void Shell::ResetGameSetting(const Row& row)
	{
		const std::string side = ps5launcher::SideName(m_side);
		const auto found = m_settings.games.find(ps5settings::GameKey(side, m_gameTitle));
		if (row.setting.empty() || found == m_settings.games.end() || !found->second.erase(row.setting))
		{
			m_feedback.Play(ui::Cue::Edge);
			return;
		}
		if (found->second.empty())
			m_settings.games.erase(found);
		SaveSettings();
		m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
		m_feedback.Play(ui::Cue::Back);
	}

	void Shell::GameSettingsOpen(int game)
	{
		m_switchExtrasKey.clear();
		if (game < 0 || game >= (int)m_games.size())
			return;
		const ps5emu::Game& g = m_games[game].entry.game;
		if (g.titleId == 0)
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(Tr("This game has no ID to save settings under"));
			return;
		}
		m_gameFrom = m_screen == ScreenId::Hub ? ScreenId::Hub : ScreenId::Library;
		m_sidePage = m_page;
		m_gameTitle = g.titleId;
		m_gameName = g.name;
		m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), g.titleId);
		m_gameRef = {SideIndex(), g.titleId, g.path.string()};
		m_gameSavesAt = -100;
		m_savesMessage.clear();
		const auto pages = SettingsPages();
		m_page = pages.empty() ? "graphics" : pages.front().id;
		m_subPage.clear();
		m_onRows = false;
		m_settingRow = 0;
		m_onBar = false;
		m_message.clear();
		Show(ScreenId::Settings);
		m_screenAt = m_now;
	}

	void Shell::GameSettingsClose()
	{
		m_gameTitle = 0;
		m_gameName.clear();
		m_page = m_sidePage;
		m_subPage.clear();
		m_onRows = false;
		m_message.clear();
		if (m_gameFrom == ScreenId::Hub)
			Show(ScreenId::Hub);
		else
			ShowTab(m_gameFrom); // the Library, or Eden's Home
	}

	// A row changed by Left or Right (step) or Cross
	void Shell::ChangeSetting(const Row& row, int step, bool cross)
	{
		const std::string& id = row.id;
		if (id.starts_with("nxextra:"))
		{
			ChangeSwitchExtra(row, step, cross);
			return;
		}
		if (row.dimmed && (row.needsCore || id == "homemenu"))
		{
			m_feedback.Play(ui::Cue::Denied);
			return;
		}
		if (row.kind == Row::Kind::Hold)
		{
			if (cross)
			{
				m_feedback.Play(ui::Cue::Denied); // held, not pressed
				m_message.clear();
			}
			else if (step != 0 && row.options.size() > 1)
			{
				// a held row with choices (which account, which backup): Left and Right choose
				int& which = m_holdChoice[id];
				which = (which + step + (int)row.options.size()) % (int)row.options.size();
				m_feedback.Play(ui::Cue::Focus);
			}
			else if (step != 0)
				m_feedback.Play(ui::Cue::Edge);
			return;
		}
		if (id == "account")
		{
			// the account games start on: the next, or the one before
			const auto accounts = ps5emu::ListAccounts();
			if (accounts.size() < 2 || m_settings.accountPerUser)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			const int count = (int)accounts.size();
			const int next = (row.index + (step < 0 ? -1 : 1) + count) % count;
			ps5emu::SetCurrentAccount(accounts[next].id);
			m_feedback.Play(ui::Cue::Focus);
			// tr: the Wii U account games start on now ({0}: its name)
			Toast(TrF("Games start on {0}'s saves", accounts[next].name));
			return;
		}
		if (id == "cheevosnotices")
		{
			if (row.dimmed)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			ps5cheevos::SetNotices(!row.on);
			m_feedback.Play(ui::Cue::Toggle);
			return;
		}
		if (InGameSettings() && id == "gamebackup")
		{
			if (!cross)
				return;
			std::string error;
			if (ps5saves::StartGameBackup(m_gameRef, error))
			{
				m_savesMessage.clear();
				m_feedback.Play(ui::Cue::Select);
				Toast(TrF("Backing up {0}'s saves…", m_gameName));
			}
			else
			{
				m_savesMessage = error;
				m_feedback.Play(ui::Cue::Denied);
			}
			return;
		}
		if (InGameSettings() && IsSwitch() && id == "nxexport")
		{
			if (!cross || row.dimmed) return;
			const auto view = ps5eden::extras::Read(SwitchExtrasContext());
			std::string error;
			if (!view.error.empty()) error = view.error;
			else if (view.profiles.empty()) error = Tr("Choose a player first, in Settings > Players and saves.");
			else if (ps5saves::StartSwitchExport(m_gameRef, view.profiles[view.profile].id, error)) m_savesMessage = Tr("Copying to USB…");
			if (!error.empty()) Toast(error);
			return;
		}
		if (InGameSettings() && row.setting.empty())
		{
			m_feedback.Play(ui::Cue::Denied); // a row with nothing to change (No saves yet)
			return;
		}
		if (InGameSettings())
		{
			// the game's own value: from its view of the settings, kept as its own
			ps5settings::Launcher view = m_gameView;
			if (ApplyRow(view, row, step, cross))
				KeepGameSetting(row.setting, view);
			return;
		}
		if (id == "usb")
		{
			// Cemu's settings.xml keeps it, as its Emulated USB Devices window does
			const auto device = (ps5usb::Device)row.index;
			ps5usb::SetEnabled(device, !ps5usb::Enabled(device));
			m_feedback.Play(ui::Cue::Toggle);
			return;
		}
		if (row.kind != Row::Kind::Action && row.kind != Row::Kind::Link)
		{
			if (ApplyRow(m_settings, row, step, cross))
				SettingsChanged(id);
			return;
		}
		if (!cross)
			return;
		m_feedback.Play(ui::Cue::Select);
		auto& hand = Handheld(m_side);
		if (id == "newaccount")
		{
			if (ps5emu::ListAccounts().size() >= 12)
			{
				Toast(Tr("The Wii U has twelve accounts at most."));
				return;
			}
			OpenNameKeyboard(Tr("A name for the new account"), "", 10, [this](const std::string& name) {
				if (name.empty())
					return;
				std::string error;
				if (ps5emu::CreateAccount(name, error))
					Toast(TrF("Games start on {0}'s saves", name));
				else
					Toast(error);
			});
		}
		else if (id == "renameaccount")
		{
			const uint32_t current = ps5emu::CurrentAccount();
			std::string name;
			for (const auto& account : ps5emu::ListAccounts())
				if (account.id == current)
					name = account.name;
			OpenNameKeyboard(Tr("A new name"), name, 10, [this, current](const std::string& name) {
				if (!name.empty() && ps5emu::RenameAccount(current, name))
					Toast(TrF("Renamed {0}", name));
			});
		}
		else if (id == "backup")
		{
			std::string error;
			if (ps5saves::StartBackup(error))
			{
				m_savesMessage.clear();
				Toast(Tr("Backing up every side's saves…"));
			}
			else
			{
				m_savesMessage = error;
				m_feedback.Play(ui::Cue::Denied);
			}
		}
		else if (id == "retro-diagnostics" && IsRetro())
			OpenHelp(Tr("Emulator diagnostics"), ps5retro::Diagnostics(SideIndex(), m_settings.retro[SideIndex() - 4].retroBackend));
		else if (id == "retro-library" && IsRetro())
		{
			ps5emu::Game game; game.name = std::string(SideTitle(m_side)) + " " + Tr("Settings");
			LaunchGame(game, Tr("Opening emulator settings"), {760, 240, 400, 560});
		}
		else if (id == "folder")
		{
			m_filesSide = m_side;
			m_pageFrom = ScreenId::Settings;
			FilesOpen(0);
		}
		else if (id == "shares")
			SharesOpen(ScreenId::Settings);
		else if (id == "rescan")
			LookForGames();
		else if (id == "boxartmissing" || id == "boxartall")
		{
			std::vector<std::string> ids;
			for (const Game& game : m_games)
				if (!game.entry.game.gameId.empty() && (id == "boxartall" || game.boxArt.empty()))
					ids.push_back(game.entry.game.gameId);
			if (!m_settings.boxArt)
			{
				// asking for covers turns box art on
				m_settings.boxArt = true;
				ps5boxart::SetEnabled(true);
				SaveSettings();
			}
			ps5boxart::Refetch(BoxSide(m_side), ids, id == "boxartall");
			if (ids.empty())
				m_feedback.Play(ui::Cue::Denied);
		}
		else if (id == "packs")
			ps5packs::Start(); // GitHub's latest community packs, when newer
		else if (id == "mods")
			AddMods(0, {}); // a mod with no rules.txt needs a title ID folder from here
		else if (id == "appupdate")
		{
			// installing shows over the screen (the update sheet); otherwise GitHub is asked again
			const auto state = ps5update::GetStatus().state;
			if (state == ps5update::Status::State::Available)
				ps5update::Install();
			else if (state == ps5update::Status::State::Ready)
				m_update.restartAt = m_now; // as the update sheet's Restart now (Frame)
			else
				ps5update::Check();
		}
		else if (id == "player")
			PlayerOpen(row.index);
		else if (id == "nxkeys" || id == "nxfirmware")
			OpenHelp(row.label, row.help);
		else if (id == "install")
			UpdatesOpen(0, ScreenId::Settings); // Updates and DLC: what the drives have (6.12)
		else if (id == "installcia")
		{
			m_filesSide = System::N3ds;
			m_pageFrom = ScreenId::Settings;
			FilesOpen(2);
		}
		else if (id == "buttons")
		{
			m_pageFrom = ScreenId::Settings;
			MappingOpen();
		}
		else if (id == "artic")
		{
			m_pageFrom = ScreenId::Settings;
			ArticOpen();
		}
		else if (id == "nx-room")
		{
			// offline again
			if (hand.nxRoomHost.empty())
				return;
			hand.nxRoomHost.clear();
			hand.nxRoomName.clear();
			hand.nxRoomPassword.clear();
			SaveSettings();
			Toast(Tr("Switch games play offline"));
		}
		else if (id == "nx-nickname")
		{
			OpenTextKeyboard(Tr("Your nickname in rooms"), hand.nxNickname, 20, false, [this](const std::string& name) {
				if (name.empty())
					return;
				if (!ps5rooms::ValidNickname(name))
				{
					Toast(Tr("A nickname is 4 to 20 letters, digits, spaces, dots, dashes or underscores."));
					return;
				}
				Handheld(System::Switch).nxNickname = name;
				SaveSettings();
				// tr: the nickname shown in Switch rooms ({0})
				Toast(TrF("In rooms you are {0}", name));
			});
		}
		else if (id == "nx-refresh")
			ps5rooms::Refresh();
		else if (id == "nx-public")
		{
			const ps5rooms::Status rooms = ps5rooms::Get();
			if (row.index < 0 || row.index >= (int)rooms.rooms.size())
				return;
			const ps5rooms::Room room = rooms.rooms[row.index];
			if (!room.compatible)
			{
				m_feedback.Play(ui::Cue::Denied);
				Toast(Tr("This room is for another version of Eden."));
				return;
			}
			auto choose = [this, room](const std::string& password) {
				ps5settings::N3ds& nx = Handheld(System::Switch);
				nx.nxRoomHost = room.address;
				nx.nxRoomPort = room.port;
				nx.nxRoomName = room.name;
				nx.nxRoomPassword = password;
				SaveSettings();
				// tr: the room ({0}) Switch games join from now on
				Toast(TrF("Switch games join {0} as they start", room.name));
			};
			if (room.password)
				// tr: the password of a room ({0}: its name)
				OpenTextKeyboard(TrF("{0}'s password", room.name), "", 128, true, [choose](const std::string& password) {
					if (!password.empty())
						choose(password);
				});
			else
				choose("");
		}
		else if (id == "nx-private")
		{
			// its address, its port, then its password (none: empty)
			const std::string host = hand.nxRoomName == hand.nxRoomHost ? hand.nxRoomHost : std::string();
			OpenTextKeyboard(Tr("The room's address"), host, 253, false, [this](const std::string& host) {
				if (host.empty())
					return;
				if (!RoomHost(host))
				{
					Toast(Tr("An address is a name or an IPv4 address: letters, digits, dots and dashes."));
					return;
				}
				OpenTextKeyboard(Tr("Its UDP port"), std::to_string(ps5rooms::kDefaultPort), 5, false, [this, host](const std::string& port) {
					const int number = std::atoi(port.c_str());
					if (number <= 0 || number > 65535 || port.find_first_not_of("0123456789") != std::string::npos)
					{
						Toast(Tr("A port is a number from 1 to 65535."));
						return;
					}
					OpenTextKeyboard(Tr("Its password (empty: none)"), "", 128, true, [this, host, number](const std::string& password) {
						ps5settings::N3ds& nx = Handheld(System::Switch);
						nx.nxRoomHost = host;
						nx.nxRoomPort = number;
						nx.nxRoomName = host;
						nx.nxRoomPassword = password;
						SaveSettings();
						Toast(TrF("Switch games join {0} as they start", host));
					});
				});
			});
		}
		else if (id == "homemenu")
		{
			// the 3DS Home Menu, from the console's files Artic Setup copied
			ps5emu::Game home;
			if (ps5azahar::HomeMenu(hand.region, home))
				LaunchGame(home, Tr("Starting the 3DS"), {760, 240, 400, 400});
			else
				m_feedback.Play(ui::Cue::Denied);
		}
		else if (id == "cheevos")
		{
			// RetroAchievements (6.16): the user name, then the password; only the answer's token is kept
			if (ps5cheevos::Get().busy)
				return;
			OpenTextKeyboard(Tr("Your RetroAchievements user name"), "", 32, false, [this](const std::string& user) {
				if (user.empty())
					return;
				OpenTextKeyboard(TrF("{0}'s RetroAchievements password", user), "", 64, true, [this, user](const std::string& password) {
					if (!password.empty() && ps5cheevos::BeginSignIn(user, password))
						Toast(Tr("Signing in to RetroAchievements…"));
				});
			});
		}
		else if (id == "makeityours")
			WelcomeOpen(true);
		else if (id == "copylogs")
			m_diagnosticsDone[0] = CopyLogsToUsb();
		else if (id == "setupcheck")
			SetupOpen(false);
	}

	void Shell::HoldDone(const std::string& id)
	{
		if (id == "nxextra:removeprofile" && IsSwitch())
		{
			const auto context = SwitchExtrasContext();
			const auto view = ps5eden::extras::Read(context);
			std::string error;
			if (!view.error.empty()) Toast(view.error);
			else if (!view.profiles.empty())
			{
				if (ps5eden::extras::RemoveProfile(context, view.profiles[view.profile].id, error)) Toast(Tr("Player removed: their saves and settings are kept"));
				else Toast(error);
			}
			m_switchExtrasKey.clear();
			return;
		}
		m_feedback.Play(ui::Cue::Hold);
		if (id == "clearcaches")
			m_diagnosticsDone[Is3ds() ? 2 : 1] = ClearShaderCaches(Is3ds());
		else if (id == "clearunpacked")
		{
			const uint64_t bytes = ps5archive::UnpackedBytes();
			const int copies = ps5archive::ClearUnpacked();
			// tr: what clearing the unpacked games did; {1} is the room it made, as in 2.4 GB
			m_unpackedDone = copies ? TrP(copies, "{0} game deleted ({1})", "{0} games deleted ({1})", SizeWords(bytes)) : std::string(Tr("None to delete"));
			m_unpackedAt = -100;
		}
		else if (id == "boxartall")
		{
			ChangeSetting(Row{Row::Kind::Action, "boxartall"}, 0, true);
			// tr: a held row's result: the covers are on their way
			m_message = Tr("Downloading in the background");
		}
		else if (id == "separate" || id == "putback")
		{
			// a folder for each system, or back as before: written down, and made as the app starts again
			if (!m_layout.blocked.empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			const auto request = id == "separate" ? ps5reorganize::Request::Separate : ps5reorganize::Request::PutBack;
			if (!ps5reorganize::Ask(ps5paths::Root(), request))
			{
				m_message = TrF("PS5-ECAM cannot write to {0}. Is the HEN loaded?", ps5paths::Root());
				return;
			}
			ps5log::Line("[reorganize] {} asked for in Settings: as the app starts again", id == "separate" ? "a folder for each system" : "putting back");
			ps5update::Restart();
			m_message = ps5update::GetStatus().message; // back only when the PS5 did not start it again
			if (m_message.empty())
				m_message = Tr("Close PS5-ECAM and start it again from the home screen: the folders move as it starts.");
		}
		else if (id == "goback")
		{
			// the release the last update replaced back in place, and not offered again at start; not while
			// the systems are in folders of their own, which a version from before would not find
			if (!m_layout.separated.empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			std::string error;
			if (ps5update::GoBack(error))
			{
				m_settings.ui.skipVersion = ps5update::Readable(PS5CEMU_VERSION);
				SaveSettings();
				ps5update::Restart();
				error = ps5update::GetStatus().message; // back only when the PS5 did not start it again
			}
			m_message = error;
		}
		else if (id == "resetcontrols")
		{
			ps5azahar::ResetControls(Handheld(m_side));
			if (m_side == System::Switch)
				Handheld(m_side).nxDeadzone = ps5settings::N3ds{}.nxDeadzone;
			SaveSettings();
			// tr: what a held row says once it has done its work
			m_message = TrC("finished", "Done");
		}
		else if (id == "deleteaccount")
		{
			const uint32_t current = ps5emu::CurrentAccount();
			std::vector<ps5emu::WiiuAccount> others;
			for (const auto& account : ps5emu::ListAccounts())
				if (account.id != current)
					others.push_back(account);
			const int which = m_holdChoice["deleteaccount"];
			if (which < (int)others.size())
			{
				std::string error;
				if (ps5emu::DeleteAccount(others[which].id, error))
					Toast(TrF("{0} is deleted; its saves stay", others[which].name));
				else
					Toast(error);
				m_holdChoice["deleteaccount"] = 0;
			}
		}
		else if (id == "restore")
		{
			const int which = m_holdChoice["restore"];
			std::string error;
			if (which < (int)m_backups.size() && ps5saves::StartRestore(m_backups[which], error))
			{
				m_savesMessage.clear();
				Toast(TrF("Restoring the saves of {0}…", m_backups[which].when));
			}
			else if (!error.empty())
				Toast(error);
		}
		else if (id == "cheevos")
		{
			ps5cheevos::SignOut();
			Toast(Tr("Signed out of RetroAchievements"));
		}
		else if (id == "nximport" && IsSwitch())
		{
			const auto view = ps5eden::extras::Read(SwitchExtrasContext());
			const int selected = m_holdChoice["nximport"];
			std::string error;
			if (!view.error.empty()) error = view.error;
			else if (view.profiles.empty()) error = Tr("Choose a player first, in Settings > Players and saves.");
			else if (selected >= 0 && selected < (int)m_switchImports.size() &&
				ps5saves::StartSwitchImport(m_gameRef, view.profiles[view.profile].id, m_switchImports[selected].folder, error, m_switchImports[selected].accountSaveId)) m_savesMessage = Tr("Taking the save…");
			if (!error.empty()) Toast(error);
		}
		else if (id == "gamerestore")
		{
			const int which = m_holdChoice["gamerestore"];
			std::string error;
			if (which < (int)m_gameBackups.size() && ps5saves::StartGameRestore(m_gameBackups[which], m_gameRef, error))
			{
				m_savesMessage.clear();
				Toast(TrF("Restoring {0}'s saves of {1}…", m_gameName, m_gameBackups[which].when));
			}
			else if (!error.empty())
				Toast(error);
		}
		else if (id.starts_with("gamesave:"))
		{
			const size_t which = (size_t)std::strtoul(id.c_str() + 9, nullptr, 10);
			if (which < m_gameSaves.size())
			{
				std::string error;
				const std::string label = SaveLabel(m_gameSaves[which]);
				if (!ps5saves::MoveAside(m_gameSaves[which], error).empty())
					// tr: one of a game's saves moved out of the way ({0}: which, as its row names it)
					Toast(TrF("{0}: moved to saves-before", label));
				else
					Toast(error);
				m_gameSavesAt = -100;
			}
		}
		else if (id == "resetgame")
		{
			m_settings.games.erase(ps5settings::GameKey(ps5launcher::SideName(m_side), m_gameTitle));
			SaveSettings();
			m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), m_gameTitle);
			m_message = Tr("Done: back to its side's settings");
		}
	}

	void Shell::SettingsUpdate(const ui::Press& press, const ui::Actions& actions)
	{
		const auto pages = SettingsPages();
		if (pages.empty())
		{
			if (InGameSettings() && press.button == Button::Circle)
				GameSettingsClose();
			return;
		}
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		// a › page is open only while its page's rows have the focus, and while it has rows
		if (!m_onRows)
			m_subPage.clear();
		auto rows = SettingRows(m_page);
		if (!m_subPage.empty() && rows.empty())
		{
			m_subPage.clear();
			m_settingRow = m_subFrom;
			rows = SettingRows(m_page);
		}
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		// what is held to confirm
		if (m_onRows && !rows.empty() && rows[m_settingRow].kind == Row::Kind::Hold)
		{
			const float seconds = m_settings.ui.holdMs / 1000.0f;
			m_holdProgress = actions.Held(Button::Cross) ? actions.HeldFor(Button::Cross) / seconds : 0.0f;
			if (m_holdProgress >= 1 && !m_holdFired)
			{
				m_holdFired = true;
				HoldDone(rows[m_settingRow].id);
			}
			if (!actions.Held(Button::Cross))
				m_holdFired = false;
		}
		else
			m_holdProgress = 0;
		const Button b = press.button;
		if (b == Button::Count)
			return;
		if (!m_onRows)
		{
			switch (b)
			{
			case Button::Up:
				if (page == 0)
					m_feedback.Play(ui::Cue::Edge, press.repeat); // a dialog: the bar is under its dim
				else
				{
					m_page = pages[page - 1].id;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			case Button::Down:
				if (page + 1 >= (int)pages.size())
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					m_page = pages[page + 1].id;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			case Button::L2:
			case Button::R2:
				m_page = b == Button::L2 ? pages.front().id : pages.back().id;
				m_feedback.Play(ui::Cue::Focus);
				break;
			case Button::Right:
			case Button::Cross:
				if (!rows.empty())
				{
					m_onRows = true;
					m_settingRow = 0;
					m_message.clear();
					m_feedback.Play(ui::Cue::Select);
				}
				else
					m_feedback.Play(ui::Cue::Edge);
				break;
			case Button::Circle:
				m_feedback.Play(ui::Cue::Back);
				if (InGameSettings())
					GameSettingsClose();
				else
				{
					ShowTab(ScreenId::Library);
					m_barFocus = kSides;
				}
				break;
			default: break;
			}
			return;
		}
		if (rows.empty())
		{
			m_onRows = false;
			return;
		}
		const Row& row = rows[m_settingRow];
		switch (b)
		{
		case Button::Circle:
			// a › page: back to its row; else back to the pages
			if (!m_subPage.empty())
				CloseSubPage();
			else
			{
				m_onRows = false;
				m_feedback.Play(ui::Cue::Back);
			}
			break;
		case Button::Up:
		case Button::Down:
		{
			const int next = m_settingRow + (b == Button::Down ? 1 : -1);
			if (next < 0 || next >= (int)rows.size())
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_settingRow = next;
				m_message.clear();
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Triangle: OpenHelp(row.label, row.help); break;
		case Button::Square:
			if (InGameSettings())
				ResetGameSetting(row);
			break;
		case Button::Left:
			if (row.kind == Row::Kind::Link || row.kind == Row::Kind::Action || row.kind == Row::Kind::Hold)
			{
				if (!m_subPage.empty())
					CloseSubPage();
				else
				{
					m_onRows = false;
					m_feedback.Play(ui::Cue::Back);
				}
			}
			else
				ChangeSetting(row, -1, false);
			break;
		case Button::Right:
		case Button::Cross:
			// a › row opens its page, Right as Cross
			if (!row.opens.empty())
				OpenSubPage(row);
			else if (b == Button::Right)
				ChangeSetting(row, 1, false);
			else
				ChangeSetting(row, 0, true);
			break;
		case Button::L2:
		case Button::R2:
			m_settingRow = b == Button::L2 ? 0 : (int)rows.size() - 1;
			m_feedback.Play(ui::Cue::Focus);
			break;
		default: break;
		}
	}

	// The remote touch screen's card (6.5): the QR code on white, the address in large type, the state in
	// a chip, one line of what to know
	void Shell::DrawRemoteScreen(Canvas& canvas, const Box& card)
	{
		const ps5stream::State state = ps5stream::Current();
		canvas.Rect(card, kRadiusCard, Surface());
		canvas.Ring(card, kRadiusCard, 1.5f, kGlassEdge);
		const float pad = 36, qr = 300;
		float tx = card.x + pad;
		if (!state.address.empty())
		{
			auto code = m_qrCodes.find(state.address);
			if (code == m_qrCodes.end())
				code = m_qrCodes.emplace(state.address, ui::QrCode(state.address)).first;
			ui::DrawQr(canvas, code->second, {card.x + pad, card.y + (card.h - qr) * 0.5f, qr, qr}, 18);
			tx += qr + 44;
		}
		const float tw = card.Right() - pad - tx;
		const bool working = state.kind == ps5stream::State::Kind::Waiting || state.kind == ps5stream::State::Kind::Showing;
		const ui::TextBlock overline = m_fonts.Layout(Style(kOverlineStyle), working ? Tr("Scan it, or open") : Tr("Remote touch screen"), tw, 1);
		const ui::TextBlock address = m_fonts.Layout(Style({46, ui::Weight::SemiBold, 1.1f}), state.address, tw, 1);
		const std::string what = ps5stream::Describe(state.kind);
		const ui::TextStyle chipStyle = Style({22, ui::Weight::Medium, 1.0f});
		const float chipW = m_fonts.Width(chipStyle, what) + 60, chipH = 44;
		const ui::TextBlock note = m_fonts.Layout(Style({21, ui::Weight::Regular, 1.5f}),
			working ? Tr("Same network as the PS5. Nothing to install: the browser is enough. Sound stays on the PS5. Any browser on the network can "
					  "see it and touch.") :
					  Tr("Turn it off and on again once that is fixed."),
			std::min(tw, 660.0f), 3);
		const float height = overline.height + 14 + (address.lines.empty() ? 0 : address.height + 22) + chipH + 22 + note.height;
		float y = card.y + (card.h - height) * 0.5f;
		canvas.Text(overline, tx, y, Tertiary());
		y += overline.height + 14;
		if (!state.address.empty())
		{
			canvas.Text(address, tx, y, kText);
			y += address.height + 22;
		}
		const uint32_t dot = state.kind == ps5stream::State::Kind::Showing ? kGood : working ? kWarn : kBad;
		canvas.Rect({tx, y, chipW, chipH}, chipH * 0.5f, Wash(0.08f));
		canvas.Rect({tx + 18, y + chipH * 0.5f - 11, 22, 22}, 11, ui::SetAlpha(dot, 0.2f));
		canvas.Rect({tx + 23, y + chipH * 0.5f - 6, 12, 12}, 6, dot);
		const ui::TextBlock chip = m_fonts.Layout(chipStyle, what, chipW, 1);
		canvas.Text(chip, tx + 48, y + (chipH - chip.height) * 0.5f, kText);
		y += chipH + 22;
		canvas.Text(note, tx, y, Secondary());
	}

	// About's card (6.5): the developer's PayPal.me as a QR code on white, the address under it in words,
	// one line saying what it is; never shown anywhere else
	void Shell::DrawSupport(Canvas& canvas, const Box& card)
	{
		static constexpr const char* kSupport = "https://www.paypal.com/paypalme/empthe";
		canvas.Rect(card, kRadiusCard, Surface());
		canvas.Ring(card, kRadiusCard, 1.5f, kGlassEdge);
		const float pad = 32, qr = std::min(card.w - 2 * pad, 260.0f);
		float y = card.y + pad;
		canvas.Text(Style(kOverlineStyle), card.x + pad, y, Tr("Support PS5-ECAM"), Accent(), card.w - 2 * pad, 1);
		y += 40;
		auto code = m_qrCodes.find(kSupport);
		if (code == m_qrCodes.end())
			code = m_qrCodes.emplace(kSupport, ui::QrCode(kSupport)).first;
		ui::DrawQr(canvas, code->second, {card.CentreX() - qr * 0.5f, y, qr, qr}, 16);
		y += qr + 20;
		const ui::TextBlock address = canvas.Text(Style({26, ui::Weight::SemiBold, 1.1f}), card.x + pad, y, "paypal.me/empthe", kText, card.w - 2 * pad, 1,
			ui::Align::Centre);
		y += address.height + 14;
		canvas.Text(Style({20, ui::Weight::Regular, 1.45f}), card.x + pad, y,
			Tr("PS5-ECAM is free. If it brings your games back, you can support its work here."), Secondary(), card.w - 2 * pad, 4,
			ui::Align::Centre);
	}

	void Shell::SettingsDraw(Canvas& canvas)
	{
		// a dialog over the screen it was opened from, on every side and in every theme (6.5): that screen as
		// it was, dimmed, taking no input and asking for no focus
		m_behind = true;
		if (InGameSettings() && m_gameFrom == ScreenId::Hub)
			HubDraw(canvas);
		else
			LibraryDraw(canvas);
		m_behind = false;
		const float appear = Enter(0);
		Scrim(canvas, appear);
		const auto pages = SettingsPages();
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;

		// the dialog: rising a little as it appears
		const Box dialog = SettingsDialog().Offset(0, (1 - appear) * 24);
		canvas.PushAlpha(appear);
		canvas.Shadow(dialog, kRadiusSheet, 40, 120, 0xb3000000);
		canvas.Rect(dialog, kRadiusSheet, m_settings.ui.highContrast ? kInk2 : ui::SetAlpha(ui::Mix(kInk1, kInk2, 0.5f), 0.97f));
		canvas.Ring(dialog, kRadiusSheet, 1.5f, kGlassEdge);
		const float navW = 380;
		canvas.PushClip(dialog, kRadiusSheet);
		canvas.Rect({dialog.x, dialog.y, navW, dialog.h}, 0, Wash(0.03f));
		canvas.Rect({dialog.x + navW, dialog.y, 1.5f, dialog.h}, 0, kLine);
		canvas.PopClip();

		// the pages down its left: one short list, each with its icon; above them whose settings they are
		const float navX = dialog.x + 20, navTop = dialog.y + 30;
		// tr: above Settings' pages: whose settings they are ({0}: Wii U, Nintendo 3DS or Nintendo DS)
		const std::string head = InGameSettings() ? m_gameName : TrF("{0} settings", SystemName(m_side));
		canvas.Text(Style(kOverlineStyle), navX + 22, navTop, head, Tertiary(), navW - 64, 1);
		const float railTop = navTop + 40, railBottom = dialog.Bottom() - 20, step = 56;
		const float listHeight = pages.size() * step;
		m_railScroll.target = std::clamp(page * step - (railBottom - railTop) * 0.5f, 0.0f, std::max(0.0f, listHeight - (railBottom - railTop)));
		m_railScroll.omega = kScrollOmega;
		m_railScroll.Update(m_dt);
		canvas.PushClip({dialog.x, railTop - 6, navW, railBottom - railTop + 6});
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const float ry = railTop + i * step - m_railScroll.value;
			const bool selected = i == page;
			const Box item{navX, ry, navW - 40, step - 4};
			if (selected)
			{
				canvas.Rect(item, 14, m_onRows ? Wash(0.06f) : Surface2());
				if (!m_onRows)
					Focus(item, 14);
			}
			canvas.Draw(pages[i].icon, {item.x + 22, item.CentreY() - 14, 28, 28}, selected ? Accent() : Secondary());
			// the pages' names all one size (#76): a long one takes two lines rather than a smaller size
			ui::TextStyle style = Style({24, selected ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f});
			style.minScale = 1;
			const ui::TextBlock text = m_fonts.Layout(style, pages[i].title, item.w - 86, 2);
			canvas.Text(text, item.x + 68, item.CentreY() - text.height * 0.5f, selected ? kText : Secondary());
		}
		canvas.PopClip();

		// the page: its title, what it is for, its rows filling the rest of the dialog; a › page's, its
		// row's label and line, the page it is on still marked in the rail
		const SettingsPage& current = pages[page];
		const bool sub = !m_subPage.empty();
		const float px = dialog.x + navW + 48, pw = dialog.Right() - 48 - px;
		float ty = dialog.y + 36;
		canvas.Text(Style(kTitleStyle), px, ty, sub ? m_subTitle : current.title, kText, pw, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), px, ty + 62, sub ? current.title + "  ›  " + m_subLine : current.subtitle, Secondary(), pw, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = ty + 112;
		const float rowH = 96;
		// rows past the dialog's height scroll with the focus
		const float available = dialog.Bottom() - 28 - ry;
		const float scroll = std::max(0.0f, (m_settingRow + 1) * (rowH + 12) - available);
		canvas.PushClip({px - 20, ry - 12, pw + 40, available + 24});
		const float labelScale = LabelScale(rows, pw); // every row's label the same size (#76)
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{px, ry + i * (rowH + 12) - scroll, pw, rowH};
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused, labelScale);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		// the remote touch screen, while it is on: the code to scan, the address, what is happening (its › page's,
		// on Controls or the Wii U's System)
		const float under = ry + rows.size() * (rowH + 12) - scroll;
		const std::string& shown = sub ? m_subPage : m_page;
		if (shown == "remote" && m_settings.remoteScreen && !InGameSettings())
			DrawRemoteScreen(canvas, {px, under + 18, pw, 372});
		else
		{
			// what the page (or the › page: the Wii U's USB devices, the remote touch screen) says under its rows,
			// in as many lines as the dialog has room for; About's beside its support card
			const std::string panel = PanelText(shown);
			float textW = pw;
			if (m_page == "about")
			{
				const float cardW = 380;
				DrawSupport(canvas, {px + pw - cardW, under, cardW, std::min(560.0f, dialog.Bottom() - 28 - under)});
				textW = pw - cardW - 40;
			}
			if (!panel.empty())
			{
				const ui::TextStyle style = Style({m_page == "about" ? 20.0f : 22.0f, ui::Weight::Regular, 1.45f});
				const float top = under + (rows.empty() ? 0 : 8);
				const int lines = std::max(1, (int)((dialog.Bottom() - 28 - top) / (style.size * style.lineHeight)));
				canvas.Text(style, px, top, panel, Secondary(), textW, lines);
			}
		}
		canvas.PopClip();
		canvas.PopAlpha();
	}
}
