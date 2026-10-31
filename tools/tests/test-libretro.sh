#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The app's libretro frontend (port/libretro/session.cpp, cores.cpp) with a core of the test's own.
# Needs a C++20 compiler and libretro-common's libretro.h (python3 -B tools/deps.py fetch
# libretro-common). Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
include=${ECAM_LIBRETRO_INCLUDE:-$root/.deps/libretro/libretro-common/include}
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" -I"$include" \
 "$root/tools/tests/test-libretro.cpp" "$root/port/libretro/session.cpp" "$root/port/libretro/cores.cpp" -o "$work/test-libretro"
"$work/test-libretro" "$work/files"
# a core linked in, as build/libretro/<core>.o is: its renamed entry points found through the same
# weak references, in a position-independent executable as the PS5's is
{
    echo '#include <libretro.h>'
    for f in init deinit reset run unload_game; do echo "void pokemini_retro_$f(void) {}"; done
    echo 'unsigned pokemini_retro_api_version(void) { return RETRO_API_VERSION; }'
    for f in get_system_info:retro_system_info get_system_av_info:retro_system_av_info; do echo "void pokemini_retro_${f%%:*}(struct ${f#*:}* i) { (void)i; }"; done
    for f in environment video_refresh audio_sample audio_sample_batch input_poll input_state; do echo "void pokemini_retro_set_$f(retro_${f}_t c) { (void)c; }"; done
    echo 'void pokemini_retro_set_controller_port_device(unsigned p, unsigned d) { (void)p; (void)d; }'
    echo 'size_t pokemini_retro_serialize_size(void) { return 0; }'
    echo 'bool pokemini_retro_serialize(void* d, size_t s) { (void)d; (void)s; return false; }'
    echo 'bool pokemini_retro_unserialize(const void* d, size_t s) { (void)d; (void)s; return false; }'
    echo 'bool pokemini_retro_load_game(const struct retro_game_info* g) { (void)g; return false; }'
    echo 'void* pokemini_retro_get_memory_data(unsigned i) { (void)i; return 0; }'
    echo 'size_t pokemini_retro_get_memory_size(unsigned i) { (void)i; return 0; }'
} > "$work/pokemini.c"
cat > "$work/linked.cpp" <<'CPP'
#include "libretro/core.h"
#include <cstdio>
int main()
{
	const bool ok = ps5libretro::Find("pokemini") && ps5libretro::Find("pokemini")->run && !ps5libretro::Find("mesen");
	std::printf("%s a core linked in is found by its renamed entry points, the others still not\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
CPP
"${CC:-cc}" -std=c11 -fPIC -I"$include" -c "$work/pokemini.c" -o "$work/pokemini.o"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -fPIE -pie -I"$root/port" -I"$include" "$work/linked.cpp" "$root/port/libretro/cores.cpp" \
 "$work/pokemini.o" -o "$work/linked"
"$work/linked"
# the PS5's /data refuses lstat to an app without a HEN: the frontend's files do without it
if [[ $(uname -s) == Linux ]]; then
    "${CC:-cc}" -shared -fPIC "$root/tools/tests/refuse-lstat.c" -o "$work/refuse-lstat.so" -ldl
    echo "lstat refused with EPERM, as /data is in an app without a HEN:"
    REFUSE_LSTAT_UNDER="$work" LD_PRELOAD="$work/refuse-lstat.so" "$work/test-libretro" "$work/refused-files"
fi
