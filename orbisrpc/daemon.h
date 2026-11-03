/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* daemon.h - orbisRPC daemon loop (payload ELF only). */
#ifndef DAEMON_H
#define DAEMON_H
/* Returns 0 on clean stop, 1 on missing/invalid configuration, or 2 when
 * Discord rejects the token with close code 4004. */
int daemon_run(void);
void daemon_request_stop(void);
void daemon_clear_stop(void);
int daemon_stop_requested(void);
/* Records argv[0] before daemon_run() so the first log lines say where the
 * payload was launched from (D-17). */
void daemon_set_launch_path(const char *path);
#endif