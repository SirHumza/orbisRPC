/* main.c - orbisRPC payload entry: detect game -> Discord presence.
 * Runs as a GoldHEN payload (BinLoader push) or standalone ELF. */
#include "daemon.h"
#include "supervise.h"
#include "log.h"
#include "notify.h"
#include <stddef.h>

int main(int argc, char **argv){
    (void)argc;(void)argv;
    log_init("/data/orbisRPC/log.txt");
    log_msg("orbisRPC payload boot");
    notify_once_boot("boot", "orbisRPC loaded");
    /* Fault isolation: a crash in the gateway loop runs the cleanup hook
     * and retries on clean state instead of dying. */
    supervise_set_crash_hook(daemon_crash_cleanup);
    return supervise_run(daemon_run);
}