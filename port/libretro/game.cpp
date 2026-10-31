// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a libretro core's game on the console (game.h). As the DS side's (port/melonds/core.cpp):
//
//  - the emulation, and the picture on VideoOut (the DS side's screens, their one picture), on a thread
//    of its own: as many of the core's frames for each frame shown as its rate over VideoOut's (60.1
//    for the NES on a 59.94 Hz TV: one a frame, now and then two; 50 for a PAL game: now and then
//    none), VideoOut's flips pacing it. Every call into the core (session.h) is made from this thread.
//  - the sound on another: the core's, at its own rate, made 48 kHz and fed to AudioOut in 256-frame
//    grains, its buffer kept about half full by stretching it a little either way.
//  - the game's loop on the main thread: the DualSense as each player's RetroPad (the side's controls,
//    A and B where the menu puts them, the left stick as the D-pad too), the port's shortcuts
//    (ps5/pad.h), and what the in-game menu changes, as the DS side's loop does.
#include "game.h"
#include "core.h"
#include "session.h"
#include "../app/ingame3ds.h"
#include "../app/lang.h"
#include "../app/saves.h"
#include "../azahar/controls.h"
#include "../melonds/screens.h"
#include "../ps5/display.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../retro/registry.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <ctime>
#include <deque>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <vector>

extern "C"
{
	int sceAudioOutInit(void);
	int sceAudioOutOpen(int userId, int type, int index, uint32_t length, uint32_t frequency, uint32_t format);
	int sceAudioOutClose(int handle);
	int sceAudioOutOutput(int handle, const void* samples);
	int sceAudioOutSetVolume(int handle, int flags, const int* volumes);
	void* scePthreadSelf(void);
	int scePthreadGetprio(void* thread, int* priority);
	int scePthreadSetprio(void* thread, int priority);
}

namespace ps5libretro
{
	namespace
	{
		namespace screens = ps5melonds::screens;
		using ps5azahar::Button;
		using ps5emu::PadInput;

		constexpr int kStateSlots = ps5ingame3ds::kStateSlots;
		constexpr int kResumeSlot = ps5saves::kResumeSlot;

		int s_side = -1;
		std::string s_key; // the side's ("snes")
		std::string s_name;
		uint64_t s_titleId = 0;
		ps5settings::N3ds s_settings; // the game's loop's: as the menu leaves them
		ps5settings::N3ds s_launched; // what the game started with, or what the menu last saved
		bool s_coreTouched = false;
		bool s_resume = false;
		std::thread s_emulation;
		std::atomic_bool s_stop = false;
		std::atomic_bool s_running = false;
		std::atomic<uint32_t> s_framesShown{0}, s_framesRun{0};

		// What the emulation reads before each frame; the game's loop changes it
		struct View
		{
			int filter = 0;
			int speedLimit = 100; // percent of the core's own rate; 0: none
		};
		std::mutex s_mutex; // s_view and s_pads
		View s_view;
		Pad s_pads[kPlayers];

		// the game's loop's requests, for the emulation to make between two frames: a save state to slot
		// n (n), a load from it (-n), the last load undone (kUndoRequest)
		constexpr int kUndoRequest = -1000;
		std::atomic<int> s_stateRequest{0};
		std::atomic<bool> s_quitAfterState{false};
		// the emulation's answers, for the game's loop to give the menu (it is not the emulation's to)
		std::mutex s_answerMutex;
		std::string s_stateMessage;
		bool s_stateAnswered = false;
		bool s_undoAvailable = false;

		// -- the sound ---------------------------------------------------------------------------------

		constexpr int kUserSystem = 0xff;
		constexpr int kPortMain = 0;
		constexpr uint32_t kGrain = 256;
		constexpr uint32_t kRate = 48000;
		constexpr uint32_t kFormatStereoS16 = 1;
		constexpr int kVolumeFlagsLeftRight = 3;
		constexpr int kVolume0dB = 32768;
		constexpr int kAudioPriority = 384;
		constexpr size_t kTargetFill = 2400; // frames waiting for AudioOut: 50 ms

