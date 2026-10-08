/* ws_skip.c - oversized-frame drain arithmetic, split out of ws.c.
 *
 * This is the only part of ws.c that can be tested on the host: it has no
 * socket, no tls_ctx_t and no PS4 headers. The rest of ws_recv_frame() is
 * wired to those, so a bug here used to be invisible until a real gateway
 * handshake on a real console.
 *
 * Why it is worth isolating: drain state lives in the ws_t, so a wrong
 * value here does not fail loudly. On console 2026-10-06 an 11.7 MB READY
 * overflowed the buffer, the drain miscounted, and the next parse read JSON
 * payload as a frame header (op=2 fin=0 plen=125 b1=0x22 -- a quote byte),
 * which surfaced only as "cannot connect to discord".
 *
 * Included by ws.c; compiled directly by tests/test_utils.c. Both paths get
 * the same code, so the tests cannot drift from the payload.
 */
#include "ws.h"

void ws_skip_plan(uint64_t plen, size_t payload_here, uint64_t *skip_left_out){
    if(!skip_left_out) return;
    *skip_left_out = ((uint64_t)payload_here >= plen)
                     ? 0 : plen - (uint64_t)payload_here;
}