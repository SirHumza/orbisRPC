/* installer.c - clean installer, one flow forward.
 * Payload placement is the whole job: copy to /data/payloads.
 * Payload Guest reads this directory. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include "ui.h"
#include "icfg.h"
#include "goldhen.h"
#include "nanodns.h"
#include "notify.h"
#include "uibridge.h"
#include "carrykeys.h"
#include "paths.h"
#include "installer_flow.h"

#ifndef SETUP_VERSION
#define SETUP_VERSION "1.0.0"
#endif

/* Every on-console path -- payload, config, logs, version marker, staging, and
 * the one line this app adds to each shared config -- is in paths.h, so that
 * uninstall removes exactly what install writes. See that header for why the two
 * flows must not hold their own copies.
 *
 * What is left here is what only install touches: sources inside the read-only
 * PKG, and the version this setup binary was built as. */
#define DAEMON_ELF  "/app0/assets/" PAYLOAD_NAME
#define CFG_SOURCE "/app0/assets/config.json"

/* Which version is installed, written at install time and read back by the
 * update check.
 *
 * Comparing against SETUP_VERSION alone is wrong in a way that gets worse over
 * time: that constant is whatever the *installer* was built as, so after
 * installing 1.70 this binary still reports 1.0.0 and offers 1.70 again on
 * every run, forever. "Up to date" never becomes reachable.
 *
 * A marker on disk is the only version that reflects what is actually
 * installed, and it survives the user updating the payload by hand -- which
 * re-installing the setup package does not. (INST_VER is in paths.h.) */

/* Stage log: every copy step records errno + sizes to a file we can read
 * back over FTP. The dialog alone can't say WHICH stage failed. */
static const char *g_stage = "-";

static void ilog(const char *tag, int err, long expect, long got){
    FILE *f;
    g_stage = tag;
    f = fopen(INST_LOG, "a");
    if(!f) f = fopen("/data/install.log", "a");   /* fallback if sandbox blocks */
    if(!f) return;
    fprintf(f, "%ld %s errno=%d(%s) expect=%ld got=%ld\n",
            (long)time(NULL), tag, err, err ? strerror(err) : "ok", expect, got);
    fclose(f);
}

/* Stage marker without errno semantics (rc values, not errnos). */
static void ilogv(const char *tag, long a, long b){
    FILE *f;
    g_stage = tag;
    f = fopen(INST_LOG, "a");
    if(!f) f = fopen("/data/install.log", "a");
    if(!f) return;
    fprintf(f, "%ld %s a=%ld b=%ld\n", (long)time(NULL), tag, a, b);
    fclose(f);
}

/* Two strings, for the values a number cannot carry: version tags, URLs,
 * paths. Guessing at these with atoi or strlen produced logs that were
 * technically present and practically useless. */
static void ilogs(const char *tag, const char *a, const char *b){
    FILE *f;
    g_stage = tag;
    f = fopen(INST_LOG, "a");
    if(!f) f = fopen("/data/install.log", "a");
    if(!f) return;
    fprintf(f, "%ld %s [%s] [%s]\n", (long)time(NULL), tag,
            a ? a : "(null)", b ? b : "(null)");
    fclose(f);
}

/* Reads the installed version marker into `out`. Falls back to SETUP_VERSION
 * when it is absent, unreadable, or empty -- which is the fresh-install case
 * and the case where an older setup package predates the marker. */
static void read_installed_version(char *out, size_t cap){
    FILE *f;
    size_t n = 0;

    if(!out || cap == 0) return;
    snprintf(out, cap, "%s", SETUP_VERSION);

    f = fopen(INST_VER, "rb");
    if(!f) return;
    while(n + 1 < cap){
        int c = fgetc(f);
        if(c == EOF || c == '\n' || c == '\r') break;
        out[n++] = (char)c;
    }
    fclose(f);
    out[n] = 0;

    if(n == 0) snprintf(out, cap, "%s", SETUP_VERSION);
}

/* Records what actually landed. Truncates first: a longer marker left over
 * from a bigger version number would otherwise make the file read back as
 * "1.7" and then "1.70" never compares correctly. */
