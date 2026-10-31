// SPDX-License-Identifier: GPL-3.0-or-later
// The app's libretro frontend (port/libretro/session.h) with a core of this test's own, which does
// what the real ones do: declares options, picks a pixel format, draws with padded rows, makes sound,
// reads the pads, keeps save RAM and serializes. Prints PASS lines; exits 1 on a failure.
#include "libretro/session.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool ok, const char* what)
{
	std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
	failures += !ok;
}

static std::vector<uint8_t> Read(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void Put(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

// -- the test's core ---------------------------------------------------------------------------------

namespace testcore
{
	retro_environment_t environment;
	retro_video_refresh_t video;
	retro_audio_sample_t sample;
	retro_audio_sample_batch_t samples;
	retro_input_poll_t poll;
	retro_input_state_t input;
	unsigned api = RETRO_API_VERSION;
	bool fullpath = false;
	bool refuseGame = false;
	std::vector<uint8_t> given; // the game as the core was handed it
	std::string givenPath;
	uint8_t saveRam[256];
	uint32_t counter = 0; // what its save state holds
	int inits = 0, deinits = 0;
	// what run does
	retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
	bool duplicate = false;
	int16_t readMask = 0, readA = 0, readStickX = 0, readAnalogButton = 0;
	bool updated = false;
	const char* optionValue = nullptr;
	retro_log_callback log{};
	retro_rumble_interface rumble{};

	retro_core_option_v2_definition definitions[] = {
		{"test_palette", "Palette", nullptr, "The colours", nullptr, nullptr, {{"warm", "Warm"}, {"cool", "Cool"}, {"raw", nullptr}, {nullptr, nullptr}}, "cool"},
		{"test_overscan", "Overscan", nullptr, nullptr, nullptr, nullptr, {{"off", nullptr}, {"on", nullptr}, {nullptr, nullptr}}, "off"},
		{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {{nullptr, nullptr}}, nullptr},
	};
	retro_core_options_v2 english{nullptr, definitions};
	retro_core_option_v2_definition german[] = {
		{"test_palette", "Farbpalette", nullptr, nullptr, nullptr, nullptr, {{"warm", "Warm (de)"}, {nullptr, nullptr}}, nullptr},
		{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {{nullptr, nullptr}}, nullptr},
	};
	retro_core_options_v2 local{nullptr, german};
	retro_core_options_v2_intl intl{&english, &local};
	retro_variable legacy[] = {{"test_speed", "Speed; normal|fast|slow"}, {nullptr, nullptr}};

	void Init() { inits++; }
	void Deinit() { deinits++; }
	unsigned Api() { return api; }
	void SystemInfo(retro_system_info* info)
	{
		*info = {};
		info->library_name = "TestCore";
		info->library_version = "1.0";
		info->valid_extensions = "tst";
		info->need_fullpath = fullpath;
	}
	void AvInfo(retro_system_av_info* info)
	{
		*info = {};
		info->geometry = {4, 2, 4, 2, 0.0f};
		info->timing = {60.0988, 44100.0};
	}
	void SetEnvironment(retro_environment_t e)
	{
		environment = e;
		unsigned version = 0;
		environment(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version);
		if (version >= 2)
			environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL, &intl);
		environment(RETRO_ENVIRONMENT_SET_VARIABLES, legacy);
	}
	void SetVideo(retro_video_refresh_t v) { video = v; }
	void SetSample(retro_audio_sample_t s) { sample = s; }
	void SetSamples(retro_audio_sample_batch_t s) { samples = s; }
	void SetPoll(retro_input_poll_t p) { poll = p; }
	void SetInput(retro_input_state_t i) { input = i; }
	void SetPort(unsigned, unsigned) {}
	void Reset() { counter = 0; }
	void Run()
	{
		poll();
		environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated);
		retro_variable variable{"test_palette", nullptr};
		environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable);
		optionValue = variable.value;
		readMask = input(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
		readA = input(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
		readStickX = input(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
		readAnalogButton = input(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2);
		counter++;
		environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
		if (duplicate)
			video(nullptr, 4, 2, 0);
		else if (format == RETRO_PIXEL_FORMAT_XRGB8888)
		{
			uint32_t frame[2][6] = {{0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff}, {0x00123456, 0, 0, 0}}; // rows of 6, 4 shown
			video(frame, 4, 2, sizeof(frame[0]));
		}
		else
		{
			const bool rgb565 = format == RETRO_PIXEL_FORMAT_RGB565;
			uint16_t frame[2][5] = {{uint16_t(rgb565 ? 0xf800 : 0x7c00), uint16_t(rgb565 ? 0x07e0 : 0x03e0), 0x001f, uint16_t(rgb565 ? 0xffff : 0x7fff)}, {0, 0, 0, 0}};
			video(frame, 4, 2, sizeof(frame[0]));
		}
		int16_t batch[8] = {1, -1, 2, -2, 3, -3, 4, -4};
		samples(batch, 4);
		sample(5, -5);
	}
	size_t SerializeSize() { return sizeof(counter); }
	bool Serialize(void* data, size_t size)
	{
		if (size < sizeof(counter))
			return false;
		std::memcpy(data, &counter, sizeof(counter));
		return true;
	}
	bool Unserialize(const void* data, size_t size)
	{
		if (size != sizeof(counter))
			return false;
		std::memcpy(&counter, data, sizeof(counter));
		return true;
	}
	bool LoadGame(const retro_game_info* game)
	{
		givenPath = game && game->path ? game->path : "";
		given.assign((const uint8_t*)(game ? game->data : nullptr), (const uint8_t*)(game && game->data ? (const uint8_t*)game->data + game->size : nullptr));
		std::memset(saveRam, 0xff, sizeof(saveRam));
		environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log);
		environment(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble);
		return !refuseGame;
	}
	void UnloadGame() {}
	void* MemoryData(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? saveRam : nullptr; }
	size_t MemorySize(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? sizeof(saveRam) : 0; }

	const ps5libretro::Core core{"testcore", Init, Deinit, Api, SystemInfo, AvInfo, SetEnvironment, SetVideo, SetSample, SetSamples, SetPoll,
		SetInput, SetPort, Reset, Run, SerializeSize, Serialize, Unserialize, LoadGame, UnloadGame, MemoryData, MemorySize};
}

static void Frames(int count, const ps5libretro::Pad (&pads)[ps5libretro::kPlayers])
{
	for (int i = 0; i < count; i++)
		ps5libretro::Run(pads);
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const fs::path root = argv[1];
	fs::remove_all(root);
	const fs::path game = root / "games/Test Game.tst";
	Put(game, "the game's bytes");
	ps5libretro::Setup setup;
	setup.system = (root / "system").string();
	setup.saves = (root / "saves").string();
	setup.states = (root / "states").string();
	setup.options = {{"test_palette", "warm"}, {"test_overscan", "sideways"}};
	Put(root / "saves/Test Game.srm", std::string(16, '\x42'));
	std::string error;

	// the cores this build links: none on the host, and a weak reference is null, not a crash
	Check(ps5libretro::Find("mesen") == nullptr && ps5libretro::Find("mgba") == nullptr && ps5libretro::Find("nothing") == nullptr,
		"a core the build does not link is not offered");

	testcore::api = 2;
	Check(!ps5libretro::Load(testcore::core, game.string(), setup, error) && error.find("API") != std::string::npos && !ps5libretro::Loaded(),
		"a core of another libretro API is refused");
	testcore::api = RETRO_API_VERSION;
	testcore::refuseGame = true;
	Check(!ps5libretro::Load(testcore::core, game.string(), setup, error) && !ps5libretro::Loaded() && testcore::deinits == testcore::inits,
		"a game the core does not take is refused, and the core stopped again");
	testcore::refuseGame = false;

	Check(ps5libretro::Load(testcore::core, game.string(), setup, error) && ps5libretro::Loaded(), "the game loads");
	Check(std::string(testcore::given.begin(), testcore::given.end()) == "the game's bytes" && testcore::givenPath == game.string(),
		"a core that takes the game from memory has its bytes and its path");
	Check(!ps5libretro::Load(testcore::core, game.string(), setup, error), "a second game is refused while one is loaded");
	Check(testcore::saveRam[0] == 0x42 && testcore::saveRam[15] == 0x42 && testcore::saveRam[16] == 0xff,
		"the save RAM is read from <saves>/<game>.srm, a shorter file over the start of it");
	Check(ps5libretro::CoreName() == "TestCore 1.0" && ps5libretro::FrameRate() > 60.09 && ps5libretro::SampleRate() == 44100.0,
		"the core's name, frame rate and sound rate");

	const auto& options = ps5libretro::Options();
	auto option = [&](const char* key) -> const ps5libretro::Option* {
		for (const auto& o : options)
			if (o.key == key)
				return &o;
		return nullptr;
	};
	const ps5libretro::Option* palette = option("test_palette");
	const ps5libretro::Option* overscan = option("test_overscan");
	const ps5libretro::Option* speed = option("test_speed");
	Check(options.size() == 3 && palette && overscan && speed, "core options v2 and SET_VARIABLES both read");
	Check(palette && palette->label == "Farbpalette" && palette->values[0].second == "Warm (de)" && palette->values[1].second == "Cool" &&
		palette->values[2].second == "raw" && palette->info == "The colours", "an option's words in the launcher's language where the core has them");
	Check(palette && palette->value == "warm" && overscan && overscan->value == "off", "the side's value over the default; one the core has not, the default");
	Check(speed && speed->label == "Speed" && speed->values.size() == 3 && speed->value == "normal", "a SET_VARIABLES option: its label, values and default");

	ps5libretro::Pad pads[ps5libretro::kPlayers];
	pads[0].buttons = (1 << RETRO_DEVICE_ID_JOYPAD_START) | (1 << RETRO_DEVICE_ID_JOYPAD_R2);
	pads[0].left[0] = -12345;
	pads[1].buttons = 1 << RETRO_DEVICE_ID_JOYPAD_A;
	ps5libretro::Run(pads);
	Check(std::string(testcore::optionValue ? testcore::optionValue : "") == "warm" && !testcore::updated, "the core reads an option's value");
	Check(testcore::readMask == pads[0].buttons && testcore::readA == 1 && testcore::readStickX == -12345 && testcore::readAnalogButton == 0x7fff,
		"the pads: a player's mask, another's button, a stick, a button as an analog one");

	const auto& picture = ps5libretro::LastPicture();
	Check(picture.width == 4 && picture.height == 2 && picture.pixels[0] == 0xffff0000u && picture.pixels[1] == 0xff00ff00u &&
		picture.pixels[2] == 0xff0000ffu && picture.pixels[3] == 0xffffffffu && picture.pixels[4] == 0xff000000u,
		"RGB565 with padded rows, in the screens' 32 bits");
	testcore::format = RETRO_PIXEL_FORMAT_0RGB1555;
	ps5libretro::Run(pads);
	Check(picture.pixels[0] == 0xffff0000u && picture.pixels[1] == 0xff00ff00u && picture.pixels[3] == 0xffffffffu, "0RGB1555");
	testcore::format = RETRO_PIXEL_FORMAT_XRGB8888;
	ps5libretro::Run(pads);
	Check(picture.pixels[0] == 0xffff0000u && picture.pixels[2] == 0xff0000ffu && picture.pixels[4] == 0xff123456u, "XRGB8888, its rows longer than shown");
	const uint64_t shown = picture.frame;
	testcore::duplicate = true;
	ps5libretro::Run(pads);
	Check(picture.frame == shown && picture.pixels[0] == 0xffff0000u, "a duplicated frame keeps the picture before");
	testcore::duplicate = false;
	const auto& sound = ps5libretro::Sound();
	Check(sound.size() == 10 && sound[0] == 1 && sound[7] == -4 && sound[8] == 5 && sound[9] == -5, "a frame's sound: the batch, then a single sample");

	Check(ps5libretro::SetOption("test_palette", "cool") && !ps5libretro::SetOption("test_palette", "purple") && !ps5libretro::SetOption("nothing", "on"),
		"an option set to one of its values, and nothing else");
	ps5libretro::Run(pads);
	Check(testcore::updated && std::string(testcore::optionValue) == "cool", "the core is told an option changed, and reads the new value");
	ps5libretro::Run(pads);
	Check(!testcore::updated, "told once");

	// the save RAM: written once it holds still, not on every frame the game writes it
	testcore::saveRam[100] = 7;
	ps5libretro::Run(pads);
	Check(Read(root / "saves/Test Game.srm").size() == 16, "a save RAM change is not written at once");
	Frames(70, pads);
	auto saved = Read(root / "saves/Test Game.srm");
	Check(saved.size() == 256 && saved[0] == 0x42 && saved[100] == 7, "it is written whole once it held still");
	Check(!fs::exists(root / "saves/Test Game.srm.new"), "written beside it, then put in its place");
	testcore::saveRam[101] = 9;
	ps5libretro::Run(pads);

	// save states
	testcore::counter = 1000;
	Check(ps5libretro::SaveState(1, error) && fs::file_size(ps5libretro::StatePath(1)) == sizeof(uint32_t), "a save state to slot 1");
	Check(ps5libretro::StatePath(1) == (root / "states/Test Game.state1").string(), "named <states>/<game>.state<slot>");
	Frames(5, pads);
	Check(!ps5libretro::UndoAvailable() && ps5libretro::LoadState(1, error) && testcore::counter == 1000, "loaded back");
	Check(ps5libretro::UndoAvailable() && ps5libretro::UndoLoad(error) && testcore::counter == 1005 && !ps5libretro::UndoAvailable(),
		"the load undone: the game as it was just before");
	Check(!ps5libretro::LoadState(4, error) && error.find("state4") != std::string::npos && testcore::counter == 1005, "an empty slot is not loaded");
	ps5libretro::Reset();
	Check(testcore::counter == 0, "reset");

	// what the core asks for
	testcore::log.log(RETRO_LOG_INFO, "a line from the core %d\n", 5);
	retro_message message{"Disk 2 inserted", 120};
	testcore::environment(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
	std::string taken;
	Check(ps5libretro::TakeMessage(taken) && taken == "Disk 2 inserted" && !ps5libretro::TakeMessage(taken), "a message for the player, taken once");
	testcore::rumble.set_rumble_state(1, RETRO_RUMBLE_STRONG, 40000);
	uint16_t strong = 0, weak = 1;
	ps5libretro::Rumble(1, strong, weak);
	Check(strong == 40000 && weak == 0 && !testcore::rumble.set_rumble_state(9, RETRO_RUMBLE_WEAK, 1), "rumble for a player's controller");
	retro_hw_render_callback hardware{};
	Check(!testcore::environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &hardware), "a GPU of the core's own is refused (software cores only)");
	testcore::environment(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
	Check(ps5libretro::TakeShutdown() && !ps5libretro::TakeShutdown(), "the core's shutdown, taken once");

	// quitting writes what is still to be written
	ps5libretro::Unload();
	saved = Read(root / "saves/Test Game.srm");
	Check(!ps5libretro::Loaded() && saved.size() == 256 && saved[101] == 9 && testcore::deinits == testcore::inits, "unloading writes the save and stops the core");

	// a core that wants the file's path, not its bytes; and again after the first: one core, one game at a time
	testcore::fullpath = true;
	Check(ps5libretro::Load(testcore::core, game.string(), setup, error) && testcore::given.empty() && testcore::givenPath == game.string() &&
		testcore::saveRam[101] == 9, "a core that reads the game itself has its path, and the save comes back");
	ps5libretro::Unload();
	return failures ? 1 : 0;
}
