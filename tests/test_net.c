/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* test_net.c - host tests for the bounded connect helper.
 *
 * Compiled with ORBISRPC_SDK_PAYLOAD so the real BSD-socket path is
 * exercised. Verifies a successful local connect returns a non-blocking fd
 * and that a refused port fails cleanly. The timeout path needs a blackholed
 * route and is not part of this test; the non-blocking + select code it
 * exercises is the same path.
 */
#define ORBISRPC_SDK_PAYLOAD 1
#include "../orbisrpc/netconnect.h"
#include <assert.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int listening_on(int *port_out){
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    assert(ls >= 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x7f000001u); /* 127.0.0.1; INADDR_LOOPBACK is
                                              * hidden by strict POSIX here */
    sa.sin_port = 0;
    assert(bind(ls, (struct sockaddr *)&sa, sizeof sa) == 0);
    assert(listen(ls, 4) == 0);
    socklen_t sl = sizeof sa;
    assert(getsockname(ls, (struct sockaddr *)&sa, &sl) == 0);
    *port_out = ntohs(sa.sin_port);
    return ls;
}

static void test_connect_ok(void){
    int port = 0;
    int ls = listening_on(&port);
    int fd = net_tcp_connect("127.0.0.1", port, 2);
    assert(fd >= 0);
    int fl = fcntl(fd, F_GETFL, 0);
    assert(fl >= 0 && (fl & O_NONBLOCK));
    close(fd);
    close(ls);
}

static void test_connect_refused(void){
    int port = 0;
    int ls = listening_on(&port);
    close(ls); /* nothing listens here now */
    int fd = net_tcp_connect("127.0.0.1", port, 2);
    assert(fd == -1);
}

static void test_bad_args(void){
    assert(net_tcp_connect(NULL, 443, 2) == -1);
    assert(net_tcp_connect("", 443, 2) == -1);
    assert(net_tcp_connect("127.0.0.1", 0, 2) == -1);
    assert(net_tcp_connect("127.0.0.1", 70000, 2) == -1);
}

int main(void){
    test_bad_args();
    test_connect_refused();
    test_connect_ok();
    puts("netconnect tests passed");
    return 0;
}
