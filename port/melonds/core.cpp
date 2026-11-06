// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: melonDS's core on the PS5 (melonds.h), built in melonDS's tree against its core
// (tools/build-melonds.sh) and linked into the app with Cemu and Azahar.
//
//  - a DS in DS mode, started straight into the game (direct boot) with melonDS's own BIOS and
//    firmware (FreeBIOS and one it makes, with the PS5's user name and the launcher's language), or
//    a DS's own dumps from Root()/bios;
//  - the CPUs on melonDS's JIT, its code in the console's executable direct memory (patches/melonds),
//    or on its interpreter where there is none; the 3D on its software renderer, on a thread of its own;
//  - the emulation, and the screens on VideoOut (screens.h), on a thread of their own: a DS frame for
//    each frame shown, VideoOut's flips pacing it; the game's loop on the main thread reads the
//    DualSense (input.cpp) and the port's shortcuts (ps5/pad.h), and applies what the in-game menu
//    (app/ingame3ds.h, drawn in the screens' frames) changes, as Azahar's does (port/azahar/core.cpp).

#include "melonds.h"
#include "frontend.h"
#include "archive.h"
#include "library.h"
#include "screens.h"
#include "slot2.h"
#include "microphone.h"
#include "../app/shots.h"
#include "../app/achievements.h"
#include "../app/frame_capture.h"
#include "../app/capture_session.h"
#include "../app/saves.h"
#include "../app/boxart.h"
#include "../app/gameinfo.h"
#include "../app/ingame3ds.h"
#include "../app/lang.h"
#include "../azahar/controls.h"
#include "../ps5/display.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../ps5/privilege.h"
#include "../stream/stream.h"

#include "ARCodeFile.h"
#include "Args.h"
#include "GPU3D_Soft.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "Platform.h"
#include "SPI.h"
#include "SPI_Firmware.h"
#include "Savestate.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <utility>
#include <variant>
#include <vector>

extern "C" int32_t sceUserServiceGetUserName(int32_t userId, char* name, size_t size);

namespace fs = std::filesystem;

namespace ps5melonds
{
	namespace
	{
		using melonDS::u8;
		using melonDS::u32;

		constexpr int kStateSlots = ps5ingame3ds::kStateSlots;
		// a DS cartridge's most (4 Gbit), with room: an archive's ROM claimed larger is not one
		constexpr uint64_t kLargestRom = 1ull << 30;

		melonDS::NDS* s_nds = nullptr;
		std::thread s_emulation;
		std::atomic_bool s_stop = false;
		std::atomic_bool s_running = false;
		std::atomic<int> s_stopReason{-1}; // melonDS stopped the console itself (Stopped)
		bool s_coreTouched = false;			// LaunchGame took VideoOut
		ps5settings::N3ds s_settings;		// the game's loop's: as the menu leaves them
		ps5settings::N3ds s_launched;	// what the game started with, or what the menu last saved
		std::string s_name;
		std::string s_stem;		  // the game's file's name without its extension: its save's, states' and cheats'
		std::string s_folder;	  // the folder the game's file is in
		uint64_t s_titleId = 0;
		bool s_jit = false;
		// Opt-in diagnostic: an existing melonds/capture-frames marker. Single emulation-thread
		// producer; the game loop writes only after joining it, so gameplay never writes capture files.
		ps5capture::Frames s_capture;
		bool s_captureEnabled = false;
		std::map<std::string, int> s_captureSettings;
		std::string s_captureGame;

		// What the emulation reads before each frame; the game's loop changes it
		struct View
		{
			int layout = 2;
			bool swapped = false;
			int rotation = 0; // the DS turned this many quarter turns clockwise (screens::Place)
			bool gap = false; // the screens held apart (screens::kGap)
			int filter = 0;
			int speedLimit = 100; // percent of the DS's; 0: none
			bool lidClosed = false;
		};
		std::mutex s_viewMutex;
		View s_view;
		std::atomic<bool> s_bottomShown{true};

		// the game's loop's requests, for the emulation to make between two frames: a save state saved
		// to slot n (n) or loaded from it (-n), or the last load undone (kUndoRequest), cheats turned on or
		// off (by their index)
		constexpr int kUndoRequest = -1000;
		std::atomic<int> s_stateRequest{0};
		// the emulation's: the game as it was just before the last load, for Undo the last load
		std::unique_ptr<melonDS::Savestate> s_undo;
		// Resume where you left off: the state saved as the game is quit, in a slot the menu does not show
		constexpr int kResumeSlot = ps5saves::kResumeSlot;
		bool s_resume = false;				   // the game goes on from it as it starts (SetResume)
		std::atomic<bool> s_quitAfterState{false}; // quit once the state saved for it is made
		std::mutex s_cheatMutex;
		std::vector<int> s_cheatToggles;

		// the emulation's: the game's cheats
		std::unique_ptr<melonDS::ARCodeFile> s_cheats;

		// The emulation's, for rewind (#54), while it has an input: a save state every kRewindEvery DS
		// frames (half a second), the last kRewindStates kept in memory (15 s, about 6 MB each). Held, the
		// newest is loaded every kRewindStep frames shown and taken off, one frame run for its picture,
		// with no sound: the game runs back about four times as fast as it ran, and goes on from where
		// it is let go
		constexpr int kRewindEvery = 30;
		constexpr size_t kRewindStates = 30;
		constexpr int kRewindStep = 8;
		std::deque<std::vector<u8>> s_rewind;
		std::vector<u8> s_rewindScratch; // a state saved into, then copied at its length
		int s_rewindDue = kRewindEvery;	 // DS frames until the next snapshot
		int s_rewindWait = 0;			 // frames shown until the next step back
		bool s_rewinding = false;
		bool s_rewound = false; // a step back taken since rewind was held

		// the game's save, as it last changed, for the game's loop to write
		std::mutex s_saveMutex;
		std::vector<u8> s_save;
		bool s_saveDirty = false;
		uint64_t s_saveChangedAt = 0; // sceKernelGetProcessTime
		std::mutex s_gbaSaveMutex;
		std::vector<u8> s_gbaSave;
		std::string s_gbaSavePath;
		bool s_gbaDirty = false;
		uint64_t s_gbaChangedAt = 0;

		// the emulation's frames, for the overlay's numbers
		std::atomic<uint32_t> s_framesEmulated{0}, s_framesShown{0};

		// RetroAchievements (app/achievements.h): signed in, the game's set is checked after each frame
		bool s_cheevos = false;

