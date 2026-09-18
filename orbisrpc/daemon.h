/* daemon.h - shared orbisRPC daemon loop (payload ELF and GoldHEN plugin). */
#ifndef DAEMON_H
#define DAEMON_H
/* fixed_game_name != NULL: post presence for that game only (plugin mode).
 * NULL: poll the foreground app (payload mode).
 * Returns 0 on clean stop, 1 on missing/invalid configuration, or 2 when
 * Discord rejects the token with close code 4004. */
int daemon_run(const char *fixed_game_name);
void daemon_request_stop(void);
void daemon_clear_stop(void);
int daemon_stop_requested(void); /* 1 when plugin_unload() wants us to exit */
#endif