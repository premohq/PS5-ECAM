// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: glibc's <endian.h> for the libretro cores (tools/build-libretro.py puts this folder first):
// the PS5's C library is FreeBSD's, whose byte order is in <sys/endian.h> with other names.
#pragma once

#include <sys/endian.h>

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER _BYTE_ORDER
#endif
