/* nettest.h - honest TCP reachability probes (no traffic beyond connect). */
#ifndef INSTALLER_NETTEST_H
#define INSTALLER_NETTEST_H

/* 1 reachable, 0 not. timeout_s clamped 1..10. Host-testable. */
int net_probe(const char *host, int port, int timeout_s);

#endif
