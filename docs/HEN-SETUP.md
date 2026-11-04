# Getting Playtendoh running on any firmware and HEN

Playtendoh is a homebrew app. It does not jailbreak your console; it runs on top of whatever
jailbreak and HEN you already have, and it is written to work with **any** of them. This page covers
two separate questions:

1. **[Can your PS5 run homebrew at all?](#1-can-your-ps5-run-homebrew)** — this depends on your
   firmware and is decided *before* Playtendoh, by the public jailbreak scene.
2. **[Given a console that runs homebrew, how do you get Playtendoh going?](#2-what-ps5cemu-har-needs)**
   — this is what the rest of the page is about, for every HEN and config.

After any change, open **Settings > Diagnostics** in the app: its first lines say exactly what the
app got (firmware, HEN, recompiler memory, `/data`). The [Setup check](#the-setup-check) on the
first start shows the same thing with a QR code to this page.

> [!IMPORTANT]
> **Legal use only.** Playtendoh plays games, keys and system files that *you* dump from hardware
> and software you own. It ships none, and this guide is not an invitation to download any. Running
> homebrew needs a modified console, which may void its warranty and breach the platform's terms.
> Use at your own risk.

---

## 1. Can your PS5 run homebrew?

Whether a PS5 can run *any* homebrew (and therefore Playtendoh) is set by its **firmware version**
and by the public exploits that exist for it. This is upstream of this project — we use the scene's
tools, we don't make them — and it **moves quickly**, so treat the table below as a snapshot and
check a current source such as [wololo.net's PS5 jailbreak
page](https://wololo.net/ps5-jailbreak-and-custom-firmware/) before you act on it.

Find your firmware on the console under **Settings > System > System Software > Console Information**
(or the app shows it in **Settings > Diagnostics**).

A jailbreak has two parts: an **entry point**, where the console first runs outside code (the web
browser, a Blu-ray disc, or the YouTube app), and a **kernel exploit** behind it, which takes over the
system so a HEN can load. The public routes:

| Route: entry point → kernel exploit | Firmware | Needs |
|---|---|---|
| Browser → **UMTX** | 1.00 – 5.50 | Nothing extra |
| YouTube app (**Y2JB**) → **Lapse** | 4.03 – 10.01 | An old version of the YouTube app (see below) |
| YouTube app (**Y2JB**) → **P2JB** | 9.00 – 12.40 (some report up to 12.70) | An old version of the YouTube app |
| Blu-ray (**BD-JB**) → **UMTX2** | up to 7.61 | A disc drive and a Blu-ray you burn |
| Blu-ray (**BD-JB**) → **Poopsploit** | up to 12.00 | A disc drive and a Blu-ray you burn |
| Browser → **Relapse** (published Sept 2026) | 7.00 – 13.60 | Nothing extra; every model, Slim and Pro included |
| None yet | **14.00 and newer** | Sony closed Relapse's hole in 14.00 (early Sept 2026), and **the PS5 cannot be downgraded**. See [Where it's impossible](#9-where-its-impossible-and-what-we-can-do) |

**So every firmware from 1.00 to 13.60 has at least one route, on disc and digital consoles alike.**
The browser covers 1.00–5.50 and 7.00–13.60, and the YouTube app covers the firmwares between them.
Each route then loads a HEN: etaHEN supports firmwares up to 12.70, and Kstuff Lite reaches 13.60.

> [!WARNING]
> **Y2JB on a console that isn't jailbroken yet** needs the YouTube app (`PPSA01650`) at an old
> version: 01.000.003 for firmwares 4.03 to 12.40, 01.000.030 from 12.60. The way to get it there
> is restoring a backup from USB, which **factory-resets the PS5**. Back up your saves first, and
> follow the [Y2JB project's instructions](https://github.com/Gezine/Y2JB) exactly.

Two things to keep in mind whatever your firmware:

- **These jailbreaks are tethered.** They don't survive a reboot or a full power-off. After every
  boot you run the exploit again (the exploit host page in the browser, the YouTube app, or the
  disc) and reload your HEN *before* starting Playtendoh. Rest mode usually keeps a jailbroken
  state; a cold boot does not.
- **Never update to chase a feature.** If your console is on a jailbreakable firmware, turn off
  automatic updates. There is no official way back down.

**The bottom line for this app:** if your console can load **etaHEN, OnionHEN, or even just an ELF
loader (elfldr) on port 9021**, Playtendoh will run. The sections below assume you are at that
point.

Sources for the firmware picture above (as of October 2026):
[wololo.net](https://wololo.net/ps5-jailbreak-and-custom-firmware/) ·
[GBAtemp's PS5 exploit guide](https://gbatemp.net/threads/ps5-exploit-guide.613891/page-974) ·
[Y2JB](https://github.com/Gezine/Y2JB) ·
[P2JB on Y2JB](https://github.com/matem6/P2JB-Y2JB-Porting) ·
[BD-JB](https://github.com/Gezine/BD-JB-1250) ·
[PSFree and Lapse](https://github.com/EchoStretch/PSFree/blob/main/README.md) ·
[Relapse 7.00–13.60 (GBAtemp)](https://gbatemp.net/threads/relapse-ps5-kernel-exploit-brings-homebrew-to-firmware-7-00-13-60.684829/) ·
[Relapse up to July 2026 firmware (Kotaku)](https://kotaku.com/new-ps5-jailbreak-exploit-works-on-systems-running-july-2026-firmware-2000738283) ·
[etaHEN and Kstuff coverage (onejailbreak)](https://onejailbreak.com/blog/kstuff-1.6.6-ps5-10-00-and-10-01-support/).

---

## 2. What Playtendoh needs

Once a HEN or ELF loader is running, the app needs three things, and it reaches for each of them by
itself:

| Need | Why | How the app gets it |
| --- | --- | --- |
| `/data` | Settings, saves, games, caches and logs all live in `/data/ps5cemu` | The HEN's app jailbreak, or the bundled `sandbox-elevator.elf` run through elfldr |
| Executable memory | Cemu's and Azahar's recompilers write and run code; without it, Wii U games fall back to the much slower interpreter | The HEN's JIT memory when granted, otherwise executable *direct* memory the app makes itself, which needs no HEN |
| USB and extended drives | Only if your games are there | The bundled `sandbox-elevator.elf` through elfldr, when a game folder or drive is refused |

### What the app does at startup, in order

1. It asks the HEN to jailbreak it, by writing `{"PID":n}` to `/download0/etahen_jailbreak` — the app
   jailbreak request etaHEN and OnionHEN both answer. The HEN only answers for title IDs in its
   allowlist, so **`PPSA99360` has to be listed there.** It waits ~5 s for the HEN to take the
   request and ~3 s more for it to finish.
2. It tests the HEN's JIT memory. If that isn't granted, it makes **executable direct memory**
   itself — no HEN grant needed — and runs a few bytes of real code to prove the kernel allows it.
3. If `/data` is still out of reach, it asks **elfldr** (port 9021) to run `sandbox-elevator.elf`,
   which opens the filesystem. It does the same later for a game folder or drive that's refused.

Because of step 2, **Wii U games run at full speed even with no app jailbreak at all** — the HEN only
has to open `/data` (or an elfldr has to be listening). This is the key to "works on any config."

---

## The Setup check

On the **first start** — and any time storage or the recompilers fail at start — Playtendoh shows a
**Setup check** screen: one row each for storage, recompilers, each side's games and keys, the 3DS's
system files and box art, with a status, what to do, and a QR code that opens the right page of this
guide on your phone. Green rows are ready; amber rows need something; grey rows are optional. You can
always reach the same information later under **Settings > Diagnostics**.

---

## 3. etaHEN (most tested)

1. Load etaHEN (via your firmware's exploit host).
2. Add `PPSA99360` to etaHEN's **app jailbreak list**, and turn on its **app jailbreak** option (in
   etaHEN's advanced or debug settings, depending on the version).
3. Start Playtendoh. Diagnostics should say `HEN: ok`, `JIT available (the HEN's)` and
   `/data reachable`.

etaHEN also provides the elfldr on port 9021 that the app uses for drives, so USB games generally
work with no extra steps.

---

## 4. OnionHEN

OnionHEN answers the same jailbreak request as etaHEN, but its allowlist is a line in its
`config.ini`:

1. Open OnionHEN's `config.ini` and find the `[app_jailbreak]` section.
2. Set `enabled=true`. Setting `debug_notifications=true` is worth it too: OnionHEN then shows a
   notification each time it jailbreaks an app, so you can see it worked.
3. Add `PPSA99360` to the **end** of `exact_title_ids`, with **no comma after it**:

   ```ini
   [app_jailbreak]
   enabled=true
   debug_notifications=true
   exact_title_ids=ITEM00001,NPXS39041,PKGI13337,PKGI12345,TOOL00001,PPSA99360
   ```

   OnionHEN drops the **whole list** if any entry is empty or isn't exactly 9 capital letters and
   digits. A trailing comma (`...,PPSA99360,`), a space-only entry or a lowercase ID all count, and
   then no app is jailbroken
   ([#12](https://github.com/premohq/PS5-ECAM/issues/12)). It allows 20 entries at most.
4. Reload OnionHEN (or restart the console and load it again), so it reads the new list.
5. Start Playtendoh. With the debug notifications on, OnionHEN says it jailbroke the app.
   Diagnostics should say `HEN: ok` and `/data reachable`.

---

## 5. Kstuff, GoldHEN, or any other HEN with no app jailbreak

Some setups (Kstuff on its own, a HEN without an app-jailbreak allowlist, or a plain exploit host
that only loads payloads) never answer the `/download0/etahen_jailbreak` request. Playtendoh still
runs — it looks after itself:

1. Load your HEN/exploit and make sure an **ELF loader is listening on port 9021** (elfldr; the
   etaHEN and OnionHEN payloads include one, and most exploit hosts can load `elfldr.elf`).
2. Start Playtendoh. It runs its bundled `sandbox-elevator.elf` through elfldr to open `/data`, and
   the recompilers use executable direct memory.
3. Diagnostics should say `JIT available (executable direct memory, no HEN needed)` and
   `/data reachable`. `HEN: no` here is **expected and fine** — it just means no HEN answered the
   app-jailbreak request; the app got everything it needs another way.

If no ELF loader is running, the app can't reach `/data` and says so at start: load one (or add
`PPSA99360` to a HEN's allowlist), then start the app again.

> **Note on the port.** The elevation helper connects to elfldr on the standard **port 9021**, which
> etaHEN, OnionHEN and the common `elfldr.elf` payload all use. If your loader listens on a different
> port, point it at 9021 (or run the standard elfldr alongside it).

---

## 6. Games on USB or extended storage

Some HENs open `/data` but not the drives (seen on 13.x,
[#16](https://github.com/premohq/PS5-ECAM/issues/16)). When a game folder or a plugged-in drive is
refused, the app runs `sandbox-elevator.elf` through elfldr to open it. Diagnostics then says
`... opened by the elevation helper`. This also needs an ELF loader on port 9021. The app checks
`/mnt/usb0`–`/mnt/usb7` and `/mnt/ext0`/`/mnt/ext1` as well as your chosen game folder.

---

## 7. Reading Diagnostics

**Settings > Diagnostics** (and the boot log) opens with four lines: the app and emulator versions,
the firmware, the privilege summary, and where the logs are. The privilege summary decodes like
this:

| Diagnostics says | Meaning |
| --- | --- |
| `Firmware 11.60 (0x...)` | Your console's firmware, as its own settings show it. Almost every bug report needs this. `unknown` means the query failed (rare). |
| `HEN: ok (HEN took the request after N ms ...)` | The HEN jailbroke the app. |
| `HEN: no (no HEN took the request ...)` | No HEN answered: `PPSA99360` isn't in its allowlist (check the comma for OnionHEN), or the HEN has no app jailbreak. The app carries on without one — this is fine if the next two lines are green. |
| `JIT available (the HEN's)` | The recompilers use the HEN's JIT memory. |
| `JIT available (executable direct memory, no HEN needed)` | The recompilers use memory the app made itself: full speed, no HEN needed. |
| `JIT unavailable (interpreter only)` | Neither worked: Wii U games run very slowly. Please report it with your firmware, HEN and `boot.log`. |
| `; no executable direct memory: 3DS games run on Azahar's interpreter` | The HEN gave JIT memory but the kernel refused executable *direct* memory; the 3DS recompiler needs the latter, so 3DS games run slower. Usually seen only with an app jailbreak on certain firmwares. |
| `/data reachable` | All good. |
| `/data unreachable (elevation helper: N)` | Neither the HEN nor elfldr opened `/data`: load an ELF loader on port 9021, or put `PPSA99360` in your HEN's allowlist. |
| `... opened by the elevation helper` | A refused drive or folder was opened by the bundled helper through elfldr. |

---

## 8. Troubleshooting matrix

| Symptom | Likely cause | Fix |
|---|---|---|
| App shows "cannot reach /data" and quits to a notice | No app jailbreak **and** no elfldr on 9021 | Add `PPSA99360` to your HEN's allowlist, or start an ELF loader on port 9021, then relaunch. |
| Diagnostics: `HEN: no` but `/data reachable`, games full speed | No app jailbreak, but elfldr + executable direct memory covered it | Nothing to fix — this is a fully working no-HEN setup. |
| Wii U games are extremely slow; `JIT unavailable (interpreter only)` | Neither JIT nor executable memory was granted | Add `PPSA99360` to the HEN's app-jailbreak list; if that's not possible on your HEN, report it with `boot.log` and your firmware — this is the one case the app can't work around alone. |
| OnionHEN never jailbreaks the app | A trailing comma or a malformed entry voided the **whole** `exact_title_ids` list | Put `PPSA99360` last with **no** trailing comma, all entries exactly 9 chars, ≤ 20 entries; reload OnionHEN. |
| USB / external games "cannot be read" | HEN opened `/data` but not the drives | Make sure elfldr is on 9021; relaunch. Diagnostics should then say the drive was "opened by the elevation helper." |
| Works once, dead after a reboot | The jailbreak is tethered | Re-run your firmware's exploit and reload the HEN after every cold boot, then start the app. |
| App won't appear / won't register on the home screen | HEN didn't jailbreak `PPSA99360`, or the folder is in the wrong place | Copy the folder to `/data/homebrew/PPSA99360` and list `PPSA99360` in your HEN before launching. |
| Everything green but a specific game crashes | Not a firmware/HEN issue | See the [compatibility list](COMPATIBILITY.md) and [report it](#reporting-problems) with logs. |

---

## 9. Where it's impossible, and what we can do

The project's goal is that **every console that can run homebrew can run Playtendoh**. Here is the
honest line between what we can fix and what we can't:

**What the app already does to reach the widest set of consoles** (no change needed from you):

- Runs the recompilers at full speed **without any HEN grant**, by making executable direct memory
  itself — so a plain elfldr setup is enough.
- Opens `/data` through elfldr when the HEN won't, and opens USB/extended drives the same way.
- Falls back to the interpreter rather than refusing to start when even that fails, so a game still
  runs (slowly) on the most locked-down setups.
- Reports firmware, HEN, recompiler memory and `/data` plainly in Diagnostics and the Setup check,
  so a failing setup is diagnosable instead of mysterious.

**What is genuinely out of reach, and why:**

- **Firmware 14.00 and newer, and only that.** Every firmware up to 13.60 has a public route, with or
  without a disc drive ([section 1](#1-can-your-ps5-run-homebrew)). From 14.00 there is no public
  exploit, so the console can't run *any* homebrew, and the PS5 can't be downgraded. This is a
  kernel-exploit problem, not an app problem — nothing Playtendoh could ship would change it. If
  and when the scene releases a route for a newer firmware, Playtendoh already works on top of
  whatever HEN it loads; this page will be updated with the coverage.

**What we're still improving** (tracked as issues, help welcome):

- Clearer, earlier messaging when the *only* missing piece is the app-jailbreak grant on a HEN that
  can't provide one, so the interpreter fallback is an informed choice rather than a surprise.
- Widening the drive-elevation probe as new mount points show up on newer firmwares.

If your console runs homebrew but Playtendoh doesn't, that's a bug we want — it almost always means
one of the three needs above wasn't met in a way we can handle better. Please
[report it](#reporting-problems).

---

## Reporting problems

Check the [compatibility list](COMPATIBILITY.md) first, then
[open an issue with the bug report form](https://github.com/premohq/PS5-ECAM/issues/new/choose).
It asks for your **firmware, HEN, app version and logs**, which almost every problem needs — all of
them are on the first lines of **Settings > Diagnostics**.

- **Copy logs to USB** (Settings > Diagnostics): puts the logs and settings in a dated
  `PS5-ECAM-logs-...` folder on a USB drive. The app keeps the last five sessions' logs, so copy
  them soon after a problem and attach them to your report.

When you report a problem, say which HEN and version you use, your firmware, and attach those logs.
