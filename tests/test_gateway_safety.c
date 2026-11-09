/* Standalone gateway regressions. Build from the repo root with:
 * cc -std=c11 -D_DEFAULT_SOURCE -Iorbisrpc tests/test_gateway_safety.c \
 *   orbisrpc/jsonlite.c orbisrpc/b64.c -o /tmp/test_gateway_safety
 * /tmp/test_gateway_safety
 */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "../orbisrpc/discord.c"
#include "../orbisrpc/ws_env.c"
#include "../orbisrpc/ws_frag.c"
#include "../orbisrpc/ws_frame.h"
#include "../orbisrpc/ws_frame.c"

static int64_t fake_now;
static int sent;
static int pongs;
static size_t last_pong_len;
static unsigned char last_pong[125];
static int scripted_recv;
static int scripted_result;
static int scripted_opcode;
static int scripted_fin;
static const char *scripted_payload;
static int connect_script;
static int auth_step;
static int connect_attempts;
static const char *script_hello;
static int auth_heartbeat_script;

int64_t orbis_mono_s(void){ return fake_now; }
void log_msg(const char *fmt, ...){ (void)fmt; }
void log_dbg(const char *fmt, ...){ (void)fmt; }
int art_resolve_mp(const char *app_id, const char *token, const char *url,
                   char *out, size_t cap){
    (void)app_id; (void)token; (void)url; (void)out; (void)cap; return -1;
}
int detect_media_type(const char *title){ (void)title; return 0; }
int ws_connect(ws_t *w, const char *host, int port, const char *resource, const char *key){
    (void)host; (void)port; (void)resource; (void)key;
    connect_attempts++;
    if(connect_script){ w->connected = 1; return 0; }
    return -1;
}
int ws_send_text(ws_t *w, const char *msg, size_t len){
    (void)w; (void)msg; sent++; return (int)len;
}
int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *op, int *fin){
    (void)w;
    if(connect_script){
        static const char *hello = "{\"op\":10,\"d\":{\"heartbeat_interval\":45000}}";
        static const char *heartbeat = "{\"op\":1,\"d\":null}";
        static const char *ready = "{\"op\":0,\"s\":42,\"t\":\"READY\",\"d\":{}}";
        assert(auth_step < (auth_heartbeat_script ? 3 : 2));
        const char *frame = auth_step == 0 ? (script_hello ? script_hello : hello)
            : auth_heartbeat_script && auth_step == 1 ? heartbeat : ready;
        auth_step++;
        size_t n = strlen(frame);
        assert(n <= cap);
        memcpy(buf, frame, n);
        if(n < cap) buf[n] = 0;
        if(op) *op = 1;
        if(fin) *fin = 1;
        return (int)n;
    }
    if(!scripted_recv) return 0;
    scripted_recv = 0;
    if(op) *op = scripted_opcode;
    if(fin) *fin = scripted_fin;
    if(scripted_result > 0 && scripted_payload){
        size_t n = (size_t)scripted_result < cap ? (size_t)scripted_result : cap;
        memcpy(buf, scripted_payload, n);
        if(n < cap) buf[n] = 0;
    }
    return scripted_result;
}
int ws_pong(ws_t *w, const unsigned char *payload, size_t len){
    (void)w;
    assert(len <= sizeof last_pong);
    if(len) memcpy(last_pong, payload, len);
    last_pong_len = len;
    pongs++;
    return 0;
}
int ws_close(ws_t *w){ (void)w; return 0; }

static void test_malformed_numbers(void){
    const char overflow[] = "{\"op\":999999999999999999999999,\"d\":{}}";
    const char suffix[] = "{\"op\":11x,\"d\":{}}";
    const char seq_overflow[] = "{\"op\":0,\"s\":999999999999999999999999,\"t\":\"READY\"}";
    discord_t d;
    memset(&d, 0, sizeof d);
    d.seq = 17;
    assert(gw_op(overflow, sizeof overflow - 1) == -1);
    assert(gw_op(suffix, sizeof suffix - 1) == -1);
    assert(valid_gateway_json(overflow)); /* JSON-valid, but integer range is not */
    gw_seq(&d, seq_overflow, sizeof seq_overflow - 1);
    assert(d.seq == 17);
    assert(!valid_gateway_json("{\"op\":11,"));
}