		int s_port = -1;
		std::thread s_soundThread;
		std::atomic_bool s_soundStop = false;
		std::mutex s_soundMutex;
		std::deque<int16_t> s_sound; // 48 kHz, left and right
		double s_position = 0;	 // where the next 48 kHz frame falls between the core's last two
		int16_t s_previous[2] = {};

		void FeedSound()
		{
			void* self = scePthreadSelf();
			int was = 0;
			scePthreadGetprio(self, &was);
			if (was <= 0 || was > kAudioPriority)
				scePthreadSetprio(self, kAudioPriority);
			std::vector<int16_t> grain(kGrain * 2);
			while (!s_soundStop)
			{
				{
					std::lock_guard lock(s_soundMutex);
					const size_t have = std::min(s_sound.size(), grain.size());
					std::copy_n(s_sound.begin(), have, grain.begin());
					std::fill(grain.begin() + have, grain.end(), 0);
					s_sound.erase(s_sound.begin(), s_sound.begin() + have);
				}
				if (sceAudioOutOutput(s_port, grain.data()) < 0)
					sceKernelUsleep(5000);
			}
		}

		void SetVolume(int percent)
		{
			if (s_port < 0)
				return;
			const int volume = kVolume0dB * std::clamp(percent, 0, 100) / 100;
			const int volumes[2] = {volume, volume};
			sceAudioOutSetVolume(s_port, kVolumeFlagsLeftRight, volumes);
		}

		void StartSound()
		{
			sceAudioOutInit(); // an error when the launcher has already: the port says
			s_port = sceAudioOutOpen(kUserSystem, kPortMain, 0, kGrain, kRate, kFormatStereoS16);
			if (s_port < 0)
			{
				ps5log::Line("[retro] AudioOut did not open ({:#x}): no sound", (uint32_t)s_port);
				return;
			}
			SetVolume(s_settings.volume);
			s_soundStop = false;
			s_soundThread = std::thread(FeedSound);
		}

		void StopSound()
		{
			s_soundStop = true;
			if (s_soundThread.joinable())
				s_soundThread.join();
			if (s_port >= 0)
			{
				sceAudioOutOutput(s_port, nullptr); // the last grain played
				sceAudioOutClose(s_port);
			}
			s_port = -1;
			std::lock_guard lock(s_soundMutex);
			s_sound.clear();
		}

		// The core's sound of one frame, made 48 kHz by its two nearest samples, a little faster or
		// slower as AudioOut's buffer is fuller or emptier than it should be
		void QueueSound(const std::vector<int16_t>& sound, double rate)
		{
			if (s_port < 0 || sound.size() < 2)
				return;
			size_t fill;
			{
				std::lock_guard lock(s_soundMutex);
				fill = s_sound.size() / 2;
			}
			const double skew = std::clamp(1.0 + (double(fill) - double(kTargetFill)) / double(kTargetFill) * 0.005, 0.995, 1.005);
			const double step = rate / kRate * skew; // the core's samples a 48 kHz frame moves on by
			std::vector<int16_t> out;
			out.reserve(size_t(sound.size() / step) + 4);
			const size_t frames = sound.size() / 2;
			for (; s_position < double(frames); s_position += step)
			{
				const size_t i = size_t(s_position);
				const double t = s_position - double(i);
				for (int c = 0; c < 2; c++)
				{
					const double a = i == 0 ? s_previous[c] : sound[(i - 1) * 2 + c];
					const double b = sound[i * 2 + c];
					out.push_back((int16_t)std::lround(a + (b - a) * t));
				}
			}
			s_position -= double(frames);
			s_previous[0] = sound[(frames - 1) * 2];
			s_previous[1] = sound[(frames - 1) * 2 + 1];
			std::lock_guard lock(s_soundMutex);
			// a backlog past a fifth of a second (the game held for the menu): dropped, not played late
			if (s_sound.size() / 2 > kRate / 5)
				s_sound.clear();
			s_sound.insert(s_sound.end(), out.begin(), out.end());
		}

