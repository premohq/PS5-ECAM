// SPDX-License-Identifier: GPL-3.0-or-later
#include "PS5HidWiimote.h"
#include "../ps5/log.h"
#include "../ps5/porpoise_hidapi.h"
#include "../ps5/porpoise_usbhid.hpp"

#include <cwchar>
#include <mutex>

namespace
{
	constexpr uint16 kNintendo = 0x057e;
	constexpr uint16 kWiiRemote = 0x0306;	  // and the DolphinBar's Wii Remote mode
	constexpr uint16 kWiiRemotePlus = 0x0330; // the Wii Remote Plus
	constexpr size_t kMaxInputReport = 22;
	// the Wii U Pro Controller has the Wii Remote Plus's ids by Bluetooth; Cemu leaves it out too
	constexpr const wchar_t* kProController = L"Nintendo RVL-CNT-01-UC";

	void Log(const char* line)
	{
		ps5log::Line("[wiimote] {}", line);
	}
}

PS5HidWiimote::PS5HidWiimote(hid_device_* device, std::string_view path)
	: m_device(device), m_path(path)
{
}

PS5HidWiimote::~PS5HidWiimote()
{
	hid_close(m_device);
}

bool PS5HidWiimote::write_data(const std::vector<uint8>& data)
{
	// a DolphinBar slot without a Remote refuses the report (EPIPE): Cemu reads that as no Remote
	return hid_write(m_device, data.data(), data.size()) >= 0;
}

std::optional<std::vector<uint8>> PS5HidWiimote::read_data()
{
	std::array<uint8, kMaxInputReport> report{};
	const int read = hid_read(m_device, report.data(), report.size()); // nonblocking: 0 is nothing yet
	if (read < 0)
		return {};
	return std::vector<uint8>(report.cbegin(), report.cbegin() + read);
}

bool PS5HidWiimote::operator==(const WiimoteDevice& o) const
{
	const auto other = dynamic_cast<const PS5HidWiimote*>(&o);
	return other && m_path == other->m_path;
}

std::vector<WiimoteDevicePtr> PS5HidWiimote::get_devices()
{
	static std::once_flag s_log;
	std::call_once(s_log, [] {
		porpoise::usbhid::set_log(Log);
		ps5log::Line("[wiimote] looking for a DolphinBar (Wii Remote mode) every two seconds");
	});
	std::vector<WiimoteDevicePtr> devices;
	hid_device_info* found = hid_enumerate(kNintendo, 0);
	for (hid_device_info* it = found; it; it = it->next)
	{
		if (it->product_id != kWiiRemote && it->product_id != kWiiRemotePlus)
			continue;
		if (it->product_string && std::wcscmp(it->product_string, kProController) == 0)
			continue;
		// EBUSY: Cemu's provider has this one open already
		if (hid_device_* device = hid_open_path(it->path))
		{
			hid_set_nonblocking(device, 1);
			devices.push_back(std::make_shared<PS5HidWiimote>(device, it->path));
		}
	}
	hid_free_enumeration(found);
	return devices;
}
