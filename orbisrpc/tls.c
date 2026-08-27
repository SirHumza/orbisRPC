/* tls.c - BearSSL-backed TLS client for orbisRPC.
 *
 * Handshake runs with the socket in blocking mode guarded by an overall
 * deadline; afterwards the caller keeps the socket non-blocking and drives
 * tls_read()/tls_write(), which pump the engine without ever stalling.
 *
 * No certificate validation: no trust store exists on console. We still get
 * an authenticated-encryption channel against whoever owns the wire; SNI is
 * sent so Discord serves its normal cert chain.
 */
#include "tls.h"
#include "log.h"
#include <bearssl.h>
#include <orbis/Net.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <time.h>

/* ---- minimal X.509 "validator" ----------------------------------------
 * Decodes certificates to extract the server's public key (required for
 * ECDHE_RSA/ECDSA ServerKeyExchange verification) but performs NO chain
 * validation — end_chain always reports success. */
typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dc;
    br_x509_pkey pkey;
    unsigned key_usages;
    int have_leaf;
} mini_x509;

static void mx_start_chain(const br_x509_class **ctx, const char *server_name){
    (void)ctx; (void)server_name;
}
static void mx_start_cert(const br_x509_class **ctx, uint32_t length){
    mini_x509 *m = (mini_x509 *)((char *)ctx - offsetof(mini_x509, vtable));
    (void)length;
    br_x509_decoder_init(&m->dc, NULL, NULL);
}
static void mx_append(const br_x509_class **ctx, const unsigned char *buf, size_t len){
    mini_x509 *m = (mini_x509 *)((char *)ctx - offsetof(mini_x509, vtable));
    br_x509_decoder_push(&m->dc, buf, len);
}
static void mx_end_cert(const br_x509_class **ctx){
    mini_x509 *m = (mini_x509 *)((char *)ctx - offsetof(mini_x509, vtable));
    const br_x509_pkey *pk = br_x509_decoder_get_pkey(&m->dc);
    /* keep only the FIRST cert's key — that's the leaf whose key verifies
     * the ServerKeyExchange signature */
    if(pk && !m->have_leaf){ m->pkey = *pk; m->have_leaf = 1; }
}
static unsigned mx_end_chain(const br_x509_class **ctx){(void)ctx; return 0;}
static const br_x509_pkey *mx_get_pkey(const br_x509_class *const *ctx, unsigned *usages){
    mini_x509 *m = (mini_x509 *)((const char *)ctx - offsetof(mini_x509, vtable));
    if(usages){
        /* allow both key-exchange and signature usages */
        *usages |= BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    }
    return &m->pkey;
}
static const br_x509_class mini_vtable = {
    sizeof(br_x509_class *),
    &mx_start_chain,
    &mx_start_cert,
    &mx_append,
    &mx_end_cert,
    &mx_end_chain,
    mx_get_pkey
};

struct tls_ctx {
    br_ssl_client_context cc;
    mini_x509 mx;               /* certificate decoder / key extractor */
    unsigned char *iobuf;
    int fd;
};

/* ---- raw socket IO (socket may be NBIO: short sends/recv are normal) -- */
static int raw_send(int fd, const unsigned char *b, size_t n){
    if(n > 32768) n = 32768;
    return (int)sceNetSend(fd, b, (int)n, 0);
}
static int raw_recv(int fd, unsigned char *b, size_t n){
    if(n > 16384) n = 16384;
    return (int)sceNetRecv(fd, b, (int)n, 0);
}

/* Drive one engine transition. Returns 1 on progress, 0 on would-block. */
static int pump_once(tls_ctx_t *t, int *iter){
    int trace = *iter < 6;
    int current = *iter;
    (*iter)++;
    br_ssl_engine_context *e = &t->cc.eng;
    unsigned st = br_ssl_engine_current_state(e);
    if(trace) log_msg("tls: pump[%d] st=0x%x", current, st);
    if(st & BR_SSL_SENDREC){
        size_t sz; unsigned char *b = br_ssl_engine_sendrec_buf(e, &sz);
        int r = raw_send(t->fd, b, sz);
        if(trace) log_msg("tls: pump[%d] send sz=%zu r=%d", current, sz, r);
        if(r > 0){ br_ssl_engine_sendrec_ack(e, (size_t)r); return 1; }
        return 0;
    }
    if(st & BR_SSL_RECVREC){
        size_t sz; unsigned char *b = br_ssl_engine_recvrec_buf(e, &sz);
        int r = raw_recv(t->fd, b, sz);
        if(trace) log_msg("tls: pump[%d] recv sz=%zu r=%d", current, sz, r);
        if(r > 0){ br_ssl_engine_recvrec_ack(e, (size_t)r); return 1; }
        if(r == 0){ log_msg("tls: recv EOF"); br_ssl_engine_recvrec_ack(e, 0); return 1; }
        return 0;
    }
    return 0;
}

static void dump_error(tls_ctx_t *t, const char *where){
    int err = (int)br_ssl_engine_last_error(&t->cc.eng);
    log_msg("tls: %s failed err=%d", where, err);
}

