#include "../orbisrpc/auth_rotation.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    auth_rotation_t p = {{0},{0}};
    assert(auth_rotation_disk_action(&p, "old") == AUTH_ROTATION_NONE);
    assert(auth_rotation_matches(&p, "old", "old"));
    auth_rotation_remember(&p, "next", "old");
    assert(auth_rotation_disk_action(&p, "old") == AUTH_ROTATION_APPLY);
    /* A failed save leaves the pending rotation eligible on the next poll. */
    assert(auth_rotation_disk_action(&p, "old") == AUTH_ROTATION_APPLY);
    assert(auth_rotation_matches(&p, "old", "next"));
    assert(auth_rotation_disk_action(&p, "manual") == AUTH_ROTATION_DISCARD);
    assert(!auth_rotation_matches(&p, "manual", "next"));
    auth_rotation_remember(&p, "newest", "next");
    assert(!strcmp(p.origin, "old"));
    assert(auth_rotation_disk_action(&p, "old") == AUTH_ROTATION_APPLY);
    assert(auth_rotation_matches(&p, "old", "newest"));
    assert(auth_rotation_matches(&p, "newest", "newest"));
    assert(!auth_rotation_matches(&p, "next", "newest"));
    assert(auth_token_wait_for_change("rejected", "rejected"));
    assert(!auth_token_wait_for_change("rejected", "replacement"));
    assert(!auth_token_wait_for_change("", "rejected"));
    puts("auth rotation lineage tests passed");
    return 0;
}