		// -- the DualSense as the RetroPad ---------------------------------------------------------------

		// the RetroPad's buttons, and the handhelds' buttons they are as the side's controls map them
		constexpr std::pair<unsigned, Button> kButtons[] = {
			{RETRO_DEVICE_ID_JOYPAD_A, Button::A}, {RETRO_DEVICE_ID_JOYPAD_B, Button::B}, {RETRO_DEVICE_ID_JOYPAD_X, Button::X},
			{RETRO_DEVICE_ID_JOYPAD_Y, Button::Y}, {RETRO_DEVICE_ID_JOYPAD_L, Button::L}, {RETRO_DEVICE_ID_JOYPAD_R, Button::R},
			{RETRO_DEVICE_ID_JOYPAD_L2, Button::ZL}, {RETRO_DEVICE_ID_JOYPAD_R2, Button::ZR}, {RETRO_DEVICE_ID_JOYPAD_START, Button::Start},
			{RETRO_DEVICE_ID_JOYPAD_SELECT, Button::Select}, {RETRO_DEVICE_ID_JOYPAD_UP, Button::Up}, {RETRO_DEVICE_ID_JOYPAD_DOWN, Button::Down},
			{RETRO_DEVICE_ID_JOYPAD_LEFT, Button::Left}, {RETRO_DEVICE_ID_JOYPAD_RIGHT, Button::Right},
		};
		std::array<PadInput, std::size(kButtons)> s_mapping{};

		float Axis(uint8_t value) { return std::clamp((int(value) - 128) / 127.0f, -1.0f, 1.0f); }

		bool Held(PadInput input, const ps5pad::Data& data, uint32_t buttons)
		{
			constexpr float kStick = 0.5f;
			switch (input)
			{
			case PadInput::None: return false;
			case PadInput::Cross: return buttons & ps5pad::kCross;
			case PadInput::Circle: return buttons & ps5pad::kCircle;
			case PadInput::Square: return buttons & ps5pad::kSquare;
			case PadInput::Triangle: return buttons & ps5pad::kTriangle;
			case PadInput::L1: return buttons & ps5pad::kL1;
			case PadInput::R1: return buttons & ps5pad::kR1;
			case PadInput::L2: return buttons & ps5pad::kL2;
			case PadInput::R2: return buttons & ps5pad::kR2;
			case PadInput::L3: return buttons & ps5pad::kL3;
			case PadInput::R3: return buttons & ps5pad::kR3;
			case PadInput::Create: return buttons & ps5pad::kCreate;
			case PadInput::Options: return buttons & ps5pad::kOptions;
			case PadInput::Up: return buttons & ps5pad::kUp;
			case PadInput::Down: return buttons & ps5pad::kDown;
			case PadInput::Left: return buttons & ps5pad::kLeft;
			case PadInput::Right: return buttons & ps5pad::kRight;
			case PadInput::LeftStickUp: return Axis(data.leftY) < -kStick;
			case PadInput::LeftStickDown: return Axis(data.leftY) > kStick;
			case PadInput::LeftStickLeft: return Axis(data.leftX) < -kStick;
			case PadInput::LeftStickRight: return Axis(data.leftX) > kStick;
			case PadInput::RightStickUp: return Axis(data.rightY) < -kStick;
			case PadInput::RightStickDown: return Axis(data.rightY) > kStick;
			case PadInput::RightStickLeft: return Axis(data.rightX) < -kStick;
			case PadInput::RightStickRight: return Axis(data.rightX) > kStick;
			case PadInput::TouchPad: return buttons & ps5pad::kTouchPad;
			}
			return false;
		}

		void Configure(const ps5settings::N3ds& settings)
		{
			for (size_t i = 0; i < std::size(kButtons); i++)
				s_mapping[i] = ps5azahar::MappedInput(settings, kButtons[i].second);
		}

