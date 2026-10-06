/* daemon.h - orbisRPC daemon loop (payload ELF only). */
#ifndef DAEMON_H
#define DAEMON_H
/* Returns 0 on clean stop, 1 on missing/invalid configuration, or 2 when
 * Discord rejects the token with close code 4004. */
int daemon_run(void);
void daemon_request_stop(void);
void daemon_clear_stop(void);
int daemon_stop_requested(void);
#endif