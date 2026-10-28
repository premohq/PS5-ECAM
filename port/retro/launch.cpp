// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch.h"
#include "library.h"
#include "../app/paths.h"
#include <filesystem>
namespace ps5retro
{
	std::vector<std::string> LaunchArguments(int side, const std::string& rom, const std::string& returnProgram)
	{
		if (!IsRetro(side) || rom.empty() || rom.front() != '/' || rom.find('\0') != std::string::npos || rom.find_first_of("\r\n") != std::string::npos || returnProgram.empty() || returnProgram.find_first_of("\r\n") != std::string::npos || returnProgram.find('\0') != std::string::npos || rom.size() > 4096 || returnProgram.size() > 4096) return {};
		return {"--rom", rom, "--ecam-return", returnProgram};
	}
	bool Available(int side)
	{
		std::error_code error;
		return IsRetro(side) && std::filesystem::is_regular_file(ps5paths::AppDir() + "/" + Of(side).runtime, error);
	}
	bool LaunchLibrary(int side, std::string& error)
	{
		if (!Available(side)) { error = "This build does not contain the selected emulator runtime."; return false; }
		FinishScans();
		const auto request = (std::filesystem::path(ps5paths::AppDir()) / Of(side).runtime).parent_path() / "ecam-request.txt";
		ps5emu::LoadExecApp(Of(side).runtime, {"--ecam-library", "--ecam-return", ps5paths::Eboot()}, true, request.string());
		error = "The emulator settings handoff returned without starting its runtime.";
		return false;
	}

	bool Launch(int side, const std::string& rom, std::string& error)
	{
		if (!Available(side)) { error = "This build does not contain the selected emulator runtime."; return false; }
		std::error_code ec;
		if (!HasExtension(Of(side).key, rom) || !std::filesystem::is_regular_file(rom, ec)) { error = "The selected game is not available or has an unsupported extension."; return false; }
		FinishScans();
		const auto arguments = LaunchArguments(side, rom, ps5paths::AppDir() + "/eboot.bin");
		if (arguments.empty()) { error = "The launch paths cannot be represented safely."; return false; }
		const auto request = (std::filesystem::path(ps5paths::AppDir()) / Of(side).runtime).parent_path() / "ecam-request.txt";
		ps5emu::LoadExecApp(Of(side).runtime, arguments, true, request.string());
		error = "The emulator handoff returned without starting its runtime.";
		return false;
	}
}
