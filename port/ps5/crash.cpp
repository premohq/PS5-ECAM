// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash.h"
#include "log.h"

#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string_view>

// The boot log, from a signal handler: write(2), no lock (log.cpp)
void PS5Cemu_CrashLine(std::string_view text, bool newLine);

namespace
{
	// One "[crash] ..." line, formatted on the stack: the crashed thread may hold the heap's lock.
	template<typename... Args>
	void Line(fmt::format_string<Args...> format, Args&&... args)
	{
		char buffer[1536];
		const auto result = fmt::format_to_n(buffer, sizeof(buffer), format, std::forward<Args>(args)...);
		PS5Cemu_CrashLine(std::string_view(buffer, std::min(result.size, sizeof(buffer))), true);
	}

	// The console's machine context, as qwords: its layout is FreeBSD's six qwords on (word 7 is
	// RDI, 23 the fault address, 26 RIP, 29 RSP)
	constexpr int kRip = 26, kRsp = 29;

	void Handler(int sig, siginfo_t* info, void* context)
	{
		const auto* machine = reinterpret_cast<const uint64_t*>(&static_cast<const ucontext_t*>(context)->uc_mcontext);
		const uintptr_t self = reinterpret_cast<uintptr_t>(&Handler);
		const uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
		Line("signal {} at {:#x} (Playtendoh's own handler: Cemu does not run in this session)", sig, fault);
		Line("PS5: rip {:#x}, rsp {:#x}, handler at {:#x}", machine[kRip], machine[kRsp], self);
		{
			char words[32 * 17];
			size_t used = 0;
			for (int i = 0; i < 32 && used < sizeof(words); i++)
				used += fmt::format_to_n(words + used, sizeof(words) - used, " {:x}", machine[i]).size;
			Line("PS5: machine context:{}", std::string_view(words, std::min(used, sizeof(words))));
		}

		// the words on the crashed code's stack that point into the app's code: the return
		// addresses of the frames it was in. From its RSP, or above RSP's page when the fault is in
		// that page (a stack overflow), up to the top of the thread's stack where it is known.
		uintptr_t here = machine[kRsp];
		if ((here ^ fault) < 0x4000)
			here = (here | 0x3fff) + 1;
		here &= ~static_cast<uintptr_t>(7);
		uintptr_t top = here + 16 * 1024;
		bool bounded = false;
		pthread_attr_t attributes;
		if (pthread_attr_init(&attributes) == 0)
		{
			void* stackAddress = nullptr;
			size_t stackSize = 0;
			if (pthread_attr_get_np(pthread_self(), &attributes) == 0 && pthread_attr_getstackaddr(&attributes, &stackAddress) == 0 &&
				pthread_attr_getstacksize(&attributes, &stackSize) == 0 && here >= reinterpret_cast<uintptr_t>(stackAddress) &&
				here < reinterpret_cast<uintptr_t>(stackAddress) + stackSize)
			{
				top = std::min<uintptr_t>(reinterpret_cast<uintptr_t>(stackAddress) + stackSize, here + 512 * 1024);
				bounded = true;
			}
			pthread_attr_destroy(&attributes);
		}
		const uintptr_t low = self > 0x3000000 ? self - 0x3000000 : 0x10000, high = self + 0x3000000;
		char frames[96 * 17];
		size_t used = 0;
		int found = 0;
		for (uintptr_t at = here; here && at + 8 <= top && found < 96; at += 8)
		{
			const uintptr_t word = *reinterpret_cast<const uintptr_t*>(at);
			if (word >= low && word < high && used < sizeof(frames))
			{
				used += fmt::format_to_n(frames + used, sizeof(frames) - used, " {:x}", word).size;
				found++;
			}
		}
		Line("PS5: code addresses on the stack ({} bytes{}):{}", top - here, bounded ? "" : ", stack bounds unknown",
			std::string_view(frames, std::min(used, sizeof(frames))));
		_Exit(1);
	}
}

namespace ps5crash
{
	void Install()
	{
		struct sigaction action{};
		sigfillset(&action.sa_mask); // nothing interrupts the report
		action.sa_flags = SA_SIGINFO;
		action.sa_sigaction = Handler;
		for (int sig : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGSYS, SIGTRAP})
			sigaction(sig, &action, nullptr);
		// with its address, where the eboot is: the PS5's own report of a crash the handler never saw
		// (its RIP and backtrace) is placed from it (tools/symbolize-crash.py --ps5)
		ps5log::Line("[main] crash reports: the app's own handler, until an emulator that has one starts (handler at {:#x})",
			reinterpret_cast<uintptr_t>(&Handler));
	}
}

// fmt's print to a FILE (fmt::print(stderr, ...)): fmt 12 writes straight into the FILE's buffer
// through the BSD layout the SDK's headers give it (_p, _w), which the console's stderr does not
// hold, and faulted. dynarmic's fault handler prints so before it passes a fault on, as mcl's failed
// assertions do: the fault became a second one in fmt, and no [crash] report was written.
// tools/link.sh wraps fmt's three FILE prints with these: stderr's and stdout's text goes to the boot
// log through the crash report's writer (write(2), no lock), every other FILE's through fwrite.
namespace
{
	void PrintToFile(FILE* file, fmt::string_view format, fmt::format_args args, bool newLine)
	{
		fmt::memory_buffer text;
		fmt::vformat_to(fmt::appender(text), format, args);
		if (newLine)
			text.push_back('\n');
		if (file != stderr && file != stdout)
		{
			std::fwrite(text.data(), 1, text.size(), file);
			return;
		}
		// one boot log line for each line printed
		std::string_view rest(text.data(), text.size());
		while (!rest.empty())
		{
			const size_t end = rest.find('\n');
			const std::string_view line = rest.substr(0, end);
			if (!line.empty())
				PS5Cemu_CrashLine(line, true);
			if (end == std::string_view::npos)
				break;
			rest.remove_prefix(end + 1);
		}
	}
}

extern "C" void __wrap__ZN3fmt3v126vprintEP7__sFILENS0_17basic_string_viewIcEENS0_17basic_format_argsINS0_7contextEEE(FILE* file,
	fmt::string_view format, fmt::format_args args)
{
	PrintToFile(file, format, args, false);
}

extern "C" void __wrap__ZN3fmt3v1215vprint_bufferedEP7__sFILENS0_17basic_string_viewIcEENS0_17basic_format_argsINS0_7contextEEE(
	FILE* file, fmt::string_view format, fmt::format_args args)
{
	PrintToFile(file, format, args, false);
}

extern "C" void __wrap__ZN3fmt3v128vprintlnEP7__sFILENS0_17basic_string_viewIcEENS0_17basic_format_argsINS0_7contextEEE(FILE* file,
	fmt::string_view format, fmt::format_args args)
{
	PrintToFile(file, format, args, true);
}
