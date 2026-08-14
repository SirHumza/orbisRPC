/* main.c - orbisRPC payload entry: detect game -> Discord presence.
 * Runs as a GoldHEN payload (BinLoader push) or standalone ELF. */
#include "daemon.h"
#include <stddef.h>

int main(int argc, char **argv){
    (void)argc;(void)argv;
    return daemon_run(NULL);
}