		// Each player's controller, a few hundred times a second; while the menu is up, let go
		void ReadPads(bool blocked)
		{
			Pad pads[kPlayers];
			const float deadzone = std::clamp(s_settings.deadzone, 0, 90) / 100.0f;
			for (int player = 0; player < kPlayers; player++)
			{
				ps5pad::Data data{};
				if (!ps5pad::Read(player, data))
					continue;
				const uint32_t buttons = ps5pad::ApplyTurbo(ps5pad::FilterShortcuts(player, data.buttons).buttons);
				if (blocked || (data.buttons & ps5pad::kIntercepted))
					continue;
				Pad& pad = pads[player];
				for (size_t i = 0; i < std::size(kButtons); i++)
					if (Held(s_mapping[i], data, buttons))
						pad.buttons |= uint16_t(1u << kButtons[i].first);
				if (buttons & ps5pad::kL3)
					pad.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_L3;
				if (buttons & ps5pad::kR3)
					pad.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_R3;
				// the left stick as the D-pad too, past the deadzone, as the DS side's circle pad
				const float x = Axis(data.leftX), y = Axis(data.leftY);
				if (std::max(std::fabs(x), std::fabs(y)) > std::max(deadzone, 0.3f))
				{
					if (std::fabs(x) > std::fabs(y) * 0.5f)
						pad.buttons |= 1u << (x < 0 ? RETRO_DEVICE_ID_JOYPAD_LEFT : RETRO_DEVICE_ID_JOYPAD_RIGHT);
					if (std::fabs(y) > std::fabs(x) * 0.5f)
						pad.buttons |= 1u << (y < 0 ? RETRO_DEVICE_ID_JOYPAD_UP : RETRO_DEVICE_ID_JOYPAD_DOWN);
				}
				auto stick = [&](uint8_t value) {
					const float v = Axis(value);
					return int16_t(std::fabs(v) < deadzone ? 0 : std::lround(v * 32767.0f));
				};
				pad.left[0] = stick(data.leftX);
				pad.left[1] = stick(data.leftY);
				pad.right[0] = stick(data.rightX);
				pad.right[1] = stick(data.rightY);
			}
			std::lock_guard lock(s_mutex);
			std::copy(std::begin(pads), std::end(pads), s_pads);
		}

		// -- the menu ------------------------------------------------------------------------------------

		ps5ingame3ds::Settings MenuSettings()
		{
			ps5ingame3ds::Settings menu;
			menu.volume = s_settings.volume;
			menu.performance = s_settings.performance;
			menu.deadzone = s_settings.deadzone;
			menu.aOnCircle = ps5azahar::MappedInput(s_settings, Button::A) == PadInput::Circle;
			menu.border = s_settings.border;
			menu.screenFilter = s_settings.dsFilter;
			std::lock_guard lock(s_mutex);
			menu.speedLimit = s_view.speedLimit;
			return menu;
		}

