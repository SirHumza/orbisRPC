/* timesync.c - minimal SNTP client (RFC 5905, client mode only).
 * One UDP exchange per call; 3s I/O budget; any failure keeps the old
 * offset. Host-testable (POSIX only). */
#include "timesync.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <orbis/Net.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#define NTP_EPOCH_OFFSET 2208988800u
#define NTP_PORT 123

static int64_t s_offset = 0;
static int s_synced = 0;

static int sntp_once(const char *host){
    unsigned char pkt[48];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = 0x1b; /* LI=0, VN=3, Mode=3 (client) */
#ifdef ORBISRPC_SDK_PAYLOAD
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    {
        char portbuf[8];
        snprintf(portbuf, sizeof portbuf, "%d", NTP_PORT);
        if(getaddrinfo(host, portbuf, &hints, &res) != 0 || !res) return -1;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { 3, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int ok = -1;
    /* Connect the UDP socket: recv() then only accepts the peer we
     * queried, instead of any spoofed datagram on the LAN. */
    if(connect(fd, res->ai_addr, res->ai_addrlen) != 0){
        freeaddrinfo(res);
        close(fd);
        return -1;
    }
    if(send(fd, pkt, sizeof pkt, 0) == (int)sizeof pkt){
        unsigned char rep[48];
        ssize_t n = recv(fd, (char *)rep, sizeof rep, 0);
        if(n >= 48){
            uint32_t tx;
            memcpy(&tx, rep + 40, 4);
            tx = ((tx & 0xff) << 24) | ((tx & 0xff00) << 8) |
                 ((tx & 0xff0000) >> 8) | ((tx & 0xff000000) >> 24);
            /* Sanity window Nov 2023..Dec 2034 (uint32 NTP wraps 2036):
             * a broken/malicious server cannot fling our clock. */
            if(tx > NTP_EPOCH_OFFSET + 1700000000u &&
               tx < NTP_EPOCH_OFFSET + 2050000000u){
                int64_t ntp_unix = (int64_t)(tx - NTP_EPOCH_OFFSET);
                s_offset = ntp_unix - (int64_t)time(NULL);
                s_synced = 1;
                ok = 0;
            }
        }
    }
    freeaddrinfo(res);
    close(fd);
    return ok;
#else
    (void)host;
    return -1;
#endif
}

int time_sync(void){
    static const char *hosts[] = {
        "time.google.com", "pool.ntp.org", "time.cloudflare.com", NULL
    };
    /* One host per call, rotating: a full 3-host sweep can stall ~20s+,
     * which must never sit inside the 1s gateway loop (heartbeat
     * starvation). Boot sweeps via time_sync_all(); hourly ticks call
     * this (single ~6s-bounded attempt). */
    static int next_host = 0;
    const char *h = hosts[next_host];
    next_host = (next_host + 1) % 3;
    if(sntp_once(h) == 0){
        log_msg("time sync ok via %s (offset %+llds)",
                h, (long long)s_offset);
        return 0;
    }
    log_msg("time sync via %s failed; using local clock (offset %+llds)",
            h, (long long)s_offset);
    return -1;
}

/* Boot sweep: try all hosts (bounded, once per boot, before first
 * connect — never in the live loop). */
int time_sync_all(void){
    for(int i = 0; i < 3; i++){
        if(time_sync() == 0) return 0;
    }
    return -1;
}

int64_t time_fixed(void){
    return (int64_t)time(NULL) + s_offset;
}
