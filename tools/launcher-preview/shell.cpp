// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the new launcher on a PC (tools/preview-shell.sh), to see its screens without a console.
//
// The launcher's own code (port/frontend/shell, the UI kit in port/ui, the catalogue, the settings,
// GameTDB's facts and the compatibility list; port/azahar's library and controls) runs as it does on
// the console, and the kit draws with the PC's Vulkan (Mesa's lavapipe will do) into an image read
// back after each frame, which the script's shots save. In place of Cemu and the console it has
// samples (console.cpp): Wii U games, graphic packs, controllers, and the 3DS games the script
// makes (tools/launcher-preview/make-3ds-samples.py).
//
//   shell-preview OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER
//
// The script has one command a line ('#' starts a comment):
//   press BUTTON...  each button down for 3 frames, then up for 3, one after another
//                    (touchpad+options: the two together)
//   hold BUTTON N    down for N frames, then up for 3
//   wait N           N frames
//   shot NAME        the frame on screen as OUTPUT_FOLDER/NAME.png
//   record NAME      from here, every other frame (30 a second) as OUTPUT_FOLDER/NAME/00000.png on,
//                    for a video of the launcher (ffmpeg -framerate 30 -i NAME/%05d.png makes one)
//   stop             no more recording
// Buttons: up down left right cross circle square triangle l1 r1 l2 r2 l3 r3 options create
// touchpad, and the sticks: ls-up ls-down ls-left ls-right rs-up rs-down rs-left rs-right.
//
// The launcher starts as it would
// after earlier sessions, on the Wii U side's Library; with PREVIEW_FIRST set, as on a first start (the
// Setup check, then the first side with games); PREVIEW_REMOTE turns the remote touch screen on; PREVIEW_UPDATE has
// a newer release found; PREVIEW_NO_DATA, /data out of reach; PREVIEW_LAUNCH_ERROR, the last game not
// started. PREVIEW_LANGUAGE names the menus' language ("de"; "qps", tools/lang.py pseudo's, for text
// that does not fit), PREVIEW_SYSTEM_LANGUAGE the PS5's (its number: 4 is German, 0 Japanese).
// PREVIEW_WHATSNEW starts as after an update (Make it yours, with what the version brings); PREVIEW_THEME
// names a theme (ui/theme.h's id: link-jack, midnight...); PREVIEW_SIDES the sides shown (1 Wii U, 2 3DS, 4 DS);
// PREVIEW_FILTERS turns the Library's filters' row on.

#include "console.h"

#include "app/compatibility.h"
#include "app/gameinfo.h"
#include "app/lang.h"
#include "app/paths.h"
#include "app/updates.h"
#include "azahar/library.h"
#include "melonds/library.h"
#include "eden/library.h"
#include "frontend/shell.h"
#include "stream/stream.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name);

// in place of host_ps5.cpp's: the PC's Vulkan loader, an image read back into the shots' frame, the
// repository's font, and the preview's clock, which the script moves on 1/60 s a frame
ps5shell::Host ps5shell::DefaultHost()
{
	Host host;
	host.getInstanceProcAddr = vkGetInstanceProcAddr;
	host.target = [] {
		ui::Target target;
		target.width = preview::kWidth;
		target.height = preview::kHeight;
		target.readback = [](const uint8_t* bgra, uint32_t width, uint32_t height, size_t pitch) {
			const uint32_t w = std::min<uint32_t>(width, preview::kWidth), h = std::min<uint32_t>(height, preview::kHeight);
			for (uint32_t y = 0; y < h; y++)
				std::memcpy(&preview::frame[(size_t)y * preview::kWidth * 4], bgra + y * pitch, (size_t)w * 4);
		};
		return target;
	};
	host.assets = "port/ui"; // its fonts/lexend.sdf, as the app's assets/ui has it
	host.fontFolders = {preview::output + "/fonts"}; // preview-shell.sh's stand-ins for the console's fonts
	host.clock = [] { return preview::timeUs / 1e6; };
	host.afterFrame = [] {
		preview::timeUs += 16667;
		preview::AdvanceScript();
	};
	return host;
}

