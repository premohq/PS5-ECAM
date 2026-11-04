// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch.h"
#include "../app/lang.h"
#include "../app/netfs.h"
#include "../app/own_status.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <filesystem>

namespace ps5eglantine
{
	std::string DataFolder() { return "/data/eglantineps5"; }

	bool Available()
	{
		std::error_code error;
		return std::filesystem::is_regular_file(ps5paths::AppDir() + "/eglantine/eboot.bin", error);
	}

	bool KeysFound()
	{
		std::error_code error;
		return std::filesystem::is_regular_file(ps5files::OwnStatus(DataFolder() + "/keys/prod.keys", error)) && !error;
	}

	bool FirmwareFound()
	{
		std::error_code error;
		const std::filesystem::path folder = DataFolder() + "/firmware";
		if (!std::filesystem::is_directory(ps5files::OwnStatus(folder, error)) || error)
			return false;
		return std::filesystem::directory_iterator(folder, error) != std::filesystem::directory_iterator{} && !error;
	}

	void LaunchGame(const ps5emu::Game& game, const ps5settings::Launcher& settings, std::string& error)
	{
		if (!Available())
		{
			error = ps5lang::Tr("This copy has no Switch 2 emulator: eglantine/eboot.bin is missing.");
			return;
		}
		const std::string path = game.path.string();
		if (ps5netfs::is_net(path))
		{
			error = ps5lang::Tr("EglantinePS5 cannot read games on a network share. Copy this game to the PS5 or a USB drive to play it.");
			return;
		}
		if (path.empty() || path.find_first_of("\r\n") != std::string::npos || path.find('\0') != std::string::npos || path.size() > 4096)
		{
			error = ps5lang::Tr("This game's path cannot be handed to the Switch 2 emulator.");
			return;
		}
		const std::string request = "/data/eglantineps5/ps5ecam-launch-request.txt";
		std::error_code directoryError;
		std::filesystem::create_directories("/data/eglantineps5", directoryError);
		if (directoryError)
		{
			error = ps5lang::TrF("EglantinePS5's data folder could not be opened: {0}", directoryError.message());
			return;
		}
		// This is a child runtime, so end its one-game session and return to ECAM after every game.
		const std::vector<std::string> args{
			"--rom", path, "--exit-after-game",
			"--ecam-setting", "resolution=" + std::to_string(settings.nx.nxResolution),
			"--ecam-setting", "filter=" + std::to_string(settings.nx.nxFilter == 1 ? 1 : settings.nx.nxFilter == 2 ? 2 : settings.nx.nxFilter == 3 ? 3 : 0),
			"--ecam-setting", "output=" + std::to_string(settings.nx.nxOutput),
			"--ecam-setting", "refresh=" + std::to_string(settings.nx.nxHighRefresh ? 1 : 0),
			"--ecam-setting", "frame_gen=" + std::to_string(settings.nx.nxFrameGeneration ? 1 : 0),
			"--ecam-setting", "volume=" + std::to_string(settings.nx.volume),
			"--ecam-setting", "hud=" + std::to_string(settings.nx.performance ? 1 : 0),
			"--ecam-setting", "vibration=" + std::to_string(settings.rumble ? 1 : 0),
			"--ecam-setting", "docked=" + std::to_string(settings.nx.nxDocked ? 1 : 0),
			"--ecam-setting", "controller=" + std::to_string(settings.nx.nxController + 1),
			"--ecam-setting", "language=" + std::to_string(settings.nx.nxLanguage + 1),
			"--ecam-setting", "async_shaders=" + std::to_string(settings.nx.nxAsyncShaders ? 1 : 0),
			"--ecam-setting", "fast_gpu=" + std::to_string(settings.nx.nxFastGpu ? 1 : 0),
			"--ecam-setting", "unsafe_cpu=" + std::to_string(settings.nx.nxUnsafeCpu ? 1 : 0),
			"--ecam-setting", "unsafe_dma=" + std::to_string(settings.nx.nxUnsafeDma ? 1 : 0),
			"--ecam-setting", "reactive_flushing=" + std::to_string(settings.nx.nxReactiveFlushing ? 1 : 0),
			"--ecam-setting", "skip_invalidation=" + std::to_string(settings.nx.nxSkipInvalidation ? 1 : 0),
		};
		ps5log::Line("[switch2] starting {} in EglantinePS5 ({})", game.name, path);
		// The child reads the staged request if PS5 LoadExec drops argv. On exit it starts this app's
		// /app0/eboot.bin again; the selected side was saved before this call.
		ps5emu::LoadExecApp("eglantine/eboot.bin", args, true, request);
		error = ps5lang::Tr("The console did not start EglantinePS5. The boot log says what it answered.");
	}
}
