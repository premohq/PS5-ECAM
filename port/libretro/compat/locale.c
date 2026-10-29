// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: POSIX 2008's locale objects for mGBA's core (tools/build-libretro.py compiles this into it,
// where its symbols are its own): the PS5's C library declares locale_t but has no newlocale or
// uselocale. Its numbers are always formatted as the "C" locale's, the one mGBA asks for, so a locale
// object stands for that one alone.
#include <locale.h>

static struct _xlocale* const kC = (struct _xlocale*)1;

locale_t newlocale(int mask, const char* name, locale_t base)
{
	(void)mask, (void)name, (void)base;
	return kC;
}

locale_t uselocale(locale_t locale)
{
	(void)locale;
	return kC;
}

int freelocale(locale_t locale) // FreeBSD's returns an int
{
	(void)locale;
	return 0;
}
