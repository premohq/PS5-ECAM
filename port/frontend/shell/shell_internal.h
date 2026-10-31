// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the new launcher's insides (shell.h). One Shell holds the state of every screen; each
// screen's file (library.cpp, hub.cpp, settings.cpp, pages.cpp, setup.cpp) has its
// Update, which takes the frame's buttons, and its Draw, which records the frame; shell.cpp has the
// loop, the bar, the hints, the backdrop and the overlays (the dropdown, a setting's help, the Game
// menu, the keyboard, the update sheet, toasts and the launch). widgets.cpp draws the parts they
// share, as the design's tokens say.

#pragma once

#include "../shell.h"
#include "../actions.h"
#include "../../app/boxart.h"
#include "../../app/catalog.h"
#include "../../app/compatibility.h"
#include "../../app/game_archives.h"
#include "../../app/gameinfo.h"
#include "../../app/lang.h"
#include "../../app/netfs.h"
#include "../../app/saves.h"
#include "../../retro/library.h"
#include "../../retro/launch.h"
#include "../../libretro/plays.h"
#include "../../eden/extras.h"
#include "../../ui/canvas.h"
#include "../../ui/feedback.h"
#include "../../ui/images.h"
#include "../../ui/input.h"
#include "../../ui/motion.h"
#include "../../ui/qr.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ps5shell
{
	using System = ps5launcher::System;
	using ui::Box;
	using ui::Button;
	using ui::Canvas;
	using ui::Icon;
	using ps5lang::Tr;
	using ps5lang::TrC;
	using ps5lang::TrF;
	using ps5lang::TrFC;
	using ps5lang::TrMark;
	using ps5lang::TrMarkC;
	using ps5lang::TrP;

	enum class ScreenId
	{
		WhatsNew, // what a version brings, once: on the first start and after an update (whatsnew.cpp)
		Welcome, // Make it yours: a first start's look and feel, or a new version's new options
		Setup,
		Library,
		Hub,
		Settings,
		Packs,
		Player,
		Mapping,
		Files,
		Artic,
		Updates, // Updates and DLC (6.12)
		Shares,	 // Network shares (6.14)
	};

	struct ShareJob; // shares.cpp: a test or a look around the network, on a thread of its own

	// A game as the shell shows it: the catalogue's entry, and what was looked up for it
	struct Game
	{
		ps5catalog::Entry entry;
		std::string boxArt;		  // GameTDB's cover, when fetched
		std::string icon;		  // the game's own icon, once looked for
		bool iconLooked = false;
		std::string bootScreen;	  // Wii U: its boot screen (bootTvTex), the backdrop, once looked for
		bool bootScreenLooked = false;
		const ps5compat::Report* report = nullptr;
		ps5gameinfo::Info info;
		bool known = false;		  // GameTDB knows it
		int year = 0;
		std::string sortName;	  // lower case, "the" moved out of the way
		int packsOn = -1;		  // Wii U: graphic packs on, once asked
	};

	struct Hint
	{
		Icon icon;
		std::string text;
	};

	// A row of Settings, or of a page's list
	struct Row
	{
		enum class Kind
		{
			Toggle,	   // on or off
			Choice,	   // one of options: Left and Right step, Cross opens the dropdown
			Slider,	   // 0 to 100 in steps
			Stepper,   // one of options, shown as pips
			Segmented, // two or three options side by side
			Swatches,  // the borders
			Action,	   // Cross does it; the value says what came of it
			Hold,	   // Cross held does it (hold to confirm)
			Link,	   // Cross opens a page
		} kind = Kind::Action;
		std::string id;
		std::string label, value;
		std::string description; // one line, under it while focused
		std::string help;		 // Triangle's
		bool on = false;		 // Toggle
		int index = 0;			 // Choice, Stepper, Segmented, Swatches: the one in use
		std::vector<std::string> options;
		float fraction = 0;		 // Slider
		bool dimmed = false;
		bool needsCore = false;	 // a Wii U row Cemu's core must be up for
		std::string setting;	 // a game can have its own of it: its name in ps5cemu.json (game settings)
		bool fromSide = false;	 // game settings: the side's value, the game has none of its own
		std::string group;		 // Settings: the › page it is on, behind its row (empty: the page itself)
		std::string opens;		 // Settings: a › row's page, whose rows it stands for
	};

	struct SettingsPage
	{
		std::string id, title, subtitle;
		Icon icon;
	};

	class Shell
	{
	public:
		Shell(ps5settings::Launcher& settings, ps5launcher::Status& status, const std::function<void(System)>& prepare);
		~Shell();
		Outcome Run(std::optional<ps5launcher::Choice>& choice);

		// -- the frame ------------------------------------------------------------------------
		void Frame();
		void Update(const ui::Actions& actions);
		void Draw(Canvas& canvas);
		void DrawBackdrop(Canvas& canvas);
		void DrawBubbles(Canvas& canvas, float alpha); // the Wii U side's motif, rising
		void DrawWaves(Canvas& canvas, float alpha);   // the 3DS side's, rolling
		void DrawPixels(Canvas& canvas, float alpha);  // the DS side's, rounded pixels rising
		void DrawJoyCons(Canvas& canvas, float alpha); // the Switch side's, rounded bars drifting in pairs
		void DrawSideMotif(Canvas& canvas, float alpha); // the side's own of the four
		void DrawBar(Canvas& canvas, bool tabs);
		void DrawHints(Canvas& canvas, const std::vector<Hint>& hints);
		void DrawOverlays(Canvas& canvas);
		std::vector<Hint> Hints() const;

		// -- sides and games ----------------------------------------------------------------------
		bool IsWiiU() const { return m_side == System::WiiU; }
		bool Is3ds() const { return m_side == System::N3ds; }
		bool IsDs() const { return m_side == System::Nds; }
		bool IsRetro() const { return ps5retro::IsRetro((int)m_side); }
		bool IsSwitch() const { return m_side == System::Switch; }
		static constexpr int kSides = ps5launcher::kSideCount;
		static int SideIndex(System side) { return (int)side; } // 0 Wii U, 1 3DS, 2 DS, 3 Switch: its bit in ui.sides, its catalogue
		int SideIndex() const { return SideIndex(m_side); }
		// the bar's order (the owner's, 2026-10-22): Wii U, Switch, DS, 3DS; m_barFocus counts in it
		static constexpr System kBarOrder[kSides] = {System::WiiU, System::Switch, System::Nds, System::N3ds, System::GameCube, System::Wii, System::N64, System::Snes, System::Nes, System::Gba, System::Gbc, System::Gb, System::VirtualBoy, System::PokemonMini};
		static int BarIndex(System side)
		{
			for (int i = 0; i < kSides; i++)
				if (kBarOrder[i] == side)
					return i;
			return 0;
		}
		static System BarSide(int position) { return kBarOrder[position < 0 ? 0 : position % kSides]; }
		// the sides the bar offers (Settings > Launcher, ui.sides): at least one
		bool SideShown(System side) const { return m_settings.ui.sides & (1 << SideIndex(side)); }
		int SidesShown() const
		{
			int shown = 0;
			for (int i = 0; i < kSides; i++)
				shown += m_settings.ui.sides >> i & 1;
			return shown;
		}
		System NextShownSide(System side) const;
		System FirstShownSide() const;
		void SidesChanged(); // a side hidden while open: the first one shown instead
		static const char* SideTitle(System side) { return ps5launcher::SideTitle(side); }
		static const char* SystemName(System side)
		{
			return ps5retro::IsRetro((int)side) ? ps5retro::Of((int)side).name : side == System::N3ds ? "Nintendo 3DS" : side == System::Nds ? "Nintendo DS" : side == System::Switch ? "Nintendo Switch" : "Wii U";
		}
		ps5catalog::System CatalogSide() const { return (ps5catalog::System)SideIndex(); }
		static ps5boxart::System BoxSide(System side)
		{
			return side == System::N3ds ? ps5boxart::System::N3ds : side == System::Nds ? ps5boxart::System::Nds :
				side == System::Switch	? ps5boxart::System::Switch :
										  ps5boxart::System::WiiU;
		}
		ps5boxart::System BoxOf(const ps5emu::Game&) const { return BoxSide(m_side); }
		bool CoreReady() const { return !IsWiiU() || m_status.coreReady; }
		// what a game's ID shows as: its title ID, a DS game's game code (the ID it is given stays out of
		// sight), a Switch game's title ID when its file's name has one (not the one made from its path,
		// ps5eden::kIdTag), else nothing
		std::string ShownId(const ps5emu::Game& game) const
		{
			if (IsDs())
				return game.gameId;
			if (IsSwitch() && (game.titleId >> 56) == 0x5A)
				return {};
			return ps5actions::Hex(game.titleId);
		}
		const std::string& Notice() const
		{
			return IsRetro() ? m_status.noticeRetro[ps5retro::Slot(SideIndex())] : Is3ds() ? m_status.notice3ds : IsDs() ? m_status.noticeDs : IsSwitch() ? m_status.noticeSwitch : m_status.notice;
		}
		bool SystemScanning() const;
		// the side's folder looked through again, its list replaced with what is found (also one a network
		// share's that this session kept the last look of, #75)
		void LookForGames();
		uint32_t Accent() const { return AccentOf(m_side); }
		static uint32_t AccentOf(System side)
		{
			return ps5retro::IsRetro((int)side) ? 0xffbca375 : side == System::N3ds ? ui::tokens::kN3ds : side == System::Nds ? ui::tokens::kNds : side == System::Switch ? ui::tokens::kNx : ui::tokens::kWiiu;
		}
		std::string& GamesFolder(System side) { return side == System::WiiU ? m_settings.gamesFolder : Handheld(side).gamesFolder; }
		const std::string& GamesFolder(System side) const { return side == System::WiiU ? m_settings.gamesFolder : Handheld(side).gamesFolder; }
		// a handheld side's settings (the 3DS's, the DS's or the Switch's: the same shape)
		ps5settings::N3ds& Handheld(System side) { return ps5retro::IsRetro((int)side) ? m_settings.retro[ps5retro::Slot((int)side)] : side == System::Nds ? m_settings.nds : side == System::Switch ? m_settings.nx : m_settings.n3ds; }
		const ps5settings::N3ds& Handheld(System side) const
		{
			return ps5retro::IsRetro((int)side) ? m_settings.retro[ps5retro::Slot((int)side)] : side == System::Nds ? m_settings.nds : side == System::Switch ? m_settings.nx : m_settings.n3ds;
		}
		uint64_t& LastGame(System side) { return side == System::WiiU ? m_settings.lastGame : Handheld(side).lastGame; }
		std::vector<uint64_t>& RecentOf(System side) { return side == System::WiiU ? m_settings.recent : Handheld(side).recent; }
		void OpenSide(System side, bool first);
		void SwitchSide(System side);
		void LoadGames();
		void PollGames();
		void PollCore(); // Cemu's library, loading beside the launcher (ps5emu::StartCore): ready, or why not
		void MakeGame(Game& game, const ps5catalog::Entry& entry);
		// a Switch game's cover: a picture beside it, else GameTDB's box art or its own icon (Settings > Online)
		std::string SwitchCover(const ps5emu::Game& game) const;
		std::string IconOf(Game& game);
		// The backdrop's picture (7.4): a Wii U game's boot screen (looked for at most one a frame,
		// sharing the icons' budget: each mounts the game), else the box art
		std::string BackdropOf(Game& game);
		const ui::Picture& Cover(Game& game, bool blurred = false);
		int FindGame(uint64_t titleId) const;
		std::string Byline(const Game& game) const;
		std::string Played(const Game& game) const;
		std::string LastPlayedWords(int64_t when) const;
		void SaveSettings();
		// the language Settings names (or the PS5's) read again, and everything that keeps words said again
		void ApplyLanguage();
		// the PS5's own language first, then every language in its own words; index: the one in use
		std::vector<std::string> LanguageOptions(int& index) const;
		void OpenLanguagePicker(const std::string& kicker);
		void SetLanguage(int index); // LanguageOptions' index: the menus in that language, at once
		void FetchBoxArt();
		void RememberCount();
		// a game of the side's chosen: a 3DS or Wii U game in an archive unpacked first (StartUnpacking),
		// unless its copy is kept from before
		void Launch(int index, bool resume = false); // resume: Resume where you left off (Choice::resume)
		void LaunchChosen(int index, const std::string& launchPath); // launchPath: its unpacked copy, or empty
		bool m_resumeNext = false; // the launch on its way resumes (an archive unpacking first keeps it)
		void LaunchGame(const ps5emu::Game& game, const std::string& caption, const Box& from);
		// a game in an archive unpacked on a thread of its own (app/game_archives.h), its sheet over the
		// launcher, Circle stopping it; started once it is done
		void StartUnpacking(int index);
		void PollUnpacking();
		void StopUnpacking(); // and joins its thread
		void Toast(const std::string& text);

		// -- shared drawing (widgets.cpp) ----------------------------------------------------------
		ui::TextStyle Style(ui::TextStyle style) const; // larger text, when asked for
		uint32_t Secondary() const;	 // text at 70 %, or full in high contrast
		uint32_t Tertiary() const;
		uint32_t Surface() const;	 // glass, or opaque in high contrast
		uint32_t Surface2() const;
		// the theme's text colour, faint: white over a dark theme, ink over a light one (for a track, a
		// hairline, a chip's ground); what is drawn on a picture stays white instead
		uint32_t Wash(float alpha) const;
		void Focus(const Box& box, float radius, uint32_t glow = 0);
		void DrawFocusRing(Canvas& canvas);
		void Panel(Canvas& canvas, const Box& box, float radius);
		// maxWidth: the button's widest, its label smaller (then cut) in a language that needs more (0: as wide as the label)
		Box PillButton(Canvas& canvas, float x, float y, const std::string& label, bool primary, bool focused, Icon icon = Icon::Play, bool withIcon = false,
			float height = 72, float maxWidth = 0);
		Box IconButton(Canvas& canvas, float x, float y, Icon icon, bool focused, float size = 72);
		float Chip(Canvas& canvas, float x, float y, const std::string& text, const char* kind = "", float height = 38);
		float Badge(Canvas& canvas, float x, float y, System side);
		void DrawCover(Canvas& canvas, Game& game, const Box& box, bool focused, float lift);
		void DrawGameName(Canvas& canvas, Game& game, const Box& box);
		// labelScale: the list's one size for its labels (LabelScale); 0: the row fits its own label,
		// a little smaller when it is long
		void DrawRow(Canvas& canvas, const Row& row, const Box& box, bool focused, float labelScale = 0);
		// The room a row's label has beside its control, and how the control takes the rest (DrawRow)
		struct RowControl
		{
			float fixed = 0;	   // the control's part that is not its value's text
			float valueMost = 0;   // how wide that text may be
			float labelWidth = 0;  // what is left for the label
			float segmentShare = 0; // a segmented control's each segment's widest, when even smaller text does not fit
			ui::TextStyle segment;
		};
		RowControl RowControlOf(const Row& row, float boxWidth, bool focused);
		// One size for every label of a list's rows (#76): the size the label tightest for its room
		// needs, at most the step text takes before an ellipsis, so a longer language's rows are not
		// each a size of their own
		float LabelScale(const std::vector<Row>& rows, float boxWidth);
		void DrawToggle(Canvas& canvas, float x, float y, bool on, bool focused);
		void HoldRing(Canvas& canvas, const Box& box, float progress);
		float Enter(int part) const; // a screen's part, arriving: 0 to 1, 40 ms apart
		void DrawSheet(Canvas& canvas, const Box& box, float appear);
		void Scrim(Canvas& canvas, float alpha);

		// -- screens ---------------------------------------------------------------------------
		void Show(ScreenId screen);
		void ShowTab(ScreenId tab);
		void NextTab(int direction);
		bool BarUpdate(const ui::Press& press);
		void FocusBar();

		// Make it yours: first, a first start's look and feel, else what this
		// version brings that the last did not have
		void WelcomeOpen(bool first);
		void WelcomeUpdate(const ui::Press& press, const ui::Actions& actions);
		void WelcomeDraw(Canvas& canvas);
		std::vector<Row> WelcomeRows();
		void WelcomeDone();
		// What's new (whatsnew.cpp): once a version, before anything else; then what was next
		enum class NewsThen
		{
			Side,
			Welcome,	 // a first start's Make it yours, then the Setup check
			Setup,
			NewOptions, // an update's Make it yours: its new options
		};
		void WhatsNewOpen(NewsThen then);
		void WhatsNewUpdate(const ui::Press& press);
		void WhatsNewDraw(Canvas& canvas);
		void WhatsNewDone();
		// the theme Settings names (ui.theme), made the kit's; the themes the list offers
		void ApplyTheme();
		std::vector<ui::Theme> m_themes;
		void DrawEden(Canvas& canvas, float alpha);	   // the Eden theme's motif (ProsperoEden 1.001's light), under the scrims
		void DrawMotes(Canvas& canvas, float alpha);   // and its motes of light rising, over them
		bool m_splashHidden = false; // the PS5's splash let go of, once the launcher has drawn

		void SetupOpen(bool first);
		void SetupUpdate(const ui::Press& press);
		void SetupDraw(Canvas& canvas);
		bool SetupNeeded() const; // a check that fails at start: the Setup check comes first
		void FinishStart();		  // the first Setup check done: the side's Library
		void DrawBrandBackdrop(Canvas& canvas, float seam); // both sides' colours meeting at the seam

		void LibraryUpdate(const ui::Press& press);
		void LibraryDraw(Canvas& canvas);
		void LibraryRefresh(); // the filter, the sort and Recently played again
		void SurpriseMe();	   // Sort's last choice: the focus on a game of the list, by chance
		int LibraryGame() const;
		Box CoverBox(Game& game, float x, float bottom, float height, float maxWidth); // the whole box art at its own shape
		int LibraryFilters() const; // All, Recently added, Favourites, and Graphic packs on (Wii U), then Hidden
		int HiddenFilter() const;	// Hidden's place among them, -1 while the side hides no game
		int LibraryFilter() const;	// the one in use: All while Settings > Launcher has the filters off
		int LibraryTop() const;		// the first zone: Recently played, else the filters, else the list
		int ArticTiles() const { return Is3ds() ? 1 : 0; } // the 3DS side's shelf starts with Artic Base
		// what the Library shows in place of its shelf: a notice, Cemu starting, the scan, no games
		// (6.1, 6.2), its buttons in m_stateButtons; false when the shelf shows
		bool LibraryState(std::string& kicker, std::string& title, std::string& text, std::vector<std::string>& buttons) const;
		void LibraryStateUpdate(const ui::Press& press);
		void LibraryStateDraw(Canvas& canvas);

		void HubOpen(int game, ScreenId from);
		void HubUpdate(const ui::Press& press);
		void HubDraw(Canvas& canvas);

		void SettingsOpen(const std::string& page, bool onRows = false, int row = 0);
		void SettingsUpdate(const ui::Press& press, const ui::Actions& actions);
		void SettingsDraw(Canvas& canvas);
		Box SettingsDialog() const { return {200, 136, 1520, 820}; } // over the Library, on every side and theme (6.5)
		std::vector<SettingsPage> SettingsPages() const; // the side's (or the game's, in game settings)
		std::vector<Row> SettingRows(const std::string& page);
		std::vector<Row> SideRows(const std::string& page, const ps5settings::Launcher& settings);
		// a page's rows as shown: a › page's behind its row, or that › page's own while it is open
		std::vector<Row> SubPageRows(const std::vector<Row>& rows) const;
		void OpenSubPage(const Row& row);
		void CloseSubPage();
		std::string PanelText(const std::string& page) const;
		std::string PlayTimeText() const; // Settings > Play time
		void ChangeSetting(const Row& row, int step, bool cross);
		// a row's value changed: in the settings given (the side's, or a game's view of them)
		bool ApplyRow(ps5settings::Launcher& settings, const Row& row, int step, bool cross);
		void SetRowIndex(ps5settings::Launcher& settings, const std::string& id, int index);
		void ChooseIndex(const Row& row, int index); // a dropdown's choice
		void DrawRemoteScreen(Canvas& canvas, const Box& card); // the remote touch screen's card
		void DrawSupport(Canvas& canvas, const Box& card);		 // About's: the developer's PayPal.me, as a QR code
		void SettingsChanged(const std::string& id); // the side's settings changed: saved, and what they change at once
		void KeepGameSetting(const std::string& setting, const ps5settings::Launcher& view);
		// game settings (6.5): the same pages, scoped to one game
		void GameSettingsOpen(int game);
		ps5eden::extras::Context SwitchExtrasContext() const;
		std::vector<Row> SwitchExtrasRows(const std::string& page);
		bool ChangeSwitchExtra(const Row& row, int step, bool cross);
		ps5eden::extras::View m_switchExtras;
		std::string m_switchExtrasKey;
		struct SwitchImport { std::string folder, label; uint64_t accountSaveId = 0; };
		std::vector<SwitchImport> m_switchImports;
		void GameSettingsClose();
		bool InGameSettings() const { return m_gameTitle != 0; }
		void ResetGameSetting(const Row& row);
		void HoldDone(const std::string& id);
		std::string PacksStatus() const;
		static std::string AppUpdateStatus();
		void PollPacks();
		// Wii U mods from the USB drives (app/mods.h): titleId and name, the game they were added from
		void AddMods(uint64_t titleId, const std::string& name);
		std::string ModsStatus() const;
		void PollMods();
		// a game's home screen tile (app/forwarders.h)
		void AddTile(Game& game);
		void RemoveTile(Game& game);
		void PollTiles();
		// a home screen tile's game, started once its side has it (Status::autoLaunch)
		void PollAutoLaunch();

		void PacksOpen(int game, ScreenId from);
		void PacksUpdate(const ui::Press& press);
		void PacksDraw(Canvas& canvas);
		void RefreshPacks();
		void SettlePackItem(int direction);
		const ps5emu::GraphicPackInfo* SelectedPack() const;
		void PlayerOpen(int player);
		void PlayerUpdate(const ui::Press& press, const ui::Actions& actions);
		void PlayerDraw(Canvas& canvas);
		void MappingOpen();
		void MappingUpdate(const ui::Press& press);
		void MappingDraw(Canvas& canvas);
		void PollCapture();
		std::vector<ps5emu::ButtonMapping> Mappings() const;
		void SetHandheldMapping(int index, ps5emu::PadInput input);
		void FilesOpen(int mode);
		bool BrowseTo(const std::string& folder);
		// the folder browser's tabs (6.6): the PS5's own storage ("/data"), then each drive plugged in
		std::vector<std::string> BrowseTabs() const;
		int BrowseTab() const; // the one the folder shown is on
		std::map<std::string, std::string> m_tabFolders; // each tab's folder, kept as L1 and R1 go between them
		void FilesUpdate(const ui::Press& press);
		void FilesDraw(Canvas& canvas);
		void PollInstall();
		bool IsFileEntry(int index) const;
		const ps5emu::InstallCandidate& Inspect(const std::string& folder);
		// Updates and DLC (installs.cpp, 6.12): what the drives have to install, by game; game: a base
		// title ID for that game's alone, 0 for every game's
		void UpdatesOpen(uint64_t game, ScreenId from);
		void UpdatesUpdate(const ui::Press& press);
		void UpdatesDraw(Canvas& canvas);
		void PollUpdates();
		void UpdatesSort();
		void UpdatesInstallNext();
		std::vector<Hint> UpdatesHints() const;
		// Network shares (shares.cpp, 6.14): the list, and the sheet that adds or changes one
		void SharesOpen(ScreenId from);
		void SharesEdit(int index); // -1: a new one
		void SharesScan();			// the computers on the network that share files
		void SharesFind();			// the computer's shared folders
		void SharesPoll();
		void SharesSave();
		void SharesRemove();
		std::vector<Row> ShareRows();
		void SharesUpdate(const ui::Press& press, const ui::Actions& actions);
		void SharesDraw(Canvas& canvas);
		std::vector<Hint> SharesHints() const;
		struct Shares
		{
			std::vector<ps5netfs::Share> list;
			std::vector<std::string> status; // each share's last test, in words
			int selected = 0;				 // in list, and Add a share after it
			ScreenId from = ScreenId::Settings;
			bool editing = false;			 // the sheet is open
			int index = -1;					 // the share it changes; -1 a new one
			ps5netfs::Share share;			 // as the sheet has it
			int row = 0;
			std::vector<ps5netfs::Found> found; // the computers that share files
			int foundIndex = 0;
			std::vector<std::string> names; // the computer's shared folders
			int nameIndex = 0;
			std::shared_ptr<ShareJob> scan, find, test;
			bool saving = false; // Save: the test first, then kept
			std::string message;
			bool messageGood = false;
		} m_shares;
		void ArticOpen();
		void ArticUpdate(const ui::Press& press, const ui::Actions& actions);
		void ArticDraw(Canvas& canvas);
		std::string ArticAddress() const;
		void DrawPageHeader(Canvas& canvas, const std::string& kicker, const std::string& title, const std::string& copy);
		void DrawList(Canvas& canvas, const Box& box, int count, int selected, float& scroll, float rowHeight,
			const std::function<void(int, const Box&, bool)>& row);

		// -- overlays ---------------------------------------------------------------------------
		void OpenPicker(const std::string& kicker, const std::string& title, std::vector<std::string> options, int active,
			std::function<void(int)> choose);
		void OpenHelp(const std::string& title, const std::string& text);
		void OpenGameMenu(int game);
		void OpenKeyboard();
		void OpenNameKeyboard(const std::string& title, const std::string& text, size_t limit, std::function<void(const std::string&)> done);
		// text as it is typed: no capitals put in, with capitals and symbols on R1 and L1; secret shows dots
		void OpenTextKeyboard(const std::string& title, const std::string& text, size_t limit, bool secret,
			std::function<void(const std::string&)> done);
		bool OverlayUpdate(const ui::Press& press);

		// -- state ----------------------------------------------------------------------------
		ps5settings::Launcher& m_settings;
		ps5launcher::Status& m_status;
		std::function<void(System)> m_prepare;
		std::optional<System> m_prepared;
		System m_side = System::WiiU;

		ui::Gfx m_gfx;
		ui::Fonts m_fonts;
		uint32_t m_atlasHeight = 0; // as Gfx has it
		std::unique_ptr<ui::Images> m_images;
		ui::Input m_input;
		ui::Feedback m_feedback;
		ui::DrawList m_list;
		double m_start = 0, m_now = 0, m_lastFrame = 0;
		double m_lastInput = -1; // the last press or held button, for the screensaver's dimming (-1: none yet)
		float m_dt = 0;
		uint64_t m_frames = 0;
		bool m_failed = false;	 // the device was lost
		bool m_videoOut = false; // VideoOut was taken (a failure from here needs a fresh process)

		std::vector<Game> m_games;
		bool m_scanning = false;
		bool m_listed = false; // the side's scan has finished: m_games is all it found
		uint32_t m_boxArrivals = 0;
		uint32_t m_switchCoverArrivals = 0; // ps5eden::CoverArrivals, last looked at
		bool m_iconLookedThisFrame = false;

		ScreenId m_screen = ScreenId::Library;
		ScreenId m_tab = ScreenId::Library;
		bool m_onBar = false;
		int m_barFocus = kSides; // the sides (0 to kSides - 1), then the Library and Settings tabs
		float m_barSlide = -1;	 // the side switch as a carousel: how far its sides have slid (-1: not one)
		double m_screenAt = -10;  // when the screen was entered (its parts arrive)
		bool m_done = false;	  // a game is chosen and the launch shown
		std::optional<ps5launcher::Choice> m_choice;

		// the focus ring, gliding (x, y, w, h, radius) and what it was asked to be this frame
		ui::Spring m_ring[5];
		bool m_ringShown = false, m_ringAsked = false;
		Box m_ringBox;
		float m_ringRadius = 0;
		uint32_t m_ringGlow = 0;
		float m_ringAlpha = 0;

		// the backdrop: the focused game's colours and picture, changed once the focus rests
		uint32_t m_ambient[2] = {};
		uint32_t m_ambientFrom[2] = {}, m_ambientTo[2] = {};
		ui::Tween m_ambientMix;
		std::string m_backdrop, m_backdropOld;
		ui::Tween m_backdropMix;
		std::string m_backdropWanted;
		uint32_t m_wantedAmbient[2] = {};
		double m_wantedAt = 0;
		struct Bubble
		{
			float x, y, radius, speed, drift, alpha;
		};
		std::vector<Bubble> m_bubbles;
		float m_wavePhase[3] = {}; // the 3DS's three waves
		ui::Tween m_sideMix; // switching sides: the backdrop cross-fades

		// what the backdrop is asked to show (the focused game's picture and colours)
		void WantBackdrop(Game* game);

		// a screen drawn behind another (the Library under Settings' dialog): it asks for no focus and
		// does not arrive again
		bool m_behind = false;
		// the canvas being drawn (Draw), so the focus ring goes where the focused box is drawn, offsets
		// (a page scrolled, a screen sliding in) included
		Canvas* m_drawing = nullptr;

		// the Library's state card (no games, a notice): its buttons, and the one in focus
		std::vector<Box> m_stateButtons;
		int m_stateButton = 0;

		// Library
		std::vector<int> m_shelf; // indices in m_games, filtered and sorted
		int m_libraryIndex = 0;	  // in m_shelf (and the Artic tile first on the 3DS side)
		int m_libraryZone = 0;	  // 0 Recently played, 1 the filters, 2 the list
		std::vector<int> m_recent; // Recently played: indices in m_games, the last played first
		int m_recentIndex = 0;
		ui::Spring m_libraryScroll;
		std::string m_search;
		uint64_t m_libraryFocusTitle = 0;

		// each side keeps its focus across a switch (5.3): its Library game, by title
		uint64_t m_sideFocus[kSides] = {};

		// the hub
		int m_hubGame = -1;
		ScreenId m_hubFrom = ScreenId::Library;
		int m_hubAction = 0;
		bool m_hubAbout = false;
		ui::Spring m_hubScroll;
		float m_hubAboutHeight = 0;

		// Settings
		std::string m_page = "graphics";
		bool m_onRows = false;
		int m_settingRow = 0;
		// a › page open over the page's rows (empty: none): its row on the page, where Circle goes back
		// to, and that row's label and line, its title
		std::string m_subPage;
		int m_subFrom = 0;
		std::string m_subTitle, m_subLine;
		ui::Spring m_railScroll;
		std::string m_diagnosticsDone[3]; // Diagnostics' rows: the logs, Cemu's caches, Azahar's
		std::string m_unpackedDone;	 // Diagnostics: what clearing the unpacked games did
		uint64_t m_unpackedBytes = 0; // their size, looked at now and then
		double m_unpackedAt = -100;
		bool m_packsReloaded = false;
		uint32_t m_modsFinished = 0;  // the mods' runs answered (ps5mods::Status::finished)
		double m_modsToastAt = 0;	  // when a copy's progress was last said
		uint32_t m_tilesFinished = 0; // the tiles' requests answered (ps5forwarders::Status::finished)
		bool m_holdFired = false;
		std::string m_message; // a row's outcome
		std::map<std::string, int> m_holdChoice; // a held row with choices (Left and Right): the one chosen
		std::vector<ps5saves::Backup> m_backups;  // Accounts and saves: those on the drives, looked for now and then
		double m_backupsAt = -100;
		std::string m_savesMessage;			   // what the last backup or restore came to
		int m_savesFinished = 0;			   // the saves' runs seen ending (ps5saves::Status::finished)
		void UseOwnAccount();				   // One account for each PS5 user: the user who started the app's
		float m_holdProgress = 0; // a Hold row's, 0 to 1
		bool m_rescan[kSides] = {}; // a side's folder changed while another side was open
		// game settings: the game's title ID (0: the side's settings), its name, what the side's
		// settings look like with its own over them, and where Circle goes back to
		uint64_t m_gameTitle = 0;
		std::string m_gameName;
		ps5settings::Launcher m_gameView;
		ScreenId m_gameFrom = ScreenId::Library;
		std::string m_sidePage = "graphics"; // the side's page, kept while game settings are open
		// a game's saves (6.15), its game settings' Saves page: the game, its saves and the backups that
		// hold some of them, looked for now and then
		ps5saves::GameRef m_gameRef;
		std::vector<ps5saves::GameSave> m_gameSaves;
		std::vector<ps5saves::Backup> m_gameBackups;
		double m_gameSavesAt = -100;
		std::vector<Row> GameSaveRows();
		std::string SaveLabel(const ps5saves::GameSave& save) const;

		// pages
		ScreenId m_pageFrom = ScreenId::Settings; // where Files and Artic Base go back to
		ScreenId m_packsFrom = ScreenId::Library;
		System m_mappingSide = System::N3ds; // a handheld side's buttons
		void ClosePage(ScreenId to);
		void ApplyCemuOptions();
		int m_packsGame = 0;
		std::vector<ps5emu::GraphicPackInfo> m_packs;
		struct PackItem
		{
			int pack = -1;
			std::string heading;
		};
		std::vector<PackItem> m_packItems;
		int m_packItem = 0;
		bool m_presetsFocus = false;
		int m_presetSelected = 0;
		ui::Spring m_listScroll, m_listScroll2;
		int m_player = 0, m_playerRow = 0;
		int m_mapSelected = 0;
		struct Capture
		{
			bool active = false, released = false;
			double until = 0;
		} m_capture;
		std::string m_mapMessage;
		int m_filesMode = 0; // 0 a games folder, 1 Cemu's install, 2 a CIA
		System m_filesSide = System::WiiU;
		std::string m_browseFolder;
		std::vector<std::string> m_browseEntries;
		int m_browseFiles = 0, m_browseSelected = 0;
		std::string m_filesMessage, m_installFolder, m_installProgress;
		float m_installFraction = -1;
		std::map<std::string, ps5emu::InstallCandidate> m_inspected;
		// the CIA file chosen to install, as the 3DS side's library reads it: once, not at each frame (a
		// compressed one is decompressed for it)
		struct ChosenCia
		{
			std::string path, name;
			uint64_t titleId = 0;
			uint16_t version = 0;
		} m_chosenCia;
		bool m_installing = false;
		struct Updates
		{
			uint64_t only = 0; // a game's base title ID: its own alone
			ScreenId from = ScreenId::Settings;
			std::vector<ps5emu::Installable> found;
			std::vector<int> rows; // found's, by game
			int selected = 0;
			float scroll = 0;
			bool looking = false;
			std::string where;			   // the folder being looked in
			std::vector<std::string> queue; // folders to install, the first installing
			int queued = 0, finished = 0;  // the run: how many asked for, how many done
			uint64_t runBytes = 0, runDone = 0; // the run's bytes, and those of the installs done
			bool installing = false, stopping = false;
			uint64_t lastCopied = 0;
			double lastAt = 0;
			float speed = 0; // bytes a second, over the last few seconds
			std::string message;
		} m_updates;
		int m_articRow = 0, m_articOctet = 3;
		bool m_articEditing = false;
		std::array<int, 4> m_articOctets{192, 168, 1, 2};

		// Make it yours
		bool m_welcomeFirst = false; // a first start's (else what is new)
		int m_welcomeRow = 0;
		bool m_welcomeThenSetup = false; // the Setup check follows (a first start)
		bool m_welcomeFromSettings = false; // opened from Settings > Launcher: back there when done
		float m_welcomeScroll = 0;
		NewsThen m_newsThen = NewsThen::Side; // what follows What's new

		// Setup check
		bool m_setupFirst = false; // as the app starts, before a side opens
		bool m_setupOnLanguage = false; // the focus on the language, above the checks
		int m_setupRow = 0;
		ScreenId m_setupFrom = ScreenId::Settings; // where Circle goes back to, when not at the start
		struct Check
		{
			std::string id, title, detail, action; // action: what Cross does (empty: nothing)
			int state; // 0 ready, 1 needs a look, 2 optional
			std::string about, aboutTitle;		  // the panel beside the list: what it means, what to do
			std::vector<std::string> chips;		  // what goes there (a games folder's formats)
			std::string guide, guideTitle;		  // the guide's page for it, as the QR code, and what it is
		};
		std::vector<Check> m_checks;
		void RunChecks();
		std::unordered_map<std::string, ui::QrCode> m_qrCodes; // made once each

		// overlays
		struct Picker
		{
			bool open = false;
			std::string kicker, title;
			std::vector<std::string> options;
			int active = -1, selected = 0;
			std::function<void(int)> choose;
			ui::Spring scroll;
			double at = 0;
		} m_picker;
		struct HelpSheet
		{
			bool open = false;
			std::string title, text;
			double at = 0;
		} m_help;
		struct GameMenu
		{
			bool open = false;
			int game = -1;
			int selected = 0;
			std::vector<std::pair<std::string, std::string>> items; // id, label
			double at = 0;
		} m_menu;
		struct Keyboard
		{
			bool open = false;
			int row = 1, column = 0;
			double at = 0;
			// naming something (an account) instead of the Library's search: its text, what the sheet
			// calls it, how long it may be, and what Done does with it (Circle leaves it as it was)
			bool naming = false;
			std::string text, title;
			size_t limit = 0;
			std::function<void(const std::string&)> done;
			// text typed as it is (a user name, a password, a computer's name): its spaces at the ends
			// kept; secret: shown as dots
			bool raw = false, secret = false;
			// the keys shown: capitals (Shift, its key or Triangle), and which of the language's pages (L1,
			// R1: app/keyboard.h), as on the in-game keyboards
			bool shift = false;
			int page = 0;
		} m_keyboard;
		struct UpdateSheet
		{
			bool open = false;
			int choice = 0;
			double at = 0;
			double restartAt = -1; // Restart now was chosen then: the app starts again a few frames after
		} m_update;
		// a folder for each system (app/reorganize.h): what can be done, looked at as the launcher starts
		// (Settings > Game files' row), and the sheet that offers it to an install from before, once a start
		ps5reorganize::Layout m_layout;
		struct ReorganizeSheet
		{
			bool offer = false; // to open once a side's library is up and nothing else asks
			bool open = false;
			int choice = 0;
			double at = 0;
			double restartAt = -1; // Reorganize chosen: the app starts again a few frames after
			std::string message;   // why it did not: the request not written, or the PS5 not starting the app again
		} m_reorganize;
		// the four systems' own folders, for the texts that name them
		static std::string SystemFolders();
		static int Today(); // days since 1970
		struct Unpacking
		{
			bool open = false;
			double at = 0;
			uint64_t titleId = 0; // the game's (the list may be read again meanwhile)
			std::string name;
			std::thread thread;
			ps5archive::Progress progress;
			std::atomic<bool> finished{false};
			bool ok = false; // the thread's, read once finished
			std::string path, error;
		} m_unpack;
		struct ToastItem
		{
			std::string text;
			double at;
		};
		std::vector<ToastItem> m_toasts;
		struct LaunchState
		{
			bool active = false;
			double at = 0;
			Box from;
			std::string caption, title;
			float aspect = 0.714f;
			int game = -1; // in m_games, drawn as its cover is; -1: a launch of its own (the Home Menu, Artic Base)
		} m_launch;

		// facts and pictures looked up once
		std::unordered_map<std::string, std::string> m_folderCounts;
	};
}