		// The picture's place on the TV: the whole height, in the core's shape
		screens::Rect PictureRect(float aspect)
		{
			const float width = (float)screens::Width(), height = (float)screens::Height();
			float w = height * aspect, h = height;
			if (w > width)
			{
				w = width;
				h = width / aspect;
			}
			return {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
		}

		void PublishScreens(float aspect)
		{
			const screens::Rect r = PictureRect(aspect);
			ps5ingame3ds::SetScreens({{r.left, r.top, r.right, r.bottom}}, (float)screens::Width(), (float)screens::Height());
		}

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

		// What the menu changed, while the game runs; kept for the next games in the side's settings, or
		// the game's own when it has some
		void ApplyMenu(const ps5ingame3ds::Settings& menu)
		{
			const bool aOnCircle = ps5azahar::MappedInput(s_settings, Button::A) == PadInput::Circle;
			s_settings.volume = menu.volume;
			s_settings.performance = menu.performance;
			s_settings.deadzone = menu.deadzone;
			s_settings.dsFilter = menu.screenFilter;
			if (menu.border != s_settings.border)
			{
				s_settings.border = menu.border;
				ps5ingame3ds::LoadBorder(menu.border);
			}
			if (menu.aOnCircle != aOnCircle)
			{
				ps5azahar::SetMapping(s_settings, (size_t)Button::A, menu.aOnCircle ? PadInput::Circle : PadInput::Cross, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::B, menu.aOnCircle ? PadInput::Cross : PadInput::Circle, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::X, menu.aOnCircle ? PadInput::Triangle : PadInput::Square, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::Y, menu.aOnCircle ? PadInput::Square : PadInput::Triangle, true);
			}
			Configure(s_settings);
			{
				std::lock_guard lock(s_mutex);
				s_view.filter = menu.screenFilter;
				s_view.speedLimit = menu.speedLimit;
			}
			SetVolume(menu.volume);
			ps5settings::Launcher before, after;
			before.retro[ps5retro::Slot(s_side)] = s_launched;
			after.retro[ps5retro::Slot(s_side)] = s_settings;
			ps5settings::SaveChanges(s_key, s_titleId, before, after);
			s_launched = s_settings;
		}

		// -- the emulation -------------------------------------------------------------------------------

		void Answer(const std::string& message)
		{
			std::lock_guard lock(s_answerMutex);
			s_stateMessage = message;
			s_stateAnswered = true;
			s_undoAvailable = UndoAvailable();
		}

		// A save state saved or loaded, between two frames, and what came of it in the menu, in the DS
		// side's words; why one did not work, in the boot log
		void MakeState(int request)
		{
			std::string error;
			const int slot = std::abs(request);
			bool done;
			std::string message;
			if (request == kUndoRequest)
			{
				done = UndoLoad(error);
				message = done ? ps5lang::Tr("Back to before the load") : ps5lang::Tr("It did not work");
			}
			else if (request > 0)
			{
				done = SaveState(slot, error);
				message = done ? ps5lang::TrF("Saved slot {0}", slot) : std::string(ps5lang::Tr("It did not work"));
			}
			else
			{
				done = LoadState(slot, error);
				message = !done ? std::string(ps5lang::Tr("It did not work")) :
					slot == kResumeSlot ? std::string(ps5lang::Tr("Back where you left off")) : ps5lang::TrF("Loaded slot {0}", slot);
			}
			if (!done)
				ps5log::Line("[retro] save state {}: {}", request, error);
			if (request == kResumeSlot)
			{
				// the quit's own: the menu is gone, the game goes
				if (s_quitAfterState)
					s_stop = true;
				return;
			}
			if (request != -kResumeSlot || done)
				Answer(message);
		}

		void Emulate()
		{
			const double refresh = ps5display::OutputRefresh() ? ps5display::OutputRefresh() / 1000.0 : 59.94;
			double owed = 0; // the core's frames due, at its rate over VideoOut's
			std::vector<uint32_t> held; // the picture shown while the menu is up
			while (!s_stop)
			{
				if (const int request = s_stateRequest.exchange(0))
					MakeState(request);
				View view;
				Pad pads[kPlayers];
				{
					std::lock_guard lock(s_mutex);
					view = s_view;
					std::copy(std::begin(s_pads), std::end(s_pads), pads);
				}
				const bool paused = ps5ingame3ds::MenuOpen();
				if (!paused)
				{
					const double rate = FrameRate();
					owed += view.speedLimit > 0 ? rate / refresh * view.speedLimit / 100.0 : 4.0;
					const int frames = std::min(8, (int)owed);
					owed -= frames;
					for (int i = 0; i < frames && !s_stop; i++)
					{
						Run(pads);
						s_framesRun++;
						// sound at the core's own speed only: faster or slower, it would be played as such
						if (view.speedLimit == 100)
							QueueSound(Sound(), SampleRate());
					}
					for (int player = 0; player < kPlayers; player++)
					{
						uint16_t strong, weak;
						Rumble(player, strong, weak);
						ps5pad::SetVibration(player, uint8_t(strong >> 8), uint8_t(weak >> 8));
					}
				}
				const Picture& picture = LastPicture();
				screens::Frame frame;
				frame.filter = view.filter;
				if (picture.width && picture.height && picture.width <= screens::kPictureMax && picture.height <= screens::kPictureMax)
				{
					frame.picture = picture.pixels.data();
					frame.pictureWidth = picture.width;
					frame.pictureHeight = picture.height;
					frame.pictureRect = PictureRect(AspectRatio());
				}
				if (!screens::Present(frame))
				{
					ps5log::Line("[retro] the picture stopped: the GPU's device was lost");
					// tr: a notification as a game on a libretro core stops because the PS5's GPU stopped drawing it
					ps5notify::Send(ps5lang::Tr("The game's picture stopped (the GPU's device was lost)."));
					break;
				}
				s_framesShown++;
			}
			for (int player = 0; player < kPlayers; player++)
				ps5pad::SetVibration(player, 0, 0);
			s_running = false;
		}
	}

