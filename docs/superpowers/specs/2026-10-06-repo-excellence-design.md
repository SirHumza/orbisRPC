# Repo excellence batch — design spec

Date: 2026-10-06. Base: `main` at CyberMask367's line + CI/README fixes.
Constraint (hard): zero changes under `orbisrpc/`, `installer/`,
`tools/`, `tests/`, `scripts/` behavior — PS4 code and build scripts
stay byte-identical. Only repo furniture changes.

## Goal

Test builds reach testers through GitHub (not just Discord uploads),
contributors get templates and hygiene, CI stops warning, and a stale
binary leaves the tree.

## Workstreams

### 1. Tag-triggered releases

New `.github/workflows/release.yml`, runs on `push.tags: v*`.
Reuses the exact SDK recipe from `ci.yml` (checkout SDK from git, pin
`573b4a0`, clang-18 build, firmware gate, daemon build, linkage gate),
then attaches `build-sdk/orbisrpc_sdk.elf` to the GitHub Release with
a tester-oriented body: delete `/data/orbisRPC`, run once, `SET_ME`
slot, re-run, Discord invite link. No PKG, no installer job (OpenOrbis
toolchain unavailable on runners; PKG ships with the official release).

### 2. Templates

- `.github/ISSUE_TEMPLATE/bug_report.yml` — requires: build string
  (`daemon start — build …` line), firmware, symptom, `log.txt`
  attached, nanoDNS/DNS answers. Points testers at Discord first.
- `.github/pull_request_template.md` — checklist: host tests + ASan +
  e2e run, no tokens committed, docs updated if behavior changed,
  no `libkernel.so`.

### 3. CI hygiene

- `actions/checkout@v4` → `v5` (kills the Node-20 deprecation warnings;
  upload-artifact stays `v4`, current latest major).
- `concurrency: group: ci-${{ github.ref }}, cancel-in-progress: true`
  on both workflows (README pushes currently burn full matrix runs).
- `.github/dependabot.yml` — weekly `github-actions` updates.

### 4. Binary cleanup

`git rm eboot.bin` (86 KB stale root binary). Shipping path is the
`orbisrpc-test-build` CI artifact; `*.elf` and `build-sdk/` are already
gitignored. Verify no script/CI/docs reference the root `eboot.bin`
before removal (only stale mentions may remain — fix those in place).

### 5. Version traceability

CI artifact keeps the stable name `orbisrpc-test-build` (per-run
artifacts don't collide), and the release body stamps commit SHA +
build date. No new version constants in code (license/versioning still
owner-decided).

## Out of scope

PKG building in CI, evict.elf artifacts, README rewrites, any `.c`
edits, LICENSE choice.

## Acceptance

- `git status` shows changes only outside `orbisrpc/ installer/ tools/
  tests/ scripts/` (plus `.github/`, root md, `eboot.bin` deletion).
- Full host suite green locally (`test`, `asan`, e2e ALL PASS).
- `ci.yml` / `release.yml` parse as YAML; release workflow is
  tag-gated (no run on `main` pushes).
- Push to `main` → CI green; no new warnings besides the Ubuntu-26
  future notice.