static void test_identify_echoes_qos_only_when_supplied(void){
    jl_val_t *identify = discord_build_identify("test-token", "signed-qos");
    assert(identify);
    const jl_val_t *data = jl_obj_get(identify, "d");
    assert(data && data->type == JL_OBJECT);
    const jl_val_t *token = jl_obj_get(data, "token");
    const jl_val_t *qos = jl_obj_get(data, "qos_token");
    assert(token && !strcmp(token->str, "test-token"));
    assert(qos && !strcmp(qos->str, "signed-qos"));
    assert(jl_obj_get(data, "capabilities") == NULL);
    jl_free(identify);

    identify = discord_build_identify("test-token", NULL);
    assert(identify);
    data = jl_obj_get(identify, "d");
    assert(jl_obj_get(data, "qos_token") == NULL);
    jl_free(identify);
}

static void test_rotation_take_validation(void){
    discord_t d;
    char out[32] = {0};
    memset(&d, 0, sizeof d);
    strcpy(d.ws.rotated_token, "rotated-B");
    assert(discord_take_rotated_token(&d, out, 4) == -1);
    assert(!strcmp(d.ws.rotated_token, "rotated-B"));
    assert(discord_take_rotated_token(&d, out, sizeof out) == 1);
    assert(!strcmp(out, "rotated-B") && d.ws.rotated_token[0] == 0);
    strcpy(d.ws.rotated_token, "bad token");
    assert(discord_take_rotated_token(&d, out, sizeof out) == -1);
    assert(d.ws.rotated_token[0] == 0);
}

static void test_credentials_clear_after_session(void){
    discord_t d;
    memset(&d, 0, sizeof d);
    strcpy(d.token, "session-token");
    strcpy(d.qos_token, "signed-qos");
    strcpy(d.ws.rotated_token, "rotated-token");
    strcpy(d.ws.qos_token, "next-qos");
    strcpy(d.ws.env.auth_token, "captured-token");
    strcpy(d.ws.env.qos_token, "captured-qos");
    d.ws.env.have_auth_token = d.ws.env.have_qos_token = 1;
    discord_clear_credentials(&d);
    assert(!d.token[0] && !d.qos_token[0]);
    assert(!d.ws.rotated_token[0] && !d.ws.qos_token[0]);
    assert(!d.ws.env.auth_token[0] && !d.ws.env.qos_token[0]);
    assert(!d.ws.env.have_auth_token && !d.ws.env.have_qos_token);
}

static void test_first_heartbeat_ack_window(void){
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 5000;
    d.last_heartbeat = 0;
    d.last_ack = 0; /* connection/HELLO time, long before the first send */
    fake_now = 100;
    sent = 0;
    assert(discord_tick(&d) == 0);
    assert(sent == 1 && d.sent_hb && d.last_ack == fake_now);

    /* An unacknowledged first heartbeat remains alive until its own timeout. */
    fake_now += 25;
    assert(discord_tick(&d) == 0);
    fake_now++;
    assert(discord_tick(&d) == -1);
}

static void test_connect_resets_session_state_before_identify(void){
    discord_t d;
    memset(&d, 0, sizeof d);
    d.seq = 99;
    d.sent_hb = 1;
    d.presence_next = 4;
    d.presence_sent_at[0] = 456;
    d.pending_presence_len = 3;
    memcpy(d.pending_presence, "old", 3);
    connect_script = 1;
    auth_step = 0;
    assert(discord_connect(&d, "test-token", NULL) == 0);
    assert(d.connected && d.seq == 42);
    assert(!d.sent_hb && d.presence_next == 0);
    assert(d.presence_sent_at[0] == 0 && d.pending_presence_len == 0);
    connect_script = 0;
    discord_clear_credentials(&d);
}

static void test_auth_heartbeat_request_is_answered_before_ready(void){
    discord_t d;
    connect_script = 1;
    auth_step = 0;
    auth_heartbeat_script = 1;
    sent = 0;
    assert(discord_connect(&d, "test-token", NULL) == 0);
    assert(sent == 2); /* IDENTIFY and the requested heartbeat */
    assert(d.sent_hb && d.last_heartbeat == fake_now);
    auth_heartbeat_script = 0;
    connect_script = 0;
    discord_clear_credentials(&d);
}

static void test_connect_rejects_credentials_that_do_not_fit(void){
    discord_t d;
    char long_token[513], long_qos[513];
    memset(long_token, 'x', sizeof long_token - 1);
    long_token[sizeof long_token - 1] = 0;
    memset(long_qos, 'y', sizeof long_qos - 1);
    long_qos[sizeof long_qos - 1] = 0;
    connect_attempts = 0;
    assert(discord_connect(&d, long_token, NULL) == -1);
    assert(!d.token[0] && connect_attempts == 0);
    assert(discord_connect(&d, "short", long_qos) == -1);
    assert(!d.token[0] && !d.qos_token[0] && connect_attempts == 0);
}

