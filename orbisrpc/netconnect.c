/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* netconnect.c - see netconnect.h. */
#include "netconnect.h"
#include "log.h"
#include "clock.h"

#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

int net_tcp_connect(const char *host, int port, int timeout_s){
    if(!host || !host[0] || port <= 0 || port > 65535) return -1;
    if(timeout_s <= 0) timeout_s = 10;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portbuf[8];
    snprintf(portbuf, sizeof portbuf, "%d", port);
    if(getaddrinfo(host, portbuf, &hints, &res) != 0 || !res){
        log_msg("net: resolve fail %s", host);
        if(res) freeaddrinfo(res);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){
        log_msg("net: socket fail errno=%d", errno);
        freeaddrinfo(res);
        return -1;
    }
    {
        struct sockaddr_in *a = (struct sockaddr_in *)res->ai_addr;
        unsigned char *q = (unsigned char *)&a->sin_addr.s_addr;
        log_msg("dial %s -> %u.%u.%u.%u:%d", host, q[0], q[1], q[2], q[3], port);
    }

    int fl = fcntl(fd, F_GETFL, 0);
    if(fl >= 0) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    int ce = errno;
    freeaddrinfo(res);

    /* Non-blocking connect reports progress as EINPROGRESS (some stacks
     * also use EWOULDBLOCK); anything else is a real failure. */
    if(rc != 0 && ce != EINPROGRESS && ce != EWOULDBLOCK){
        log_msg("net: connect %s:%d fail errno=%d", host, port, ce);
        close(fd);
        return -1;
    }
    if(rc != 0){
        int64_t dl = orbis_mono_s() + timeout_s;
        for(;;){
            int64_t left = dl - orbis_mono_s();
            if(left <= 0){
                log_msg("net: connect %s:%d timeout after %ds", host, port,
                        timeout_s);
                close(fd);
                return -1;
            }
            fd_set wf;
            FD_ZERO(&wf);
            FD_SET(fd, &wf);
            struct timeval tv = { .tv_sec = (time_t)left, .tv_usec = 0 };
            int s = select(fd + 1, NULL, &wf, NULL, &tv);
            if(s > 0) break;
            if(s < 0 && errno == EINTR) continue;
            log_msg("net: connect %s:%d select fail errno=%d", host, port,
                    s < 0 ? errno : 0);
            close(fd);
            return -1;
        }
        int err = 0;
        socklen_t el = sizeof err;
        if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0){
            log_msg("net: connect %s:%d fail errno=%d", host, port,
                    err ? err : errno);
            close(fd);
            return -1;
        }
    }
    return fd;
}

#else
int net_tcp_connect(const char *host, int port, int timeout_s){
    (void)host; (void)port; (void)timeout_s;
    return -1; /* app build: modules keep their Orbis transport */
}
#endif
