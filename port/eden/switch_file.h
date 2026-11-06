// SPDX-License-Identifier: GPL-3.0-or-later
// Switch package names accepted by the shared Switch and Switch 2 library scanner.
#pragma once

#include <cctype>
#include <string>

namespace ps5eden
{
	inline bool IsSwitchPackageName(std::string name)
	{
		for (char& c : name)
			c = (char)std::tolower((unsigned char)c);
		return name.size() > 4 && (name.ends_with(".nsp") || name.ends_with(".xci") || name.ends_with(".nup"));
	}

	// Keep the shared scanner's Switch and Switch 2 sides mutually exclusive.
	inline bool IsSwitch2Format(const std::string& format)
	{
		return format == "Switch 2";
	}

	inline bool BelongsToSwitchSide(const std::string& format, bool switch2Side)
	{
		return IsSwitch2Format(format) == switch2Side;
	}

	inline bool IsSwitch2PackageName(std::string name)
	{
		for (char& c : name)
			c = (char)std::tolower((unsigned char)c);
		return name.size() > 4 && name.ends_with(".nup");
	}
}