static void test_unrelated_dispatch_does_not_ack_heartbeat(void){
    static const char dispatch[] = "{\"op\":0,\"s\":7,\"t\":\"PRESENCE_UPDATE\"}";
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 5000;
    d.last_heartbeat = fake_now;
    d.last_ack = fake_now;
    d.sent_hb = 1;
    scripted_recv = 1;
    scripted_result = (int)sizeof dispatch - 1;
    scripted_opcode = 1;
    scripted_fin = 1;
    scripted_payload = dispatch;
    assert(discord_tick(&d) == 0);
    assert(d.last_ack == fake_now);

    fake_now += 26; /* > 2 heartbeat intervals + 15 second grace */
    d.last_heartbeat = fake_now;
    assert(discord_tick(&d) == -1);
    assert(!d.connected);
}

static void test_server_heartbeat_request_is_answered_immediately(void){
    static const char request[] = "{\"op\":1,\"d\":null}";
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 45000;
    d.last_heartbeat = fake_now;
    d.last_ack = fake_now;
    d.sent_hb = 1;
    scripted_recv = 1;
    scripted_result = (int)sizeof request - 1;
    scripted_opcode = 1;
    scripted_fin = 1;
    scripted_payload = request;
    sent = 0;
    assert(discord_tick(&d) == 0);
    assert(sent == 1);
    assert(d.last_heartbeat == fake_now);
}

static void test_out_of_range_hello_heartbeat_interval_uses_default(void){
    static const char hello[] =
        "{\"op\":10,\"d\":{\"heartbeat_interval\":1e300}}";
    discord_t d;
    connect_script = 1;
    auth_step = 0;
    script_hello = hello;
    assert(discord_connect(&d, "test-token", NULL) == 0);
    assert(d.hb_interval_ms == 45000);
    script_hello = NULL;
    connect_script = 0;
    discord_clear_credentials(&d);
}

static void test_empty_close_frame_disconnects_immediately(void){
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 5000;
    d.last_heartbeat = fake_now;
    d.last_ack = fake_now;
    d.sent_hb = 1;
    scripted_recv = 1;
    scripted_result = 0;
    scripted_opcode = 8;
    scripted_fin = 1;
    scripted_payload = NULL;
    assert(discord_tick(&d) == -1);
    assert(!d.connected);
}

static void test_invalid_close_payload_disconnects(void){
    static const char reserved_code[] = { 0x03, (char)0xED };
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 5000;
    d.last_heartbeat = fake_now;
    d.last_ack = fake_now;
    d.sent_hb = 1;
    scripted_recv = 1;
    scripted_result = (int)sizeof reserved_code;
    scripted_opcode = 8;
    scripted_fin = 1;
    scripted_payload = reserved_code;
    assert(discord_tick(&d) == -1);
    assert(!d.connected);
}

static void test_empty_ping_is_answered(void){
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 5000;
    d.last_heartbeat = fake_now;
    d.last_ack = fake_now;
    d.sent_hb = 1;
    scripted_recv = 1;
    scripted_result = 0;
    scripted_opcode = 9;
    scripted_fin = 1;
    scripted_payload = NULL;
    pongs = 0;
    assert(discord_tick(&d) == 0);
    assert(!scripted_recv);
    assert(pongs == 1);
    assert(d.connected);
}

static void test_ping_payload_reaches_pong(void){
    discord_t d;
    memset(&d, 0, sizeof d);
    d.connected = 1;
    d.hb_interval_ms = 5000;
    d.last_heartbeat = fake_now;
    d.last_ack = fake_now;
    d.sent_hb = 1;
    scripted_recv = 1;
    scripted_result = 5;
    scripted_opcode = 9;
    scripted_fin = 1;
    scripted_payload = "probe";
    pongs = 0;
    last_pong_len = 0;
    assert(discord_tick(&d) == 0);
    assert(pongs == 1 && last_pong_len == 5);
    assert(!memcmp(last_pong, "probe", 5));
}

