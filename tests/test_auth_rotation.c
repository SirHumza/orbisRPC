#include "../orbisrpc/auth_rotation.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    auth_rotation_t p = {{0},{0}};
    assert(auth_rotation_matches(&p, "old", "old"));
    auth_rotation_remember(&p, "next", "old");
    assert(auth_rotation_matches(&p, "old", "next"));
    assert(!auth_rotation_matches(&p, "manual", "next"));
    auth_rotation_remember(&p, "newest", "next");
    assert(!strcmp(p.origin, "old"));
    assert(auth_rotation_matches(&p, "old", "newest"));
    assert(auth_rotation_matches(&p, "newest", "newest"));
    assert(!auth_rotation_matches(&p, "next", "newest"));
    assert(auth_token_wait_for_change("rejected", "rejected"));
    assert(!auth_token_wait_for_change("rejected", "replacement"));
    assert(!auth_token_wait_for_change("", "rejected"));
    puts("auth rotation lineage tests passed");
    return 0;
}
