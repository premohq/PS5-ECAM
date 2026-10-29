// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the DS's two screens on the TV, for melonDS's frontend (core.cpp). melonDS draws them in
// software, 256 x 192 each; this shows them on VideoOut through a Vulkan device of its own on the
// driver the app links, at 3840 x 2160 and FIFO, which paces the game to the display's refresh, the
// way Cemu's and Azahar's renderers present.
//
// Each frame copies the two screens to the GPU, draws them where the layout puts them (the 3DS's
// layouts, laid out as Azahar lays out its screens) with the screen filter, the touch cursor over the
// bottom one (Azahar's crosshair), then lets the handhelds' in-game menu draw in the same frame
// (ps5ingame3ds::Record): the menu, the border around the screens, the performance overlay. Built with
// the app, so it names no melonDS type; everything is made, used and destroyed on the thread that
// calls Start, Present and Stop (the emulation's).

#pragma once

#include <cstdint>
#include <string>

namespace ps5melonds::screens
{
	constexpr int kScreenWidth = 256, kScreenHeight = 192;

	// A screen's place on VideoOut's picture, in its pixels; empty (right <= left) when it is not shown
	struct Rect
	{
		float left = 0, top = 0, right = 0, bottom = 0;
		bool Shown() const { return right > left && bottom > top; }
		float Width() const { return right - left; }
		float Height() const { return bottom - top; }
	};

	// Where the layout (ps5azahar::Layout: stacked, the top screen only, a large top screen, side by side)
	// puts the screens on a width x height picture; swapped: the bottom screen takes the top one's place.
	// rotation: the DS turned that many quarter turns clockwise, both screens and the layout with them,
	// for the games held on their side (Hotel Dusk, Brain Age): laid out upright on the picture turned
	// back, then turned onto it.
	// gap: the screens held kGap of the DS's pixels apart, as the hinge holds them, where both show.
	constexpr float kGap = 24;
	void Place(int layout, bool swapped, int rotation, bool gap, float width, float height, Rect& top, Rect& bottom);
	// The quarter turns, 0 to 3, of any number of them
	inline int QuarterTurns(int rotation)
	{
		return ((rotation % 4) + 4) % 4;
	}
	// A point on a screen as the TV shows it (0 to 1 across its rectangle there) as the DS has it (0 to
	// 1 across its 256 x 192), for a screen turned rotation quarter turns clockwise
	inline void Unturn(int rotation, float x, float y, float& dsX, float& dsY)
	{
		switch (QuarterTurns(rotation))
		{
		case 1: dsX = y, dsY = 1.0f - x; break;
		case 2: dsX = 1.0f - x, dsY = 1.0f - y; break;
		case 3: dsX = 1.0f - y, dsY = x; break;
		default: dsX = x, dsY = y; break;
		}
	}

	enum Filter
	{
		kSharp,	 // each DS pixel a solid square, its edges blended over one TV pixel
		kSmooth, // bilinear
		kPixels, // the nearest DS pixel
		kFilterCount,
	};
	constexpr const char* kFilterNames[kFilterCount] = {"Sharp", "Smooth", "Square pixels"};

	struct Frame
	{
		// the screens melonDS drew (256 x 192, 32 bits a pixel: blue, green, red, then an unused byte);
		// null: the ones shown last (the game held while its menu is open)
		const uint32_t* top = nullptr;
		const uint32_t* bottom = nullptr;
		Rect topRect, bottomRect;
		int rotation = 0; // as Place had it: each screen's picture turned that many quarter turns
		int filter = kSharp;
		bool cursor = false;	   // the touch cursor, over the bottom screen
		float cursorX = 0, cursorY = 0; // 0 to 1 across it, as the TV shows it
		bool cursorOnTop = false;  // over the top screen instead: the bottom one is not shown and touched all the same
		// A libretro core's one picture (port/libretro), in place of the DS's screens: pictureWidth x
		// pictureHeight of kPictureMax square at most, 32 bits a pixel as the screens'; null: the one shown
		// last. Shown at pictureRect with the filter, when that is not empty.
		const uint32_t* picture = nullptr;
		uint32_t pictureWidth = 0, pictureHeight = 0;
		Rect pictureRect;
	};
	constexpr uint32_t kPictureMax = 1024;

	// The device and the swapchain on VideoOut. False, with the reason, when they cannot be made (what
	// was made is destroyed again). who: the boot log's word for it ("ds", "retro").
	bool Start(std::string& error, const char* who = "ds");
	// Draws a frame and shows it: returns once it is queued, the frame two back done (VideoOut's flips
	// pace it). False when the device was lost.
	bool Present(const Frame& frame);
	// Everything Start made, once the GPU is done with it.
	void Stop();
	// VideoOut's picture, once started
	uint32_t Width();
	uint32_t Height();
}
