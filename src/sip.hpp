#ifndef __SIP_HPP__
#define __SIP_HPP__

#include <pjsip.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjsip_ua.h>
#include <pjsip_simple.h>
#include <pjlib-util.h>
#include <pjlib.h>

#define FLAG_NO_AUTO_100_TRYING 1

#define CALL_CREATE_FLAG_LATE_NEGOTIATION 1


int __pjw_init();
int __pjw_poll(char *out_evt);
int pjw_transport_create(const char *sip_ipaddr, int port, pjsip_transport_type_e type, int *out_t_id, int *out_port);
int pjw_transport_get_info(int t_id, char *out_sip_ipaddr, int *out_port);

int pjw_account_create(int t_id, const char *domain, const char *server, const char *username, const char *password, const char *additional_headers, const char *c_to_url, int expires, int *out_acc_id);
int pjw_account_register(long acc_id, pj_bool_t autoreg);
int pjw_account_unregister(long acc_id);

int pjw_call_create(long t_id, unsigned flags, const char *from_uri, const char *to_uri, const char *request_uri, const char *proxy_uri, const char *additional_headers, const char *realm, const char *username, const char *password, long *out_call_id, char *out_sip_call_id);
int pjw_call_respond(long call_id, int code, const char *reason, const char *additional_headers);
int pjw_call_terminate(long call_id, int code, const char *reason, const char *additional_headers);
int pjw_call_send_dtmf(long call_id, const char *digits, int mode);
int pjw_call_reinvite(long call_id, int hold);
int pjw_call_send_request(long call_id, const char *method_name, const char *additional_headers, const char *body, const char *ct_type, const char *ct_subtype);
int pjw_call_start_record_wav(long call_id, const char *file);
int pjw_call_start_play_wav(long call_id, const char *file);
int pjw_call_stop_play_wav(long call_id);
int pjw_call_stop_record_wav(long call_id);
int pjw_call_get_stream_stat(long call_id, char *out_stats);
int pjw_call_refer(long call_id, const char *dest_uri, const char *additional_headers, long *out_subscription_id);
int pjw_call_get_info(long call_id, const char *required_info, char *out_info);
int pjw_call_gen_string_replaces(long call_id, char *out_replaces);

int pjw_packetdump_start(const char *dev, const char *file);
int pjw_packetdump_stop();

int pjw_get_codecs(char *out_codecs);
int pjw_set_codecs(const char *in_codec_info);
int __pjw_shutdown();

int pjw_notify(long subscriber_id, const char *content_type, const char *body, int subscription_state, const char *reason, const char *additional_headers);
int pjw_notify_xfer(long subscriber_id, int subscription_state, int xfer_status_code, const char *xfer_status_text);

int pjw_register_pkg(const char *event, const char *accept);

int pjw_subscription_create(long transport_id, const char *event, const char *accept, const char *from_uri, const char *to_uri, const char *request_uri, const char *proxy_uri, const char *realm, const char *username, const char *password, long *out_subscription_id);
int pjw_subscription_subscribe(long subscription_id, int expires, const char *additional_headers);

int pjw_log_level(long log_level);

int pjw_set_flags(unsigned flags);

int pjw_dtmf_aggregation_on(int inter_digit_timer);
int pjw_dtmf_aggregation_off();

char *pjw_get_error();
#endif