		// The DS's memory as rcheevos' DS map has it: 0 to 0x3FFFFF the main RAM (4 MiB at 0x02000000; a
		// DSi's more is not there), 0x1000000 to 0x1003FFF the ARM9's data TCM. From the emulation's thread,
		// between two frames.
		uint32_t ReadDsMemory(uint32_t address, uint8_t* buffer, uint32_t size)
		{
			melonDS::NDS* nds = s_nds;
			if (!nds)
				return 0;
			if (address < 0x400000)
			{
				const uint32_t count = std::min<uint32_t>(size, 0x400000 - address);
				std::memcpy(buffer, nds->MainRAM + address, count);
				return count;
			}
			if (address >= 0x1000000 && address < 0x1004000)
			{
				const uint32_t offset = address - 0x1000000;
				const uint32_t count = std::min<uint32_t>(size, 0x4000 - offset);
				std::memcpy(buffer, nds->ARM9.DTCM + offset, count);
				return count;
			}
			return 0;
		}

		std::string Folder(const char* name)
		{
			return Root() + "/" + name;
		}

		std::string SavePath()
		{
			return Folder("saves") + "/" + s_stem + ".sav";
		}

		std::string StatePath(int slot)
		{
			return fmt::format("{}/{}.ml{}", Folder("states"), s_stem, slot);
		}

		// melonDS's own place for the game's cheats, unless a .mch is beside the game
		std::string CheatPath()
		{
			const std::string own = Folder("cheats") + "/" + s_stem + ".mch";
			std::error_code ec;
			const std::string beside = s_folder + "/" + s_stem + ".mch";
			if (!fs::exists(own, ec) && fs::exists(beside, ec))
				return beside;
			return own;
		}

		std::vector<u8> ReadFile(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			return std::vector<u8>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}

		bool WriteFile(const std::string& path, const u8* data, size_t length)
		{
			const std::string temporary = path + ".tmp";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				out.write((const char*)data, (std::streamsize)length);
				if (!out)
					return false;
			}
			std::error_code ec;
			fs::rename(temporary, path, ec);
			return !ec;
		}

		// -- the console's parts -----------------------------------------------------------------

		// A DS's own BIOS from Root()/bios, when both are there at their sizes
		template<size_t N>
		std::unique_ptr<std::array<u8, N>> ReadBios(const char* name)
		{
			const std::vector<u8> data = ReadFile(Folder("bios") + "/" + name);
			if (data.size() != N)
				return nullptr;
			auto bios = std::make_unique<std::array<u8, N>>();
			std::copy(data.begin(), data.end(), bios->begin());
			return bios;
		}

		// UTF-8 into the firmware's UTF-16 name, 10 characters at most
		size_t Utf16(const std::string& text, char16_t* out, size_t most)
		{
			size_t count = 0;
			for (size_t i = 0; i < text.size() && count < most;)
			{
				const unsigned char c = text[i];
				uint32_t code = c;
				int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
				if (extra)
					code = c & (0x3F >> extra);
				i++;
				for (; extra > 0 && i < text.size(); extra--, i++)
					code = code << 6 | (text[i] & 0x3F);
				out[count++] = code < 0x10000 ? (char16_t)code : u'?';
			}
			return count;
		}

		// The firmware a DS game sees: a DS's own (firmware.bin) as it is, with the launcher's language;
		// else one melonDS makes, with the PS5 user's name as the DS's
		melonDS::Firmware MakeFirmware(const ps5settings::N3ds& settings, bool own, std::string& about)
		{
			melonDS::Firmware firmware(0);
			bool loaded = false;
			if (own)
				if (melonDS::Platform::FileHandle* file = melonDS::Platform::OpenFile(Folder("bios") + "/firmware.bin", melonDS::Platform::Read))
				{
					melonDS::Firmware dumped(file);
					melonDS::Platform::CloseFile(file);
					if (dumped.Buffer())
					{
						firmware = std::move(dumped);
						loaded = true;
					}
				}
			auto& user = firmware.GetEffectiveUserData();
			if (!loaded)
			{
				char name[32] = {};
				if (sceUserServiceGetUserName(ps5pad::UserId(0), name, sizeof(name) - 1) != 0 || !name[0])
					std::snprintf(name, sizeof(name), "PS5");
				std::fill(std::begin(user.Nickname), std::end(user.Nickname), u'\0');
				user.NameLength = (melonDS::u16)Utf16(name, user.Nickname, 10);
			}
			// the 3DS's languages are the DS's up to Chinese (6); Korean and the rest have none on a DS
			if (settings.language >= 0)
			{
				const int language = settings.language <= 6 ? settings.language : 1;
				user.Settings &= ~melonDS::Firmware::Language::Reserved;
				user.Settings |= (melonDS::u16)language;
			}
			firmware.UpdateChecksums();
			about = loaded ? "the DS's own firmware" : "melonDS's firmware";
			return firmware;
		}

		// The game's save: Root()/saves, or the first time, a .sav beside the game (taken, not moved)
		std::vector<u8> ReadSave()
		{
			std::error_code ec;
			if (fs::exists(SavePath(), ec))
				return ReadFile(SavePath());
			const std::string beside = s_folder + "/" + s_stem + ".sav";
			if (fs::exists(beside, ec))
			{
				ps5log::Line("[ds] the save beside the game is taken: {}", beside);
				return ReadFile(beside);
			}
			return {};
		}

		void FlushSave(bool now)
		{
			{
				std::lock_guard lock(s_gbaSaveMutex);
				if (s_gbaDirty && !s_gbaSavePath.empty() && (now || sceKernelGetProcessTime() - s_gbaChangedAt >= 1000000))
				{
					std::error_code ec;
					fs::create_directories(fs::path(s_gbaSavePath).parent_path(), ec);
					if (!ec && WriteFile(s_gbaSavePath, s_gbaSave.data(), s_gbaSave.size())) s_gbaDirty = false;
					else ps5log::Line("[ds] Slot-2 save could not be written: {}", s_gbaSavePath);
				}
			}
			std::vector<u8> data;
			{
				std::lock_guard lock(s_saveMutex);
				// once the game has stopped writing for a second (a save is written in many pieces)
				if (!s_saveDirty || (!now && sceKernelGetProcessTime() - s_saveChangedAt < 1000000))
					return;
				data = s_save;
				s_saveDirty = false;
			}
			if (!WriteFile(SavePath(), data.data(), data.size()))
				ps5log::Line("[ds] the save could not be written: {}", SavePath());
		}

		// -- the menu ------------------------------------------------------------------------------

		// The menu's save state slots: when each was saved
		void ShowStateSlots()
		{
			std::vector<std::string> times(kStateSlots);
			for (int slot = 1; slot <= kStateSlots; slot++)
			{
				struct stat info{};
				if (stat(StatePath(slot).c_str(), &info) != 0)
					continue;
				const std::time_t time = info.st_mtime;
				char text[32] = "saved";
				if (const std::tm* local = std::localtime(&time))
					std::strftime(text, sizeof(text), "%b %d, %H:%M", local);
				times[slot - 1] = text;
			}
			ps5ingame3ds::SetStateSlots(times);
		}

