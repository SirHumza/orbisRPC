/* installer.c - clean installer, one flow forward.
 * Payload placement is the whole job: copy to /data/payloads.
 * Payload Guest reads this directory. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include "ui.h"
#include "icfg.h"

#ifndef SETUP_VERSION
#define SETUP_VERSION "1.1.0"
#endif
#define DAEMON_ELF "/app0/assets/daemon.elf"
#define GEN_PATH "/data/orbisRPC/daemon.gen"
#define INST_DIR "/data/orbisRPC"
#define INST_LOG "/data/orbisRPC/install.log"
#define PAYLOAD_BIN "/data/payloads/orbisrpc.bin"
#define STATUS_PATH "/data/orbisRPC/status.json"

/* Stage log: every copy step records errno + sizes to a file we can read
 * back over FTP. The dialog alone can't say WHICH stage failed. */
static const char *g_stage = "-";

/* --- pre-main boot marker ------------------------------------------
 * The wizard died before main() on every packaging experiment, leaving
 * install.log untouched and the crash handler (installed inside main)
 * never armed. A constructor runs before main, so it separates the two
 * failure classes for good:
 *   no BOOT line at all  -> the loader/SELF never got control (packaging,
 *                           authinfo, SIGSYS) or a pre-main fault
 *   BOOT but no stage    -> main started, died before the first ilog()
 * Async-signal-safe only: raw open/write, no stdio, no malloc. */
__attribute__((constructor))
static void boot_marker(void){
    static const char msg[] = "BOOT installer entered pre-main\n";
    const char *paths[2];
    size_t i;
    paths[0] = "/data/install.log";
    paths[1] = INST_LOG;
    for(i = 0; i < 2; i++){
        int fd = open(paths[i], O_WRONLY | O_APPEND | O_CREAT, 0666);
        if(fd < 0) continue;
        { ssize_t w = write(fd, msg, sizeof msg - 1); (void)w; }
        close(fd);
    }
}

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

/* --- crash guard ----------------------------------------------------
 * The wizard has died twice mid-flow with Sony's crash reporter eating
 * the core file, so the fault address never reached us. These handlers
 * run async-signal-safe only (no stdio, no malloc): they append the
 * signal, faulting address and current stage to install.log, then exit
 * cleanly. A crash becomes evidence instead of a crash dialog. */
static volatile sig_atomic_t g_crashing = 0;

static size_t h_s(char *b, size_t cap, size_t p, const char *s){
    while(*s && p + 1 < cap) b[p++] = *s++;
    return p;
}
static size_t h_u(char *b, size_t cap, size_t p, unsigned long v){
    char t[24];
    int n = 0;
    if(v == 0) t[n++] = '0';
    while(v && n < 24){ t[n++] = (char)('0' + v % 10); v /= 10; }
    while(n && p + 1 < cap) b[p++] = t[--n];
    return p;
}
static size_t h_x(char *b, size_t cap, size_t p, unsigned long long v){
    static const char hx[] = "0123456789abcdef";
    char t[20];
    int n = 0;
    if(v == 0) t[n++] = '0';
    while(v && n < 20){ t[n++] = hx[v & 15]; v >>= 4; }
    while(n && p + 1 < cap) b[p++] = t[--n];
    return p;
}

