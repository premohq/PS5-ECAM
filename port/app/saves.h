// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: every side's saves backed up to a USB drive, and restored from one.
// A backup is a folder, PS5CEMU-HAR-saves/<date and time>/ on the drive, with:
//
//   wiiu/save/          mlc01/usr/save: every game's saves, every account's, and the accounts
//   3ds/sdmc/           the 3DS's games' saves (title/.../data) and extra data (extdata), from
//                       azahar/sdmc/Nintendo 3DS; never the games installed there
//   3ds/nand/           azahar/nand/data: the system's saves
//   3ds/states/         Azahar's save states
//   ds/saves/, ds/states/  melonDS's saves and save states
//   switch/save/        Eden's games' saves (eden/user/nand/user/save, in /data/ps5cemu whatever the
//                       data folder), and switch/profiles/ its user profiles (system save 8000000000000010)
//   retro/<backend>/   Porpoise GC/Wii saves, and native N64/NES/GB/GBC/GBA saves and states
//   settings/           ps5cemu.json (the launcher's, each game's own), Cemu's settings.xml, its
//                       controllerProfiles/ and themes/: a whole backup's, never a game's
//   backup.json         when, by which version, how many files and bytes
//
// Restoring puts each of a backup's files back where it came from; a file there now is first moved
// to its owning data folder's saves-before/<date and time>/, in the same shape, so restoring
// loses nothing. Saves
// the backup does not have stay as they are. Both run on a thread of their own, one at a time.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5saves
{
	struct Backup
	{
		std::string folder; // /mnt/usb0/PS5CEMU-HAR-saves/2026-10-12 214000
		std::string drive;	// /mnt/usb0
		std::string when;	// as its folder names it: 2026-10-12 21:40
		uint64_t bytes = 0;
		int files = 0;
	};

	// The backups on the drives plugged in, newest first
	std::vector<Backup> FindBackups();

	struct Status
	{
		enum class State
		{
			Idle,
			Running,
			Done,
			Failed,
		} state = State::Idle;
		bool restoring = false;
		uint64_t copied = 0, total = 0; // bytes
		int files = 0;
		std::string where;	 // the backup's folder
		std::string message; // why it failed
		int finished = 0;	 // how many runs have ended, so a caller sees each end once
		// a restore put the settings back: the launcher holds the old ones, so the app starts again
		bool settingsRestored = false;
	};

	// To the first USB drive that takes a folder; false, with why, when none does or one is running
	bool StartBackup(std::string& error);
	bool StartRestore(const Backup& backup, std::string& error);
	Status Current();
	void Finish(); // waits for the thread (before a game)

	// -- one game's saves ------------------------------------------------
	// The game whose saves these are: its side (0 Wii U, 1 3DS, 2 DS, 3 Switch), its
	// title ID (the Wii U's and the 3DS's), and its file (the DS's saves and states are named by it)
	struct GameRef
	{
		int side = 0;
		uint64_t titleId = 0;
		std::string file;
	};
	// One of its saves: a Wii U account's, or the save every account shares; the 3DS's save data, extra
	// data or save states; the DS's save or save states
	struct GameSave
	{
		enum class Kind
		{
			Account,  // a Wii U account's (account: its persistent ID)
			Common,	  // the Wii U save every account of the game shares
			Data,	  // the 3DS's save data, or the DS's save
			ExtData,  // the 3DS's extra data
			States,	  // save states
			Slot2,    // optional DS Slot-2 cartridge's working save
		} kind = Kind::Data;
		uint32_t account = 0;
		std::vector<std::string> paths; // the files or folders it is
		uint64_t bytes = 0;
		int files = 0;
		int64_t changed = 0; // its newest file's time, seconds since 1970
	};
	std::vector<GameSave> FindGameSaves(const GameRef& game);
	// Moved, never deleted: to the owning system's saves-before/<date and time>/, in the same shape, as a
	// restore moves what it replaces. Where to, or empty with why in error.
	std::string MoveAside(const GameSave& save, std::string& error);
	// This game's saves alone, as a whole backup is, so Restore from USB knows it too
	bool StartGameBackup(const GameRef& game, std::string& error);
	// The backups on the drives that hold some of this game's saves, newest first
	std::vector<Backup> FindGameBackups(const GameRef& game);
	// This game's saves alone from a backup, what is there now moved aside first
	bool StartGameRestore(const Backup& backup, const GameRef& game, std::string& error);
	// Plain-folder/Ryujinx transfer for one Switch title and the explicitly selected existing
	// player. Runs on the same worker as backups; Finish() waits before a game can start.
	bool StartSwitchImport(const GameRef& game, const std::string& profile, const std::string& sourceRoot, std::string& error, uint64_t accountSaveId = 0);
	bool StartSwitchExport(const GameRef& game, const std::string& profile, std::string& error);

	// Resume where you left off: the state a 3DS or DS game saves as it is quit to the library, in a
	// slot the menus do not show (kResumeSlot: Azahar's azahar/states/<title ID>.11.cst, melonDS's
	// melonds/states/<game file>.ml11), loaded as the game starts when the launcher asks for it
	constexpr int kResumeSlot = 11;
	// Its file for a game (side 1 the 3DS, 2 the DS, as GameRef has them), or empty for another side
	std::string ResumeState(const GameRef& game);
}