		// The game's cheats as the menu lists them, in the order melonDS keeps them
		std::vector<melonDS::ARCode*> CheatList()
		{
			std::vector<melonDS::ARCode*> codes;
			if (!s_cheats || s_cheats->Error)
				return codes;
			for (auto& item : s_cheats->RootCat.Children)
			{
				if (auto* category = std::get_if<melonDS::ARCodeCat>(&item))
				{
					for (auto& child : category->Children)
						if (auto* code = std::get_if<melonDS::ARCode>(&child))
							codes.push_back(code);
				}
				else if (auto* code = std::get_if<melonDS::ARCode>(&item))
					codes.push_back(code);
			}
			return codes;
		}

		void ShowCheats()
		{
			std::vector<std::pair<std::string, bool>> cheats;
			for (const melonDS::ARCode* code : CheatList())
				cheats.emplace_back(code->Name, code->Enabled);
			ps5ingame3ds::SetExtras({}, cheats);
		}

		// The menu's Achievements: the set as RetroAchievements has it now, the earned first
		void ShowAchievements()
		{
			std::vector<ps5cheevos::Item> items;
			std::string summary;
			ps5cheevos::List(items, summary);
			std::vector<ps5ingame3ds::Achievement> shown;
			for (const ps5cheevos::Item& item : items)
			{
				ps5ingame3ds::Achievement a{item.title, item.description, item.points, item.unlocked, {}};
				const std::time_t when = (std::time_t)item.unlockedAt;
				std::tm local{};
				if (item.unlocked && when > 0 && localtime_r(&when, &local))
					a.when = ps5lang::ShortDateTime(local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
				shown.push_back(std::move(a));
			}
			ps5ingame3ds::SetAchievements(true, summary, shown);
		}

		ps5ingame3ds::Settings MenuSettings()
		{
			View view;
			{
				std::lock_guard lock(s_viewMutex);
				view = s_view;
			}
			ps5ingame3ds::Settings menu;
			menu.layout = s_settings.layout;
			menu.swapScreens = view.swapped;
			menu.resolution = s_settings.resolution;
			menu.textureFilter = s_settings.textureFilter;
			menu.volume = s_settings.volume;
			menu.performance = s_settings.performance;
			menu.cpuClock = s_settings.cpuClock;
			menu.speedLimit = view.speedLimit;
			menu.motion = s_settings.motion;
			menu.deadzone = s_settings.deadzone;
			menu.aOnCircle = ps5azahar::MappedInput(s_settings, ps5azahar::Button::A) == ps5emu::PadInput::Circle;
			menu.border = s_settings.border;
			menu.screenFilter = s_settings.dsFilter;
			menu.rotation = s_settings.dsRotation;
			menu.screenGap = s_settings.screenGap;
			menu.hiddenTouch = s_settings.hiddenTouch;
			menu.lidClosed = view.lidClosed;
			return menu;
		}

		void ShowInMenu()
		{
			ps5ingame3ds::Start(s_name, s_titleId, MenuSettings(), ps5ingame3ds::Console::Nds);
		}

		// Where the screens are, for the border, the touch screen and the emulation
		void PublishScreens()
		{
			View view;
			{
				std::lock_guard lock(s_viewMutex);
				view = s_view;
			}
			const float width = (float)screens::Width(), height = (float)screens::Height();
			screens::Rect top, bottom;
			screens::Place(view.layout, view.swapped, view.rotation, view.gap, width, height, top, bottom);
			std::vector<ps5ingame3ds::ScreenRect> shown;
			for (const screens::Rect& r : {top, bottom})
				if (r.Shown())
					shown.push_back({r.left, r.top, r.right, r.bottom});
			ps5ingame3ds::SetScreens(shown, width, height);
			s_bottomShown = bottom.Shown();
		}

		// What the menu changed, while the game runs; kept for the next games in the DS side's settings,
		// or the game's own when it has some
		void ApplyMenu(const ps5ingame3ds::Settings& menu)
		{
			using ps5azahar::Button;
			using ps5emu::PadInput;
			const bool aOnCircle = ps5azahar::MappedInput(s_settings, Button::A) == PadInput::Circle;
			const bool controls = menu.deadzone != s_settings.deadzone || menu.aOnCircle != aOnCircle;
			s_settings.layout = menu.layout;
			s_settings.volume = menu.volume;
			s_settings.performance = menu.performance;
			s_settings.deadzone = menu.deadzone;
			s_settings.dsFilter = menu.screenFilter;
			s_settings.dsRotation = screens::QuarterTurns(menu.rotation);
			input::SetRotation(s_settings.dsRotation);
			s_settings.screenGap = menu.screenGap;
			s_settings.hiddenTouch = menu.hiddenTouch;
			input::SetHiddenTouch(menu.hiddenTouch);
			if (menu.border != s_settings.border)
			{
				s_settings.border = menu.border;
				ps5ingame3ds::LoadBorder(menu.border);
			}
			if (menu.aOnCircle != aOnCircle)
			{
				// A, B, X and Y: on Circle, Cross, Triangle and Square, or on Cross, Circle, Square and Triangle
				ps5azahar::SetMapping(s_settings, (size_t)Button::A, menu.aOnCircle ? PadInput::Circle : PadInput::Cross, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::B, menu.aOnCircle ? PadInput::Cross : PadInput::Circle, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::X, menu.aOnCircle ? PadInput::Triangle : PadInput::Square, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::Y, menu.aOnCircle ? PadInput::Square : PadInput::Triangle, true);
			}
			{
				std::lock_guard lock(s_viewMutex);
				s_view.layout = menu.layout;
				s_view.swapped = menu.swapScreens;
				s_view.rotation = s_settings.dsRotation;
				s_view.gap = menu.screenGap;
				s_view.filter = menu.screenFilter;
				s_view.lidClosed = menu.lidClosed;
				if (menu.speedLimit != s_view.speedLimit)
					ps5log::Line("[ds] speed limit {}", menu.speedLimit ? fmt::format("{}%", menu.speedLimit) : "none");
				s_view.speedLimit = menu.speedLimit;
			}
			PublishScreens();
			sound::SetVolume(menu.volume);
			if (controls)
				input::Configure(s_settings);

			// into the game's own settings when it has some, else the side's
			ps5settings::Launcher before, after;
			before.nds = s_launched;
			after.nds = s_settings;
			ps5settings::SaveChanges("ds", s_titleId, before, after);
			s_launched = s_settings;
		}

		// -- the emulation ---------------------------------------------------------------------------

		// A save state saved or loaded, between two frames, and what came of it in the menu
		void MakeState(melonDS::NDS& nds, int request)
		{
			const bool load = request < 0;
			const int slot = std::abs(request);
			const std::string path = StatePath(slot);
			std::string message;
			if (request == kUndoRequest)
			{
				// back to the game as the last load found it
				if (s_undo)
					s_undo->Rewind(false);
				if (s_undo && nds.DoSavestate(s_undo.get()) && !s_undo->Error)
				{
					// tr: how undoing the last load went
					message = ps5lang::Tr("Back to before the load");
					ps5cheevos::Reset();
					s_rewind.clear();
				}
				else
					message = ps5lang::Tr("It did not work");
				s_undo.reset();
				ps5ingame3ds::SetUndoAvailable(false);
			}
			else if (!load)
			{
				melonDS::Savestate state;
				if (!state.Error)
					nds.DoSavestate(&state);
				// the top screen, for Orbit's background (app/shots.h)
				if (!state.Error)
					ps5shots::SaveStatePicture(path, (const uint32_t*)nds.GPU.Framebuffer[nds.GPU.FrontBuffer][0].get(), 256, 192, 256);
				// what came of it, in the 3DS side's words (azahar/core.cpp, ReportState)
				message = !state.Error && WriteFile(path, (const u8*)state.Buffer(), state.Length()) ? ps5lang::TrF("Saved slot {0}", slot) :
																										   std::string(ps5lang::Tr("It did not work"));
			}
			else
			{
				std::vector<u8> data = ReadFile(path);
				// what the game was, to go back to when the state cannot be read
				auto backup = std::make_unique<melonDS::Savestate>(melonDS::Savestate::DEFAULT_SIZE);
				if (data.empty())
					message = ps5lang::Tr("It did not work");
				else if (backup->Error || !nds.DoSavestate(backup.get()) || backup->Error)
					message = ps5lang::Tr("It did not work");
				else
				{
					melonDS::Savestate state(data.data(), (u32)data.size(), false);
					if (!state.Error && nds.DoSavestate(&state) && !state.Error)
					{
						message = slot == kResumeSlot ? std::string(ps5lang::Tr("Back where you left off")) : ps5lang::TrF("Loaded slot {0}", slot);
						ps5cheevos::Reset(); // the set's conditions start again from the state's memory
						s_rewind.clear();	 // and a rewind, from here
						if (slot != kResumeSlot)
						{
							s_undo = std::move(backup); // what the load replaced, for Undo the last load
							ps5ingame3ds::SetUndoAvailable(true);
						}
					}
					else
					{
						backup->Rewind(false);
						nds.DoSavestate(backup.get());
						message = ps5lang::Tr("Made by another version");
					}
				}
			}
			ps5log::Line("[ds] {}: {}", request == kUndoRequest ? std::string("undo the last load") :
				fmt::format("{} slot {}", load ? "load from" : "save to", slot), message);
			if (request == kResumeSlot)
			{
				// the quit's own, said by nothing: now the quit
				if (s_quitAfterState)
					s_stop = true;
				return;
			}
			ps5ingame3ds::SetStateMessage(message);
			if (!ps5ingame3ds::MenuOpen())
				ps5notify::Send(message); // a shortcut's: the menu is not there to say it
			ShowStateSlots();
		}

		// Cheats turned on or off, kept in the game's cheat file as melonDS's own window keeps them
		void ToggleCheats(melonDS::NDS& nds)
		{
			std::vector<int> toggles;
			{
				std::lock_guard lock(s_cheatMutex);
				toggles.swap(s_cheatToggles);
			}
			if (toggles.empty())
				return;
			const std::vector<melonDS::ARCode*> codes = CheatList();
			for (const int index : toggles)
				if (index >= 0 && index < (int)codes.size())
				{
					codes[index]->Enabled = !codes[index]->Enabled;
					ps5log::Line("[ds] cheat {}: {}", codes[index]->Name, codes[index]->Enabled ? "on" : "off");
				}
			if (s_cheats)
			{
				s_cheats->Save();
				nds.AREngine.Cheats = s_cheats->GetCodes();
			}
			ShowCheats();
		}

		// A snapshot for the rewind, the oldest's memory taken for it once they are all there
		void RewindCapture(melonDS::NDS& nds)
		{
			if (s_rewindScratch.empty())
				s_rewindScratch.resize(melonDS::Savestate::DEFAULT_SIZE);
			melonDS::Savestate state(s_rewindScratch.data(), (u32)s_rewindScratch.size(), true);
			if (state.Error || !nds.DoSavestate(&state) || state.Error)
				return;
			std::vector<u8> snapshot;
			if (s_rewind.size() >= kRewindStates)
			{
				snapshot = std::move(s_rewind.front());
				s_rewind.pop_front();
			}
			snapshot.assign(s_rewindScratch.data(), s_rewindScratch.data() + state.Length());
			s_rewind.push_back(std::move(snapshot));
		}

		// One step back: the newest snapshot loaded and taken off; false with none left
		bool RewindStep(melonDS::NDS& nds)
		{
			if (s_rewind.empty())
				return false;
			std::vector<u8>& snapshot = s_rewind.back();
			melonDS::Savestate state(snapshot.data(), (u32)snapshot.size(), false);
			const bool loaded = !state.Error && nds.DoSavestate(&state) && !state.Error;
			s_rewind.pop_back();
			if (!loaded)
			{
				ps5log::Line("[ds] a rewind snapshot did not load: the rewind stops there");
				s_rewind.clear();
			}
			return loaded;
		}

		// The remote touch screen's picture (port/stream): the DS's touch screen, as melonDS drew it, in
		// RGB rows. A copy on the CPU: the DS's screens are not on the GPU until they are shown.
		void SendTouchScreen(const uint32_t* pixels)
		{
			static std::vector<uint8_t> rgb(256 * 192 * 3);
			for (int i = 0; i < 256 * 192; i++)
			{
				const uint32_t pixel = pixels[i]; // blue, green, red, unused: 0x--RRGGBB
				rgb[i * 3] = (uint8_t)(pixel >> 16);
				rgb[i * 3 + 1] = (uint8_t)(pixel >> 8);
				rgb[i * 3 + 2] = (uint8_t)pixel;
			}
			ps5stream::SubmitFrame(rgb.data(), 256, 192);
		}

		void Emulate()
		{
			melonDS::NDS& nds = *s_nds;
			const double refresh = ps5display::OutputRefresh() ? ps5display::OutputRefresh() / 1000.0 : 59.94;
			const uint64_t period = (uint64_t)(1e6 / refresh);
			double owed = 0;		   // DS frames to run at a speed above 100%
			uint64_t nextFrame = 0;	   // when the next frame is due (sceKernelGetProcessTime), should VideoOut not pace
			while (!s_stop)
			{
				if (const int request = s_stateRequest.exchange(0))
				{
					if (s_captureEnabled) s_capture.Discontinuity();
					MakeState(nds, request);
				}
				ToggleCheats(nds);
				View view;
				{
					std::lock_guard lock(s_viewMutex);
					view = s_view;
				}
				if (nds.IsLidClosed() != view.lidClosed)
					nds.SetLidClosed(view.lidClosed);
				screens::Frame frame;
				screens::Place(view.layout, view.swapped, view.rotation, view.gap, (float)screens::Width(), (float)screens::Height(), frame.topRect,
					frame.bottomRect);
				frame.rotation = view.rotation;
				frame.filter = view.filter;
				const bool held = ps5ingame3ds::MenuOpen() || !nds.IsRunning();
				// over the bottom screen, or over the top one while the bottom one is touched all the same
				frame.cursorOnTop = !frame.bottomRect.Shown();
				frame.cursor = input::Cursor(frame.cursorX, frame.cursorY) && !held && (!frame.cursorOnTop || input::HiddenTouch());
				if (!held)
				{
					// rewind, held (#54): no sound while it is; let go, the game goes on from there, the
					// achievements' conditions with it
					const bool rewind = input::Rewinding() && input::RewindBound();
					if (rewind != s_rewinding)
					{
						s_rewinding = rewind;
						s_rewindWait = 0;
						sound::SetMuted(rewind);
						if (!rewind)
						{
							nds.SPU.DrainOutput();
							s_rewindDue = kRewindEvery;
							if (s_rewound)
								ps5cheevos::Reset();
							s_rewound = false;
						}
					}
					int speed = view.speedLimit;
					if (rewind)
					{
						// a step back every kRewindStep frames shown, one frame run for its picture with the
						// game's buttons let go; with no snapshot left, the picture holds
						if (--s_rewindWait <= 0)
						{
							s_rewindWait = kRewindStep;
							if (RewindStep(nds))
							{
								s_rewound = true;
								nds.SetKeyMask(0xFFF);
								nds.ReleaseScreen();
								nds.RunFrame();
							}
						}
					}
					else
					{
						nds.SetKeyMask(input::KeyMask());
						int x, y;
						float remoteX, remoteY;
						// the remote touch screen's touch (port/stream) before the touchpad's, whether or not
						// the TV shows the touch screen
						if (ps5stream::Touch(remoteX, remoteY))
							nds.TouchScreen((melonDS::u16)std::clamp((int)(remoteX * 256.0f), 0, 255), (melonDS::u16)std::clamp((int)(remoteY * 192.0f), 0, 191));
						else if (input::Touch(x, y))
							nds.TouchScreen((melonDS::u16)x, (melonDS::u16)y);
						else
							nds.ReleaseScreen();
						// fast forward, held (#54): no speed limit while it is
						if (input::FastForward())
							speed = 0;
						// the DS frames this frame shows: one, more at a higher speed limit, as many as fit
						// in 12 ms with none
						int frames = 1;
						if (speed > 100)
						{
							owed += speed / 100.0;
							frames = std::max(1, (int)owed);
							owed -= frames;
						}
						const uint64_t start = sceKernelGetProcessTime();
						int ran = 0;
						for (int i = 0; !s_stop && (i < frames || (speed == 0 && sceKernelGetProcessTime() - start < 12000)); i++)
						{
							nds.RunFrame();
							s_framesEmulated++;
							ran++;
							if (s_cheevos)
								ps5cheevos::DoFrame();
						}
						// a snapshot for the rewind every half a second of the game's, while it has an input
						if (input::RewindBound() && (s_rewindDue -= ran) <= 0)
						{
							RewindCapture(nds);
							s_rewindDue = kRewindEvery;
						}
						sound::Pace(nds, refresh, speed);
					}
					frame.top = nds.GPU.Framebuffer[nds.GPU.FrontBuffer][0].get();
					frame.bottom = nds.GPU.Framebuffer[nds.GPU.FrontBuffer][1].get();
					if (ps5stream::WantFrame())
						SendTouchScreen(frame.bottom);
					// VideoOut's flips pace the frames; should they not, a frame is not shown more than
					// 2 ms before it is due
					const uint64_t now = sceKernelGetProcessTime();
					if (speed == 100 && nextFrame > now + 2000)
						sceKernelUsleep((uint32_t)(nextFrame - now - 1000));
					nextFrame = std::max(nextFrame + period, sceKernelGetProcessTime());
				}
				else if (s_cheevos)
					ps5cheevos::Idle(); // paused: what the server answered is still dealt with
				if (!screens::Present(frame))
				{
					ps5log::Line("[ds] the screens stopped: the GPU's device was lost");
					// tr: a notification as a DS game stops because the PS5's GPU stopped drawing its screens
					ps5notify::Send(ps5lang::Tr("The DS game's screens stopped (the GPU's device was lost)."));
					break;
				}
				if (s_captureEnabled)
				{
					if (held || s_rewinding || input::FastForward() || s_stateRequest.load()) s_capture.Discontinuity();
					else s_capture.Record(sceKernelGetProcessTime());
				}
				s_framesShown++;
			}
			// the rewind's memory given back
			s_rewind = {};
			s_rewindScratch = {};
			s_rewinding = s_rewound = false;
			sound::SetMuted(false);
			s_running = false;
		}
	}

