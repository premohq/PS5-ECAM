// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the remote touch screen's pictures (stream.h) as JPEGs, at quality 75, made no larger
// than the GamePad's own 854 x 480. TurboJPEG, which the app already links for box art.

#pragma once

#include <cstdint>
#include <vector>

namespace ps5stream
{
	// RGB rows in; empty when it fails
	std::vector<uint8_t> EncodeJpeg(const uint8_t* rgb, int width, int height);
}
