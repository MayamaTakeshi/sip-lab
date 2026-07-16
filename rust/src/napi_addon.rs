use napi::bindgen_prelude::*;
use napi_derive::napi;
use crate::sip_core;

#[napi]
pub fn transport_create(json: String) -> Result<serde_json::Value> {
    let (id, address, port) = sip_core::transport_create(&json)
        .map_err(|e| Error::new(Status::GenericFailure, e))?;

    Ok(serde_json::json!({
        "id": id,
        "address": address,
        "port": port
    }))
}

#[napi]
pub fn account_create(transport_id: i32, json: String) -> Result<i32> {
    sip_core::account_create(transport_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn account_register(account_id: i32, json: String) -> Result<()> {
    sip_core::account_register(account_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn account_unregister(account_id: i32) -> Result<()> {
    sip_core::account_unregister(account_id)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn request_create(transport_id: i32, json: String) -> Result<serde_json::Value> {
    let (id, sip_call_id) = sip_core::request_create(transport_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))?;

    Ok(serde_json::json!({
        "id": id,
        "sip_call_id": sip_call_id
    }))
}

#[napi]
pub fn request_respond(request_id: i32, json: String) -> Result<()> {
    sip_core::request_respond(request_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_create(transport_id: i32, json: String) -> Result<serde_json::Value> {
    let (id, sip_call_id) = sip_core::call_create(transport_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))?;

    Ok(serde_json::json!({
        "id": id,
        "sip_call_id": sip_call_id
    }))
}

#[napi]
pub fn call_respond(call_id: i32, json: String) -> Result<()> {
    sip_core::call_respond(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_terminate(call_id: i32, json: String) -> Result<()> {
    sip_core::call_terminate(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_send_dtmf(call_id: i32, json: String) -> Result<()> {
    sip_core::call_send_dtmf(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_send_bfsk(call_id: i32, json: String) -> Result<()> {
    sip_core::call_send_bfsk(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_reinvite(call_id: i32, json: String) -> Result<()> {
    sip_core::call_reinvite(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_update(call_id: i32, json: String) -> Result<()> {
    sip_core::call_update(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_send_request(call_id: i32, json: String) -> Result<()> {
    sip_core::call_send_request(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_record_wav(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_record_wav(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_play_wav(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_play_wav(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_fax(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_fax(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_speech_synth(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_speech_synth(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_speech_recog(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_speech_recog(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_inband_dtmf_detection(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_inband_dtmf_detection(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_inband_dtmf_detection(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_inband_dtmf_detection(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_start_bfsk_detection(call_id: i32, json: String) -> Result<()> {
    sip_core::call_start_bfsk_detection(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_bfsk_detection(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_bfsk_detection(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_record_wav(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_record_wav(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_play_wav(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_play_wav(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_fax(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_fax(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_speech_synth(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_speech_synth(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_stop_speech_recog(call_id: i32, json: String) -> Result<()> {
    sip_core::call_stop_speech_recog(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_get_stream_stat(call_id: i32, json: String) -> Result<String> {
    sip_core::call_get_stream_stat(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_get_info(call_id: i32, required_info: String) -> Result<String> {
    sip_core::call_get_info(call_id, &required_info)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_gen_string_replaces(call_id: i32) -> Result<String> {
    sip_core::call_gen_string_replaces(call_id)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn call_send_tcp_msg(call_id: i32, json: String) -> Result<()> {
    sip_core::call_send_tcp_msg(call_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn dtmf_aggregation_on(inter_digit_timer: i32) -> Result<()> {
    sip_core::dtmf_aggregation_on(inter_digit_timer)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn dtmf_aggregation_off() -> Result<()> {
    sip_core::dtmf_aggregation_off()
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn get_codecs() -> Result<String> {
    sip_core::get_codecs()
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn set_codecs(codec_info: String) -> Result<()> {
    sip_core::set_codecs(&codec_info)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn _set_opus_config(json: String) -> Result<()> {
    sip_core::set_opus_config(&json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn notify(subscriber_id: i32, json: String) -> Result<()> {
    sip_core::notify(subscriber_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn notify_xfer(subscriber_id: i32, json: String) -> Result<()> {
    sip_core::notify_xfer(subscriber_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn register_pkg(event: String, accept: String) -> Result<()> {
    sip_core::register_pkg(&event, &accept)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn subscription_create(transport_id: i32, json: String) -> Result<i64> {
    sip_core::subscription_create(transport_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn subscription_subscribe(subscription_id: i32, json: String) -> Result<()> {
    sip_core::subscription_subscribe(subscription_id, &json)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn set_log_level(log_level: i32) -> Result<i32> {
    sip_core::set_log_level(log_level)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn set_flags(flags: u32) -> Result<i32> {
    Ok(sip_core::set_flags(flags))
}

#[napi]
pub fn enable_telephone_event() -> Result<i32> {
    Ok(sip_core::enable_telephone_event())
}

#[napi]
pub fn disable_telephone_event() -> Result<i32> {
    Ok(sip_core::disable_telephone_event())
}

#[napi]
pub fn do_poll() -> Option<String> {
    sip_core::pjw_poll()
}

#[napi]
pub fn shutdown(clean_up: i32) -> Result<()> {
    sip_core::shutdown(clean_up)
        .map_err(|e| Error::new(Status::GenericFailure, e))
}

#[napi]
pub fn init() -> i32 {
    sip_core::pjw_init()
}

#[napi]
pub fn start() -> Result<()> {
    Ok(())
}
