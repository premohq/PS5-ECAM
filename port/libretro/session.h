// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a libretro core's game, as the app's frontend runs it (one at a time, as the DS side
// runs melonDS): the core's calls (its environment, its picture, its sound, the RetroPads), its
// options, the game's save RAM and its save states. Nothing here is the PS5's: the game's loop on the
// console (the screens, AudioOut, the DualSense, the in-game menu) calls it, and so does the host test
// (tools/tests/test-libretro.sh), with a core of its own.
//
// A game's files, in the folders Setup names:
//   <saves>/<game file>.srm      its save RAM, as the core keeps it (the RetroArch name, so a save
//                                from RetroArch's own saves folder is taken as it is); .rtc its clock
//   <states>/<game file>.state1-5  save states, the core's own serialized data; .state<resume slot>
//                                the one quitting saves (ps5saves::kResumeSlot)
#pragma once

#include "core.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ps5libretro
{
	struct Setup
	{
		std::string system; // the core's own files (a BIOS: disksys.rom, gba_bios.bin): GET_SYSTEM_DIRECTORY
		std::string saves;	// GET_SAVE_DIRECTORY, and the game's .srm
		std::string states;
		std::map<std::string, std::string> options; // the side's values for the core's options, over its defaults
		unsigned language = RETRO_LANGUAGE_ENGLISH;	 // the launcher's, as libretro numbers them
		std::string user = "Player";				 // GET_USERNAME
	};

	// Each player's RetroPad, as the game's loop reads the DualSense
	constexpr int kPlayers = 4;
	struct Pad
	{
		uint16_t buttons = 0;	  // 1 << RETRO_DEVICE_ID_JOYPAD_B ... _R3
		int16_t left[2] = {};	  // the sticks, x and y, -32768 to 32767 (y down)
		int16_t right[2] = {};
	};

	// A core option, as the core gave it (SET_VARIABLES, SET_CORE_OPTIONS, SET_CORE_OPTIONS_V2; in the
	// launcher's language where the core has it), and its value now
	struct Option
	{
		std::string key, label, info;
		std::vector<std::pair<std::string, std::string>> values; // the value, and its label
		std::string defaultValue, value;
		bool visible = true;
	};

	// The core's picture: width x height, 32 bits a pixel as the screens' (blue, green, red, then an
	// unused byte), whatever the core's own format
	struct Picture
	{
		std::vector<uint32_t> pixels;
		unsigned width = 0, height = 0;
		uint64_t frame = 0; // the Run that made it: a frame the core duplicated keeps the one before
	};

	// The core started and the game loaded into it, its save RAM from <saves>/<stem>.srm when there.
	// False, with why, when the core refuses it or one is loaded already.
	bool Load(const Core& core, const std::string& game, const Setup& setup, std::string& error);
	bool Loaded();
	// One frame of the game, each player holding what pads says
	void Run(const Pad (&pads)[kPlayers]);
	const Picture& LastPicture();
	// The sound the last Run made: left and right, one after the other, at SampleRate()
	const std::vector<int16_t>& Sound();
	double FrameRate();
	double SampleRate();
	// The picture's shape, width over height (the core's, else its pixels')
	float AspectRatio();
	// The core's name and version, as it gives them (the boot log, the in-game menu's badge)
	std::string CoreName();

	const std::vector<Option>& Options();
	// A value from those the option has: the core reads it before its next frame. False when the core
	// has no such option or value.
	bool SetOption(const std::string& key, const std::string& value);

	// The save RAM to its file: once it has changed and then held still for a second of frames, or now
	// (force: as the game is quit). True when there is nothing to write or it was written.
	bool FlushSave(bool force, std::string& error);

	// Save states: slot 1 to ps5ingame3ds::kStateSlots, or the resume slot
	std::string StatePath(int slot);
	bool SaveState(int slot, std::string& error);
	// The game as it was just before is kept, for UndoLoad
	bool LoadState(int slot, std::string& error);
	bool UndoAvailable();
	bool UndoLoad(std::string& error);
	void Reset();

	// What the core asked of the frontend, taken once: to stop (RETRO_ENVIRONMENT_SHUTDOWN), a message
	// for the player (SET_MESSAGE)
	bool TakeShutdown();
	bool TakeMessage(std::string& message);
	// The rumble the core asks of a player's controller, its strong and weak motors, 0 to 65535
	void Rumble(int player, uint16_t& strong, uint16_t& weak);

	// The save written, the game unloaded, the core stopped
	void Unload();

	// Where the core's log lines go (the boot log); stderr when unset
	void SetLog(void (*log)(int level, const std::string& line));
}
