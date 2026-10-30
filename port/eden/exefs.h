// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a game folder that holds its game as an ExeFS and a RomFS (exefs/ with main and main.npdm,
// romfs/), how a Switch 2 title is dumped. Eden plays it from the path of its main, "<game>/exefs/main"
// (eden-nx2, patches/eden 0021 and 0022). Its title ID is its main.npdm's, and a Switch 2 title is one
// whose npdm has the Switch 2 layout (eden-nx2's ProgramMetadata::IsSwitch2).

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace ps5eden::exefs
{
	inline std::string Lower(std::string text)
	{
		for (char& c : text)
			c = (char)std::tolower((unsigned char)c);
		return text;
	}

	// A game folder's ExeFS main: "<game>/exefs/main" (exefs/ in any case)
	inline bool IsMain(const std::filesystem::path& main)
	{
		return main.filename() == "main" && main.has_parent_path() && Lower(main.parent_path().filename().string()) == "exefs" &&
			   main.parent_path().has_parent_path() && !main.parent_path().parent_path().filename().empty();
	}

	// The main of a folder that holds its game as an ExeFS (exefs/ with main and main.npdm), else empty
	inline std::filesystem::path MainIn(const std::filesystem::path& folder)
	{
		std::error_code ec;
		for (const auto& part : std::filesystem::directory_iterator(folder, ec))
		{
			std::error_code type;
			if (Lower(part.path().filename().string()) == "exefs" && part.is_directory(type) &&
				std::filesystem::is_regular_file(part.path() / "main", type) && std::filesystem::is_regular_file(part.path() / "main.npdm", type))
				return part.path() / "main";
		}
		return {};
	}

	// What a main.npdm says: the program's ID (its ACI0's), and whether its ACID has the Switch 2 layout,
	// whose signature and key take 0x300 bytes before it where a Switch's take 0x200
	struct Npdm
	{
		uint64_t titleId = 0;
		bool switch2 = false;
	};

	// The npdm beside a game folder's ExeFS main; false when it is not one
	inline bool Read(const std::filesystem::path& main, Npdm& out)
	{
		std::ifstream in(main.parent_path() / "main.npdm", std::ios::binary);
		std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (data.size() < 0x80 || data.size() > (1u << 20) || data.compare(0, 4, "META") != 0)
			return false;
		const auto u32 = [&](size_t at) {
			uint32_t value = 0;
			std::memcpy(&value, data.data() + at, 4);
			return (size_t)value;
		};
		const size_t aci = u32(0x70), acid = u32(0x78);
		const auto magic = [&](size_t at, const char* what) { return at <= data.size() && data.size() - at >= 4 && data.compare(at, 4, what) == 0; };
		if (!magic(aci, "ACI0") || data.size() - aci < 0x18)
			return false;
		if (magic(acid + 0x200, "ACID"))
			out.switch2 = false;
		else if (magic(acid + 0x300, "ACID"))
			out.switch2 = true;
		else
			return false;
		std::memcpy(&out.titleId, data.data() + aci + 0x10, 8);
		return true;
	}
}
