// SPDX-License-Identifier: GPL-3.0-or-later
// Each console's look: its controller (or the handheld that is its controller, or what stands for it: a Disk
// Card, a cabinet, a dish) drawn in a few rounded
// shapes, for its tile in the side switch and the mark at the top left, every console's in a ring; and the
// later consoles' backdrop motifs, each its own, as the first four sides have theirs (shell.cpp: bubbles,
// waves, pixels, bars in pairs). Its colour is its accent (AccentOf: ps5retro::kSystems' for the later ones).
#include "shell_internal.h"
#include <algorithm>
#include <cmath>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr float kTau = 6.2831853f;
		constexpr uint32_t kJoyConBlue = 0xfff5b800; // #00b8f5, the Switch's neon blue (DrawJoyCons)

		uint32_t FromRgb(uint32_t rgb) { return 0xff000000 | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff); }

		// its colour as the mark shows it: the dark themes' (the mark's ground is dark on every theme)
		uint32_t Vivid(System side)
		{
			if (ps5retro::IsRetro((int)side))
				return FromRgb(ps5retro::Of((int)side).colour);
			return side == System::N3ds ? kN3dsDefault : side == System::Nds ? kNdsDefault : side == System::Switch ? kNxDefault : kWiiuDefault;
		}
	}

	void DrawConsole(Canvas& canvas, System side, float cx, float cy, float s, uint32_t body, uint32_t cut, uint32_t second)
	{
		// a shape by its centre, width and height, in the drawing's width (s)
		auto shape = [&](float x, float y, float w, float h, float r, uint32_t colour) {
			canvas.Rect({cx + (x - w / 2) * s, cy + (y - h / 2) * s, w * s, h * s}, r * s, colour);
		};
		auto cross = [&](float x, float y, float size) {
			shape(x, y, size, size * 0.34f, 0, cut);
			shape(x, y, size * 0.34f, size, 0, cut);
		};
		const std::string_view key = ps5retro::IsRetro((int)side) ? ps5retro::Of((int)side).key : "";
		if (side == System::WiiU)
		{
			// the GamePad: its screen between the two sticks
			shape(0, 0, 1.0f, 0.6f, 0.16f, body);
			shape(0, 0, 0.5f, 0.38f, 0.03f, cut);
			shape(-0.37f, -0.12f, 0.09f, 0.09f, 0.045f, cut);
			shape(0.37f, -0.12f, 0.09f, 0.09f, 0.045f, cut);
		}
		else if (side == System::Switch)
		{
			// the two Joy-Con, each flat on the side that slides onto the console
			shape(-0.25f, 0, 0.34f, 0.86f, 0.17f, second);
			shape(-0.16f, 0, 0.16f, 0.86f, 0, second);
			shape(0.25f, 0, 0.34f, 0.86f, 0.17f, body);
			shape(0.16f, 0, 0.16f, 0.86f, 0, body);
			shape(-0.25f, -0.16f, 0.12f, 0.12f, 0.06f, cut);
			shape(0.25f, 0.12f, 0.12f, 0.12f, 0.06f, cut);
		}
		else if (side == System::Nds || side == System::N3ds)
		{
			// open, its two screens: the 3DS's top one wider
			const bool n3ds = side == System::N3ds;
			shape(0, -0.24f, 0.72f, 0.44f, 0.08f, body);
			shape(0, -0.24f, n3ds ? 0.56f : 0.46f, 0.3f, 0.02f, cut);
			shape(0, 0.25f, 0.72f, 0.44f, 0.08f, body);
			shape(0, 0.24f, 0.38f, 0.28f, 0.02f, cut);
			if (n3ds)
				shape(-0.27f, 0.16f, 0.09f, 0.09f, 0.045f, cut);
		}
		else if (key == "gamecube")
		{
			// its body, the two grips under it, the stick and the big A
			shape(-0.3f, 0.14f, 0.28f, 0.5f, 0.14f, body);
			shape(0.3f, 0.14f, 0.28f, 0.5f, 0.14f, body);
			shape(0, -0.08f, 0.9f, 0.46f, 0.22f, body);
			shape(-0.22f, -0.1f, 0.13f, 0.13f, 0.065f, cut);
			shape(0.2f, -0.08f, 0.16f, 0.16f, 0.08f, cut);
		}
		else if (key == "wii")
		{
			// the Wii Remote, upright: its cross, A, 1 and 2
			shape(0, 0, 0.3f, 1.0f, 0.13f, body);
			cross(0, -0.3f, 0.15f);
			shape(0, -0.1f, 0.1f, 0.1f, 0.05f, cut);
			shape(0, 0.22f, 0.06f, 0.06f, 0.03f, cut);
			shape(0, 0.33f, 0.06f, 0.06f, 0.03f, cut);
		}
		else if (key == "n64")
		{
			// three handles, the stick on the middle one
			shape(0, -0.2f, 0.98f, 0.34f, 0.17f, body);
			shape(-0.36f, 0.12f, 0.24f, 0.5f, 0.12f, body);
			shape(0, 0.1f, 0.22f, 0.42f, 0.11f, body);
			shape(0.36f, 0.12f, 0.24f, 0.5f, 0.12f, body);
			shape(0, 0.06f, 0.09f, 0.09f, 0.045f, cut);
		}
		else if (key == "nes")
		{
			// a flat box with square corners: the cross, Select and Start, B and A
			shape(0, 0, 1.0f, 0.44f, 0.05f, body);
			cross(-0.27f, 0, 0.21f);
			shape(-0.02f, 0.05f, 0.1f, 0.04f, 0.02f, cut);
			shape(0.2f, 0.05f, 0.11f, 0.11f, 0.055f, cut);
			shape(0.35f, 0.05f, 0.11f, 0.11f, 0.055f, cut);
		}
		else if (key == "snes")
		{
			// rounded ends, the cross and the four buttons in a diamond
			shape(0, 0, 1.0f, 0.5f, 0.25f, body);
			cross(-0.27f, 0, 0.19f);
			for (const auto& [x, y] : {std::pair{0.3f, -0.09f}, {0.3f, 0.09f}, {0.21f, 0.0f}, {0.39f, 0.0f}})
				shape(x, y, 0.08f, 0.08f, 0.04f, cut);
		}
		else if (key == "gb" || key == "gbc")
		{
			// upright, its screen above the cross and the two buttons; the Game Boy's screen its green
			const bool gb = key == "gb";
			shape(0, 0, gb ? 0.66f : 0.6f, 1.0f, gb ? 0.07f : 0.1f, body);
			shape(0, -0.2f, gb ? 0.52f : 0.46f, 0.4f, 0.04f, cut);
			if (gb)
				shape(0, -0.2f, 0.34f, 0.28f, 0.01f, second);
			cross(-0.15f, 0.23f, 0.16f);
			shape(0.12f, 0.27f, 0.09f, 0.09f, 0.045f, cut);
			shape(0.21f, 0.2f, 0.09f, 0.09f, 0.045f, cut);
		}
		else if (key == "gba")
		{
			// wide, its screen between the cross and A and B
			shape(0, 0, 1.0f, 0.56f, 0.26f, body);
			shape(0, -0.01f, 0.42f, 0.34f, 0.03f, cut);
			cross(-0.33f, -0.02f, 0.13f);
			shape(0.31f, 0.02f, 0.08f, 0.08f, 0.04f, cut);
			shape(0.39f, -0.05f, 0.08f, 0.08f, 0.04f, cut);
		}
		else if (key == "vb")
		{
			// the visor and its two lenses, on its stand
			shape(0, 0.18f, 0.1f, 0.32f, 0, body);
			shape(0, 0.39f, 0.62f, 0.08f, 0.04f, body);
			shape(0, -0.15f, 1.0f, 0.4f, 0.12f, body);
			shape(-0.22f, -0.15f, 0.28f, 0.18f, 0.07f, cut);
			shape(0.22f, -0.15f, 0.28f, 0.18f, 0.07f, cut);
		}
		else if (key == "pokemini")
		{
			// small and nearly square, its screen and A
			shape(0, 0, 0.74f, 0.62f, 0.14f, body);
			shape(0, -0.06f, 0.44f, 0.3f, 0.03f, cut);
			shape(0.24f, 0.17f, 0.08f, 0.08f, 0.04f, cut);
		}
		else if (key == "fds")
		{
			// the Disk Card: its label, and the window its disk shows through
			shape(0, 0, 0.8f, 0.98f, 0.05f, body);
			shape(0, -0.26f, 0.6f, 0.34f, 0.03f, cut);
			shape(0, 0.24f, 0.24f, 0.34f, 0.03f, cut);
		}
		else if (key == "sgb")
		{
			// its cartridge, and the Game Boy's in the top of it
			shape(0, -0.34f, 0.4f, 0.34f, 0.04f, body);
			shape(0, -0.36f, 0.26f, 0.16f, 0.02f, cut);
			shape(0, 0.14f, 0.92f, 0.62f, 0.1f, body);
			shape(0, 0.17f, 0.6f, 0.32f, 0.03f, cut);
		}
		else if (key == "gw")
		{
			// one screen, a button either side of it, Game and Time above the right one
			shape(0, 0, 1.0f, 0.64f, 0.08f, body);
			shape(0, -0.02f, 0.5f, 0.42f, 0.02f, cut);
			shape(-0.38f, 0.14f, 0.13f, 0.13f, 0.065f, cut);
			shape(0.38f, 0.14f, 0.13f, 0.13f, 0.065f, cut);
			shape(0.33f, -0.2f, 0.08f, 0.04f, 0.02f, cut);
			shape(0.43f, -0.2f, 0.08f, 0.04f, 0.02f, cut);
		}
		else if (key == "satellaview")
		{
			// a dish turned to the sky, on its stand
			shape(-0.06f, 0.4f, 0.1f, 0.16f, 0, body);
			shape(-0.06f, 0.47f, 0.5f, 0.08f, 0.04f, body);
			shape(-0.06f, -0.04f, 0.74f, 0.74f, 0.37f, body);
			shape(0.06f, -0.16f, 0.62f, 0.62f, 0.31f, cut);
			canvas.Line(cx - 0.06f * s, cy - 0.04f * s, cx + 0.3f * s, cy - 0.36f * s, 0.07f * s, body);
			shape(0.32f, -0.38f, 0.13f, 0.13f, 0.065f, body);
		}
		else if (key == "vs")
		{
			// a cabinet: its marquee, screen, controls and coin slots
			shape(0, 0.04f, 0.62f, 0.92f, 0.06f, body);
			shape(0, -0.42f, 0.72f, 0.14f, 0.03f, body);
			shape(0, -0.14f, 0.46f, 0.32f, 0.03f, cut);
			shape(0, 0.12f, 0.52f, 0.06f, 0, cut);
			shape(-0.08f, 0.31f, 0.06f, 0.12f, 0.02f, cut);
			shape(0.08f, 0.31f, 0.06f, 0.12f, 0.02f, cut);
		}
		else if (key == "ique")
		{
			// the console that is a controller: its cross, buttons and stick
			shape(0, 0.12f, 0.42f, 0.5f, 0.18f, body);
			shape(0, -0.02f, 1.0f, 0.48f, 0.24f, body);
			cross(-0.3f, -0.04f, 0.16f);
			shape(0.26f, -0.08f, 0.09f, 0.09f, 0.045f, cut);
			shape(0.36f, 0.0f, 0.09f, 0.09f, 0.045f, cut);
			shape(0, 0.14f, 0.1f, 0.1f, 0.05f, cut);
		}
		else
		{
			// a system with no drawing of its own yet: a pad
			shape(0, 0, 0.9f, 0.46f, 0.23f, body);
			cross(-0.24f, 0, 0.18f);
			shape(0.22f, 0, 0.1f, 0.1f, 0.05f, cut);
		}
	}

	void Shell::DrawMark(Canvas& canvas, float cx, float cy, float radius)
	{
		// a dark disc on every theme, every console's controller in a ring on it in the bar's order from
		// the top, and the open side's larger in the middle
		constexpr uint32_t kGround = 0xff2b1d16, kGroundLow = 0xff1d130e; // #161d2b, #0e131d
		const Box disc{cx - radius, cy - radius, radius * 2, radius * 2};
		canvas.LinearGradient(disc, radius, kGround, kGroundLow, cx, disc.y, cx, disc.Bottom());
		canvas.Ring(disc, radius, 1.5f, kGlassEdge);
		auto second = [](System side, uint32_t colour) {
			return side == System::Switch ? kJoyConBlue : ps5retro::Is((int)side, "gb") ? ui::Mix(colour, 0xffffffff, 0.5f) : colour;
		};
		const float ring = radius * 0.7f;
		const float size = std::min(radius * 0.36f, 2 * ring * std::sin(kTau / 2 / kSides) * 0.95f);
		for (int i = 0; i < kSides; i++)
		{
			const System side = BarSide(i);
			const float angle = kTau * i / kSides - kTau / 4;
			const uint32_t colour = side == m_side ? Vivid(side) : ui::Mix(Vivid(side), kGroundLow, 0.25f);
			DrawConsole(canvas, side, cx + std::cos(angle) * ring, cy + std::sin(angle) * ring, size, colour, kGround, second(side, colour));
		}
		DrawConsole(canvas, m_side, cx, cy, radius * 0.72f, Vivid(m_side), kGround, second(m_side, Vivid(m_side)));
	}

	// -- the later consoles' motifs: each one's own, in its colour, on the bubbles' places and speeds ----------

	void Shell::DrawConsoleMotif(Canvas& canvas, float alpha)
	{
		const bool still = m_settings.ui.reduceMotion;
		const float t = still ? 0.0f : (float)(m_now - m_start);
		const uint32_t colour = Accent();
		const std::string_view key = ps5retro::Of(SideIndex()).key;
		// a place rising at its speed (times rate), back under the screen once it is above it
		auto rise = [&](Bubble& place, float rate, float margin) {
			if (still)
				return;
			place.y -= place.speed * rate * m_dt;
			if (place.y < -margin)
				place.y = 1080 + margin;
		};
		auto faint = [&](const Bubble& place, float strength) { return std::min(1.0f, place.alpha * 1.6f) * strength * alpha; };
		if (key == "gamecube")
		{
			// cubes, edges drawn, rising slowly: the console's own shape
			for (size_t i = 0; i < m_bubbles.size(); i += 4)
			{
				Bubble& place = m_bubbles[i];
				const float a = 12 + place.radius * 0.9f;
				rise(place, 0.5f, a * 2);
				const float x = place.x, y = place.y, w = a * 0.866f;
				const uint32_t edge = ui::SetAlpha(colour, faint(place, 0.6f));
				const float px[6] = {x, x + w, x + w, x, x - w, x - w}, py[6] = {y - a, y - a / 2, y + a / 2, y + a, y + a / 2, y - a / 2};
				for (int k = 0; k < 6; k++)
					canvas.Line(px[k], py[k], px[(k + 1) % 6], py[(k + 1) % 6], 2.5f, edge);
				canvas.Line(x, y, px[1], py[1], 2.5f, edge);
				canvas.Line(x, y, px[5], py[5], 2.5f, edge);
				canvas.Line(x, y, px[3], py[3], 2.5f, edge);
				canvas.Triangle(px[0], py[0], px[1], py[1], x, y, ui::SetAlpha(colour, faint(place, 0.18f)));
				canvas.Triangle(px[0], py[0], x, y, px[5], py[5], ui::SetAlpha(colour, faint(place, 0.18f)));
			}
		}
		else if (key == "wii")
		{
			// rings spreading slowly from still points, as the Wii Menu's calm
			const uint32_t light = ui::Mix(colour, 0xffffffff, 0.4f);
			for (size_t i = 0; i < m_bubbles.size(); i += 6)
			{
				const Bubble& place = m_bubbles[i];
				const float period = 6 + place.radius * 0.12f;
				for (float offset : {0.0f, 0.5f})
				{
					const float f = still ? 0.3f + offset : std::fmod(t / period + i * 0.37f + offset, 1.0f);
					const float r = 14 + f * (90 + place.radius * 3);
					canvas.Ring({place.x - r, place.y - r, r * 2, r * 2}, r, 2.5f, ui::SetAlpha(light, faint(place, 0.95f) * (1 - f)));
				}
			}
		}
		else if (key == "n64")
		{
			// triangles turning slowly as they rise: the first polygons at home
			for (size_t i = 0; i < m_bubbles.size(); i += 3)
			{
				Bubble& place = m_bubbles[i];
				const float a = 10 + place.radius * 0.8f;
				rise(place, 0.6f, a * 2);
				const float turn = t * (place.drift > 0 ? 0.3f : -0.3f) + i;
				float x[3], y[3];
				for (int k = 0; k < 3; k++)
				{
					x[k] = place.x + std::cos(turn + k * kTau / 3) * a;
					y[k] = place.y + std::sin(turn + k * kTau / 3) * a;
				}
				const uint32_t fill = i % 9 == 0 ? ui::Mix(colour, 0xffffffff, 0.5f) : colour;
				canvas.Triangle(x[0], y[0], x[1], y[1], x[2], y[2], ui::SetAlpha(fill, faint(place, 0.5f)));
			}
		}
		else if (key == "snes")
		{
			// the four buttons in their diamond and their colours, rising
			static constexpr uint32_t kButtons[4] = {0xffd86240, 0xff3f33e5, 0xff30c3f2, 0xff5aae2f}; // X blue, A red, B yellow, Y green
			for (size_t i = 0; i < m_bubbles.size(); i += 5)
			{
				Bubble& place = m_bubbles[i];
				const float u = 4 + place.radius * 0.3f;
				rise(place, 0.55f, u * 4);
				const float dx[4] = {0, 1.6f, 0, -1.6f}, dy[4] = {-1.6f, 0, 1.6f, 0};
				for (int k = 0; k < 4; k++)
				{
					const float x = place.x + dx[k] * u, y = place.y + dy[k] * u;
					canvas.Rect({x - u, y - u, u * 2, u * 2}, u, ui::SetAlpha(kButtons[k], faint(place, 0.55f)));
				}
			}
		}
		else if (key == "nes")
		{
			// the pad's cross rising, and a television's lines across the picture
			for (size_t i = 0; i < m_bubbles.size(); i += 3)
			{
				Bubble& place = m_bubbles[i];
				const float a = 10 + place.radius * 0.9f;
				rise(place, 0.65f, a);
				const uint32_t fill = ui::SetAlpha(colour, faint(place, 0.5f));
				canvas.Rect({place.x - a / 2, place.y - a / 6, a, a / 3}, 0, fill);
				canvas.Rect({place.x - a / 6, place.y - a / 2, a / 3, a / 6 * 2}, 0, fill);
				canvas.Rect({place.x - a / 6, place.y + a / 6, a / 3, a / 6 * 2}, 0, fill);
			}
			for (float y = 0; y < 1080; y += 6)
				canvas.Rect({0, y, 1920, 2}, 0, ui::SetAlpha(0xff000000, 0.035f * alpha));
		}
		else if (key == "gba")
		{
			// streaks of light gliding up and across, as its start's sweep
			const float dx = std::cos(-0.35f), dy = std::sin(-0.35f);
			for (size_t i = 0; i < m_bubbles.size(); i += 5)
			{
				Bubble& place = m_bubbles[i];
				const float length = 140 + place.radius * 7;
				if (!still)
				{
					place.x += place.speed * 1.2f * m_dt;
					place.y -= place.speed * 0.42f * m_dt;
					// its tail off the screen: back in at the other side
					if (place.x - dx * length > 1920)
						place.x -= 1920 + dx * length;
					if (place.y - dy * length < 0)
						place.y += 1080 - dy * length;
				}
				const float width = 2 + place.radius * 0.1f;
				for (int k = 0; k < 3; k++)
				{
					const float from = length * k / 3, to = length * (k + 1) / 3;
					canvas.Line(place.x - dx * from, place.y - dy * from, place.x - dx * to, place.y - dy * to, width,
						ui::SetAlpha(ui::Mix(colour, 0xffffffff, 0.3f), faint(place, 0.95f) * (1 - k / 3.0f)));
				}
			}
		}
		else if (key == "gbc")
		{
			// small pixels in its shells' colours, rising
			static constexpr uint32_t kShells[5] = {0xff7b44e8, 0xffc94f7a, 0xff2ad1a8, 0xff32d1f7, 0xffb5b92b}; // berry, grape, kiwi, dandelion, teal
			for (size_t i = 0; i < m_bubbles.size(); i += 2)
			{
				Bubble& place = m_bubbles[i];
				const float size = 8 + place.radius * 0.5f;
				rise(place, 0.8f, size);
				canvas.Rect({place.x - size / 2, place.y - size / 2, size, size}, size * 0.18f, ui::SetAlpha(kShells[i / 2 % 5], faint(place, 0.8f)));
			}
		}
		else if (key == "gb")
		{
			// its screen's dot matrix, a slow shimmer through its four shades
			constexpr float kCell = 54, kDot = 40;
			for (int row = 0; row * kCell < 1080; row++)
				for (int column = 0; column * kCell < 1920; column++)
				{
					if ((column * 7 + row * 13) % 3)
						continue;
					const float shade = std::floor((std::sin(t * 0.4f + (column + row) * 0.35f) + 1) * 1.99f); // 0 to 3
					canvas.Rect({column * kCell + (kCell - kDot) / 2, row * kCell + (kCell - kDot) / 2, kDot, kDot}, 4,
						ui::SetAlpha(colour, (0.03f + 0.03f * shade) * alpha));
				}
		}
		else if (key == "vb")
		{
			// its red lines on black: a floor of lines running to the horizon, coming closer
			const float horizon = 560, centre = 960;
			const uint32_t line = ui::SetAlpha(colour, 0.16f * alpha);
			const float scroll = still ? 0.0f : std::fmod(t * 0.5f, 1.0f);
			for (int k = 1; k <= 16; k++)
			{
				const float z = k - scroll;
				const float y = horizon + 420 / z;
				if (y < 1080 && z > 0.05f)
					canvas.Line(0, y, 1920, y, 2, ui::SetAlpha(colour, 0.16f * alpha * std::min(1.0f, (y - horizon) / 120)));
			}
			for (int j = -9; j <= 9; j++)
				canvas.Line(centre, horizon, centre + j * 520, 1080 + 260, 2, line);
			canvas.Line(0, horizon, 1920, horizon, 2, ui::SetAlpha(colour, 0.24f * alpha));
		}
		else if (key == "pokemini")
		{
			// little bolts rising, each with a bounce of its own
			for (size_t i = 0; i < m_bubbles.size(); i += 5)
			{
				Bubble& place = m_bubbles[i];
				const float a = 9 + place.radius * 0.6f;
				rise(place, 0.6f, a * 2);
				const float x = place.x, y = place.y + (still ? 0 : std::sin(t * 3 + i) * 4);
				const float px[4] = {x - 0.25f * a, x + 0.18f * a, x - 0.12f * a, x + 0.25f * a}, py[4] = {y - a, y - 0.08f * a, y + 0.06f * a, y + a};
				for (int k = 0; k < 3; k++)
					canvas.Line(px[k], py[k], px[k + 1], py[k + 1], a * 0.22f, ui::SetAlpha(colour, faint(place, 0.9f)));
			}
		}
		else if (key == "fds")
		{
			// Disk Cards rising, their labels along the top
			for (size_t i = 0; i < m_bubbles.size(); i += 4)
			{
				Bubble& place = m_bubbles[i];
				const float w = 14 + place.radius * 0.9f, h = w * 1.22f;
				rise(place, 0.5f, h);
				const Box card{place.x - w / 2, place.y - h / 2, w, h};
				canvas.Ring(card, w * 0.08f, 2.5f, ui::SetAlpha(colour, faint(place, 0.6f)));
				canvas.Rect({card.x + w * 0.16f, card.y + h * 0.1f, w * 0.68f, h * 0.3f}, w * 0.04f, ui::SetAlpha(colour, faint(place, 0.35f)));
			}
		}
		else if (key == "sgb")
		{
			// a border round the picture, as its games drew round theirs, a light going round it
			constexpr float kOuter = 30, kInner = 64, kStep = 40;
			canvas.Ring({kOuter, kOuter, 1920 - 2 * kOuter, 1080 - 2 * kOuter}, 26, 3, ui::SetAlpha(colour, 0.14f * alpha));
			canvas.Ring({kInner, kInner, 1920 - 2 * kInner, 1080 - 2 * kInner}, 18, 2, ui::SetAlpha(colour, 0.1f * alpha));
			const float middle = (kOuter + kInner) / 2, square = 9;
			const float perimeter = 2 * (1920 - 2 * middle) + 2 * (1080 - 2 * middle);
			const float lit = std::fmod(t * 160, perimeter);
			for (float d = 0; d < perimeter; d += kStep)
			{
				float x, y;
				const float w = 1920 - 2 * middle, h = 1080 - 2 * middle;
				if (d < w) x = middle + d, y = middle;
				else if (d < w + h) x = 1920 - middle, y = middle + d - w;
				else if (d < 2 * w + h) x = 1920 - middle - (d - w - h), y = 1080 - middle;
				else x = middle, y = 1080 - middle - (d - 2 * w - h);
				const float near = std::max(0.0f, 1 - std::fabs(d - lit) / 400);
				canvas.Rect({x - square / 2, y - square / 2, square, square}, 2, ui::SetAlpha(colour, (0.08f + 0.22f * near) * alpha));
			}
		}
		else if (key == "gw")
		{
			// a juggler's three arcs in LCD segments, each lit one stepping along as on its screen
			const int step = still ? 2 : (int)(t * 2.5f);
			for (int arc = 0; arc < 3; arc++)
			{
				const float radius = 170 + arc * 90;
				const int lit = (step + arc * 2) % 7;
				for (int k = 0; k < 7; k++)
				{
					const float angle = kTau * (0.56f + 0.065f * (k + (arc % 2 ? 0.5f : 0.0f)));
					const float x = 1420 + std::cos(angle) * radius, y = 760 + std::sin(angle) * radius;
					canvas.Rect({x - 12, y - 12, 24, 24}, 7, ui::SetAlpha(colour, (k == lit ? 0.32f : 0.06f) * alpha));
				}
			}
		}
		else if (key == "satellaview")
		{
			// the broadcast: rings spreading from a satellite above the top right corner
			const float cx = 1780, cy = -120;
			for (int k = 0; k < 7; k++)
			{
				const float f = still ? k / 7.0f : std::fmod(t * 0.06f + k / 7.0f, 1.0f);
				const float r = 160 + f * 1900;
				canvas.Ring({cx - r, cy - r, r * 2, r * 2}, r, 3, ui::SetAlpha(colour, 0.16f * alpha * (1 - f)));
			}
		}
		else if (key == "vs")
		{
			// a cabinet's bulbs down both sides of the screen, their light chasing
			const int step = still ? 0 : (int)(t * 6);
			int bulb = 0;
			for (float y = 150; y < 1060; y += 36, bulb++)
				for (float x : {30.0f, 1890.0f})
				{
					const bool lit = (bulb + step) % 4 == 0;
					canvas.Rect({x - 7, y - 7, 14, 14}, 7, ui::SetAlpha(colour, (lit ? 0.38f : 0.08f) * alpha));
				}
		}
		else if (key == "ique")
		{
			// diamonds rising, drawn in their edges, a dot in each
			for (size_t i = 0; i < m_bubbles.size(); i += 4)
			{
				Bubble& place = m_bubbles[i];
				const float a = 10 + place.radius * 0.7f;
				rise(place, 0.55f, a * 2);
				const uint32_t edge = ui::SetAlpha(colour, faint(place, 0.6f));
				const float x = place.x, y = place.y;
				canvas.Line(x, y - a, x + a, y, 2.5f, edge);
				canvas.Line(x + a, y, x, y + a, 2.5f, edge);
				canvas.Line(x, y + a, x - a, y, 2.5f, edge);
				canvas.Line(x - a, y, x, y - a, 2.5f, edge);
				canvas.Rect({x - a * 0.18f, y - a * 0.18f, a * 0.36f, a * 0.36f}, a * 0.18f, edge);
			}
		}
		else
		{
			// a system with no motif of its own yet: pixels, as the DS's, in its colour
			for (size_t i = 0; i < m_bubbles.size(); i += 3)
			{
				Bubble& place = m_bubbles[i];
				const float size = 8 + place.radius * 0.9f;
				rise(place, 0.7f, size);
				canvas.Rect({place.x - size / 2, place.y - size / 2, size, size}, size * 0.22f, ui::SetAlpha(colour, faint(place, 0.5f)));
			}
		}
	}
}
