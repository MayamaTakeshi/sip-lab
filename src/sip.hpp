#ifndef __SIP_HPP__
#define __SIP_HPP__

#include <pjlib-util.h>
#include <pjlib.h>
#include <pjmedia-codec.h>
#include <pjmedia.h>
#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjsip_ua.h>

#define FLAG_NO_AUTO_100_TRYING 1

#define CALL_FLAG_DELAYED_MEDIA 1

int __pjw_init();
int __pjw_poll(char *out_evt);

int pjw_transport_create(const char *json, int *out_t_id, char *out_t_address,
                         int *out_port);

int pjw_transport_get_info(int t_id, char **out_sip_ipaddr, int *out_port);

int pjw_account_create(int t_id, const char *json, int *out_acc_id);

int pjw_account_register(long acc_id, const char *json);

int pjw_account_unregister(long acc_id);

int pjw_request_create(long t_id, const char *json, long *out_request_id,
                       char *out_sip_call_id);

int pjw_request_respond(long request_id, const char *json);

int pjw_call_create(long t_id, const char *json, long *out_call_id,
                    char *out_sip_call_id);

int pjw_call_respond(long call_id, const char *json);

int pjw_call_terminate(long call_id, const char *json);

int pjw_call_send_dtmf(long call_id, const char *json);

int pjw_call_reinvite(long call_id, const char *json);

int pjw_call_send_request(long call_id, const char *json);

int pjw_call_start_record_wav(long call_id, const char *file);

int pjw_call_start_play_wav(long call_id, const char *file);

int pjw_call_stop_play_wav(long call_id, const char *json);

int pjw_call_stop_record_wav(long call_id, const char *json);

int pjw_call_start_fax(long call_id, const char *json);

int pjw_call_stop_fax(long call_id, const char *json);

int pjw_call_start_speech(long call_id, const char *json);

int pjw_call_get_stream_stat(long call_id, const char *json, char *out_stats);

// int pjw_call_refer(long call_id, const char *json, long
// *out_subscription_id);

int pjw_call_get_info(long call_id, const char *required_info, char *out_info);

int pjw_call_gen_string_replaces(long call_id, char *out_replaces);

int pjw_call_send_tcp_msg(long call_id, const char *json);

int pjw_get_codecs(char *out_codecs);

int pjw_set_codecs(const char *in_codec_info);

int __pjw_shutdown();

int pjw_notify(long subscriber_id, const char *json);

int pjw_notify_xfer(long subscriber_id, const char *json);

int pjw_register_pkg(const char *event, const char *accept);

int pjw_subscription_create(long transport_id, const char *json,
                            long *out_subscription_id);

int pjw_subscription_subscribe(long subscription_id, const char *json);

int pjw_log_level(long log_level);

int pjw_set_flags(unsigned flags);

int pjw_dtmf_aggregation_on(int inter_digit_timer);
int pjw_dtmf_aggregation_off();

int pjw_enable_telephone_event();
int pjw_disable_telephone_event();

char *pjw_get_error();
#endif
