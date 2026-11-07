// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden/save_transfer.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace ps5eden::transfer;
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
static void Put(const fs::path& path, const std::string& text) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text; }
static std::string Get(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	fs::create_directories(argv[1]);
	const auto root = fs::canonical(argv[1]);
	const auto input = root / "input";
	const std::string title = "0100ABCDEF123000";
	const uint64_t id = 0x0100ABCDEF123000;
	Put(input / title / "account/main", "new account");
	Put(input / title / "device/main", "new device");
	Put(input / "0100ABCDEF124000/account/other", "other title");
	std::string error, kept;
	auto sources = Discover(input.string(), id, error);
	Check(sources.size() == 2 && error.empty(), "discover only requested title in split folder layout");
	const auto account = root / "nand/player/title", device = root / "nand/zero/title", recovery = root / "recovery";
	Put(account / "main", "old account"); Put(account / "stale", "old leftover"); Put(device / "main", "old device");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error), "account and device imported together");
	Check(Get(account / "main") == "new account" && Get(device / "main") == "new device" && !fs::exists(account / "stale"), "replace complete directories instead of merging stale files");
	Check(Get(fs::path(kept) / "account.before/main") == "old account" && Get(fs::path(kept) / "device.before/main") == "old device", "both prior saves retained");
	Check(Get(input / title / "account/main") == "new account", "source never modified");
	Check(Export(account.string(), device.string(), (root / "export" / title).string(), error), "plain-folder export");
	Check(Discover((root / "export").string(), id, error).size() == 2, "export is discoverable as a new import");
	Check(!Export(account.string(), device.string(), (root / "export" / title).string(), error) && Get(root / "export" / title / "account/main") == "new account", "existing exports never replaced");
	const auto firstBefore = Get(account / "main");
	const auto malformed = std::vector<Source>{{Kind::Account, (input / title / "account").string()}, {Kind::Device, (root / "missing").string()}};
	Check(!Import(malformed, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == firstBefore, "failure in second source leaves first destination unchanged");
	Check(!Import({sources[0], sources[0]}, account.string(), device.string(), recovery.string(), kept, error), "duplicate save types rejected");
	Check(!Import(sources, account.string(), account.string(), recovery.string(), kept, error), "overlapping destinations rejected");
	Check(!Import({{Kind::Account, account.string()}}, account.string(), device.string(), recovery.string(), kept, error), "source destination overlap rejected");
	fs::create_directory(root / "empty");
	Check(!Import({{Kind::Account, (root / "empty").string()}}, account.string(), device.string(), recovery.string(), kept, error), "empty save cannot erase existing files");
	fs::create_symlink(input / title / "account/main", input / title / "account/linked");
	Check(!Import(sources, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == firstBefore, "symlink inside source refused before replacement");
	fs::remove(input / title / "account/linked");
	fs::create_directory_symlink(input, root / "linked-root");
	Check(Discover((root / "linked-root").string(), id, error).empty(), "symlinked source ancestor refused");
	Check(Discover(input.string(), 0x5A12345678901234, error).empty(), "synthetic title rejected");
	// Standalone Ryujinx index layout, generated synthetic fixture; no copyrighted content.
	std::array<char, 12 + 0x8C * 2> index{};
	std::memcpy(index.data(), "IMKV", 4);
	for (int i = 0; i < 2; i++)
	{
		char* entry = index.data() + 12 + i * 0x8C;
		std::memcpy(entry, "IMEN", 4); std::memcpy(entry + 0x0C, &id, 8);
		entry[0x2C] = i ? 3 : 1;
		const uint64_t saveId = i + 1;
		std::memcpy(entry + 0x4C, &saveId, 8);
	}
	const auto ryujinx = root / "ryujinx";
	Put(ryujinx / "bis/system/save/8000000000000000/0/imkvdb.arc", std::string(index.data(), index.size()));
	Put(ryujinx / "bis/user/save/0000000000000001/0/main", "ryu account");
	Put(ryujinx / "bis/user/save/0000000000000002/0/main", "ryu device");
	sources = Discover(ryujinx.string(), id, error);
	Check(sources.size() == 2, "Ryujinx index identifies account and device by program ID");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == "ryu account" && Get(device / "main") == "ryu device", "Ryujinx transfer uses exact indexed folders");
	Check(Choices(input.string(), id, error).size() == 1, "plain folder retains a single source choice");
	std::string multiple(index.data(), index.size());
	std::string third(index.data() + 12, 0x8C);
	const uint64_t thirdId = 3;
	std::memcpy(third.data() + 0x4C, &thirdId, 8); multiple += third;
	const auto indexPath = ryujinx / "bis/system/save/8000000000000000/0/imkvdb.arc";
	Put(indexPath, multiple);
	Put(ryujinx / "bis/user/save/0000000000000003/0/main", "second player");
	const auto choices = Choices(ryujinx.string(), id, error);
	Check(choices.size() == 2 && choices[0].accountSaveId == 1 && choices[1].accountSaveId == 3, "all Ryujinx account saves get stable source choices");
	Check(Discover(ryujinx.string(), id, error).empty() && !error.empty(), "ambiguous account import requires explicit choice");
	sources = Discover(ryujinx.string(), id, error, 3);
	Check(sources.size() == 2 && error.empty(), "chosen second account includes shared device save");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == "second player" && Get(device / "main") == "ryu device", "selected account alone replaces destination with retained recovery");
	Put(indexPath, std::string(index.data(), index.size()));
	Check(Discover(ryujinx.string(), id, error, 3).empty(), "vanished selected entry never falls back to another account");
	fs::rename(ryujinx / "bis", ryujinx / "missing-bis");
	Check(Discover(ryujinx.string(), id, error, 3).empty(), "vanished index never falls back to plain folder");
	fs::rename(ryujinx / "missing-bis", ryujinx / "bis");
	std::string deviceOnly(index.data(), 12); deviceOnly.append(index.data() + 12 + 0x8C, 0x8C);
	Put(indexPath, deviceOnly);
	Check(Choices(ryujinx.string(), id, error).size() == 1 && Discover(ryujinx.string(), id, error).size() == 1, "device-only index remains importable");
	std::string zero(index.data(), index.size()); std::memset(zero.data() + 12 + 0x4C, 0, 8); Put(indexPath, zero);
	Check(Choices(ryujinx.string(), id, error).empty(), "zero source save ID cannot bypass explicit choice");
	Put(indexPath, multiple + third);
	Check(Choices(ryujinx.string(), id, error).empty(), "duplicate save IDs refused rather than presented twice");
	Put(indexPath, std::string(index.data(), index.size()));
	fs::create_directory_symlink(ryujinx, root / "ryu-linked");
	Check(Choices((root / "ryu-linked").string(), id, error).empty(), "choice discovery rejects symlinked roots");
	Put(ryujinx / "bis/system/save/8000000000000000/0/imkvdb.arc", "bad index");
	Check(Discover(ryujinx.string(), id, error).empty(), "malformed Ryujinx index refused");
	// A standalone ProsperoEden user/save tree: one title folder below each 32-hex profile ID.
	const auto standalone = root / "prosperoeden" / "user/nand/user/save/0000000000000000";
	const std::string profileA = "00112233445566778899AABBCCDDEEFF";
	const std::string profileB = "FFEEDDCCBBAA99887766554433221100";
	Put(standalone / profileA / "0100ABCDEF123000/main", "standalone player A");
	Put(standalone / profileB / "0100ABCDEF124000/main", "other game");
	const auto standaloneChoices = ProsperoEdenChoices(standalone.string(), id, error);
	Check(standaloneChoices.size() == 1 && standaloneChoices[0].profileId == profileA,
		"standalone ProsperoEden discovery lists only profiles with this game's save");
	sources = standaloneChoices.empty() ? std::vector<Source>{} : Discover(standaloneChoices[0].folder, id, error);
	Check(sources.size() == 1 && sources[0].kind == Kind::Account, "standalone profile save is imported as the account save");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error) &&
		Get(account / "main") == "standalone player A" && Get(device / "main") == "ryu device",
		"standalone ProsperoEden account save imports without changing the device save");
	Check(Get(standalone / profileA / "0100ABCDEF123000/main") == "standalone player A",
		"standalone ProsperoEden source remains unchanged");
	fs::create_directory_symlink(standalone / profileA, standalone / "11112222333344445555666677778888");
	Check(ProsperoEdenChoices(standalone.string(), id, error).size() == 1,
		"standalone save discovery skips symlinked profile folders");
	return failures ? 1 : 0;
}
