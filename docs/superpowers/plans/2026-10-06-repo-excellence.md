# Repo excellence batch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship tag-triggered ELF releases, contributor templates, quiet green CI, and a binary-free tree without touching PS4 code.

**Architecture:** Five independent tasks, each one file-group plus its own check. CI YAML validated by parse; templates by required-field grep; binary removal by `git ls-files`; everything finished with the untouched-code proof (`git diff --stat` shows nothing under `orbisrpc/ installer/ tools/ tests/ scripts/`) and the host suite.

**Tech Stack:** GitHub Actions (`actions/checkout@v5`, `actions/upload-artifact@v4`, `softprops/action-gh-release`), dependabot, Markdown templates.

**Spec:** `docs/superpowers/specs/2026-10-06-repo-excellence-design.md`

## Global Constraints

- Zero changes under `orbisrpc/ installer/ tools/ tests/ scripts/` — PS4 code and build scripts stay byte-identical.
- SDK pin stays `573b4a0` with `fetch-depth: 0` (a raw SHA is not a branch/tag ref).
- Daemon-world linkage set only: `libkernel_web.sprx`, `libSceLibcInternal.sprx`, `libSceNet.sprx`, `libSceSystemService.sprx` — never `libkernel.so`.
- No PKG building in CI; no `evict.elf` artifacts; no LICENSE choice.
- Uploads keep the name `orbisrpc-test-build` with `if-no-files-found: error`.

## Review Focus

- A tag push firing CI's full matrix instead of only the release job — check `release.yml` scoping so `main` pushes never trigger it.
- A release body leaking the webhook URL or a token — body is static text, no secrets interpolated.
- Dependabot opening noise PRs against PS4 code — scope is `github-actions` ecosystem only, weekly.
- `eboot.bin` referenced somewhere after deletion — grep the tree for `eboot.bin` minus the installer-staged `installer/eboot.bin` and `build-sdk` outputs.
- Checkout v5 changing SDK pin behavior — pin step stays a separate `git -C sdk checkout` run step, unaffected by the checkout major.

---

### Task 1: Tag-triggered ELF releases

**Files:**
- Create: `.github/workflows/release.yml`
- Test: YAML parse + tag-gate grep (no runner needed)

**Interfaces:**
- Consumes: SDK recipe from `.github/workflows/ci.yml` (pin `573b4a0`, clang-18, firmware gate, `./scripts/build_sdk.sh`, linkage gate).
- Produces: `orbisrpc_sdk.elf` attached to the GitHub Release; nothing other tasks consume.

- [ ] **Step 1: Create `.github/workflows/release.yml`**

Trigger only:
```yaml
on:
  push:
    tags:
      - 'v*'
```
One job `build-and-release` (`ubuntu-latest`) with steps: checkout repo → checkout SDK (no ref, `fetch-depth: 0`) → pin `git -C sdk checkout 573b4a0` → install clang-18/lld-18/llvm-18 → `make -C sdk DESTDIR="$HOME/ps4-payload-sdk" clean install` → firmware gate (`grep -q "case 0x1352:" sdk/crt/patch.c`) → `./scripts/build_sdk.sh` with `PS4_PAYLOAD_SDK`/`PS4_SDK_SRC` exports → linkage gate (same four `grep -q` lines as ci.yml plus the `libkernel\.so` forbid) → attach via `softprops/action-gh-release@v2` with `files: build-sdk/orbisrpc_sdk.elf`, `generate_release_notes: true`, and a `body:` containing the tester flow (delete `/data/orbisRPC`, run once, `SET_ME` slot, re-run) plus the `https://discord.gg/BWEyfcT7ZQ` invite. No secrets interpolated in the body.

- [ ] **Step 2: Validate the workflow file**

Run: `python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/release.yml')); print(sorted(d[True] if True in d else d['on'].keys() if isinstance(d.get('on'), dict) else [d.get('on')]))"`
Expected: parses, trigger keys include `push` with tags `v*`.

