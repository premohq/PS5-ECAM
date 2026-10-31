// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a libretro core's game (session.h). A core calls back through plain functions with no
// pointer of the frontend's, so what they need is this file's one session, as RetroArch keeps its.
#include "session.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace ps5libretro
{
	namespace
	{
		// a game the core is handed whole (need_fullpath false) is read into memory; one larger is not
		// one of these systems'
		constexpr uintmax_t kLargestGame = 512ull << 20;
		// the save RAM is written once it has held still this many frames (a second at 60), so a game
		// that writes it a byte at a time is written once
		constexpr int kSaveSettleFrames = 60;

		struct State
		{
			const Core* core = nullptr;
			bool loaded = false;
			Setup setup;
			std::string game, stem;
			std::vector<uint8_t> data; // the game, when the core takes it from memory
			retro_system_info info{};
			retro_system_av_info av{};
			retro_pixel_format format = RETRO_PIXEL_FORMAT_0RGB1555;
			Pad pads[kPlayers];
			Picture picture;
			std::vector<int16_t> sound;
			uint64_t frames = 0;
			std::vector<Option> options;
			bool optionsChanged = false;
			bool shutdown = false;
			std::vector<std::string> messages;
			uint16_t rumble[kPlayers][2] = {};
			// the save RAM as last written, and the frames it has held still since it last changed
			std::vector<uint8_t> written;
			std::vector<uint8_t> lastSeen;
			int still = 0;
			bool pending = false;
			std::vector<uint8_t> undo;
		};
		State s;
		void (*s_log)(int level, const std::string& line) = nullptr;

		void Log(int level, const std::string& line)
		{
			if (s_log)
				s_log(level, line);
			else
				std::fprintf(stderr, "[libretro] %s\n", line.c_str());
		}

		void RETRO_CALLCONV CoreLog(retro_log_level level, const char* format, ...)
		{
			char line[1024];
			va_list arguments;
			va_start(arguments, format);
			std::vsnprintf(line, sizeof(line), format, arguments);
			va_end(arguments);
			std::string text = line;
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			Log((int)level, (s.core ? std::string(s.core->name) + ": " : std::string()) + text);
		}

		bool RETRO_CALLCONV SetRumble(unsigned port, retro_rumble_effect effect, uint16_t strength)
		{
			if (port >= (unsigned)kPlayers || (effect != RETRO_RUMBLE_STRONG && effect != RETRO_RUMBLE_WEAK))
				return false;
			s.rumble[port][effect == RETRO_RUMBLE_STRONG ? 0 : 1] = strength;
			return true;
		}

		Option* FindOption(const char* key)
		{
			if (!key)
				return nullptr;
			for (Option& option : s.options)
				if (option.key == key)
					return &option;
			return nullptr;
		}

		// An option as the core declares it, its value the side's when that is one of its values
		void AddOption(Option option)
		{
			if (option.key.empty() || option.values.empty())
				return;
			const bool known = std::any_of(option.values.begin(), option.values.end(), [&](const auto& v) { return v.first == option.defaultValue; });
			if (!known)
				option.defaultValue = option.values.front().first;
			option.value = option.defaultValue;
			const auto given = s.setup.options.find(option.key);
			if (given != s.setup.options.end())
				for (const auto& v : option.values)
					if (v.first == given->second)
						option.value = given->second;
			if (Option* old = FindOption(option.key.c_str()))
				*old = std::move(option);
			else
				s.options.push_back(std::move(option));
		}

		// SET_VARIABLES: "Its label; first|second|third", the first the default
		void AddVariables(const retro_variable* variables)
		{
			for (; variables && variables->key; variables++)
			{
				Option option;
				option.key = variables->key;
				const std::string text = variables->value ? variables->value : "";
				const size_t semicolon = text.find("; ");
				option.label = semicolon == std::string::npos ? option.key : text.substr(0, semicolon);
				std::string list = semicolon == std::string::npos ? text : text.substr(semicolon + 2);
				for (size_t start = 0; start <= list.size();)
				{
					const size_t bar = list.find('|', start);
					const std::string value = list.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
					if (!value.empty())
						option.values.emplace_back(value, value);
					if (bar == std::string::npos)
						break;
					start = bar + 1;
				}
				option.defaultValue = option.values.empty() ? "" : option.values.front().first;
				AddOption(std::move(option));
			}
		}

		void Values(const retro_core_option_value* values, Option& option)
		{
			for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && values[i].value; i++)
				option.values.emplace_back(values[i].value, values[i].label ? values[i].label : values[i].value);
		}

		// The launcher's language's words over the English ones, where the core has them
		void Localize(const char* key, const char* label, const char* info, const retro_core_option_value* values)
		{
			Option* option = FindOption(key);
			if (!option)
				return;
			if (label && *label)
				option->label = label;
			if (info && *info)
				option->info = info;
			for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && values[i].value; i++)
				for (auto& v : option->values)
					if (v.first == values[i].value && values[i].label)
						v.second = values[i].label;
		}

		void AddDefinitions(const retro_core_option_definition* definitions, bool local)
		{
			for (; definitions && definitions->key; definitions++)
			{
				if (local)
				{
					Localize(definitions->key, definitions->desc, definitions->info, definitions->values);
					continue;
				}
				Option option;
				option.key = definitions->key;
				option.label = definitions->desc ? definitions->desc : definitions->key;
				option.info = definitions->info ? definitions->info : "";
				Values(definitions->values, option);
				option.defaultValue = definitions->default_value ? definitions->default_value : "";
				AddOption(std::move(option));
			}
		}

		void AddDefinitionsV2(const retro_core_options_v2* options, bool local)
		{
			if (!options)
				return;
			for (const retro_core_option_v2_definition* d = options->definitions; d && d->key; d++)
			{
				if (local)
				{
					Localize(d->key, d->desc, d->info, d->values);
					continue;
				}
				Option option;
				option.key = d->key;
				option.label = d->desc ? d->desc : d->key;
				option.info = d->info ? d->info : "";
				Values(d->values, option);
				option.defaultValue = d->default_value ? d->default_value : "";
				AddOption(std::move(option));
			}
		}

		bool RETRO_CALLCONV Environment(unsigned command, void* data)
		{
			switch (command)
			{
			case RETRO_ENVIRONMENT_GET_CAN_DUPE:
				if (data)
					*(bool*)data = true;
				return true;
			case RETRO_ENVIRONMENT_SET_MESSAGE:
				if (data && ((const retro_message*)data)->msg)
					s.messages.push_back(((const retro_message*)data)->msg);
				return true;
			case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
				if (data)
					*(unsigned*)data = 1;
				return true;
			case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
				if (data && ((const retro_message_ext*)data)->msg)
				{
					const auto* message = (const retro_message_ext*)data;
					if (message->target == RETRO_MESSAGE_TARGET_LOG)
						Log((int)message->level, message->msg);
					else
						s.messages.push_back(message->msg);
				}
				return true;
			case RETRO_ENVIRONMENT_SHUTDOWN:
				s.shutdown = true;
				return true;
			case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
			case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
				if (data)
					*(const char**)data = s.setup.system.c_str();
				return true;
			case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
				if (data)
					*(const char**)data = s.setup.saves.c_str();
				return true;
			case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
			{
				if (!data)
					return false;
				const auto format = *(const retro_pixel_format*)data;
				if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 && format != RETRO_PIXEL_FORMAT_RGB565)
					return false;
				s.format = format;
				return true;
			}
			case RETRO_ENVIRONMENT_GET_VARIABLE:
			{
				auto* variable = (retro_variable*)data;
				const Option* option = variable ? FindOption(variable->key) : nullptr;
				if (!option)
					return false;
				variable->value = option->value.c_str();
				return true;
			}
			case RETRO_ENVIRONMENT_SET_VARIABLES:
				AddVariables((const retro_variable*)data);
				return true;
			case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
				if (data)
					*(bool*)data = s.optionsChanged;
				s.optionsChanged = false;
				return true;
			case RETRO_ENVIRONMENT_SET_VARIABLE:
			{
				const auto* variable = (const retro_variable*)data;
				if (!variable)
					return true;
				return variable->key && variable->value && SetOption(variable->key, variable->value);
			}
			case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
				if (data)
					*(unsigned*)data = 2;
				return true;
			case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
				AddDefinitions((const retro_core_option_definition*)data, false);
				return true;
			case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
			{
				const auto* intl = (const retro_core_options_intl*)data;
				if (intl)
				{
					AddDefinitions(intl->us, false);
					AddDefinitions(intl->local, true);
				}
				return true;
			}
			case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
				AddDefinitionsV2((const retro_core_options_v2*)data, false);
				return true;
			case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
			{
				const auto* intl = (const retro_core_options_v2_intl*)data;
				if (intl)
				{
					AddDefinitionsV2(intl->us, false);
					AddDefinitionsV2(intl->local, true);
				}
				return true;
			}
			case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
			{
				const auto* display = (const retro_core_option_display*)data;
				if (display)
					if (Option* option = FindOption(display->key))
						option->visible = display->visible;
				return true;
			}
			case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
				if (!data)
					return false;
				((retro_log_callback*)data)->log = CoreLog;
				return true;
			case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
				if (!data)
					return false;
				((retro_rumble_interface*)data)->set_rumble_state = SetRumble;
				return true;
			case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
				if (data)
					*(uint64_t*)data = (1ull << RETRO_DEVICE_JOYPAD) | (1ull << RETRO_DEVICE_ANALOG);
				return true;
			case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
				return true;
			case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
				if (data)
					*(unsigned*)data = kPlayers;
				return true;
			case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
				if (data)
					s.av = *(const retro_system_av_info*)data;
				return true;
			case RETRO_ENVIRONMENT_SET_GEOMETRY:
				if (data)
					s.av.geometry = *(const retro_game_geometry*)data;
				return true;
			case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
				if (data)
					*(int*)data = 1 | 2; // video and audio
				return true;
			case RETRO_ENVIRONMENT_GET_USERNAME:
				if (data)
					*(const char**)data = s.setup.user.c_str();
				return true;
			case RETRO_ENVIRONMENT_GET_LANGUAGE:
				if (data)
					*(unsigned*)data = s.setup.language;
				return true;
			// told, and nothing to do: the frontend shows no core's own descriptions or menus
			case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
			case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
			case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
			case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
			case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
			case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
			case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
			case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
			case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
			case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
			case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
				return true;
			default:
				// not this frontend's (a GPU of the core's own, a keyboard, a camera): the core does without
				return false;
			}
		}

		// The core's picture, in the screens' 32 bits a pixel whatever its own format
		void RETRO_CALLCONV Video(const void* data, unsigned width, unsigned height, size_t pitch)
		{
			if (!data || data == RETRO_HW_FRAME_BUFFER_VALID || width == 0 || height == 0 || width > 4096 || height > 4096)
				return; // a duplicate: the last picture stays
			Picture& p = s.picture;
			p.width = width;
			p.height = height;
			p.frame = s.frames;
			p.pixels.resize(size_t(width) * height);
			for (unsigned y = 0; y < height; y++)
			{
				const uint8_t* row = (const uint8_t*)data + y * pitch;
				uint32_t* out = p.pixels.data() + size_t(y) * width;
				if (s.format == RETRO_PIXEL_FORMAT_XRGB8888)
				{
					for (unsigned x = 0; x < width; x++)
					{
						uint32_t pixel;
						std::memcpy(&pixel, row + x * 4, 4);
						out[x] = 0xff000000u | (pixel & 0xffffffu);
					}
					continue;
				}
				for (unsigned x = 0; x < width; x++)
				{
					uint16_t pixel;
					std::memcpy(&pixel, row + x * 2, 2);
					uint32_t r, g, b;
					if (s.format == RETRO_PIXEL_FORMAT_RGB565)
					{
						r = (pixel >> 11) & 31;
						g = (pixel >> 5) & 63;
						b = pixel & 31;
						g = (g << 2) | (g >> 4);
					}
					else
					{
						r = (pixel >> 10) & 31;
						g = (pixel >> 5) & 31;
						b = pixel & 31;
						g = (g << 3) | (g >> 2);
					}
					r = (r << 3) | (r >> 2);
					b = (b << 3) | (b >> 2);
					out[x] = 0xff000000u | (r << 16) | (g << 8) | b;
				}
			}
		}

		void RETRO_CALLCONV Sample(int16_t left, int16_t right)
		{
			s.sound.push_back(left);
			s.sound.push_back(right);
		}

		size_t RETRO_CALLCONV Samples(const int16_t* data, size_t frames)
		{
			if (data)
				s.sound.insert(s.sound.end(), data, data + frames * 2);
			return frames;
		}

		void RETRO_CALLCONV Poll()
		{
		}

		int16_t RETRO_CALLCONV InputState(unsigned port, unsigned device, unsigned index, unsigned id)
		{
			if (port >= (unsigned)kPlayers)
				return 0;
			const Pad& pad = s.pads[port];
			switch (device & RETRO_DEVICE_MASK)
			{
			case RETRO_DEVICE_JOYPAD:
				if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
					return (int16_t)pad.buttons;
				return id < 16 ? (pad.buttons >> id) & 1 : 0;
			case RETRO_DEVICE_ANALOG:
				if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
					return id < 16 && ((pad.buttons >> id) & 1) ? 0x7fff : 0;
				if (id > RETRO_DEVICE_ID_ANALOG_Y)
					return 0;
				if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
					return pad.left[id];
				if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
					return pad.right[id];
				return 0;
			default:
				return 0;
			}
		}

		std::string SavePath(const char* extension)
		{
			return (fs::path(s.setup.saves) / (s.stem + extension)).string();
		}

		bool ReadFile(const std::string& path, std::vector<uint8_t>& data)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return false;
			data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return !in.bad();
		}

		// Written beside it, then put in its place: a quit or a crash midway leaves the one before
		bool WriteFile(const std::string& path, const void* data, size_t size, std::string& error)
		{
			std::error_code ec;
			fs::create_directories(fs::path(path).parent_path(), ec);
			const std::string temporary = path + ".new";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				if (!out || !out.write((const char*)data, (std::streamsize)size) || !out.flush())
				{
					error = "cannot write " + temporary;
					return false;
				}
			}
			fs::rename(temporary, path, ec);
			if (ec)
			{
				error = "cannot replace " + path + ": " + ec.message();
				fs::remove(temporary, ec);
				return false;
			}
			return true;
		}

		// The save RAM (and the clock) from their files, as the game is loaded
		void ReadSave()
		{
			for (const auto& [id, extension] : {std::pair{RETRO_MEMORY_SAVE_RAM, ".srm"}, std::pair{RETRO_MEMORY_RTC, ".rtc"}})
			{
				uint8_t* memory = (uint8_t*)s.core->getMemoryData(id);
				const size_t size = s.core->getMemorySize(id);
				std::vector<uint8_t> file;
				if (!memory || size == 0 || !ReadFile(SavePath(extension), file) || file.empty())
					continue;
				// a save from the same game in another frontend may be larger (padded) or shorter
				std::memcpy(memory, file.data(), std::min(size, file.size()));
				Log(RETRO_LOG_INFO, std::string("save read: ") + SavePath(extension) + " (" + std::to_string(file.size()) + " bytes)");
			}
			const uint8_t* memory = (const uint8_t*)s.core->getMemoryData(RETRO_MEMORY_SAVE_RAM);
			const size_t size = s.core->getMemorySize(RETRO_MEMORY_SAVE_RAM);
			if (memory && size)
				s.written.assign(memory, memory + size);
			s.lastSeen = s.written;
		}

		void Clear()
		{
			const auto log = s_log;
			s = State();
			s_log = log;
		}
	}

	bool Load(const Core& core, const std::string& game, const Setup& setup, std::string& error)
	{
		if (s.loaded)
		{
			error = "a game is loaded already";
			return false;
		}
		Clear();
		s.core = &core;
		s.setup = setup;
		s.game = game;
		s.stem = fs::path(game).stem().string();
		if (core.apiVersion() != RETRO_API_VERSION)
		{
			error = std::string(core.name) + " is libretro API " + std::to_string(core.apiVersion()) + ", not " + std::to_string(RETRO_API_VERSION);
			Clear();
			return false;
		}
		core.setEnvironment(Environment);
		core.init();
		core.setVideoRefresh(Video);
		core.setAudioSample(Sample);
		core.setAudioSampleBatch(Samples);
		core.setInputPoll(Poll);
		core.setInputState(InputState);
		core.getSystemInfo(&s.info);

		retro_game_info info{};
		info.path = s.game.c_str();
		if (!s.info.need_fullpath)
		{
			std::error_code ec;
			const uintmax_t size = fs::file_size(game, ec);
			if (ec || size == 0 || size > kLargestGame || !ReadFile(game, s.data) || s.data.size() != size)
			{
				error = ec ? game + ": " + ec.message() : game + " cannot be read";
				core.deinit();
				Clear();
				return false;
			}
			info.data = s.data.data();
			info.size = s.data.size();
		}
		if (!core.loadGame(&info))
		{
			error = std::string(s.info.library_name ? s.info.library_name : core.name) + " did not load " + fs::path(game).filename().string();
			core.deinit();
			Clear();
			return false;
		}
		core.getSystemAvInfo(&s.av);
		for (unsigned port = 0; port < (unsigned)kPlayers; port++)
			core.setControllerPortDevice(port, RETRO_DEVICE_JOYPAD);
		ReadSave();
		s.loaded = true;
		Log(RETRO_LOG_INFO, CoreName() + ": " + fs::path(game).filename().string() + ", " + std::to_string(s.av.geometry.base_width) + "x" +
			std::to_string(s.av.geometry.base_height) + " at " + std::to_string(s.av.timing.fps) + " fps, sound at " +
			std::to_string(s.av.timing.sample_rate) + " Hz, " + std::to_string(s.options.size()) + " options");
		return true;
	}

	bool Loaded() { return s.loaded; }

	void Run(const Pad (&pads)[kPlayers])
	{
		if (!s.loaded)
			return;
		std::copy(std::begin(pads), std::end(pads), s.pads);
		s.sound.clear();
		s.frames++;
		s.core->run();
		std::string error;
		if (!FlushSave(false, error))
			Log(RETRO_LOG_WARN, error);
	}

	const Picture& LastPicture() { return s.picture; }
	const std::vector<int16_t>& Sound() { return s.sound; }
	double FrameRate() { return s.av.timing.fps > 0 ? s.av.timing.fps : 60.0; }
	double SampleRate() { return s.av.timing.sample_rate > 0 ? s.av.timing.sample_rate : 48000.0; }

	float AspectRatio()
	{
		if (s.av.geometry.aspect_ratio > 0)
			return s.av.geometry.aspect_ratio;
		const unsigned w = s.picture.width ? s.picture.width : s.av.geometry.base_width;
		const unsigned h = s.picture.height ? s.picture.height : s.av.geometry.base_height;
		return w && h ? float(w) / float(h) : 4.0f / 3.0f;
	}

	std::string CoreName()
	{
		if (!s.core)
			return {};
		std::string name = s.info.library_name ? s.info.library_name : s.core->name;
		if (s.info.library_version && *s.info.library_version)
			name += std::string(" ") + s.info.library_version;
		return name;
	}

	const std::vector<Option>& Options() { return s.options; }

	bool SetOption(const std::string& key, const std::string& value)
	{
		Option* option = FindOption(key.c_str());
		if (!option || std::none_of(option->values.begin(), option->values.end(), [&](const auto& v) { return v.first == value; }))
			return false;
		if (option->value != value)
		{
			option->value = value;
			s.optionsChanged = true;
		}
		return true;
	}

	bool FlushSave(bool force, std::string& error)
	{
		if (!s.core || (!s.loaded && !force))
			return true;
		const uint8_t* memory = (const uint8_t*)s.core->getMemoryData(RETRO_MEMORY_SAVE_RAM);
		const size_t size = s.core->getMemorySize(RETRO_MEMORY_SAVE_RAM);
		if (!memory || size == 0)
			return true;
		const bool changed = s.lastSeen.size() != size || std::memcmp(s.lastSeen.data(), memory, size) != 0;
		if (changed)
		{
			s.lastSeen.assign(memory, memory + size);
			s.still = 0;
			s.pending = s.written.size() != size || std::memcmp(s.written.data(), memory, size) != 0;
		}
		else if (s.pending)
			s.still++;
		if (!s.pending || (!force && s.still < kSaveSettleFrames))
			return true;
		if (!WriteFile(SavePath(".srm"), memory, size, error))
			return false;
		s.written.assign(memory, memory + size);
		s.pending = false;
		Log(RETRO_LOG_INFO, "save written: " + SavePath(".srm"));
		if (const uint8_t* clock = (const uint8_t*)s.core->getMemoryData(RETRO_MEMORY_RTC))
			if (const size_t clockSize = s.core->getMemorySize(RETRO_MEMORY_RTC))
				WriteFile(SavePath(".rtc"), clock, clockSize, error);
		return true;
	}

	std::string StatePath(int slot)
	{
		return (fs::path(s.setup.states) / (s.stem + ".state" + std::to_string(slot))).string();
	}

	bool SaveState(int slot, std::string& error)
	{
		if (!s.loaded)
			return false;
		const size_t size = s.core->serializeSize();
		std::vector<uint8_t> state(size);
		if (size == 0 || !s.core->serialize(state.data(), size))
		{
			error = CoreName() + " cannot save a state now";
			return false;
		}
		return WriteFile(StatePath(slot), state.data(), size, error);
	}

	bool LoadState(int slot, std::string& error)
	{
		if (!s.loaded)
			return false;
		std::vector<uint8_t> state;
		if (!ReadFile(StatePath(slot), state) || state.empty())
		{
			error = StatePath(slot) + " cannot be read";
			return false;
		}
		std::vector<uint8_t> before(s.core->serializeSize());
		const bool kept = !before.empty() && s.core->serialize(before.data(), before.size());
		if (!s.core->unserialize(state.data(), state.size()))
		{
			error = CoreName() + " did not take " + StatePath(slot);
			return false;
		}
		if (kept)
			s.undo = std::move(before);
		return true;
	}

	bool UndoAvailable() { return !s.undo.empty(); }

	bool UndoLoad(std::string& error)
	{
		if (!s.loaded || s.undo.empty())
			return false;
		if (!s.core->unserialize(s.undo.data(), s.undo.size()))
		{
			error = CoreName() + " did not take the game as it was";
			return false;
		}
		s.undo.clear();
		return true;
	}

	void Reset()
	{
		if (s.loaded)
			s.core->reset();
	}

	bool TakeShutdown() { return std::exchange(s.shutdown, false); }

	bool TakeMessage(std::string& message)
	{
		if (s.messages.empty())
			return false;
		message = s.messages.front();
		s.messages.erase(s.messages.begin());
		return true;
	}

	void Rumble(int player, uint16_t& strong, uint16_t& weak)
	{
		strong = weak = 0;
		if (player < 0 || player >= kPlayers)
			return;
		strong = s.rumble[player][0];
		weak = s.rumble[player][1];
	}

	void Unload()
	{
		if (!s.loaded)
			return;
		std::string error;
		if (!FlushSave(true, error))
			Log(RETRO_LOG_ERROR, error);
		s.core->unloadGame();
		s.core->deinit();
		Clear();
	}

	void SetLog(void (*log)(int level, const std::string& line)) { s_log = log; }
}
