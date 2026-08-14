/* daemon.h - shared orbisRPC daemon loop (payload ELF and GoldHEN plugin). */
#ifndef DAEMON_H
#define DAEMON_H
/* fixed_game_name != NULL: post presence for that game only (plugin mode).
 * NULL: poll the foreground app (payload mode). Returns 0 on clean exit. */
int daemon_run(const char *fixed_game_name);
void daemon_request_stop(void);
void daemon_clear_stop(void);
#endif