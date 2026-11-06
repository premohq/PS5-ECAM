// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps5eden::transfer
{
	enum class Kind { Account, Device };
	struct Source { Kind kind; std::string folder; };
	// Detect the selected title only, using the pinned standalone's plain-folder/Ryujinx index
	// readers. No download, conversion or source modification. A Symlinked save is refused.
	struct Choice { uint64_t accountSaveId = 0; std::string label; };
	struct FolderChoice { std::string folder; std::string profileId; };
	// Ryujinx account save IDs are stable selectors, not display names or Eden player IDs.
	std::vector<Choice> Choices(const std::string& root, uint64_t title, std::string& error);
	// Account saves in the standalone ProsperoEden user/save tree, selected by profile folder.
	std::vector<FolderChoice> ProsperoEdenChoices(const std::string& root, uint64_t title, std::string& error);
	std::vector<Source> Discover(const std::string& root, uint64_t title, std::string& error, uint64_t accountSaveId = 0);
	// Explicit destinations; caller resolves the selected Eden player. All source trees are
	// staged and checked before replacing either destination. Old directories remain in recovery.
	// Must run while the emulator is stopped. These synchronous helpers belong on a worker thread.
	bool Import(const std::vector<Source>& sources, const std::string& account, const std::string& device,
		const std::string& recovery, std::string& kept, std::string& error);
	// A new target only, containing account/ and/or device/. Never overwrites an export.
	bool Export(const std::string& account, const std::string& device, const std::string& target, std::string& error);
}