tls_ctx_t *tls_start(int fd, const char *host){
    tls_ctx_t *t = (tls_ctx_t*)calloc(1, sizeof *t);
    if(!t) return NULL;
    t->fd = fd;
    t->iobuf = (unsigned char*)malloc(BR_SSL_BUFSIZE_BIDI);
    if(!t->iobuf){ free(t); return NULL; }

    br_ssl_client_zero(&t->cc);
    memset(&t->mx, 0, sizeof t->mx);
    {
        /* full profile wires prf/ciphers/ec/rsa defaults; we then swap the
         * verifier for our decode-only validator (no trust anchors) */
        static br_x509_minimal_context xc;
        br_ssl_client_init_full(&t->cc, &xc, NULL, 0);
    }
    t->mx.vtable = &mini_vtable;
    br_ssl_engine_set_x509(&t->cc.eng, &t->mx.vtable);
    br_ssl_engine_set_buffer(&t->cc.eng, t->iobuf, BR_SSL_BUFSIZE_BIDI, 1);

    if(br_ssl_client_reset(&t->cc, host, 0) == 0){
        log_msg("tls: client reset fail");
        free(t->iobuf); free(t);
        return NULL;
    }
    log_msg("tls: handshake start");

    /* handshake: pump until application-data phase or failure */
    int hs_iter = 0;
    int64_t dl = time(NULL) + 15;
    for(;;){
        unsigned st = br_ssl_engine_current_state(&t->cc.eng);
        if(st & BR_SSL_CLOSED){
            log_msg("tls: hs closed st=0x%x err=%d (0x%x)", st,
                    (int)br_ssl_engine_last_error(&t->cc.eng),
                    (unsigned)br_ssl_engine_last_error(&t->cc.eng));
            free(t->iobuf); free(t);
            return NULL;
        }
        if((st & BR_SSL_RECVAPP) || (st & BR_SSL_SENDAPP)) break; /* ready */
        if(!pump_once(t, &hs_iter)){
            if(time(NULL) > dl){ log_msg("tls: handshake timeout"); free(t->iobuf); free(t); return NULL; }
            usleep(20000);
        }
    }
    log_msg("tls: established");
    return t;
}

int tls_read(tls_ctx_t *t, void *buf, size_t cap){
    br_ssl_engine_context *e = &t->cc.eng;
    unsigned st = br_ssl_engine_current_state(e);
    if(st & BR_SSL_CLOSED) return -1;

    /* decrypted bytes waiting? hand them over */
    if(st & BR_SSL_RECVAPP){
        size_t sz; unsigned char *p = br_ssl_engine_recvapp_buf(e, &sz);
        size_t n = sz < cap ? sz : cap;
        memcpy(buf, p, n);
        br_ssl_engine_recvapp_ack(e, n);
        return (int)n;
    }

    /* opportunistic flush of queued outbound records */
    if(st & BR_SSL_SENDREC){
        size_t sz; unsigned char *b = br_ssl_engine_sendrec_buf(e, &sz);
        int r = raw_send(t->fd, b, sz);
        if(r > 0) br_ssl_engine_sendrec_ack(e, (size_t)r);
    }

    /* pull ciphertext to decrypt more app data */
    if(st & BR_SSL_RECVREC){
        size_t sz; unsigned char *b = br_ssl_engine_recvrec_buf(e, &sz);
        int r = raw_recv(t->fd, b, sz);
        if(r > 0){ br_ssl_engine_recvrec_ack(e, (size_t)r); return 0; }
        if(r == 0){ br_ssl_engine_recvrec_ack(e, 0); return -1; }
        /* r<0: would-block or dead — let the heartbeat timeout decide */
        return 0;
    }
    return 0;
}

int tls_write(tls_ctx_t *t, const void *buf, size_t len){
    const unsigned char *p = (const unsigned char*)buf;
    int64_t dl = time(NULL) + 10;
    while(len > 0){
        br_ssl_engine_context *e = &t->cc.eng;
        unsigned st = br_ssl_engine_current_state(e);
        if(st & BR_SSL_CLOSED){ dump_error(t, "write"); return -1; }
        if(st & BR_SSL_SENDAPP){
            size_t sz; unsigned char *b = br_ssl_engine_sendapp_buf(e, &sz);
            size_t n = sz < len ? sz : len;
            memcpy(b, p, n);
            br_ssl_engine_sendapp_ack(e, n);
            p += n; len -= n;
            continue;
        }
        if(st & BR_SSL_SENDREC){
            size_t sz; unsigned char *b = br_ssl_engine_sendrec_buf(e, &sz);
            int r = raw_send(t->fd, b, sz);
            if(r > 0){ br_ssl_engine_sendrec_ack(e, (size_t)r); continue; }
        }
        if(time(NULL) > dl){ log_msg("tls: write timeout"); return -1; }
        usleep(10000);
    }
    /* flush records out promptly */
    int64_t t2 = time(NULL) + 5;
    for(;;){
        br_ssl_engine_context *e = &t->cc.eng;
        unsigned st = br_ssl_engine_current_state(e);
        if(st & BR_SSL_CLOSED){ dump_error(t, "flush"); return -1; }
        if(!(st & BR_SSL_SENDREC)) break;
        size_t sz; unsigned char *b = br_ssl_engine_sendrec_buf(e, &sz);
        int r = raw_send(t->fd, b, sz);
        if(r > 0){ br_ssl_engine_sendrec_ack(e, (size_t)r); continue; }
        if(time(NULL) > t2){ log_msg("tls: flush timeout"); return -1; }
        usleep(10000);
    }
    return (int)(p - (const unsigned char*)buf);
}

void tls_free(tls_ctx_t *t){
    if(!t) return;
    free(t->iobuf);
    free(t);
}
