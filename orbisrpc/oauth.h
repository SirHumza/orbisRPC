/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
#ifndef ORBISRPC_OAUTH_H
#define ORBISRPC_OAUTH_H
#include <stddef.h>
int oauth_enabled(void);
int oauth_prepare(void); /* 1 ready, 0 waiting/retry */
int oauth_due(void);
void oauth_rejected(void);
const char *oauth_token(void);
int oauth_valid_origin(const char *url);
#ifdef OAUTH_TESTABLE
#include "jsonlite.h"
void oauth_test_setup(const char *directory, jl_val_t *(*transport)(const char *, const char *, int *));
void oauth_test_retry(void);
#endif
#endif
