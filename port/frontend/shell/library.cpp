// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Library, where every side opens, the launcher's one layout (Meyer Oscar's, 6.11).
// Recently played first: the side's games by when they were last played, a row of their box art under
// Today, This week and Earlier, the focused one's name, play time, when it was played and its update
// under the row. Then the Library: Sort (Square) and Search (Triangle), its filters with their counts
// (All, Recently added, Favourites, Graphic packs on) when Settings > Launcher has them on, and every
// game on the side as a list, a row
// each with its cover, its name, when it was played and how it runs; L2 and R2 jump to the previous or
// next letter (or year, or status), an index of the letters at the right for A to Z. On the 3DS side the
// list's first row is Artic Base. Every cover is the whole box art at its own shape, never cut to fit.
// Before there is a game to show (a notice, Cemu starting, the first scan, no games yet) a card says
// why, with what to do (6.1: what Home showed). The theme (ui/theme.h) only changes how it looks.

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/emulator.h"
#include "../../eden/library.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <random>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		// tr: the Library's orders (Square)
		constexpr const char* kSorts[] = {TrMark("Recently played"), TrMark("A to Z"), TrMark("Release year"), TrMark("How it runs")};

		std::vector<std::string> SortNames()
		{
			std::vector<std::string> names;
			for (const char* sort : kSorts)
				names.push_back(Tr(sort));
			return names;
		}

		constexpr int kRecentMost = 8;
		constexpr float kRecentTop = 224, kRecentHeight = 300, kRecentGap = 24;
		constexpr float kListPitch = 104;

		int StatusRank(const ps5compat::Report* report)
		{
			if (!report)
				return 4;
			const std::string kind = ps5compat::Kind(report->status);
			return report->status == "Playable" ? 0 : kind == "good" ? 0 : kind == "warn" ? 1 : report->status == "Crashes" ? 2 : 3;
		}

		// a game's facts in a line, the empty ones left out
		std::string Facts(const std::vector<std::string>& facts)
		{
			std::string line;
			for (const std::string& fact : facts)
				if (!fact.empty())
					line += (line.empty() ? "" : " · ") + fact;
			return line;
		}

		// when a game was played, as Recently played groups them: 0 today, 1 this week, 2 earlier
		int Group(int64_t when)
		{
			const int64_t now = std::time(nullptr);
			std::tm today{}, then{};
			const std::time_t t0 = now, t1 = when;
			if (localtime_r(&t0, &today) && localtime_r(&t1, &then) && today.tm_yday == then.tm_yday && today.tm_year == then.tm_year)
				return 0;
			return now - when < 7 * 86400 ? 1 : 2;
		}
	}

	int Shell::LibraryFilters() const
	{
		return (IsWiiU() && m_status.coreReady ? 4 : 3) + (HiddenFilter() >= 0 ? 1 : 0);
	}

	// Hidden, after the others while the side has a game hidden (from the game menu); -1 when it has none
	int Shell::HiddenFilter() const
	{
		for (const Game& game : m_games)
			if (game.entry.hidden)
				return IsWiiU() && m_status.coreReady ? 4 : 3;
		return -1;
	}

	int Shell::LibraryFilter() const
	{
		const int filter = m_settings.ui.libraryFilter[SideIndex()];
		return m_settings.ui.libraryFilters && filter < LibraryFilters() ? filter : 0;
	}

	int Shell::LibraryTop() const
	{
		return !m_recent.empty() ? 0 : m_settings.ui.libraryFilters ? 1 : 2;
	}

	int Shell::LibraryGame() const
	{
		if (m_libraryZone == 0)
			return m_recentIndex >= 0 && m_recentIndex < (int)m_recent.size() && m_recent[m_recentIndex] < (int)m_games.size() ? m_recent[m_recentIndex] : -1;
		const int at = m_libraryIndex - ArticTiles();
		const int game = at >= 0 && at < (int)m_shelf.size() ? m_shelf[at] : -1;
		return game < (int)m_games.size() ? game : -1; // a list of a side being left has none
	}

	Box Shell::CoverBox(Game& game, float x, float bottom, float height, float maxWidth)
	{
		// the box art's own shape once it has loaded, else its case's: the Wii U's tall, the 3DS's and
		// the DS's a little wider than tall
		const ui::Picture& picture = Cover(game);
		float aspect = IsWiiU() ? 0.714f : 1.08f;
		if (picture.texture && picture.width > 0 && picture.height > 0)
			aspect = (float)picture.width / picture.height;
		float w = height * aspect, h = height;
		if (w > maxWidth)
		{
			w = maxWidth;
			h = w / aspect;
		}
		return {x, bottom - h, w, h};
	}

	bool Shell::LibraryState(std::string& kicker, std::string& title, std::string& text, std::vector<std::string>& buttons) const
	{
		buttons.clear();
		if (!Notice().empty())
		{
			kicker = Tr("Setup");
			title = Tr("Something needs a look");
			text = Notice();
			buttons = {Tr("Setup check"), Tr("Diagnostics")};
			return true;
		}
		// while Cemu's library loads, the games the catalogue has are shown (Play waits for it)
		if (!CoreReady() && (!m_status.coreStarting || m_games.empty()))
		{
			kicker = "Wii U";
			title = Tr("Starting Cemu");
			text = Tr("Its settings, graphic packs and controllers are loading, and then it looks for your games.");
			return true;
		}
		if (!m_games.empty())
			return false;
		if (m_scanning)
		{
			kicker = SystemName(m_side);
			title = Tr("Looking for games…");
			text = TrF("In {0}.", ShortPath(GamesFolder(m_side), 60));
			return true;
		}
		const std::string& folder = GamesFolder(m_side);
		kicker = Tr("Welcome");
		title = Tr("Your games go here");
		// tr: {0} is the games folder
		text = IsRetro() ? TrF("Put this system's supported game files in {0}, or choose a local or USB folder. Subfolders are searched to four levels. GameCube and Wii use separately selected folders.", folder) : Is3ds() ? TrF("Put your 3DS games (.3ds, .cci, .cxi, .3dsx, decrypted, alone or in .zip, .7z or .rar archives) in {0}, or "
							 "choose any folder the PS5 can read, such as one on a USB drive. Folders inside it are searched too.",
							 folder) :
			IsDs()	   ? TrF("Put your DS games (.nds, alone or in .zip, .7z or .rar archives) in {0}, or choose any folder the PS5 can read, "
							 "such as one on a USB drive. Folders inside it are searched too.",
							 folder) :
			// tr: {1} is Eden's game files folder, which has keys/ and firmware/ in it
			IsSwitch() ? TrF("Put your Switch games (.nsp or .xci, alone or each in a folder of its own with its update and DLC) in {0}. "
							 "Eden needs your Switch's keys in {1}/keys and its firmware in {1}/firmware too.",
							 ps5eden::ScanFolder(folder), ps5eden::GameFilesFolder()) :
						 TrF("Put your Wii U games (.wua, .wud, .wux, or folders with code, content and meta, alone or in .zip, .7z or .rar "
							 "archives) in {0}, or choose any folder the PS5 can read, such as one on a USB drive.",
							 folder);
		buttons = {Tr("Choose a folder"), Tr("Setup check")};
		if (Is3ds())
			buttons.insert(buttons.begin() + 1, Tr("Play from your 3DS")); // Artic Base needs no games here
		return true;
	}

	void Shell::LibraryStateUpdate(const ui::Press& press)
	{
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		const Button b = press.button;
		const int count = (int)buttons.size();
		if (b == Button::Up || b == Button::Circle)
			FocusBar();
		else if ((b == Button::Left || b == Button::Right) && count > 0)
		{
			m_stateButton = (m_stateButton + (b == Button::Right ? 1 : count - 1)) % count;
			m_feedback.Play(ui::Cue::Focus);
		}
		else if (b == Button::Cross)
		{
			if (count == 0)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			m_feedback.Play(ui::Cue::Select);
			// the buttons are in the menus' language: each is told by its words there
			const std::string& chosen = buttons[std::clamp(m_stateButton, 0, count - 1)];
			if (chosen == Tr("Setup check"))
				SetupOpen(false);
			else if (chosen == Tr("Play from your 3DS"))
			{
				m_pageFrom = ScreenId::Library;
				ArticOpen();
			}
			else if (chosen == Tr("Diagnostics"))
			{
				ShowTab(ScreenId::Settings);
				SettingsOpen("diagnostics");
			}
			else
			{
				m_pageFrom = ScreenId::Library;
				m_filesSide = m_side;
				FilesOpen(0);
			}
		}
	}

	void Shell::LibraryStateDraw(Canvas& canvas)
	{
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		WantBackdrop(nullptr);
		m_stateButtons.clear();
		canvas.PushAlpha(Enter(1));
		const Box card{kSafeX, 300, 1100, 520};
		canvas.Text(Style(kOverlineStyle), card.x, card.y, kicker, Secondary());
		const ui::TextBlock titleBlock = canvas.Text(Style(kDisplayStyle), card.x, card.y + 36, title, kText, card.w, 2);
		const ui::TextBlock textBlock = canvas.Text(Style(kBodyStyle), card.x, card.y + 56 + titleBlock.height, text, Secondary(), 960, 5);
		float x = card.x;
		m_stateButton = std::clamp(m_stateButton, 0, std::max(0, (int)buttons.size() - 1));
		for (int i = 0; i < (int)buttons.size(); i++)
		{
			const Box b = PillButton(canvas, x, card.y + 96 + titleBlock.height + textBlock.height, buttons[i], i == 0, !m_onBar && m_stateButton == i);
			m_stateButtons.push_back(b);
			x += b.w + 20;
		}
		canvas.PopAlpha();
	}

	void Shell::LibraryRefresh()
	{
		const int side = SideIndex();
		const int filter = LibraryFilter();
		const int sort = std::clamp(m_settings.ui.librarySort[side], 0, 3);
		const int focused = LibraryGame();
		if (focused >= 0)
			m_libraryFocusTitle = m_games[focused].entry.game.titleId;
		const int64_t recent = std::time(nullptr) - 30 * 86400;
		const std::string search = Lower(m_search);
		const int hidden = HiddenFilter();
		m_shelf.clear();
		for (int i = 0; i < (int)m_games.size(); i++)
		{
			Game& game = m_games[i];
			// a hidden game in the Hidden filter alone, and found by a search
			if (hidden >= 0 && filter == hidden ? !game.entry.hidden : game.entry.hidden && search.empty())
				continue;
			if (filter == 1 && game.entry.added < recent)
				continue;
			if (filter == 2 && !game.entry.favourite)
				continue;
			if (filter == 3 && IsWiiU() && filter != hidden)
			{
				if (game.packsOn < 0) // none known while Cemu's library loads; asked again once it has
					game.packsOn = m_status.coreReady ? ps5emu::EnabledGraphicPackCount(game.entry.game.titleId) : 0;
				if (game.packsOn == 0)
					continue;
			}
			// the keyboard's letters are Latin: a name in another script is found by GameTDB's English one too
			if (!search.empty() && Lower(game.entry.game.name).find(search) == std::string::npos &&
				Lower(game.info.title).find(search) == std::string::npos)
				continue;
			m_shelf.push_back(i);
		}
		std::stable_sort(m_shelf.begin(), m_shelf.end(), [&](int a, int b) {
			const Game& x = m_games[a];
			const Game& y = m_games[b];
			switch (sort)
			{
			case 0:
				if (x.entry.lastPlayed != y.entry.lastPlayed)
					return x.entry.lastPlayed > y.entry.lastPlayed;
				break;
			case 2:
				if (x.year != y.year)
					return (x.year ? x.year : -1) > (y.year ? y.year : -1);
				break;
			case 3:
				if (StatusRank(x.report) != StatusRank(y.report))
					return StatusRank(x.report) < StatusRank(y.report);
				break;
			default: break;
			}
			return x.sortName < y.sortName;
		});
		// the focus stays on the game it was on (or goes to the one asked for: the one played last)
		const int offset = ArticTiles();
		if (m_libraryIndex < 0 && !m_shelf.empty())
			m_libraryIndex = offset; // the first game, unless the one asked for is found below
		m_libraryIndex = std::clamp(m_libraryIndex, 0, std::max(0, (int)m_shelf.size() + offset - 1));
		for (int i = 0; i < (int)m_shelf.size(); i++)
			if (m_games[m_shelf[i]].entry.game.titleId == m_libraryFocusTitle)
				m_libraryIndex = i + offset;

		// Recently played: the games last played, the last first; the focus on its game, else on the one asked for
		m_recent.clear();
		for (int i = 0; i < (int)m_games.size(); i++)
			if (m_games[i].entry.lastPlayed > 0 && !m_games[i].entry.hidden)
				m_recent.push_back(i);
		std::stable_sort(m_recent.begin(), m_recent.end(), [&](int a, int b) { return m_games[a].entry.lastPlayed > m_games[b].entry.lastPlayed; });
		if ((int)m_recent.size() > kRecentMost)
			m_recent.resize(kRecentMost);
		m_recentIndex = 0;
		for (int i = 0; i < (int)m_recent.size(); i++)
			if (m_games[m_recent[i]].entry.game.titleId == m_libraryFocusTitle)
				m_recentIndex = i;
		m_libraryZone = std::max(m_libraryZone, LibraryTop());
		if (m_libraryZone == 1 && !m_settings.ui.libraryFilters)
			m_libraryZone = 2;
	}

	// Surprise me: the focus on a game of the list as it is (its filter and search, no hidden game), by
	// chance, never the one it is on when there are others
	void Shell::SurpriseMe()
	{
		if (m_shelf.empty())
		{
			m_feedback.Play(ui::Cue::Denied);
			return;
		}
		static std::mt19937 chance{std::random_device{}()};
		const int offset = ArticTiles();
		int pick = (int)(chance() % m_shelf.size());
		if (m_shelf.size() > 1 && m_libraryZone == 2 && pick + offset == m_libraryIndex)
			pick = (pick + 1) % (int)m_shelf.size();
		m_libraryZone = 2;
		m_libraryIndex = pick + offset;
		const Game& game = m_games[m_shelf[pick]];
		m_libraryFocusTitle = game.entry.game.titleId;
		m_feedback.Play(ui::Cue::Select);
		// tr: Surprise me's answer: {0} is the game it picked, now in focus in the Library
		Toast(TrF("How about {0}?", game.entry.game.name));
	}

	void Shell::LibraryUpdate(const ui::Press& press)
	{
		const Button b = press.button;
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				LibraryStateUpdate(press);
				return;
			}
		}
		const int side = SideIndex();
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		const int top = LibraryTop();
		const bool filters = m_settings.ui.libraryFilters;
		m_libraryZone = std::max(m_libraryZone, top);
		if (m_libraryZone == 1 && !filters)
			m_libraryZone = 2;
		auto zone = [&](int to) {
			m_libraryZone = to;
			m_feedback.Play(ui::Cue::Focus);
		};
		switch (b)
		{
		case Button::Square:
		{
			// the orders, then Surprise me, which goes to a game of the list by chance and keeps the order
			std::vector<std::string> choices = SortNames();
			// tr: the last choice of the Library's Sort list: a game of the list picked by chance, the order kept
			choices.push_back(Tr("Surprise me"));
			OpenPicker(TrC("tab", "Library"), Tr("Sort"), choices, m_settings.ui.librarySort[side], [this, side](int choice) {
				if (choice == (int)std::size(kSorts))
				{
					SurpriseMe();
					return;
				}
				m_settings.ui.librarySort[side] = choice;
				SaveSettings();
				LibraryRefresh();
			});
			return;
		}
		case Button::Triangle: OpenKeyboard(); return;
		case Button::Circle:
			// the search let go, then up to the top, then the bar: the Library is where a side opens
			if (!m_search.empty())
			{
				m_search.clear();
				LibraryRefresh();
				m_feedback.Play(ui::Cue::Back);
			}
			else if (m_libraryZone > top)
			{
				m_libraryZone = top;
				m_feedback.Play(ui::Cue::Back);
			}
			else
				FocusBar();
			return;
		case Button::Options:
			if (LibraryGame() >= 0)
				OpenGameMenu(LibraryGame());
			return;
		case Button::Cross:
			if (m_libraryZone == 1)
			{
				if (total > 0)
					zone(2);
				else
					m_feedback.Play(ui::Cue::Edge, press.repeat);
			}
			else if (m_libraryZone == 2 && m_libraryIndex < offset)
			{
				m_feedback.Play(ui::Cue::Select);
				m_pageFrom = ScreenId::Library;
				ArticOpen();
			}
			else if (LibraryGame() >= 0)
				Launch(LibraryGame());
			else
				m_feedback.Play(ui::Cue::Denied);
			return;
		default: break;
		}
		if (m_libraryZone == 0)
		{
			switch (b)
			{
			case Button::Left:
			case Button::Right:
			{
				const int next = m_recentIndex + (b == Button::Right ? 1 : -1);
				if (next < 0 || next >= (int)m_recent.size())
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					m_recentIndex = next;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			}
			case Button::Up: FocusBar(); break;
			case Button::Down:
				if (filters || total > 0)
					zone(filters ? 1 : 2);
				else
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				break;
			default: break;
			}
		}
		else if (m_libraryZone == 1)
		{
			int& filter = m_settings.ui.libraryFilter[side];
			switch (b)
			{
			case Button::Left:
			case Button::Right:
			{
				const int next = filter + (b == Button::Right ? 1 : -1);
				if (next < 0 || next >= LibraryFilters())
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					filter = next;
					SaveSettings();
					m_libraryIndex = 0;
					m_libraryFocusTitle = 0;
					LibraryRefresh();
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			}
			case Button::Up:
				if (top == 0)
					zone(0);
				else
					FocusBar();
				break;
			case Button::Down:
				if (total > 0)
					zone(2);
				else
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				break;
			default: break;
			}
		}
		else if (total > 0)
		{
			int& at = m_libraryIndex;
			at = std::clamp(at, 0, total - 1);
			auto move = [&](int to) {
				to = std::clamp(to, 0, total - 1);
				if (to == at)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					at = to;
					m_feedback.Play(ui::Cue::Focus);
				}
			};
			switch (b)
			{
			case Button::Up:
				if (at > 0)
					move(at - 1);
				else if (top < 2)
					zone(filters ? 1 : 0);
				else
					FocusBar();
				break;
			case Button::Down: move(at + 1); break;
			case Button::L2:
			case Button::R2:
			{
				// to the previous or next group: letter, year or status, as sorted
				const int sort = m_settings.ui.librarySort[side];
				auto group = [&](int index) -> int {
					if (index < offset)
						return -1;
					const Game& game = m_games[m_shelf[index - offset]];
					if (sort == 2)
						return game.year;
					if (sort == 3)
						return StatusRank(game.report);
					if (sort == 0)
						return (index - offset) / 10;
					const char c = game.sortName.empty() ? '#' : game.sortName[0];
					return std::isalpha((unsigned char)c) ? c : '#';
				};
				const int current = group(at);
				int to = at;
				if (b == Button::R2)
				{
					while (to < total - 1 && group(to) == current)
						to++;
				}
				else
				{
					while (to > 0 && group(to - 1) == current)
						to--;
					if (to == at && to > 0)
					{
						const int previous = group(to - 1);
						to--;
						while (to > 0 && group(to - 1) == previous)
							to--;
					}
				}
				move(to);
				break;
			}
			default: break;
			}
		}
		else if (b == Button::Up)
		{
			if (top < 2)
				zone(filters ? 1 : 0);
			else
				FocusBar();
		}
		if (LibraryGame() >= 0)
			m_libraryFocusTitle = m_games[LibraryGame()].entry.game.titleId;
	}

	void Shell::LibraryDraw(Canvas& canvas)
	{
		DrawBar(canvas, true);
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				LibraryStateDraw(canvas);
				return;
			}
		}
		const int side = SideIndex();
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		const bool recent = !m_recent.empty(), filters = m_settings.ui.libraryFilters;
		m_libraryZone = std::max(m_libraryZone, LibraryTop());
		const float headTop = recent ? 590.0f : 132.0f, filterTop = headTop + 58, listTop = filterTop + (filters ? 76 : 8);
		const int game = LibraryGame();
		WantBackdrop(game >= 0 ? &m_games[game] : nullptr);

		// the page scrolls as one: past the list's first row, Recently played goes up and away, the
		// Library's heading at the top; then only as far as the focused row needs to be seen
		float target = m_libraryScroll.target;
		if (m_libraryZone < 2 || m_libraryIndex == 0)
			target = 0;
		else
		{
			const float rowTop = listTop + m_libraryIndex * kListPitch, rowBottom = rowTop + kListPitch - 12;
			if (rowBottom - target > 960)
				target = rowBottom - 960;
			if (rowTop - target < 196)
				target = rowTop - 196;
			target = std::max(target, headTop - 132);
		}
		m_libraryScroll.target = target;
		m_libraryScroll.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		m_libraryScroll.Update(m_dt);
		const float scroll = m_libraryScroll.value;

		canvas.PushAlpha(Enter(1));
		canvas.PushClip({0, 116, 1920, 964});
		canvas.PushOffset(0, -scroll);
		const ui::TextStyle heading = Style({32, ui::Weight::SemiBold, 1.0f});
		const ui::TextStyle tool = Style({22, ui::Weight::Medium, 1.0f});

		// Recently played
		if (recent)
		{
			canvas.Text(heading, kSafeX, 132, Tr("Recently played"), kText, 900, 1);
			// tr: when the games in Recently played were last played: the labels over the row
			const char* groups[3] = {Tr("Today"), Tr("This week"), Tr("Earlier")};
			float x = kSafeX;
			int lastGroup = -1;
			Box focusedBox{};
			for (int i = 0; i < (int)m_recent.size(); i++)
			{
				Game& g = m_games[m_recent[i]];
				const Box box = CoverBox(g, x, kRecentTop + kRecentHeight, kRecentHeight, 420);
				const int group = Group(g.entry.lastPlayed);
				if (group != lastGroup)
				{
					canvas.Text(Style(kOverlineStyle), box.x, 176, groups[group], Tertiary(), 0, 1);
					lastGroup = group;
				}
				const bool focused = !m_onBar && m_libraryZone == 0 && i == m_recentIndex;
				if (box.x < 1920)
				{
					if (focused)
						focusedBox = box;
					else
						DrawCover(canvas, g, box, false, 0);
				}
				x = box.Right() + kRecentGap + (focused ? 12 : 0);
			}
			if (focusedBox.w > 0)
			{
				// lifted from its bottom left, so it rises clear of its neighbours
				const float lift = m_settings.ui.reduceMotion ? 0.0f : 0.05f;
				const Box lifted{focusedBox.x, focusedBox.Bottom() - focusedBox.h * (1 + lift), focusedBox.w * (1 + lift), focusedBox.h * (1 + lift)};
				DrawCover(canvas, m_games[m_recent[m_recentIndex]], lifted, true, 0);
			}
			// the game in focus there (or played last), under the row: its name, play time, when it was last
			// played, its update and DLC
			Game& g = m_games[m_recent[std::clamp(m_recentIndex, 0, (int)m_recent.size() - 1)]];
			const ui::TextBlock name = canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), kSafeX, 544, g.entry.game.name, kText, 900, 1);
			std::vector<std::string> facts = {Played(g), LastPlayedWords(g.entry.lastPlayed)};
			const ps5emu::Game& entry = g.entry.game;
			if (IsWiiU() && entry.hasUpdate)
				// tr: the update a Wii U game has installed ({0}: its version, as 208)
				facts.push_back(TrF("Update v{0}", entry.version));
			else if (IsSwitch() && entry.hasUpdate && !entry.versionName.empty())
				facts.push_back(TrF("Update v{0}", entry.versionName)); // a Switch game's, as it shows it (1.2.0)
			if (entry.dlcCount)
				facts.push_back(TrP(entry.dlcCount, "{0} DLC", "{0} DLC"));
			canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), kSafeX + name.width + 16, 548, Facts(facts), Secondary(), 1920 - 2 * kSafeX - name.width - 16, 1);
		}

		// the Library: its heading and count, Sort and Search at the right
		const ui::TextBlock title = canvas.Text(heading, kSafeX, headTop, TrC("tab", "Library"), kText, 600, 1);
		const int listed = (int)std::count_if(m_games.begin(), m_games.end(), [](const Game& g) { return !g.entry.hidden; }); // as All counts them
		// tr: how many games the Library has
		const ui::TextBlock count = canvas.Text(tool, kSafeX + title.width + 24, headTop + 9, TrP(listed, "{0} game", "{0} games"), Tertiary(), 300, 1);
		if (m_scanning)
			canvas.Text(Style(kCaptionStyle), kSafeX + title.width + 24 + count.width + 24, headTop + 11, Tr("Looking for new games…"), Tertiary(), 360, 1);
		else if (m_status.lastLook[side])
			// tr: the Library's list is the one found on the network share last time (#75); Look for games now is in Settings > Game files
			canvas.Text(Style(kCaptionStyle), kSafeX + title.width + 24 + count.width + 24, headTop + 11, Tr("As found last time"), Tertiary(), 360, 1);
		{
			float right = 1920 - kSafeX;
			const ui::TextBlock search = m_fonts.Layout(tool, m_search.empty() ? std::string(Tr("Search")) : TrF("Search: {0}", m_search));
			right -= search.width;
			canvas.Text(search, right, headTop + 20 - search.height * 0.5f, m_search.empty() ? Secondary() : kText);
			canvas.Draw(Icon::Triangle, {right - 38, headTop + 5, 30, 30}, Secondary());
			right -= 38 + 34;
			const ui::TextBlock sort = m_fonts.Layout(tool, TrF("Sort: {0}", Tr(kSorts[std::clamp(m_settings.ui.librarySort[side], 0, 3)])));
			right -= sort.width;
			canvas.Text(sort, right, headTop + 20 - sort.height * 0.5f, Secondary());
			canvas.Draw(Icon::Square, {right - 38, headTop + 5, 30, 30}, Secondary());
		}

		// the filters, with their counts (Settings > Launcher)
		if (filters)
		{
			const int64_t added = std::time(nullptr) - 30 * 86400;
			const int hidden = HiddenFilter();
			int counts[5] = {0, 0, 0, 0, 0};
			for (Game& g : m_games)
			{
				if (g.entry.hidden)
				{
					counts[4]++;
					continue;
				}
				counts[0]++;
				counts[1] += g.entry.added >= added;
				counts[2] += g.entry.favourite;
				counts[3] += g.packsOn > 0;
			}
			// tr: the Library's filters, each with its count after it
			const char* names[5] = {TrC("filter", "All"), Tr("Recently added"), Tr("Favourites"), Tr("Graphic packs on"),
				// tr: the Library's filter of the games hidden from its list (the game menu's Hide from the Library)
				TrC("filter", "Hidden")};
			if (hidden >= 0)
			{
				// Hidden in the place after the others (the Wii U's Graphic packs on, or Favourites)
				names[hidden] = names[4];
				counts[hidden] = counts[4];
			}
			const int count = LibraryFilters();
			const int filter = LibraryFilter();
			ui::TextStyle pillStyle = Style({24, ui::Weight::Medium, 1.0f});
			ui::TextStyle countStyle = Style({19, ui::Weight::SemiBold, 1.0f, 0, false, true});
			auto filtersWidth = [&] {
				float width = 0;
				for (int i = 0; i < count; i++)
					width += 24 + m_fonts.Width(pillStyle, names[i]) + 12 + m_fonts.Width(countStyle, std::to_string(counts[i])) + 24 + 14;
				return width;
			};
			// a step smaller at a time in a language whose words do not fit the row
			for (int step = 0; step < 6 && filtersWidth() > 1920 - 2 * kSafeX; step++)
			{
				pillStyle.size -= 2;
				countStyle.size -= 1;
			}
			float x = kSafeX;
			for (int i = 0; i < count; i++)
			{
				const bool on = i == filter;
				const std::string number = i == 3 && i != hidden && counts[3] == 0 ? "" : std::to_string(counts[i]);
				const float w = 24 + m_fonts.Width(pillStyle, names[i]) + (number.empty() ? 0 : 12 + m_fonts.Width(countStyle, number)) + 24;
				const Box pill{x, filterTop, w, 48};
				canvas.Rect(pill, 24, on ? ui::SetAlpha(Accent(), 0.22f) : Surface());
				canvas.Ring(pill, 24, 1.5f, on ? ui::SetAlpha(Accent(), 0.5f) : kGlassEdge);
				const ui::TextBlock text = m_fonts.Layout(pillStyle, names[i]);
				canvas.Text(text, x + 24, pill.CentreY() - text.height * 0.5f, on ? kText : Secondary());
				if (!number.empty())
				{
					const ui::TextBlock digits = m_fonts.Layout(countStyle, number);
					canvas.Text(digits, x + 24 + text.width + 12, pill.CentreY() - digits.height * 0.5f, on ? Accent() : Tertiary());
				}
				if (!m_onBar && m_libraryZone == 1 && on)
					Focus(pill, 24);
				x += w + 14;
			}
		}

		// the list
		if (total == 0)
		{
			const int filter = LibraryFilter();
			canvas.Text(Style(kBodyStyle), kSafeX, listTop + 10,
				!m_search.empty() ? TrF("Nothing matches “{0}”. Circle clears the search.", m_search) :
				m_scanning		  ? std::string(Tr("Looking for games…")) :
				filter == 2		  ? std::string(Tr("Options on a game, then Favourite, puts it here.")) :
				filter			  ? std::string(Tr("All shows every game.")) :
									std::string(Tr("No games yet. Settings > Game files says where they go.")),
				Secondary(), 1200, 2);
		}
		for (int index = 0; index < total; index++)
		{
			const float rowTop = listTop + index * kListPitch;
			if (rowTop - scroll > 1080 || rowTop + kListPitch - scroll < 116)
				continue;
			const bool focused = !m_onBar && m_libraryZone == 2 && index == m_libraryIndex;
			// a row each: a small cover, the name, play time and when it was played, how it runs
			const Box row{kSafeX, rowTop, 1920 - 2 * kSafeX - 40, kListPitch - 12};
			canvas.Rect(row, kRadiusCard, focused ? Surface2() : Surface());
			if (focused)
				Focus(row, kRadiusCard, index < offset ? kN3ds : 0);
			if (index < offset)
			{
				// Artic Base: a game from your 3DS
				canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), row.x + 124, row.y + 18, Tr("Play from your 3DS"), kText, 1000, 1);
				canvas.Text(Style({21, ui::Weight::Regular, 1.0f}), row.x + 124, row.y + 54, Tr("Artic Base, over your network"), Secondary(), 1000, 1);
				continue;
			}
			Game& g = m_games[m_shelf[index - offset]];
			const Box cover = CoverBox(g, row.x + 14, row.Bottom() - 10, row.h - 20, 90);
			DrawCover(canvas, g, cover.Offset((90 - cover.w) * 0.5f, 0), false, 0);
			canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), row.x + 124, row.y + 18, g.entry.game.name, kText, 1000, 1);
			const std::string facts = Facts({Played(g), LastPlayedWords(g.entry.lastPlayed)});
			canvas.Text(Style({21, ui::Weight::Regular, 1.0f}), row.x + 124, row.y + 54, facts.empty() ? Byline(g) : facts, Secondary(), 1000, 1);
			if (g.report)
				Chip(canvas, row.Right() - 260, row.CentreY() - 17, TrC("status", g.report->status), ps5compat::Kind(g.report->status), 34);
		}
		canvas.PopOffset();
		canvas.PopClip();

		// the list fades into the page under its last row seen, over the hints
		canvas.LinearGradient({0, 880, 1920, 200}, 0, ui::SetAlpha(kInk0, 0), ui::SetAlpha(kInk0, 1), 0, 880, 0, 1020);
		const float viewTop = std::max(listTop - scroll, 196.0f), view = 960 - viewTop;
		if (m_settings.ui.librarySort[side] == 1 && m_shelf.size() > 12 && view > 160)
		{
			// sorted A to Z: an index of the letters at the right, the focused game's lit
			std::vector<char> letters;
			for (int index : m_shelf)
			{
				const char c = m_games[index].sortName.empty() ? '#' : (char)std::toupper((unsigned char)m_games[index].sortName[0]);
				const char letter = std::isalpha((unsigned char)c) ? c : '#';
				if (std::find(letters.begin(), letters.end(), letter) == letters.end())
					letters.push_back(letter);
			}
			const char current = game >= 0 && !m_games[game].sortName.empty() && std::isalpha((unsigned char)m_games[game].sortName[0]) ?
				(char)std::toupper((unsigned char)m_games[game].sortName[0]) :
				'#';
			const float step = std::min(25.0f, (view - 20) / letters.size());
			float y = viewTop + (view - letters.size() * step) / 2;
			for (char letter : letters)
			{
				const bool on = letter == current && game >= 0 && m_libraryZone == 2;
				if (on)
					canvas.Rect({1864 - 15, y - 2, 30, 30}, 15, Accent());
				canvas.Text(Style({16, ui::Weight::SemiBold, 1.0f}), 1864, y + 6, std::string(1, letter), on ? kInk1 : Tertiary(), 0, 0, ui::Align::Centre);
				y += step;
			}
		}
		else
		{
			// where in the list the page is: a thin bar at the right, while it has more than is seen
			const float content = total * kListPitch;
			if (total > 0 && content > view + 1 && view > 80)
			{
				const float seen = std::clamp(scroll + viewTop - listTop, 0.0f, content - view);
				const float thumb = std::max(48.0f, view * view / content);
				const float x = 1920 - 52 - 4;
				canvas.Rect({x, viewTop, 4, view}, 2, Wash(0.08f));
				canvas.Rect({x, viewTop + (view - thumb) * seen / std::max(1.0f, content - view), 4, thumb}, 2, Wash(0.4f));
			}
		}
		canvas.PopAlpha();
	}
}