static void test_fragment_reassembly(void){
    ws_frag_t s = {0};
    const unsigned char *message = NULL;
    size_t message_len = 0;
    assert(ws_frag_feed(&s, 0, 1, (const unsigned char *)"x", 1,
                        &message, &message_len) == -1);
    assert(ws_frag_feed(&s, 1, 0, (const unsigned char *)"{\"op\":", 6,
                        &message, &message_len) == 0);
    assert(ws_frag_feed(&s, 0, 0, (const unsigned char *)"0,", 2,
                        &message, &message_len) == 0);
    assert(ws_frag_feed(&s, 0, 1, (const unsigned char *)"\"t\":\"READY\"}", 12,
                        &message, &message_len) == 1);
    assert(message_len == 20);
    assert(!memcmp(message, "{\"op\":0,\"t\":\"READY\"}", 20));
    ws_frag_reset(&s);

    size_t too_big = WS_BODY_MAX + 1;
    unsigned char *large = malloc(too_big);
    assert(large);
    memset(large, 'x', too_big);
    assert(ws_frag_feed(&s, 1, 0, large, too_big, &message, &message_len) == 0);
    assert(s.discarding && s.active && !s.data);
    assert(ws_frag_feed(&s, 0, 1, (const unsigned char *)"tail", 4,
                        &message, &message_len) == -2);
    assert(!s.active && !s.discarding);
    free(large);

    assert(ws_frag_feed(&s, 1, 0, (const unsigned char *)"part", 4,
                        &message, &message_len) == 0);
    assert(ws_frag_feed(&s, 1, 1, (const unsigned char *)"new", 3,
                        &message, &message_len) == -1);
    ws_frag_reset(&s);
}

static void test_websocket_frame_headers(void){
    const unsigned char text[] = {0x81, 0x00};
    const unsigned char reserved_control[] = {0x8b, 0x00};
    const unsigned char reserved_control2[] = {0x8c, 0x00};
    const unsigned char reserved_control3[] = {0x8f, 0x00};
    const unsigned char reserved_data[] = {0x83, 0x00};
    const unsigned char fragmented_ping[] = {0x09, 0x00};
    const unsigned char oversized_ping[] = {0x89, 0x7e};
    const unsigned char reserved_bits[] = {0xc1, 0x00};
    const unsigned char masked_server_frame[] = {0x81, 0x80};
    const unsigned char overflowing_length[] = {
        0x81, 0x7f, 0x80, 0, 0, 0, 0, 0, 0, 0
    };
    assert(ws_frame_header_valid(text, 0));
    assert(!ws_frame_header_valid(reserved_control, 0));
    assert(!ws_frame_header_valid(reserved_control2, 0));
    assert(!ws_frame_header_valid(reserved_control3, 0));
    assert(!ws_frame_header_valid(reserved_data, 0));
    assert(!ws_frame_header_valid(fragmented_ping, 0));
    assert(!ws_frame_header_valid(oversized_ping, 126));
    assert(!ws_frame_header_valid(reserved_bits, 0));
    assert(!ws_frame_header_valid(masked_server_frame, 0));
    assert(!ws_frame_header_valid(overflowing_length, UINT64_MAX));
}

static void test_masked_control_frame_echo_encoding(void){
    const unsigned char payload[] = "probe";
    const unsigned char mask[] = {0x12, 0x34, 0x56, 0x78};
    unsigned char frame[131];
    size_t n = ws_control_frame_encode(0x0a, payload, sizeof payload - 1,
                                       mask, frame, sizeof frame);
    assert(n == 11 && frame[0] == 0x8a && frame[1] == 0x85);
    assert(!memcmp(frame + 2, mask, sizeof mask));
    for(size_t i = 0; i < sizeof payload - 1; ++i)
        assert((unsigned char)(frame[6 + i] ^ mask[i % 4]) == payload[i]);
    assert(ws_control_frame_encode(0x0a, payload, 126, mask,
                                   frame, sizeof frame) == 0);
    assert(ws_control_frame_encode(0x0b, payload, sizeof payload - 1, mask,
                                   frame, sizeof frame) == 0);
}

static void test_close_payload_validation(void){
    const unsigned char valid[] = { 0x03, 0xE8, 'o', 'k' };
    const unsigned char rotated[] = { 0x0F, 0xA4 };
    const unsigned char one_byte[] = { 0x03 };
    const unsigned char zero_code[] = { 0x00, 0x00 };
    const unsigned char reserved[] = { 0x03, 0xED };
    const unsigned char invalid_utf8[] = { 0x03, 0xE8, 0xC0, 0xAF };
    unsigned code = 123;
    assert(ws_close_payload_valid(NULL, 0, &code) && code == 0);
    assert(ws_close_payload_valid(valid, sizeof valid, &code) && code == 1000);
    assert(ws_close_payload_valid(rotated, sizeof rotated, &code) && code == 4004);
    assert(!ws_close_payload_valid(one_byte, sizeof one_byte, &code));
    assert(!ws_close_payload_valid(zero_code, sizeof zero_code, &code));
    assert(!ws_close_payload_valid(reserved, sizeof reserved, &code));
    assert(!ws_close_payload_valid(invalid_utf8, sizeof invalid_utf8, &code));
    unsigned char oversized[126] = { 0x03, 0xE8 };
    assert(!ws_close_payload_valid(oversized, sizeof oversized, &code));
}

