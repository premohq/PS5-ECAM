// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: What's new, once a version, before anything else: on the app's first start and after an
// update, what the version brings, a card each, as its release notes' highlights say it. Cross or
// Circle goes on to what was next: a first start's Make it yours and Setup check, the Setup check, an
// update's new options, or the side.

#include "shell_internal.h"
#include "../../app/updates.h"

#include <algorithm>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		struct News
		{
			Icon icon;
			const char* title;
			const char* text;
		};

		// the release notes' highlights: changed with each release that has something to say
		const News kNews[] = {
			// tr: What's new: each system's files in a folder of its own (/data/cemu, /data/azahar...), which Reorganize moves an install from before into
			{Icon::Folder, TrMark("A folder for each system"), TrMark("Wii U, 3DS, DS and Switch files each in a folder of their own, moved there in a moment by Reorganize.")},
			// tr: What's new: the app installed on a USB drive keeps every system's files on that drive
			{Icon::Usb, TrMark("Everything on your USB drive"), TrMark("With the app on a USB drive, every system's files can go beside it, the Switch's too.")},
			// tr: What's new: a Switch game's update and DLC files are found next to it, and its page shows them
			{Icon::Download, TrMark("Updates and DLC"), TrMark("A Switch game's update and DLC are found beside it, in any games folder, and its page shows them.")},
			// tr: What's new: the Switch side's Mods and Cheats pages, its players, and its saves to a USB drive and back
			{Icon::Gear, TrMark("Switch mods and cheats"), TrMark("Choose a game's mods and cheats, add players, and take saves to USB and back.")},
			// tr: What's new: Lossless Scaling's frame generation for Switch games; Lossless.dll and MihawkGen are names, left as they are
			{Icon::Sparkle, TrMark("Frame generation"), TrMark("Switch games use Lossless Scaling with your own Lossless.dll: steadier than MihawkGen.")},
			// tr: What's new: an update of PS5-ECAM itself installs fast
			{Icon::Pulse, TrMark("Updates in a minute"), TrMark("A new version installs in about a minute instead of half an hour, and can be stopped.")},
			// tr: What's new: leaving a 3DS or DS game shows the library without the app starting again
			{Icon::Screens, TrMark("Straight back to the library"), TrMark("Leaving a 3DS or DS game shows the library at once, without the app starting over.")},
			// tr: What's new: each game's play time is counted and shown
			{Icon::Play, TrMark("Play time"), TrMark("How long you've played each game, in the library and on its page.")},
			// tr: What's new: an installed update or DLC can be taken out again from a game's Options menu
			{Icon::Check, TrMark("Remove its update or DLC"), TrMark("In a Wii U or 3DS game's Options, for one that keeps it from starting.")},
		};

		constexpr int kColumns = 3;
		constexpr float kGridTop = 290, kGap = 24, kCardHeight = 196, kPad = 26, kIcon = 52;
	}

	void Shell::WhatsNewOpen(NewsThen then)
	{
		m_newsThen = then;
		m_onBar = false;
		m_message.clear();
		Show(ScreenId::WhatsNew);
		m_screenAt = m_now;
	}

	void Shell::WhatsNewDone()
	{
		m_feedback.Play(ui::Cue::Select);
		switch (m_newsThen)
		{
		case NewsThen::Welcome: WelcomeOpen(true); break; // then the Setup check, then the side
		case NewsThen::Setup: SetupOpen(true); break;	  // then the side (FinishStart)
		case NewsThen::NewOptions: WelcomeOpen(false); break;
		case NewsThen::Side: OpenSide(m_side, !m_prepared); break;
		}
	}

	void Shell::WhatsNewUpdate(const ui::Press& press)
	{
		if (press.button == Button::Cross || press.button == Button::Circle)
			WhatsNewDone();
	}

	void Shell::WhatsNewDraw(Canvas& canvas)
	{
		// what this is
		canvas.PushAlpha(Enter(0));
		canvas.Text(Style(kOverlineStyle), kSafeX, 92, TrF("Playtendoh {0}", ps5update::Readable(PS5CEMU_VERSION)), Secondary(), 1100, 1);
		// tr: the heading of the screen a new version opens on, once: what it brings
		canvas.Text(Style({56, ui::Weight::Bold, 1.1f, -0.8f}), kSafeX, 122, Tr("What's new"), kText, 1100, 1);
		canvas.Text(Style({24, ui::Weight::Regular, 1.3f}), kSafeX, 206,
			Tr("The biggest update yet, at a glance. Its release notes on GitHub have everything."), Secondary(), 1920 - 2 * kSafeX, 2);
		canvas.PopAlpha();

		// the cards, each in one of the four systems' colours in turn
		static constexpr System kColours[] = {System::Switch, System::WiiU, System::N3ds, System::Nds};
		const float width = (1920 - 2 * kSafeX - (kColumns - 1) * kGap) / kColumns;
		const float textWidth = width - kPad - kIcon - 22 - kPad;
		const int count = (int)std::size(kNews);
		for (int i = 0; i < count; i++)
		{
			const int row = i / kColumns, column = i % kColumns;
			const float enter = Enter(1 + row);
			canvas.PushAlpha(enter);
			canvas.PushOffset(0, (1 - enter) * 24);
			const Box card{kSafeX + column * (width + kGap), kGridTop + row * (kCardHeight + kGap), width, kCardHeight};
			canvas.Rect(card, kRadiusCard, Surface());
			// the title and its line, as many lines as the card holds, in the card's middle
			const ui::TextBlock title = m_fonts.Layout(Style({26, ui::Weight::SemiBold, 1.15f}), Tr(kNews[i].title), textWidth, 2);
			const ui::TextStyle textStyle = Style({20, ui::Weight::Regular, 1.4f});
			const int lines = std::clamp((int)((kCardHeight - 2 * kPad + 4 - title.height - 8) / (textStyle.size * textStyle.lineHeight)), 1, 4);
			const ui::TextBlock text = m_fonts.Layout(textStyle, Tr(kNews[i].text), textWidth, lines);
			const float top = card.y + std::max(kPad, (kCardHeight - (title.height + 8 + text.height)) * 0.5f);
			const uint32_t colour = AccentOf(kColours[i % std::size(kColours)]);
			const Box icon{card.x + kPad, top, kIcon, kIcon};
			canvas.Rect(icon, kIcon / 2, ui::SetAlpha(colour, 0.18f));
			canvas.Draw(kNews[i].icon, icon.Inset(13), colour);
			const float tx = icon.Right() + 22;
			canvas.Text(title, tx, top, kText);
			canvas.Text(text, tx, top + title.height + 8, Secondary());
			canvas.PopOffset();
			canvas.PopAlpha();
		}
	}
}