	void Stopped(int reason)
	{
		s_stopReason = reason;
	}

	void SaveChanged(const u8* data, u32 length)
	{
		std::lock_guard lock(s_saveMutex);
		s_save.assign(data, data + length);
		s_saveDirty = true;
		s_saveChangedAt = sceKernelGetProcessTime();
	}
	void GbaSaveChanged(const u8* data, u32 length)
	{
		if (!data || !length || length > (1u << 20)) return;
		std::lock_guard lock(s_gbaSaveMutex);
		if (s_gbaSavePath.empty()) return;
		s_gbaSave.assign(data, data + length); s_gbaDirty = true;
		s_gbaChangedAt = sceKernelGetProcessTime();
	}

	bool Available()
	{
		return true;
	}

	bool CoreTouched()
	{
		return s_coreTouched;
	}

	bool LaunchGame(const ps5emu::Game& game, const ps5settings::N3ds& settings, std::string& error)
	{
		std::error_code ec;
		for (const char* name : {"saves", "states", "cheats", "bios"})
			fs::create_directories(Folder(name), ec);
		const std::string path = game.path.string();
		{
			std::error_code ec;
			s_captureEnabled = fs::is_regular_file(fs::path(Root()) / "capture-frames", ec);
			if (s_captureEnabled)
				try { s_capture.Begin(); }
				catch (const std::exception& failure) { s_captureEnabled = false; ps5log::Line("[ds] frame capture disabled: {}", failure.what()); }
		}
		s_stem = game.path.stem().string();
		// an archive's game is named by the archive, as a ROM by its own file: a .tar.gz's without its .tar
		std::string inner = fs::path(s_stem).extension().string();
		for (char& c : inner)
			c = (char)std::tolower((unsigned char)c);
		if (archive::IsArchive(path) && inner == ".tar")
			s_stem = fs::path(s_stem).stem().string();
		s_folder = game.path.parent_path().string();
		ps5log::Line("[ds] launching {} ({}) from {}", game.name, game.gameId.empty() ? "no game code" : game.gameId, path);

		// a game in an archive (#50): its ROM read out of it into memory, once, before anything starts, as
		// melonDS's own frontend loads one; nothing is written out
		std::vector<u8> file;
		Title title;
		if (archive::IsArchive(path))
		{
			archive::Rom found;
			// an archive's ROM claimed larger than any cartridge is not one either
			if (!archive::FindRom(path, found) || found.index < 0 || found.size > kLargestRom)
			{
				// tr: why a DS game in a .zip, .7z or .rar archive did not start
				error = ps5lang::Tr("There is no DS game in this archive, or the archive could not be read.");
				return false;
			}
			const uint64_t started = sceKernelGetProcessTime();
			if (!archive::Read(path, found, file))
			{
				error = ps5lang::Tr("The DS game in this archive could not be read: is the archive whole?");
				return false;
			}
			ps5log::Line("[ds] {} read from the archive: {} MiB in {} ms", found.member.empty() ? std::string("its DS ROM") : found.member,
				file.size() >> 20, (sceKernelGetProcessTime() - started) / 1000);
			title = Inspect(file);
		}
		else
			title = Inspect(path);
		if (!title.readable)
		{
			// tr: why a DS game did not start
			error = ps5lang::Tr("This is not a DS game melonDS can read.");
			return false;
		}
		if (title.dsiOnly)
		{
			// melonDS plays DSi games only as a DSi, with a DSi's own BIOS and system memory, which this
			// app does not set up yet
			error = ps5lang::Tr("DSi games are not supported yet.");
			return false;
		}
		if (file.empty())
			file = ReadFile(path);
		if (file.empty())
		{
			error = ps5lang::Tr("The game could not be loaded.");
			return false;
		}
		auto rom = std::make_unique<u8[]>(file.size());
		std::copy(file.begin(), file.end(), rom.get());
		const u32 romLength = (u32)file.size();
		// RetroAchievements' hash from the file's bytes, before melonDS re-encrypts its copy (#107)
		const std::string cheevosHash = ps5cheevos::HashDs(file.data(), file.size());
		file = {};
		std::vector<u8> save = ReadSave();
		melonDS::NDSCart::NDSCartArgs cartArgs;
		if (!save.empty())
		{
			cartArgs.SRAM = std::make_unique<u8[]>(save.size());
			std::copy(save.begin(), save.end(), cartArgs.SRAM.get());
			cartArgs.SRAMLength = (u32)save.size();
		}
		{
			std::lock_guard lock(s_saveMutex);
			s_save = save;
			s_saveDirty = false;
		}
		auto cart = melonDS::NDSCart::ParseROM(std::move(rom), romLength, nullptr, std::move(cartArgs));
		if (!cart)
		{
			error = ps5lang::Tr("This is not a DS game melonDS can read.");
			return false;
		}

		// a GBA cartridge in Slot-2, when it is on and the game has one (melonds/slot2/<game>.gba): a game
		// without one starts with the slot empty, and so does one whose cartridge cannot be read, saying why
		auto slot = slot2::Load(Root(), s_stem, settings.dsSlot2);
		std::unique_ptr<melonDS::GBACart::CartCommon> gba;
		if (!slot.rom.empty())
		{
			gba = melonDS::GBACart::ParseROM(slot.rom.data(), (u32)slot.rom.size(), slot.save.empty() ? nullptr : slot.save.data(), (u32)slot.save.size(), nullptr);
			if (!gba)
				slot.error = "melonDS could not read the cartridge";
		}
		if (!slot.error.empty())
		{
			ps5log::Line("[ds] Slot-2 left empty: {}", slot.error);
			ps5notify::Send(ps5lang::Tr("The GBA cartridge for Slot-2 could not be read: the game starts without it (the boot log says why)."));
			gba.reset();
			slot = {};
		}
		else if (gba)
			ps5log::Line("[ds] Slot-2: {} ({} bytes), its save {}", s_stem + ".gba", slot.rom.size(), slot.save.empty() ? "new" : "found");
		{
			std::lock_guard lock(s_gbaSaveMutex);
			s_gbaSavePath = slot.savePath; s_gbaSave = slot.save; s_gbaDirty = false;
		}

		// the console: a DS's own BIOS and firmware, or melonDS's
		melonDS::NDSArgs args;
		auto bios9 = settings.dsOwnBios ? ReadBios<melonDS::ARM9BIOSSize>("bios9.bin") : nullptr;
		auto bios7 = settings.dsOwnBios ? ReadBios<melonDS::ARM7BIOSSize>("bios7.bin") : nullptr;
		const bool ownBios = bios9 && bios7;
		if (ownBios)
		{
			args.ARM9BIOS = std::move(bios9);
			args.ARM7BIOS = std::move(bios7);
		}
		std::string firmwareAbout;
		args.Firmware = MakeFirmware(settings, ownBios, firmwareAbout);
		// the JIT where the console gives executable memory, its code reached by the slow path (no fast memory)
		s_jit = settings.dsJit && ps5privilege::Current().executable;
		if (s_jit)
			args.JIT = melonDS::JITArgs{32, true, true, false};
		else
			args.JIT = std::nullopt;
		args.OutputSampleRate = 48000.0;
		args.Renderer3D = std::make_unique<melonDS::SoftRenderer>();
		ps5log::Line("[ds] {} and {}; the CPUs on {}", ownBios ? "the DS's own BIOS" : "melonDS's BIOS (FreeBIOS)", firmwareAbout,
			s_jit ? "the JIT" : settings.dsJit ? "the interpreter (no executable memory)" : "the interpreter (dsJit off)");

		const auto gameMic = fs::path(Root()) / "microphone" / (s_stem + ".wav");
		const auto micStatus = settings.dsMicSource == 2 ? ps5files::OwnStatus(gameMic, ec) : fs::file_status{};
		const auto micFile = micStatus.type() != fs::file_type::not_found && micStatus.type() != fs::file_type::none ? gameMic : fs::path(Root()) / "microphone.wav";
		// a recorded WAV that is missing or cannot be read: blowing makes noise as before, rather than the
		// game not starting
		if (std::string why; !microphone::source.Configure(settings.dsMicSource, micFile.string(), why))
		{
			ps5log::Line("[ds] the recorded microphone ({}): {}; noise instead", micFile.string(), why);
			ps5notify::Send(ps5lang::Tr("The recorded microphone could not be read: blowing makes noise instead (the boot log says why)."));
			microphone::source.Configure(0, {}, why);
		}

		if (s_captureEnabled)
		{
			s_captureGame = game.gameId.empty() ? s_stem : game.gameId;
			s_captureSettings = {{"jit_active", s_jit}, {"own_bios_active", ownBios},
				{"slot2", settings.dsSlot2}, {"microphone_source", settings.dsMicSource},
				{"touch_x", settings.dsTouchX}, {"touch_y", settings.dsTouchY}, {"touch_scale", settings.dsTouchScale},
				{"layout", settings.layout}, {"filter", settings.dsFilter}, {"rotation", settings.dsRotation},
				{"volume", settings.volume}, {"language_requested", settings.language}};
		}
		s_nds = new melonDS::NDS(std::move(args), nullptr);
		s_nds->Reset();
		s_nds->SPI.GetPowerMan()->SetBatteryLevelOkay(true);
		{
			// the DS's clock is the PS5's
			const std::time_t now = std::time(nullptr);
			if (const std::tm* local = std::localtime(&now))
				s_nds->RTC.SetDateTime(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
		}
		s_nds->SetNDSCart(std::move(cart));
		if (gba) s_nds->SetGBACart(std::move(gba));
		// straight into the game, past the DS's menu (melonDS's BIOS needs it)
		s_nds->SetupDirectBoot(game.path.filename().string());
		static_cast<melonDS::SoftRenderer&>(s_nds->GetRenderer3D()).SetThreaded(true, s_nds->GPU);
		s_cheats = std::make_unique<melonDS::ARCodeFile>(CheatPath());
		s_nds->AREngine.Cheats = s_cheats->GetCodes();
		s_nds->Start();

		// the screens: VideoOut is the game's from here
		s_coreTouched = true;
		if (!screens::Start(error))
		{
			error = ps5lang::TrF("The Vulkan renderer did not start: {0}", error);
			delete s_nds;
			s_nds = nullptr;
			return false;
		}

		s_settings = s_launched = settings;
		s_name = game.name;
		s_titleId = game.titleId;
		{
			std::lock_guard lock(s_viewMutex);
			s_view = {};
			s_view.layout = settings.layout;
			s_view.rotation = screens::QuarterTurns(settings.dsRotation);
			s_view.gap = settings.screenGap;
			s_view.filter = settings.dsFilter;
		}
		input::SetRotation(settings.dsRotation);
		input::SetHiddenTouch(settings.hiddenTouch);
		s_stopReason = -1;
		s_stateRequest = 0;
		s_undo.reset();
		ps5ingame3ds::SetUndoAvailable(false);
		{
			// the menu's top: GameTDB's publisher and year, and the box art (or the game's icon)
			ps5gameinfo::Info info;
			std::string details;
			if (ps5gameinfo::Find(ps5boxart::System::Nds, game.gameId, info))
				for (const std::string& part : {info.publisher, ps5gameinfo::Year(info.released)})
					if (!part.empty())
						details += (details.empty() ? "" : "  /  ") + part;
			if (details.empty())
				details = game.publisher;
			std::string cover = ps5boxart::Path(ps5boxart::System::Nds, game.gameId);
			if (cover.empty())
				cover = CoverPath(game.titleId);
			ps5ingame3ds::SetGame(details, cover);
		}
		ShowInMenu();
		ShowStateSlots();
		ShowCheats();
		ps5ingame3ds::LoadBorder(settings.border);
		PublishScreens();
		input::Configure(settings);
		sound::Start(*s_nds);
		sound::SetVolume(settings.volume);
		s_stop = false;
		s_running = true;
		s_framesEmulated = 0;
		s_framesShown = 0;
		// RetroAchievements, when someone has signed in: the ROM's hash finds the game's set
		s_cheevos = ps5cheevos::StartGame(ReadDsMemory, cheevosHash);
		ps5ingame3ds::SetAchievements(s_cheevos, {}, {});
		s_emulation = std::thread(Emulate);
		ps5log::Line("[ds] {} is running", game.name);
		return true;
	}

	void SetResume(bool resume)
	{
		s_resume = resume;
	}

	void RunGame()
	{
		// tr: the PS5's notification as a DS game starts
		ps5notify::Send(ps5lang::Tr("Touchpad click + Options: the in-game menu. A touchpad click alone: the touch screen. R3: the microphone"));
		ps5pad::SetStateShortcuts(true);
		s_quitAfterState = false;
		if (std::exchange(s_resume, false))
		{
			ps5log::Line("[ds] resuming where it was left");
			s_stateRequest = -kResumeSlot;
		}
		uint64_t polls = 0, quitAskedAt = 0; // quitAskedAt: when the quit asked for its state (sceKernelGetProcessTime)
		uint32_t lastEmulated = 0, lastShown = 0;
		uint64_t lastSecond = sceKernelGetProcessTime();
		ps5emu::LogMemory();
		while (s_running && !s_stop)
		{
			sceKernelUsleep(4000);
			input::Update(ps5ingame3ds::MenuOpen(), s_bottomShown); // the game sees no buttons while the menu is up
			if (++polls % 500 == 0)
				ps5pad::Rescan(); // controllers joining or leaving, about every two seconds
			if (polls % 15000 == 0)
				ps5emu::LogMemory(); // about once a minute: what keeps growing is a leak
			if (polls % 250 == 0)
			{
				// about once a second: the overlay's numbers, from the frames shown and emulated
				const uint64_t now = sceKernelGetProcessTime();
				const double seconds = std::max(1e-3, (now - lastSecond) / 1e6);
				const uint32_t emulated = s_framesEmulated, shown = s_framesShown;
				const double fps = (shown - lastShown) / seconds;
				const double speed = (emulated - lastEmulated) / seconds / kDsFrameRate * 100.0;
				lastSecond = now;
				lastEmulated = emulated;
				lastShown = shown;
				ps5ingame3ds::SetPerformance(fps, speed, s_jit ? "DS: melonDS, JIT" : "DS: melonDS, interpreter");
				// every ten seconds in the boot log, so a slow game can be told from a slow PS5
				if (polls % 2500 == 0)
					ps5log::Line("[perfds] {:.0f} fps, speed {:.0f}%; {}; filter {}", fps, speed, s_jit ? "JIT" : "interpreter", s_settings.dsFilter);
			}
			FlushSave(false);
			// RetroAchievements in the menu: what is earned, read again about once a second while it is open
			if (s_cheevos && polls % 250 == 1 && ps5ingame3ds::MenuOpen())
				ShowAchievements();
			const ps5pad::Shortcut shortcut = ps5pad::TakeShortcut();
			switch (shortcut)
			{
			case ps5pad::Shortcut::Menu:
				ps5ingame3ds::ToggleMenu();
				break;
			case ps5pad::Shortcut::SwapScreens:
			{
				ps5ingame3ds::Settings menu = MenuSettings();
				menu.swapScreens = !menu.swapScreens;
				ApplyMenu(menu);
				ShowInMenu();
				break;
			}
			case ps5pad::Shortcut::CornerScreen:
			{
				// the next layout, as the menu's Screens item goes
				ps5ingame3ds::Settings menu = MenuSettings();
				menu.layout = (menu.layout + 1) % 4;
				ApplyMenu(menu);
				ShowInMenu();
				break;
			}
			case ps5pad::Shortcut::SaveState:
			case ps5pad::Shortcut::LoadState:
			{
				// touchpad click + Square, or + Triangle held: the menu's slot, as its rows ask
				std::string why;
				if (!ps5ingame3ds::AskState(shortcut == ps5pad::Shortcut::LoadState, why))
					ps5notify::Send(why);
				break;
			}
			case ps5pad::Shortcut::None:
				break;
			}
			ps5ingame3ds::Settings menu;
			if (ps5ingame3ds::TakeChanges(menu))
				ApplyMenu(menu);
			ps5ingame3ds::ExtrasRequest extras;
			if (ps5ingame3ds::TakeExtrasRequest(extras) && extras.kind == ps5ingame3ds::ExtrasRequest::Cheat)
			{
				std::lock_guard lock(s_cheatMutex);
				s_cheatToggles.push_back(extras.index);
			}
			bool load = false;
			int slot = 0;
			if (ps5ingame3ds::TakeStateRequest(load, slot))
				s_stateRequest = load ? -slot : slot;
			else if (ps5ingame3ds::TakeUndoRequest())
				s_stateRequest = kUndoRequest;
			// a state for the quit that does not come (the emulation stuck): the quit without it
			if (quitAskedAt && sceKernelGetProcessTime() - quitAskedAt > 8000000 && !s_stop)
			{
				ps5log::Line("[ds] no state for the quit after 8 s: quitting without it");
				s_stop = true;
			}
			if (ps5ingame3ds::TakeLibraryRequest())
			{
				// a state to go on from first (Resume where you left off), made between two frames, then the quit
				ps5log::Line("[ds] back to the library");
				if (!s_quitAfterState)
				{
					s_quitAfterState = true;
					quitAskedAt = sceKernelGetProcessTime();
					s_stateRequest = kResumeSlot;
				}
				else
					s_stop = true;
			}
			if (const int reason = s_stopReason.exchange(-1); reason >= 0)
			{
				// the game shut the DS down (or asked for the GBA's mode): back to the library
				using Reason = melonDS::Platform::StopReason;
				ps5log::Line("[ds] melonDS stopped the DS ({})", reason);
				if (reason == (int)Reason::GBAModeNotSupported)
					// tr: notifications as melonDS stops a DS game
					ps5notify::Send(ps5lang::Tr("The game asked for the DS's GBA mode, which melonDS does not have."));
				else if (reason == (int)Reason::BadExceptionRegion)
					ps5notify::Send(ps5lang::Tr("The DS game crashed."));
				s_stop = true;
			}
		}
		s_stop = true;
		const auto started = std::chrono::steady_clock::now();
		auto elapsed = [&] {
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
		};
		// The library shows after this, in this process (main_ps5.cpp), and the save is written below: a
		// shutdown that hangs is cut short by starting the app over from here, as the 3DS's is. Each
		// game's shutdown watched afresh: the process can run several
		static std::atomic_bool s_shutDown = false;
		s_shutDown = false;
		std::thread([] {
			for (int waited = 0; waited < 50 && !s_shutDown; waited++)
				sceKernelUsleep(100000);
			if (s_shutDown)
				return;
			ps5log::Line("[ds] shutting down takes over 5 s: starting over without waiting");
			FlushSave(true);
			ps5settings::Launcher all = ps5settings::Load();
			all.side = "ds";
			ps5settings::Save(all);
			ps5emu::RestartToLibrary();
		}).detach();
		if (s_emulation.joinable())
			s_emulation.join();
		if (s_captureEnabled)
		{
			s_capture.Stop();
			std::error_code ec;
			const fs::path captures = fs::path(Root()) / "captures";
			fs::create_directories(captures, ec);
			const std::string captureFile = (captures / fmt::format("{}-{}.csv", s_stem, sceKernelGetProcessTime())).string();
			if (!ec && s_capture.WriteCsv(captureFile))
			{
#ifndef PS5CEMU_VERSION
#define PS5CEMU_VERSION "unknown"
#endif
#ifndef PS5CEMU_MELONDS_COMMIT
#define PS5CEMU_MELONDS_COMMIT "unknown"
#endif
				try
				{
					const auto metadata = fs::path(captureFile).replace_extension(".json").string();
					const auto json = ps5capture::Session(s_capture, "ds", s_captureGame, PS5CEMU_VERSION,
						PS5CEMU_MELONDS_COMMIT, s_captureSettings);
					if (!ps5capture::WriteSession(metadata, json)) ps5log::Line("[ds] capture metadata could not be written: {}", metadata);
				}
				catch (const std::exception& failure) { ps5log::Line("[ds] capture metadata failed: {}", failure.what()); }
				ps5log::Line("[ds] {} CPU present-return intervals captured in {}; stopped: {}", s_capture.Samples().size(), captureFile, s_capture.Reason());
			}
			else ps5log::Line("[ds] frame capture could not be exported");
		}
		ps5log::Line("[ds] emulation stopped in {} ms", elapsed());
		FlushSave(true);
		if (s_cheevos)
			ps5cheevos::EndGame(2000); // an unlock still on its way, a moment to go
		sound::Stop();
		screens::Stop();
		delete s_nds; // its 3D renderer's thread with it
		s_nds = nullptr;
		s_cheats.reset();
		ps5log::Line("[ds] shut down in {} ms", elapsed());
		ps5pad::SetStateShortcuts(false);
		s_shutDown = true;
	}
}
