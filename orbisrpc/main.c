/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* main.c - orbisRPC payload entry: detect game -> Discord presence.
 * Runs as a GoldHEN payload (BinLoader push) or standalone ELF. */
#include "daemon.h"
#include "log.h"
#include "notify.h"
#include <stddef.h>

int main(int argc, char **argv){
    (void)argc;
    daemon_set_launch_path(argv && argv[0] ? argv[0] : "");
    log_init("/data/orbisRPC/log.txt");
    log_msg("orbisRPC payload boot");
    notify_once_boot("boot", "orbisRPC loaded");
    return daemon_run();
}