	bool CoreTouched() { return s_coreTouched; }
	void SetResume(bool resume) { s_resume = resume; }

	bool LaunchGame(int side, const ps5emu::Game& game, const ps5settings::N3ds& settings, std::string& error)
	{
		s_coreTouched = false;
		const Core* core = Plays(side) ? Find(ps5retro::Of(side).core) : nullptr;
		if (!core)
		{
			error = ps5lang::Tr("This side's emulator is not in this copy of PS5-ECAM.");
			return false;
		}
		const ps5retro::System& system = ps5retro::Of(side);
		s_side = side;
		s_key = system.key;
		s_name = game.name;
		s_titleId = game.titleId;
		s_settings = s_launched = settings;
		{
			std::lock_guard lock(s_mutex);
			s_view = View{settings.dsFilter, 100};
			std::fill(std::begin(s_pads), std::end(s_pads), Pad{});
		}
		Configure(settings);
		Setup setup;
		const std::string data = system.data;
		setup.system = data + "/system";
		setup.saves = data + "/saves";
		setup.states = data + "/states";
		// the launcher's language, as libretro numbers them, for the cores that have their own words
		const std::string language = ps5lang::Current();
		setup.language = language == "ja" ? RETRO_LANGUAGE_JAPANESE : language.rfind("fr", 0) == 0 ? RETRO_LANGUAGE_FRENCH :
			language.rfind("es", 0) == 0 ? RETRO_LANGUAGE_SPANISH : language == "de" ? RETRO_LANGUAGE_GERMAN :
			language == "it" ? RETRO_LANGUAGE_ITALIAN : language == "nl" ? RETRO_LANGUAGE_DUTCH :
			language == "pt-BR" ? RETRO_LANGUAGE_PORTUGUESE_BRAZIL : language == "pt-PT" ? RETRO_LANGUAGE_PORTUGUESE_PORTUGAL :
			language == "ru" ? RETRO_LANGUAGE_RUSSIAN : language == "ko" ? RETRO_LANGUAGE_KOREAN :
			language == "zh-Hant" ? RETRO_LANGUAGE_CHINESE_TRADITIONAL : language == "zh-Hans" ? RETRO_LANGUAGE_CHINESE_SIMPLIFIED :
			language == "pl" ? RETRO_LANGUAGE_POLISH : language == "tr" ? RETRO_LANGUAGE_TURKISH : RETRO_LANGUAGE_ENGLISH;
		SetLog([](int, const std::string& line) { ps5log::Line("[retro] {}", line); });
		if (!Load(*core, game.path.string(), setup, error))
		{
			ps5log::Line("[retro] {} did not load: {}", game.name, error);
			return false;
		}
		// the screens on VideoOut: from here, a failure needs a fresh process to show the launcher
		s_coreTouched = true;
		if (!screens::Start(error, "retro"))
		{
			Unload();
			return false;
		}
		StartSound();
		ps5ingame3ds::Start(s_name, s_titleId, MenuSettings(), ps5ingame3ds::Console::Retro, system.name);
		ps5ingame3ds::LoadBorder(s_settings.border);
		PublishScreens(AspectRatio());
		ShowStateSlots();
		ps5ingame3ds::SetUndoAvailable(false);
		s_stop = false;
		s_running = true;
		s_framesShown = s_framesRun = 0;
		s_emulation = std::thread(Emulate);
		ps5log::Line("[retro] {}: {} on {}", system.key, game.name, CoreName());
		return true;
	}

