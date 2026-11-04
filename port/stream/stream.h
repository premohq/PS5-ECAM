// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the remote touch screen (#38, from dolphin78's idea
// and prototype). A small HTTP server on the local network shows the game's second screen (the Wii
// U GamePad's, the 3DS's bottom screen, the DS's touch screen) in any browser, and takes its
// touches back: the page is browser.h, a WebSocket each for touches (/input) and pictures (/video).
//
// It costs a game nothing while it is off: WantFrame and Touch read one atomic flag and return. On,
// its threads sleep until a browser connects; only while one shows the screen are pictures asked
// for, at most 30 a second, and made into JPEGs on the server's own thread (4.2, rule 13). The
// server is the app's, not the launcher's: turned on, it runs from the app's start through the game.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps5stream
{
	// On or off, from the settings at start and from the menus after: the server starts or stops.
	// The same again does nothing.
	void Configure(bool enabled);
	bool Enabled();

	// What the menus show
	struct State
	{
		enum class Kind
		{
			Off,
			Waiting,   // running, no browser showing it
			Showing,   // a browser shows it
			NoNetwork, // running, but the PS5 has no address on a network yet
			NoPort,	   // no port from 8080 up could be opened
		};
		Kind kind = Kind::Off;
		std::string address; // http://192.168.1.25:8080/ while Waiting or Showing
	};
	State Current();
	// The state in a few words, as the menus say it
	const char* Describe(State::Kind kind);

	// The emulators' side. WantFrame: whether to send a picture now (a browser is showing it, and
	// 33 ms have passed since the last). SubmitFrame: the picture, as RGB rows, copied at once.
	bool WantFrame();
	void SubmitFrame(const uint8_t* rgb, int width, int height);
	// The browser's touch, from 0 to 1 across the picture; false when it touches nothing. A quick tap
	// is not lost between two reads; a browser that stops sending lets go after a second.
	bool Touch(float& x, float& y);

	// Files sent from a browser: the same server's /files page (files.h) puts a game, keys or music
	// straight into the app's folders, from a phone or a computer on the network. Only into the
	// folders listed here (asked for at each request, so a folder chosen in Settings counts at once),
	// and never over a file that is there; written as a hidden .part file, renamed once whole.
	struct Folder
	{
		std::string id;	   // in the upload's address: "3ds"
		std::string label; // on the page: "3DS games"
		std::string hint;  // under it: what goes there
		std::string path;  // where it goes
	};
	void SetUploadFolders(std::function<std::vector<Folder>()> list);
}
