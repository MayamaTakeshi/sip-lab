use std::os::raw::{c_char, c_int, c_long};

extern "C" {
    pub fn handle_events();
    pub fn __pjw_init() -> c_int;
    pub fn __pjw_poll(out_evt: *mut c_char) -> c_int;

    pub fn pjw_transport_create(
        json: *const c_char,
        out_t_id: *mut c_int,
        out_t_address: *mut c_char,
        out_port: *mut c_int,
    ) -> c_int;

    pub fn pjw_transport_get_info(
        t_id: c_int,
        out_sip_ipaddr: *mut *mut c_char,
        out_port: *mut c_int,
    ) -> c_int;

    pub fn pjw_account_create(t_id: c_int, json: *const c_char, out_acc_id: *mut c_int) -> c_int;

    pub fn pjw_account_register(acc_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_account_unregister(acc_id: c_long) -> c_int;

    pub fn pjw_request_create(
        t_id: c_long,
        json: *const c_char,
        out_request_id: *mut c_long,
        out_sip_call_id: *mut c_char,
    ) -> c_int;

    pub fn pjw_request_respond(request_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_create(
        t_id: c_long,
        json: *const c_char,
        out_call_id: *mut c_long,
        out_sip_call_id: *mut c_char,
    ) -> c_int;

    pub fn pjw_call_respond(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_terminate(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_send_dtmf(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_send_bfsk(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_reinvite(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_update(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_send_request(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_start_record_wav(call_id: c_long, file: *const c_char) -> c_int;

    pub fn pjw_call_start_play_wav(call_id: c_long, file: *const c_char) -> c_int;

    pub fn pjw_call_stop_play_wav(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_stop_record_wav(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_start_fax(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_stop_fax(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_start_speech_synth(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_stop_speech_synth(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_start_speech_recog(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_stop_speech_recog(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_start_inband_dtmf_detection(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_stop_inband_dtmf_detection(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_start_bfsk_detection(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_stop_bfsk_detection(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_call_get_stream_stat(
        call_id: c_long,
        json: *const c_char,
        out_stats: *mut c_char,
    ) -> c_int;

    pub fn pjw_call_get_info(
        call_id: c_long,
        required_info: *const c_char,
        out_info: *mut c_char,
    ) -> c_int;

    pub fn pjw_call_gen_string_replaces(call_id: c_long, out_replaces: *mut c_char) -> c_int;

    pub fn pjw_call_send_tcp_msg(call_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_get_codecs(out_codecs: *mut c_char) -> c_int;

    pub fn pjw_set_codecs(in_codec_info: *const c_char) -> c_int;

    pub fn pjw_set_opus_config(json: *const c_char) -> c_int;

    pub fn __pjw_shutdown(clean_up: c_int) -> c_int;

    pub fn pjw_notify(subscriber_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_notify_xfer(subscriber_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_register_pkg(event: *const c_char, accept: *const c_char) -> c_int;

    pub fn pjw_subscription_create(
        transport_id: c_long,
        json: *const c_char,
        out_subscription_id: *mut c_long,
    ) -> c_int;

    pub fn pjw_subscription_subscribe(subscription_id: c_long, json: *const c_char) -> c_int;

    pub fn pjw_log_level(log_level: c_long) -> c_int;

    pub fn pjw_set_flags(flags: std::os::raw::c_uint) -> c_int;

    pub fn pjw_dtmf_aggregation_on(inter_digit_timer: c_int) -> c_int;
    pub fn pjw_dtmf_aggregation_off() -> c_int;

    pub fn pjw_enable_telephone_event() -> c_int;
    pub fn pjw_disable_telephone_event() -> c_int;

    pub fn pjw_get_error() -> *mut c_char;
}