Run: `grep -c "softprops/action-gh-release" .github/workflows/release.yml`
Expected: `1`.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml docs/superpowers/specs/2026-10-06-repo-excellence-design.md
git commit -m "ci: tag-triggered ELF releases to GitHub"
```

### Task 2: Issue + PR templates

**Files:**
- Create: `.github/ISSUE_TEMPLATE/bug_report.yml`
- Create: `.github/pull_request_template.md`

- [ ] **Step 1: Create `.github/ISSUE_TEMPLATE/bug_report.yml`**

Form fields (all required except Discord name): build string (placeholder: the `daemon start — build …` log line), firmware (e.g. `9.00`–`13.52`), symptom, steps, `log.txt` upload (file type), nanoDNS answers (blocker on/off, `127.0.0.1` set, exception added, rebooted). Intro body points testers at Discord `testers-chat` first.

- [ ] **Step 2: Create `.github/pull_request_template.md`**

Checklist: host tests + ASan + e2e run with outputs; no tokens/webhook URLs committed (`ORBISRPC_STRICT_SECRETS=1` e2e green); docs updated if behavior changed; linkage set unchanged; `orbisrpc/` diff is empty unless the PR is a daemon change with on-console proof.

- [ ] **Step 3: Verify required fields exist**

Run: `grep -E "log.txt|firmware|127.0.0.1" .github/ISSUE_TEMPLATE/bug_report.yml && grep -c "STRICT_SECRETS\|libkernel.so" .github/pull_request_template.md`
Expected: three matches in the form, count `2`.

- [ ] **Step 4: Commit**

```bash
git add .github/ISSUE_TEMPLATE/bug_report.yml .github/pull_request_template.md
git commit -m "github: bug report form and PR checklist"
```

### Task 3: CI hygiene (checkout v5, concurrency, dependabot)

**Files:**
- Modify: `.github/workflows/ci.yml` (3 spots), `.github/workflows/release.yml` (2 spots: checkout line, concurrency block)
- Create: `.github/dependabot.yml`

- [ ] **Step 1: Bump `actions/checkout@v4` → `v5` in both workflows**

Run (per file): replace-all `uses: actions/checkout@v4` with `uses: actions/checkout@v5`. Leave `actions/upload-artifact@v4` and `softprops/action-gh-release@v2` pinned.

- [ ] **Step 2: Add concurrency to both workflows**

Insert at top level of each file:
```yaml
concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: true
```
Release file uses the same group key shape (`release-${{ github.ref }}`).

- [ ] **Step 3: Create `.github/dependabot.yml`**

```yaml
version: 2
updates:
  - package-ecosystem: "github-actions"
    directory: "/"
    schedule:
      interval: "weekly"
```

- [ ] **Step 4: Validate**

Run: `python3 -c "import yaml;[yaml.safe_load(open(f)) for f in ['.github/workflows/ci.yml','.github/workflows/release.yml','.github/dependabot.yml']]; print('YAML OK')"`
Expected: `YAML OK`.

Run: `grep -rn "checkout@v4" .github/workflows/ || echo "no v4 left"`
Expected: `no v4 left`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/release.yml .github/dependabot.yml
git commit -m "ci: checkout v5, concurrency cancel, dependabot for actions"
```

### Task 4: Remove stale root `eboot.bin`

**Files:**
- Delete: `eboot.bin` (tracked, ~86 KB)

- [ ] **Step 1: Confirm nothing live references it**

Run: `grep -rn "eboot.bin" --exclude-dir=.git --exclude-dir=build-sdk . | grep -v "installer/eboot.bin\|build-sdk\|docs/research/retired-tables" || echo "no live refs"`
Expected: `no live refs` (stale doc mentions fixed in place if any appear outside research history).

- [ ] **Step 2: Remove and verify**

Run: `git rm -q eboot.bin && git ls-files | grep -c "^eboot.bin$" || echo "gone"`
Expected: `gone`.

- [ ] **Step 3: Commit**

```bash
git commit -m "chore: drop stale root eboot.bin (artifacts ship via CI)"
```
(Note: `git rm` already staged it; commit directly.)

### Task 5: Untouched-code proof + full suite + push

**Files:** none (verification only).

- [ ] **Step 1: Prove PS4 code untouched**

Run: `git status -sb && git diff HEAD --stat -- orbisrpc/ installer/ tools/ tests/ scripts/ | wc -l`
Expected: status lists only `.github/`, root md, `eboot.bin` deletion; second command outputs `0`.

- [ ] **Step 2: Run the full host suite**

Run: `make -C tests test 2>&1 | tail -1`
Expected: `utility tests passed`.

Run: `make -C tests asan 2>&1 | tail -1`
Expected: `utility tests passed`.

Run: `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py 2>&1 | tail -1`
Expected: `E2E host simulation: ALL PASS`.

- [ ] **Step 3: Push and watch CI**

Run: `git push origin main && sleep 150 && gh run list --repo SirHumza/orbisRPC --limit 1`
Expected: push succeeds; newest run for the pushed SHA is `completed success` (allow one rerun cycle: if red, read `--log-failed` and fix forward, never force-push over it without reading).
