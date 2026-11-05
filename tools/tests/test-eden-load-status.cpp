// SPDX-License-Identifier: GPL-3.0-or-later
// Eden's "Loader status N" in words (port/eden/load_status.h)
#include "eden/load_status.h"
#include <iostream>
#include <string>
using ps5eden::LoadProblem;
using ps5eden::LoadStatusProblem;
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
int main()
{
	const char* tail = ". Check this ROM, its keys and firmware; see stderr.log.";
	const auto status = [&](int n) { return LoadStatusProblem("Loader status " + std::to_string(n) + tail); };
	Check(status(28) == LoadProblem::KeysTooOld, "28, the key area key missing (issue 108): keys older than the game");
	Check(status(26) == LoadProblem::KeysTooOld, "26, the title KEK missing: keys older than the game");
	Check(status(20) == LoadProblem::NoKeys, "20, no prod.keys");
	Check(status(25) == LoadProblem::NoTicket, "25, the title key missing: no ticket");
	for (const int n : {21, 22, 27, 29, 30})
		Check(status(n) == LoadProblem::KeysWrong, ("wrong keys or a damaged file: " + std::to_string(n)).c_str());
	for (const int n : {0, 2, 5, 6, 7, 23, 24, 31, 40, 100})
		Check(status(n) == LoadProblem::None, ("not about the keys: " + std::to_string(n)).c_str());
	Check(LoadStatusProblem("Loader status 28") == LoadProblem::KeysTooOld, "the number alone");
	Check(LoadStatusProblem("Graphics backend initialization failed. Try another backend in Settings") == LoadProblem::None,
		"another failure");
	Check(LoadStatusProblem("Loader status . Check") == LoadProblem::None && LoadStatusProblem("Loader status x28.") == LoadProblem::None &&
			  LoadStatusProblem("Loader status 1234567.") == LoadProblem::None && LoadStatusProblem("") == LoadProblem::None,
		"no number, not a number, too long a number, nothing");
	Check(LoadStatusProblem("room:Name\tLoader status 28.") == LoadProblem::None, "only at the start");
	return failures ? 1 : 0;
}
