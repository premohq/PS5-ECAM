#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The libretro cores the app runs in its own frontend (port/libretro), built for the PS5.

  tools/build-libretro.py [CORE...]     every core (or those), then libretro-common

Each core is compiled from its own sources (.deps/libretro, tools/deps.json) with the app's compiler
and flags (tools/ps5.cmake's), by this script alone: a core's Makefile is read as a list of files and
never run. Then it is made one object with symbols of its own, as the app links several:

  build/libretro/<core>.o   linked into one relocatable object, its section groups dropped (so the
                            inline functions it has from the same headers as the app stay its own),
                            every symbol it defines made local but retro_*, and those renamed
                            <core>_retro_* (port/libretro/cores.h names them)

A core built for a static frontend leaves libretro-common to it, as RetroArch has it on consoles: one
copy, build/libretro/libretro-common.o, serves them all. build/libretro/objects.txt lists the objects
the app links (tools/build-cemu.sh).
"""

import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(ROOT, ".deps", "libretro")
OUT = os.path.join(ROOT, "build", "libretro")
SDK = os.environ.get("PS5_PAYLOAD_SDK") or os.path.join(
    ROOT, ".deps/ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk")

# tools/ps5.cmake's, with -O2: the cores are the emulators' hot loops
COMMON = ["--target=x86_64-sie-ps5", "-isysroot", SDK, "-I" + os.path.join(ROOT, "port/libretro/compat"),
          "-isystem", SDK + "/target/include", "-march=znver2",
          "-fno-stack-protector", "-fno-plt", "-femulated-tls", "-fvisibility-nodllstorageclass=default",
          "-ffunction-sections", "-fdata-sections", "-fdenormal-fp-math=ieee", "-fPIC", "-O2", "-DNDEBUG",
          "-w"]
C_FLAGS = ["clang-18"] + COMMON
CXX_FLAGS = ["clang++-18", "-isystem", SDK + "/target/include/c++/v1"] + COMMON + [
    "-frtti", "-fexceptions", "-fcxx-exceptions", "-D_LIBCPP_ENABLE_EXPERIMENTAL=1"]


def makefile_list(path, variables, names=("SOURCES_C", "SOURCES_CXX")):
    """The files a Makefile's source lists name ($(DIR)/file.c), with its folders' variables given"""
    text = open(path, encoding="utf-8", errors="replace").read().replace("\\\n", " ")
    files = []
    for line in text.splitlines():
        match = re.match(r"\s*(\w+)\s*[:+]?=(.*)", line)
        if not match or match.group(1) not in names:
            continue
        for word in match.group(2).split():
            for name, value in variables.items():
                word = word.replace("$(" + name + ")", value)
            if "$(" not in word and word.endswith((".c", ".cpp", ".cc")):
                files.append(os.path.normpath(word))
    return files


class Core:
    def __init__(self, name, folder, sources, includes=(), defines=(), cxx_std="c++17", exclude=()):
        self.name = name
        self.folder = os.path.join(DEPS, folder)
        # once each (a Makefile's lists are read with every branch of its conditions)
        self.sources = [s for s in dict.fromkeys(sources) if not any(re.search(x, s) for x in exclude)]
        self.includes = list(includes)
        self.defines = list(defines)
        self.cxx_std = cxx_std


def cores():
    found = {}

    # PokeMini (GPL-3.0-or-later): the Pokémon mini
    folder = "PokeMini"
    sources = ["freebios/freebios.c", "resource/PokeMini_ColorPal.c", "libretro/libretro.c"] + [
        "source/" + n + ".c" for n in ("CommandLine", "Hardware", "Joystick", "MinxAudio", "MinxColorPRC", "MinxCPU_CE",
                                       "MinxCPU_CF", "MinxCPU_SP", "MinxCPU_XX", "MinxCPU", "MinxIO", "MinxIRQ", "MinxLCD",
                                       "MinxPRC", "MinxTimers", "Multicart", "PMCommon", "PokeMini", "Video_x1", "Video_x2",
                                       "Video_x3", "Video_x4", "Video_x5", "Video_x6", "Video_x7", "Video")]
    found["pokemini"] = Core("pokemini", folder, sources,
                             ["libretro", "libretro/libretro-common/include", "source", "resource", "freebios"],
                             ["__LIBRETRO__", "STATIC_LINKING", 'GIT_VERSION=""'])

    # Mesen (GPL-3.0): the NES and the Famicom Disk System, VS. System
    folder = "Mesen"
    sources = makefile_list(os.path.join(DEPS, folder, "Libretro/Makefile.common"),
                            {"LIBRETRO_DIR": "Libretro", "SEVENZIP_DIR": "SevenZip", "CORE_DIR": "Core", "UTIL_DIR": "Utilities"})
    found["mesen"] = Core("mesen", folder, sources, [], ["LIBRETRO"], "c++17")

    # Mesen-S (GPL-3.0): the SNES, Satellaview and Sufami Turbo
    folder = "Mesen-S"
    sources = makefile_list(os.path.join(DEPS, folder, "Libretro/Makefile.common"),
                            {"LIBRETRO_DIR": "Libretro", "SEVENZIP_DIR": "SevenZip", "CORE_DIR": "Core", "UTIL_DIR": "Utilities"})
    found["mesens"] = Core("mesens", folder, sources, [], ["LIBRETRO"], "c++17")

    # Beetle VB (Mednafen's, GPL-2.0-or-later): the Virtual Boy
    folder = "beetle-vb-libretro"
    sources = makefile_list(os.path.join(DEPS, folder, "Makefile.common"),
                            {"CORE_DIR": ".", "MEDNAFEN_DIR": "mednafen", "CORE_EMU_DIR": "mednafen/vb",
                             "LIBRETRO_COMM_DIR": "libretro-common"})
    sources = [s for s in sources if "libretro-common" not in s and "glsym" not in s]
    found["beetlevb"] = Core("beetlevb", folder, sources,
                             [".", "mednafen", "mednafen/include", "mednafen/hw_sound", "mednafen/hw_cpu", "mednafen/hw_misc",
                              "libretro-common/include"],
                             ["FRONTEND_SUPPORTS_RGB565", "WANT_32BPP", "LSB_FIRST", 'MEDNAFEN_VERSION="0.9.31"',
                              "MEDNAFEN_VERSION_NUMERIC=931", "STDC_HEADERS", "__STDC_LIMIT_MACROS", "__LIBRETRO__",
                              "INLINE=inline"], "c++11")

    # mGBA (MPL-2.0): the Game Boy, the Game Boy Color and the Game Boy Advance. Its sources as its
    # CMakeLists.txt gathers them for the libretro core on a POSIX system, its two generated files
    # written here (flags.h with none of its switches: they are given as definitions)
    folder = os.path.join(DEPS, "mgba")
    generated = os.path.join(OUT, "mgba", "generated")
    os.makedirs(os.path.join(generated, "include", "mgba"), exist_ok=True)
    flags = open(os.path.join(folder, "src/core/flags.h.in")).read()
    write_if_changed(os.path.join(generated, "include/mgba/flags.h"), re.sub(r"#cmakedefine.*", "", flags))
    version = open(os.path.join(folder, "src/core/version.c.in")).read()
    commit = git_commit(folder)
    for key, value in {"GIT_COMMIT": commit, "GIT_COMMIT_SHORT": commit[:7], "GIT_BRANCH": "libretro", "GIT_REV": "0",
                       "BINARY_NAME": "mgba", "PROJECT_NAME": "mGBA", "VERSION_STRING": "libretro " + commit[:7]}.items():
        version = version.replace("${" + key + "}", value)
    write_if_changed(os.path.join(generated, "version.c"), version)

    def files(directory, listing, drop=()):
        return [f"src/{directory}/{name}" for name in listing if name not in drop]

    def cmake_list(directory, variable="SOURCE_FILES"):
        text = open(os.path.join(folder, "src", directory, "CMakeLists.txt")).read()
        match = re.search(r"set\(" + variable + r"\s+(.*?)\)", text, re.S)
        return [w for w in match.group(1).split() if w.endswith(".c")]

    util = [w for w in cmake_list("util", "BASE_SOURCE_FILES")] + [w for w in cmake_list("util") if not w.startswith("$")]
    sources = (files("core", cmake_list("core"), {"scripting.c", "library.c", "mem-search.c", "rewind.c", "input.c"}) +
               files("sm83", cmake_list("sm83")) + files("gb", cmake_list("gb")) + files("arm", cmake_list("arm")) +
               [os.path.normpath("src/gba/" + w) for w in cmake_list("gba")] +
               files("util", util, {"audio-resampler.c", "interpolator.c"}) +
               ["src/util/vfs/vfs-mem.c", "src/util/vfs/vfs-fifo.c", "src/util/vfs/vfs-fd.c", "src/util/vfs/vfs-dirent.c",
                "src/platform/posix/memory.c"] +
               sorted(os.path.relpath(p, folder) for p in glob.glob(os.path.join(folder, "src/third-party/inih/*.c"))) +
               sorted(os.path.relpath(p, folder) for p in glob.glob(os.path.join(folder, "src/platform/libretro/*.c"))) +
               [os.path.relpath(os.path.join(generated, "version.c"), folder),
                os.path.relpath(os.path.join(ROOT, "port/libretro/compat/locale.c"), folder)])
    found["mgba"] = Core("mgba", "mgba", sources, ["include", "src", os.path.relpath(os.path.join(generated, "include"), folder)],
                         ["__LIBRETRO__", "_GNU_SOURCE", "COLOR_16_BIT", "COLOR_5_6_5", "DISABLE_THREADING", "MGBA_STANDALONE",
                          "ENABLE_VFS_FD", "HAVE_STRDUP", "HAVE_STRNDUP", "HAVE_LOCALTIME_R", "ENABLE_VFS", "ENABLE_DIRECTORIES",
                          "HAVE_LOCALE", "HAVE_XLOCALE",  # the SDK's locale_t, with port/libretro/compat/locale.c
                          "MINIMAL_CORE=2", "M_PI=3.14159265358979323846", "M_CORE_GB", "M_CORE_GBA"])
    return found


def write_if_changed(path, text):
    if not os.path.exists(path) or open(path).read() != text:
        open(path, "w").write(text)


def git_commit(folder):
    """The checkout's commit, read from its .git (not by running git in it)"""
    head = open(os.path.join(folder, ".git", "HEAD")).read().strip()
    if head.startswith("ref: "):
        ref = os.path.join(folder, ".git", head[5:])
        if os.path.exists(ref):
            return open(ref).read().strip()
        for line in open(os.path.join(folder, ".git", "packed-refs")):
            if line.strip().endswith(head[5:]):
                return line.split()[0]
    return head


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    return result.returncode, result.stderr


def stale(target, depfile):
    """Whether target is older than anything its depfile (-MMD) names, or missing"""
    if not os.path.exists(target) or not os.path.exists(depfile):
        return True
    when = os.path.getmtime(target)
    words = open(depfile).read().replace("\\\n", " ").split()[1:]
    return any(os.path.exists(w) and os.path.getmtime(w) > when for w in words)


def compile_all(name, folder, sources, includes, defines, cxx_std):
    objdir = os.path.join(OUT, name, "obj")
    jobs, objects = [], []
    for source in sources:
        cxx = source.endswith((".cpp", ".cc"))
        obj = os.path.join(objdir, re.sub(r"\.(c|cpp|cc)$", ".o", source.replace("..", "__")))
        objects.append(obj)
        dep = obj[:-2] + ".d"
        if not stale(obj, dep):
            continue
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        command = (CXX_FLAGS + ["-std=" + cxx_std] if cxx else C_FLAGS + ["-std=gnu11"])
        command += ["-I" + os.path.join(folder, i) for i in includes] + ["-D" + d for d in defines]
        command += ["-MMD", "-MF", dep, "-c", os.path.join(folder, source), "-o", obj]
        jobs.append((source, command))
    failures = []
    with concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", os.cpu_count() or 4))) as pool:
        for (source, _), (code, errors) in zip(jobs, pool.map(lambda job: run(job[1], folder), jobs)):
            if code:
                failures.append((source, errors))
    for source, errors in failures[:6]:
        print(f"==> [libretro] {name}: {source} did not compile:\n{errors[-2500:]}", file=sys.stderr)
    if failures:
        raise SystemExit(f"{name}: {len(failures)} of {len(sources)} files did not compile")
    print(f"==> [libretro] {name}: {len(sources)} files, {len(jobs)} compiled")
    return objects


def isolate(name, objects):
    """One relocatable object: groups dropped, retro_* kept global as <name>_retro_*, the rest local"""
    work = os.path.join(OUT, name)
    partial, local, out = (os.path.join(work, name + ".partial.o"), os.path.join(work, name + ".local.o"),
                           os.path.join(OUT, name + ".o"))
    listing = os.path.join(work, "objects.rsp")
    open(listing, "w").write("\n".join(objects))
    subprocess.run(["/usr/lib/llvm-18/bin/ld.lld", "-r", "-o", partial, "@" + listing], check=True)
    symbols = subprocess.run(["llvm-nm-18", "--defined-only", "--extern-only", partial], check=True, capture_output=True,
                             text=True).stdout
    api = sorted({line.split()[-1] for line in symbols.splitlines() if line.split()[-1].startswith("retro_")})
    keep, rename = os.path.join(work, "keep.txt"), os.path.join(work, "rename.txt")
    open(keep, "w").write("\n".join(api) + "\n")
    open(rename, "w").write("".join(f"{s} {name}_{s}\n" for s in api))
    subprocess.run(["llvm-objcopy-18", "--remove-section=.group", "--keep-global-symbols=" + keep, partial, local], check=True)
    subprocess.run(["llvm-objcopy-18", "--redefine-syms=" + rename, local, out], check=True)
    os.remove(partial)
    os.remove(local)
    print(f"==> [libretro] {name}: {out} ({len(api)} entry points)")
    return out


def libretro_common():
    folder = os.path.join(DEPS, "libretro-common")
    sources = ["compat/compat_posix_string.c", "compat/compat_snprintf.c", "compat/compat_strcasestr.c", "compat/compat_strl.c",
               "compat/fopen_utf8.c", "encodings/encoding_utf.c", "file/file_path.c", "file/file_path_io.c",
               "file/retro_dirent.c", "streams/file_stream.c", "streams/file_stream_transforms.c", "streams/memory_stream.c",
               "string/stdstring.c", "time/rtime.c", "vfs/vfs_implementation.c", "lists/string_list.c", "lists/dir_list.c",
               "file/config_file.c", "file/config_file_userdata.c", "rthreads/rthreads.c", "features/features_cpu.c",
               "string/rstrtod.c"]
    objects = compile_all("libretro-common", folder, sources, ["include"], ["__LIBRETRO__", "HAVE_THREADS"], "c++17")
    out = os.path.join(OUT, "libretro-common.o")
    listing = os.path.join(OUT, "libretro-common", "objects.rsp")
    open(listing, "w").write("\n".join(objects))
    subprocess.run(["/usr/lib/llvm-18/bin/ld.lld", "-r", "-o", out, "@" + listing], check=True)
    return out


def main():
    available = cores()
    wanted = sys.argv[1:] or list(available)
    built = []
    for name in wanted:
        core = available[name]
        built.append(isolate(name, compile_all(name, core.folder, core.sources, core.includes, core.defines, core.cxx_std)))
    built.append(libretro_common())
    open(os.path.join(OUT, "objects.txt"), "w").write("\n".join(built) + "\n")
    # what no object here gives: the C library's, the app's, or missing
    defined = set()
    undefined = set()
    for obj in built:
        out = subprocess.run(["llvm-nm-18", obj], check=True, capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] in ("U", "w"):
                undefined.add(parts[1])
            elif len(parts) == 3 and parts[1].isupper():
                defined.add(parts[2])
    print("==> [libretro] left to the C library and the app:", " ".join(sorted(undefined - defined)))


if __name__ == "__main__":
    main()
