/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* builtin_titles.h - names for titles no network index knows.
 *
 * D-08/D-09: the metadata sources (Sony TMDB, pkg-zone, retro indexes) have
 * no entry for some homebrew IDs, and users saw their app reported as
 * "unknown". A short built-in table covers the ones reported in the wild;
 * every other unresolved title still falls back to its title ID plus the
 * generic icon, never to "not recognized".
 *
 * Header-only so host tests cover the exact lookup the payload uses. */
#ifndef ORBISRPC_BUILTIN_TITLES_H
#define ORBISRPC_BUILTIN_TITLES_H
#include <string.h>

/* Returns a static name, or NULL when the id is not in the table. */
static inline const char *builtin_title_name(const char *id){
    static const struct { const char *id; const char *name; } known[] = {
        /* Elysium homebrew app (reported 2026-10-19, D-09). */
        { "ELYS00001", "Elysium" },
    };
    if(!id || !id[0]) return NULL;
    for(unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
        if(!strcmp(id, known[i].id)) return known[i].name;
    return NULL;
}

#endif /* ORBISRPC_BUILTIN_TITLES_H */
