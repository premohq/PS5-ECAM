// SPDX-License-Identifier: GPL-3.0-or-later
// Why Eden could not load a game, from the words its program fails with (ProsperoEdenX's
// headless/main.cpp): "Loader status N. Check this ROM, its keys and firmware; see stderr.log.", N
// being Eden's Core::SystemResultStatus. A loader's own failure is ErrorLoader (7) plus its
// Loader::ResultStatus (src/core/loader/loader.h, core.cpp's Load), so the keys' failures are 20 to
// 30. Players read only the number (issue 108: Super Mario Bros. Wonder, Princess Peach Showtime and
// Kirby's Return to Dream Land Deluxe at 28, ErrorMissingKeyAreaKey, where older games started: keys
// dumped before the system version those games were made for).

#pragma once

#include <string_view>

namespace ps5eden
{
	enum class LoadProblem
	{
		None,		// not a loader's failure, or one that is not about the keys
		NoKeys,		// no prod.keys at all
		KeysTooOld, // prod.keys has no keys for the game's (or its update's) generation of them
		KeysWrong,	// keys missing from or wrong in prod.keys, or a damaged file
		NoTicket,	// the game's title key: its file came without its ticket, and title.keys has none
	};

	inline LoadProblem LoadStatusProblem(std::string_view error)
	{
		constexpr std::string_view prefix = "Loader status ";
		if (error.substr(0, prefix.size()) != prefix)
			return LoadProblem::None;
		const std::string_view number = error.substr(prefix.size(), error.find('.') - prefix.size());
		if (number.empty() || number.size() > 3 || number.find_first_not_of("0123456789") != std::string_view::npos)
			return LoadProblem::None;
		int status = 0;
		for (const char digit : number)
			status = status * 10 + (digit - '0');
		constexpr int kErrorLoader = 7;
		switch (status - kErrorLoader)
		{
		case 13: // ErrorMissingProductionKeyFile
			return LoadProblem::NoKeys;
		case 19: // ErrorMissingTitlekek
		case 21: // ErrorMissingKeyAreaKey
			return LoadProblem::KeysTooOld;
		case 18: // ErrorMissingTitlekey
			return LoadProblem::NoTicket;
		case 14: // ErrorMissingHeaderKey
		case 15: // ErrorIncorrectHeaderKey
		case 20: // ErrorInvalidRightsID
		case 22: // ErrorIncorrectKeyAreaKey
		case 23: // ErrorIncorrectTitlekeyOrTitlekek
			return LoadProblem::KeysWrong;
		default:
			return LoadProblem::None;
		}
	}
}
