// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the Library's second layout, Orbit, and the question that picks it. The mark, every
// console's controller in a ring, is a dial as tall as the screen (DrawDial, consoles.cpp) that L1 and R1
// turn; the side at its pointer is open beside it, its games in a row under the one in focus, whose
// picture fills the screen under a dark shade. R3 goes to the NES pad in its middle, Home: the games
// played last, on every side. The same Library underneath: its order, its search, its game menu.

#include "shell_internal.h"

#include <algorithm>
#include <cmath>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr float kColumn = 680; // right of the dial
		constexpr float kRailLeft = 688, kRailBottom = 980, kCoverHeight = 220, kRailGap = 22, kFocusScale = 1.14f, kFocusRoom = 14;
		constexpr int kHomeMost = 12;
		constexpr uint32_t kNight = 0xff180f0b; // #0b0f18

		constexpr uint32_t Rgb(uint32_t rgb)
		{
			return 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
		}

		// the row's covers: disc cases tall, cartridges' boxes and handhelds' wider
		float CoverWidth(System side)
		{
			const bool tall = side == System::WiiU || side == System::Switch || side == System::Switch2 || side == System::GameCube || side == System::Wii;
			return tall ? 156.0f : 210.0f;
		}
	}

	// -- what the sides are asked for from any side -------------------------------------------------

	void Shell::StartFromAnySide(System side, const Game& game)
	{
		// as a home screen tile's game starts: once its side has it listed, and its emulator is up
		m_status.autoLaunch = game.entry.game.path.string();
		m_status.autoLaunchSide = side;
		m_status.autoLaunchResume = false;
		m_status.autoLaunchFrontend = false;
		m_orbitHome = false;
		if (side != m_side)
			SwitchSide(side);
		else
			Show(ScreenId::Library);
	}

	void Shell::GameMenuFromAnySide(System side, const Game& game)
	{
		const uint64_t title = game.entry.game.titleId;
		m_orbitHome = false;
		if (side != m_side)
			SwitchSide(side);
		m_libraryFocusTitle = title;
		LibraryRefresh();
		m_libraryZone = 2;
		if (LibraryGame() >= 0)
			OpenGameMenu(LibraryGame());
	}

	// -- Orbit ----------------------------------------------------------------------------------------

	void Shell::OrbitHomeOpen(bool home)
	{
		m_orbitHome = home;
		m_feedback.Play(home ? ui::Cue::Select : ui::Cue::Back);
		if (!home)
			return;
		// the catalogue has every side's games as last seen: the ones played last, on the sides shown
		struct Found
		{
			System side;
			const ps5catalog::Entry* entry;
		};
		std::vector<Found> found;
		for (int i = 0; i < kSides; i++)
		{
			const System side = BarSide(i);
			if (!SideShown(side))
				continue;
			for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)SideIndex(side)))
				if (entry.lastPlayed > 0 && !entry.hidden)
					found.push_back({side, &entry});
		}
		std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.entry->lastPlayed > b.entry->lastPlayed; });
		if ((int)found.size() > kHomeMost)
			found.resize(kHomeMost);
		m_homeGames.clear();
		for (const Found& one : found)
		{
			SideGame game{one.side, {}};
			AsSide(one.side, [&] { MakeGame(game.game, *one.entry); });
			m_homeGames.push_back(std::move(game));
		}
		m_homeIndex = 0;
		m_orbitRail.value = m_orbitRail.target = 0;
	}

	bool Shell::OrbitUpdate(const ui::Press& press)
	{
		const Button b = press.button;
		if (b == Button::R3)
		{
			OrbitHomeOpen(!m_orbitHome);
			return true;
		}
		if (m_orbitHome)
		{
			const int count = (int)m_homeGames.size();
			switch (b)
			{
			case Button::Left:
			case Button::Right:
			{
				const int next = m_homeIndex + (b == Button::Right ? 1 : -1);
				if (next < 0 || next >= count)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					m_homeIndex = next;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			}
			case Button::Up: FocusBar(); break;
			case Button::Cross:
				if (count > 0)
				{
					const SideGame chosen = m_homeGames[std::clamp(m_homeIndex, 0, count - 1)];
					StartFromAnySide(chosen.side, chosen.game);
				}
				else
					m_feedback.Play(ui::Cue::Denied);
				break;
			case Button::Options:
				if (count > 0)
				{
					const SideGame chosen = m_homeGames[std::clamp(m_homeIndex, 0, count - 1)];
					GameMenuFromAnySide(chosen.side, chosen.game);
				}
				break;
			case Button::Circle: OrbitHomeOpen(false); break;
			default: break;
			}
			return true;
		}
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
				return false; // its card's buttons, as in Classic
		}
		// the Library's list, as a row: its own Cross, Options, Sort, Search and jumps by letter go on
		// working on it (LibraryUpdate)
		m_libraryZone = 2;
		const int total = (int)m_shelf.size() + ArticTiles();
		switch (b)
		{
		case Button::Left:
		case Button::Right:
		{
			const int next = m_libraryIndex + (b == Button::Right ? 1 : -1);
			if (next < 0 || next >= total)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_libraryIndex = next;
				m_feedback.Play(ui::Cue::Focus);
				if (LibraryGame() >= 0)
					m_libraryFocusTitle = m_games[LibraryGame()].entry.game.titleId;
			}
			return true;
		}
		case Button::Up: FocusBar(); return true;
		case Button::Down: m_feedback.Play(ui::Cue::Edge, press.repeat); return true;
		case Button::Circle:
			if (!m_search.empty())
				return false; // the search let go first
			OrbitHomeOpen(true);
			return true;
		default: return false;
		}
	}

	void Shell::DrawOrbitBackdrop(Canvas& canvas, float pictureMix)
	{
		const Box screen{0, 0, 1920, 1080};
		canvas.Rect(screen, 0, kNight);
		if (m_settings.ui.gamePictures && !m_settings.ui.highContrast)
		{
			auto picture = [&](const std::string& path, float alpha) {
				if (path.empty() || alpha <= 0)
					return;
				// a picture as wide as the TV as it is; a small one (a cover) softened, not stretched
				const bool wide = m_images->Get(path).width >= 960;
				const ui::Picture& p = m_images->Get(path, !wide);
				if (p.texture)
					canvas.ImageCover(p.texture, p.width, p.height, screen, 0, ui::SetAlpha(0xffffffff, alpha));
			};
			picture(m_backdropOld, 1 - pictureMix);
			picture(m_backdrop, pictureMix);
		}
		// the dark shade that keeps it from distracting, darkest beside the dial and at the foot
		canvas.Rect(screen, 0, ui::SetAlpha(kNight, 0.7f));
		canvas.LinearGradient(screen, 0, kNight, ui::SetAlpha(kNight, 0), 0, 0, 1150, 0);
		canvas.LinearGradient(screen, 0, ui::SetAlpha(kNight, 0.55f), ui::SetAlpha(kNight, 0), 0, 0, 0, 200);
		canvas.LinearGradient(screen, 0, ui::SetAlpha(kNight, 0), ui::SetAlpha(kNight, 0.9f), 0, 626, 0, 1080);
		const uint32_t glow = m_orbitHome ? Rgb(0xff7043) : Accent();
		canvas.RadialGradient(screen, 0, ui::SetAlpha(glow, 0.26f), ui::SetAlpha(glow, 0), m_orbitHome ? 90.0f : 440.0f, 540, 420, 420);
		canvas.Grain(screen, Wash(0.05f));
	}

	void Shell::OrbitDraw(Canvas& canvas)
	{
		m_libraryZone = 2;
		DrawDial(canvas);
		DrawBar(canvas, true);
		const float x = kColumn, width = 1920 - kSafeX - x;
		canvas.PushAlpha(Enter(0));
		DrawWordmark(canvas, x, 72, 38, kText);
		canvas.PopAlpha();
		std::string kicker, stateTitle, stateText;
		std::vector<std::string> buttons;
		const bool state = !m_orbitHome && LibraryState(kicker, stateTitle, stateText, buttons);

		int place = 0, sides = 0;
		for (int i = 0; i < kSides; i++)
			if (SideShown(BarSide(i)))
			{
				sides++;
				if (BarSide(i) == m_side)
					place = sides;
			}

		Game* focus = nullptr;
		System focusSide = m_side;
		if (m_orbitHome)
		{
			if (!m_homeGames.empty())
			{
				m_homeIndex = std::clamp(m_homeIndex, 0, (int)m_homeGames.size() - 1);
				focus = &m_homeGames[m_homeIndex].game;
				focusSide = m_homeGames[m_homeIndex].side;
			}
		}
		else if (LibraryGame() >= 0)
			focus = &m_games[LibraryGame()];

		// the console at the pointer, or Home
		canvas.PushAlpha(Enter(1));
		// tr: Orbit's dial: which console of those it shows is at its pointer ({0} of {1})
		const std::string overline = m_orbitHome ? std::string(Tr("Home")) : TrF("Console {0} of {1}", place, sides);
		const std::string name = m_orbitHome ? std::string(Tr("Continue")) : SystemName(m_side);
		canvas.Text(Style(kOverlineStyle), x, 150, overline, Secondary(), width, 1);
		const ui::TextBlock head = canvas.Text(Style({name.size() > 14 ? 66.0f : 92.0f, ui::Weight::Bold, 1.0f, -1.0f}), x, 182, name, kText, width, 1);
		const float lineY = 182 + head.height + 16;
		canvas.Rect({x, lineY + 7, 12, 12}, 6, m_orbitHome ? Rgb(0xff7043) : Accent());
		std::string line;
		if (m_orbitHome)
			// tr: under Orbit's Home: what its row shows
			line = Tr("The games you played last, on every console");
		else
		{
			const int listed = (int)std::count_if(m_games.begin(), m_games.end(), [](const Game& g) { return !g.entry.hidden; });
			uint32_t minutes = 0;
			for (const Game& g : m_games)
				minutes += g.entry.minutesPlayed;
			line = TrP(listed, "{0} game", "{0} games");
			if (minutes >= 60)
				line += " · " + TrP(minutes / 60, "{0} h played", "{0} h played");
		}
		canvas.Text(Style({24, ui::Weight::Regular, 1.0f}), x + 26, lineY, line, Secondary(), width - 26, 1);
		canvas.PopAlpha();

		if (state)
		{
			LibraryStateDraw(canvas);
			OrbitHintsDraw(canvas, true);
			return;
		}
		AsSide(focusSide, [&] { WantBackdrop(focus); });

		// the game in focus
		canvas.PushAlpha(Enter(2));
		if (focus)
		{
			Game& g = *focus;
			const ui::TextBlock title = canvas.Text(Style({48, ui::Weight::SemiBold, 1.12f}), x, 396, g.entry.game.name, kText, width, 2);
			float y = 396 + title.height + 22, chipX = x;
			auto chip = [&](const std::string& words, const char* kind = "") {
				const float w = Chip(canvas, chipX, y, words, kind);
				if (w > 0)
					chipX += w + 12;
			};
			const std::string last = LastPlayedWords(g.entry.lastPlayed);
			if (!last.empty())
				// tr: a game's chip in Orbit: {0} is when, as "yesterday" or "3 days ago"
				chip(TrF("Last played {0}", last));
			chip(Played(g));
			if (g.report)
				chip(TrC("status", g.report->status), ps5compat::Kind(g.report->status));
			if (m_orbitHome)
				chip(SystemName(focusSide));
			y += 38 + 26;
			const Box play = PillButton(canvas, x, y, Tr("Play"), true, false, Icon::Cross, true, 64);
			PillButton(canvas, play.Right() + 16, y, Tr("Options"), false, false, Icon::Options, true, 64);
		}
		else if (m_orbitHome)
			// tr: Orbit's Home before any game was played
			canvas.Text(Style(kBodyStyle), x, 410, Tr("The games you play show up here."), Secondary(), width, 2);

		// the row of games, slid as far as the one in focus needs
		const int offset = m_orbitHome ? 0 : ArticTiles();
		const int total = m_orbitHome ? (int)m_homeGames.size() : (int)m_shelf.size() + offset;
		const int at = m_orbitHome ? m_homeIndex : m_libraryIndex;
		auto tileWidth = [&](int i) { return CoverWidth(m_orbitHome ? m_homeGames[i].side : m_side); };
		{
			float along = 0, right = 0;
			for (int i = 0; i < total; i++)
			{
				const float w = tileWidth(i);
				if (i == at)
					right = along + w * kFocusScale + kFocusRoom * 2;
				along += w + kRailGap + (i == at ? kFocusRoom * 2 : 0);
			}
			m_orbitRail.target = std::max(0.0f, right - (1920 - kSafeX - kRailLeft));
			m_orbitRail.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
			m_orbitRail.Update(m_dt);
		}
		auto drawTile = [&](int i, const Box& box, bool focused) {
			if (m_orbitHome)
				AsSide(m_homeGames[i].side, [&] { DrawCover(canvas, m_homeGames[i].game, box, focused, 0); });
			else if (i < offset)
			{
				// the 3DS's Artic Base, ahead of its games
				Panel(canvas, box, kRadiusCover);
				canvas.Text(Style({20, ui::Weight::SemiBold, 1.2f}), box.x + 16, box.y + 20, Tr("Play from your 3DS"), kText, box.w - 32, 4);
				if (focused)
					Focus(box, kRadiusCover);
			}
			else
				DrawCover(canvas, m_games[m_shelf[i - offset]], box, focused, 0);
		};
		canvas.PushClip({640, 690, 1280, 390});
		float along = kRailLeft - m_orbitRail.value;
		Box focused{};
		for (int i = 0; i < total; i++)
		{
			const float w = tileWidth(i);
			const Box box{along + (i == at ? kFocusRoom : 0), kRailBottom - kCoverHeight, w, kCoverHeight};
			const float step = w + kRailGap + (i == at ? kFocusRoom * 2 : 0);
			if (along > 1920 + w)
				break;
			if (i == at)
				focused = box;
			else if (box.Right() > 640)
				drawTile(i, box, false);
			along += step;
		}
		// lifted, and over its neighbours
		if (focused.w > 0 && total > 0)
		{
			const float scale = m_settings.ui.reduceMotion ? 1.0f : kFocusScale;
			const Box lifted{focused.x, focused.Bottom() - focused.h * scale - 8, focused.w * scale, focused.h * scale};
			drawTile(at, lifted, !m_onBar);
		}
		canvas.PopClip();
		canvas.PopAlpha();
		OrbitHintsDraw(canvas, false);
	}

	void Shell::OrbitHintsDraw(Canvas& canvas, bool state)
	{
		// its own row: the keys its dial has, in the column beside it, and none under a sheet (Hints has theirs)
		if (m_onBar || m_picker.open || m_help.open || m_menu.open || m_keyboard.open || m_update.open || m_unpack.open || m_reorganize.open)
			return;
		const ui::TextStyle style = Style({19, ui::Weight::Medium, 1.0f});
		const ui::TextStyle keyStyle = Style({14, ui::Weight::Bold, 1.0f});
		const uint32_t ink = Secondary();
		struct Item
		{
			std::vector<Icon> icons;
			const char* key;
			std::string words;
		};
		std::vector<Item> items;
		// tr: Orbit's hints: L1 and R1 turn its dial
		items.push_back({{Icon::L1, Icon::R1}, nullptr, Tr("Turn the dial")});
		items.push_back({{}, "R3", Tr("Home")});
		if (state)
			items.push_back({{Icon::Cross}, nullptr, Tr("Choose")});
		else
		{
			items.push_back({{Icon::Cross}, nullptr, Tr("Play")});
			if (!m_orbitHome)
				items.push_back({{Icon::Square}, nullptr, Tr("Sort")});
			items.push_back({{Icon::Options}, nullptr, Tr("Options")});
		}
		items.push_back({{Icon::Touchpad}, nullptr, Tr("Timeline")});
		constexpr float kIcon = 28, kGap = 30;
		const float y = 1080 - 30 - 28;
		float width = 0;
		std::vector<float> widths;
		for (const Item& item : items)
		{
			const float keys = item.icons.size() * (kIcon + 4) + (item.key ? m_fonts.Width(keyStyle, item.key) + 12 + 6 : 0);
			widths.push_back(keys + 6 + m_fonts.Width(style, item.words));
			width += widths.back() + kGap;
		}
		float at = 1920 - kSafeX - (width - kGap);
		for (size_t i = 0; i < items.size(); i++)
		{
			const Item& item = items[i];
			float along = at;
			for (Icon icon : item.icons)
			{
				canvas.Draw(icon, {along, y, kIcon, kIcon}, ink);
				along += kIcon + 4;
			}
			if (item.key)
			{
				const ui::TextBlock key = m_fonts.Layout(keyStyle, item.key);
				const Box pill{along, y + 1, key.width + 12, 26};
				canvas.Rect(pill, 7, Wash(0.14f));
				canvas.Text(key, pill.x + 6, pill.CentreY() - key.height * 0.5f, kText);
				along += pill.w + 6;
			}
			const ui::TextBlock words = m_fonts.Layout(style, item.words);
			canvas.Text(words, along + 6, y + kIcon * 0.5f - words.height * 0.5f, ink);
			at += widths[i] + kGap;
		}
	}

	// -- which layout: Make it yours' first step ----------------------------------------------------------

	void Shell::LayoutOpen(std::function<void()> then)
	{
		m_layoutThen = std::move(then);
		m_layoutFocus = std::clamp(m_settings.ui.layout, 0, 1);
		m_onBar = false;
		Show(ScreenId::Layout);
		m_screenAt = m_now;
	}

	void Shell::LayoutUpdate(const ui::Press& press)
	{
		switch (press.button)
		{
		case Button::Left:
		case Button::Right:
		{
			const int next = press.button == Button::Right ? 1 : 0;
			if (next == m_layoutFocus)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_layoutFocus = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Cross:
		case Button::Circle:
		{
			// Circle leaves it as it is: nothing here has to be answered, as the rest of Make it yours
			if (press.button == Button::Cross)
				m_settings.ui.layout = m_layoutFocus;
			m_settings.ui.layoutChosen = true;
			SaveSettings();
			ApplyTheme();
			m_feedback.SetLight(Accent());
			m_feedback.Play(ui::Cue::Select);
			if (m_layoutThen)
			{
				const std::function<void()> then = std::move(m_layoutThen);
				m_layoutThen = nullptr;
				then();
			}
			break;
		}
		default: break;
		}
	}

	void Shell::LayoutClassicPreview(Canvas& canvas, const Box& box)
	{
		// the Library as Classic has it, small
		const float px = box.x, py = box.y;
		const uint32_t line = 0x38fbf7f5, strong = 0x99fbf7f5;
		canvas.RadialGradient(box, 16, 0x808a3a1e, 0x008a3a1e, px + 610, py + 30, 240, 170);
		DrawMark(canvas, px + 42, py + 26, 13);
		canvas.Rect({px + 62, py + 22, 46, 8}, 4, line);
		// five consoles at a time, as the carousel shows them
		constexpr System kTiles[5] = {System::Snes, System::N64, System::WiiU, System::Switch, System::Nds};
		for (int i = 0; i < 5; i++)
		{
			const float away = std::abs(i - 2.0f);
			const float size = away < 1 ? 26.0f : 18.0f;
			const Box tile{px + 322 + (i - 2) * 46 - size / 2, py + 25 - size / 2, size, size};
			canvas.PushAlpha(1.0f - 0.22f * away);
			DrawBareConsole(canvas, kTiles[i], tile, i == 2);
			canvas.PopAlpha();
		}
		canvas.Rect({px + 296, py + 46, 52, 5}, 2.5f, 0x66fbf7f5);
		canvas.Rect({px + 600, py + 14, 86, 22}, 11, 0x14ffffff);
		canvas.Rect({px + 603, py + 17, 40, 16}, 8, 0xfffbf7f5);
		canvas.Rect({px + 36, py + 68, 70, 5}, 2.5f, line);
		constexpr uint32_t kCovers[5][2] = {{Rgb(0x1e3a8a), Rgb(0xe11d48)}, {Rgb(0x65a30d), Rgb(0x7c3aed)}, {Rgb(0x0c1a3a), Rgb(0x0ea5e9)},
			{Rgb(0xdc2626), Rgb(0xf59e0b)}, {Rgb(0x1f1235), Rgb(0xc026d3)}};
		for (int i = 0; i < 5; i++)
		{
			const Box cover = i == 0 ? Box{px + 36, py + 84, 63, 88} : Box{px + 41 + i * 66, py + 90, 58, 82};
			canvas.LinearGradient(cover, 6, kCovers[i][0], kCovers[i][1], cover.x, cover.y, cover.Right(), cover.Bottom());
			if (i == 0)
				canvas.Ring(cover, 6, 2, 0xffffffff);
		}
		const Box panel{px + 500, py + 84, 184, 104};
		canvas.Rect(panel, 10, 0x0fffffff);
		canvas.Ring(panel, 10, 1, 0x1affffff);
		canvas.Rect({px + 514, py + 98, 110, 9}, 4, strong);
		canvas.Rect({px + 514, py + 116, 150, 5}, 2.5f, line);
		canvas.Rect({px + 514, py + 160, 54, 18}, 9, 0xfffbf7f5);
		canvas.Rect({px + 36, py + 206, 60, 9}, 4, strong);
		for (int i = 0; i < 4; i++)
		{
			const float y = py + 232 + i * 38;
			if (i == 0)
			{
				canvas.Rect({px + 30, y, 656, 34}, 8, 0x14ffffff);
				canvas.Ring({px + 30, y, 656, 34}, 8, 1, 0xb3ffffff);
			}
			canvas.LinearGradient({px + 40, y + 4, 18, 26}, 3, kCovers[i][0], kCovers[i][1], px + 40, y + 4, px + 58, y + 30);
			canvas.Rect({px + 68, y + 10, 130.0f - i * 10, 7}, 3.5f, i == 0 ? strong : 0x73fbf7f5);
			canvas.Rect({px + 68, y + 22, 84, 4}, 2, line);
		}
	}

	void Shell::LayoutOrbitPreview(Canvas& canvas, const Box& box)
	{
		// the same, as Orbit has it: a game's picture under its shade, and the dial
		const float px = box.x, py = box.y;
		const uint32_t line = 0x38fbf7f5, strong = 0xb3fbf7f5;
		constexpr uint32_t kNightPreview = 0xff180f0b;
		canvas.LinearGradient(box, 16, Rgb(0x0d3a5a), Rgb(0x6fb7d8), px, py, px, box.Bottom());
		canvas.PushClip(box, 16);
		canvas.Rect({px, py + 250, box.w, 155}, 0, Rgb(0x0e5a40));
		canvas.PopClip();
		canvas.Rect(box, 16, ui::SetAlpha(kNightPreview, 0.72f));
		canvas.LinearGradient(box, 16, kNightPreview, ui::SetAlpha(kNightPreview, 0), px, py, px + 450, py);
		canvas.PushClip(box, 16);
		DrawMark(canvas, px + 33, py + 202, 187);
		canvas.PopClip();
		const uint32_t accent = Rgb(0x2bc994);
		canvas.Ring({px + 140, py + 177, 50, 50}, 25, 2, accent);
		canvas.Rect({px + 262, py + 22, 60, 10}, 5, strong);
		canvas.Rect({px + 262, py + 64, 70, 5}, 2.5f, line);
		canvas.Rect({px + 262, py + 78, 210, 20}, 6, 0xd9fbf7f5);
		canvas.Rect({px + 262, py + 106, 110, 5}, 2.5f, line);
		canvas.Rect({px + 262, py + 148, 240, 14}, 5, strong);
		canvas.Rect({px + 262, py + 172, 50, 12}, 4, 0x1fffffff);
		canvas.Rect({px + 318, py + 172, 40, 12}, 4, 0x4da3d63d);
		canvas.Rect({px + 262, py + 196, 76, 24}, 12, 0xfffbf7f5);
		canvas.Rect({px + 346, py + 196, 70, 24}, 12, 0x24ffffff);
		constexpr uint32_t kCovers[4][2] = {{Rgb(0x064e3b), Rgb(0xeab308)}, {Rgb(0x92400e), Rgb(0xeab308)}, {Rgb(0x1d4ed8), Rgb(0xeab308)}, {Rgb(0xb91c1c), Rgb(0x16a34a)}};
		for (int i = 0; i < 4; i++)
		{
			const Box cover = i == 0 ? Box{px + 264, py + 286, 58, 82} : Box{px + 272 + i * 60, py + 296, 52, 72};
			canvas.LinearGradient(cover, 6, kCovers[i][0], kCovers[i][1], cover.x, cover.y, cover.Right(), cover.Bottom());
			if (i == 0)
				canvas.Ring(cover, 6, 2, 0xffffffff);
		}
	}

	void Shell::LayoutDraw(Canvas& canvas)
	{
		// its own ground on every theme (DrawBackdrop), so its own ink
		constexpr uint32_t kInk = 0xfffbf7f5, kInk2 = 0xc7fbf7f5, kInk3 = 0x9efbf7f5;
		const float left = 140, width = 1920 - 280;
		canvas.PushAlpha(Enter(0));
		DrawMark(canvas, 115, 70, 30);
		DrawWordmark(canvas, 160, 70, 28, kInk);
		canvas.Text(Style(kOverlineStyle), left, 136, Tr("Make it yours"), kInk3, width, 1);
		// tr: the first start's question: the two layouts of the Library, Classic and Orbit
		canvas.Text(Style({54, ui::Weight::SemiBold, 1.1f}), left, 164, Tr("How should your games be laid out?"), kInk, width, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), left, 240,
			Tr("Pick one now. You can switch any time in Settings > Launcher > Layout, and your games, settings and saves stay as they are."), kInk2, width, 1);
		canvas.PopAlpha();

		struct Card
		{
			const char* name;
			const char* tag;
			const char* about;
			const char* goodFor;
		};
		const Card cards[2] = {
			{Tr("Classic"), "", Tr("One console at a time, from the carousel; L1 and R1 turn it. Recently played on top, every game in a list below."),
				Tr("Good for big libraries on a few consoles")},
			{Tr("Orbit"), Tr("New in 6.0"),
				Tr("Turn the dial to pick a console; its middle is what you played lately, on every console. The game you're on fills the background."),
				Tr("Good for playing across many consoles")},
		};
		canvas.PushAlpha(Enter(1));
		for (int i = 0; i < 2; i++)
		{
			const bool on = i == m_layoutFocus;
			const Box card{i == 0 ? 140.0f : 1000.0f, on ? 286.0f : 296.0f, 780, 600};
			canvas.PushAlpha(on ? 1.0f : 0.62f);
			if (on)
				canvas.Shadow(card, 28, 30, 70, 0x99000000);
			canvas.Rect(card, 28, on ? 0x14ffffff : 0x0dffffff);
			canvas.Ring(card, 28, on ? 4.0f : 1.0f, on ? 0xffffffff : 0x1affffff);
			const Box preview{card.x + 30, card.y + 30, 720, 405};
			canvas.Rect(preview, 16, 0xff0d0705);
			if (i == 0)
				LayoutClassicPreview(canvas, preview);
			else
				LayoutOrbitPreview(canvas, preview);
			canvas.Ring(preview, 16, 1, 0x14ffffff);
			const float x = card.x + 36, textWidth = card.w - 72;
			const ui::TextBlock name = canvas.Text(Style({36, ui::Weight::SemiBold, 1.0f}), x, card.y + 456, cards[i].name, kInk, textWidth, 1);
			if (*cards[i].tag)
				canvas.Text(Style({18, ui::Weight::Regular, 1.0f}), x + name.width + 14, card.y + 470, cards[i].tag, kInk3, textWidth - name.width, 1);
			const ui::TextBlock about = canvas.Text(Style({20, ui::Weight::Regular, 1.4f}), x, card.y + 502, cards[i].about, kInk2, textWidth, 2);
			canvas.Text(Style({17, ui::Weight::Regular, 1.0f}), x, card.y + 510 + about.height, cards[i].goodFor, kInk3, textWidth, 1);
			canvas.PopAlpha();
		}
		canvas.PopAlpha();

		canvas.PushAlpha(Enter(2));
		canvas.Text(Style({19, ui::Weight::Regular, 1.3f}), left, 926, Tr("Both have the Timeline view: every console on one shelf. Press the touchpad from either."), kInk2, 1180, 2);
		// tr: the button of the first start's question: {0} is the layout in focus, Classic or Orbit
		const std::string use = TrF("Use {0}", cards[m_layoutFocus].name);
		const ui::TextStyle useStyle = Style({22, ui::Weight::SemiBold, 1.0f});
		const float useWidth = m_fonts.Width(useStyle, use) + 72 + 36;
		const Box button{1920 - 140 - useWidth, 912, useWidth, 68};
		canvas.Rect(button, 34, kInk);
		canvas.Draw(Icon::Cross, {button.x + 32, button.CentreY() - 13, 26, 26}, 0xff1b0f0a);
		const ui::TextBlock label = m_fonts.Layout(useStyle, use);
		canvas.Text(label, button.x + 68, button.CentreY() - label.height * 0.5f, 0xff1b0f0a);
		canvas.PopAlpha();

		// its hints, in its ink
		const ui::TextStyle hint = Style({17, ui::Weight::Regular, 1.0f});
		const ui::TextBlock choose = m_fonts.Layout(hint, Tr("Choose"));
		canvas.Draw(Icon::LeftRight, {1920 - 96 - choose.width - 34, 1080 - 56, 26, 26}, kInk2);
		canvas.Text(choose, 1920 - 96 - choose.width, 1080 - 43 - choose.height * 0.5f, kInk2);
	}
}
