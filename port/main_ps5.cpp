// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the entry point (Cemu's src/main.cpp on the desktop).
//
//  1. out of the sandbox: /data, and JIT memory for the recompilers (ps5/privilege.h);
//  2. the boot log, the DualSense and Cemu's core (settings, MLC, graphic packs, the game scan);
//  3. the launcher until a game is chosen (frontend/shell.h), on the side last used;
//  4. the game, on Cemu's or Azahar's Vulkan renderer, or melonDS's screens on VideoOut, with its side's
//     settings and the game's own over them (frontend/settings.h, ForGame), until the in-game menu
//     (touchpad + Options) asks for the library: a 3DS or DS game's emulator shuts down and the
//     launcher shows again in this process; a Wii U game starts PS5-ECAM over (app/emulator.h,
//     RestartToLibrary), as Cemu cannot let VideoOut go. A Switch game runs in Eden's own program
//     instead (eden/eboot.bin, port/eden), which starts PS5-ECAM again on the Switch side when it ends.

#include "app/achievements.h"
#include "app/catalog.h"
#include "app/emulator.h"
#include "app/file_modes.h"
#include "app/forwarders.h"
#include "app/game_archives.h"
#include "app/lang.h"
#include "app/mods.h"
#include "app/netfs.h"
#include "app/pack_updates.h"
#include "app/paths.h"
#include "app/reorganize.h"
#include "app/updates.h"
#include "azahar/azahar.h"
#include "azahar/library.h"
#include "eden/eden.h"
#include "eden/library.h"
#include "esde/esde.h"
#include "frontend/launcher.h"
#include "retro/library.h"
#include "retro/launch.h"
#include "libretro/game.h"
#include "melonds/library.h"
#include "melonds/melonds.h"
#include "frontend/settings.h"
#include "frontend/shell.h"
#include "ps5/crash.h"
#include "ps5/display.h"
#include "ps5/driver_clock.h"
#include "ps5/kernel.h"
#include "ps5/log.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/privilege.h"
#include "ps5/profiler.h"
#include "ps5/threads.h"
#include "ps5/window.h"
#include "stream/stream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Cemu's recompiler: the exact stfs conversion, or cvtsd2ss (Cemu patch 0024)
extern bool g_ps5AccurateSingleStores;
// patches/cemu: the GPU's time on Cemu's command buffers (VulkanRenderer.cpp), for profiling runs
extern bool g_ps5GpuTimestamps;
// patches/cemu: Cemu's samplers without custom border colours (VulkanRenderer.cpp)
extern bool g_ps5StandardBorderColors;
// ps5/heap_wrap.c: the title heap's per-thread cache of small blocks (weak: a link check has no heap)
extern "C" __attribute__((weak)) int g_ps5HeapThreadCache;
// patches/cemu: Cemu's millisecond clock on the TSC (Common/unix/platform.cpp)
void PS5Cemu_UseTscTickCount(bool enabled);
// patches/cemu: Cemu's other host clocks on the TSC (Common/unix/platform.cpp)
void PS5Cemu_UseTscClocks(bool enabled);
// patches/cemu: an idle emulated core's spin before it sleeps, in PAUSEs (coreinit_Thread.cpp), and
// the least time between the main core's idle looks at the system events, in TSC ticks (0037)
namespace coreinit
{
	extern uint32_t g_ps5IdleSpinIterations;
	extern uint64_t g_ps5IdleEventTicks;
}
// patches/cemu 0037: how long a thread retries the taken scheduler lock before it sleeps, in PAUSEs
// (coreinit_Scheduler.cpp)
extern uint32_t g_ps5SchedulerSpins;
// patches/compiler-rt: thread_local reads from a per-thread cache (weak: builtins without the patch
// have none)
extern "C" __attribute__((weak)) int __ps5_emutls_fast;
extern "C" uint64_t sceKernelGetTscFrequency();

// The PS4 SDK's version record: its size is set by the caller (libkernel)
struct SceKernelSwVersion
{
	size_t size;
	char text[0x1c];
	uint32_t version;
};
extern "C" int32_t sceKernelGetSystemSwVersion(SceKernelSwVersion* version);

namespace
{
	// The session before this one's first crash report (ps5/crash.h, Cemu's handler: boot.prev.log's
	// first "[crash] " line, without it), or empty when it ended without one
	std::string LastCrash()
	{
		std::ifstream log(ps5paths::Logs() + "/boot.prev.log");
		std::string line;
		while (std::getline(log, line))
			if (line.rfind("[crash] ", 0) == 0)
				return line.substr(8, 160);
		return {};
	}

	// The console's firmware, as its settings show it ("11.60"), for the boot log and Diagnostics:
	// most reports hinge on it
	std::string Firmware()
	{
		SceKernelSwVersion version{};
		version.size = sizeof(version);
		if (sceKernelGetSystemSwVersion(&version) != 0)
			return "unknown";
		version.text[sizeof(version.text) - 1] = 0;
		return fmt::format("{} ({:#010x})", version.text, version.version);
	}

	// A diagnostic for SYSTEM_ILLEGAL_FUNCTION_CALL (0xA002030A), which ends the app with no signal for
	// a crash handler: every import as the console resolved it. With got-range.txt in the data folder
	// ("SELF GOT_START GOT_END", this build's ELF addresses of this function and of .got, in hex), the
	// .got as loaded goes to logs/got.bin, to be read against the ELF's relocations: an import the
	// console does not give the app points where the others it lacks do. Nothing without the file.
	void DumpImports()
	{
		FILE* range = std::fopen((ps5paths::Root() + "/got-range.txt").c_str(), "rb");
		if (!range)
			return;
		char text[128] = {};
		(void)!std::fread(text, 1, sizeof(text) - 1, range);
		std::fclose(range);
		char* at = text;
		const uint64_t self = std::strtoull(at, &at, 16), start = std::strtoull(at, &at, 16), end = std::strtoull(at, &at, 16);
		if (!self || end <= start || end - start > (1u << 20))
			return;
		const uintptr_t base = (uintptr_t)&DumpImports - (uintptr_t)self;
		FILE* out = std::fopen((ps5paths::Logs() + "/got.bin").c_str(), "wb");
		if (!out)
			return;
		std::fwrite((const void*)(uintptr_t)(start + base), 1, (size_t)(end - start), out);
		std::fclose(out);
		ps5log::Line("[diag] imports: {} slots in logs/got.bin, the eboot {:#x} above its ELF", (end - start) / 8, base);
	}

