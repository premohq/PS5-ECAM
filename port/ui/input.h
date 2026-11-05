// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM's UI kit: the DualSense as actions. Any connected
// controller drives the UI; the left stick moves as the D-pad does; the directions, the shoulders
// and the triggers repeat while held (400 ms, then every 90 ms, as the classic launcher's); every
// button's hold is timed, for what is held to confirm. Nothing counts as pressed until it has been
// let go once, and while the system has the controller (its menu) the UI sees nothing.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ui
{
	enum class Button : uint8_t
	{
		Up,
		Down,
		Left,
		Right,
		Cross,
		Circle,
		Square,
		Triangle,
		L1,
		R1,
		L2,
		R2,
		Options,
		Touchpad,
		R3,
		Count,
	};

	struct Press
	{
		Button button;
		bool repeat; // a held button's repeat, not its first press
	};

	struct Actions
	{
		std::vector<Press> presses; // this frame's, in order
		uint32_t held = 0;			// 1 << Button
		std::array<float, (size_t)Button::Count> heldSeconds{};
		std::vector<Button> released; // let go this frame

		bool Held(Button button) const { return held & (1u << (int)button); }
		float HeldFor(Button button) const { return Held(button) ? heldSeconds[(size_t)button] : 0.0f; }
		bool WasReleased(Button button) const
		{
			for (Button b : released)
				if (b == button)
					return true;
			return false;
		}
	};

	class Input
	{
	public:
		// The controllers now; now in seconds
		Actions Poll(double now);
		// What is held now counts once it is let go (after something else had the controller)
		void Reset() { m_armed.fill(false); }

	private:
		std::array<bool, (size_t)Button::Count> m_down{}, m_armed{};
		std::array<double, (size_t)Button::Count> m_since{};
		std::array<double, (size_t)Button::Count> m_repeatAt{};
	};
}
