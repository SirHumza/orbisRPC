# HANDOFF — OrbisRPC PS4 Rich Presence

Date: 2026-09-24. Branch: `main` (pushed). Console: PS4 9.00, GoldHEN 2.4,
`192.168.1.136` (FTP :2121 anon, klog :3232, BinLoader one-shot :9020).

## Folder layout (repo root)

- `orbisrpc/` — all daemon sources. Key files: `daemon.c` (loop/sessions),
  `discord.c` (presence serialize), `detect.c` (game finding), `tmdb.c` +
  `art_table.h` (Sony metadata), `art.c` (mp: proxy resolve), `tls.c` +
  `ws.c` (transport), `updater.c` + `manifest.c` + `health.c` (updates),
  `timesync.c` (SNTP), `lock.c`, `cfg.c`, `log.c`, `jsonlite.c`.
- `scripts/build_sdk.sh` — builds the daemon payload with ps4-payload-sdk
  (`build-sdk/orbisrpc_sdk.elf`). Toolchain: `~/ps4-payload-sdk`, shim in
  `~/llvmshim`. NEVER link `libkernel.so` (instant death in spawn context;
  gate in CI + `tests/e2e_consumer.py`).
- `tests/` — host unit tests + e2e contracts. `make test`, `make asan`.
- `scripts/build_art_table.py` + `titles.txt` — regenerates art table/icons.
- `docs/injecting.md` — how payloads get onto the console (one-shot rule!).
- `findings/` — hardware research archive (loaders, TLS PSA, IME, linkage).
- `scripts/ps4_watch.py` — one-shot console state diff (repo copy may be
  absent; logic: FTP file sizes + ports + newest screenshot).

## Existing issues (ranked, all reproduced)

1. **Cover art tile** — shows "?" instead of game art. mp: proxy renders
   (proven live), uploaded asset stuck `private`, external URLs drop the
   whole activity. Daemon resolves mp: per session (`art: resolved mp` in
   log). Needs: propagation wait or per-title asset strategy (below).
2. **Stale title on fresh launches** — save-mtimes get bulk-touched by
   cloud sync (all 28 titles shared one mtime!); app.pkg atime added as
   sharper signal; eboot-set change fast-path added. Unproven live.
3. **Game crashes (CE-34878-0)** on some titles with daemon on — no
   mechanism found after full audit; console has prior crash history.
   Needs clean-boot test (reboot, no payload, launch).
4. **IME keyboard** never opens (0x80BC0010 through six fixes). Token is
   pre-seeded via FTP; do not block on this.
5. **Rest Mode** — wake/resume coded, never tested through a real cycle.
6. **Updater drill** — staged-update path never tested live.
7. **Plugin path** — PRX can't deploy via FTP (console converts SELF→ELF);
   must ship inside PKG. Untested live.
8. **`main` vs `WIP` branches** — main = proven runtime line; WIP = old
   1.0 product line (installer/app/docs). Do not merge blindly.

## What could be better (next agent: pick these up)

- **Rest Mode field test** + home-screen presence proof.
- **Updater live drill** (stage → reboot → rollback check).
- **IME**: needs on-console iteration with return-code logging (already
  instrumented in `pkg/app_main.c` on the WIP line).
- **Plugin live test** via PKG delivery.
- **e2e/CI**: `.github/workflows/ci.yml` exists; extend coverage for new
  modules (lock, timesync, art, session persistence).
- **Playtime ledger reader** (totals per title; data already banked).
- **Media-type table**: only Netflix/YouTube mapped; add IDs as found.

## Auto icons from Sony CDN (no manual adding) — the design

Goal: never hand-add a title again. Three layers, in order:

1. **Live TMDB-over-TLS** (`tmdb_https_get()` in `tmdb.c`, already coded):
   queries `tmdb.np.dl.playstation.net:443` for any title ID at resolve
   time. Port 80 is blocked on-console; **443 was never proven — first
   job is running `probe_tmdb443.elf` on hardware.** If green, every
   current and future game resolves fully automatically.
2. **Self-growing table fallback**: daemon already logs unknown title IDs
   implicitly (raw-ID sessions). Add a CI job: parse `titles.txt` +
   any new IDs seen, run `build_art_table.py`, commit regenerated
   `art_table.h` + icons. Table then grows itself with zero hand edits.
3. **mp: proxy at post time** (`art.c`, proven rendering): whatever URL
   layers 1–2 produce gets proxied per session; cache expires per game
   switch so mappings never go stale.

Rule: hands touch art ZERO times — new game installs resolve through
layer 1, or land in the table via layer 2's automation.