static void write_installed_version(const char *version){
    FILE *f = fopen(INST_VER, "wb");
    if(!f){
        ilog("ver-write", errno, 0, 0);
        return;
    }
    fputs(version && version[0] ? version : SETUP_VERSION, f);
    if(fputc('\n', f) == EOF || fclose(f) != 0)
        ilog("ver-close", errno, 0, 0);
    else
        ilogv("ver-ok", 0, 0);
}

static int copy_file(const char *src, const char *dst);
static int mkdirs(const char *path);
static int place_config(const char *src);

/* Copy the payload through unchanged: ELF in, ELF out. */
static int copy_payload(const char *src)
{
    char report[320];
    int ok;

    mkdir(INST_DIR, 0777);
    mkdirs("/data/payloads");

    ok = copy_file(src, PAYLOAD_ELF);
    ilog("payload-copy", ok, 0, 0);

    snprintf(report, sizeof report,
             "%s : %s%s%s%s",
             PAYLOAD_ELF, ok == 0 ? "OK" : "denied",
             ok == 0 ? "" : " [stage ", ok == 0 ? "" : g_stage, ok == 0 ? "" : "]");
    uibridge_ok("Payload copied", report);

    /* Circle is the way out of the flow, and this is the one place in it where
     * leaving is not safe on its own.
     *
     * Everything before this screen wrote nothing, and everything after it has
     * already finished the install -- the config, the logo, the version marker
     * and the optional prompts all land as one block once this returns. This
     * screen sits in the gap between them, with the payload on disk and no
     * config beside it.
     *
     * That combination is the one state this flow must never leave behind: the
     * next run opens on a payload, calls it "already installed", and tells the
     * user to go and uninstall something they cannot now finish. So taking the
     * payload back off is not tidiness, it is what makes cancelling here mean
     * anything at all.
     *
     * Only the payload. The logo has not been written yet, and no config
     * exists to remove -- this is the narrowest possible rollback. */
    if(uibridge_cancelled()){
        int rc = remove(PAYLOAD_ELF);
        ilog("payload-cancelled", rc, 0, 0);
        return -2;
    }

    if(ok != 0){
        uibridge_ok("Install failed",
                    "Could not write the payload. Stopping here.");
        return -1;
    }

    ilogv("report-done", ok, 0);
    return 0;
}

/* Preflight: the payload must exist somewhere. Either the PKG carries one, or
 * phase 1 staged a download. */
static int step_assets(void){
    FILE *a = fopen(DAEMON_ELF, "rb");
    if(a){
        fclose(a);
        return 0;
    }
    uibridge_ok("Missing payload",
                "This install is missing its payload.\n\n"
                "Reinstall the PKG (do not just relaunch it), "
                "then open it again.");
    return -1;
}

/* ---- step 1: is it already installed? -------------------------------
 * The check is on the live payload only. A config.json without a payload is a
 * half-finished install, not an install, and refusing on that would block a
 * repair over a broken one.
 *
 * Reads without stat(): the sandbox reports sizes it does not have, and only
 * the open succeeding is being asked about here.
 *
 * There is no "install anyway" path. Installing means overwriting a running
 * payload, and with evict.elf gone there is no way to unload the old one
 * first -- so the honest move is to stop and tell the user to uninstall. */
static int step_installed(void)
{
    FILE *f = fopen(PAYLOAD_ELF, "rb");

    if(!f) return 0;
    fclose(f);

    ilog("installed-stop", 0, 0, 0);

    uibridge_ok("Already installed",
                "orbisRPC is already installed.\n\n"
                "Uninstall it first, then run this again.");

    return -1;
}

/* The sandbox makes stat()/fstat() lie about sizes (observed: 4096/4160
 * for a 2071552-byte file, which made a perfect copy report "denied").
 * open/read/write are truthful, so the proof is: count and hash what we
 * wrote, then count and hash what we read back. No stat anywhere. */
