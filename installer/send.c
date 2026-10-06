/* send.c - loopback payload injection: file bytes over TCP to the
 * console's own loaders. Chunked send (no whole-file malloc). */
#include "send.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#ifdef INSTALLER_PS4
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#endif

#define SEND_CHUNK (64u*1024u)
static const int SEND_PORTS[] = { 9090, 9021, 9020 };

int net_init(void){
#ifdef INSTALLER_PS4
    static int done = 0;
    int probe;
    if(done) return 0;
    /* Internal NET module (Payload Guest boot pattern) + best-effort
     * stack init. Ground truth is a probe socket, not return codes. */
    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    (void)sceNetInit();
    (void)sceNetPoolCreate("orbisrpc", 64*1024, 0);
    probe = socket(AF_INET, SOCK_STREAM, 0);
    if(probe < 0) return -1;
    close(probe);
    done = 1;
#endif
    return 0;
}

int sock_connect_deadline(int fd, const struct sockaddr *sa, socklen_t len, int timeout_s){
    int flags, rc, err = 0;
    socklen_t elen = sizeof err;
    fd_set wf;
    struct timeval tv;
    if(timeout_s < 1) timeout_s = 1;
    if(timeout_s > 15) timeout_s = 15;
    flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) return -1;
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    rc = connect(fd, sa, len);
    if(rc == 0){
        fcntl(fd, F_SETFL, flags);
        return 0;
    }
    if(errno != EINPROGRESS){
        fcntl(fd, F_SETFL, flags);
        return -1;
    }
    FD_ZERO(&wf);
    FD_SET(fd, &wf);
    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;
    rc = select(fd + 1, NULL, &wf, NULL, &tv);
    fcntl(fd, F_SETFL, flags);
    if(rc <= 0) return -1;
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) return -1;
    return err == 0 ? 0 : -1;
}

static int send_one(int fd, const char *path, void (*progress)(unsigned)){
    FILE *f = fopen(path, "rb");
    /* Static, not stack: 64 KB is a real fraction of an app thread stack. */
    static unsigned char buf[SEND_CHUNK];
    long total = 0, sent = 0;
    size_t n;
    if(!f) return -1;
    if(fseek(f, 0, SEEK_END) != 0){ fclose(f); return -1; }
    total = ftell(f);
    if(total <= 0 || total > 16*1024*1024){ fclose(f); return -1; }
    if(fseek(f, 0, SEEK_SET) != 0){ fclose(f); return -1; }
    if(progress) progress(0);
    while((n = fread(buf, 1, sizeof buf, f)) > 0){
        size_t off = 0;
        int stalls = 0;
        while(off < n){
            ssize_t w = send(fd, buf + off, n - off, 0);
            if(w <= 0){
                /* Blocking socket + SNDTIMEO: EAGAIN means the loader
                 * stopped reading. Retry briefly, then fail instead of
                 * spinning forever. */
                if((errno == EAGAIN || errno == EWOULDBLOCK) && ++stalls < 4)
                    continue;
                fclose(f);
                return -1;
            }
            stalls = 0;
            off += (size_t)w;
        }
        sent += (long)n;
        if(progress) progress((unsigned)(sent * 100 / total));
    }
    fclose(f);
    if(progress) progress(100);
    return 0;
}

int send_file_loopback(const char *path, int *port_used, void (*progress)(unsigned)){
    unsigned i;
    if(!path || !path[0]) return -1;
    if(net_init() != 0) return -1; /* no stack, no sockets: fail, don't crash */
    for(i = 0; i < sizeof SEND_PORTS/sizeof SEND_PORTS[0]; i++){
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa;
        struct timeval tv = { 20, 0 };
        if(fd < 0) continue;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)SEND_PORTS[i]);
        sa.sin_addr.s_addr = inet_addr("127.0.0.1");
        if(sock_connect_deadline(fd, (struct sockaddr *)&sa, sizeof sa, 5) < 0){
            close(fd);
            continue;
        }
        /* Back to blocking for the bulk send, WITH a send timeout so a
         * stalled loader surfaces EAGAIN (capped retries below) instead
         * of wedging forever. */
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if(send_one(fd, path, progress) == 0){
            close(fd);
            if(port_used) *port_used = SEND_PORTS[i];
            return 0;
        }
        close(fd);
    }
    return -1;
}
