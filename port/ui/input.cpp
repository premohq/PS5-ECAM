// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM's UI kit: the DualSense as actions (input.h).

#include "input.h"
#include "../ps5/pad.h"

namespace ui
{
	namespace
	{
		constexpr double kRepeatDelay = 0.4, kRepeatRate = 0.09;

		struct Mapping
		{
			uint32_t mask;
			Button button;
			bool repeats;
		};
		constexpr Mapping kMap[] = {
			{ps5pad::kUp, Button::Up, true},
			{ps5pad::kDown, Button::Down, true},
			{ps5pad::kLeft, Button::Left, true},
			{ps5pad::kRight, Button::Right, true},
			{ps5pad::kCross, Button::Cross, false},
			{ps5pad::kCircle, Button::Circle, false},
			{ps5pad::kSquare, Button::Square, false},
			{ps5pad::kTriangle, Button::Triangle, false},
			{ps5pad::kL1, Button::L1, true},
			{ps5pad::kR1, Button::R1, true},
			{ps5pad::kL2, Button::L2, true},
			{ps5pad::kR2, Button::R2, true},
			{ps5pad::kOptions, Button::Options, false},
			{ps5pad::kTouchPad, Button::Touchpad, false},
			{ps5pad::kR3, Button::R3, false},
		};
	}

	Actions Input::Poll(double now)
	{
		uint32_t buttons = 0;
		bool intercepted = false;
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
		{
			ps5pad::Data data;
			if (!ps5pad::Read(player, data))
				continue;
			if (data.buttons & ps5pad::kIntercepted)
			{
				intercepted = true;
				continue;
			}
			buttons |= data.buttons;
			if (data.leftY < 64)
				buttons |= ps5pad::kUp;
			else if (data.leftY > 192)
				buttons |= ps5pad::kDown;
			if (data.leftX < 64)
				buttons |= ps5pad::kLeft;
			else if (data.leftX > 192)
				buttons |= ps5pad::kRight;
		}
		Actions actions;
		if (intercepted)
		{
			// the system's menu has the controller: what is held as it gives it back counts once let go
			Reset();
			m_down.fill(false);
			return actions;
		}
		for (const Mapping& mapping : kMap)
		{
			const size_t slot = (size_t)mapping.button;
			const bool down = buttons & mapping.mask;
			if (!down)
			{
				if (m_down[slot] && m_armed[slot])
					actions.released.push_back(mapping.button);
				m_down[slot] = false;
				m_armed[slot] = true;
				continue;
			}
			if (!m_armed[slot])
				continue; // held from before: not until it is let go
			if (!m_down[slot])
			{
				actions.presses.push_back({mapping.button, false});
				m_since[slot] = now;
				m_repeatAt[slot] = now + kRepeatDelay;
			}
			else if (mapping.repeats && now >= m_repeatAt[slot])
			{
				actions.presses.push_back({mapping.button, true});
				m_repeatAt[slot] = now + kRepeatRate;
			}
			m_down[slot] = true;
			actions.held |= 1u << slot;
			actions.heldSeconds[slot] = (float)(now - m_since[slot]);
		}
		return actions;
	}
}
