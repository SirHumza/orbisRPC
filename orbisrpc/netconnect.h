/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* netconnect.h - bounded IPv4 TCP connect for the payload build.
 *
 * Why this exists: SO_SNDTIMEO does NOT bound connect(). Measured on a
 * blackholed route, a blocking connect() sat for the kernel's full SYN
 * budget (75 s observed) even with a 2 s send timeout, freezing the
 * daemon's only thread: no heartbeat, no detection, no STOP check. Every
 * network caller needs the same non-blocking connect + select deadline,
 * so it lives here instead of being re-implemented (and re-broken) per
 * module.
 *
 * The returned fd is non-blocking, which is what every TLS caller already
 * wants: tls_start() and the read loops carry their own deadlines and
 * treat WANT_READ/WANT_WRITE as retry.
 *
 * SDK payload build only. The OpenOrbis app build keeps its sceNet
 * transport in each module (that path is not the shipped daemon).
 */
#ifndef ORBISRPC_NETCONNECT_H
#define ORBISRPC_NETCONNECT_H

/* Resolve `host` (IPv4), connect to port, and give up after timeout_s.
 * Returns a connected non-blocking fd, or -1. Logs the resolved peer and
 * every failure so the daemon's log shows the cause. */
int net_tcp_connect(const char *host, int port, int timeout_s);

#endif /* ORBISRPC_NETCONNECT_H */
