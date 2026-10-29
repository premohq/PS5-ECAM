// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the DualSense, through libScePad.
//
// The sample layout is the one ProsperoEden checks with static_asserts and runs on the console
// (third_party/ps5_pad.hpp there, GPL-3.0-or-later): 120 bytes, touch data at 0x34.
//
// One pad service serves the launcher and Cemu's DualSense controller provider: player 1 is the
// signed-in user who started PS5Cemu, players 2-4 the other signed-in users, rescanned now and
// then so controllers can join and leave.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ps5pad
{
	enum Button : uint32_t
	{
		kCreate = 0x00000001,
		kL3 = 0x00000002,
		kR3 = 0x00000004,
		kOptions = 0x00000008,
		kUp = 0x00000010,
		kRight = 0x00000020,
		kDown = 0x00000040,
		kLeft = 0x00000080,
		kL2 = 0x00000100,
		kR2 = 0x00000200,
		kL1 = 0x00000400,
		kR1 = 0x00000800,
		kTriangle = 0x00001000,
		kCircle = 0x00002000,
		kCross = 0x00004000,
		kSquare = 0x00008000,
		kTouchPad = 0x00100000,
		kIntercepted = 0x80000000, // the system has the controller (its menu is open)
	};

	struct Data
	{
		uint32_t buttons;
		uint8_t leftX, leftY, rightX, rightY;
		uint8_t l2, r2;
		uint8_t reserved0[2];
		float orientation[4];		// quaternion x y z w
		float acceleration[3];		// in g
		float angularVelocity[3];	// in radians per second
		uint8_t touchCount;
		uint8_t reserved1[7];
		struct
		{
			uint16_t x, y;
			uint8_t id;
			uint8_t reserved[3];
		} touch[2];
		int32_t connected;
		uint64_t timestampUs;
		uint8_t extension[16];
		uint8_t connectedCount;
		uint8_t reserved2[2];
		uint8_t deviceUniqueDataLength;
		uint8_t deviceUniqueData[12];
	};
	static_assert(sizeof(Data) == 120);
	static_assert(offsetof(Data, touchCount) == 0x34);
	static_assert(offsetof(Data, connected) == 0x4c);
	static_assert(offsetof(Data, timestampUs) == 0x50);

	constexpr int kMaxPlayers = 4;

	// Opens player 1's controller. False when no user or controller is available.
	bool Init();
	void Shutdown();
	// Looks for users who signed in or out (cheap; call it now and then).
	void Rescan();
	// The latest sample of a player's controller; false when that player has none.
	bool Read(int player, Data& out);
	bool IsConnected(int player);
	// The signed-in user whose controller a player's is (for its speaker's audio port); -1: none.
	int32_t UserId(int player);
	// That user's name, as the PS5 shows it (empty when there is none)
	std::string UserName(int player);
	// The touchpad's resolution, for normalising touch positions.
	void TouchResolution(int player, float& width, float& height);
	void SetVibration(int player, uint8_t largeMotor, uint8_t smallMotor);
	// The launcher's vibration setting: when off, SetVibration stops the motors instead.
	void SetVibrationEnabled(bool enabled);
	void SetLightBar(int player, uint8_t r, uint8_t g, uint8_t b);

	// The port's shortcuts, the same from every controller: the touchpad clicked, then
	//   Options: the in-game menu (app/ingame.h)
	//   L1: the TV or the GamePad as the main screen
	//   R1: the other screen in a corner, or not
	//   Square: a save state to the in-game menu's slot, and Triangle held a moment: a load from it,
	//   on the sides that have save states (SetStateShortcuts)
	// While the touchpad is clicked, Options, L1 and R1 are the shortcuts', not the game's, and Square
	// and Triangle where they are shortcuts.
	enum class Shortcut
	{
		None,
		Menu,
		SwapScreens,
		CornerScreen,
		SaveState,
		LoadState,
	};
	struct Filtered
	{
		uint32_t buttons;		 // what the game sees
		bool touch;				 // the touchpad clicked to touch the GamePad's screen
		bool touchClick = false; // the touchpad clicked as a button (SetTouchpadButton), which a mapping can use
	};
	// Feeds a controller's sample through the shortcut detector. A click touches once it has lasted
	// a moment with no shortcut (whose buttons usually follow at once), or as a tap on its release
	// when it was shorter. For a player whose click is a button, it presses that instead, held while
	// the click is, from the same moment, or as a tap on the release of a shorter one; a shortcut's
	// click presses nothing.
	Filtered FilterShortcuts(int player, uint32_t buttons);
	// FilterShortcuts for a player's controller when nothing has fed it through for a moment: Cemu
	// reads a Pro Controller, a Classic Controller or a Wii Remote only when the game asks for one, so
	// where a game read only the GamePad, the shortcuts of a player given one were never seen (#93)
	void KeepShortcuts(int player);
	// Whether a player's touchpad click is a button rather than a touch of the GamePad's screen (#37)
	void SetTouchpadButton(int player, bool button);
	bool TouchpadButton(int player);
	// A DualSense button (Button's bits) that, while held, presses and lets go again and again by
	// itself, about fifteen times a second, for games that make you mash; 0: none (the app's turbo
	// button setting, one for every player)
	void SetTurbo(uint32_t buttons);
	// A DualSense button's bit by the name the settings keep it by ("Cross", "L2", "D-pad up"); 0 for
	// a stick's direction or anything else
	uint32_t ButtonNamed(const std::string& name);
	// The turbo's beat: buttons with the turbo's held ones let go every other beat
	uint32_t ApplyTurbo(uint32_t buttons);
	// The next shortcut pressed since the last call (or None).
	Shortcut TakeShortcut();
	// Whether touchpad click + Square and + Triangle are the save state shortcuts: on while a 3DS or
	// DS game runs, off otherwise (the Wii U's games keep the buttons)
	void SetStateShortcuts(bool on);
}
