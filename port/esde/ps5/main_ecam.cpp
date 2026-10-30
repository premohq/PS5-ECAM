// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: EmulationStation's entry point on the PS5. After Mihawk's main_ps5.cpp (PS5 RetroArch's
// frontends/es-de), in its place: ES-DE runs as a program of its own in the app's folder,
// es-de/es-de.bin, started by PS5-ECAM through LoadExec (port/esde/launch.cpp) with
//
//   --ecam-root <PS5-ECAM's data folder>   ES-DE's home is its es-de/ (settings, game lists, media)
//   --return <PS5-ECAM's eboot.bin>        what plays the games, and what quitting ES-DE goes back to
//
// also in es-de/ecam-arguments.txt, one a line, for when LoadExec drops them. ES-DE's own main (esde_main,
// Mihawk's patches/0001) wants what a desktop gives it, which a PS5 process does not have: argv[0] the
// program (it finds its resources beside it), $HOME its data folder, a working directory getcwd can name
// (frontends/common/frontend_shims.c). Its systems and game lists are PS5-ECAM's, written before it
// started (port/esde/esde.cpp): --gamelist-only, so it scans for nothing.
//
// When ES-DE is quit, PS5-ECAM starts again with --own-launcher: its own launcher, this time.
#include "ecam_frontend.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

int esde_main(int argc, char* argv[]);

extern "C" int sceSystemServiceLoadExec(const char* path, const char* const* argv);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" int sceKernelUsleep(unsigned int microseconds);
extern "C" int ps5_frontend_chdir(const char* path);
extern "C" int ecam_esde_take_result(void);

// ../PS5_OpenGL's heap (native-app/app_heap.c), which every allocation of this program goes through: its
// 128 MiB default is too little for a theme's 1920x1080 backgrounds (Mihawk's note in main_ps5.cpp). A size
// over 128 MiB is mapped from direct memory.
extern "C" const size_t ps5_opengl_heap_size = size_t(1) << 30;

namespace
{
	std::string logFile;

	void Note(const char* what, long long value)
	{
		if (std::FILE* log = std::fopen(logFile.c_str(), "a"))
		{
			std::fprintf(log, "%lld %s %lld\n", (long long)std::time(nullptr), what, value);
			std::fclose(log);
		}
	}

	bool Exists(const std::string& path)
	{
		struct stat status;
		return stat(path.c_str(), &status) == 0;
	}

	// The arguments PS5-ECAM gave: argv's, else those in es-de/ecam-arguments.txt (removed once read)
	std::vector<std::string> Arguments(int argc, char** argv)
	{
		std::vector<std::string> arguments;
		for (int i = 0; i < argc && argv && argv[i]; i++)
			arguments.emplace_back(argv[i]);
		const char* file = "/app0/es-de/ecam-arguments.txt";
		bool given = false;
		for (const std::string& argument : arguments)
			given |= argument == "--ecam-root";
		if (std::FILE* in = std::fopen(file, "rb"))
		{
			if (!given)
			{
				char line[4096];
				while (std::fgets(line, sizeof(line), in))
				{
					line[std::strcspn(line, "\r\n")] = '\0';
					arguments.emplace_back(line);
				}
			}
			std::fclose(in);
			std::remove(file);
		}
		return arguments;
	}
}

int main(int argc, char** argv)
{
	std::string root = "/data/ps5cemu";
	std::vector<std::string> passed;
	const std::vector<std::string> arguments = Arguments(argc, argv);
	for (size_t i = 0; i < arguments.size(); i++)
	{
		if (arguments[i] == "--ecam-root" && i + 1 < arguments.size())
			root = arguments[++i];
		else if (arguments[i] == "--return" && i + 1 < arguments.size())
			ecam_frontend::Eboot() = arguments[++i];
		else if (!arguments[i].empty() && arguments[i] != "/app0/es-de/es-de.bin")
			passed.push_back(arguments[i]);
	}
	const std::string home = root + "/es-de";
	ecam_frontend::Home() = home;
	mkdir(home.c_str(), 0777);
	logFile = home + "/es-de-ps5.log";
	struct stat logStatus;
	if (stat(logFile.c_str(), &logStatus) == 0 && logStatus.st_size > 512 * 1024)
		std::rename(logFile.c_str(), (home + "/es-de-ps5.1.log").c_str());
	Note("start: hide splash", sceSystemServiceHideSplashScreen());

	// the program, from whose folder ES-DE finds its resources: beside PS5-ECAM's eboot, else /app0's
	const std::string eboot = ecam_frontend::Eboot();
	std::string program = eboot.substr(0, eboot.rfind('/') + 1) + "es-de/es-de.bin";
	if (!Exists(program))
		program = "/app0/es-de/es-de.bin";

	// the OpenGL driver's own reports, a line at a time; the last start's kept
	const std::string out = home + "/stdout.txt";
	std::rename(out.c_str(), (home + "/stdout.1.txt").c_str());
	if (std::freopen(out.c_str(), "w", stdout))
		std::setvbuf(stdout, nullptr, _IOLBF, 0);
	setenv("HOME", home.c_str(), 1);
	// env.txt in ES-DE's home, NAME=VALUE a line: the driver's switches, tried without a new build
	if (std::FILE* environment = std::fopen((home + "/env.txt").c_str(), "r"))
	{
		char line[256];
		int set = 0;
		while (std::fgets(line, sizeof(line), environment))
		{
			line[std::strcspn(line, "\r\n")] = '\0';
			char* equals = std::strchr(line, '=');
			if (line[0] == '#' || !equals || equals == line)
				continue;
			*equals = '\0';
			set += setenv(line, equals + 1, 1) == 0 ? 1 : 0;
		}
		std::fclose(environment);
		Note("start: variables from env.txt", set);
	}
	Note("start: working directory (errno if refused)", ps5_frontend_chdir(home.c_str()) == 0 ? 0 : errno);
	// back from a game PS5-ECAM played (game_ecam.cpp)? ES-DE's start asks
	Note("start: back from a game", ecam_esde_take_result());

	static char gamelistOnly[] = "--gamelist-only";
	std::vector<char*> esde{program.data(), gamelistOnly};
	for (std::string& argument : passed)
		esde.push_back(argument.data());
	esde.push_back(nullptr);
	const int status = esde_main((int)esde.size() - 1, esde.data());
	Note("ES-DE returned", status);

	// quit: PS5-ECAM's own launcher
	const char* back[] = {"--own-launcher", nullptr};
	Note("back to PS5-ECAM: LoadExec", sceSystemServiceLoadExec(eboot.c_str(), back));
	for (;;)
		sceKernelUsleep(100000);
}
