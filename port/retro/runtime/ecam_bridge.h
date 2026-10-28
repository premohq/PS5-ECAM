// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the bundled runtimes. LoadExec may drop argv; consume a bounded request beside them.
#pragma once
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace ecam_bridge
{
	struct Request { std::string rom, returnProgram; bool valid = false; bool library = false; };
	inline Request Parse(const std::vector<std::string>& arguments)
	{
		Request r;
		for (size_t i = 0; i < arguments.size(); i++)
		{
			const auto& key = arguments[i];
			if (key == "--ecam-library") r.library = true;
			if ((key == "--rom" || key == "--return" || key == "--ecam-return") && i + 1 < arguments.size())
			{
				auto& value = key == "--rom" ? r.rom : r.returnProgram;
				value = arguments[++i];
			}
		}
		r.valid = ((r.library && r.rom.empty()) || (!r.library && !r.rom.empty() && r.rom.front() == '/')) && !r.returnProgram.empty() && r.returnProgram.front() == '/' && r.returnProgram.ends_with("/eboot.bin");
		for (const auto& value : {r.rom, r.returnProgram})
			if (value.size() > 4096 || value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos) r.valid = false;
		return r;
	}
	inline Request Read(const std::string& path)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, ec)) || ec) return {};
		const auto size = std::filesystem::file_size(path, ec);
		if (ec || size > 65536) return {};
		std::ifstream in(path, std::ios::binary);
		std::vector<std::string> args; size_t total = 0;
		for (std::string line; std::getline(in, line);)
		{
			if ((total += line.size()) > 65536 || args.size() >= 32) return {};
			args.push_back(std::move(line));
		}
		if (!in.eof()) return {};
		auto request = Parse(args);
		if (request.valid) std::filesystem::remove(path, ec); // never replay an already consumed handoff
		return request;
	}
	inline Request request;
	// Porpoise frees its allocator before LoadExec. Keep the return path in static storage.
	inline const char* ReturnProgram()
	{
		static char path[4097];
		if (!request.valid) return "exit";
		const auto count = std::min(request.returnProgram.size(), sizeof(path) - 1);
		std::copy_n(request.returnProgram.data(), count, path); path[count] = 0;
		return path;
	}
	inline void Init(int argc, char** argv, const char* runtime)
	{
		std::vector<std::string> args;
		for (int i = 0; argv && i < argc && i < 32; i++) if (argv[i]) args.emplace_back(argv[i]);
		request = Parse(args);
		std::vector<std::string> roots{"/app0", "/download0", "/data/homebrew/PPSA99360", "/mnt/ext0/homebrew/PPSA99360", "/mnt/ext1/homebrew/PPSA99360", "/system_ex/app/PPSA99360", "/mnt/sandbox/PPSA99360_000/app0"};
		for (int drive = 0; drive < 8; drive++) roots.push_back("/mnt/usb" + std::to_string(drive) + "/homebrew/PPSA99360");
		for (const auto& root : roots)
		{
			auto recorded = Read(root + "/retro/" + runtime + "/ecam-request.txt");
			if (recorded.valid) { request = std::move(recorded); break; }
		}
		if (request.valid && !request.rom.empty()) setenv("PS5ECAM_ROM", request.rom.c_str(), 1);
	}
}
