#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A crash report from the boot log, in function names and source lines.

    symbolize-crash.py ELF LOG [LOG...]
    symbolize-crash.py ELF LOG --ps5 RIP [BACKTRACE...]

ELF is the unstripped build/cemu/ps5cemu.elf of the build that crashed (`make release` keeps each
release's as dist/ps5cemu-vVERSION.elf); LOG is a boot log (logs/boot.log, or a report's attached
one) or Cemu's log.txt, "-" for standard input. Every crash report in them is printed with its
addresses looked up: the instruction that crashed (RIP), and the code addresses found on the crashed
thread's stack, the return addresses of the calls it was in, innermost first. So is every hang dump
(the "[hang]" lines of a game that stopped drawing): where Cemu's GPU thread and emulated CPU cores
were.

The eboot is not loaded at the ELF's addresses: the report gives its crash handler's address, so the
load offset is that minus the handler's address in the ELF (Cemu's handlerDumpingSignal, or the
port's own handler in port/ps5/crash.cpp when Cemu did not run). Needs llvm-nm-18 and
llvm-symbolizer-18 (or the unversioned ones).

With --ps5, the addresses are the PS5's own crash report's (its RIP and backtrace lines), for a crash
the app's handler never saw and the boot log has no report of: the eboot is placed from the handler's
address the boot log's start gives ("crash reports: ... (handler at 0x...)"), the last one in LOG.
"""

import re
import shutil
import subprocess
import sys

# the console's machine context, as qwords (port/ps5/crash.cpp): FreeBSD's fields six qwords on
WORDS = {7: "rdi", 8: "rsi", 9: "rdx", 10: "rcx", 11: "r8", 12: "r9", 13: "rax", 14: "rbx", 15: "rbp",
         16: "r10", 17: "r11", 18: "r12", 19: "r13", 20: "r14", 21: "r15", 23: "fault address", 25: "error",
         26: "rip", 29: "rsp"}
HANDLERS = {"cemu": "handlerDumpingSignal(int, __siginfo*, void*)",
            "port": "(anonymous namespace)::Handler(int, __siginfo*, void*)",
            "hang": "PS5Cemu_HangSignal"}


def tool(name):
    for candidate in (f"{name}-18", name):
        if shutil.which(candidate):
            return candidate
    sys.exit(f"{name}-18 (or {name}) is needed: apt install llvm-18")


def handler_addresses(elf):
    """The crash handlers' addresses in the ELF."""
    out = subprocess.run([tool("llvm-nm"), "-C", elf], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3:
            continue
        for kind, name in HANDLERS.items():
            # the port's handler is local to its file, and may carry an LTO suffix
            if kind not in found and (parts[2] == name or parts[2].startswith(name + " (")):
                found[kind] = int(parts[0], 16)
    return found


def reports(lines):
    """Each crash report's lines: from a "signal N at" line to the next one, or from the first
    "PS5:" line of Cemu's log.txt."""
    current = None
    for raw in lines:
        line = raw.rstrip("\n")
        text = line.split("[crash] ", 1)[1] if "[crash] " in line else line.strip()
        if re.match(r"signal \d+ at", text) or (current is None and text.startswith("PS5: fault address")):
            if current:
                yield current
            current = [text]
        elif current is not None and ("[crash] " in line or text.startswith("PS5:")):
            current.append(text)
    if current:
        yield current


def hangs(lines):
    """Each hang dump (port/ps5/threads.h): its first line, which has the handler's address, and its
    threads' "NAME: rip ..., code on the stack: ..." lines."""
    current = None
    for raw in lines:
        if "[hang] " not in raw:
            continue
        text = raw.split("[hang] ", 1)[1].strip()
        if "handler at" in text:
            if current:
                yield current
            current = [text]
        elif current is not None:
            current.append(text)
    if current:
        yield current


def symbolize(elf, addresses):
    """Address (in the ELF) -> [(function, file:line)], inlined frames first."""
    if not addresses:
        return {}
    request = "\n".join(f"0x{a:x}" for a in addresses) + "\n"
    out = subprocess.run([tool("llvm-symbolizer"), f"--obj={elf}", "--functions=linkage", "--demangle", "--inlines",
                          "--relativenames"], input=request, capture_output=True, text=True, check=True).stdout
    results, blocks = {}, out.strip("\n").split("\n\n")
    for address, block in zip(addresses, blocks):
        lines = block.splitlines()
        frames = [(lines[i], lines[i + 1] if i + 1 < len(lines) else "") for i in range(0, len(lines), 2)]
        results[address] = [(function, location) for function, location in frames if function != "??"]
    return results


def show(elf, handlers, report, number):
    text = "\n".join(report)
    print(f"=== crash report {number}: {report[0]}")
    own = ("Playtendoh's own handler" in text or "PS5-ECAM's own handler" in text  # before the rename
           or "PS5CEMU-HAR's own handler" in text)  # before 5.0.0
    handler = re.search(r"handler at (0x[0-9a-f]+)", text)
    kind = "port" if own else "cemu"
    if not handler or kind not in handlers:
        print("  no handler address in this report (or its symbol in the ELF): cannot place the eboot")
        return
    offset = int(handler[1], 16) - handlers[kind]
    print(f"  {'the port' if own else 'Cemu'}'s handler at {handler[1]}: the eboot is loaded {offset:#x} above the ELF's addresses")
    words = re.search(r"machine context:((?: [0-9a-f]+)+)", text)
    context = [int(w, 16) for w in words[1].split()] if words else []
    if context:
        print("  registers: " + ", ".join(f"{name} {context[i]:#x}" for i, name in WORDS.items() if i < len(context)))
    rip = context[26] if len(context) > 26 else None
    rip_line = re.search(r"rip (0x[0-9a-f]+)", text)
    if rip is None and rip_line:
        rip = int(rip_line[1], 16)
    stack = re.search(r"code addresses on the stack \(([^)]*)\):((?: [0-9a-f]+)*)", text)
    frames = [int(w, 16) for w in stack[2].split()] if stack else []
    wanted = ([rip] if rip is not None else []) + frames
    looked_up = symbolize(elf, [a - offset for a in wanted if a >= offset])

    def describe(runtime):
        address = runtime - offset
        found = looked_up.get(address)
        if not found:
            return [f"{runtime:#x}: not in the eboot's code"]
        first, *inlined = found
        out = [f"{runtime:#x} ({address:#x}): {first[0]}  [{first[1]}]"]
        out += [f"    inlined into {function}  [{location}]" for function, location in inlined]
        return out

    if rip is not None:
        print("  crashed in:")
        for line in describe(rip):
            print("    " + line)
    if stack:
        print(f"  on the stack ({stack[1]}), innermost first; a scan, so some are stale return addresses:")
        for runtime in frames:
            for line in describe(runtime):
                print("    " + line)


def show_hang(elf, handlers, dump, number):
    print(f"=== hang {number}: {dump[0]}")
    handler = re.search(r"handler at (0x[0-9a-f]+)", dump[0])
    if not handler or "hang" not in handlers:
        print("  no handler address (or its symbol in the ELF): cannot place the eboot")
        return
    offset = int(handler[1], 16) - handlers["hang"]
    threads = []
    for line in dump[1:]:
        match = re.match(r"(.+?): rip (0x[0-9a-f]+), code on the stack:(.*)", line)
        if match:
            threads.append((match[1], int(match[2], 16), [int(w, 16) for w in re.findall(r"0x[0-9a-f]+", match[3])]))
        else:
            print(f"  {line}")
    wanted = [address for _, rip, stack in threads for address in [rip] + stack]
    looked_up = symbolize(elf, sorted({a - offset for a in wanted if a >= offset}))

    def describe(runtime):
        found = looked_up.get(runtime - offset)
        if not found:
            return f"{runtime:#x}: not in the eboot's code (recompiled code, or a system library)"
        return f"{runtime:#x}: {found[0][0]}  [{found[0][1]}]"

    for name, rip, stack in threads:
        print(f"  {name}: at {describe(rip)}")
        for runtime in stack:
            print(f"      {describe(runtime)}")


def show_ps5(elf, handlers, lines, addresses):
    """The PS5's own report's addresses, placed from the handler's address at the session's start."""
    started = [m for line in lines for m in [re.search(r"crash reports: .*\(handler at (0x[0-9a-f]+)\)", line)] if m]
    if not started or "port" not in handlers:
        sys.exit("no \"crash reports: ... (handler at 0x...)\" line in the log (a build before it), or no handler in the ELF")
    offset = int(started[-1][1], 16) - handlers["port"]
    print(f"=== the PS5's report: the port's handler at {started[-1][1]}, the eboot loaded {offset:#x} above the ELF's addresses")
    looked_up = symbolize(elf, [a - offset for a in addresses if a >= offset])
    for name, runtime in zip(["RIP"] + [f"backtrace[{i}]" for i in range(len(addresses) - 1)], addresses):
        found = looked_up.get(runtime - offset)
        if not found:
            print(f"  {name} {runtime:#x}: not in the eboot's code (recompiled code, or a system library)")
            continue
        (function, location), *inlined = found
        print(f"  {name} {runtime:#x} ({runtime - offset:#x}): {function}  [{location}]")
        for function, location in inlined:
            print(f"      inlined into {function}  [{location}]")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    arguments = sys.argv[1:]
    ps5 = []
    if "--ps5" in arguments:
        at = arguments.index("--ps5")
        ps5 = [int(a, 16) for a in arguments[at + 1:]]
        arguments = arguments[:at]
        if not ps5:
            sys.exit(__doc__)
    elf, logs = arguments[0], arguments[1:]
    handlers = handler_addresses(elf)
    lines = []
    for log in logs:
        if log == "-":
            lines += sys.stdin.read().splitlines()
        else:
            with open(log, errors="replace") as file:
                lines += file.read().splitlines()
    if ps5:
        show_ps5(elf, handlers, lines, ps5)
        return
    found = list(reports(lines))
    for number, report in enumerate(found, 1):
        show(elf, handlers, report, number)
    dumps = list(hangs(lines))
    for number, dump in enumerate(dumps, 1):
        show_hang(elf, handlers, dump, number)
    if not found and not dumps:
        print("no crash report or hang dump in " + ", ".join(logs))


if __name__ == "__main__":
    main()