	std::vector<std::string> Diagnostics(const ps5privilege::Result& privileges)
	{
		return {
			fmt::format("PS5-ECAM {}: Cemu at {}, Azahar at {}, melonDS at {}, Eden at {}", PS5CEMU_VERSION, PS5CEMU_CEMU_COMMIT,
				PS5CEMU_AZAHAR_COMMIT, PS5CEMU_MELONDS_COMMIT, PS5CEMU_EDEN_COMMIT),
			ps5lang::TrF("Firmware {0}", Firmware()),
			privileges.summary,
			ps5log::Path()[0] ? ps5lang::TrF("Logs: {0}, {1}/log.txt", ps5log::Path(), ps5paths::CemuRoot()) :
								ps5lang::Tr("Boot log not written (/data is unreachable)"),
		};
	}

	ps5emu::Options Options(const ps5settings::Launcher& settings)
	{
		return {settings.gamesFolder, settings.overlay, settings.volume, settings.upscaleFilter, settings.asyncShaders, settings.gamePadSpeaker,
			settings.shaderNotices, {settings.touchpadClick[0], settings.touchpadClick[1], settings.touchpadClick[2], settings.touchpadClick[3]}};
	}

	// The side the launcher opens on when the app starts over (and only then), and why the game did
	// not start, when there is a reason.
	void RememberSide(const char* side, const std::string& launchError = {})
	{
		// the game's menu may have saved settings since the launcher's were read
		ps5settings::Launcher settings = ps5settings::Load();
		settings.side = side;
		settings.launchError = launchError;
		ps5settings::Save(settings);
	}

	// The 119.88 Hz mode for the next game, where the setting asks for it. The driver configures
	// VideoOut once a process, when a renderer first asks for the display's modes, and offers 119.88
	// Hz only to a title whose param.json declares high-frame-rate output. It reads /app0's unless
	// PS5_VIDEOOUT_PARAM_JSON names another, and a process the HEN has jailbroken has no /app0
	// (app/paths.h), so it is told where the app's is; with the setting off it is told of one that
	// is not there, which declares nothing, so the display stays at 59.94 Hz in a sandboxed process too.
	void SetHighFrameRate(bool highFrameRate)
	{
		ps5display::SetHighFrameRate(highFrameRate);
		const std::string paramJson = ps5paths::AppDir() + (highFrameRate ? "/sce_sys/param.json" : "/sce_sys/no-high-frame-rate.json");
		setenv("PS5_VIDEOOUT_PARAM_JSON", paramJson.c_str(), 1);
		// the new launcher opened VideoOut through the driver at 59.94 Hz: the game's rate asked for again
		ps5display::ConfigureOutput(highFrameRate);
	}

	// A home screen tile's arguments (app/forwarders.h, the PS5 Forwarder Format's): --rom <path> names
	// the game to start at once, a full path or a file's name in its side's library, and
	// --exit-after-game closes the app after it instead of showing the library. --resume, the app's own
	// (a game handed to a fresh process: main), has it go on from its last quit's state
	struct TileGame
	{
		std::string rom;
		bool exitAfterGame = false;
		bool resume = false;
		// --frontend es-de: the game is EmulationStation's, which it goes back to (port/esde/esde.h);
		// --own-launcher: EmulationStation was quit, for this launcher, whatever the frontend is
		bool frontend = false;
		bool ownLauncher = false;
		ps5launcher::System side = ps5launcher::System::WiiU;
	};

	TileGame ReadArguments(int argc, char* argv[], const ps5settings::Launcher& settings)
	{
		TileGame tile;
		for (int i = 0; i < argc; i++)
		{
			const std::string arg = argv[i] ? argv[i] : "";
			if (arg == "--rom" && i + 1 < argc && argv[i + 1])
				tile.rom = ps5paths::Moved(argv[++i]); // a tile made before its side had a folder of its own (app/reorganize.h)
			else if (arg == "--exit-after-game")
				tile.exitAfterGame = true;
			else if (arg == "--resume")
				tile.resume = true;
			else if (arg == "--frontend" && i + 1 < argc && argv[i + 1])
				tile.frontend = std::string(argv[++i]) == "es-de";
			else if (arg == "--own-launcher")
				tile.ownLauncher = true;
		}
		// EmulationStation's game (port/esde/esde.h): taken, or started from what it wrote when LoadExec
		// dropped the arguments it was given
		if (tile.frontend || tile.rom.empty())
			if (std::string requested = ps5esde::TakeRequest(); !requested.empty() && tile.rom.empty())
			{
				tile.rom = requested;
				tile.frontend = true;
			}
		// the side by the file's type: the DS's, the 3DS's and the Switch's are their own, the rest the Wii
		// U's (.wua, .wud, .wux, .rpx, a folder)
		std::string extension = std::filesystem::path(tile.rom).extension().string();
		for (char& c : extension)
			c = (char)std::tolower((unsigned char)c);
		for (const char* ds : {".nds", ".srl", ".dsi"})
			if (extension == ds)
				tile.side = ps5launcher::System::Nds;
		for (const char* n3ds : {".3ds", ".cci", ".cxi", ".cia", ".3dsx", ".app", ".elf", ".axf", ".z3ds", ".zcci", ".zcxi", ".z3dsx", ".zcia"})
			if (extension == n3ds)
				tile.side = ps5launcher::System::N3ds;
		for (const char* nx : {".nsp", ".xci"})
			if (extension == nx)
				tile.side = ps5launcher::System::Switch;
		// a game folder's ExeFS main (a Switch 2 title, eden/library.h)
		if (ps5eden::IsExeFsMain(tile.rom))
			tile.side = ps5launcher::System::Switch;
		// the other sides' games, by their file type (a GameCube or Wii disc by its header)
		if (const int retro = ps5retro::IdentifySystem(tile.rom); ps5retro::IsRetro(retro))
			tile.side = (ps5launcher::System)retro;
		// an archive's game is any side's (app/game_archives.h): the side whose library listed it last (the
		// catalogue), else the side whose game files folder holds it (the deepest of those that do), else
		// the DS side's, whose tiles were its only ones
		if (ps5archive::IsArchive(tile.rom))
		{
			for (int index = 0; index < ps5launcher::kSideCount; ++index)
			{
				const auto side = (ps5catalog::System)index;
				for (const ps5catalog::Entry& entry : ps5catalog::Games(side))
					if (entry.game.path.string() == tile.rom)
					{
						tile.side = (ps5launcher::System)side;
						return tile;
					}
			}
			tile.side = ps5launcher::System::Nds;
			size_t deepest = 0;
			for (const auto& [side, setting] : {std::pair{ps5launcher::System::WiiU, &settings.gamesFolder},
					 std::pair{ps5launcher::System::N3ds, &settings.n3ds.gamesFolder}, std::pair{ps5launcher::System::Nds, &settings.nds.gamesFolder}})
			{
				std::string folder = *setting;
				while (folder.size() > 1 && folder.back() == '/')
					folder.pop_back();
				if (!folder.empty() && folder.size() > deepest && tile.rom.compare(0, folder.size() + 1, folder + "/") == 0)
				{
					tile.side = side;
					deepest = folder.size();
				}
			}
		}
		return tile;
	}

