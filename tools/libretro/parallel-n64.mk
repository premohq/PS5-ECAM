# SPDX-License-Identifier: GPL-3.0-or-later
# ParaLLEl-N64's sources and flags as its own Makefile.common has them for a core on the CPU, which
# tools/build-libretro.py reads (`make -s -f <this> list` and `flags`, in the core's folder): no
# OpenGL, no ParaLLEl-RDP or -RSP (Vulkan), Angrylion's renderer on threads of its own. As PS5N64X
# (Yahballon) builds it for the PS5: n64/tools/core-sources.mk there.
ROOT_DIR := .
HAVE_OPENGL := 0
HAVE_PARALLEL := 0
HAVE_PARALLEL_RSP := 0
HAVE_THR_AL := 1
STATIC_LINKING := 1
platform := ps5
include Makefile.common
.PHONY: list flags
list:
	@printf '%s\n' $(SOURCES_CXX) $(SOURCES_C)
flags:
	@printf '%s\n' $(CFLAGS) $(INCFLAGS) $(DYNAFLAGS) -D__LIBRETRO__ -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES -DHAVE_POSIX_MEMALIGN -DINLINE=inline -DNDEBUG
