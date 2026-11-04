// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube and Wii sides' settings: Porpoise-X's own (src/ui_app_settings.cpp, add_game_rows), in the
// app's words, on the pages the other sides have. Each is read from and written to Porpoise's files
// (port/retro/porpoise_settings.h): every game's in its settings.ini, which the two sides share (Porpoise
// plays both), a game's own in its file in game-settings, which has only the keys the game changes.
// Porpoise reads them as PS5-ECAM starts a game in it.
#include "shell_internal.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace ps5shell
{
	using ps5lang::Tr;
	using ps5lang::TrF;
	using ps5lang::TrMark;

	namespace
	{
		enum class Kind
		{
			Toggle,	 // 0 or 1
			Choice,	 // one of its options, from its first value
			Percent, // tenths, shown as a percentage on a slider
		};
		enum class Only
		{
			Both,
			GameCube,
			Wii,
		};
		struct Setting
		{
			const char* page; // its Settings page
			const char* key;  // its key in Porpoise's files (ui_settings.cpp's kFields)
			Kind kind;
			const char* label;
			const char* description;
			const char* help; // Triangle's; the description where there is none
			std::vector<const char*> options; // a choice's (Tr looks up the names marked)
			int first, last, fallback; // its values, and Porpoise's own default (ui_settings.hpp)
			Only only = Only::Both;
			bool everyGame = false; // never a game's own (add_game_rows leaves it out of a game's settings)
		};

		// Porpoise's Settings has them in this order. My layouts (button_layout 2 to 5) are named in
		// PorpoiseRows.
		const std::vector<Setting> kSettings = {
			{"graphics", "resolution", Kind::Choice, TrMark("Internal resolution"),
				TrMark("How sharp games render: higher is sharper, and can slow a game."), nullptr,
				{"1x (480p)", "2x (720p)", "3x (1080p)", "4x (1440p)", "5x (1800p)", "6x (4K)"}, 1, 6, 3},
			{"graphics", "antialiasing", Kind::Choice, TrMark("Anti-aliasing"),
				TrMark("Smooths jagged edges. SSAA is the sharpest, and asks the most of the GPU."), nullptr,
				{TrMark("Off"), "2x MSAA", "4x MSAA", "8x MSAA", "2x SSAA", "4x SSAA", "8x SSAA"}, 0, 6, 0},
			{"graphics", "wide", Kind::Choice, TrMark("Widescreen"),
				TrMark("16:9 for a game with a widescreen code or option of its own, 4:3 for the rest."),
				TrMark("Automatic: 16:9 for a game with a widescreen code or a 16:9 option of its own, 4:3 for the rest. On: the same, "
					   "and Dolphin's widescreen hack for a game with neither, which can glitch at the screen's edges. Off: always 4:3. "
					   "Universal (beta): Porpoise widens the game's own 3D view to 16:9 where it can, and uses the hack where it cannot."),
				{TrMark("Automatic"), TrMark("On"), TrMark("Off"), TrMark("Universal (beta)")}, 0, 3, 0},
			{"graphics", "screen_filter", Kind::Choice, TrMark("Screen filter"),
				TrMark("Porpoise's own filter on the picture: scaling, CRTs, tapes, pixels or an LCD."),
				TrMark("Smooth or sharp scaling, sharpening, CRTs and an arcade monitor, VHS tapes, 8-bit and 16-bit pixels, a green "
					   "handheld's screen, scanlines, a composite cable, a TV's shadow mask, an aperture grille, a sharp LCD, AMD's FSR 1 "
					   "upscaler, or edges smoothed with FXAA. Filter strength sets how strong it is."),
				{TrMark("Smooth"), TrMark("Sharp"), TrMark("Sharpen (CAS)"), "CRT", TrMark("Arcade CRT"), "VHS", TrMark("Soft VHS"),
					TrMark("8-bit"), TrMark("Pocket"), TrMark("Scanlines"), TrMark("Shadow mask"), "LCD", "FSR 1", TrMark("16-bit"),
					TrMark("NTSC composite"), TrMark("Aperture grille"), TrMark("Smooth edges (FXAA)")},
				0, 16, 0},
			{"graphics", "filter_strength", Kind::Percent, TrMark("Filter strength"), TrMark("How strong the screen filter is."), nullptr, {},
				1, 10, 6},
			{"graphics", "frame_gen", Kind::Toggle, TrMark("Frame generation"),
				TrMark("A picture made between each two of the game's: 30 fps shown at 60. Adds a little delay."),
				TrMark("Makes a picture between each two of the game's: a game at 30 fps is shown at 60, and with the PS5 at 120 Hz a "
					   "game at 60 fps is shown at 120. Adds a little delay, and can smear fast motion."),
				{}, 0, 1, 0},
			{"graphics", "fg_steady30", Kind::Toggle, TrMark("Steady 30 fps"),
				TrMark("With frame generation, the game's frames at an even 30, for a steady 60."),
				TrMark("With frame generation on, the game's own frames are shown at an even 30 a second and frame generation fills in "
					   "the rest, for a steady 60: for games that swing between 30 and 60. It does not make a slow game faster. Leave it "
					   "off for a game that holds 60."),
				{}, 0, 1, 0},
			{"graphics", "fps_overlay", Kind::Toggle, TrMark("Frame rate counter"), TrMark("The frame rate in a corner while you play."),
				nullptr, {}, 0, 1, 0},
			{"graphics", "output_res", Kind::Choice, TrMark("Output resolution"),
				TrMark("The picture sent to the TV. Match the PS5 keeps the console's own."),
				TrMark("The picture Porpoise sends to the TV. Match the PS5 keeps the console's own output, so the TV does not switch "
					   "modes. 1080p is the quickest; 1440p and 4K are sharper on a 4K TV with a high internal resolution."),
				{TrMark("Match the PS5"), "1080p", "1440p", "4K"}, 0, 3, 0, Only::Both, true},
			{"controls", "button_layout", Kind::Choice, TrMark("Button layout"),
				TrMark("PlayStation: Cross is A and Circle B. GameCube: Circle is A and Cross B."),
				TrMark("PlayStation: Cross is A and Circle is B. GameCube: Circle is A and Cross is B. My layouts are your own, made in "
					   "Porpoise's Customize Buttons. For GameCube games, and Wii games played with the GameCube controller."),
				{"GameCube", "PlayStation"}, 0, 5, 1},
			{"controls", "rumble", Kind::Toggle, TrMark("Vibration"), TrMark("The controller's rumble, as the game asks for it."), nullptr, {},
				0, 1, 1},
			{"controls", "wii_controller", Kind::Choice, TrMark("Wii controller"), TrMark("How a Wii game sees your DualSense."),
				TrMark("Remote and Nunchuk: the Nunchuk on the left stick, L1 and L2. Remote: held pointing at the TV, with its motion. "
					   "Held sideways: like an NES pad, tilted to steer. Two DualSenses (alpha): the second one is the Nunchuk. GameCube "
					   "controller: for the Wii games that take one. Every other DualSense is another player's own Wii Remote."),
				{TrMark("Remote and Nunchuk"), TrMark("Remote"), TrMark("Remote held sideways"), TrMark("Classic Controller"),
					TrMark("Two DualSenses (alpha)"), TrMark("GameCube controller")},
				0, 5, 0, Only::Wii},
			{"controls", "wii_pointer", Kind::Choice, TrMark("Pointer"), TrMark("What moves the Wii Remote's pointer."),
				TrMark("Gyro: point the controller at the screen, and hold R1 a moment to centre it. Touchpad: slide a finger on it. "
					   "Right stick: push it."),
				{TrMark("Gyro"), TrMark("Touchpad"), TrMark("Right stick")}, 0, 2, 0, Only::Wii},
			{"system", "volume", Kind::Percent, TrMark("Volume"), TrMark("The game's sound."), nullptr, {}, 0, 10, 10},
			{"system", "quick_resume", Kind::Toggle, TrMark("Resume where you left off"),
				TrMark("A game left from its menu goes on from there the next time."),
				TrMark("Leaving a game from the in-game menu keeps where you were, and the game picks up right there the next time it "
					   "starts. Start Over in the in-game menu starts it afresh."),
				{}, 0, 1, 0},
			{"system", "language", Kind::Choice, TrMark("System language"),
				TrMark("The console's language: European games show their text in it."), nullptr,
				{TrMark("English"), TrMark("Japanese"), TrMark("German"), TrMark("French"), TrMark("Spanish"), TrMark("Italian"),
					TrMark("Dutch"), TrMark("Chinese (simplified)"), TrMark("Chinese (traditional)"), TrMark("Korean")},
				0, 9, 0},
		};

		constexpr const char* kPrefix = "porpoise:";

		const Setting* Find(const std::string& id)
		{
			if (!id.starts_with(kPrefix))
				return nullptr;
			for (const Setting& setting : kSettings)
				if (id.compare(std::strlen(kPrefix), std::string::npos, setting.key) == 0)
					return &setting;
			return nullptr;
		}

		// The value in Porpoise's files, as its Settings::load reads it; fallback where they have none
		int Value(const Setting& setting, const std::map<std::string, std::string>& values, int fallback, bool* found = nullptr)
		{
			const auto it = values.find(setting.key);
			if (found)
				*found = it != values.end();
			if (it == values.end())
				return fallback;
			if (setting.kind == Kind::Toggle)
			{
				const std::string& v = it->second;
				return v == "1" || v == "true" || v == "on" || v == "yes" || v == "enabled" ? 1 : 0;
			}
			return std::clamp(std::atoi(it->second.c_str()), setting.first, setting.last);
		}
	}

	void Shell::ReadPorpoise()
	{
		const std::string game = InGameSettings() ? m_gameRef.file : "";
		if (m_porpoise.read && m_porpoise.game == game)
			return;
		m_porpoise.read = true;
		m_porpoise.game = game;
		m_porpoise.gameFile = game.empty() ? "" : ps5porpoise::GameFile(game);
		m_porpoise.side = ps5porpoise::Read(ps5porpoise::SettingsFile());
		m_porpoise.own = game.empty() ? std::map<std::string, std::string>{} : ps5porpoise::Read(m_porpoise.gameFile);
	}

	std::vector<Row> Shell::PorpoiseRows(const std::string& page)
	{
		ReadPorpoise();
		const bool wii = ps5retro::Is(SideIndex(), "wii");
		std::vector<Row> rows;
		for (const Setting& setting : kSettings)
		{
			if (page != setting.page || (setting.only == Only::Wii && !wii) || (setting.only == Only::GameCube && wii) ||
				(InGameSettings() && setting.everyGame))
				continue;
			bool own = false;
			const int sideValue = Value(setting, m_porpoise.side, setting.fallback);
			const int value = InGameSettings() ? Value(setting, m_porpoise.own, sideValue, &own) : sideValue;
			Row row;
			row.id = std::string(kPrefix) + setting.key;
			row.setting = row.id;
			row.fromSide = !own;
			row.label = Tr(setting.label);
			row.description = Tr(setting.description);
			row.help = Tr(setting.help ? setting.help : setting.description);
			if (setting.kind == Kind::Toggle)
			{
				row.kind = Row::Kind::Toggle;
				row.on = value != 0;
				row.value = row.on ? Tr("On") : Tr("Off");
			}
			else if (setting.kind == Kind::Percent)
			{
				row.kind = Row::Kind::Slider;
				row.fraction = value / 10.0f;
				row.value = ps5lang::Percent(value * 10);
			}
			else
			{
				row.kind = Row::Kind::Choice;
				for (const char* option : setting.options)
					row.options.push_back(Tr(option));
				// your own button layouts, made in Porpoise's Customize Buttons
				for (int layout = 1; setting.key == std::string_view("button_layout") && layout <= 4; layout++)
					// tr: a button layout of the player's own, made in Porpoise's Customize Buttons: {0} is its number
					row.options.push_back(TrF("My layout {0}", layout));
				row.index = std::clamp(value - setting.first, 0, (int)row.options.size() - 1);
				row.value = row.options[row.index];
			}
			rows.push_back(std::move(row));
		}
		return rows;
	}

	bool Shell::SetPorpoise(const std::string& id, int value)
	{
		const Setting* setting = Find(id);
		if (!setting)
			return false;
		ReadPorpoise();
		std::string error;
		const std::string file = InGameSettings() ? m_porpoise.gameFile : ps5porpoise::SettingsFile();
		if (!ps5porpoise::Write(file, {{setting->key, std::to_string(value)}}, error))
		{
			m_feedback.Play(ui::Cue::Denied);
			// tr: {0} is why, in the system's words
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
			return false;
		}
		m_porpoise.read = false;
		m_feedback.Play(setting->kind == Kind::Toggle ? ui::Cue::Toggle : ui::Cue::Focus);
		return true;
	}

	bool Shell::ChangePorpoise(const Row& row, int step, bool cross)
	{
		const Setting* setting = Find(row.id);
		if (!setting)
			return false;
		if (setting->kind == Kind::Toggle)
		{
			if (!cross && step != 0 && ((step > 0) == row.on))
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			return SetPorpoise(row.id, row.on ? 0 : 1);
		}
		if (setting->kind == Kind::Choice)
		{
			if (cross)
			{
				// the dropdown: its choice is written where the row's value lives (every game's, or the game's)
				const Row chosen = row;
				OpenPicker(InGameSettings() ? m_gameName : SystemName(m_side), row.label, row.options, row.index,
					[this, chosen](int index) { SetPorpoise(chosen.id, Find(chosen.id)->first + index); });
				return false;
			}
			const int count = (int)row.options.size();
			return SetPorpoise(row.id, setting->first + (row.index + step + count) % count);
		}
		const int value = (int)(row.fraction * 10 + 0.5f);
		int next = value + (cross ? 1 : step);
		if (cross && next > setting->last)
			next = setting->first;
		if (next < setting->first || next > setting->last)
		{
			m_feedback.Play(ui::Cue::Edge);
			return false;
		}
		return SetPorpoise(row.id, next);
	}

	bool Shell::ResetPorpoise(const Row& row)
	{
		const Setting* setting = Find(row.id);
		ReadPorpoise();
		std::string error;
		if (!setting || !InGameSettings() || !m_porpoise.own.count(setting->key))
		{
			m_feedback.Play(ui::Cue::Edge);
			return false;
		}
		if (!ps5porpoise::Remove(m_porpoise.gameFile, {setting->key}, error))
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
			return false;
		}
		m_porpoise.read = false;
		m_feedback.Play(ui::Cue::Back);
		return true;
	}

	void Shell::ForgetPorpoiseGame()
	{
		// all of the game's own, as Porpoise's Reset to Defaults: its file, with whatever else Porpoise kept in it
		ReadPorpoise();
		std::error_code ec;
		if (!m_porpoise.gameFile.empty())
			std::filesystem::remove(m_porpoise.gameFile, ec);
		m_porpoise.read = false;
	}
}
