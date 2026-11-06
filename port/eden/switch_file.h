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

	inline bool IsSwitch2PackageName(std::string name)
	{
		for (char& c : name)
			c = (char)std::tolower((unsigned char)c);
		return name.size() > 4 && name.ends_with(".nup");
	}
}
