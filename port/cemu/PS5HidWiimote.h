// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: a real Wii Remote for Cemu's own Wii Remote code (input/api/Wiimote), through a
// Mayflash DolphinBar in its Wii Remote mode, as Cemu's HidapiWiimote has one on a PC. The PS5 has
// no hidapi in a title; the hid_* calls here are Project Porpoise's, over the console's USB nodes
// (ps5/porpoise_usbhid.hpp says how).

#pragma once

#include "input/api/Wiimote/WiimoteDevice.h"

struct hid_device_;

class PS5HidWiimote : public WiimoteDevice
{
public:
	PS5HidWiimote(hid_device_* device, std::string_view path);
	~PS5HidWiimote() override;

	bool write_data(const std::vector<uint8>& data) override;
	std::optional<std::vector<uint8>> read_data() override;
	bool operator==(const WiimoteDevice& o) const override;

	// The DolphinBar's Wii Remote slots not open yet (an open one stays with Cemu's provider), each
	// opened: Cemu's provider keeps those whose status request goes through, a slot with a Remote.
	static std::vector<WiimoteDevicePtr> get_devices();

private:
	hid_device_* m_device;
	const std::string m_path;
};
