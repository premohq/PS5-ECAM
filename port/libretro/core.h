// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a libretro core, as the app links it. tools/build-libretro.py makes each core one object
// whose entry points are <core>_retro_* (mesen_retro_run, mgba_retro_load_game); cores.cpp names them
// for each core the registry knows (ps5retro::kSystems' core), and Find gives the ones this build has.
#pragma once

#include <libretro.h>

#include <cstddef>
#include <string_view>

namespace ps5libretro
{
	struct Core
	{
		const char* name; // its name in the registry and in build/libretro ("mesen")
		void (*init)();
		void (*deinit)();
		unsigned (*apiVersion)();
		void (*getSystemInfo)(retro_system_info* info);
		void (*getSystemAvInfo)(retro_system_av_info* info);
		void (*setEnvironment)(retro_environment_t callback);
		void (*setVideoRefresh)(retro_video_refresh_t callback);
		void (*setAudioSample)(retro_audio_sample_t callback);
		void (*setAudioSampleBatch)(retro_audio_sample_batch_t callback);
		void (*setInputPoll)(retro_input_poll_t callback);
		void (*setInputState)(retro_input_state_t callback);
		void (*setControllerPortDevice)(unsigned port, unsigned device);
		void (*reset)();
		void (*run)();
		size_t (*serializeSize)();
		bool (*serialize)(void* data, size_t size);
		bool (*unserialize)(const void* data, size_t size);
		bool (*loadGame)(const retro_game_info* game);
		void (*unloadGame)();
		void* (*getMemoryData)(unsigned id);
		size_t (*getMemorySize)(unsigned id);
	};

	// The core of that name, when this build links it (build/libretro/objects.txt); null when not
	const Core* Find(std::string_view name);
}
