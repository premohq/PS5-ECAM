// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the Timeline, a special way to view the library from either layout (the touchpad): every
// console's games on one shelf, oldest console to newest, each game drawn as what it came on: its
// cartridge, its disk card, its mini disc or its case. A ruler of years over the shelf has every
// console's mark; Cross puts the game in the slot and starts it, Circle goes back to where you were.
// Its own light ground and ink on every theme.

#include "shell_internal.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr uint32_t Rgb(uint32_t rgb)
		{
			return 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
		}
		constexpr uint32_t kInk = Rgb(0x15171c), kInk2 = Rgb(0x3b404a), kInk3 = Rgb(0x4a4f5a), kPaper = Rgb(0xe7e8eb);
		constexpr float kLeft = 96, kView = 1728, kShelfTop = 300, kShelfBottom = 618, kItemGap = 18, kShelfWidth = 280;
		constexpr int kFirstYear = 1980, kYears = 46;

		float YearX(int year) { return kLeft + (year - kFirstYear) / (float)kYears * kView; }

		// what a game came on, at the shelf's size
		struct Media
		{
			float w, h, radius;
			uint32_t body;
			float ridge, band;
			uint32_t bandColour;
			float labelX, labelY, labelH, labelRadius;
			bool disc;
		};
		enum MediaKind { kNesCart, kDiskCard, kGamePak, kPakColor, kPakBlack, kGbaPak, kSnesCart, kN64Cart, kMiniDisc, kDsCard, k3dsCard, kWiiCase, kWiiUCase, kSwitchCase, kSwitch2Case };
		constexpr Media kMedia[] = {
			{138, 156, 6, Rgb(0x8d8d92), 24, 0, 0, 14, 34, 96, 4, false},
			{123, 144, 8, Rgb(0xf2c230), 0, 0, 0, 12, 16, 72, 4, false},
			{87, 99, 5, Rgb(0xa7a6a3), 10, 0, 0, 10, 18, 50, 3, false},
			{87, 99, 5, Rgb(0x2f3036), 10, 0, 0, 10, 18, 50, 3, false},
			{87, 99, 5, Rgb(0x1b1b1d), 10, 0, 0, 10, 18, 50, 3, false},
			{99, 63, 6, Rgb(0x9fa0a4), 0, 0, 0, 10, 10, 36, 3, false},
			{174, 126, 10, Rgb(0xb9b8bd), 0, 0, 0, 22, 18, 74, 5, false},
			{156, 120, 8, Rgb(0x3f4048), 22, 0, 0, 20, 32, 64, 4, false},
			{129, 129, 64.5f, Rgb(0xc3c6cd), 0, 0, 0, 30, 30, 69, 34.5f, true},
			{69, 78, 4, Rgb(0xbfc1c6), 0, 0, 0, 8, 12, 46, 3, false},
			{69, 78, 4, Rgb(0xeceef2), 0, 0, 0, 8, 12, 46, 3, false},
			{117, 165, 6, Rgb(0xf4f5f7), 0, 22, Rgb(0xc9ccd2), 6, 28, 131, 3, false},
			{117, 165, 6, Rgb(0xe8f1fb), 0, 22, Rgb(0x2a8be0), 6, 28, 131, 3, false},
			{105, 168, 6, Rgb(0xf7f7f8), 0, 24, Rgb(0xe8323f), 6, 30, 132, 3, false},
			{105, 168, 6, Rgb(0xf7f7f8), 0, 24, Rgb(0xa51d2a), 6, 30, 132, 3, false},
		};

		// the consoles, by the year each came out
		struct Era
		{
			System side;
			int year;
			const char* brief;
			int media;
		};
		constexpr Era kEras[] = {
			{System::GameWatch, 1980, "G&W", kGamePak},
			{System::Nes, 1983, "NES", kNesCart},
			{System::VsSystem, 1984, "VS.", kNesCart},
			{System::Fds, 1986, "FDS", kDiskCard},
			{System::Gb, 1989, "GB", kGamePak},
			{System::Snes, 1990, "SNES", kSnesCart},
			{System::Sgb, 1994, "SGB", kGamePak},
			{System::Satellaview, 1995, "BS-X", kGbaPak},
			{System::VirtualBoy, 1995, "VB", kPakBlack},
			{System::N64, 1996, "N64", kN64Cart},
			{System::Gbc, 1998, "GBC", kPakColor},
			{System::Gba, 2001, "GBA", kGbaPak},
			{System::GameCube, 2001, "GC", kMiniDisc},
			{System::PokemonMini, 2001, "mini", kDsCard},
			{System::Ique, 2003, "iQue", kDsCard},
			{System::Nds, 2004, "DS", kDsCard},
			{System::Wii, 2006, "Wii", kWiiCase},
			{System::N3ds, 2011, "3DS", k3dsCard},
			{System::WiiU, 2012, "Wii U", kWiiUCase},
			{System::Switch, 2017, "Switch", kSwitchCase},
			{System::Switch2, 2025, "Switch 2", kSwitch2Case},
		};
	}

	void Shell::TimelineOpen()
	{
		m_feedback.Play(ui::Cue::Select);
		m_shelves.clear();
		for (const Era& era : kEras)
		{
			if (!SideShown(era.side))
				continue;
			TimelineShelf shelf;
			shelf.side = era.side;
			shelf.year = era.year;
			shelf.brief = era.brief;
			shelf.media = era.media;
			AsSide(era.side, [&] {
				for (const ps5catalog::Entry& entry : ps5catalog::Games(CatalogSide()))
				{
					if (entry.hidden)
						continue;
					Game game;
					MakeGame(game, entry);
					shelf.games.push_back(std::move(game));
				}
			});
			// each by its own year, where GameTDB gives it, else by its name
			std::stable_sort(shelf.games.begin(), shelf.games.end(), [](const Game& a, const Game& b) {
				if (a.year != b.year)
					return a.year < b.year;
				return a.sortName < b.sortName;
			});
			const Media& media = kMedia[shelf.media];
			const int count = (int)shelf.games.size();
			shelf.w = std::max(kShelfWidth, count * media.w + std::max(0, count - 1) * kItemGap + 40);
			m_shelves.push_back(std::move(shelf));
		}
		float along = 0;
		for (TimelineShelf& shelf : m_shelves)
		{
			shelf.x = along;
			along += shelf.w;
		}
		// the shelf of the side open, on the game in focus
		m_shelfAt = 0;
		m_shelfGame = 0;
		const uint64_t title = !m_orbitHome && LibraryGame() >= 0 ? m_games[LibraryGame()].entry.game.titleId : 0;
		for (int i = 0; i < (int)m_shelves.size(); i++)
		{
			if (m_shelves[i].side != m_side)
				continue;
			m_shelfAt = i;
			for (int j = 0; j < (int)m_shelves[i].games.size(); j++)
				if (m_shelves[i].games[j].entry.game.titleId == title)
					m_shelfGame = j;
		}
		m_insertAt = 0;
		m_shelfScroll.value = m_shelfScroll.target = -1; // there at once
		m_onBar = false;
		Show(ScreenId::Timeline);
		m_screenAt = m_now;
	}

	void Shell::TimelineUpdate(const ui::Press& press)
	{
		if (m_insertAt > 0 || m_shelves.empty())
		{
			if (press.button == Button::Circle && m_insertAt <= 0)
				Show(ScreenId::Library);
			return;
		}
		TimelineShelf& shelf = m_shelves[m_shelfAt];
		const int last = (int)m_shelves.size() - 1;
		auto toShelf = [&](int to, bool lastGame = false) {
			to = std::clamp(to, 0, last);
			if (to == m_shelfAt)
			{
				m_feedback.Play(ui::Cue::Edge, press.repeat);
				return;
			}
			m_shelfAt = to;
			m_shelfGame = lastGame ? std::max(0, (int)m_shelves[to].games.size() - 1) : 0;
			m_feedback.Play(ui::Cue::Focus);
		};
		switch (press.button)
		{
		case Button::Left:
			if (m_shelfGame > 0)
			{
				m_shelfGame--;
				m_feedback.Play(ui::Cue::Focus);
			}
			else
				toShelf(m_shelfAt - 1, true);
			break;
		case Button::Right:
			if (m_shelfGame + 1 < (int)shelf.games.size())
			{
				m_shelfGame++;
				m_feedback.Play(ui::Cue::Focus);
			}
			else
				toShelf(m_shelfAt + 1);
			break;
		case Button::L1: toShelf(m_shelfAt - 1); break;
		case Button::R1: toShelf(m_shelfAt + 1); break;
		case Button::L2:
		case Button::R2:
		{
			// a decade back or on, to the nearest console of that time
			const int wanted = shelf.year + (press.button == Button::R2 ? 10 : -10);
			int best = m_shelfAt;
			for (int i = 0; i <= last; i++)
				if (std::abs(m_shelves[i].year - wanted) < std::abs(m_shelves[best].year - wanted))
					best = i;
			toShelf(best);
			break;
		}
		case Button::Cross:
			if (!shelf.games.empty())
			{
				m_insertAt = m_now;
				m_feedback.Play(ui::Cue::Select);
			}
			else
			{
				// an empty shelf: its side, with its own words on where its games go
				const System side = shelf.side;
				Show(ScreenId::Library);
				SwitchSide(side);
			}
			break;
		case Button::Circle:
			m_feedback.Play(ui::Cue::Back);
			Show(ScreenId::Library);
			break;
		default: break;
		}
	}

	void Shell::TimelineDraw(Canvas& canvas)
	{
		if (m_shelves.empty())
			return;
		m_shelfAt = std::clamp(m_shelfAt, 0, (int)m_shelves.size() - 1);
		TimelineShelf& shelf = m_shelves[m_shelfAt];
		m_shelfGame = std::clamp(m_shelfGame, 0, std::max(0, (int)shelf.games.size() - 1));
		Game* focus = shelf.games.empty() ? nullptr : &shelf.games[m_shelfGame];
		const Media& media = kMedia[shelf.media];

		// Cross has put it in the slot: it starts as the cartridge has gone in
		constexpr double kInsertSeconds = 0.7;
		const float inserted = m_insertAt > 0 ? ui::ease::CubicOut(std::min(1.0f, (float)((m_now - m_insertAt) / (m_settings.ui.reduceMotion ? 0.01 : 0.56)))) : 0.0f;
		if (m_insertAt > 0 && m_now - m_insertAt > kInsertSeconds && focus)
		{
			const System side = shelf.side;
			const Game game = *focus;
			m_insertAt = 0;
			StartFromAnySide(side, game);
			return;
		}

		// the top: its name, what it is, Back, how many, the clock
		int games = 0;
		for (const TimelineShelf& one : m_shelves)
			games += (int)one.games.size();
		char clock[16] = "";
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		if (localtime_r(&now, &local))
			std::strftime(clock, sizeof(clock), "%H:%M", &local);
		canvas.PushAlpha(Enter(0));
		const float logo = DrawWordmark(canvas, kLeft, 71, 34, kInk);
		canvas.Rect({kLeft + logo + 18, 56, 2, 30}, 0, ui::SetAlpha(kInk, 0.25f));
		// tr: the view of every console's games on one shelf, opened with the touchpad
		const ui::TextBlock heading = canvas.Text(Style({32, ui::Weight::Bold, 1.0f, 0.3f, true}), kLeft + logo + 38, 54, Tr("Timeline"), kInk, 300, 1);
		canvas.Text(Style({16, ui::Weight::Medium, 1.0f, 0.6f, true}), kLeft + logo + 58 + heading.width, 64, Tr("Every console, oldest to newest"), kInk3, 620, 1);
		const ui::TextStyle right = Style({20, ui::Weight::Medium, 1.0f, 0.4f, true, true});
		float rightEdge = 1920 - kSafeX;
		const ui::TextBlock time = m_fonts.Layout(right, clock);
		rightEdge -= time.width;
		canvas.Text(time, rightEdge, 58, kInk);
		const ui::TextBlock count = m_fonts.Layout(right, TrP(games, "{0} game", "{0} games"));
		rightEdge -= count.width + 28;
		canvas.Text(count, rightEdge, 58, kInk);
		canvas.PopAlpha();

		// the ruler: its years, a mark for each console, a band over the consoles in view
		const float ruler = 224;
		canvas.PushAlpha(Enter(0));
		canvas.Rect({kLeft, ruler, kView, 2}, 0, kInk);
		const ui::TextStyle yearStyle = Style({14, ui::Weight::Medium, 1.0f, 0.3f, false, true});
		for (int year = kFirstYear; year < kFirstYear + kYears; year++)
		{
			const float x = YearX(year);
			const bool five = year % 5 == 0;
			canvas.Rect({x - 1, ruler + 2, 2, five ? 12.0f : 6.0f}, 0, kInk);
			if (five)
			{
				const ui::TextBlock label = m_fonts.Layout(yearStyle, std::to_string(year));
				canvas.Text(label, x - label.width * 0.5f, ruler + 16, kInk3);
			}
		}
		float lanes[3] = {-999, -999, -999};
		const ui::TextStyle markStyle = Style({14, ui::Weight::Bold, 1.0f});
		for (int i = 0; i < (int)m_shelves.size(); i++)
		{
			const TimelineShelf& one = m_shelves[i];
			const float x = YearX(one.year);
			int lane = 0;
			while (lane < 2 && x - lanes[lane] < 84)
				lane++;
			lanes[lane] = x;
			const float y = 184 - lane * 30;
			const uint32_t colour = AccentOf(one.side) | 0xff000000;
			canvas.Rect({x - 1, y + 16, 2, ruler - (y + 16)}, 0, ui::SetAlpha(colour, 0.7f));
			const float size = i == m_shelfAt ? 24.0f : 16.0f;
			const Box dot{x - size / 2, y + 8 - size / 2, size, size};
			canvas.Rect(dot.Scaled(1.25f), size * 0.625f, kPaper);
			canvas.Rect(dot, size / 2, colour);
			if (i == m_shelfAt)
				canvas.Ring(dot.Scaled(1.3f), size * 0.65f, 2, kInk);
			canvas.Text(markStyle, x + size / 2 + 6, y, one.brief, kInk, 90, 1);
		}
		canvas.PopAlpha();

		// the shelf: its boards, slid to the one in focus
		float total = 0;
		for (const TimelineShelf& one : m_shelves)
			total += one.w;
		const float wanted = std::min(shelf.x, std::max(0.0f, total - kView));
		// a long shelf slides on with its game in focus
		float slideTo = wanted;
		if (focus)
		{
			const float itemRight = shelf.x + 20 + (m_shelfGame + 1) * media.w + m_shelfGame * kItemGap + 40;
			slideTo = std::max(wanted, std::min(itemRight - kView, std::max(0.0f, total - kView)));
		}
		m_shelfScroll.target = slideTo;
		if (m_shelfScroll.value < 0)
			m_shelfScroll.value = slideTo;
		m_shelfScroll.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		m_shelfScroll.Update(m_dt);
		const float scroll = m_shelfScroll.value;

		const Box view{kLeft, kShelfTop, kView, 360};
		canvas.PushAlpha(Enter(1));
		canvas.PushClip(view);
		canvas.Rect({kLeft, kShelfBottom, kView, 8}, 2, kInk);
		canvas.LinearGradient({kLeft, kShelfBottom + 8, kView, 26}, 0, ui::SetAlpha(kInk, 0.18f), ui::SetAlpha(kInk, 0), 0, kShelfBottom + 8, 0, kShelfBottom + 34);
		auto drawItem = [&](TimelineShelf& one, Game& game, const Media& m, const Box& box, bool drawLabel) {
			const float scale = box.w / m.w;
			canvas.Shadow(box, m.radius * scale, 6, 14, 0x29000000);
			canvas.Rect(box, m.radius * scale, m.body);
			if (m.ridge > 0)
				for (float y = 0; y < m.ridge * scale; y += 5 * scale)
					canvas.Rect({box.x, box.y + y, box.w, 2 * scale}, 0, 0x2effffff);
			if (m.band > 0)
				canvas.Rect({box.x, box.y, box.w, m.band * scale}, 0, m.bandColour);
			const Box label{box.x + m.labelX * scale, box.y + m.labelY * scale, box.w - 2 * m.labelX * scale, m.labelH * scale};
			const float labelRadius = m.labelRadius * scale;
			bool art = false;
			AsSide(one.side, [&] {
				const ui::Picture& picture = Cover(game);
				if (picture.texture)
				{
					canvas.ImageCover(picture.texture, picture.width, picture.height, label, labelRadius, 0xffffffff, 0.0f);
					art = true;
				}
			});
			if (!art)
			{
				const uint32_t accent = AccentOf(one.side) | 0xff000000;
				canvas.LinearGradient(label, labelRadius, ui::Mix(accent, 0xff000000, 0.55f), accent, label.x, label.y, label.Right(), label.Bottom());
				if (drawLabel && !m.disc && m.w >= 99)
					canvas.Text(Style({12 * scale, ui::Weight::Bold, 1.1f}), label.x + 6 * scale, label.Bottom() - 34 * scale, game.entry.game.name, 0xffffffff, label.w - 12 * scale, 2);
			}
			if (m.disc)
			{
				const float hole = 8 * scale;
				canvas.Rect({box.CentreX() - hole, box.CentreY() - hole, hole * 2, hole * 2}, hole, kPaper);
				canvas.Ring({box.CentreX() - hole, box.CentreY() - hole, hole * 2, hole * 2}, hole, 2 * scale, Rgb(0x9da1aa));
			}
		};
		Box focused{};
		for (int i = 0; i < (int)m_shelves.size(); i++)
		{
			TimelineShelf& one = m_shelves[i];
			const float x = kLeft + one.x - scroll;
			if (x > 1920 || x + one.w < 0)
				continue;
			const Media& m = kMedia[one.media];
			canvas.Rect({x, kShelfTop, 1, kShelfBottom - kShelfTop}, 0, ui::SetAlpha(kInk, 0.14f));
			const uint32_t colour = AccentOf(one.side) | 0xff000000;
			canvas.Rect({x + 20, kShelfTop + 12, 12, 12}, 6, colour);
			ui::TextStyle shelfName = Style({28, ui::Weight::Bold, 1.0f, 0.2f, true});
			shelfName.size = m_fonts.Fit(shelfName, SystemName(one.side), one.w - 56, 1, 16, 1);
			canvas.Text(shelfName, x + 42, kShelfTop + 6, SystemName(one.side), kInk, one.w - 56, 1);
			const std::string sub = std::to_string(one.year) + " · " + (one.games.empty() ? std::string(Tr("No games yet")) : TrP((int)one.games.size(), "{0} game", "{0} games"));
			canvas.Text(Style({14, ui::Weight::Medium, 1.0f, 0.4f, true, true}), x + 20, kShelfTop + 42, sub, kInk3, one.w - 40, 1);
			if (one.games.empty())
			{
				const Box empty{x + 20, kShelfBottom - 120, 220, 120};
				canvas.Ring(empty, 10, 2, ui::SetAlpha(kInk, 0.3f));
				canvas.Text(Style({16, ui::Weight::SemiBold, 1.0f}), empty.x + 14, empty.y + 22, Tr("An empty shelf"), kInk, empty.w - 28, 1);
				canvas.Text(Style({11, ui::Weight::Medium, 1.2f}), empty.x + 14, empty.y + 50, GamesFolder(one.side), kInk3, empty.w - 28, 3);
				continue;
			}
			float itemX = x + 20;
			for (int j = 0; j < (int)one.games.size(); j++)
			{
				const Box box{itemX, kShelfBottom - m.h, m.w, m.h};
				itemX += m.w + kItemGap;
				if (box.Right() < 0)
					continue;
				if (box.x > 1920)
					break;
				if (i == m_shelfAt && j == m_shelfGame)
					focused = box;
				else
					drawItem(one, one.games[j], m, box, true);
			}
		}
		if (focused.w > 0 && focus)
		{
			// lifted off the shelf, over its neighbours
			const float scale = m_settings.ui.reduceMotion ? 1.0f : 1.12f;
			const Box lifted{focused.x - focused.w * (scale - 1) * 0.5f, focused.Bottom() - focused.h * scale - 36, focused.w * scale, focused.h * scale};
			canvas.Shadow(lifted, media.radius * scale, 30, 40, 0x52000000);
			drawItem(shelf, *focus, media, lifted, true);
			canvas.Ring(lifted, media.radius * scale, 3, kInk);
		}
		canvas.PopClip();
		canvas.PopAlpha();

		// the game in focus, and the slot it goes in
		canvas.PushAlpha(Enter(2));
		const float x = kLeft, width = 1080;
		if (focus)
		{
			const int year = focus->year ? focus->year : shelf.year;
			canvas.Text(Style({17, ui::Weight::Medium, 1.0f, 0.6f, true, true}), x, 712, std::string(SystemName(shelf.side)) + " · " + std::to_string(year), kInk3, width, 1);
			const std::string& gameName = focus->entry.game.name;
			const ui::TextBlock title = canvas.Text(Style({gameName.size() > 22 ? 56.0f : 72.0f, ui::Weight::Bold, 1.04f}), x, 742, gameName, kInk, width, 2);
			std::string facts = Played(*focus);
			const std::string last = LastPlayedWords(focus->entry.lastPlayed);
			if (!last.empty())
				facts += (facts.empty() ? "" : " · ") + TrF("Last played {0}", last);
			const float factsY = 742 + title.height + 12;
			canvas.Text(Style({22, ui::Weight::Regular, 1.2f}), x, factsY, facts, kInk2, width, 1);
			const ui::TextStyle playStyle = Style({22, ui::Weight::SemiBold, 1.0f});
			const std::string play = Tr("Play");
			const Box button{x, factsY + 44, m_fonts.Width(playStyle, play) + 96, 60};
			canvas.Rect(button, 30, kInk);
			canvas.Draw(Icon::Cross, {button.x + 28, button.CentreY() - 12, 24, 24}, 0xffffffff);
			const ui::TextBlock label = m_fonts.Layout(playStyle, play);
			canvas.Text(label, button.x + 62, button.CentreY() - label.height * 0.5f, 0xffffffff);

			// the slot: the game over it, going in
			const Box machine{1364, 866, 460, 124};
			const float fit = std::min(1.4f, 150 / media.h);
			const Box cart{machine.CentreX() - media.w * fit / 2, 720 + inserted * 150, media.w * fit, media.h * fit};
			canvas.PushClip({1364, 690, 460, 300});
			drawItem(shelf, *focus, media, cart, false);
			canvas.PopClip();
			canvas.Rect({machine.x, machine.y, machine.w, 148}, 22, kInk);
			canvas.Rect({machine.CentreX() - 150, machine.y + 26, 300, 14}, 7, 0xff000000);
			const ui::TextBlock caption = m_fonts.Layout(Style({15, ui::Weight::Medium, 1.0f, 0.4f, true}), SystemName(shelf.side));
			canvas.Text(caption, machine.CentreX() - caption.width * 0.5f, machine.y + 58, Rgb(0xd7d9de));
			canvas.Rect({machine.CentreX() - 5, machine.y + 90, 10, 10}, 5, m_insertAt > 0 ? Rgb(0x2bd67b) : Rgb(0x5b6070));
		}
		else
		{
			canvas.Text(Style({17, ui::Weight::Medium, 1.0f, 0.6f, true, true}), x, 712, std::string(SystemName(shelf.side)) + " · " + std::to_string(shelf.year), kInk3, width, 1);
			const ui::TextBlock title = canvas.Text(Style({72, ui::Weight::Bold, 1.04f}), x, 742, Tr("An empty shelf"), kInk, width, 1);
			canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), x, 754 + title.height, TrF("Put its games in {0}, on the PS5 or a USB drive.", GamesFolder(shelf.side)), kInk2, width, 2);
		}
		canvas.PopAlpha();

		// its hints, in its ink
		const ui::TextStyle hint = Style({18, ui::Weight::Regular, 1.0f});
		struct Item
		{
			Icon first, second;
			bool two;
			std::string words;
		};
		const Item items[3] = {{Icon::L1, Icon::R1, true, Tr("Previous or next console")}, {Icon::L2, Icon::R2, true, Tr("Jump a decade")}, {Icon::Circle, Icon::Circle, false, Tr("Back")}};
		float at = 1920 - kSafeX;
		const float hintY = 1080 - 26 - 28;
		for (int i = 2; i >= 0; i--)
		{
			const ui::TextBlock words = m_fonts.Layout(hint, items[i].words);
			at -= words.width;
			canvas.Text(words, at, hintY + 14 - words.height * 0.5f, kInk2);
			at -= 32;
			canvas.Draw(items[i].second, {at, hintY, 26, 28}, kInk2);
			if (items[i].two)
			{
				at -= 30;
				canvas.Draw(items[i].first, {at, hintY, 26, 28}, kInk2);
			}
			at -= 30;
		}
	}
}