static unsigned long long fnv1a(const unsigned char *p, size_t n,
                                unsigned long long h){
    size_t i;
    for(i = 0; i < n; i++){
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int copy_file(const char *src, const char *dst){
    FILE *in, *out;
    static unsigned char buf[65536];
    size_t n;
    long written = 0, reread = 0;
    unsigned long long hw = 14695981039346656037ULL, hr = 14695981039346656037ULL;
    char detail[96];

    in = fopen(src, "rb");
    if(!in){ ilog("src-open", errno, -1, -1); return -1; }
    out = fopen(dst, "wb");
    if(!out){ ilog("dst-open", errno, -1, -1); fclose(in); return -1; }
    while((n = fread(buf, 1, sizeof buf, in)) > 0){
        if(fwrite(buf, 1, n, out) != n){
            ilog("dst-write", errno, written, -1);
            fclose(in); fclose(out); return -1;
        }
        hw = fnv1a(buf, n, hw);
        written += (long)n;
    }
    fclose(in);
    if(fclose(out) != 0){ ilog("dst-close", errno, written, -1); return -1; }
    if(written <= 0){ ilog("src-empty", 0, written, -1); return -1; }

    /* Read-back proof: reopen and re-count/re-hash the destination. */
    in = fopen(dst, "rb");
    if(!in){ ilog("verify-open", errno, written, -1); return -1; }
    while((n = fread(buf, 1, sizeof buf, in)) > 0){
        hr = fnv1a(buf, n, hr);
        reread += (long)n;
    }
    fclose(in);
    snprintf(detail, sizeof detail, "h=%016llx/%016llx", hw, hr);
    if(written == reread && hw == hr){
        ilog("copy-ok", 0, written, reread);
        return 0;
    }
    ilog("verify-mismatch", 0, written, reread);
    ilog(detail, 0, 0, 0);
    g_stage = "verify-mismatch";
    return -1;
}

/* mkdir -p (parents as needed). 0 ok or already there. */
static int mkdirs(const char *path){
    char tmp[256];
    size_t i, n;
    if(!path || !path[0]) return -1;
    n = strlen(path);
    if(n >= sizeof tmp) return -1;
    memcpy(tmp, path, n + 1);
    for(i = 1; i < n; i++){
        if(tmp[i] == '/'){
            tmp[i] = 0;
            mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
    if(mkdir(path, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* Config template. Written when the package carries no config.json, so
 * ICFG_PATH always exists after this stage. */
#define CFG_TEMPLATE "{\"schema_version\":1,\"token\":\"SET_ME\",\"presence_state\":\"On PS4\"}"

/* Options carried across the config overwrite. The names live in carrykeys.h so
 * the config host test can check them against the shipped config.json -- a typo
 * here fails silently, resetting the user's setting with nothing to show why. */
static const char *const g_carry_keys[] = {
    CARRY_KEYS_LIST,
    NULL
};
#define CARRY_COUNT ((int)(sizeof g_carry_keys / sizeof g_carry_keys[0]) - 1)

/* Replaces config.json with `src`, preserving the user's own options.
 *
 * All three steps live in one function on purpose. Split into "place the config"
 * and "carry the options", the two can be called in the wrong order -- and when
 * they were, the capture read the file *after* it had been overwritten, so it
 * saved the release's values and wrote them straight back. A no-op that looked
 * like it worked, and the comment above it claimed the opposite.
 *
 * Nothing outside can observe the sequence, so making it impossible to get
 * wrong is cheaper than a comment asking future me to remember.
 *
 * Every failure here is non-fatal. A missing or unparseable old config means
 * nothing to carry, and a failed apply means the new config is intact minus the
 * user's options -- worth reporting, never worth aborting an install over. */
static void install_config(const char *src)
{
    /* Static, not stack: ICFG_CARRY_MAX entries of a ~250-byte struct is a few
     * KB, and this runs where the stack has no room to spare. */
    static icfg_carry_t snap[ICFG_CARRY_MAX];

    int found = 0;
    int applied = 0;

    /* 1. Capture, BEFORE anything overwrites the file. This is the whole trick:
     * after the overwrite the old values no longer exist anywhere. */
    if(CARRY_COUNT > 0)
        found = icfg_carry_capture(ICFG_PATH, g_carry_keys, snap, CARRY_COUNT);

    ilogv("cfg-captured", found, 0);

    /* 2. Replace with the incoming config. */
    place_config(src);

    /* 3. Write the snapshot back over the new file.
     *
     * capture returns how many were FOUND; apply needs how many SLOTS there are.
     * Passing the count as the length would drop the tail whenever an early key
     * is absent -- caught by the config host tests. */
    if(found > 0)
        applied = icfg_carry_apply(ICFG_PATH, snap, CARRY_COUNT);

    ilogv("cfg-carried", applied, 0);

    if(found > 0 && applied != found)
        uibridge_ok("Settings",
                    "The new settings could not be fully applied.\n\n"
                    "Some of your saved options were reset to defaults.");
}

static int write_template(void){
    FILE *f = fopen(ICFG_PATH, "wb");
    if(!f){
        ilog("cfg-template", errno, 0, 0);
        return -1;
    }
    fputs(CFG_TEMPLATE, f);
    if(fclose(f) != 0){
        ilog("cfg-template-close", errno, 0, 0);
        return -1;
    }
    return 0;
}

/* Copies `src` to ICFG_PATH, or writes the template when src is absent.
 * Reports via the log; the carry-over runs afterwards either way. */
static int place_config(const char *src)
{
    FILE *f = fopen(src, "rb");
    int rc;

    if(!f){
        ilog("cfg-noasset", errno, 0, 0);
        write_template();
        return 0;
    }
    fclose(f);

    rc = copy_file(src, ICFG_PATH);
    ilog("cfg-copy", rc == 0 ? 0 : (errno ? errno : -1), 0, 0);
    return 0;
}

/* Copy the payload's icon next to it.
 *
 * Best effort, and deliberately not fatal. The payload is the thing being
 * installed; an icon is decoration, and failing the whole install because one
 * image could not be written would be a bad trade for a cosmetic problem. A
 * missing logo shows as a blank row in the payload launcher -- visibly wrong, but
 * not broken.
 *
 * Silent to the user, unlike copy_payload's screen. One confirmation covers the
 * payload; a second one for a picture would be a screen for nothing.
 *
 * Returns 0 when the icon is in place, non-zero when it is not. The value is
 * logged rather than acted on.
 */
static int copy_logo(void)
{
    int rc;

    mkdirs("/data/payloads");

    rc = copy_file(LOGO_SRC, LOGO_ICON);
    ilog("logo-copy", rc, 0, 0);
    return rc;
}

static int step_files(const char *payloadSource, const char *configSource){
    /* Phase 2: place the payload, then its icon beside it, then the config.
     * Icon first so that if the install is abandoned midway, the payload is
     * already whole rather than paired with a missing picture.
     *
     * -2 is a cancel from the payload screen, passed through untouched so the
     * caller can say "cancelled" on the menu rather than "stopped early" -- the
     * user chose that, and nothing failed. */
    {
        int rc = copy_payload(payloadSource);

        if(rc == -2) return -2;
        if(rc != 0) return -1;
    }

    copy_logo();
    /* Config replaced, with the user's own options carried across. One call
     * because the capture has to happen before the overwrite, and that ordering
     * is the whole feature. */
    install_config(configSource);
    ilogv("cfg-done", 0, 0);
    return 0;
}

/* --- clean exit ---------------------------------------------------
 * The merged app owns process lifetime and keeps the dialog modules loaded
 * for its own ui_input() calls, so this only records the outcome. It must not
 * call ui_shutdown(): the menu needs the IME afterwards. */
static void finish(int code){
    ilogv(code == 0 ? "exit" : "fatal", code, 0);
}

/* ---- phase 1: check GitHub for a newer release -------------------
 * The only C++ in the wizard. Fails soft throughout: an offline console
 * must still be able to install, so every failure path returns 0 and
 * the flow carries on with whatever payload the PKG carries. */
#include "updatecheck_c.h"
#include "uibridge.h"

/* Where releases come from.
 *
 * Now the same repo the config is fetched from, so the payload and config share
 * one source. That was not true before: the payload came from CyberMask367's
 * releases while the config came from SirHumza's `main`, meaning an update could
 * pair a payload from one project with a config from another.
 *
 * The asset name is never matched on: the first asset carrying a download URL is
 * taken, whatever it is called, and it is saved under PAYLOAD_NAME. That is what
 * lets the repo's asset be named orbisRPC.elf without this code caring, and what
 * let it be downloaded at all when it was still orbisrpc.bin.
 *
 * The cost of not matching is that a release with more than one uploaded binary
 * is ambiguous and the first one wins. GitHub exposes auto-generated source
 * archives as zipball_url/tarball_url rather than browser_download_url, so a
 * single-asset release is unambiguous and adding a second real upload would be
 * the thing that breaks it.
 *
 * The in-package copy at DAEMON_ELF does have to carry the name exactly: the
 * console's filesystem is case-sensitive, so a mismatch there is a payload that
 * fails to open with nothing in the log to explain it. */
static const char *g_repo = "SirHumza/orbisRPC";

/* The version of the release that was just downloaded.
 *
 * Module-level rather than returned by installer_check_update() because the
 * toast is raised after the files are promoted, by which point staging has
 * already been cleared -- the candidate tag is no longer anywhere else. */
static char g_installed[32];

/* Staging -- TEMP_DIR, TEMP_PAYLOAD and TEMP_CONFIG -- is in paths.h, since
 * uninstall clears a staging directory an interrupted install may have left. */

/* The config is fetched from a branch, not from the release the payload came
 * from, so it moves independently of any version number. Consequences, both
 * accepted deliberately:
 *   - the payload and the config are not guaranteed to be tested together
 *   - the content here is whatever `main` holds at fetch time
 * raw.githubusercontent serves no size header, so this download is verified by
 * read-back hash only (see update_download). */
#define CONFIG_URL \
    "https://raw.githubusercontent.com/SirHumza/orbisRPC/" \
    "refs/heads/main/config/config.json"

/* Holds the network context open across the whole flow.
 *
 * Declared here rather than including netclient.h, which is C++ and installer.c
 * is C -- a C translation unit cannot pull in <string>. The C++ side declares
 * these with extern "C" in nethold.cpp, so the linkage matches and the types are
 * checked in one place rather than guessed at from C.
 *
 * Why it exists at all: Check() and Download() each used to pair their own
 * Init/Term, so the payload download was the first request made against a context
 * that had just been torn down and rebuilt. Whether that was the whole cause is
 * unproven -- the log will say -- but acquiring once removes the cycle outright
 * rather than relying on Term() being harmless. */
int  net_flow_hold(void);
void net_flow_release(void);

static void net_hold(void)     { net_flow_hold(); }
static void net_release(void)  { net_flow_release(); }

/* Progress is indeterminate, so the screen animates rather than claiming a
 * percentage it cannot know. Drives the drawn spinner while the request is in
 * flight. */
static void update_tick(int percent, void *ctx){
    (void)percent; (void)ctx;
    uibridge_tick();
}

/* Wipes the staging directory.
 *
 * This runs before every download, not just on failure. update_download()
 * writes to "<path>.part" and renames, so a half file cannot be promoted -- but
 * a *complete* leftover from an earlier run would be, and promoting that is
 * installing something nobody just downloaded. */
static void clear_staging(void){
    remove(TEMP_PAYLOAD);
    remove(TEMP_CONFIG);
    remove(TEMP_PAYLOAD ".part");
    remove(TEMP_CONFIG ".part");
    ilogv("stage-cleared", 0, 0);
}

/* Fetches both files into staging. All-or-nothing: a payload without its
 * config, or the reverse, is a mismatched install, so a partial success is
 * treated as total failure and the caller falls back to the package.
 *
 * `payloadSize` is the byte count the release API reported; -1 skips the size
 * check, which is the case for the config URL since raw.githubusercontent
 * serves no length. Both files are still read back and hashed by
 * update_download. */
static int fetch_to_staging(const char *payloadUrl, long payloadSize,
                            const char *configUrl)
{
    int why_net = -1;
    int why_status = 0;
    int why_errno = 0;

    mkdirs(TEMP_DIR);
    clear_staging();

    uibridge_progress_open("Downloading update");
    uibridge_progress_status("Payload");

    if(!update_download(payloadUrl, TEMP_PAYLOAD, payloadSize,
                        &why_net, &why_status, &why_errno,
                        update_tick, NULL)){
        uibridge_progress_close();
        /* Transport codes, not errno. The failure was below the C library, so
         * errno was 0 -- logging it produced "errno=0(ok)" on a download that
         * plainly did not work, which is worse than logging nothing.
         *
         * net:    1 uninit, 2 cancelled, 3 connect, 4 send, 5 status, 6 read,
         *         7 too-large
         * status: HTTP status, 0 when none was obtained
         * errno:  sceHttpGetLastErrno, the only thing that says WHY. */
        ilog("dl-payload-fail", why_errno, why_net, why_status);
        return -1;
    }
    ilog("dl-payload-ok", 0, payloadSize, 0);

    uibridge_progress_status("Config");

    if(!update_download(configUrl, TEMP_CONFIG, -1,
                        &why_net, &why_status, &why_errno,
                        update_tick, NULL)){
        uibridge_progress_close();
        ilog("dl-config-fail", why_errno, why_net, why_status);
        return -1;
    }
    ilog("dl-config-ok", 0, 0, 0);

    uibridge_progress_close();
    return 0;
}

/* `stagingPath` is unused: the staging paths are the TEMP_* macros above, and
 * taking them as a parameter would only let a caller disagree with where the
 * rest of the flow looks for staged files. */
int installer_check_update(const char *localVersion, const char *stagingPath){
    int status;
    char r_candidate[32];
    char r_url[256];
    long r_size = -1;
    int http = 0;
    long bytes = 0;

    (void)stagingPath;

    ilogv("update-begun", 0, 0);

    uibridge_progress_open("Checking for updates");
    uibridge_progress_status("Contacting GitHub");
    status = update_check(g_repo, localVersion,
                        r_candidate, sizeof r_candidate,
                        r_url, sizeof r_url, &r_size,
                        &http, &bytes, update_tick, NULL);
    uibridge_progress_close();

    /* Recorded before any branch so a wrong outcome is diagnosable from
     * install.log alone: what the server said, how much came back, and what the
     * two versions actually were. Previously the detail only reached menu.log,
     * which is not where anyone looks after a failed install. */
    ilogv("update-http", http, bytes);
    ilogs("update-versions", r_candidate, localVersion);

    if(status == UPDATE_UNKNOWN){
        ilog("update-unknown", 0, 0, 0);
        return 0;
    }

    if(status == UPDATE_CURRENT){
        uibridge_ok("orbisRPC is up to date", "Nothing to do.");
        ilog("update-current", 0, 0, 0);
        return 0;
    }

    ilogv("update-offered", r_size, 0);

    {
        char body[320];
        int answer;

        snprintf(body, sizeof body,
                 "You have version %s.\n\n"
                 "Download version %s and install it now?",
                 localVersion, r_candidate);

        answer = uibridge_yesno("Update available", body);

        /* Recorded because "the update was offered and nothing happened" is
         * otherwise indistinguishable from "it was never offered" -- both look
         * like silence from the console. */
        ilogv("update-ask", answer, 0);

        if(answer == 0){
            ilog("update-declined", 0, 0, 0);
            return 0;
        }
    }

    ilog("update-accepted", 0, 0, 0);

    if(fetch_to_staging(r_url, r_size, CONFIG_URL) != 0){
        clear_staging();
        ilog("dl-failed", 0, 0, 0);
        uibridge_ok("Download failed",
                    "The download did not finish.\n\n"
                    "Installing the version that came with this app instead.");
        return 0;
    }

    ilog("dl-ok", 0, 0, 0);

    /* Remembered for the post-install toast: on the update path the version
     * that landed is the release's, not this binary's. */
    snprintf(g_installed, sizeof g_installed, "%s", r_candidate);

    return 1;
}

/* The version of the release that was just downloaded.
 *
 * Kept module-level rather than returned by installer_check_update() because
 * the toast is raised after the files are promoted, by which point the staging
 * directory has already been cleared. */
static const char *downloadedVersion(void){
    return g_installed[0] ? g_installed : NULL;
}

/* ---- GoldHEN auto-run ------------------------------------------------
 * Adds the payload to GoldHEN's boot list, which is what makes it start without
 * the user launching it each time.
 *
 * Silent when GoldHEN is absent: it is not an install, most consoles do not have
 * it, and a prompt about a homebrew manager that is not installed is noise.
 *
 * The restart question is not asked here. Every screen in this flow already ends
 * with "Restart your console" on the update path, and on a fresh install this is
 * the only thing that would need one -- which is why the final screen mentions it
 * when this added something. */
static int step_goldhen(void)
{
    int rc;

    if(!goldhen_present()){
        ilogv("goldhen-absent", 0, 0);
        return 0;
    }

    ilogv("goldhen-found", 0, 0);

    if(uibridge_yesno("GoldHEN detected",
                      "GoldHEN auto-run config was found.\n\n"
                      "Add orbisRPC to the auto-run queue so it starts "
                      "when the console boots?") != 1){
        ilog("goldhen-declined", 0, 0, 0);
        return 0;
    }

    rc = goldhen_add_autorun(GOLDHEN_AUTORUN_ELF);
    ilog("goldhen-add", rc, 0, 0);

    if(rc < 0){
        uibridge_ok("GoldHEN",
                    "Could not write to GoldHEN's payloads.ini.\n\n"
                    "The payload was installed but will not start on boot.");
        return -1;
    }

    if(rc == 0){
        uibridge_ok("GoldHEN",
                    "orbisRPC was already in the auto-run queue.\n\n"
                    "Nothing was changed.");
        return 0;
    }

    uibridge_ok("GoldHEN", "orbisRPC will start automatically on boot.");
    return 1;
}

/* ---- nanoDNS exception ------------------------------------------------
 * The host needs Sony's TMDB endpoint exempted from whatever the console resolves
 * it to.
 *
 * nanoDNS blackholes *.playstation.net.* to 0.0.0.0 in [overrides], which takes
 * the TMDB endpoint with it. [exceptions] is the way back out: a query matching a
 * mask listed there is forwarded upstream and skips the overrides entirely.
 *
 * A bare line, no "= value". That is how nanoDNS reads the section, and getting
 * it wrong is invisible -- "host = 1" is not a mask, so nanoDNS ignores it, the
 * presence check keeps not seeing it, and every run appends another one.
 *
 * TMDB_EXCEPTION is in paths.h: uninstall asks whether to take this line back out
 * again, and needs to be looking at exactly the same mask install added.
 */

static void step_nanodns(void)
{
    int rc;

    if(!nanodns_present()){
        ilogv("nanodns-absent", 0, 0);
        return;
    }

    ilogv("nanodns-found", 0, 0);

    if(uibridge_yesno("nanoDNS detected",
                      "A nanoDNS config was found.\n\n"
                      "Do you want to add the Sony TMDB exception to it?") != 1){
        ilog("nanodns-declined", 0, 0, 0);
        return;
    }

    rc = nanodns_add_exception(TMDB_EXCEPTION);
    ilog("nanodns-add", rc, 0, 0);

    if(rc < 0)
        uibridge_ok("nanoDNS",
                    "Could not write to the nanoDNS config.\n\n"
                    "It was left as it was.");
    else if(rc == 0)
        uibridge_ok("nanoDNS",
                    "The TMDB exception was already there.\n\n"
                    "Nothing was changed.");
    else
        uibridge_ok("nanoDNS", "The Sony TMDB exception was added.");
}

/* ---- phases 2 onward ------------------------------------------------ */

int installer_run(const char *payloadSource, const char *configSource,
                  int replacing, const char *installedVersion){
    /* Set when something done here needs a reboot to take effect. An update
     * always does -- there is a running payload to replace -- and so does adding
     * a GoldHEN auto-run entry on a fresh install, where the entry only comes
     * into effect on the next boot. */
    int needsRestart = replacing;

    /* Cancel came back from step_files. Distinguished from a failure because it
     * was not one: nothing errored, the user pressed Circle, and the menu saying
     * "install stopped early" would read as though something had broken. */
    {
        int rc = step_files(payloadSource, configSource);

        if(rc == -2){
            /* Staging too: the payload came from there a moment ago and the
             * rollback took the installed copy off, so what is left is a partial
             * download nothing will ever read. */
            clear_staging();
            finish(0);
            return INSTALLER_CANCELLED;
        }

        if(rc != 0){ finish(1); return INSTALLER_STOPPED; }
    }
    /* Staging has served its purpose once both files are in place. Leaving it
     * behind costs nothing but disk, and a stale payload there is the kind of
     * thing that gets mistaken for a real install later. */
    clear_staging();

    /* Record what landed, before anything can fail afterwards: this is what the
     * next run compares against, and a stale marker means the updater re-offers
     * the version that is already installed. */
    write_installed_version(installedVersion);

    /* GoldHEN auto-run, then nanoDNS. Both after the payload is in place, both
     * silent when the thing they configure is not installed. */
    if(step_goldhen() > 0)
        needsRestart = 1;

    step_nanodns();

    finish(0);

    /* The last screen, and the one that waits for a button press before the menu
     * comes back. "Update done" rather than "Install done" when something was
     * actually replaced: fetching a release onto a console that had nothing is
     * still a first install, and telling the user they updated something they
     * never had is the kind of small lie that makes a log hard to trust.
     *
     * The toast fires only for a genuine update. A fresh install that added a
     * GoldHEN entry needs the reboot the screen mentions, but it did not update
     * anything, and a notification claiming otherwise is worse than none. */
    uibridge_ok(replacing ? "Update done" : "Install done",
                needsRestart
                    ? "Restart your console for changes to apply."
                    : "Set your Discord Token then launch orbisRPC from GoldHEN.");

    if(replacing)
        notify_update_installed(installedVersion);

    return INSTALLER_OK;
}

/* 1 when a payload is already on the console.
 *
 * Read once, at the very top of the flow, and separate from step_installed()
 * below for two reasons. It has to be silent -- this one only informs the final
 * wording, and a dialog here would interrupt before the user had even been asked
 * anything. And it has to run unconditionally: the update path skips
 * step_installed() entirely, so if this fact were only known there, a clean
 * console downloading a release would have no way to tell that nothing was
 * actually replaced.
 */
static int already_installed(void)
{
    FILE *f = fopen(PAYLOAD_ELF, "rb");

    if(!f) return 0;
    fclose(f);
    return 1;
}

/* The whole install flow, callable from the merged app's main.
 * Returns 0 on success, non-zero if it stopped early. */
int installer_flow(void){
    /* Sources for step 2. Default to the package; the update check swaps both
     * or neither -- mixing a downloaded payload with a packaged config would
     * be an install nobody tested. */
    const char *payloadSource = DAEMON_ELF;
    const char *configSource = CFG_SOURCE;
    /* What actually landed on disk, which is not the same as what this binary
     * was built with -- that is the point of an update. SETUP_VERSION would
     * report the old number in the toast. */
    const char *installedVersion = SETUP_VERSION;
    /* What the next run will compare against. Read before the check, not after:
     * an update overwrites it, so reading later would compare this run against
     * the version it just installed and always report "up to date". */
    char baseline[32];
    int downloaded;
    int result;
    /* Whether this run replaces something or puts a first install in place.
     * Taken before the update check, because the update path never reaches the
     * already-installed step and so has no other way to find out. */
    int replacing = already_installed();

    /* Hide the PS4 splash first -- dialogs opened while the splash
     * is visible get auto-dismissed (half-second flash) or never
     * surface. Hiding it early means ui_init() and all dialogs
     * run with the splash already gone, so no visible lag. */
    sceSystemServiceHideSplashScreen();

    /* ui_init() is still required even though the install flow no longer opens a
     * system dialog: the Settings page's text field uses the OS keyboard, and the
     * flow can leave the app in a state where that field is reachable. */
    if(ui_init() != 0){ finish(1); return INSTALLER_STOPPED; }

    /* One network context for the whole flow, so the check and the download
     * cannot end up on opposite sides of a teardown. */
    net_hold();

    /* No opening splash dialog: Restore goes straight into the update check. */
    mkdirs(INST_DIR);
    read_installed_version(baseline, sizeof baseline);

    /* Update check runs first, always. It is the only step that can make this
     * run an update rather than a fresh install, and that decision has to be
     * made before anything asks about what is already on disk. */
    downloaded = installer_check_update(baseline, TEMP_DIR);

    if(downloaded){
        payloadSource = TEMP_PAYLOAD;
        configSource = TEMP_CONFIG;
        installedVersion = downloadedVersion();
    } else {
        /* Fresh install only. An accepted update skips this deliberately: it is
         * replacing the files, not stacking on top of them.
         *
         * Note the consequence: with evict.elf gone there is no way to unload a
         * running payload, so a replace over a live daemon may fail to write or
         * leave the old ELF mapped. copy_file() reports that honestly via the
         * stage log rather than claiming success. */
        if(step_installed() != 0){
            finish(1);
            net_release();
            return INSTALLER_ALREADY_INSTALLED;
        }

        payloadSource = DAEMON_ELF;
        configSource = CFG_SOURCE;
    }

    if(step_assets() != 0 && !downloaded){
        finish(1);
        net_release();
        return INSTALLER_STOPPED;
    }

    ilogv("assets-ok", 0, 0);

    result = installer_run(payloadSource, configSource, replacing,
                           installedVersion);

    net_release();
    return result;
}
