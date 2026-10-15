/* supervise.c - fault isolation around the daemon body.
 * A fault (SEGV/BUS/ILL/FPE) inside the gateway loop lands back in
 * supervise_run via siglongjmp; the crash hook tears down the live
 * connection, and the body is retried with backoff on clean state
 * (daemon_run resets all session state at entry). Never exits on
 * faults: delays grow 5,10,20.. capped at 600s (quiet persistence). */
#include "supervise.h"
#include "daemon.h"
#include "log.h"
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>

static supervise_hook_t s_hook = NULL;
static sigjmp_buf s_jmp;

void supervise_set_crash_hook(supervise_hook_t hook){ s_hook = hook; }

int supervise_fault_delay(int nfaults){
    if(nfaults < 0) nfaults = 0;
    if(nfaults > 7) nfaults = 7;   /* 5<<7 = 640, capped below */
    int d = 5 << nfaults;
    return d > 600 ? 600 : d;
}

/* daemon_sess_reset lives here (not daemon.c) so host tests can link
 * and drive it without pulling in the whole daemon. */
void daemon_sess_reset(daemon_sess_t *s){
    if(!s) return;
    s->active = 0;
    s->pres = PS_NONE;
    s->last[0] = 0;
    s->sess_tid[0] = 0;
    s->started = 0;
    s->last_tsync = 0;
    s->home_posted = 0;
    s->prev_tid[0] = 0;
    s->prev_started = 0;
    s->prev_end = 0;
    s->cand_title[0] = 0;
    s->cand_hits = 0;
    s->miss_hits = 0;
    s->healthy_marked = 0;
    s->sess_restored = 0;
    s->last_reconcile = 0;
}

/* Fault handler: async-signal-safe (siglongjmp only, no logging here —
 * the fault context is not safe for I/O; supervise_run logs after). */
static void on_fault(int sig){
    (void)sig;
    siglongjmp(s_jmp, 1);
}

int supervise_run(int (*body)(void)){
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
    struct sigaction sa, old[4];
    unsigned i;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* no SA_RESTART: a fault must interrupt sleeps */
    for(i = 0; i < 4; i++)
        sigaction(sigs[i], &sa, &old[i]);
    int nfaults = 0;
    int rc = 0;
    for(;;){
        if(sigsetjmp(s_jmp, 1) == 0){
            rc = body();
            break;
        }
        log_msg("supervisor: fault in daemon body; running crash hook");
        int retry = s_hook ? s_hook() : 1;
        if(!retry){ rc = 0; break; }
        int d = supervise_fault_delay(nfaults++);
        log_msg("supervisor: retrying daemon in %ds (fault %d)", d, nfaults);
        sleep((unsigned)d);   /* interrupted sleep just retries sooner */
    }
    for(i = 0; i < 4; i++)
        sigaction(sigs[i], &old[i], NULL);
    return rc;
}
