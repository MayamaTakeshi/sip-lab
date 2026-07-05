use serde_json::json;

pub fn make_evt_incoming_call(transport_id: i64, call_id: i64, sip_msg: &str) -> String {
    let evt = json!({
        "event": "incoming_call",
        "transport_id": transport_id,
        "call_id": call_id
    });
    format!("{}
{}", evt.to_string(), sip_msg)
}

pub fn make_evt_request(entity_type: &str, id: i64, sip_msg: &str) -> String {
    let mut evt = json!({
        "event": "request"
    });
    evt.as_object_mut().unwrap().insert(format!("{}_id", entity_type), json!(id));
    format!("{}
{}", evt.to_string(), sip_msg)
}

pub fn make_evt_response(entity_type: &str, id: i64, mname: &str, sip_msg: &str) -> String {
    let mut evt = json!({
        "event": "response",
        "method": mname
    });
    evt.as_object_mut().unwrap().insert(format!("{}_id", entity_type), json!(id));
    format!("{}
{}", evt.to_string(), sip_msg)
}

pub fn make_evt_media_update(call_id: i64, status: &str, media: Option<&str>) -> String {
    let mut evt = json!({
        "event": "media_update",
        "call_id": call_id,
        "status": status
    });

    if status == "ok" {
        if let Some(media_str) = media {
            if let Ok(media_json) = serde_json::from_str::<serde_json::Value>(media_str) {
                evt.as_object_mut().unwrap().insert("media".to_string(), media_json);
            }
        }
    }
    evt.to_string()
}

pub fn make_evt_dtmf(call_id: i64, digits: &str, mode: i32, media_id: i32) -> String {
    json!({
        "event": "dtmf",
        "call_id": call_id,
        "digits": digits,
        "mode": mode,
        "media_id": media_id
    }).to_string()
}

pub fn make_evt_bfsk(call_id: i64, bits: &str, media_id: i32) -> String {
    json!({
        "event": "bfsk",
        "call_id": call_id,
        "bits": bits,
        "media_id": media_id
    }).to_string()
}

pub fn make_evt_call_ended(call_id: i64, sip_msg: Option<&str>) -> String {
    if let Some(msg) = sip_msg {
        let len = msg.len();
        if len > 500 && len < 2000 {
            let evt = json!({
                "event": "call_ended",
                "call_id": call_id
            });
            return format!("{}
{}", evt.to_string(), msg);
        }
    }
    json!({
        "event": "call_ended",
        "call_id": call_id
    }).to_string()
}

pub fn make_evt_non_dialog_request(transport_id: i64, request_id: i64, sip_msg: &str) -> String {
    let evt = json!({
        "event": "non_dialog_request",
        "request_id": request_id,
        "transport_id": transport_id
    });
    format!("{}
{}", evt.to_string(), sip_msg)
}

pub fn make_evt_internal_error(msg: &str) -> String {
    json!({
        "event": "internal_error",
        "error": msg
    }).to_string()
}

pub fn make_evt_reinvite(call_id: i64, sip_msg: &str) -> String {
    let evt = json!({
        "event": "reinvite",
        "call_id": call_id
    });
    format!("{}
{}", evt.to_string(), sip_msg)
}

pub fn make_evt_registration_status(account_id: i64, code: i32, reason: &str, expires: i32) -> String {
    json!({
        "event": "registration_status",
        "account_id": account_id,
        "code": code,
        "reason": reason,
        "expires": expires
    }).to_string()
}

pub fn make_evt_fax_result(call_id: i64, result: i32) -> String {
    json!({
        "event": "fax_result",
        "call_id": call_id,
        "result": result
    }).to_string()
}

pub fn make_evt_end_of_file(call_id: i64) -> String {
    json!({
        "event": "end_of_file",
        "call_id": call_id
    }).to_string()
}

pub fn make_evt_speech_synth_complete(call_id: i64) -> String {
    json!({
        "event": "speech_synth_complete",
        "call_id": call_id
    }).to_string()
}

pub fn make_evt_speech(call_id: i64, transcript: &str) -> String {
    json!({
        "event": "speech",
        "call_id": call_id,
        "transcript": transcript
    }).to_string()
}

pub fn make_evt_tcp_msg(call_id: i64, protocol: &str, data: &str) -> String {
    let evt = json!({
        "event": format!("{}_msg", protocol),
        "call_id": call_id
    });
    format!("{}
{}", evt.to_string(), data)
}

pub fn make_evt_ws_speech_event(call_id: i64, data: &str) -> String {
    let mut evt = json!({
        "event": "ws_speech_event",
        "call_id": call_id
    });
    if let Ok(data_json) = serde_json::from_str::<serde_json::Value>(data) {
        evt.as_object_mut().unwrap().insert("data".to_string(), data_json);
    } else {
        evt.as_object_mut().unwrap().insert("data".to_string(), json!(data));
    }
    evt.to_string()
}
