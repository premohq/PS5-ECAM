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
	// Standalone ProsperoEden's Eden user folder (issue 104): a player's save under its ID, the device save
	// under zeros, the players' names in Eden's player list (synthetic fixture)
	const auto user = root / "prosperoeden/user";
	const auto players = user / "nand/user/save/0000000000000000";
	const std::string alice = "0123456789ABCDEF0011223344556677", bob = "FEDCBA98765432100011223344556677", zeros(32, '0');
	std::array<char, 0x10 + 8 * 0xC8> list{};
	const uint64_t aliceHigh = 0x0123456789ABCDEF, aliceLow = 0x0011223344556677;
	std::memcpy(list.data() + 0x10, &aliceLow, 8); std::memcpy(list.data() + 0x18, &aliceHigh, 8);
	std::memcpy(list.data() + 0x10 + 0x28, "Alice", 5);
	Put(user / "nand/system/save/8000000000000010/su/avators/profiles.dat", std::string(list.data(), list.size()));
	Put(players / alice / title / "main", "alice save");
	Put(players / bob / "0100abcdef123000/main", "bob save");
	Put(players / bob / "0100ABCDEF124000/main", "another game");
	Put(players / zeros / title / "main", "eden device");
	auto picks = Choices(user.string(), id, error);
	Check(picks.size() == 2 && picks[0].label == "Alice" && picks[1].label == "Player " + bob && picks[0].folder == (players / alice).string(),
		"a choice for each ProsperoEden player with the game's save, by name, else by ID (title ID in either case)");
	Check(Discover(user.string(), id, error).empty() && !error.empty(), "two players' saves need a choice first");
	sources = picks.size() == 2 ? Discover(picks[0].folder, id, error) : std::vector<Source>{};
	Check(sources.size() == 2 && sources[0].kind == Kind::Account && sources[0].folder == (players / alice / title).string() &&
			  sources[1].kind == Kind::Device && sources[1].folder == (players / zeros / title).string(),
		"the chosen player's save and the game's device save");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == "alice save" &&
			  Get(device / "main") == "eden device" && Get(players / alice / title / "main") == "alice save",
		"ProsperoEden's save taken, its own left as it was");
	sources = picks.size() == 2 ? Discover(picks[1].folder, id, error) : std::vector<Source>{};
	Check(sources.size() == 2 && sources[0].folder == (players / bob / "0100abcdef123000").string(), "the other player's, in small letters");
	Check(Discover(picks.empty() ? std::string() : picks[0].folder, id, error, 1).empty(), "no Ryujinx save ID for a ProsperoEden player");
	Check(Choices(user.string(), 0x0100ABCDEF125000, error).empty() && !error.empty(), "a game with no ProsperoEden save");
	fs::remove_all(players / bob);
	Check(Choices(user.string(), id, error).size() == 1 && Discover(user.string(), id, error).size() == 2, "one player alone needs no choice");
	fs::remove_all(players / alice);
	picks = Choices(user.string(), id, error);
	Check(picks.size() == 1 && picks[0].label == "Device save" && Discover(picks[0].folder, id, error).size() == 1 &&
			  Discover(picks[0].folder, id, error)[0].kind == Kind::Device,
		"the device save alone when no player has one");
	fs::create_directories(players / alice);
	fs::create_directory_symlink(input / title / "account", players / alice / title);
	Check(Discover((players / alice).string(), id, error).empty(), "a symlinked ProsperoEden save refused");
	Check(Choices((root / "nowhere").string(), id, error).empty(), "no ProsperoEden installed");
	return failures ? 1 : 0;
}
