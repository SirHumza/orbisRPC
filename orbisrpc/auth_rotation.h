#ifndef ORBISRPC_AUTH_ROTATION_H
#define ORBISRPC_AUTH_ROTATION_H
#include <string.h>
/* Pending server credential plus its original on-disk generation. */
typedef struct { char value[512]; char origin[512]; } auth_rotation_t;
typedef enum {
    AUTH_ROTATION_NONE,
    AUTH_ROTATION_APPLY,
    AUTH_ROTATION_DISCARD
} auth_rotation_action_t;
static inline auth_rotation_action_t auth_rotation_disk_action(
    const auth_rotation_t *pending, const char *disk){
    if(!pending || !pending->value[0] || !disk) return AUTH_ROTATION_NONE;
    return strcmp(disk, pending->origin) == 0 ?
           AUTH_ROTATION_APPLY : AUTH_ROTATION_DISCARD;
}
static inline int auth_rotation_matches(const auth_rotation_t *pending,
                                        const char *disk, const char *live){
    if(!disk || !live) return 0;
    if(!strcmp(disk, live)) return 1;
    return pending && pending->value[0] && !strcmp(pending->value, live) &&
           !strcmp(pending->origin, disk);
}
static inline void auth_rotation_remember(auth_rotation_t *pending,
                                          const char *next, const char *previous){
    if(!pending || !next || !previous || strlen(next) >= sizeof pending->value ||
       strlen(previous) >= sizeof pending->origin) return;
    if(!pending->value[0] || strcmp(pending->value, previous))
        strcpy(pending->origin, previous);
    strcpy(pending->value, next);
}

/* A definitive gateway authentication rejection is not a transport failure.
 * Avoid sending the same credential over and over; retry only after config
 * changes (the caller polls config again on its normal cadence). */
static inline int auth_token_wait_for_change(const char *rejected,
                                             const char *current) {
    return rejected && rejected[0] && current &&
           strcmp(rejected, current) == 0;
}
#endif
