# Foreground detection: from heuristics to asking the OS

## Superseded

The offset-hard-coded process-table approach described here is **no longer on the
detection path**. It was removed 2026-10-03. This file is kept because the
surviving users of the offset-free walk it motivated are still live, and because
the reasoning is what justifies them.

## What was removed

`scan_sandbox_mount()`, `scan_newest_save()`, `proc_has_eboot()`,
`detect_eboot_count()`, `note_proc_layout()`.

All four signals were *inferences* about what the console was doing:

- a `/mnt/sandbox/TITLEID_000` mount existing
- whichever savedata / app dir / `app.pkg` had the newest mtime
- whether any process was named `eboot.bin`
- how many were

Each is a proxy for "a game is in the foreground", and every proxy can be
wrong: a background app can hold a sandbox mount, cloud sync bulk-touches
mtimes, and the process-table leg depends on `kinfo_proc` offsets that move
between firmwares (see below).

## What replaced it

The OS answers the question directly:

```c
int32_t app_id = sceSystemServiceGetAppIdOfBigApp();  /* what is in front */
sceLncUtilGetAppTitleId((uint32_t)app_id, title_id);  /* ...as a TITLEID */
```

Both live in `libSceSystemService.sprx`. No offsets, no directory scans, no
mtimes, and the TITLEID comes back in the same call that established the state,
so identity and state cannot disagree.

### The distinction that carries the design

| Reading | Meaning | Presence |
|---|---|---|
| `app_id == -1` | nothing in front | may clear |
| `app_id == 0` / `NPXS` id | system app in front | **hold** — a game is probably still running |
| any negative | unreadable | **hold** — never treat as a close |
| a 9-char `[A-Z0-9]` id | a game | post it |

Collapsing the middle two rows into "no game" is what made presence vanish
while a game was open. `detect_current_game()` therefore returns three
separate codes: `-1` closed, `-2` uncertain, `-3` system app covering a live
game. Both `-2` and `-3` suppress the clear path in `daemon.c`.

Classification lives in `orbisrpc/bigapp.c`, separate from the syscalls, because
`detect.c` cannot be host-compiled at all (`dlfcn.h`, `orbis/` headers). That
is the same split `procwalk.c` exists for.

## Settings

ShellUI logs every scene change into `kern.msgbuf`. Reading the newest
`OnFocusActiveSceneChanged` entry answers "is Settings in front" without
inference. Verified against a real console capture: the settings scenes carry
`: SettingPage` (`settings_root`, `storage_data#storage`, `id_goldhen_menu`).

`detect_settings_active()` returns 1 / 0 / **-1**, and -1 is never folded into
0 — an unavailable probe is not evidence that Settings closed. Settings overlays
a running game without closing it, so the game stays posted with its timer and
only the activity state line swaps (`presence_state_settings`, default
`"In Settings"`).

### Buffer sizing

`sysctlbyname("kern.msgbuf")` fails with **ENOMEM when the supplied buffer is
smaller than the ring**. A hardcoded 128 KB read therefore fails on *every*
poll against a larger ring, leaving the feature permanently dead while still
looking configured. The size is taken from the kernel's own answer
(`sysctlbyname(name, NULL, &len, ...)`), `mmap`ed once, 2 MB cap, and proven
with a real read before being committed to.

A sibling payload hit exactly this and needed a dynamic-size rework; it is the
reason the sizing is not a literal here.

## Still true: offsets are not self-describing

`struct kinfo_proc` field offsets shift between firmwares. That is why the
detection path no longer reads any of them. Two survivors still walk the
process table and now use `procwalk.c`, which keeps the self-describing
`ki_structsize` framing but searches inside a record instead of trusting a byte
position:

- `lock.c` — single-instance guard
- `tools/evict.c` — stop a running daemon before replacing it

Measured, old logic vs `procwalk.c`:

| Layout | old | new |
|---|---|---|
| 9.00 (name @447, record 479 B) | 1 | 1 |
| record 736 B, name @611 | **0** | 1 |
| record 520 B, name @120 | **0** | 1 |

A hit is corroboration, not proof: records are zero-padded, so both callers
reject `pid <= 0` before searching, and their failure biases are opposite
(`lock.c` assumes a peer on an unreadable table; `evict.c` refuses to kill).

## Cost of the change

`libSceSystemService.sprx` is now a hard `NEEDED` entry. If it fails to bind,
the payload dies before `main()` with no diagnostics — the same silent failure
class as the SDK firmware gate. Previously detection failing only broke
detection. `.github/workflows/ci.yml` asserts the module is present so the link
cannot silently lose it.

## Unproven

None of this has run on a console. Specifically unverified: that
`sceSystemServiceGetAppIdOfBigApp` reports the same values in a *spawned*
payload as it does in a process the user launched, and that Settings detection
survives a ring larger than the probed size.