# The Vulkan driver and Breath of the Wild at 4K60 and 8K30

This is a read-through of the Vulkan driver Playtendoh links (Mihawk-99's RADV port: the
[PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) fork at `7b59ef27`, built with
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s recipe at `fde9e37`), of how Cemu drives it,
and of what could make Breath of the Wild run at 4K and 60 fps, and at 8K and 30. It ends with the
settings for both, changes ranked, and how to test them.

Nothing here was measured on a console. The driver patches in `patches/mesa` were built and run on
the driver's PC model (the PS5 winsys's host build, which runs everything but the GPU), and their
console code was compiled against the platform headers. Every number for the console below comes
from PS5_Vulkan's own measurements, cited by their finding names (R34, R68 and so on in its
`docs/HARDWARE_FINDINGS.md`), or is marked as an estimate.

## The driver: 7b59ef2, with 120 Hz that works and frame pacing

The app pinned PS5_Mesa `0b2d6d1a` until 2026-10-06. Upstream has since rewritten its commit ids
(that tree is now `504adad`, and the SDK fork's `95c08f27` is now `b83202b`, the same tree), so the old
pins may stop being fetchable. `tools/deps.json` now pins upstream's current driver, `7b59ef27`
(PS5_Vulkan `fde9e37` builds the same), which is `504adad` and four changes to the VideoOut swapchain:

| Change | What it does | What Playtendoh does with it |
| --- | --- | --- |
| `f46af72` `PS5_VIDEOOUT_PARAM_JSON` | The driver offers 119.88 Hz only to a title whose `param.json` declares it, and read only `/app0/sce_sys/param.json`. A process the HEN jailbreaks has no `/app0` (`port/app/paths.h`), so **119.88 Hz was never offered there, and "120 Hz output" did nothing** (the boot log said `VideoOut surface 3840x2160 at 59.94 Hz`). The variable names another file | With "120 Hz output" on, the app names its own `param.json` before a Wii U game starts. With it off, it names a file that does not exist: in a sandboxed process the driver used to find `/app0`'s and switch the display to 119.88 Hz whatever the setting said, since the 59.94 Hz mode Cemu then took was only a mode handle |
| `12d0391` attribute3 bit 0x40 alone | 0x80000 (a 120 Hz mode that requires VRR) turned the system's VRR off; retail titles with VRR declare 0x40 with 0x40000 | `sce_sys/param.json` declares 0x40040 (was 0x80040). The driver reads the file either way; the home screen may keep the old value until the app is registered again |
| `aa44fa4` `wsi_videoout_set_flip_rate` | Each flip shows for at least 1, 2 or 3 vblanks (`sceVideoOutSetFlipRate`) | **Frame pacing**, a new setting (below) |
| `7b59ef2` a mode for each size | Modes at 3840x2160, 2560x1440 and 1920x1080, which VideoOut scales to the screen | The surface takes the 3840x2160 mode by size (`port/ps5/vulkan_display.cpp`), as before |

The winsys, where `patches/mesa` apply, is the same in both, and the patches apply unchanged.

### Frame pacing

**Settings > Video > Frame pacing** (and the in-game menu's Graphics, which changes it at once) keeps
each frame on screen for at least two or three refreshes, through the driver's flip rate
(`port/ps5/display.cpp`). A game that cannot hold the display's rate then runs at an even lower one,
instead of mixing frames shown for one refresh with frames shown for two:

| Output | Frame pacing | Each frame shows for | When the next is late, a frame shows for | Suits |
| --- | --- | --- | --- | --- |
| 119.88 Hz | Off | 8.3 ms or more | 16.7 ms | a game that holds 120 |
| 119.88 Hz | 60 fps | 16.7 ms or more | 25 ms | **BotW at 4K**, FPS++ at 60 |
| 119.88 Hz | 40 fps | 25 ms or more | 33 ms | BotW at 5K or 8K, if it holds 40 |
| 59.94 Hz | Off | 16.7 ms or more | 33 ms | as before |
| 59.94 Hz | 30 fps | 33.3 ms or more | 50 ms | **BotW at 8K**, FPS++ at 30 |

The boot log says what was asked of VideoOut (`[vulkan] frame pacing: each frame shown for at least
2 refreshes`) and what the surface got (`[vulkan] VideoOut surface 3840x2160 at 119.88 Hz`, or a line
saying 120 Hz output is on but VideoOut offers 59.94 Hz only). The pacing is the flip rate of the
whole output, so the app puts it back to every refresh before the launcher and the 3DS side.

## Breath of the Wild at 4K60 and 8K30: the settings

VideoOut's largest buffer is 3840x2160, so 8K is drawn at 7680x4320 and scaled to the 4K screen by
Cemu (four samples a pixel). Both use the community graphic packs (the in-game menu, or
Triangle on the game in the library):

| | 4K at 60 | 8K at 30 |
| --- | --- | --- |
| Graphics > Resolution | 3840x2160 (4K) | 7680x4320 (8K) |
| Graphics > Anti-Aliasing | Normal FXAA | **None**: the 8K picture scaled to 4K is already smooth, and FXAA would be one more full pass at 8K |
| Graphics > Shadows | Medium (100%) | Medium (100%) |
| Mods > FPS++ | On, 60FPS Limit | On, 30FPS Limit (or off: the game's own 30) |
| Settings > Video > 120 Hz output | On, where the TV has it | **Off** (frame pacing cannot make 30 at 120 Hz) |
| Settings > Video > Frame pacing | 60 fps (with 120 Hz output; Off at 60 Hz) | 30 fps |
| In-game > Graphics > Accurate barriers | Off, if the game shows no glitches with it off | the same |

If 8K does not hold 30, 5120x2880 (5K) with 120 Hz output and frame pacing at 40 fps is the next
step. 8K's render targets are four times 4K's, and nothing has measured how much of the console's
memory BotW at 8K takes: the boot log's memory lines (`[perf] ... heap` and Cemu's start) are where
to look first if 8K stops or stutters where 4K does not.

## Where things stand

- **BotW already holds about 60 fps at 1440p** ([COMPATIBILITY.md](COMPATIBILITY.md), and the
  timer regression patch 0011 found while testing it). 4K is 2.25 times the pixels. Most of Cemu's
  CPU work does not grow with resolution, so going from 1440p to 4K is mostly a question of GPU
  time, and of anything that leaves the GPU idle while there is work for it.
- **The GPU has room on paper.** PS5_Vulkan reports Eden's Vulkan renderer on this same driver
  holding 4K60 with the GPU about 19% busy, and mostly 60 at 8K at about 55%. BotW through Cemu is
  a heavier GPU load (a deferred renderer, Cemu's translated shaders, its surface copies), so that
  is a ceiling, not a forecast.
- **Nothing measures whether BotW is GPU-bound on the console.** That decides which of the changes
  below matter, which is why the first two are measurements. (Measured since: it is not. See the
  next section.)

## Measured on the console, 2026-10-13: BotW at 4K is bound by the CPU side

Profiles and boot logs of Breath of the Wild at 4K (FPS++ at 60, 120 Hz output with frame pacing at
60) on PS5-680: `~/sessions/2026-10-13-botw4k` (the 4.0.0 build then on the console, 709d296) and the
A/B sessions `~/sessions/ab-perf-*` in WSL, each with its ELF in `~/builds`.

**Before (709d296):** 54-61 fps, median 56.5 over the last 80 s; the GPU 58-71% busy, so not the
limit. Cemu's GPU thread was:

| Where its time went | Share | A PC pays |
| --- | --- | --- |
| Blocked in `sceAgcDriverSubmitDcb` (`agc_ms` 1,300-1,800 in each 10 s, 100-250 us a submission, almost none of it on the CPU) | 13-18% of its wall time | a few us a submission |
| `clock_gettime`, 890 ns a call (Cemu's vsync timer in the command loop; the driver's waits and its seven reads a submission) | about 10% of its CPU time | ~20 ns a call (vDSO) |
| Waiting for the emulated CPU (the command ring empty) | 10-20% | |

The emulated cores ran the same clock call too: about 20% of the main core's time, though most of
that in its idle loop, which on the main core never sleeps and checks the system events every turn.

**The first round (0f7012f):** patches/mesa 0008 (the driver's own submit thread, `asyncSubmit`),
0009 (the waits' sleep, `gpuWaitSleepUs` 200), Cemu's timers and the driver's clock on the TSC
(`tscClocks`, `driverClockTsc`, both only once the TSC's reported rate checks out:
`ps5clock::TscAgrees`, 1596.30 MHz reported, 1596.40 measured). A/B on the console, the same build,
alternating settings: the GPU thread went from 8-19% idle to 30-55% idle, and from 14% waiting on the
GPU to 1-3%. No run was slower. But the frame rate stayed about 58 at ~4,400 draws a frame either
way: the next limit is the emulated CPU.

**The emulated CPU (profiles of the same runs):** the third emulated core was busy 83-92%, and about
a fifth of it was not the game's code:

| Cost on core 2 | Share | Why it is the console's |
| --- | --- | --- |
| `_umtx_op` (a sleep or a wake in the kernel) | 6-9% | Cemu's scheduler lock is a pthread mutex outside Windows; a default-type mutex found taken sleeps in the kernel at once, and its unlock wakes the sleeper with another call |
| Spinning on `sTimerSpinlock` in `PPCTimer_getFromRDTSC`, inlined into every thread switch | ~1.4% | with the clocks on the TSC the main core's idle loop took that lock some ten million times a second |
| Two `MFENCE`s around every fiber switch | ~0.7% | FiberUnix's, copied by FiberPS5; Cemu's Windows fibers have none |
| `__emutls_get_address` and libkernel's `pthread_getspecific` | ~2.5% | every `thread_local` is emulated on the console |
| Switching between the game's threads (context, fiber, the thread map) | ~4% | the same code as on a PC, on a slower core |

**The second round (fbd3780, 3b0d0a5, 4ccc57d), awaiting its A/B:** Cemu patch 0037 (the scheduler
lock retried for `schedulerSpinUs` 20 before a thread sleeps on it; the main core's idle loop looks
at the system events every `idleEventsUs` 20 at most), `isolateCores` and `idleSpinUs` 100 on, the
fiber switch without fences, a cache line's round trip between cores in the boot log (which cores
share an L3, for where the four busy threads should sit), and `fastTls` (patches/compiler-rt: a
per-thread cache in front of `pthread_getspecific`, off until measured). The profiler now reads the
fibers' stacks, so the emulated cores' samples get callers: the next profile says who makes each
call into the kernel.

## How the driver works

It is upstream RADV and ACO (Mesa 26.2) with a winsys of its own in place of amdgpu
(`src/amd/vulkan/winsys/ps5`), a VideoOut swapchain in place of DRM's (`src/vulkan/wsi/wsi_common_videoout.c`),
and a few `RADV_PS5` changes in RADV itself. The GPU is described as GFX10.3 (Navi 21's register
programming and shader ISA) with GC 10.1.3's fixed-function traits, each found on the console first.

### Memory

- Every buffer and image is one direct-memory allocation of **type 12**, mapped for CPU and GPU read
  and write at one address, for its whole life. The GPU address is the CPU address, so nothing is
  ever "made resident" and mapping is free.
- Measured (2026-09-27): that memory is **CPU-cached and coherent both ways** for shader and copy
  access, so host-visible memory needs no flushes. Buffers zeroed at creation (the GTT domain) pay a
  memset and an eviction of the whole buffer once.
- Every Vulkan memory type RADV reports (device-local, host-visible, cached; the 2/3 "VRAM" and 1/3
  "GTT" split of the pool) is that same memory. The distinctions are bookkeeping.
- Descriptors, push constants and shader code sit in a 4 GiB window at `0x2_0000_0000`, because the
  shaders' 32-bit pointers carry high word 2. Everything else goes in a 256 GiB region at
  `0x40_0000_0000`, handed out in 2 MiB granules.
- Type 12 is the type the platform header calls the title's CPU-only memory. No other type was ever
  tried, and the GPU's bandwidth on it was never measured; Eden's numbers suggest it is fine.

### Command streams and submission

This is where the port differs most from Linux.

- The console faults the GPU on an `INDIRECT_BUFFER` into title memory (PS5_Vulkan B8), so **nothing
  is chained**: a command buffer's words live in ordinary `malloc` chunks, and every submission
  **copies all of them** into a 16 MiB ring of GPU memory (or a buffer of its own when larger than
  half the ring), then **evicts every line** it wrote with `CLFLUSHOPT`, then calls
  `sceAgcDriverSubmitDcb`.
- Each AGC submission **starts from reset GPU state** and is limited to 2^20 words (2026-09-26), so
  every one starts with RADV's full preamble, and a stream is split at points where RADV re-emits
  its state (every 512K words, at a draw or dispatch).
- Every submission ends with a `RELEASE_MEM` that flushes the colour and depth caches and **writes
  back and invalidates the GPU's L2**, GL1 and the vector caches (GCR `0x30c`: GL2_WB, GL2_INV,
  GL1_INV, GLV_INV), then writes the submission's sequence number to a marker.
- Without `sceAgcSuspendPoint` after it, a submission starts up to a refresh late (R68). A kick
  thread makes that call, so the submitting thread no longer waits the ~0.13-0.3 ms it takes. On a
  tester's base PS5 the late start happened *with* the suspend point too, which is unexplained.
- The submission path times itself for a statistics line every 10 s (`radv/ps5 submissions:
  window count= kwords= claim_ms= copy_ms= flush_ms= agc_ms= suspend_ms=`), with **seven reads of
  the monotonic clock per submission**. `clock_gettime` is a system call on the console: 20 us in
  R34, about 800 ns in R68.

### Completion and waits

- One queue for everything; submissions run in order. A sync object is a sequence number: a wait
  for a semaphore needs only its signal submitted, never a GPU wait.
- The CPU learns of completion by evicting the marker's line and reading it. A wait spins for
  1.5 ms, then **sleeps 1 ms between looks**. Each look, and each `vkGetFenceStatus` that finds its
  fence unsignalled, reads the clock.

### Presenting

- VideoOut takes five 32 MiB 3840x2160 framebuffers, registered once, as 64 KiB R_X tiles (no DCC on
  the swapchain); a 2560x1440 or 1920x1080 swapchain gets five of its own. FIFO is the only present
  mode. The driver configures 119.88 Hz when it first opens VideoOut, where `param.json` declares it
  (`PS5_VIDEOOUT_PARAM_JSON`, above) and the display runs at it, and a flip shows for at least as
  many vblanks as the flip rate says.
- A present goes to a **flip thread**, which waits for the frame's fence (the spin-then-1 ms-sleep
  wait above) and then calls `sceVideoOutSubmitFlip` for the next vblank. A frame that finishes
  just before a vblank can miss it while the thread sleeps. A 1 ms sleep is an eighth of a refresh
  at 119.88 Hz; patch 0003 makes it 200 us.
- `sceAgcDcbSetFlip`, a flip packet the GPU runs in the stream itself, works once VideoOut is open
  (2026-09-17) and was how PS5_Vulkan's first driver (ps5vk) flipped. RADV's swapchain does not use
  it; patch 0005 does, with `RADV_PS5_GPU_FLIP=1`.

### What the GPU cannot do here, and what it costs

| Missing | What the driver does instead | Matters for BotW? |
| --- | --- | --- |
| Legacy geometry shaders (they hang) | Every GS is NGG; a GS with streamout runs as compute (poly) | Only if a GS with transform feedback is used |
| `v_dot4_u32_u8` | NGG culling's workgroup repack uses a slower fallback | Possibly: NGG culling is on by default (test `nonggc`) |
| Flat scratch | Scratch through buffer instructions | Only for shaders that spill |
| Indirect buffers | No device-generated commands; no IB chaining (the copy above) | Yes: every submission is copied |
| Perf counters, VRS, RB+ | Not reported | No |

### Threaded recording

The fork has a layer of its own (`RADV_THREADED_RECORDING=1`): the application's `vkCmd*` calls
are queued with their arguments copied, and a worker thread records them while the application
goes on; `vkEndCommandBuffer` waits for the worker. It passed a 474,440-case CTS gate with every
case unchanged, and stays off until it measures faster in a game. **Playtendoh never sets it.**

## How Cemu drives it

From Cemu's Vulkan renderer at the pinned commit:

- **Submissions:** every 300 draws (`cemuSubmitDraws` in `ps5cemu.json` changes it, below), within
  10 draws of any occlusion query ending, on texture readbacks, and at each swap. Each waits on the previous one's semaphore and has a fence.
- **Polling:** `vkGetFenceStatus` on the oldest unfinished command buffers after every submit.
- **At swap:** a wait for the *previous* frame's last command buffer, so one frame is in flight.
- **All on one thread.** Cemu's GPU thread decodes the Wii U's command stream, records Vulkan,
  and submits. RADV submits at once (no wait comes before its signal), so the copy, the flush and
  `sceAgcDriverSubmitDcb` all land on that thread.
- **Guest memory as a GPU buffer** (`m_useHostMemoryForCache`, through
  `VK_EXT_external_memory_host`, which this driver supports) is compiled out upstream. Cemu
  uploads vertex and uniform data instead. The Metal backend uses the same idea on Apple's
  unified memory.

## Where a BotW frame's time can go

| Cost | Where | Size per frame | Source |
| --- | --- | --- | --- |
| Clock reads while submitting | Cemu's GPU thread | 7 per submission: 6-140 us each submission at R68/R34 costs, times 10-30 submissions | Estimate |
| Copy and eviction of every word | Cemu's GPU thread | Two passes over everything recorded | Code; host benchmark below |
| `sceAgcDriverSubmitDcb` | Cemu's GPU thread | `agc_ms` in the statistics line | Unmeasured for Cemu |
| RADV recording (state, descriptors, draws) | Cemu's GPU thread | Thousands of draws | Unmeasured; threaded recording moves it |
| Each AGC submission's state reset, preamble and L2 write-back and invalidate | GPU | A bubble at each submission boundary | Unmeasured |
| Waits waking up to 1 ms late | Swap wait; flip thread | Up to a missed vblank | Code |
| Base PS5 late start (R68) | GPU idle | Up to a refresh per idle start | Measured on one tester's console |

## Changes, ranked

### 1. Measure first (in this repo, no driver rebuild)

These are in the working tree alongside this document.

- **The driver's messages in `boot.log`** (`port/ps5/log.cpp`, `ps5log::ForwardDriverMessages`).
  RADV writes its statistics, the VideoOut mode it took (`wsi/videoout: 119.88 Hz refused ...`) and
  its errors to stderr, which only the klog sees. Lines starting `radv`, `wsi/`, `MESA` or `ACO`
  now also go to the boot log as `[driver] ...`; the rest of stderr goes where it went. A full pipe
  drops a line rather than stalling a writer.
- **`radvEnvironment` in `ps5cemu.json`** (`port/frontend/settings.*`, `port/main_ps5.cpp`): driver
  variables set before either emulator starts, for A/B tests without a rebuild. Only `RADV_`,
  `MESA_` and `ACO_` names are taken, and `RADV_DEBUG` stays `radvDebug`. For example:

  ```json
  "radvEnvironment": {"RADV_THREADED_RECORDING": "1", "RADV_PS5_GPU_TIME": "1"}
  ```

### 2. Driver patches (`patches/mesa`, against PS5_Mesa `7b59ef27`)

Built and run on the PC model: about 110,000 submissions in 11 s, including 3.1M-word command
buffers split into five AGC submissions in a buffer of their own, with RADV's assertions on (its
check that the words copied equal the words planned held throughout). The console branch compiles
against PS5_PayloadSDK's headers. None of them has run on a console. 0001-0004 were written against
`0b2d6d1a` and apply to `7b59ef27` unchanged (the four commits between touch only the swapchain);
0005 and 0006 were written against `7b59ef27`. RADV builds with all six there, and with 0006 alone.
`make radv` builds 0006-0009 by default (0005 needs 0001-0004 under it; 0008 and 0009 replace parts of 0001-0003 and do not apply over them); `RADV_PATCHES` chooses others ([below](#building-with-the-patches)).
0007 is not a performance change: it is the driver fix 3.0.0's archive was built with (Mesa
`13aa9a88`), which the newer driver lacks, against GPU faults that stop the console.

| Patch | What it changes | Expected effect | Risk |
| --- | --- | --- | --- |
| 0001 TSC timing | The submission path times with the TSC (about 12 ns); the statistics window reads the clock once per 64 submissions and calibrates its tick rate over the window itself (no TSC frequency assumed) | Removes 7 system calls per submission from Cemu's GPU thread | Low: only the statistics use it |
| 0002 Streamed copy | Words go into the ring with non-temporal stores; the eviction pass becomes one `SFENCE` | About half the copy cost (on a host Xeon, a 256 KiB submission: 72 us to 33-43 us) | Low: correct whether or not the CP's fetch sees CPU caches, which was never measured |
| 0003 Waits | `vkGetFenceStatus` (a zero timeout) reads no clock; waits past their spin sleep 200 us, not 1 ms | Fewer system calls; the flip thread and the swap wait wake within 0.2 ms of the GPU | Low: a few more wake-ups per long wait |
| 0004 GPU busy time | With `RADV_PS5_GPU_TIME=1`, each submission carries a start timestamp (`COPY_DATA` of the GPU clock, RADV's top-of-pipe timestamp) and an end one (the bottom-of-pipe `RELEASE_MEM` the driver's GPU clock read already uses), and the statistics line gains `gpu_busy_pct` | Says whether BotW at 4K is GPU-bound | Low: off by default, and then nothing changes |
| 0005 Flips on the GPU | With `RADV_PS5_GPU_FLIP=1`, a present puts `sceAgcDcbSetFlip`'s packets (mode 1, the flip's argument as their marker, as ps5vk flipped) on the queue in a submission of their own, behind the frame (`radv_ps5_submit_after`, which the swapchain links weak), instead of handing the frame to the flip thread | The GPU hands the flip to VideoOut as the frame's work ends: no core spinning on each frame's fence, no sleep between the frame and its flip, so a frame done just before a vblank makes it | Medium: off by default. Flips from the CPU and the GPU are never mixed out of order (a present goes to the thread only while the thread still holds flips, or when its frame's submission was deferred, which Cemu's never is), but whether VideoOut paces the GPU's flips with the flip rate as it paces `sceVideoOutSubmitFlip`'s is unmeasured |
| 0006 Refresh rate asked for again | `wsi_videoout_set_high_frame_rate(high)` configures the open output again once no swapchain presents on it: 119.88 Hz where `param.json` (`PS5_VIDEOOUT_PARAM_JSON`, read again) declares high-frame-rate output, else 59.94 Hz; the display modes offered follow. The app calls it, weak, before each Wii U game (`ps5display::ConfigureOutput`) | **120 Hz output works after the new launcher**, which opens VideoOut first at 59.94 Hz: without it, the driver keeps the launcher's rate for the process. The boot log says `VideoOut configured again for the next surface: 119.88 Hz` | Low: **in the default build**. A process that never calls it behaves as before; it refuses (-1) while a swapchain still presents |
| 0007 Freed buffers wait for the GPU | A destroyed buffer's direct memory waits, with the newest submission's sequence number, until the queue has run that far; what the GPU is done with is freed at each later destroy | **No GPU fault on memory freed while submitted work still uses it** (the whole console hangs on such a fault: Breath of the Wild's runes and scope did it before 3.0.0) | Low: **in the default build**, as in 3.0.0's driver. Memory is freed a little later |
| 0008 Submissions on a thread of their own | With `RADV_PS5_SUBMIT_THREAD=1` (the app's `asyncSubmit`), a submission is planned, numbered and copied into the ring where it is made; a thread of the driver's own flushes it and calls `sceAgcDriverSubmitDcb`, in order, spinning `RADV_PS5_SUBMIT_SPIN_US` (2000) before it sleeps. What AGC is told outside a submission (the GPU clock read, the tessellation rings) waits for it to empty | The GPU thread no longer blocks in AGC: measured 8-19% idle before, 30-55% after (BotW at 4K, 2026-10-13) | Low: stress-tested (the real code, up to 8 submitters, sleeps and wakes, a full job ring, a refused submission; clean under ThreadSanitizer); off unless asked |
| 0009 Waits' spin and sleep at run time | `RADV_PS5_WAIT_SPIN_US` and `RADV_PS5_WAIT_SLEEP_US` (1500 and 1000, as before; the app's `gpuWaitSleepUs`) for the waits for the GPU; `vkGetFenceStatus` answers without the clock. Replaces 0003's fixed 200 us | A wait wakes within a fifth of the time after the GPU gets there | Low: unchanged unless asked |

0005 on the PC model: 2,000,000 presents through three images, two submissions a frame (the frame
and the flip's 72 words); a frame held behind a timeline semaphore that another thread opens 30 ms
later presents on the GPU too; a run under the Khronos validation layer reports nothing. The boot
log says `[driver] wsi/videoout: flips on the GPU (RADV_PS5_GPU_FLIP)` and `the first flip on the
GPU`, and says so if a flip ever goes to the thread instead.

### 3. Settings to test (no rebuild)

| Test | Where | Why |
| --- | --- | --- |
| `RADV_THREADED_RECORDING=1` | `radvEnvironment` | Takes RADV's recording off Cemu's GPU thread, the thread that also decodes the Wii U's commands |
| `nonggc` | `radvDebug` | NGG culling pays the slow repack fallback here; Wii U geometry is light and 4K is pixel-bound |
| Accurate barriers off | In-game menu, Graphics | Fewer full pipeline barriers between Cemu's passes, which matter more at 4K |
| 120 Hz output and frame pacing at 60 fps | Settings > Video | A late frame costs 8.3 ms instead of 16.7 ms, so "mostly 60" looks closer to 60; with the driver before `7b59ef2`, 120 Hz never reached a jailbroken process |
| `RADV_PERFTEST=pswave32` | `radvEnvironment` | RADV recommends wave64 for pixel shaders; a quick check, low expectations |
| `RADV_PS5_GPU_FLIP=1` (patch 0005) | `radvEnvironment` | The flip on the GPU behind the frame: compare FPS and how often a frame shows for two refreshes at 119.88 Hz with frame pacing off |
| `cemuSubmitDraws` at 600, 1000 and 1500 | `ps5cemu.json` (a number; 0 is Cemu's 300) | Fewer submissions a frame, each a copy, an AGC submission, a state reset and an L2 write-back; too few delay the GPU's start on a frame. Read `count=` and `gpu_busy_pct` |

### 4. Driver changes proposed, not written

1. **Flip on the GPU: written, patch 0005** (`RADV_PS5_GPU_FLIP=1`, above). It needs a console run:
   whether VideoOut paces flips from the GPU with the flip rate, and what the flip thread cost.
2. **Fewer AGC submissions per frame: a setting now.** Each one resets state, replays the preamble
   and empties the L2. Cemu's 300-draw threshold (`VulkanRenderer::SubmitCommandBuffer`) comes from
   `cemuSubmitDraws` in `ps5cemu.json` (`patches/cemu` 0020, `port/app/emulator.cpp`; 0 keeps 300),
   read when the app starts. Too few submissions delay the GPU's start on a frame, so it is a
   measurement, not a default to change.
3. **Investigate the base PS5's late start (R68).** If each idle start of the GPU waits for a
   refresh on a base console, Cemu's many submissions are the worst case for it. A probe like R67's
   stamps, run on a base PS5 with several submissions per frame, would settle it.
4. **No copy at all.** Record straight into GPU memory, reserving room for the preamble before a
   stream and the completion after it, so a one-time-submit command buffer is submitted where it
   was written. Larger change; patch 0002 takes most of the cost away first.
5. **Waits that block on the GPU's interrupt.** On the PS4, `RELEASE_MEM` can raise an
   end-of-pipe interrupt that wakes a kernel event queue. Nothing in PS5_PayloadSDK's AGC header
   exposes one, but titles import `sceAgcDriverAddEqEvent` (PS5_Vulkan's `AGC_ENTRY_POINTS.md`, its
   NID from Kyty's table, never called), the SDK's stubs have the kernel's `sceKernelCreateEqueue`
   and `sceKernelWaitEqueue`, and VideoOut's `sceVideoOutAddFlipEvent`. A probe would register an
   end-of-pipe event, set the completion packet's `INT_SEL`, and time the wake-up against the
   marker; it would end the spinning and the sleep latency both, and the acquire's vblank polling
   with the flip event.
6. **Build at `-O3` with LTO.** The "release" archive is `-Dbuildtype=debugoptimized` (`-O2 -g`)
   with assertions off. A small CPU gain on recording, to be measured.
7. **Measure the GPU's bandwidth on type-12 memory**, and try the other types, with a streaming
   compute shader. Low priority given Eden's numbers.

Bigger, outside the driver: **Cemu's guest memory as a GPU buffer** (the compiled-out path above).
BotW's vertex and uniform uploads go away when the GPU reads the Wii U's RAM directly, and this
driver can import it (the port maps guest RAM as type-12 direct memory, which
`sceKernelMprotect` can give GPU access). Cemu's Vulkan path is unfinished upstream; its Metal
path is the model.

## Building with the patches

`make radv` (`tools/build-radv.sh`) builds the patches `RADV_PATCHES` names, and the app links that
build. PS5_Vulkan's recipe exports exactly the pinned revision (`git archive 7b59ef27`), so after
the recipe has run, the script applies the patches to the same revision in a temporary index of
the fork's repository, exports that tree, and builds it in `.deps/PS5_Vulkan/.deps/work/ps5cemu-radv-build`
with the recipe's own release options, cross files and `mesa_clc`, into
`.deps/PS5_Vulkan/.deps/native/radv-ps5cemu` (its `PROVENANCE.txt` lists the patches):

```sh
make release                          # 0006-0009, the default: 120 Hz output after the new launcher, freed buffers safe, the submit thread and wait timing the app can ask for
make release RADV_PATCHES=all         # the earlier experiments, 0001-0007, for the A/B runs below
make release RADV_PATCHES=0004,0006   # some of them, by number
make release RADV_PATCHES=            # none: the recipe's archive as it is
```

A change of patches recompiles only the files they touch, and `make build` links the app again
whenever the archive changed (`tools/build-cemu.sh`; ninja does not track it). The build id stays
the pinned revision while the patches touch only the winsys and WSI, which keeps existing shader
caches valid; the script gives a patch set that touches anything else, such as the compiler, the
patched tree's id instead, so caches start over. Moving from
`0b2d6d1a` to `7b59ef27` is a new id: each game's pipelines are compiled again once, on its first
start with the new driver.

The winsys's PC model (`-Dradv-winsys=ps5` without the cross files) builds on Ubuntu 24.04 with
`libdrm-dev` installed and `-Dc_args="-include host-types.h"` (and `cpp_args`), where `host-types.h`
includes `<linux/types.h>` outside assembly: `ac_surface.c` uses the kernel's `__u64`, which no
header includes when DRM is left out.

## Test plan

Same save, same spot, same graphic packs; 60 s per run, then **Settings > Diagnostics > Copy logs to
USB**. Busy places are the useful ones: Kakariko Village, Hateno Village, the view from the Great
Plateau tower.

1. Baseline at 1440p and at 4K, with `"radvEnvironment": {"RADV_PS5_GPU_TIME": "1"}` and the
   driver with every patch (`make release RADV_PATCHES=all`; steps 2, 3, 5 and 6 use it too).
   Record FPS and the `[driver] radv/ps5 submissions` lines. With 120 Hz output on,
   check the boot log says `VideoOut surface 3840x2160 at 119.88 Hz`.
2. The two columns of "Breath of the Wild at 4K60 and 8K30" as they stand, then with frame pacing
   off, for what pacing changes.
3. Each row of "Settings to test", one at a time, at 4K.
4. The default driver at 4K (`make release`: 0006 and 0007, which leave submissions as they were), for
   the other patches' own effect (`copy_ms`, `flush_ms` and FPS).
5. `RADV_PS5_GPU_FLIP=1` at 4K, with the 4K60 column's settings: the boot log's `flips on the GPU`
   lines, FPS, and with frame pacing at 60 fps whether the pacing still holds.
6. `cemuSubmitDraws` at 600, 1000 and 1500 at 4K, one per start of the app.

Reading the lines (per 10 s window):

- `count` divided by 10 s and by the FPS is **submissions per frame** (one of them the flip's, with
  `RADV_PS5_GPU_FLIP=1`).
- `copy_ms + flush_ms + agc_ms + claim_ms` is the **driver's submission cost on Cemu's GPU thread**;
  divided by the frames in the window, it is milliseconds per frame out of 16.7.
- `gpu_busy_pct` near 90-100 with FPS under 60 at 4K: **GPU-bound**. The levers are fewer
  submissions, `nonggc`, Accurate barriers off, graphic-pack costs (shadow resolution, AA), and if
  it still falls short, a lower internal resolution (1800p) scaled to the 4K output.
- `gpu_busy_pct` well under 80 with FPS under 60: **CPU- or latency-bound**. The levers are
  threaded recording, the patches, the flip on the GPU, and Cemu's own CPU settings.