static void crash_handler(int sig, siginfo_t *si, void *uc){
    char b[200];
    size_t p = 0;
    int fd;
    ssize_t w;
    (void)uc;
    if(g_crashing) _exit(99);
    g_crashing = 1;
    p = h_s(b, sizeof b, p, "CRASH sig=");
    p = h_u(b, sizeof b, p, (unsigned)sig);
    p = h_s(b, sizeof b, p, " addr=0x");
    p = h_x(b, sizeof b, p, si ? (unsigned long long)(uintptr_t)si->si_addr : 0);
    p = h_s(b, sizeof b, p, " stage=");
    p = h_s(b, sizeof b, p, g_stage ? g_stage : "-");
    if(p + 1 < sizeof b) b[p++] = '\n';
    fd = open(INST_LOG, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if(fd < 0) fd = open("/data/install.log", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if(fd >= 0){ w = write(fd, b, p); (void)w; close(fd); }
    _exit(99);
}

static void crash_guard(void){
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
    struct sigaction sa;
    size_t i;
    memset(&sa, 0, sizeof sa);
    /* Header bug: the sa_sigaction macro expands to a member that does
     * not exist, so address the union directly. */
    sa.__sa_handler.__sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for(i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        sigaction(sigs[i], &sa, NULL);
}

/* Preflight: the payload must exist inside this package. A PKG built
 * or installed without staged assets fails here with a precise message
 * instead of a mysterious write error three steps later. */
static int step_assets(void){
    FILE *a = fopen(DAEMON_ELF, "rb");
    if(a) fclose(a);
    if(!a){
        ui_ok("This install is missing its payload.\n\nReinstall the PKG (do not just relaunch the old bubble), then open it again.");
        return -1;
    }
    return 0;
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

/* Existence check without stat() (the sandbox lies about stat sizes). */
static int exists(const char *p){
    FILE *f = fopen(p, "rb");
    if(!f) return 0;
    fclose(f);
    return 1;
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

static int step_files(void){
    char report[512];
    int ok = -1;
    mkdir(INST_DIR, 0777);
    mkdirs("/data/payloads");
    ui_progress_open("Installing orbisRPC");
    /* Copy daemon payload. */
    ui_progress_msg("Copying payload");
    ok = copy_file(DAEMON_ELF, PAYLOAD_BIN);
    ilogv("daemon-copy", ok, 0);
    ui_progress_set(60);
    /* Pre-save config from PKG asset on FRESH installs only. A reinstall
     * must never clobber the existing config: it holds the token and
     * every learned title. step_token() below handles token entry. */
    ui_progress_msg("Saving config");
    {
        if(exists(ICFG_PATH)){
            ilogv("cfg-keep", 0, 0);
        } else if(exists("/app0/assets/config.json")){
            int rc = copy_file("/app0/assets/config.json", ICFG_PATH);
            ilogv("cfg-copy", rc, 0);
            if(rc == 0) chmod(ICFG_PATH, 0600);
        } else {
            ilog("cfg-noasset", errno, 0, 0);
            /* Fallback: write template with pre-set token. */
            FILE *f = fopen(ICFG_PATH, "wb");
            if(f){
                fputs("{\"schema_version\":1,\"token\":\"SET_ME\",\"presence_state\":\"On PS4\"}", f);
                fclose(f);
                chmod(ICFG_PATH, 0600);
            }
        }
    }
    snprintf(report, sizeof report,
             "%s : %s%s%s%s",
             PAYLOAD_BIN, ok == 0 ? "OK" : "denied",
             ok == 0 ? "" : " [stage ", ok == 0 ? "" : g_stage, ok == 0 ? "" : "]");
    ui_progress_set(80);
    ui_progress_close();
    ui_ok(report);
    if(ok != 0){
        ui_ok("Install failed: could not write the payload.\n\nStopping here.");
        return -1;
    }
    ilogv("report-done", ok, 0);
    /* Config template when missing; never clobbers learned titles. */
    {
        char tok[160];
        tok[0] = 0;
        icfg_token_load(ICFG_PATH, tok, sizeof tok);
        if(!token_valid(tok)){
            FILE *f = fopen(ICFG_PATH, "rb");
            if(!f){
                f = fopen(ICFG_PATH, "wb");
                if(f){
                    fputs("{\"schema_version\":1,\"token\":\"SET_ME\",\"presence_state\":\"On PS4\"}", f);
                    fclose(f);
                    chmod(ICFG_PATH, 0600);
                }
            } else {
                fclose(f);
            }
        }
    }
    ilogv("cfg-done", 0, 0);
    /* Generation bump: any older running daemon sees the new generation
     * and exits cleanly so the fresh payload takes over (no evict). */
    {
        long cur = 0;
        FILE *gf = fopen(GEN_PATH, "rb");
        if(gf){
            char gb[32];
            size_t n = fread(gb, 1, sizeof gb - 1, gf);
            fclose(gf);
            if(n > 0){ gb[n] = 0; cur = atol(gb); }
        }
        gf = fopen(GEN_PATH, "wb");
        if(gf){
            fprintf(gf, "%ld\n", cur + 1);
            fclose(gf);
            ilogv("gen-bump", cur + 1, 0);
        } else {
            ilog("gen-bump", errno ? errno : -1, 0, 0);
        }
    }
    return 0;
}

/* 2: token. Skips silently when one already validates. */
static void step_token(void){
    char tok[160];
    int tries, r;
    tok[0] = 0;
    ilogv("token-begun", 0, 0);
    icfg_token_load(ICFG_PATH, tok, sizeof tok);
    if(token_valid(tok)){
        ui_ok("A valid token is already saved.\n\nSkipping.");
        ilogv("token-skip", 0, 0);
        return;
    }
    for(tries = 0; tries < 3; tries++){
        r = ui_input("Discord token", "paste token, Done to save", tok, sizeof tok);
        if(r < 0){
            /* IME failure is transient (system dialog busy, OOM): retry
             * instead of surrendering, and say so. Last try keeps the
             * FTP fallback message. */
            ilog("token-imefail", tries, 0, 0);
            if(tries < 2){
                ui_ok("Text input glitched. Try again.");
                continue;
            }
            ui_ok("Text input failed. You can paste the token into "
                  "/data/orbisRPC/config.json over FTP instead.");
            return;
        }
        if(r == 0) return;
        if(token_valid(tok)){
            if(icfg_token_save(ICFG_PATH, tok) == 0){
                ilogv("token-saved", 0, 0);
                ui_ok("Config saved.");
            } else {
                ilog("token-save", errno, 0, 0);
                ui_ok("Could not save the token.\n\nPaste it into /data/orbisRPC/config.json over FTP instead.");
            }
            return;
        }
        ui_ok("That doesn't look like a token.\nCheck it and try again, or cancel to skip.");
    }
}

/* --- clean exit ---------------------------------------------------
 * Tear dialogs down, unload modules, then _exit. Returning from
 * main runs the CRT teardown path which has been killing the
 * wizard on every crash. */
static void finish(int code){
    ilogv(code == 0 ? "exit" : "fatal", code, 0);
    ui_shutdown();
    _exit(code);
}

/* Final screen reports what is actually true: daemon liveness comes
 * from status.json freshness, not from the copy steps succeeding. */
static void step_done(void){
    char st[32] = "";
    char msg[256];
    if(icfg_daemon_state(STATUS_PATH, (long)time(NULL), 120, st, sizeof st)){
        snprintf(msg, sizeof msg,
                 "Done.\n\nDaemon is alive (%s).\nLaunch orbisrpc from the payload launcher after a reboot.", st);
    } else {
        snprintf(msg, sizeof msg,
                 "Done.\n\nNo live daemon seen — normal on first install.\nLaunch orbisrpc from the payload launcher.");
    }
    ilogv("done-shown", 0, 0);
    ui_ok(msg);
}

int main(void){
    /* Hide the PS4 splash first — dialogs opened while the splash
     * is visible get auto-dismissed (half-second flash) or never
     * surface. Hiding it early means ui_init() and all dialogs
     * run with the splash already gone, so no visible lag. */
    sceSystemServiceHideSplashScreen();
    crash_guard();
    if(ui_init() != 0){ ilogv("ui-init-fail", 0, 0); finish(1); }
    ui_ok("orbisRPC");
    if(step_assets() != 0) finish(1);
    ilogv("assets-ok", 0, 0);
    if(step_files() != 0) finish(1);
    step_token();
    ilogv("post-token", 0, 0);
    step_done();
    finish(0);
}
