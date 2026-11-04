// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: RetroAchievements in DS games, with rcheevos' rc_client
// (.deps/rcheevos): RetroAchievements has sets for the DS, none for the Wii U or the 3DS. Softcore
// only: save states, cheats and the speed settings stay as they are.
//
// The account: Settings > Online on the DS side signs in with a user name and password, once; only
// the token RetroAchievements answers with is kept, in /data/ps5cemu/retroachievements.json, with the
// user name and the points as of the last sign-in. Signing out forgets them.
//
// A DS game (port/melonds/core.cpp): StartGame as it starts, signed in, identifies its ROM and loads
// its set on a thread of the module's own; the emulation calls DoFrame after each DS frame, which
// checks the set against the DS's memory (the read function's) and does nothing until a set is
// loaded; unlocks become the PS5's notifications. Nothing of this runs until someone signs in.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5cheevos
{
	struct Account
	{
		bool signedIn = false;
		bool busy = false;	  // signing in
		std::string user;	  // as RetroAchievements writes it
		uint32_t points = 0;  // softcore points, as of the last sign-in
		bool notices = true;  // unlocks as the PS5's notifications
		std::string message;  // why the last sign-in did not work, or empty
	};

	// Reads the saved account (as the app starts); cheap when there is none.
	void Load();
	Account Get();
	// Starts signing in on a thread; false when one is already running.
	bool BeginSignIn(const std::string& user, const std::string& password);
	void SignOut();
	void SetNotices(bool on);

	// -- a DS game ------------------------------------------------------------------------------
	// Reads size bytes of the DS's memory at an rcheevos address (its DS map: 0 to 0x3FFFFF the main
	// RAM, 0x1000000 to 0x1003FFF the ARM9's data TCM); how many it read.
	using ReadMemory = uint32_t (*)(uint32_t address, uint8_t* buffer, uint32_t size);
	// The game's RetroAchievements hash, from its ROM as its file has it: before melonDS takes the
	// ROM, which re-encrypts the secure area in its copy and so changes the ARM9 code the hash reads
	// (#107). Empty when no one is signed in, or the ROM cannot be hashed.
	std::string HashDs(const uint8_t* rom, size_t size);
	// As the game starts (from its loop), signed in: HashDs's hash finds its set. False when no one
	// is signed in or there is no hash, so the emulation need not call DoFrame.
	bool StartGame(ReadMemory read, const std::string& hash);
	// The emulation's thread, after each DS frame (and while paused, now and then: Idle).
	void DoFrame();
	void Idle();
	// A save state loaded, or the DS reset: the set's conditions start again.
	void Reset();
	// The in-game menu's list: the unlocked first, then the rest. summary: "8 of 42 · 120 of 400
	// points", or why there is none (no set, still loading, not signed in).
	struct Item
	{
		std::string title, description;
		uint32_t points = 0;
		bool unlocked = false;
		int64_t unlockedAt = 0; // seconds since 1970, 0 when locked
	};
	bool List(std::vector<Item>& items, std::string& summary);
	// Before the core goes: waits up to maxMs for unlocks still being sent, then ends the session.
	void EndGame(int maxMs = 2000);
}
