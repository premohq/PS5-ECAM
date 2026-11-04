// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the cores the registry names (ps5retro::kSystems' core), by the entry points
// tools/build-libretro.py gives them. Each is a weak reference: a core this build does not link is
// null, and Find leaves it out, so the app links with any of them or none (hidden: resolved to null
// when the app is linked, with nothing left for the PS5's loader).
#include "core.h"
#include "plays.h"
#include "../app/paths.h"
#include "../retro/registry.h"

#include <filesystem>
#include <string_view>
#include <utility>

#define PS5LIBRETRO_CORE(core) \
	extern "C" { \
	void core##_retro_init(void) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_deinit(void) __attribute__((weak, visibility("hidden"))); \
	unsigned core##_retro_api_version(void) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_get_system_info(retro_system_info*) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_get_system_av_info(retro_system_av_info*) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_environment(retro_environment_t) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_video_refresh(retro_video_refresh_t) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_audio_sample(retro_audio_sample_t) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_audio_sample_batch(retro_audio_sample_batch_t) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_input_poll(retro_input_poll_t) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_input_state(retro_input_state_t) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_set_controller_port_device(unsigned, unsigned) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_reset(void) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_run(void) __attribute__((weak, visibility("hidden"))); \
	size_t core##_retro_serialize_size(void) __attribute__((weak, visibility("hidden"))); \
	bool core##_retro_serialize(void*, size_t) __attribute__((weak, visibility("hidden"))); \
	bool core##_retro_unserialize(const void*, size_t) __attribute__((weak, visibility("hidden"))); \
	bool core##_retro_load_game(const retro_game_info*) __attribute__((weak, visibility("hidden"))); \
	void core##_retro_unload_game(void) __attribute__((weak, visibility("hidden"))); \
	void* core##_retro_get_memory_data(unsigned) __attribute__((weak, visibility("hidden"))); \
	size_t core##_retro_get_memory_size(unsigned) __attribute__((weak, visibility("hidden"))); \
	}

#define PS5LIBRETRO_ENTRY(core) \
	ps5libretro::Core{#core, core##_retro_init, core##_retro_deinit, core##_retro_api_version, core##_retro_get_system_info, \
		core##_retro_get_system_av_info, core##_retro_set_environment, core##_retro_set_video_refresh, core##_retro_set_audio_sample, \
		core##_retro_set_audio_sample_batch, core##_retro_set_input_poll, core##_retro_set_input_state, \
		core##_retro_set_controller_port_device, core##_retro_reset, core##_retro_run, core##_retro_serialize_size, core##_retro_serialize, \
		core##_retro_unserialize, core##_retro_load_game, core##_retro_unload_game, core##_retro_get_memory_data, core##_retro_get_memory_size}

// tools/build-libretro.py's cores (its cores(), and ps5retro::kSystems' core)
PS5LIBRETRO_CORE(mesen)
PS5LIBRETRO_CORE(mesens)
PS5LIBRETRO_CORE(mgba)
PS5LIBRETRO_CORE(beetlevb)
PS5LIBRETRO_CORE(pokemini)
PS5LIBRETRO_CORE(paralleln64)
PS5LIBRETRO_CORE(gw)

namespace ps5libretro
{
	namespace
	{
		const Core kCores[] = {
			PS5LIBRETRO_ENTRY(mesen),
			PS5LIBRETRO_ENTRY(mesens),
			PS5LIBRETRO_ENTRY(mgba),
			PS5LIBRETRO_ENTRY(beetlevb),
			PS5LIBRETRO_ENTRY(pokemini),
			PS5LIBRETRO_ENTRY(paralleln64),
			PS5LIBRETRO_ENTRY(gw),
		};

		// a side's program of its own (PR #87's and #91's), when this copy of the app has it
		bool Packaged(const char* runtime)
		{
			std::error_code error;
			return *runtime && std::filesystem::is_regular_file(ps5paths::AppDir() + "/" + runtime, error);
		}

		// every entry point there: a core is linked whole or not at all, but a half one is never run
		bool Linked(const Core& c)
		{
			return c.init && c.deinit && c.apiVersion && c.getSystemInfo && c.getSystemAvInfo && c.setEnvironment && c.setVideoRefresh &&
				c.setAudioSample && c.setAudioSampleBatch && c.setInputPoll && c.setInputState && c.setControllerPortDevice && c.reset &&
				c.run && c.serializeSize && c.serialize && c.unserialize && c.loadGame && c.unloadGame && c.getMemoryData && c.getMemorySize;
		}
	}

	const Core* Find(std::string_view name)
	{
		for (const Core& core : kCores)
			if (name == core.name)
				return Linked(core) ? &core : nullptr;
		return nullptr;
	}

	bool Plays(int side)
	{
		if (!ps5retro::IsRetro(side))
			return false;
		const ps5retro::System& system = ps5retro::Of(side);
		// a program of its own that is not built here (the NES's, the Game Boys', the Nintendo 64's)
		// gives way to the core; one that is stays the side's until the core is measured against it
		return *system.core && Find(system.core) && !Packaged(system.runtime);
	}

	const char* CoreTitle(int side)
	{
		static constexpr std::pair<std::string_view, const char*> kTitles[] = {{"mesen", "Mesen"}, {"mesens", "Mesen-S"}, {"mgba", "mGBA"},
			{"beetlevb", "Beetle VB"}, {"pokemini", "PokeMini"}, {"paralleln64", "ParaLLEl-N64"}};
		if (ps5retro::IsRetro(side))
			for (const auto& [core, title] : kTitles)
				if (core == ps5retro::Of(side).core)
					return title;
		return "";
	}
}