int main(int argc, char* argv[])
{
	if (argc != 6)
	{
		std::fprintf(stderr, "shell-preview OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER DS_GAMES_FOLDER\n");
		return 2;
	}
	preview::output = argv[1];
	// the app's folder: preview-shell.sh's, with the assets the launcher reads pictures from (the sides'
	// tiles, Eden's launch picture)
	ps5paths::detail::AppDirSlot() = preview::output + "/app";
	ps5gameinfo::SetFolder("port/app/gametdb"); // the repository's: the preview runs from its root
	ps5compat::SetPath("docs/COMPATIBILITY.md");
	if (std::getenv("PREVIEW_UPDATE"))
		ps5update::Start(); // as the app's check finding a newer release
	preview::LoadScript(argv[2]);

	ps5settings::Launcher settings;
	settings.gamesFolder = argv[3];
	settings.n3ds.gamesFolder = argv[4];
	settings.nds.gamesFolder = argv[5];
	if (!std::getenv("PREVIEW_FIRST"))
	{
		// earlier sessions: the side last used, the games last played, the counts their scans saved
		settings.ui.setupDone = true;
		// the version run last: this one, unless the preview is of what an update brings
		settings.ui.lastVersion = std::getenv("PREVIEW_WHATSNEW") ? "3.5.0" : PS5CEMU_VERSION;
		settings.ui.lastSide = "wiiu";
		settings.lastGame = 0x00050000101C9400;
		settings.recent = {0x00050000101C9400, 0x0005000010143500, 0x000500001010EC00, 0x0005000010101D00};
		settings.n3ds.lastGame = 0x0004000000053F00;
		settings.n3ds.recent = {0x0004000000053F00, 0x0004000000030600, 0x000400000017C100};
		settings.gameCount = 11;
		settings.n3ds.gameCount = 6;
		settings.nds.gameCount = 4;
		settings.nx.lastGame = 0x0100000000010000;
		settings.nx.recent = {0x0100000000010000, 0x0100152000022000};
		settings.nx.gameCount = 5;
	}
	if (std::getenv("PREVIEW_REMOTE"))
		settings.remoteScreen = true;
	if (std::getenv("PREVIEW_FILTERS"))
		settings.ui.libraryFilters = true;
	if (const char* theme = std::getenv("PREVIEW_THEME"))
		settings.ui.theme = theme;
	// the side it opens on: wiiu, 3ds, ds or switch
	if (const char* side = std::getenv("PREVIEW_SIDE"))
		settings.ui.lastSide = side;
	if (const char* sides = std::getenv("PREVIEW_SIDES"))
		settings.ui.sides = std::clamp(std::atoi(sides), 1, 2047);
	ps5stream::Configure(settings.remoteScreen); // as main does
	// the menus' language: PREVIEW_LANGUAGE's code (qps: tools/lang.py pseudo's), else the PS5's, which
	// PREVIEW_SYSTEM_LANGUAGE gives as the console's number (console.cpp)
	if (const char* language = std::getenv("PREVIEW_LANGUAGE"))
		settings.ui.language = language;
	ps5lang::SetFolder("port/lang");
	ps5lang::Load(settings.ui.language);
	ps5launcher::Status status;
	status.diagnostics = {"PS5-ECAM preview: Cemu at 32e6628, Azahar at 4aef900", "Jailbroken by the HEN: /data reachable, executable memory",
		"Boot log: /data/ps5cemu/logs/boot.log", "Cemu's log: " + ps5paths::CemuRoot() + "/log.txt"};
	if (std::getenv("PREVIEW_NO_DATA"))
		status.notice = status.notice3ds = status.noticeDs = status.noticeSwitch =
			"PS5-ECAM cannot reach /data. Load a HEN with PPSA99360 in its app jailbreak list, or elfldr, then restart PS5-ECAM.";
	else if (std::getenv("PREVIEW_LAUNCH_ERROR"))
		status.launchError = ps5lang::Tr("This is not a Wii U game PS5-ECAM can start.") + std::string(" ") +
			ps5lang::TrF("Its disc key is missing from {0}/keys.txt.", ps5paths::CemuRoot());

	// as main_ps5.cpp's: a side opens when the launcher opens it (the preview's Cemu starts at once)
	std::optional<ps5launcher::Choice> choice;
	const ps5shell::Outcome outcome = ps5shell::Run(settings, status, [&](ps5launcher::System system) {
		if (system == ps5launcher::System::N3ds)
			ps5azahar::StartScan(settings.n3ds.gamesFolder);
		else if (system == ps5launcher::System::Nds)
			ps5melonds::StartScan(settings.nds.gamesFolder);
		else if (system == ps5launcher::System::Switch)
			ps5eden::StartScan(settings.nx.gamesFolder);
		else
			status.coreReady = true;
	}, choice);
	std::fprintf(stderr, "[preview] the launcher ended: %s\n", choice ? choice->game.name.c_str() : "no game");
	return outcome == ps5shell::Outcome::Chosen ? 0 : 1;
}
