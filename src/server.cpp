/*
 * sip-lab-server.cpp
 *
 * Standalone server wrapping the pjw_* SIP engine.
 * Communicates with the Node.js parent over a TCP loopback socket.
 *
 * Protocol (newline-delimited JSON over TCP):
 *
 * Client -> Server (command):
 *   {"seq":<int>, "cmd":"<name>", ...args...}\n
 *
 * Server -> Client (response to command):
 *   {"seq":<int>, "result":<value>}\n          on success
 *   {"seq":<int>, "error":"<message>"}\n        on failure
 *
 * Server -> Client (async event, seq absent or -1):
 *   {"type":"<event_type>", ...fields...}\n
 *
 * The server polls pjw every 50 ms and forwards any pending events.
 * Commands are processed synchronously in the read loop (single client).
 */

#include "sip.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>

/* ------------------------------------------------------------------ */
/* Minimal JSON helpers (no external dep beyond what's already in the  */
/* project — rapidjson is available but we keep the server self-        */
/* contained with simple sprintf-based building for responses, and we  */
/* use rapidjson only for parsing incoming commands).                  */
/* ------------------------------------------------------------------ */
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace rapidjson;
using namespace std;

/* ------------------------------------------------------------------ */
/* Write a complete line (JSON + newline) to fd.                       */
/* ------------------------------------------------------------------ */
static void send_line(int fd, const char *buf) {
    size_t len = strlen(buf);
    /* write json */
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) return;
        written += n;
    }
    /* write newline */
    write(fd, "\n", 1);
}

/* ------------------------------------------------------------------ */
/* Build and send a simple success response (null result).             */
/* ------------------------------------------------------------------ */
static void send_ok(int fd, int seq) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"seq\":%d,\"result\":null}", seq);
    send_line(fd, buf);
}

/* ------------------------------------------------------------------ */
/* Build and send an error response.                                   */
/* ------------------------------------------------------------------ */
static void send_error(int fd, int seq, const char *msg) {
    /* Escape the message minimally */
    StringBuffer sb;
    Writer<StringBuffer> w(sb);
    w.String(msg);
    char buf[4096];
    snprintf(buf, sizeof(buf), "{\"seq\":%d,\"error\":%s}", seq, sb.GetString());
    send_line(fd, buf);
}

/* ------------------------------------------------------------------ */
/* Build and send a response with an integer result.                   */
/* ------------------------------------------------------------------ */
static void send_int(int fd, int seq, long val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"seq\":%d,\"result\":%ld}", seq, val);
    send_line(fd, buf);
}

/* ------------------------------------------------------------------ */
/* Build and send a response with a string result.                     */
/* ------------------------------------------------------------------ */
static void send_string(int fd, int seq, const char *val) {
    StringBuffer sb;
    Writer<StringBuffer> w(sb);
    w.String(val);
    char buf[8192];
    snprintf(buf, sizeof(buf), "{\"seq\":%d,\"result\":%s}", seq, sb.GetString());
    send_line(fd, buf);
}

/* ------------------------------------------------------------------ */
/* Require a member from the document, send error and return false     */
/* if missing.                                                         */
/* ------------------------------------------------------------------ */
#define REQUIRE_INT(doc, field, varname)                                   \
    if (!(doc).HasMember(field) || !(doc)[field].IsInt()) {                \
        send_error(client_fd, seq, "missing or invalid field: " field);    \
        return;                                                            \
    }                                                                      \
    int varname = (doc)[field].GetInt();

#define REQUIRE_STRING(doc, field, varname)                                \
    if (!(doc).HasMember(field) || !(doc)[field].IsString()) {             \
        send_error(client_fd, seq, "missing or invalid field: " field);    \
        return;                                                            \
    }                                                                      \
    const char *varname = (doc)[field].GetString();

