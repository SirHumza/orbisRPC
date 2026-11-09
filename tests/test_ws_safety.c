/* Focused host regressions for ws_connect cleanup and request bounds. */
#define ORBISRPC_SDK_PAYLOAD 1
#include "../orbisrpc/ws.c"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

struct tls_ctx { int unused; };
static struct tls_ctx fake_tls;
static int writes;
int64_t orbis_mono_s(void) { return 0; }
int daemon_stop_requested(void) { return 0; }
void log_msg(const char *fmt, ...) { (void)fmt; }
int log_is_debug(void) { return 0; }
tls_ctx_t *tls_start(int fd, const char *host) { (void)fd; (void)host; return (tls_ctx_t *)&fake_tls; }
void tls_free(tls_ctx_t *t) { (void)t; }
int tls_write(tls_ctx_t *t, const void *buf, size_t len) { (void)t; (void)buf; writes++; return (int)len; }
int tls_read(tls_ctx_t *t, void *buf, size_t cap) { (void)t; (void)buf; (void)cap; return -1; }

static int listen_local(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7f000001u);
    assert(bind(fd, (struct sockaddr *)&a, sizeof a) == 0);
    assert(listen(fd, 1) == 0);
    socklen_t n = sizeof a;
    assert(getsockname(fd, (struct sockaddr *)&a, &n) == 0);
    *port = ntohs(a.sin_port);
    return fd;
}

int main(void) {
    int saved_stdin = dup(STDIN_FILENO);
    close(STDIN_FILENO);
    ws_t w;
    (void)ws_connect(&w, "127.0.0.1", 1, "/", "key");
    assert(fcntl(STDIN_FILENO, F_GETFD) == -1 && errno == EBADF);
    assert(w.fd == -1 && w.sock == -1);
    if(saved_stdin >= 0) { assert(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO); close(saved_stdin); }

    uint16_t port;
    int server = listen_local(&port);
    char resource[700];
    memset(resource, 'x', sizeof resource - 1);
    resource[sizeof resource - 1] = 0;
    writes = 0;
    assert(ws_connect(&w, "127.0.0.1", port, resource, "key") < 0);
    assert(writes == 0);
    assert(w.fd == -1 && w.sock == -1);
    close(server);
    return 0;
}
