// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch.h"
#include "library.h"
#include "../app/lang.h"
#include "../app/paths.h"
#include "../libretro/plays.h"
#include <filesystem>
namespace ps5retro
{
	using ps5lang::Tr;
	using ps5lang::TrF;
	std::vector<std::string> LaunchArguments(int side, const std::string& rom, const std::string& returnProgram)
	{
		if (!IsRetro(side) || rom.empty() || rom.front() != '/' || rom.find('\0') != std::string::npos || rom.find_first_of("\r\n") != std::string::npos || returnProgram.empty() || returnProgram.front() != '/' || !returnProgram.ends_with("/eboot.bin") || returnProgram.find_first_of("\r\n") != std::string::npos || returnProgram.find('\0') != std::string::npos || rom.size() > 4096 || returnProgram.size() > 4096) return {};
		return {"--rom", rom, "--ecam-return", returnProgram};
	}
	bool Available(int side, int backend)
	{
		// a side on its core in the app (port/libretro/game.h); else its program beside the app
		if (backend == 0 && ps5libretro::Plays(side))
			return true;
		std::error_code error;
		return IsRetro(side) && *BackendFor(side, backend).runtime &&
			std::filesystem::is_regular_file(ps5paths::AppDir() + "/" + BackendFor(side, backend).runtime, error);
	}
	// a program to start: none for a side on its core
	static bool HasProgram(int side, int backend) { return IsRetro(side) && *BackendFor(side, backend).runtime && Available(side, backend); }
	std::string Diagnostics(int side, int backend)
	{
		if (!IsRetro(side)) return "Unknown emulator system.";
		const auto b = BackendFor(side, backend);
		// tr: a retro side's emulator: {0} its name, {1} its program in the app's folder, {2} whether this copy has it, {3} its data folder
		return TrF("{0}\nIts program: {1}, {2}\nIts data and logs: {3}\nIts settings and save states are its own: no other emulator reads them.", b.name, b.runtime,
			Available(side, backend) ? Tr("in this copy of PS5-ECAM") : Tr("not in this copy of PS5-ECAM"), b.data);
	}
	bool LaunchLibrary(int side, std::string& error, int backend)
	{
		if (!HasProgram(side, backend)) { error = Tr("This side's emulator is not in this copy of PS5-ECAM."); return false; }
		FinishScans();
		const auto request = (std::filesystem::path(ps5paths::AppDir()) / BackendFor(side, backend).runtime).parent_path() / "ecam-request.txt";
		ps5emu::LoadExecApp(BackendFor(side, backend).runtime, {"--ecam-library", "--ecam-return", ps5paths::Eboot()}, true, request.string());
		error = Tr("The emulator could not be started.");
		return false;
	}

	bool Launch(int side, const std::string& rom, std::string& error, int backend)
	{
		if (!HasProgram(side, backend)) { error = Tr("This side's emulator is not in this copy of PS5-ECAM."); return false; }
		std::error_code ec;
		if (!HasExtension(Of(side).key, rom) || !std::filesystem::is_regular_file(rom, ec)) { error = Tr("This game is no longer where it was, or is not one this side can start."); return false; }
		if (!ValidateRom(side, rom, error, backend)) return false;
		FinishScans();
		const auto arguments = LaunchArguments(side, rom, ps5paths::AppDir() + "/eboot.bin");
		if (arguments.empty()) { error = Tr("This game's folder or name cannot be handed to its emulator: move it or rename it."); return false; }
		const auto request = (std::filesystem::path(ps5paths::AppDir()) / BackendFor(side, backend).runtime).parent_path() / "ecam-request.txt";
		ps5emu::LoadExecApp(BackendFor(side, backend).runtime, arguments, true, request.string());
		error = Tr("The game could not be started.");
		return false;
	}
}
