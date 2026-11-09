#ifndef WS_FRAME_H
#define WS_FRAME_H
#include <stddef.h>
#include <stdint.h>

size_t ws_control_frame_encode(unsigned char opcode,
                               const unsigned char *payload, size_t len,
                               const unsigned char mask[4],
                               unsigned char *out, size_t cap);
int ws_frame_header_valid(const unsigned char *header, uint64_t payload_len);
int ws_close_payload_valid(const unsigned char *payload, size_t len,
                           unsigned *code_out);

#endif
