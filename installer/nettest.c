/* nettest.c - connect-only probes. A closed port and a filtered host
 * both read as unreachable; that honesty is the point (Sony TMDB from
 * the console genuinely fails while Discord succeeds). */
#include "nettest.h"
#include "send.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>

/* Non-blocking connect with a hard deadline lives in send.c (shared);
 * blocking connect ignores SNDTIMEO on filtered hosts (75 s hole). */

int net_probe(const char *host, int port, int timeout_s){
    struct addrinfo hints, *res = NULL, *rp;
    char svc[8];
    int ok = 0;
    if(!host || !host[0] || port <= 0 || port > 65535) return 0;
    if(net_init() != 0) return 0; /* stack dead: everything unreachable */
    if(timeout_s < 1) timeout_s = 1;
    if(timeout_s > 10) timeout_s = 10;
    snprintf(svc, sizeof svc, "%d", port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; /* v4 only: deterministic on console stacks */
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, svc, &hints, &res) != 0 || !res) return 0;
    for(rp = res; rp; rp = rp->ai_next){
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(fd < 0) continue;
        if(sock_connect_deadline(fd, rp->ai_addr, rp->ai_addrlen, timeout_s) == 0) ok = 1;
        close(fd);
        if(ok) break;
    }
    freeaddrinfo(res);
    return ok;
}
