/* send.h - loopback payload injection (clean-room, standard sockets).
 * Tries native GoldHEN BinLoader 9090, then elfldr 9021, then 9020.
 * 9020 is one-shot: accepted bytes count as sent. */
#ifndef INSTALLER_SEND_H
#define INSTALLER_SEND_H
#include <sys/socket.h>

/* progress(pct) may be NULL. Returns 0 + port_used set on success. */
int send_file_loopback(const char *path, int *port_used, void (*progress)(unsigned pct));

/* Non-blocking connect with a hard deadline (filtered hosts can't stall
 * past timeout_s). 0 connected, -1 failed. Leaves fd blocking. */
int sock_connect_deadline(int fd, const struct sockaddr *sa, socklen_t len, int timeout_s);

/* One-time network stack init (sceNetInit + pool). 0 ready, -1 dead.
 * No-op returning 0 on host test builds. Safe to call repeatedly. */
int net_init(void);

#endif
