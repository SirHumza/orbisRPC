/* tls.c - mbedTLS-backed TLS client for orbisRPC.
 *
 * Why mbedTLS and not BearSSL: Cloudflare (fronting gateway.discord.gg)
 * silently drops handshakes whose ClientHello looks non-browser. BearSSL
 * cannot offer session tickets, EMS or encrypt-then-MAC at all, so its
 * fingerprint always scores as a bot and the server ghosts us after the
 * handshake (established TLS, zero HTTP bytes back — verified on-host).
 * mbedTLS sends the standard extension set and gets 101 immediately.
 *
 * The socket stays non-blocking; WANT_READ/WRITE maps to our pump model.
 * No certificate validation: no trust store exists on console. Same
 * posture as before — encryption against passive sniffing, SNI is sent.
 */
#include "tls.h"
#include "log.h"
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <orbis/Net.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

struct tls_ctx {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_context ent;
    int fd;
};

/* Strong entropy from the OS; weak time fallback so we never hard-fail. */
static int orbis_poll(void *data, unsigned char *out, size_t len, size_t *olen){
    (void)data;
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd >= 0){
        size_t got = 0;
        while(got < len){
            long r = read(fd, (char *)out + got, len - got);
            if(r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
        if(got == len){ *olen = len; return 0; }
    }
    /* fallback: time + address jitter (weak, but keeps us functional) */
    srand((unsigned)(time(NULL) ^ (uintptr_t)out));
    for(size_t i = 0; i < len; i++) out[i] = (unsigned char)rand();
    *olen = len;
    return 0;
}

static int net_send(void *ctx, const unsigned char *b, size_t n){
    tls_ctx_t *t = (tls_ctx_t *)ctx;
    size_t cap = n > 32768 ? 32768 : n;
    int r = (int)sceNetSend(t->fd, b, (int)cap, 0);
    if(r < 0) return MBEDTLS_ERR_SSL_WANT_WRITE; /* NBIO: retry till deadline */
    return r;
}

static int net_recv(void *ctx, unsigned char *b, size_t n){
    tls_ctx_t *t = (tls_ctx_t *)ctx;
    size_t cap = n > 16384 ? 16384 : n;
    int r = (int)sceNetRecv(t->fd, b, (int)cap, 0);
    if(r < 0) return MBEDTLS_ERR_SSL_WANT_READ; /* NBIO: retry till deadline */
    return r;
}

tls_ctx_t *tls_start(int fd, const char *host){
    tls_ctx_t *t = (tls_ctx_t *)calloc(1, sizeof *t);
    if(!t) return NULL;
    t->fd = fd;
    int rc = 0;

    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ctr_drbg_init(&t->rng);
    mbedtls_entropy_init(&t->ent);
    mbedtls_entropy_add_source(&t->ent, orbis_poll, NULL, 64,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);

    if((rc = mbedtls_ctr_drbg_seed(&t->rng, mbedtls_entropy_func, &t->ent,
                                   (const unsigned char *)"orbisRPC", 8)) != 0){
        log_msg("tls: rng seed fail %d", rc);
        goto fail;
    }
    if((rc = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0){
        log_msg("tls: config fail %d", rc);
        goto fail;
    }
    /* No trust store on console: parse the chain, skip validation. */
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->rng);
    {
        static const char *protos[] = { "http/1.1", NULL };
        mbedtls_ssl_conf_alpn_protocols(&t->conf, protos);
    }
    if((rc = mbedtls_ssl_setup(&t->ssl, &t->conf)) != 0){
        log_msg("tls: setup fail %d", rc);
        goto fail;
    }
    if((rc = mbedtls_ssl_set_hostname(&t->ssl, host)) != 0){
        log_msg("tls: hostname fail %d", rc);
        goto fail;
    }
    mbedtls_ssl_set_bio(&t->ssl, t, net_send, net_recv, NULL);

    log_msg("tls: handshake start");
    {
        int64_t dl = time(NULL) + 15;
        extern int daemon_stop_requested(void) __attribute__((weak));
        for(;;){
            if(daemon_stop_requested && daemon_stop_requested()){ log_msg("tls: aborted (stop)"); goto fail; }
            rc = mbedtls_ssl_handshake(&t->ssl);
            if(rc == 0) break;
            if(rc != MBEDTLS_ERR_SSL_WANT_READ &&
               rc != MBEDTLS_ERR_SSL_WANT_WRITE){
                log_msg("tls: handshake fail %d", rc);
                goto fail;
            }
            if(time(NULL) > dl){ log_msg("tls: handshake timeout"); goto fail; }
            usleep(20000);
        }
    }
    log_msg("tls: established (%s)", mbedtls_ssl_get_version(&t->ssl));
    return t;

fail:
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->rng);
    mbedtls_entropy_free(&t->ent);
    free(t);
    return NULL;
}

int tls_read(tls_ctx_t *t, void *buf, size_t cap){
    if(!t || !buf || cap == 0) return -1;
    int r = mbedtls_ssl_read(&t->ssl, (unsigned char *)buf, cap);
    if(r > 0) return r;
    if(r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 0; /* nothing yet */
    if(r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0) return -1;
    return -1;
}

int tls_write(tls_ctx_t *t, const void *buf, size_t len){
    if(!t || (!buf && len)) return -1;
    const unsigned char *p = (const unsigned char *)buf;
    size_t done = 0;
    int64_t dl = time(NULL) + 10;
    while(done < len){
        int r = mbedtls_ssl_write(&t->ssl, p + done, len - done);
        if(r > 0){ done += (size_t)r; continue; }
        if(r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE){
            log_msg("tls: write fail %d", r);
            return -1;
        }
        if(time(NULL) > dl){ log_msg("tls: write timeout"); return -1; }
        usleep(10000);
    }
    return (int)done;
}

void tls_free(tls_ctx_t *t){
    if(!t) return;
    mbedtls_ssl_close_notify(&t->ssl);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->rng);
    mbedtls_entropy_free(&t->ent);
    free(t);
}