/* ------------------------------------------------------------------ */
/* Dispatch a single parsed command document.                          */
/* ------------------------------------------------------------------ */
static void dispatch(int client_fd, const Document &doc) {
    int seq = -1;
    if (doc.HasMember("seq") && doc["seq"].IsInt()) {
        seq = doc["seq"].GetInt();
    }

    if (!doc.HasMember("cmd") || !doc["cmd"].IsString()) {
        send_error(client_fd, seq, "missing 'cmd' field");
        return;
    }
    const char *cmd = doc["cmd"].GetString();

    /* ---- transport_create ---------------------------------------- */
    if (strcmp(cmd, "transport_create") == 0) {
        REQUIRE_STRING(doc, "params", params_json)
        int out_t_id;
        char out_t_address[256];
        int out_t_port;
        int res = pjw_transport_create(params_json, &out_t_id, out_t_address, &out_t_port);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        char buf[512];
        StringBuffer sb; Writer<StringBuffer> w(sb); w.String(out_t_address);
        snprintf(buf, sizeof(buf),
            "{\"seq\":%d,\"result\":{\"id\":%d,\"address\":%s,\"port\":%d}}",
            seq, out_t_id, sb.GetString(), out_t_port);
        send_line(client_fd, buf);
        return;
    }

    /* ---- account_create ------------------------------------------ */
    if (strcmp(cmd, "account_create") == 0) {
        REQUIRE_INT(doc, "transport_id", transport_id)
        REQUIRE_STRING(doc, "params", params_json)
        int out_a_id;
        int res = pjw_account_create(transport_id, params_json, &out_a_id);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_int(client_fd, seq, out_a_id);
        return;
    }

    /* ---- account_register ---------------------------------------- */
    if (strcmp(cmd, "account_register") == 0) {
        REQUIRE_INT(doc, "account_id", account_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_account_register(account_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- account_unregister -------------------------------------- */
    if (strcmp(cmd, "account_unregister") == 0) {
        REQUIRE_INT(doc, "account_id", account_id)
        int res = pjw_account_unregister(account_id);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- request_create ------------------------------------------ */
    if (strcmp(cmd, "request_create") == 0) {
        REQUIRE_INT(doc, "transport_id", transport_id)
        REQUIRE_STRING(doc, "params", params_json)
        long out_request_id;
        char out_sip_call_id[256];
        int res = pjw_request_create(transport_id, params_json, &out_request_id, out_sip_call_id);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        StringBuffer sb; Writer<StringBuffer> w(sb); w.String(out_sip_call_id);
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"seq\":%d,\"result\":{\"id\":%ld,\"sip_call_id\":%s}}",
            seq, out_request_id, sb.GetString());
        send_line(client_fd, buf);
        return;
    }

    /* ---- request_respond ----------------------------------------- */
    if (strcmp(cmd, "request_respond") == 0) {
        REQUIRE_INT(doc, "request_id", request_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_request_respond(request_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_create --------------------------------------------- */
    if (strcmp(cmd, "call_create") == 0) {
        REQUIRE_INT(doc, "transport_id", transport_id)
        REQUIRE_STRING(doc, "params", params_json)
        long out_call_id;
        char out_sip_call_id[256];
        int res = pjw_call_create(transport_id, params_json, &out_call_id, out_sip_call_id);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        StringBuffer sb; Writer<StringBuffer> w(sb); w.String(out_sip_call_id);
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"seq\":%d,\"result\":{\"id\":%ld,\"sip_call_id\":%s}}",
            seq, out_call_id, sb.GetString());
        send_line(client_fd, buf);
        return;
    }

    /* ---- call_respond -------------------------------------------- */
    if (strcmp(cmd, "call_respond") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_respond(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_terminate ------------------------------------------ */
    if (strcmp(cmd, "call_terminate") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_terminate(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_send_dtmf ------------------------------------------ */
    if (strcmp(cmd, "call_send_dtmf") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_send_dtmf(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_send_bfsk ------------------------------------------ */
    if (strcmp(cmd, "call_send_bfsk") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_send_bfsk(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_reinvite ------------------------------------------- */
    if (strcmp(cmd, "call_reinvite") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_reinvite(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_update --------------------------------------------- */
    if (strcmp(cmd, "call_update") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_update(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_send_request --------------------------------------- */
    if (strcmp(cmd, "call_send_request") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_send_request(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_record_wav ----------------------------------- */
    if (strcmp(cmd, "call_start_record_wav") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_record_wav(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_play_wav ------------------------------------- */
    if (strcmp(cmd, "call_start_play_wav") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_play_wav(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_record_wav ------------------------------------ */
    if (strcmp(cmd, "call_stop_record_wav") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_record_wav(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_play_wav -------------------------------------- */
    if (strcmp(cmd, "call_stop_play_wav") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_play_wav(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_fax ------------------------------------------ */
    if (strcmp(cmd, "call_start_fax") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_fax(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_fax ------------------------------------------- */
    if (strcmp(cmd, "call_stop_fax") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_fax(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_speech_synth --------------------------------- */
    if (strcmp(cmd, "call_start_speech_synth") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_speech_synth(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_speech_synth ---------------------------------- */
    if (strcmp(cmd, "call_stop_speech_synth") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_speech_synth(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_speech_recog --------------------------------- */
    if (strcmp(cmd, "call_start_speech_recog") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_speech_recog(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_speech_recog ---------------------------------- */
    if (strcmp(cmd, "call_stop_speech_recog") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_speech_recog(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_inband_dtmf_detection ------------------------ */
    if (strcmp(cmd, "call_start_inband_dtmf_detection") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_inband_dtmf_detection(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_inband_dtmf_detection ------------------------- */
    if (strcmp(cmd, "call_stop_inband_dtmf_detection") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_inband_dtmf_detection(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_start_bfsk_detection ------------------------------- */
    if (strcmp(cmd, "call_start_bfsk_detection") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_start_bfsk_detection(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_stop_bfsk_detection -------------------------------- */
    if (strcmp(cmd, "call_stop_bfsk_detection") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_stop_bfsk_detection(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- call_get_stream_stat ------------------------------------ */
    if (strcmp(cmd, "call_get_stream_stat") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        char out_stats[4096];
        int res = pjw_call_get_stream_stat(call_id, params_json, out_stats);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_string(client_fd, seq, out_stats);
        return;
    }

    /* ---- call_get_info ------------------------------------------- */
    if (strcmp(cmd, "call_get_info") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "required_info", required_info)
        char out_info[4096];
        int res = pjw_call_get_info(call_id, required_info, out_info);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_string(client_fd, seq, out_info);
        return;
    }

    /* ---- call_gen_string_replaces -------------------------------- */
    if (strcmp(cmd, "call_gen_string_replaces") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        char out_replaces[4096];
        int res = pjw_call_gen_string_replaces(call_id, out_replaces);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_string(client_fd, seq, out_replaces);
        return;
    }

    /* ---- call_send_tcp_msg (MRCP) -------------------------------- */
    if (strcmp(cmd, "call_send_tcp_msg") == 0) {
        REQUIRE_INT(doc, "call_id", call_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_call_send_tcp_msg(call_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- dtmf_aggregation_on ------------------------------------- */
    if (strcmp(cmd, "dtmf_aggregation_on") == 0) {
        REQUIRE_INT(doc, "inter_digit_timer", inter_digit_timer)
        int res = pjw_dtmf_aggregation_on(inter_digit_timer);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- dtmf_aggregation_off ------------------------------------ */
    if (strcmp(cmd, "dtmf_aggregation_off") == 0) {
        int res = pjw_dtmf_aggregation_off();
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- get_codecs ---------------------------------------------- */
    if (strcmp(cmd, "get_codecs") == 0) {
        char out_codecs[4096];
        int res = pjw_get_codecs(out_codecs);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_string(client_fd, seq, out_codecs);
        return;
    }

    /* ---- set_codecs ---------------------------------------------- */
    if (strcmp(cmd, "set_codecs") == 0) {
        REQUIRE_STRING(doc, "codec_info", codec_info)
        int res = pjw_set_codecs(codec_info);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- set_opus_config ----------------------------------------- */
    if (strcmp(cmd, "set_opus_config") == 0) {
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_set_opus_config(params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- notify -------------------------------------------------- */
    if (strcmp(cmd, "notify") == 0) {
        REQUIRE_INT(doc, "subscriber_id", subscriber_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_notify(subscriber_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- notify_xfer --------------------------------------------- */
    if (strcmp(cmd, "notify_xfer") == 0) {
        REQUIRE_INT(doc, "subscriber_id", subscriber_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_notify_xfer(subscriber_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- register_pkg -------------------------------------------- */
    if (strcmp(cmd, "register_pkg") == 0) {
        REQUIRE_STRING(doc, "event", event_str)
        REQUIRE_STRING(doc, "accept", accept_str)
        int res = pjw_register_pkg(event_str, accept_str);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- subscription_create ------------------------------------- */
    if (strcmp(cmd, "subscription_create") == 0) {
        REQUIRE_INT(doc, "transport_id", transport_id)
        REQUIRE_STRING(doc, "params", params_json)
        long out_subscription_id;
        int res = pjw_subscription_create(transport_id, params_json, &out_subscription_id);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_int(client_fd, seq, out_subscription_id);
        return;
    }

    /* ---- subscription_subscribe ---------------------------------- */
    if (strcmp(cmd, "subscription_subscribe") == 0) {
        REQUIRE_INT(doc, "subscription_id", subscription_id)
        REQUIRE_STRING(doc, "params", params_json)
        int res = pjw_subscription_subscribe(subscription_id, params_json);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_ok(client_fd, seq);
        return;
    }

    /* ---- set_log_level ------------------------------------------- */
    if (strcmp(cmd, "set_log_level") == 0) {
        REQUIRE_INT(doc, "log_level", log_level)
        int res = pjw_log_level(log_level);
        if (res != 0) { send_error(client_fd, seq, pjw_get_error()); return; }
        send_int(client_fd, seq, 0);
        return;
    }

    /* ---- set_flags ----------------------------------------------- */
    if (strcmp(cmd, "set_flags") == 0) {
        if (!doc.HasMember("flags") || !doc["flags"].IsUint()) {
            send_error(client_fd, seq, "missing or invalid field: flags");
            return;
        }
        unsigned flags = doc["flags"].GetUint();
        pjw_set_flags(flags);
        send_int(client_fd, seq, 0);
        return;
    }

    /* ---- enable_telephone_event ---------------------------------- */
    if (strcmp(cmd, "enable_telephone_event") == 0) {
        pjw_enable_telephone_event();
        send_int(client_fd, seq, 0);
        return;
    }

    /* ---- disable_telephone_event --------------------------------- */
    if (strcmp(cmd, "disable_telephone_event") == 0) {
        pjw_disable_telephone_event();
        send_int(client_fd, seq, 0);
        return;
    }

    /* ---- shutdown ------------------------------------------------ */
    if (strcmp(cmd, "shutdown") == 0) {
        int clean_up = 0;
        if (doc.HasMember("clean_up") && doc["clean_up"].IsInt()) {
            clean_up = doc["clean_up"].GetInt();
        }
        __pjw_shutdown(clean_up);
        send_ok(client_fd, seq);
        return;
    }

    /* Unknown command */
    char errbuf[256];
    snprintf(errbuf, sizeof(errbuf), "unknown command: %s", cmd);
    send_error(client_fd, seq, errbuf);
}

/* ------------------------------------------------------------------ */
/* serve_client: read/dispatch loop for one connected client.          */
/* Uses select() with a 50 ms timeout to interleave reading commands   */
/* with polling pjw for async events.                                  */
/* ------------------------------------------------------------------ */
static void serve_client(int client_fd) {
    /* Line-buffer for incoming data */
    static char recv_buf[65536];
    int recv_len = 0;

    /* Event poll buffer */
    static char evt_buf[4096];

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50 * 1000; /* 50 ms */

        int ret = select(client_fd + 1, &rfds, NULL, NULL, &tv);

        /* Drain ALL pending pjw events before checking client data */
        while (__pjw_poll(evt_buf) == 0) {
            /* evt_buf may contain "json\nmsg" — if so, parse the JSON part,
             * inject msg as a field, and send the merged object.
             * This matches what the original index.js poll handler did. */
            char *nl = strchr(evt_buf, '\n');
            if (nl != NULL) {
                *nl = '\0';
                const char *msg_part = nl + 1;

                Document evtDoc;
                evtDoc.Parse(evt_buf);
                if (!evtDoc.HasParseError() && evtDoc.IsObject() && msg_part[0] != '\0') {
                    evtDoc.AddMember(
                        rapidjson::Value("msg", evtDoc.GetAllocator()),
                        rapidjson::Value(msg_part, evtDoc.GetAllocator()),
                        evtDoc.GetAllocator()
                    );
                    StringBuffer sb;
                    Writer<StringBuffer> w(sb);
                    evtDoc.Accept(w);
                    send_line(client_fd, sb.GetString());
                } else {
                    /* Restore and forward as-is */
                    *nl = '\n';
                    send_line(client_fd, evt_buf);
                }
            } else {
                send_line(client_fd, evt_buf);
            }
        }

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) {
            /* timeout, already polled above */
            continue;
        }

        /* Data available on the socket */
        int space = (int)sizeof(recv_buf) - recv_len - 1;
        if (space <= 0) {
            /* Buffer full without a newline — shouldn't happen in practice */
            recv_len = 0;
            continue;
        }

        ssize_t n = read(client_fd, recv_buf + recv_len, space);
        if (n <= 0) {
            /* Client disconnected */
            break;
        }
        recv_len += (int)n;
        recv_buf[recv_len] = '\0';

        /* Process all complete lines in the buffer */
        char *start = recv_buf;
        char *nl;
        while ((nl = (char *)memchr(start, '\n', recv_buf + recv_len - start)) != NULL) {
            *nl = '\0';
            /* Parse and dispatch */
            Document doc;
            doc.ParseInsitu(start);
            if (!doc.HasParseError()) {
                dispatch(client_fd, doc);
            }
            start = nl + 1;
        }

        /* Move remaining partial line to front of buffer */
        int remaining = (int)(recv_buf + recv_len - start);
        if (remaining > 0 && start != recv_buf) {
            memmove(recv_buf, start, remaining);
        }
        recv_len = remaining;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    int port = 0; /* 0 = let OS assign a free port */

    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    /* Initialise the SIP engine */
    int init_res = __pjw_init();
    if (init_res != 0) {
        fprintf(stderr, "sip-lab-server: __pjw_init failed (%d)\n", init_res);
        return 1;
    }

    /* Create listening TCP socket on loopback */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("sip-lab-server: socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 only */
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("sip-lab-server: bind");
        return 1;
    }

    if (listen(listen_fd, 1) < 0) {
        perror("sip-lab-server: listen");
        return 1;
    }

    /* Discover the actual bound port */
    socklen_t addrlen = sizeof(addr);
    getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen);
    int actual_port = ntohs(addr.sin_port);

    /* Print the port on stderr so the Node.js parent can connect. */
    fprintf(stderr, "READY %d\n", actual_port);
    fflush(stderr);

    /* Accept exactly one client (Node.js process) and serve it */
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("sip-lab-server: accept");
        return 1;
    }
    opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    close(listen_fd);

    serve_client(client_fd);

    close(client_fd);
    return 0;
}