static void test_bounded_large_envelope(void){
    char payload[4096];
    strcpy(payload, "{\"op\":0,\"s\":41,\"t\":\"READY\",\"d\":\"");
    size_t used = strlen(payload);
    memset(payload + used, 'x', 3000);
    used += 3000;
    strcpy(payload + used, "\"}");

    ws_env_t env;
    ws_env_init(&env);
    ws_env_feed(&env, (const unsigned char *)payload, strlen(payload));
    assert(ws_env_is_ready(&env));
    char envelope[128];
    size_t n = ws_env_render(&env, envelope, sizeof envelope);
    assert(n && !strcmp(envelope, "{\"op\":0,\"s\":41,\"t\":\"READY\"}"));
}

static void test_token_fields_require_ready_envelope(void){
    ws_env_t env;
    ws_env_init(&env);
    const char *not_ready = "{\"op\":0,\"t\":\"MESSAGE_CREATE\",\"d\":{"
                            "\"auth_token\":\"should-not-rotate\"}}";
    ws_env_feed(&env, (const unsigned char *)not_ready, strlen(not_ready));
    assert(env.have_auth_token);
    assert(!ws_env_is_ready(&env));
    char out[64] = {0};
    assert(ws_env_take_auth_token(&env, out, sizeof out) == 0);
    assert(out[0] == 0);

    ws_env_init(&env);
    const char *ready = "{\"op\":0,\"t\":\"READY\",\"d\":{"
                        "\"meta\":{\"auth_token\":\"nested-decoy\"},"
                        "\"auth_token\":\"rotated-A\",\"qos_token\":\"signed-qos\"}}";
    ws_env_feed(&env, (const unsigned char *)ready, strlen(ready));
    assert(ws_env_take_auth_token(&env, out, 4) == -1); /* retain on short buffer */
    assert(env.have_auth_token);
    assert(ws_env_take_auth_token(&env, out, sizeof out) == 1);
    assert(!strcmp(out, "rotated-A"));
    memset(out, 0, sizeof out);
    assert(ws_env_take_qos_token(&env, out, sizeof out) == 1);
    assert(!strcmp(out, "signed-qos"));
    assert(ws_env_take_qos_token(&env, out, sizeof out) == 0);
}

static void test_split_ready_rotation_with_decoys(void){
    ws_env_t env;
    ws_env_init(&env);
    const char *a = "{\"op\":0,\"t\":\"READY\",\"d\":{\"note\":\"auth_token decoy\",";
    const char *b = "\"auth_token\":\"rotated-";
    const char *c = "C\",\"qos_token\":\"signed-";
    const char *d = "qos-C\"}}";
    ws_env_feed(&env, (const unsigned char *)a, strlen(a));
    ws_env_feed(&env, (const unsigned char *)b, strlen(b));
    assert(!env.have_auth_token); /* incomplete value is not promoted */
    ws_env_feed(&env, (const unsigned char *)c, strlen(c));
    ws_env_feed(&env, (const unsigned char *)d, strlen(d));
    char out[64] = {0};
    assert(ws_env_take_auth_token(&env, out, sizeof out) == 1);
    assert(!strcmp(out, "rotated-C"));
    memset(out, 0, sizeof out);
    assert(ws_env_take_qos_token(&env, out, sizeof out) == 1);
    assert(!strcmp(out, "signed-qos-C"));
}

int main(void){
    test_malformed_numbers();
    test_identify_echoes_qos_only_when_supplied();
    test_rotation_take_validation();
    test_credentials_clear_after_session();
    test_first_heartbeat_ack_window();
    test_connect_resets_session_state_before_identify();
    test_auth_heartbeat_request_is_answered_before_ready();
    test_connect_rejects_credentials_that_do_not_fit();
    test_unrelated_dispatch_does_not_ack_heartbeat();
    test_server_heartbeat_request_is_answered_immediately();
    test_out_of_range_hello_heartbeat_interval_uses_default();
    test_empty_close_frame_disconnects_immediately();
    test_invalid_close_payload_disconnects();
    test_empty_ping_is_answered();
    test_ping_payload_reaches_pong();
    test_fragment_reassembly();
    test_websocket_frame_headers();
    test_masked_control_frame_echo_encoding();
    test_close_payload_validation();
    test_bounded_large_envelope();
    test_token_fields_require_ready_envelope();
    test_split_ready_rotation_with_decoys();
    return 0;
}