	// Whether the game chosen is the tile's (by its path, or its file's name)
	bool IsTileGame(const TileGame& tile, const ps5emu::Game& game)
	{
		if (tile.rom.empty())
			return false;
		return tile.rom.find('/') == std::string::npos ? game.path.filename().string() == tile.rom : game.path.string() == tile.rom;
	}
}

// What Cemu's src/main.cpp defines for the rest of Cemu.
std::atomic_bool g_isGPUInitFinished = false;
// Cemu's command line asks for a console window with some options; there is none on the PS5.
void requireConsole() {}

int main(int argc, char* argv[])
{
	// No umask(0) here: on the PS5 it is a raw system call in the app's own code (the SDK has no import
	// for it), which the console answers by ending the app before anything is shown (CE-108255-1). What
	// the app makes is opened to every user by the files' walk at each start instead (app/file_modes.h,
	// #55, #58).
	ps5log::Line("[main] PS5-ECAM {} starting (Cemu at {}, Azahar at {}, melonDS at {}, Eden at {})", PS5CEMU_VERSION, PS5CEMU_CEMU_COMMIT,
		PS5CEMU_AZAHAR_COMMIT, PS5CEMU_MELONDS_COMMIT, PS5CEMU_EDEN_COMMIT);
	// before any thread starts: the HEN jailbreaks the process as it is. The game folders are read
	// first, so a HEN is asked only when something the app needs is out of reach without one
	// (ps5cemu.json as it is: the defaults when /data is not open yet)
	const ps5settings::Launcher early = ps5settings::Load();
	const uint64_t acquiring = sceKernelGetProcessTime();
	const ps5privilege::Result privileges =
		ps5privilege::Acquire({early.gamesFolder, early.n3ds.gamesFolder, early.nds.gamesFolder}, early.alwaysAskHen);
	const uint64_t acquired = sceKernelGetProcessTime();
	ps5reorganize::Outcome reorganized;
	if (privileges.filesystem)
	{
		// the drives the HEN left out opened first (below for the game folders the settings name):
		// the data folder may be on the one the app is installed on (app/paths.h, #52), and the boot
		// log goes in it
		ps5privilege::ReachFolders({early.gamesFolder, early.n3ds.gamesFolder, early.nds.gamesFolder});
		ps5paths::DecideRoot();
		// a folder for each system, asked for at the last start (or left unfinished by a power cut):
		// moved before any system's folder is decided or a file of theirs opened (the log's rotation
		// opens Cemu's log.txt), and said in the log once it is open
		std::vector<std::string> reorganizing;
		reorganized = ps5reorganize::Run(ps5paths::Root(), ps5paths::kDefaultRoot, reorganizing);
		ps5paths::DecideSystemRoots();
		ps5log::Open(ps5paths::Logs().c_str());
		for (const std::string& line : reorganizing)
			ps5log::Line("[reorganize] {}", line);
	}
	ps5log::Line("[main] firmware {}", Firmware());
	// (how long the HEN, the probes and the elevation helper took: a start's first wait)
	ps5log::Line("[main] {} ({} ms)", privileges.summary, (acquired - acquiring) / 1000);
	if (privileges.filesystem)
		DumpImports();
	ps5crash::Install(); // Cemu's own replaces it, in a session Cemu runs in
	{
		// how the console starts the CPU's floating point: desktop systems keep denormals (0x1f80);
		// flush-to-zero (bit 15) or denormals-are-zero (bit 6) would make Cemu's and Azahar's float
		// results differ from a desktop's
		uint32_t mxcsr = 0;
		asm volatile("stmxcsr %0" : "=m"(mxcsr));
		ps5log::Line("[main] MXCSR {:#06x}: flush-to-zero {}, denormals-are-zero {}", mxcsr, mxcsr & 0x8000 ? "on" : "off",
			mxcsr & 0x40 ? "on" : "off");
	}
	ps5threads::Initialize(); // before any thread starts: they inherit the main thread's CPUs

	ps5settings::Launcher settings = ps5settings::Load();
	// the update notice, on a fresh start only (not each time a game hands back to the library)
	const bool freshStart = settings.side.empty();
	const TileGame tile = ReadArguments(argc, argv, settings);
	if (!tile.rom.empty())
		ps5log::Line("[main] a home screen tile: --rom {}{} ({} side)", tile.rom, tile.exitAfterGame ? " --exit-after-game" : "",
			ps5launcher::SideTitle(tile.side));
	// still before any thread: the game folders and drives the HEN may have left out (#16)
	if (privileges.filesystem)
		ps5privilege::ReachFolders({settings.gamesFolder, settings.n3ds.gamesFolder, settings.nds.gamesFolder});
	// and the files another user made in /data/ps5cemu that a walk could not open (app/file_modes.h,
	// #55, #56): root for this session, so its walk opens them. On a fresh start only: a game starts
	// the app over
	if (privileges.filesystem && freshStart && ps5filemodes::ClosedLeft())
		ps5privilege::ReachClosedFiles();
	// the experiment that gives the Wii U's emulated CPU cores and GPU thread cores of their own: once
	// the elevation helper is done (its timing starts threads) and before any thread that should
	// inherit the other CPUs
	ps5threads::SetIsolation(settings.isolateCores);
	// the first thread but main: the boot log's writer, after the elevation helper is done with the
	// process (it must find it single-threaded)
	if (privileges.filesystem)
	{
		for (const std::string& directory : ps5paths::SystemDirectories())
		{
			std::error_code ec;
			std::filesystem::create_directories(directory, ec);
			if (ec) ps5log::Line("[paths] could not create {}: {}", directory, ec.message());
		}
		// Only a comment, never a supplied key. Do not create an empty 3DS aes_keys.txt: that
		// would override Azahar's built-in keys. DS BIOS and Switch keys are supplied by the user.
		if (FILE* keys = std::fopen(ps5paths::DiscKeys().c_str(), "wx"))
		{
			std::fputs("# Add your Wii U disc keys here, one per line.\n", keys);
			std::fclose(keys);
		}
		ps5log::Line("[paths] Wii U: {}; 3DS: {}; DS: {}; Switch: {}", ps5paths::CemuRoot(), ps5paths::Azahar(), ps5paths::MelonDs(), ps5paths::Eden());
	}
	ps5log::StartWriter();
	{
		// the app's folder decided here, the process's root now final (app/paths.h, #26)
		bool image = false;
		const std::string source = ps5paths::MountSource(&image);
		ps5log::Line("[main] app folder {}{}", ps5paths::AppDir(),
			!source.empty() ? fmt::format(" (mounted from {})", source) : image ? " (mounted from an image)" : "");
		ps5log::Line("[main] data folder {}{}", ps5paths::Root(),
			ps5paths::Root() != ps5paths::kDefaultRoot ? " (on the drive the app is installed on)" : "");
	}
	// the menus' language, from the app's assets (so only once its folder is decided), before
	// anything says a word: the PS5's, unless Settings names another
	ps5lang::Load(settings.ui.language);
	// a RetroAchievements account signed in from Settings > Online (6.16): the file alone, no network
	ps5cheevos::Load();
	{
		// the network shares (6.14), as /net/<name>: connected only when a file on one is first read
		std::vector<ps5netfs::Share> shares;
		if (ps5netfs::load(ps5paths::Root() + "/network-shares.txt", shares) && !shares.empty())
		{
			ps5netfs::set_shares(shares);
			ps5log::Line("[netfs] {} network share(s)", shares.size());
		}
	}
	ps5threads::SetPinning(settings.pinCpuThreads);
	// the driver's messages into the boot log (a thread: after the elevation helper too)
	ps5log::ForwardDriverMessages();
	ps5threads::SetCompileBelowNormal(settings.compileBelowNormal);
	// before a game starts: about 20 ns a PAUSE on the console's CPU
	coreinit::g_ps5IdleSpinIterations = (uint32_t)settings.idleSpinUs * 50;
	if (settings.idleSpinUs)
		ps5log::Line("[threads] an idle emulated core spins {} us before it sleeps (idleSpinUs in ps5cemu.json)", settings.idleSpinUs);
	// thread_local reads from a per-thread cache instead of a libkernel call each (fastTls)
	if (&__ps5_emutls_fast)
	{
		__ps5_emutls_fast = settings.fastTls ? 1 : 0;
		ps5log::Line("[threads] thread_local reads: {} (fastTls in ps5cemu.json)",
			settings.fastTls ? "a per-thread cache" : "pthread_getspecific each time");
	}
	// the scheduler lock retried a while before a thread sleeps on it in the kernel (schedulerSpinUs),
	// and the main core's idle loop looking at the system events at most so often (idleEventsUs)
	g_ps5SchedulerSpins = (uint32_t)settings.schedulerSpinUs * 50;
	coreinit::g_ps5IdleEventTicks = (uint64_t)settings.idleEventsUs * (sceKernelGetTscFrequency() / 1000000);
	ps5log::Line("[threads] Cemu's scheduler lock: {} (schedulerSpinUs); the main core's idle loop looks at the system events "
				 "{} (idleEventsUs)",
		settings.schedulerSpinUs ? fmt::format("retried {} us before a thread sleeps on it", settings.schedulerSpinUs) :
								   std::string("a thread sleeps on it at once"),
		settings.idleEventsUs ? fmt::format("every {} us at most", settings.idleEventsUs) : std::string("at every turn"));
	if (settings.profile && privileges.filesystem)
		ps5profile::Start();
	// before Cemu's renderer starts: with the profiler, the [perf] lines say how busy the GPU is too
	g_ps5GpuTimestamps = settings.profile;
	g_ps5StandardBorderColors = settings.standardBorderColors;
	if (settings.standardBorderColors)
		ps5log::Line("[vulkan] Cemu's samplers: standard border colours only (standardBorderColors in ps5cemu.json)");
	// before Cemu starts: its millisecond clock, taken at every draw call
	PS5Cemu_UseTscTickCount(settings.tscTickCount);
	ps5log::Line("[clock] Cemu's millisecond clock: {} (tscTickCount in ps5cemu.json)", settings.tscTickCount ? "the TSC" : "clock_gettime");
	// and its other clocks: the emulated vsync's timer and the audio output's, which the main emulated
	// core reads at every switch between the game's threads (tscClocks), once the TSC's reported rate
	// has checked out: every timer on it runs at that rate
	const bool tscClocks = settings.tscClocks && ps5clock::TscAgrees();
	PS5Cemu_UseTscClocks(tscClocks);
	ps5log::Line("[clock] Cemu's timers: {} (tscClocks in ps5cemu.json)", tscClocks ? "the TSC" : "clock_gettime");
	// before either emulator's Vulkan driver starts: the driver's clock without a system call a read
	// (driverClockTsc in ps5cemu.json)
	if (settings.driverClockTsc)
		ps5clock::UseTscForDriver();
	else
		ps5log::Line("[vulkan] the driver's clock is clock_gettime (driverClockTsc in ps5cemu.json)");
	// the driver's submissions on a thread of its own, off the GPU thread that records them
	// (asyncSubmit; patches/mesa 0008), and how long its waits for the GPU sleep between looks
	// (gpuWaitSleepUs; 0009). Read once, as the driver makes its first submission or wait
	if (settings.asyncSubmit)
		setenv("RADV_PS5_SUBMIT_THREAD", "1", 1);
	if (settings.gpuWaitSleepUs > 0)
		setenv("RADV_PS5_WAIT_SLEEP_US", std::to_string(settings.gpuWaitSleepUs).c_str(), 1);
	ps5log::Line("[vulkan] submissions: {} (asyncSubmit in ps5cemu.json); waits for the GPU sleep {} us between looks "
				 "(gpuWaitSleepUs)", settings.asyncSubmit ? "on the driver's own thread" : "on the thread that records them",
		settings.gpuWaitSleepUs > 0 ? settings.gpuWaitSleepUs : 1000);
	// before Cemu recompiles any of a game's code
	// before the game's threads start: they keep their own lists of small blocks
	if (&g_ps5HeapThreadCache)
	{
		g_ps5HeapThreadCache = settings.heapThreadCache;
		if (settings.heapThreadCache)
			ps5log::Line("[heap] small blocks cached per thread (heapThreadCache in ps5cemu.json)");
	}
	g_ps5AccurateSingleStores = settings.accurateSingleStores;
	if (settings.accurateSingleStores)
		ps5log::Line("[emu] single-precision stores: Cemu's exact conversion (accurateSingleStores in ps5cemu.json)");
	// before either emulator's Vulkan driver starts, which reads them once. RADV keeps its shader
	// cache in /app0 unless told otherwise, and a jailbroken process has none: told here, so the 3DS's
	// side has it too (it was set as Cemu started, which a 3DS game's process never does)
	setenv("MESA_SHADER_CACHE_DIR", ps5paths::RadvCache().c_str(), 1);
	// The driver keeps the shaders it compiles for the next start, as on a PC (shaderDiskCache in
	// ps5cemu.json, on by default). Mesa keeps no disk cache in a process whose effective user or
	// group is not its real one, and the HEN makes only the effective ones root: the real ones are
	// made the effective ones.
	if (!settings.shaderDiskCache)
		setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
	else if (geteuid() != getuid() || getegid() != getgid())
	{
		const int user = setreuid(geteuid(), geteuid()), group = setregid(getegid(), getegid());
		ps5log::Line("[vulkan] RADV's shader cache: real user {} (euid {}){}, real group {} (egid {}){}", getuid(), geteuid(),
			user ? " (refused)" : "", getgid(), getegid(), group ? " (refused)" : "");
	}
	ps5log::Line("[vulkan] RADV's shader cache in {}: {}", ps5paths::RadvCache(),
		!settings.shaderDiskCache ? "off (shaderDiskCache in ps5cemu.json)" :
		geteuid() == getuid() && getegid() == getgid() ? "on" : "off (the console kept the real user)");
	// before either emulator's Vulkan driver starts, which reads it once
	if (!settings.radvDebug.empty())
	{
		setenv("RADV_DEBUG", settings.radvDebug.c_str(), 1);
		ps5log::Line("[vulkan] RADV_DEBUG={} (radvDebug in ps5cemu.json)", settings.radvDebug);
	}
	for (const auto& [name, value] : settings.radvEnvironment)
	{
		// the driver's own variables only: the rest of the environment is the app's
		const bool driver = name.rfind("RADV_", 0) == 0 || name.rfind("MESA_", 0) == 0 || name.rfind("ACO_", 0) == 0;
		if (!driver || name == "RADV_DEBUG")
		{
			ps5log::Line("[vulkan] {} in radvEnvironment left out: only RADV_, MESA_ and ACO_ names, and radvDebug for RADV_DEBUG", name);
			continue;
		}
		setenv(name.c_str(), value.c_str(), 1);
		ps5log::Line("[vulkan] {}={} (radvEnvironment in ps5cemu.json)", name, value);
	}
	ps5emu::SetSubmitDraws(settings.cemuSubmitDraws);
	ps5emu::SetFramesInFlight(settings.framesInFlight);
	ps5pad::Init();
	ps5pad::SetVibrationEnabled(settings.rumble);
	ps5window::Initialize();
	// the remote touch screen, when it is on: the app's, so it runs from here through the game. Its
	// files page sends to each side's games folder as the settings have it at the time, Eden's keys
	// and firmware, amiibo and the launcher's music; never to a network share (nothing is written there)
	ps5stream::SetUploadFolders([] {
		const ps5settings::Launcher now = ps5settings::Load();
		const std::string eden = ps5eden::GameFilesFolder();
		std::vector<ps5stream::Folder> folders = {
			{"wiiu", "Wii U games", "WUA, WUD and WUX files, or an unpacked game in a .zip, .7z or .rar.", now.gamesFolder},
			{"3ds", "3DS games",
				"Decrypted .3ds, .cci, .cxi and .3dsx files, alone or in an archive. A CIA sent here is installed from Settings > Game "
				"files > Install a CIA file.",
				now.n3ds.gamesFolder},
			{"ds", "DS games", ".nds files, alone or in a .zip, .7z or .rar.", now.nds.gamesFolder},
			{"switch", "Switch games", ".nsp and .xci files: a game, its update or its DLC.", ps5eden::ScanFolder(now.nx.gamesFolder)},
			{"switch-keys", "Switch keys", "prod.keys and title.keys, dumped from your own Switch.", eden + "/keys"},
			{"switch-firmware", "Switch firmware", "The firmware's .nca files, dumped from your own Switch.", eden + "/firmware"},
			{"amiibo", "Amiibo", "Your amiibo dumps (.bin), for the Wii U and the 3DS.", ps5paths::Root() + "/amiibo"},
			{"3ds-camera", "3DS camera",
				"A picture (PNG or JPEG) the 3DS's cameras see, the newest sent: a QR code for a game to scan.",
				ps5paths::Azahar() + "/camera"},
			{"music", "Launcher music", "MP3 and WAV files, for Settings > System > Launcher sound.", ps5paths::Root() + "/music"},
		};
		std::erase_if(folders, [](const ps5stream::Folder& folder) { return folder.path.empty() || ps5netfs::is_net(folder.path); });
		return folders;
	});
	ps5stream::Configure(settings.remoteScreen);

	ps5launcher::Status status;
	status.diagnostics = Diagnostics(ps5privilege::Current());
	status.lastCrash = LastCrash();
	if (!status.lastCrash.empty())
		ps5log::Line("[main] the last session ended in a crash: {}", status.lastCrash);
	status.autoLaunch = tile.rom;
	status.autoLaunchSide = tile.side;
	status.autoLaunchResume = tile.resume;
	status.autoLaunchFrontend = tile.frontend && !tile.rom.empty();
	status.reorganized = reorganized;
	// after the game: the library, or for a tile's game (--exit-after-game) the PS5's home screen, as
	// the in-game menus say (SetExitAfterGame, below)
	auto afterGame = [&](const char* side) {
		if (ps5esde::GameFromFrontend())
			ps5esde::BackToFrontend(ps5settings::Load()); // EmulationStation's game: back to it
		if (ps5emu::ExitAfterGame())
			ps5emu::ExitApp();
		RememberSide(side);
		ps5emu::RestartToLibrary();
	};
	// A 3DS or DS game's emulator, shut down whole, gives way to the launcher in this process: no
	// restart, which the PS5 has answered by closing the app instead (#60, #68). What the in-game menu
	// saved is read again; the launcher opens on the game's side.
	std::optional<ps5launcher::System> gameRan; // the emulator whose game ran in this process
	// A game that ended: the minutes since the launcher started it (ps5catalog::Played) added to its play
	// time, which the library and the game's page show
	auto countPlayTime = [](ps5catalog::System side, uint64_t titleId, const std::string& name, int64_t ended) {
		const uint32_t minutes = ps5catalog::Ended(side, titleId, ended);
		ps5catalog::Save();
		ps5log::Line("[main] {} played for {} min", name, minutes);
	};
	auto backInProcess = [&](ps5launcher::System system) {
		if (ps5esde::GameFromFrontend())
			ps5esde::BackToFrontend(ps5settings::Load()); // EmulationStation's game: back to it
		if (ps5emu::ExitAfterGame())
			ps5emu::ExitApp();
		settings = ps5settings::Load();
		settings.side = ps5launcher::SideName(system);
		ps5pad::SetTurbo(0); // the game's turbo button is not the launcher's
		gameRan = system;
		ps5log::Line("[main] back to the library in this process, on the {} side", ps5launcher::SideTitle(system));
	};
	std::string error;
	if (!privileges.filesystem)
	{
		status.notice = status.notice3ds = status.noticeDs = status.noticeSwitch =
			ps5lang::Tr("PS5-ECAM cannot reach /data. Load a HEN with PPSA99360 in its app jailbreak list, or elfldr, then restart PS5-ECAM.");
		ps5log::Line("[main] {}", status.notice);
		ps5notify::Send(status.notice);
	}
	else if (!settings.launchError.empty())
	{
		// the last game failed after its renderer started, and the process was started over to show it
		(settings.side == "3ds"		 ? status.launchError3ds :
				settings.side == "ds"	 ? status.launchErrorDs :
				settings.side == "switch" ? status.launchErrorSwitch :
										   status.launchError) = settings.launchError;
		settings.launchError.clear();
		ps5settings::Save(settings);
	}
	// whether the last way back to the library is what started the app: one that took it to the home
	// screen instead is left out from now on (#68)
	if (privileges.filesystem)
		ps5emu::TakeRestartRecord();
	// the Switch side's last game, when Eden never took what it was started with (port/eden/eden.h):
	// why, said once on the Switch side, and Eden's trace in the boot log when it got that far
	if (privileges.filesystem)
		if (std::string why = ps5eden::TakeStaleRequest(); !why.empty())
			status.launchErrorSwitch = why;
	// what the Switch side's last game came to in Eden's program (port/eden/eden.h): why it did not start
	// or stopped, said once on the Switch side, never in ProsperoEden's launcher; and what its in-game
	// menu changed, kept as the other sides' menus keep theirs
	if (privileges.filesystem)
		if (const ps5eden::Result result = ps5eden::TakeResult(); result.found)
		{
			if (std::string why = ps5eden::ResultMessage(result); !why.empty())
				status.launchErrorSwitch = why;
			// to when the game ended or was being left; a crash leaves "running", written as it started
			if ((result.state == "ended" || result.state == "leaving") && result.written > 0)
				if (const ps5catalog::Entry* entry = ps5catalog::Find(ps5catalog::System::Switch, result.titleId))
					countPlayTime(ps5catalog::System::Switch, result.titleId, entry->game.name, result.written);
			if (result.changed)
			{
				ps5eden::KeepMenuChanges(result);
				const ps5settings::Launcher kept = ps5settings::Load();
				settings.nx = kept.nx;
				settings.games = kept.games;
				settings.rumble = kept.rumble;
				ps5pad::SetVibrationEnabled(settings.rumble);
			}
		}

	// EmulationStation (port/esde/esde.h): the app started over after a game it started (the Wii U's, the
	// Switch's and the other sides' programs come back so), which goes back to it with why it stopped,
	// when it did; or the frontend this start opens, unless L1 is held or it was just quit
	if (privileges.filesystem && tile.rom.empty())
	{
		if (ps5esde::GameFromFrontend())
		{
			std::string why = status.lastCrash;
			for (const std::string* error : {&status.launchError, &status.launchError3ds, &status.launchErrorDs, &status.launchErrorSwitch})
				if (why.empty() && !error->empty())
					why = *error;
			ps5esde::BackToFrontend(settings, why);
		}
		else if (settings.ui.emulationStation && !tile.ownLauncher && ps5esde::Available())
		{
			if (ps5esde::OwnLauncherHeld())
				ps5log::Line("[main] L1 held: the launcher, not EmulationStation");
			else
				ps5esde::Start(settings);
		}
		// here when ES-DE did not start: the launcher, as without it
	}

	// One emulator per session: none starts until a side opens (the side last used, or the chooser's).
	// Cemu's core (its guest memory, system threads, crash handler, graphic packs and game scan) runs
	// only for the Wii U; Azahar (its game scan, and its core once a game starts) only for the 3DS;
	// melonDS (its game scan, and its core once a game starts) only for the DS.
	// Each time the launcher moves to a side (a game that did not start brings the launcher back).
	// Each side gives way to another in the same process: the emulators' cores start only for a game,
	// so until then only their game lists and settings are loaded.
	std::optional<ps5launcher::System> started;
	const size_t sessionLine = status.diagnostics.size();
	// A games folder on a network share is not looked through each time its side opens unless Settings >
	// Game files asks (#75): with hundreds of games that is a long wait, at every start (and the app
	// starts again after every game). The launcher shows the side's list from its last look, until Look
	// for games now. A first look, and one in a folder the list has no game from, happen anyway.
	auto keepList = [&](ps5launcher::System system, const std::string& folder) {
		bool keep = false;
		if (ps5netfs::is_net(folder) && !settings.lookOnShares)
			for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)system))
				if (entry.game.path.string().rfind(folder, 0) == 0)
				{
					keep = true;
					break;
				}
		status.lastLook[(int)system] = keep;
		if (keep)
			ps5log::Line("[main] the {} side's games folder {} is a network share's: the list from its last look, no look now",
				ps5launcher::SideTitle(system), folder);
		return keep;
	};
	auto prepare = [&](ps5launcher::System system) {
		if (started == system)
			return;
		started = system;
		status.diagnostics.resize(sessionLine);
		if (ps5retro::IsRetro((int)system))
		{
			const int index = (int)system;
			status.noticeRetro[ps5retro::Slot(index)] = ps5retro::Available(index, settings.retro[ps5retro::Slot(index)].retroBackend) ? std::string() : ps5lang::Tr("This side's emulator is not in this copy of PS5-ECAM.");
			if (!keepList(system, settings.retro[ps5retro::Slot(index)].gamesFolder)) ps5retro::StartScan(index, settings.retro[ps5retro::Slot(index)].gamesFolder);
			return;
		}
		if (system == ps5launcher::System::N3ds)
		{
			ps5log::Line("[main] the 3DS side: Cemu's emulated Wii U is not started");
			status.diagnostics.push_back(ps5lang::Tr("This session: Azahar (3DS); Cemu's emulated Wii U is not started"));
			if (!keepList(system, settings.n3ds.gamesFolder))
				ps5azahar::StartScan(settings.n3ds.gamesFolder);
			ps5emu::LogMemory(); // the 3DS side's start, against Cemu's
			return;
		}
		if (system == ps5launcher::System::Switch)
		{
			// Eden runs in a program of its own (eden/eboot.bin): this side only lists its games
			ps5log::Line("[main] the Switch side: Cemu's emulated Wii U is not started (Eden runs in eden/eboot.bin)");
			status.diagnostics.push_back(ps5lang::Tr("This session: Eden (Switch), in a program of its own; Cemu's emulated Wii U is not started"));
			if (privileges.filesystem && !ps5eden::Available())
				status.noticeSwitch = ps5lang::Tr("This build has no Switch emulator: eden/eboot.bin is not in the app's folder.");
			if (!keepList(system, settings.nx.gamesFolder))
				ps5eden::StartScan(settings.nx.gamesFolder);
			return;
		}
		if (system == ps5launcher::System::Nds)
		{
			ps5log::Line("[main] the DS side: Cemu's emulated Wii U is not started");
			status.diagnostics.push_back(ps5lang::Tr("This session: melonDS (DS); Cemu's emulated Wii U is not started"));
			if (!keepList(system, settings.nds.gamesFolder))
				ps5melonds::StartScan(settings.nds.gamesFolder);
			ps5emu::LogMemory();
			return;
		}
		ps5log::Line("[main] the Wii U side: Azahar's core is not started");
		status.diagnostics.push_back(ps5lang::Tr("This session: Cemu (Wii U); Azahar's core is not started"));
		if (!privileges.filesystem || status.coreReady || status.coreStarting)
			return;
		// Cemu's library loads on a thread of its own while the launcher draws, from its catalogue: the
		// app opens at once (the launcher polls it, and says why when it fails)
		status.coreStarting = true;
		ps5emu::Options options = Options(settings);
		options.keepGameList = keepList(system, settings.gamesFolder);
		ps5emu::StartCore(options);
		if (!privileges.jit)
			ps5notify::Send(ps5lang::Tr("No JIT memory: Wii U games run on the interpreter, much slower. Is PPSA99360 in your HEN's app jailbreak list?"));
	};

	// what this app made since the last start, and earlier builds and sessions as another user, opened
	// to every user in /data/ps5cemu: at each start, as a game stops the walk
	if (privileges.filesystem)
		ps5filemodes::Start();
	if (freshStart && privileges.filesystem)
		ps5update::Start();
	// how long the app took to get here, from the PS5 starting it: the launcher's first frame follows
	ps5log::Line("[main] the launcher starts {} ms into the app", sceKernelGetProcessTime() / 1000);
	for (;;)
	{
		SetHighFrameRate(false); // the launcher at 59.94 Hz
		ps5display::SetFramePacing(1); // and the 3DS's games every refresh
		std::optional<ps5launcher::Choice> choice;
		const ps5shell::Outcome outcome = ps5shell::Run(settings, status, prepare, choice);
		const uint64_t stopping = sceKernelGetProcessTime();
		ps5emu::FinishCoreStart(); // a 3DS or DS game chosen while Cemu's library was still loading waits for it
		ps5update::Stop(); // nothing of the launcher's runs beside a game
		ps5packs::Stop();
		ps5mods::Stop();
		ps5forwarders::Stop();
		ps5filemodes::Stop();
		ps5log::Line("[main] the launcher's work stopped in {} ms", (sceKernelGetProcessTime() - stopping) / 1000);
		if ((outcome != ps5shell::Outcome::Chosen || !choice) && gameRan)
		{
			// after a game in this process the launcher could not draw again: from a fresh process, as
			// before it came back in place
			ps5log::Line("[main] the launcher could not start again after the game: starting PS5-ECAM over");
			RememberSide(ps5launcher::SideName(*gameRan));
			ps5emu::RestartToLibrary();
		}
		if (outcome != ps5shell::Outcome::Chosen || !choice)
		{
			// the launcher could not draw, and there is nothing else to show: the boot log says why. Wait
			// for the player to close the app from the PS5's menu
			ps5log::Line("[main] the launcher could not start or stopped drawing");
			// the splash goes, so the notification is seen, and it says why (as 3.0.0's did)
			sceSystemServiceHideSplashScreen();
			const std::string& why = ps5shell::LastFailure();
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			ps5notify::Send(why.empty() ? ps5lang::TrF("The launcher could not start. The boot log in {0}/logs says why.", ps5paths::Root()) :
										  // tr: {0} is why; {1} the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the app's drive
										  ps5lang::TrF("The launcher could not start: {0}. The boot log in {1}/logs has more.", why, ps5paths::Root()));
			for (;;)
				sceKernelUsleep(1000000);
		}
		const ps5emu::Game& game = choice->game;
		// what the emulator starts: an archived game's unpacked copy (app/game_archives.h); the game's
		// settings, saves and tile stay the archive's
		ps5emu::Game launched = game;
		if (!choice->launchPath.empty())
		{
			launched.path = choice->launchPath;
			ps5log::Line("[main] {} starts from its copy unpacked from {}", game.name, game.path.string());
		}
		const char* side = ps5launcher::SideName(choice->system);
		// a game on another emulator than the one whose game ran here: from a fresh process, where every
		// renderer has always started (by --rom, as a home screen tile's); the same emulator's starts here
		// again. Should the app not start again, the game starts here all the same
		// (Artic Base's, from the 3DS on the network, is no game a library lists: it starts here)
		if (gameRan && choice->system != *gameRan && choice->system != ps5launcher::System::Switch && !ps5retro::IsRetro((int)choice->system) &&
			!ps5azahar::IsArtic(game.path.string()))
		{
			ps5log::Line("[main] {} is the {} side's, after a {} game here: starting PS5-ECAM over for it", game.name,
				ps5launcher::SideTitle(choice->system), ps5launcher::SideTitle(*gameRan));
			RememberSide(side);
			std::vector<std::string> args = {"--rom", game.path.string()};
			if (choice->resume)
				args.push_back("--resume");
			ps5emu::LoadExecApp("eboot.bin", args, false);
			ps5log::Line("[main] PS5-ECAM did not start again: {} starts in this process", game.name);
		}
		// a tile's game that closes the app after it: its in-game menu says so instead of the library (#48)
		ps5emu::SetExitAfterGame(tile.exitAfterGame && IsTileGame(tile, game));
		// the side's settings, and the game's own over them
		const ps5settings::Launcher forGame = ps5settings::ForGame(settings, side, game.titleId);
		if (settings.games.count(ps5settings::GameKey(side, game.titleId)))
			ps5log::Line("[main] {} starts with settings of its own", game.name);
		// a handheld's touchpad is its touch screen, whatever a Wii U player's click was made earlier on
		if (choice->system != ps5launcher::System::WiiU)
			for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				ps5pad::SetTouchpadButton(player, false);
		// the side's turbo button, held, presses again and again in the game (Settings > Controls)
		{
			const std::string& turbo = choice->system == ps5launcher::System::N3ds ? forGame.n3ds.turbo :
				choice->system == ps5launcher::System::Nds						   ? forGame.nds.turbo :
																					 forGame.turbo;
			ps5pad::SetTurbo(ps5pad::ButtonNamed(turbo));
			if (!turbo.empty())
				ps5log::Line("[main] turbo button: {}", turbo);
		}

		if (ps5libretro::Plays((int)choice->system) && !game.path.empty())
		{
			// the side's libretro core, in this process as melonDS below: its picture on VideoOut at 59.94 Hz
			const auto side = ps5launcher::SideName(choice->system);
			const int index = (int)choice->system;
			RememberSide(side);
			SetHighFrameRate(false);
			ps5threads::EndIsolation();
			ps5libretro::SetResume(choice->resume);
			if (ps5libretro::LaunchGame(index, game, forGame.retro[ps5retro::Slot(index)], error))
			{
				ps5libretro::RunGame();
				countPlayTime((ps5catalog::System)index, game.titleId, game.name, std::time(nullptr));
				backInProcess(choice->system);
				continue;
			}
			ps5log::Line("[main] {} did not start: {}", game.name, error);
			if (ps5libretro::CoreTouched())
			{
				RememberSide(side, error);
				ps5emu::RestartToLibrary();
				return 1;
			}
			status.launchError = error;
			settings.side = side;
			continue;
		}

		if (ps5retro::IsRetro((int)choice->system))
		{
			const auto side = ps5launcher::SideName(choice->system);
			RememberSide(side);
			if (game.path.empty()) ps5retro::LaunchLibrary((int)choice->system, error, settings.retro[ps5retro::Slot((int)choice->system)].retroBackend);
			else ps5retro::Launch((int)choice->system, game.path.string(), error, settings.retro[ps5retro::Slot((int)choice->system)].retroBackend);
			status.launchError = error;
			settings.side = side;
			continue;
		}
		if (choice->system == ps5launcher::System::Switch)
		{
			// Eden's own program in place of this one: it starts the game at once, with this side's settings
			// and the game's own, and starts this app again on the Switch side when the game ends (or
			// closes it, for a tile's game). The side is saved first, so a crash in it comes back here too.
			// Eden draws PS5-ECAM's loading screen and in-game menu over the game (port/eden/ingame.h),
			// from what the launcher left it (ps5eden::WriteSession).
			RememberSide("switch");
			ps5eden::LaunchGame(game, forGame, ps5emu::ExitAfterGame(), error);
			ps5log::Line("[main] {} did not start: {}", game.name, error);
			status.launchErrorSwitch = error;
			settings.side = "switch"; // Shell::Run clears it, and says why on the Switch side
			continue;
		}

		if (choice->system == ps5launcher::System::Nds)
		{
			// melonDS, its screens on VideoOut at 59.94 Hz, as Azahar's below; its threads on every CPU
			SetHighFrameRate(false);
			ps5threads::EndIsolation();
			ps5melonds::SetResume(choice->resume);
			if (ps5melonds::LaunchGame(game, forGame.nds, error))
			{
				ps5melonds::RunGame();
				countPlayTime(ps5catalog::System::Nds, game.titleId, game.name, std::time(nullptr));
				backInProcess(ps5launcher::System::Nds);
				continue;
			}
			ps5log::Line("[main] {} did not start: {}", game.name, error);
			if (ps5melonds::CoreTouched())
			{
				// the screens took VideoOut: show why from a fresh process
				RememberSide("ds", error);
				ps5emu::RestartToLibrary();
				return 1;
			}
			status.launchErrorDs = error;
			settings.side = "ds"; // the launcher shows why on melonDS's side, as the others do
			continue;
		}

		if (choice->system == ps5launcher::System::N3ds)
		{
			// VideoOut configured again once the new launcher's surface is gone, as before Cemu's below:
			// Azahar's surface follows on the output the launcher presented on, at 59.94 Hz; its threads
			// on every CPU
			SetHighFrameRate(false);
			ps5threads::EndIsolation();
			ps5azahar::SetResume(choice->resume);
			if (ps5azahar::LaunchGame(launched, forGame.n3ds, error))
			{
				ps5azahar::RunGame();
				countPlayTime(ps5catalog::System::N3ds, game.titleId, game.name, std::time(nullptr));
				backInProcess(ps5launcher::System::N3ds);
				continue;
			}
			ps5log::Line("[main] {} did not start: {}", game.name, error);
			if (ps5azahar::CoreTouched())
			{
				// Azahar's core is half started: show why from a fresh process
				RememberSide("3ds", error);
				ps5emu::RestartToLibrary();
				return 1;
			}
			status.launchError3ds = error;
			settings.side = "3ds"; // the launcher shows why on Azahar's side
			continue;
		}

		ps5emu::ApplyOptions(Options(forGame));
		ps5emu::SetGameSettings(game.titleId, forGame);
		SetHighFrameRate(forGame.highFrameRate);
		ps5display::SetFramePacing(forGame.framePacing);
		ps5emu::SetFrameGeneration(forGame.frameGeneration);
		if (forGame.frameGeneration)
			ps5log::Line("[framegen] on for this game: frames are made while it takes two refreshes a frame or more");
		ps5emu::SetReShade(forGame.reshade);
		if (forGame.reshade)
			ps5log::Line("[reshade] on for this game: its preset is compiled at the first frame");
		ps5window::Initialize();
		if (ps5emu::LaunchGame(launched, error))
		{
			ps5emu::RunGame();
			countPlayTime(ps5catalog::System::WiiU, game.titleId, game.name, std::time(nullptr));
			afterGame("wiiu");
			return 0;
		}
		ps5log::Line("[main] {} did not start: {}", game.name, error);
		if (!ps5emu::RendererStarted())
		{
			status.launchError = error;
			settings.side = "wiiu";
			continue;
		}
		// Cemu's renderer holds VideoOut: show the reason from a fresh process
		RememberSide("wiiu", error);
		ps5emu::RestartToLibrary();
		return 1;
	}
}