	void RunGame()
	{
		ps5notify::Send(ps5lang::Tr("Touchpad click + Options: the in-game menu (screens, picture, volume, controls, the library)"));
		ps5pad::SetStateShortcuts(true);
		s_quitAfterState = false;
		if (std::exchange(s_resume, false))
			s_stateRequest = -kResumeSlot;
		uint64_t polls = 0, quitAskedAt = 0;
		uint32_t lastRun = 0, lastShown = 0;
		uint64_t lastSecond = sceKernelGetProcessTime();
		while (s_running && !s_stop)
		{
			sceKernelUsleep(4000);
			ReadPads(ps5ingame3ds::MenuOpen());
			if (++polls % 500 == 0)
				ps5pad::Rescan();
			if (polls % 250 == 0)
			{
				const uint64_t now = sceKernelGetProcessTime();
				const double seconds = std::max(1e-3, (now - lastSecond) / 1e6);
				const uint32_t run = s_framesRun, shown = s_framesShown;
				const double fps = (shown - lastShown) / seconds;
				const double speed = (run - lastRun) / seconds / FrameRate() * 100.0;
				lastSecond = now;
				lastRun = run;
				lastShown = shown;
				ps5ingame3ds::SetPerformance(fps, speed, s_key + ": " + CoreName());
				if (polls % 2500 == 0)
					ps5log::Line("[perfretro] {:.0f} fps, speed {:.0f}%", fps, speed);
			}
			{
				std::lock_guard lock(s_answerMutex);
				if (std::exchange(s_stateAnswered, false))
				{
					ps5ingame3ds::SetStateMessage(s_stateMessage);
					ps5ingame3ds::SetUndoAvailable(s_undoAvailable);
					ShowStateSlots();
				}
			}
			const ps5pad::Shortcut shortcut = ps5pad::TakeShortcut();
			if (shortcut == ps5pad::Shortcut::Menu)
				ps5ingame3ds::ToggleMenu();
			else if (shortcut == ps5pad::Shortcut::SaveState || shortcut == ps5pad::Shortcut::LoadState)
			{
				std::string why;
				if (!ps5ingame3ds::AskState(shortcut == ps5pad::Shortcut::LoadState, why))
					ps5notify::Send(why);
			}
			ps5ingame3ds::Settings menu;
			if (ps5ingame3ds::TakeChanges(menu))
				ApplyMenu(menu);
			bool load = false;
			int slot = 0;
			if (ps5ingame3ds::TakeStateRequest(load, slot))
				s_stateRequest = load ? -slot : slot;
			else if (ps5ingame3ds::TakeUndoRequest())
				s_stateRequest = kUndoRequest;
			if (quitAskedAt && sceKernelGetProcessTime() - quitAskedAt > 8000000 && !s_stop)
			{
				ps5log::Line("[retro] no state for the quit after 8 s: quitting without it");
				s_stop = true;
			}
			if (ps5ingame3ds::TakeLibraryRequest())
			{
				// a state to go on from first (Resume where you left off), made between two frames, then the quit
				ps5log::Line("[retro] back to the library");
				if (!s_quitAfterState)
				{
					s_quitAfterState = true;
					quitAskedAt = sceKernelGetProcessTime();
					s_stateRequest = kResumeSlot;
				}
				else
					s_stop = true;
			}
		}
		s_stop = true;
		if (s_emulation.joinable())
			s_emulation.join();
		std::string message;
		while (TakeMessage(message))
			ps5log::Line("[retro] the core said: {}", message);
		Unload(); // the save written, the core stopped
		StopSound();
		screens::Stop();
		ps5pad::SetStateShortcuts(false);
		ps5log::Line("[retro] {} ended", s_name);
	}
}
