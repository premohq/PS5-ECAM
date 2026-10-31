// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the cores the registry names (ps5retro::kSystems' core), by the entry points
// tools/build-libretro.py gives them. Each is a weak reference: a core this build does not link is
// null, and Find leaves it out, so the app links with any of them or none (hidden: resolved to null
// when the app is linked, with nothing left for the PS5's loader).
#include "core.h"
#include "plays.h"
#include "../retro/registry.h"

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
		};

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
		return !*system.runtime && *system.core && Find(system.core);
	}
}
