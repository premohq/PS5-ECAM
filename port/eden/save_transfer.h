// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps5eden::transfer
{
	enum class Kind { Account, Device };
	struct Source { Kind kind; std::string folder; };
	// Standalone ProsperoEden's Eden user folder on this console (its storage_paths.h: /data/prosperoeden,
	// and user/ in it), whose players' saves a game can take (issue 104)
	inline constexpr const char* kStandaloneUser = "/data/prosperoeden/user";
	// Detect the selected title only, using the pinned standalone's plain-folder/Ryujinx index
	// readers. No download, conversion or source modification. A Symlinked save is refused.
	// folder, when not empty, is what Discover takes the choice from instead of the root asked: an
	// Eden user folder's player (nand/user/save/0000000000000000/<player ID>), named in the label.
	struct Choice { uint64_t accountSaveId = 0; std::string label, folder; };
	// Ryujinx account save IDs are stable selectors, not display names or Eden player IDs.
	std::vector<Choice> Choices(const std::string& root, uint64_t title, std::string& error);
	std::vector<Source> Discover(const std::string& root, uint64_t title, std::string& error, uint64_t accountSaveId = 0);
	// Explicit destinations; caller resolves the selected Eden player. All source trees are
	// staged and checked before replacing either destination. Old directories remain in recovery.
	// Must run while the emulator is stopped. These synchronous helpers belong on a worker thread.
	bool Import(const std::vector<Source>& sources, const std::string& account, const std::string& device,
		const std::string& recovery, std::string& kept, std::string& error);
	// A new target only, containing account/ and/or device/. Never overwrites an export.
	bool Export(const std::string& account, const std::string& device, const std::string& target, std::string& error);
}
