// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the launcher's settings, ps5cemu.json in the data folder (app/paths.h). Cemu's own
// settings stay in its settings.xml; the launcher writes the few it manages (game folder, volume,
// overlay, upscaling filter) into both. Azahar's are here (n3ds), and given to it when a 3DS game
// starts; melonDS's (nds) have the same shape, the DS side's own (its folder, screens, sound,
// controls) and the few only a DS has (ds...); so have Eden's (nx, the Switch side's: its folder,
// volume, overlay and the few only a Switch has, nx...), given to eden/eboot.bin as it starts a game.
// A game can have its own of some of them (games: 6.5), which it starts with over its side's.

#pragma once
#include <array>

#include "../app/paths.h"
#include "../retro/registry.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps5settings
{
	// Azahar's side, and melonDS's (the fields a DS has: its folder, layout, border, volume, deadzone,
	// buttons, language and the ds... ones)
	struct N3ds
	{
		int retroBackend = 0; // N64/NES: 0 original donor, 1 optional alternative; no save migration.
		std::string gamesFolder = ps5paths::Azahar() + "/games";
		int resolution = 6;			  // the internal resolution, times the 3DS's 400x240
		int layout = 2;				  // the screens' layout (ps5azahar::Layout): the top one large
		int textureFilter = 0;		  // none
		int volume = 100;			  // percent
		bool motion = true;			  // the DualSense's motion sensors as the 3DS's
		int deadzone = 15;			  // the sticks', percent
		bool performance = false;	  // the frame rate and speed over the game (its in-game menu)
		int region = -1;			  // the emulated 3DS's region: -1 automatic (the game's), else Azahar's region_value
		int language = -1;			  // the emulated 3DS's language: -1 as Azahar sets it, else its SystemLanguage (0 to 11)
		bool customTextures = false;  // texture packs from azahar/load/textures/<title ID> (Azahar's custom_textures)
		// Azahar's async_shader_compilation (Settings > Graphics): off, as desktop Azahar has it. On, a
		// draw whose pipeline is still building is skipped (a Pokemon game's models drawn as their dark
		// outline alone until it is ready); off, it waits for it once (a stutter)
		bool asyncShaders = false;
		int border = 0;				  // artwork around the screens: 0 none, else ps5ingame3ds::kBorderNames' theme
		int cpuClock = 100;			  // the emulated CPU's speed, percent of the 3DS's (Azahar's cpu_clock_percentage)
		// only in ps5cemu.json: Azahar's statistics and frame limiter on the TSC (Azahar patch 0023),
		// which time every SVC; false: the steady clock, a call into the kernel each read
		bool tscClock = true;
		// every 3DS vertex position computed invariantly (Azahar patch 0010), as Azahar does on Apple
		// GPUs: Settings > Graphics' Outline fix, which a game can have its own of. Off by default
		// everywhere: the Pokemon games it was made for (#23) draw right with hwShaders off instead
		bool invariantPosition = false;
		// Azahar's hardware shaders (use_hw_shader): the 3DS's vertex shaders on the GPU. Off, they run on
		// the CPU (Azahar's shader JIT), where a model and the outline drawn around it are computed alike
		// and the normals' quaternions are corrected per triangle: off by default for the Pokemon games
		// (PokemonByDefault), whose outlines, lighting and shadows glitch on the console's GPU otherwise
		// (it has no fragment shader barycentrics, and RADV and the outlines' invariance disagree,
		// Citra #7376). Settings > Graphics, and a game's own
		bool hwShaders = true;
		// the 3DS's 3D, as red and cyan for anaglyph glasses (Azahar's render_3d, the 3D slider all the way
		// up): Settings > Graphics, and a game's own. Off, the 3DS's 3D screen shows one eye, as before
		bool anaglyph = false;
		// the DS's games (melonDS: port/melonds)
		int dsFilter = 0;			  // how the DS's screens are scaled to the TV: sharp, smooth, square pixels (ps5melonds::screens::Filter)
		// the touchpad touches the bottom screen while only the top one is shown (the top screen alone):
		// its cursor over the top screen, for the games that want a touch now and then (Star Fox 64 3D)
		bool hiddenTouch = false;
		int dsRotation = 0;			  // the DS (or the 3DS) turned this many quarter turns clockwise, for the games held on their side (Hotel Dusk)
		// a gap between the two screens, as the hinge makes on the console: one above the other, side by
		// side and turned like a book (the in-game menu's quick action, Settings > Screens)
		bool screenGap = false;
		// the 3DS's: MihawkGen's picture between each two frames of a game at 30 fps (port/cemu/FrameGenPS5.h,
		// #66), off until measured on the console
		bool frameGeneration = false;
		bool dsJit = true;			  // melonDS's recompiler where the console gives executable memory; off: its interpreter, slower
		bool dsOwnBios = true;		  // the DS's own BIOS and firmware when they are in melonds/bios; off (or missing): melonDS's FreeBIOS
		int dsTouchX = 0, dsTouchY = 0, dsTouchScale = 100; // Display-space calibration; defaults preserve mapping.
		int dsMicSource = 0; // 0: existing R3 noise, 1: silence, 2: recorded WAV; next launch.
		bool dsSlot2 = false; // Optional GBA cartridge for DS interactions; no GBA execution, off by default.
		bool dsStylus = false;	  // the right stick drags a touch on the DS's bottom screen (Metroid Prime Hunters' aim); its directions then nothing else
		// the Switch's games (Eden: port/eden), handed to its program as a game starts
		int nxResolution = 2;		  // Eden's internal resolution: 0.5x, 0.75x, 1x (the game's own), 1.5x, 2x, 3x, 4x
		int nxFilter = 0;			  // how its picture is scaled to the TV: bilinear, AMD FSR 1, bicubic, nearest
		int nxAstc = 0;				  // ASTC textures, which the PS5's GPU cannot read: kept compressed (as BC3), or at full quality
		bool nxDocked = true;		  // the emulated Switch in its dock (the TV's picture), else handheld
		bool nxFrameGeneration = false; // ProsperoEden's frame generation, with MihawkGen: off, as ProsperoEden has it
		int nxSpeedLimit = 0;		  // Eden's speed limit: 100%, 150%, 200%, 300% or none (ProsperoEden's kSpeedLimitKeys)
		int nxCovers = 0;			  // the Switch side's covers: GameTDB's box art, or each game's own icon
		int nxAntiAliasing = 0;		  // Eden's anti-aliasing: none, FXAA, SMAA
		int nxSharpening = 25;		  // AMD FSR 1's sharpening, Eden's slider: 0 (the sharpest) to 200
		int nxController = -1;		  // the controller a Switch game sees: -1 automatic, then Pro, handheld, Joy-Con pair, left, right
		int nxDeadzone = 8;			  // the sticks' deadzone in a Switch game, percent (Eden's own default)
		// ProsperoEden's Settings, as its own launcher has them (port/eden/launch.cpp hands them over)
		int nxOutput = 0;			  // the picture Eden presents: 1080p, 1440p, 2160p
		bool nxHighRefresh = false;	  // the TV at 120 Hz, else 60
		int nxLanguage = -1;		  // the Switch's system language (ps5eden::kLanguages); -1: the menus' where the Switch has it
		int nxMemory = 0;			  // the emulated Switch's memory: 4, 6 or 8 GB (Eden's memory layout)
		bool nxAsyncShaders = false;  // draw before a new shader is ready
		bool nxFastGpu = false;		  // Eden's lowest GPU accuracy
		bool nxUnsafeCpu = false;	  // dynarmic's inexact floating-point shortcuts
		bool nxUnsafeDma = false;	  // Eden's unsafe DMA accuracy
		bool nxReactiveFlushing = true; // Eden's reactive flushing: off is faster, some effects break
		bool nxSkipInvalidation = false; // fewer CPU cache invalidations
		bool nxBlockList = false;	  // the blocks of earlier sessions compiled ahead
		bool nxCompactMemory = false; // the game's memory in 16 KiB pages where it is mostly zeroes
		// ProsperoEden's rooms (Settings > Online > Multiplayer): the one a Switch game joins as it starts, none
		// when nxRoomHost is empty, and the nickname shown in it
		std::string nxRoomHost, nxRoomName, nxRoomPassword, nxNickname;
		int nxRoomPort = 24872;
		std::string articAddress;	  // the last Artic Base server's IPv4 address, a 3DS on the network
		int gameCount = -1;			  // the library's games when last looked for (the start screen's); -1: never
		std::map<std::string, std::string> buttons; // 3DS button: DualSense input, where not the default
		std::string turbo;			  // the DualSense input that presses again and again while held (Controls > Turbo button); empty: none
		uint64_t lastGame = 0;		  // title ID, or one made from the path
		std::vector<uint64_t> recent; // newest first, at most four
	};

	// The DS side's defaults, where they are not the 3DS's: its own folder, and the screens one above
	// the other, as on a DS
	inline N3ds DsDefaults()
	{
		N3ds nds;
		nds.gamesFolder = ps5paths::MelonDs() + "/games";
		nds.layout = 0;
		return nds;
	}

	// The Switch side's: Eden's selected console games folder (/data/eden/roms on a new
	// install, or legacy discovery), and its volume, as Eden has them
	inline N3ds SwitchDefaults()
	{
		N3ds nx;
		nx.gamesFolder = ps5paths::Eden() + "/roms";
		return nx;
	}

	// A game's own settings: a setting's name in ps5cemu.json and its value (on and off as 1 and 0)
	using GameValues = std::map<std::string, int>;

	// The new launcher's own, under "ui"
	struct Ui
	{
		std::string lastSide;		 // "wiiu", "3ds", "ds" or "switch": the side last used, restored at every start
		std::string language;		 // the menus' language (ps5lang's code, "de"); empty: the PS5's
		bool largerText = false;
		bool highContrast = false;
		bool reduceMotion = false;
		int holdMs = 800;			 // hold to confirm, 400 to 1500
		bool gamePictures = true;	 // the focused game's picture behind the menus
		bool setupDone = false;		 // the Setup check was shown once
		// the sides the bar offers, Wii U 1, 3DS 2, DS 4, Switch 8 (Settings > Launcher > Sides, and the
		// first start's Make it yours); at least one
		int sides = 15;
		bool switchOffered = true;	 // the Switch side was offered: those whose sides predate it (4.0.2) see it once
		std::string lastVersion;	 // the version that ran last: a newer one says what it brings, once (Make it yours)
		// a release the start does not offer (the update's Skip this version, or one gone back from), as
		// ps5update::Readable writes it; Settings > Online still finds it
		std::string skipVersion;
		// the day (since 1970) A folder for each system was put off (Not now): asked again a week later
		int reorganizeAsked = 0;
		std::string theme;			 // the launcher's look (ui/theme.h): a built-in theme's id, or user:<file> in themes/; empty: the default
		bool libraryFilters = false;		 // the Library's filter row (Settings > Launcher); off: every game, as All
		int libraryFilter[ps5retro::kSideCount] = {}; // per side: All, Recently added, Favourites, Graphic packs on (Wii U)
		int librarySort[ps5retro::kSideCount] = {}; // Recently played (the default: the Library is where a side opens), A to Z, Release year, How it runs
	};

	struct Launcher
	{
		std::string gamesFolder = ps5paths::Games();
		int upscaleFilter = 1;		  // how the game's picture is scaled to 4K: Cemu's upscale_filter
		bool highFrameRate = false;	  // the 119.88 Hz mode where the display has it
		// frame pacing (ps5/display.h): each frame shown for at least this many refreshes, 1 to 3;
		// 2 is an even 60 fps at 119.88 Hz and an even 30 at 59.94 Hz (Settings > Graphics, in-game Graphics)
		int framePacing = 1;
		// MihawkGen's picture between each two frames of a game at two refreshes a frame or more
		// (port/cemu/FrameGenPS5.h, #66): off until measured on the console
		bool frameGeneration = false;
		// ReShade's preset on the TV's picture (port/cemu/ReShadePS5.h): /data/ps5cemu/reshade's
		// ReShadePreset.ini, or the game's own in presets/
		bool reshade = false;
		bool overlay = false;		  // Cemu's performance overlay from the start
		bool asyncShaders = true;	  // Cemu's async_compile (Settings > Graphics, and the in-game menu's Graphics)
		bool shaderNotices = true;	  // Cemu's "compiled shaders/pipelines" notices over a game (its notification.shader_compiling, #37)
		bool gamePadSpeaker = false;  // the GamePad's sound on player 1's DualSense speaker (Settings > System)
		bool remoteScreen = false;	  // the remote touch screen (stream/stream.h), every side's: Settings and the in-game menus
		bool rumble = true;
		std::string turbo;			  // the Wii U's turbo button: a DualSense input that presses again and again while held; empty: none
		// what each Wii U player's touchpad click is (#37): 0 as their controller suits (it touches the
		// GamePad's screen, a button on the others), 1 it touches the screen, 2 it presses a button
		int touchpadClick[4] = {0, 0, 0, 0};
		// A and B for every Wii U player, chosen before Cemu was up (Make it yours): 1 on Circle, 2 on Cross,
		// made the players' as the Wii U side next opens; 0: nothing waiting
		int wiiuFaceLayout = 0;
		// Wii U accounts (Settings > Accounts and saves): each PS5 user who starts the app plays on a Wii U
		// account of their own, by their PS5 user ID ("0x10fd1069") to Cemu's persistent ID ("80000002")
		bool accountPerUser = false;
		std::map<std::string, std::string> accountsByUser;
		// a side whose games folder is on a network share looks through it each time it opens (Settings >
		// Game files, #75); off: the list from its last look shows at once, until Look for games now
		bool lookOnShares = false;
		bool pinCpuThreads = false;	  // an experiment (ps5/threads.h): only in ps5cemu.json
		// only in ps5cemu.json: Cemu's pipeline compile threads but the first below normal priority, as
		// Cemu runs them on Windows (ps5/threads.h). An experiment, off: the console schedules by strict
		// priority, where Windows lifts a starving thread now and then, so a compile thread kept off the
		// CPUs while it holds a lock others need (the heap's arenas) could hold them up for as long
		// (ProsperoEden hung for good that way, in its allocator)
		bool compileBelowNormal = false;
		// only in ps5cemu.json: how long, in microseconds, an idle emulated CPU core spins for a thread
		// to run before it sleeps (Cemu patch 0029); any but 0 also has the GPU thread wait for
		// commands without yielding (Cemu patch 0030). Games hand work between their cores many times a
		// frame, and a sleeping core then waits on the console's thread wake-up; ProsperoEden spins
		// 100 us for the same reason (a racing game's heavy phase from ~55 to ~59.6 fps). 100 in 4.0.0's
		// test builds, for the A/B that decides it; 0: Cemu's own (sleep at once)
		int idleSpinUs = 100;
		// only in ps5cemu.json: how long, in microseconds, an emulated core that finds Cemu's scheduler
		// lock taken retries it before it sleeps in the kernel (Cemu patch 0037). The three cores take
		// it at every switch between the game's threads and every guest mutex, event and alarm; on the
		// console a pthread mutex found taken sleeps in the kernel at once, and its unlock wakes the
		// sleeper with another call. 20 in 4.0.0's test builds, for the A/B that decides it; 0: sleeps
		// at once, as before
		int schedulerSpinUs = 20;
		// only in ps5cemu.json: the least time, in microseconds, between the main emulated core's looks
		// at the system events (the audio output, alarms, NFP) while it idles (Cemu patch 0037). With
		// the clocks on the TSC its idle loop turns in a fraction of a microsecond, each turn taking
		// locks the other cores and the audio threads need. 20 in 4.0.0's test builds, for the A/B
		// that decides it; 0: every turn, as before
		int idleEventsUs = 20;
		// only in ps5cemu.json: thread_local reads from a per-thread cache (patches/compiler-rt) rather than
		// a call to libkernel's pthread_getspecific each time: on the console every thread_local is
		// emulated, about 2% of each emulated core in Breath of the Wild's profile (2026-10-13). Tested
		// on a PC (14,400 threads, sanitizers); off until a console A/B has measured it
		bool fastTls = false;
		// only in ps5cemu.json: the emulated CPU cores and Cemu's GPU thread each alone on a physical core
		// of its own, every other thread kept off them, as ProsperoEden 1.001 places its threads, the
		// cores found by a timing where CPUID does not tell them apart (ps5/threads.h). Read as the app
		// starts. On in 4.0.0's test builds, for the A/B that decides it: the driver's submit thread and
		// the main core's idle loop now spin, and must not share a physical core with an emulated one
		bool isolateCores = true;
		// only in ps5cemu.json: Cemu's samplers with standard border colours only (Cemu patch 0031), for
		// a game that stops the console's GPU: ProsperoEden saw custom ones fault it in some games.
		// Off, as on a PC
		bool standardBorderColors = false;
		// only in ps5cemu.json: RADV's shader disk cache, on as on a PC. Mesa turns it off in a process
		// whose effective user is not its real one (the HEN's root), so the real user and group are
		// made the effective ones and the driver keeps the shaders it compiles for the next start.
		// false: every start compiles them again (as before 3.5.1)
		bool shaderDiskCache = true;
		// only in ps5cemu.json: Cemu's exact stfs conversion (Cemu patch 0024), as upstream Cemu now
		// stores on a PC; off, as every Cemu before it and every PS5-ECAM release so far stored (with
		// cvtsd2ss). Which costs more on the console has not been measured yet
		bool accurateSingleStores = false;
		// only in ps5cemu.json: Cemu's millisecond clock, which its GPU thread takes at every draw call,
		// on the TSC instead of clock_gettime, a call into the kernel (890 ns a call, measured): it gave
		// Breath of the Wild's GPU thread back 3.3 ms of each 16.7 ms frame (A/B on a console,
		// 2026-10-03, at 4,300 draws a frame); false: clock_gettime
		bool tscTickCount = true;
		// only in ps5cemu.json: Cemu's other host clocks on the TSC too (Cemu patch 0036): the timer that
		// paces the emulated vsync, which the GPU thread reads in its command loop, and the one the
		// main emulated core reads at every switch between the game's threads (the audio output's).
		// clock_gettime took 22% of the emulated CPU thread and 11-13% of the GPU thread in 8-player
		// Super Smash Bros. at 4K, and 20% of Breath of the Wild's main emulated core and 10% of its GPU
		// thread at 4K (profiles, 2026-10-13). Only when the TSC's reported rate checks out
		// (ps5clock::TscAgrees). On in 4.0.0's test builds, for the A/B that decides it: the timer was
		// on the TSC once before (2026-10-03, Cemu patch 0011 took it back) together with thread
		// pinning, when Breath of the Wild fell to the 40s, and neither was measured alone (the
		// pinning put the driver's kick thread on the GPU thread's CPU, the likelier cause)
		bool tscClocks = true;
		// only in ps5cemu.json: the Vulkan driver reads the time from the TSC (ps5/driver_clock.h)
		// rather than with clock_gettime, an 890 ns system call here: seven reads a submission and
		// every turn of its waits for the GPU. On in 4.0.0's test builds, for the A/B that decides it
		bool driverClockTsc = true;
		// only in ps5cemu.json: the driver hands each submission to a thread of its own, which makes
		// the AGC call (patches/mesa 0008, RADV_PS5_SUBMIT_THREAD): sceAgcDriverSubmitDcb blocked Cemu's
		// GPU thread for 13-18% of its time in Breath of the Wild at 4K (agc_ms, 2026-10-13), time a
		// PC's driver does not take. On in 4.0.0's test builds, for the A/B that decides it; false:
		// the GPU thread submits itself, as before
		bool asyncSubmit = true;
		// only in ps5cemu.json: how long the driver's waits for the GPU sleep between looks once their
		// 1.5 ms of spinning is over, in microseconds (patches/mesa 0009, RADV_PS5_WAIT_SLEEP_US): Cemu
		// waits so for the previous frame at every swap, and wakes up to a sleep late. 0: the driver's
		// own 1000. 200 in 4.0.0's test builds, for the A/B that decides it
		int gpuWaitSleepUs = 200;
		// only in ps5cemu.json: the sampling profiler (ps5/profiler.h), into logs/profile.bin
		bool profile = false;
		// only in ps5cemu.json: a per-thread cache of small blocks in front of the title heap
		// (ps5/heap_wrap.c), so a thread's malloc and free of up to 1 KiB take no lock and no system
		// call: Cemu's recompiler spent about a third of its time in them. An experiment, off until a
		// console has measured it
		bool heapThreadCache = false;
		// only in ps5cemu.json: ask a HEN (etaHEN, OnionHEN) at every start, as before 4.0.0, even when
		// the app has what it needs without one; off: only when something is out of reach without it
		bool alwaysAskHen = false;
		// only in ps5cemu.json: RADV_DEBUG for the Vulkan driver (e.g. "nongg,nohiz"), to narrow down
		// a GPU hang by turning hardware features off; empty by default
		std::string radvDebug;
		// only in ps5cemu.json: more of the Vulkan driver's environment, name to value, for its
		// performance experiments (RADV_, MESA_ and ACO_ names only), e.g.
		// {"RADV_THREADED_RECORDING": "1", "RADV_PS5_GPU_TIME": "1"}; empty by default
		std::map<std::string, std::string> radvEnvironment;
		// only in ps5cemu.json: how many draws Cemu records in a command buffer before it submits it,
		// for the driver's performance experiments (docs/DRIVER-PERFORMANCE.md); 0, the default,
		// keeps Cemu's own 300
		int cemuSubmitDraws = 0;
		// only in ps5cemu.json: how many frames the GPU may still be drawing as Cemu presents the next
		// (Cemu patch 0038): 1, Cemu's own, waits at each swap for the previous frame; 2 for the one
		// before it, so the GPU thread and the GPU overlap more at 4K, a frame later on screen. An
		// experiment, 1 until the [perf] lines' waits show the previous frame is what the GPU thread waits on
		int framesInFlight = 1;
		int volume = 100;			  // the TV sound, in percent
		int gameCount = -1;			  // the Wii U library's games when last looked for (the start screen's); -1: never
		uint64_t lastGame = 0;		  // title ID
		std::vector<uint64_t> recent; // newest first, at most four
		N3ds n3ds;
		N3ds nds = DsDefaults();
		N3ds nx = SwitchDefaults();
		// the other sides', in ps5retro::kSystems' order, each with its default games folder
		std::array<N3ds, ps5retro::kSystems.size()> retro = [] { std::array<N3ds, ps5retro::kSystems.size()> values{}; for (size_t i = 0; i < values.size(); i++) values[i].gamesFolder = ps5retro::kSystems[i].games; return values; }();
		// games' own settings, by GameKey: "3ds:0004000000055d00" -> {"resolution": 3}
		std::map<std::string, GameValues> games;
		// The launcher's own sound, on both sides (frontend/sound.h): its music ("setup" or "off") and the music's volume in percent, and the menu's sounds.
		std::string music = "setup";
		int musicVolume = 50;
		bool boxArt = true;			  // box art from GameTDB (Settings > Online)
		bool menuSounds = true;
		// The side the launcher opens on after a game, the one last played ("wiiu", "3ds", "ds" or "switch");
		// empty: a fresh start.
		std::string side;
		// Why the last game did not start, when that needed a fresh process to show (the launcher
		// shows it once, then clears it).
		std::string launchError;
		Ui ui;
	};

	Launcher Load();
	bool Save(const Launcher& settings);
	// A ps5cemu.json Load could not read is moved to ps5cemu.json.broken, and the defaults used, so the
	// next Save does not write over it (a hand edit's slip costs nothing): where it went, once, then empty
	std::string TakeBrokenFile();

	// -- games' own settings -------------------------------------------
	// side: "wiiu", "3ds", "ds" or "switch", as ps5cemu.json names them

	std::string GameKey(const std::string& side, uint64_t titleId);
	// The settings a game on a side can have its own of, by their names in ps5cemu.json
	const std::vector<std::string>& GameSettingNames(const std::string& side);
	// A side's setting as a number (on and off as 1 and 0), and set from one (clamped as Load clamps).
	// The Wii U's are the Launcher's own, the 3DS's n3ds', the DS's nds', the Switch's nx'.
	bool Value(const Launcher& settings, const std::string& side, const std::string& name, int& value);
	void SetValue(Launcher& settings, const std::string& side, const std::string& name, int value);
	// The settings a game starts with: its side's, with the game's own over them (and hardware shaders
	// off for the Pokemon games, PokemonByDefault, unless the game says otherwise)
	Launcher ForGame(const Launcher& settings, const std::string& side, uint64_t titleId);
	bool PokemonByDefault(uint64_t titleId);
	// After a game's menu changed settings: each one it changed (from before, what the game started
	// with, to after) saved in ps5cemu.json, into the game's own settings when it has some and the
	// setting is one a game can have, else into the side's; the rest of the side's as after has them.
	void SaveChanges(const std::string& side, uint64_t titleId, const Launcher& before, const Launcher& after);
	// The remote touch screen turned on or off in a game's menu: saved at once, for every side, the
	// rest of ps5cemu.json as it is
	void SaveRemoteScreen(bool on);
	// The game played last and first among the recent ones (at most four).
	void AddRecent(uint64_t& lastGame, std::vector<uint64_t>& recent, uint64_t titleId);
	inline void AddRecent(Launcher& settings, uint64_t titleId) { AddRecent(settings.lastGame, settings.recent, titleId); }
}
