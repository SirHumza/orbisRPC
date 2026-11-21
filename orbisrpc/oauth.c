/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
#include "oauth.h"
#include "cfg.h"
#include "clock.h"
#include "jsonlite.h"
#include "log.h"
#include "notify.h"
#include "updater_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include "netconnect.h"
#include "tls.h"
#endif
static char session_path[512] = DATA_DIR "/oauth-session.json";
static char reset_path[512] = DATA_DIR "/oauth.unlink";
#define SESSION_PATH session_path
#define RESET_PATH reset_path
static char session[65], bearer[2048], origin[256];
static int64_t expiry, next_attempt;
static int loaded;
static int64_t wall_expiry, issued_at;
static int rejected;

int oauth_enabled(void){ return !strcmp(g_cfg.auth_mode, "oauth"); }
const char *oauth_token(void){ return oauth_enabled() ? bearer : g_cfg.token; }
int oauth_valid_origin(const char *url){
    if(!url || strncmp(url, "https://", 8) || !url[8]) return 0;
    for(const char *p = url + 8; *p; p++)
        if(!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
             (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return 0;
    return 1;
}
static const char *strfield(const jl_val_t *r, const char *key){
    const jl_val_t *v = jl_obj_get(r, key);
    return v && v->type == JL_STRING ? v->str : "";
}
static int seconds(const jl_val_t *r, const char *key, int fallback){
    const jl_val_t *v = jl_obj_get(r, key);
    return v && v->type == JL_NUMBER && v->num >= 1 && v->num <= 31536000
        ? (int)v->num : fallback;
}
static int secret_valid(const char *s){
    if(strlen(s) != 64) return 0;
    for(size_t i = 0; i < 64; i++)
        if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}
static int access_valid(const char *token){
    size_t n = strlen(token);
    if(!n || n >= sizeof bearer - 8) return 0;
    for(size_t i = 0; i < n; i++){
        unsigned char c = (unsigned char)token[i];
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || strchr("-._~+/=", c))) return 0;
    }
    return 1;
}
static int64_t epoch_field(const jl_val_t *r, const char *key){
    const jl_val_t *v = jl_obj_get(r, key);
    return v && v->type == JL_NUMBER && v->num >= 1577836800 &&
        v->num <= 4102444800LL && v->num == (double)(int64_t)v->num
        ? (int64_t)v->num : 0;
}
static int save_session(void){
    jl_val_t *r = jl_new_object();
    if(!r) return 0;
    jl_obj_set(r, "origin", jl_new_string(origin));
    jl_obj_set(r, "session", jl_new_string(session));
    jl_obj_set(r, "rejected", jl_new_bool(rejected));
    if(bearer[0] && issued_at && wall_expiry > issued_at){
        jl_obj_set(r, "access_token", jl_new_string(bearer + 7));
        jl_obj_set(r, "issued_at", jl_new_int(issued_at));
        jl_obj_set(r, "expires_at", jl_new_int(wall_expiry));
    }
    char *text = jl_stringify(r);
    jl_free(r);
    if(!text) return 0;
    char temporary[520];
    snprintf(temporary, sizeof temporary, "%s.tmp", SESSION_PATH);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int ok = fd >= 0;
    if(ok){
        size_t n = strlen(text), pos = 0;
        if(fchmod(fd, 0600)) ok = 0;
        while(ok && pos < n){
            ssize_t wrote = write(fd, text + pos, n - pos);
            if(wrote <= 0) ok = 0;
            else pos += (size_t)wrote;
        }
        if(fsync(fd)) ok = 0;
        if(close(fd)) ok = 0;
        if(ok && rename(temporary, SESSION_PATH)) ok = 0;
    }
    memset(text, 0, strlen(text));
    free(text);
    if(!ok) unlink(temporary);
    return ok;
}
static void load_session(void){
    char buf[4096];
    int fd = open(SESSION_PATH, O_RDONLY);
    if(fd < 0) return;
    (void)fchmod(fd, 0600);
    ssize_t n = read(fd, buf, sizeof buf);
    close(fd);
    if(n <= 0 || n == (ssize_t)sizeof buf) return;
    jl_val_t *r = jl_parse(buf, (size_t)n);
    if(r && !strcmp(strfield(r, "origin"), origin) && secret_valid(strfield(r, "session"))){
        snprintf(session, sizeof session, "%s", strfield(r, "session"));
        const jl_val_t *refused = jl_obj_get(r, "rejected");
        rejected = refused && refused->type == JL_BOOL && refused->num;
        int64_t now = (int64_t)time(NULL);
        int64_t issued = epoch_field(r, "issued_at");
        int64_t until = epoch_field(r, "expires_at");
        if(!rejected && issued && issued <= now && until > now &&
           until > issued && until - issued <= 31536000 &&
           access_valid(strfield(r, "access_token"))){
            snprintf(bearer, sizeof bearer, "Bearer %s", strfield(r, "access_token"));
            issued_at = issued; wall_expiry = until;
            expiry = orbis_mono_s() + until - now;
        }
    }
    jl_free(r);
    memset(buf, 0, sizeof buf);
}
#ifdef OAUTH_TESTABLE
static jl_val_t *(*test_request)(const char *, const char *, int *);
void oauth_test_setup(const char *directory, jl_val_t *(*transport)(const char *, const char *, int *)){
    snprintf(session_path, sizeof session_path, "%s/oauth-session.json", directory);
    snprintf(reset_path, sizeof reset_path, "%s/oauth.unlink", directory);
    session[0] = bearer[0] = origin[0] = 0;
    expiry = next_attempt = wall_expiry = issued_at = 0; loaded = rejected = 0;
    test_request = transport;
}
void oauth_test_retry(void){ next_attempt = 0; }
#endif
static jl_val_t *request(const char *method, const char *path, int *status){
#ifdef OAUTH_TESTABLE
    if(test_request) return test_request(method, path, status);
#endif
    *status = 0;
#ifdef ORBISRPC_SDK_PAYLOAD
    int fd = net_tcp_connect(origin + 8, 443, 10);
    if(fd < 0) return NULL;
    tls_ctx_t *tls = tls_start(fd, origin + 8);
    if(!tls){ close(fd); return NULL; }
    char req[1024];
    int n = snprintf(req, sizeof req,
        "%s %s HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\n"
        "Content-Length: 0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        method, path, origin + 8, session);
    char raw[16384];
    size_t used = 0;
    int complete = 0;
    if(n > 0 && n < (int)sizeof req && tls_write(tls, req, (size_t)n) >= 0){
        int64_t deadline = orbis_mono_s() + 20;
        while(used < sizeof raw - 1 && orbis_mono_s() < deadline){
            int got = tls_read(tls, raw + used, sizeof raw - 1 - used);
            if(got < 0){ complete = 1; break; }
            if(!got){ usleep(20000); continue; }
            used += (size_t)got;
        }
    }
    memset(req, 0, sizeof req);
    tls_free(tls);
    close(fd);
    if(!complete || !used) return NULL;
    raw[used] = 0;
    size_t length = 0;
    char *body = upd_parse_response(raw, used, 8192, status, &length, NULL, 0);
    memset(raw, 0, sizeof raw);
    if(!body) return NULL;
    jl_val_t *result = jl_parse(body, length);
    memset(body, 0, length);
    free(body);
    return result;
#else
    (void)method; (void)path;
    return NULL;
#endif
}
int oauth_due(void){ return oauth_enabled() && (!bearer[0] || orbis_mono_s() >= expiry ||
    (issued_at && (int64_t)time(NULL) < issued_at) ||
    (wall_expiry && (int64_t)time(NULL) >= wall_expiry) || access(RESET_PATH, F_OK) == 0); }
void oauth_rejected(void){
    memset(bearer, 0, sizeof bearer);
    expiry = wall_expiry = issued_at = 0;
    rejected = 1;
    if(!save_session()){
        unlink(SESSION_PATH);
        log_msg("oauth: rejected authorization cache removed after save failure");
    }
    /* A broker may still cache the refused token. Relink deliberately. */
    log_msg("oauth: Discord rejected authorization; create oauth.unlink to relink");
    next_attempt = 0;
}
int oauth_prepare(void){
    if(!oauth_enabled()) return 1;
    if(!oauth_valid_origin(g_cfg.broker_url)){
        log_msg("oauth: broker_url must be an HTTPS origin without a path or port");
        return 0;
    }
    if(strcmp(origin, g_cfg.broker_url)){
        snprintf(origin, sizeof origin, "%s", g_cfg.broker_url);
        memset(session, 0, sizeof session);
        memset(bearer, 0, sizeof bearer);
        loaded = 0; expiry = wall_expiry = issued_at = 0; next_attempt = 0; rejected = 0;
    }
    if(!loaded){ load_session(); loaded = 1; }
    if(access(RESET_PATH, F_OK) == 0){
        int st;
        jl_val_t *r = request("DELETE", "/v1/session", &st);
        jl_free(r);
        if(st != 204 && st != 401) return 0;
        unlink(RESET_PATH); unlink(SESSION_PATH);
        memset(session, 0, sizeof session); memset(bearer, 0, sizeof bearer);
        expiry = wall_expiry = issued_at = 0; next_attempt = 0; rejected = 0;
        return 0;
    }
    if(rejected) return 0;
    if(bearer[0] && !oauth_due()) return 1;
    if(orbis_mono_s() < next_attempt) return 0;
    next_attempt = orbis_mono_s() + 30;
    int status;
    if(!session[0]){
        jl_val_t *r = request("POST", "/v1/pair", &status);
        const char *secret = strfield(r, "session_token");
        const char *uri = strfield(r, "verification_uri");
        const char *code = strfield(r, "user_code");
        if(status == 201 && secret_valid(secret) &&
           !strcmp(uri, "https://discord.com/activate") && strlen(code) < 32 && code[0]){
            snprintf(session, sizeof session, "%s", secret);
            if(save_session()){
                char msg[160];
                snprintf(msg, sizeof msg, "Discord pairing: %s - code %s", uri, code);
                log_msg("%s", msg); notify_show(msg);
                next_attempt = orbis_mono_s() + seconds(r, "interval", 5);
            } else {
                memset(session, 0, sizeof session);
                log_msg("oauth: could not save pairing state");
            }
        } else log_msg("oauth: pairing unavailable (HTTP %d)", status);
        jl_free(r);
        return 0;
    }
    jl_val_t *r = request("POST", "/v1/token", &status);
    if(status == 200){
        const char *token = strfield(r, "access_token");
        int valid = access_valid(token) && !strcmp(strfield(r, "token_type"), "Bearer");
        if(valid){
            snprintf(bearer, sizeof bearer, "Bearer %s", token);
            int lifetime = seconds(r, "expires_in", 0);
            if(lifetime > 60){
                expiry = orbis_mono_s() + lifetime - 60;
                issued_at = (int64_t)time(NULL);
                wall_expiry = issued_at + lifetime - 60;
                if(!save_session()) log_msg("oauth: access token cache could not be saved");
                jl_free(r);
                return 1;
            }
            memset(bearer, 0, sizeof bearer);
        }
    } else if(status == 401 || status == 403){
        unlink(SESSION_PATH); memset(session, 0, sizeof session);
        memset(bearer, 0, sizeof bearer);
        expiry = wall_expiry = issued_at = 0;
        log_msg("oauth: authorization expired or revoked; pairing again");
    } else if(status == 202 || status == 429){
        int interval = seconds(r, "interval", 30);
        if(interval < 5) interval = 5;
        next_attempt = orbis_mono_s() + interval;
    } else log_msg("oauth: broker unavailable (HTTP %d)", status);
    jl_free(r);
    return 0;
}
