use std::sync::Mutex;
use std::collections::VecDeque;
use crate::id_manager::IdManager;
use once_cell::sync::Lazy;

pub static G_TRANSPORT_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));
pub static G_ACCOUNT_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));
pub static G_REQUEST_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));
pub static G_CALL_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));
pub static G_SUBSCRIPTION_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));
pub static G_SUBSCRIBER_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));
pub static G_DIALOG_IDS: Lazy<Mutex<IdManager>> = Lazy::new(|| Mutex::new(IdManager::new(2000)));

pub static G_EVENTS: Lazy<Mutex<VecDeque<String>>> = Lazy::new(|| Mutex::new(VecDeque::new()));

static PJW_ERRORSTRING: Lazy<Mutex<String>> = Lazy::new(|| Mutex::new(String::new()));

pub struct Transport {
    pub id: i32,
    pub tp_type: i32,
    pub sip_transport: *mut std::ffi::c_void,
    pub tpfactory: *mut std::ffi::c_void,
    pub address: String,
    pub port: i32,
    pub tag: String,
}

pub struct Account {
    pub id: i32,
    pub regc: *mut std::ffi::c_void,
}

pub struct Call {
    pub id: i32,
    pub inv: *mut std::ffi::c_void,
    pub transport_id: i32,
}

pub fn clear_error() {
    if let Ok(mut err) = PJW_ERRORSTRING.lock() {
        err.clear();
    }
}

pub fn set_error(msg: String) {
    if let Ok(mut err) = PJW_ERRORSTRING.lock() {
        *err = msg;
    }
}

pub fn get_error() -> String {
    if let Ok(err) = PJW_ERRORSTRING.lock() {
        err.clone()
    } else {
        "Lock failed".to_string()
    }
}

pub fn dispatch_event(evt: String) {
    if let Ok(mut events) = G_EVENTS.lock() {
        events.push_back(evt);
    }
}

pub fn pjw_poll() -> Option<String> {
    unsafe {
        crate::sip_ffi::handle_events();
    }

    if let Ok(mut events) = G_EVENTS.lock() {
        events.pop_front()
    } else {
        None
    }
}

pub fn pjw_init() -> i32 {
    clear_error();
    unsafe { crate::sip_ffi::__pjw_init() }
}

pub fn transport_create(json: &str) -> Result<(i32, String, i32), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;
    let mut out_t_id: i32 = 0;
    let mut out_t_address = [0u8; 256];
    let mut out_t_port: i32 = 0;

    let res = unsafe {
        crate::sip_ffi::pjw_transport_create(
            c_json.as_ptr(),
            &mut out_t_id,
            out_t_address.as_mut_ptr() as *mut i8,
            &mut out_t_port,
        )
    };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    let address = unsafe {
        CStr::from_ptr(out_t_address.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    };

    Ok((out_t_id, address, out_t_port))
}

pub fn account_create(t_id: i32, json: &str) -> Result<i32, String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;
    let mut out_a_id: i32 = 0;

    let res = unsafe { crate::sip_ffi::pjw_account_create(t_id, c_json.as_ptr(), &mut out_a_id) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(out_a_id)
}

pub fn account_register(acc_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_account_register(acc_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn account_unregister(acc_id: i32) -> Result<(), String> {
    use std::ffi::CStr;
    let res = unsafe { crate::sip_ffi::pjw_account_unregister(acc_id as i64) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn request_create(t_id: i32, json: &str) -> Result<(i64, String), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;
    let mut out_request_id: i64 = 0;
    let mut out_sip_call_id = [0u8; 256];

    let res = unsafe {
        crate::sip_ffi::pjw_request_create(
            t_id as i64,
            c_json.as_ptr(),
            &mut out_request_id,
            out_sip_call_id.as_mut_ptr() as *mut i8,
        )
    };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    let sip_call_id = unsafe {
        CStr::from_ptr(out_sip_call_id.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    };

    Ok((out_request_id, sip_call_id))
}

pub fn request_respond(request_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_request_respond(request_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_create(t_id: i32, json: &str) -> Result<(i64, String), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;
    let mut out_call_id: i64 = 0;
    let mut out_sip_call_id = [0u8; 256];

    let res = unsafe {
        crate::sip_ffi::pjw_call_create(
            t_id as i64,
            c_json.as_ptr(),
            &mut out_call_id,
            out_sip_call_id.as_mut_ptr() as *mut i8,
        )
    };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    let sip_call_id = unsafe {
        CStr::from_ptr(out_sip_call_id.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    };

    Ok((out_call_id, sip_call_id))
}

pub fn call_respond(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_respond(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_terminate(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_terminate(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_send_dtmf(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_send_dtmf(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_send_bfsk(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_send_bfsk(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_reinvite(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_reinvite(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_update(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_update(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_send_request(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_send_request(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_record_wav(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_record_wav(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_play_wav(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_play_wav(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_fax(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_fax(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_speech_synth(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_speech_synth(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_speech_recog(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_speech_recog(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_inband_dtmf_detection(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_inband_dtmf_detection(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_inband_dtmf_detection(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_inband_dtmf_detection(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_start_bfsk_detection(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_start_bfsk_detection(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_bfsk_detection(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_bfsk_detection(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_record_wav(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_record_wav(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_play_wav(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_play_wav(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_fax(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_fax(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_speech_synth(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_speech_synth(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_stop_speech_recog(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_stop_speech_recog(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn call_get_stream_stat(call_id: i32, json: &str) -> Result<String, String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;
    let mut out_stats = [0u8; 4096];

    let res = unsafe {
        crate::sip_ffi::pjw_call_get_stream_stat(
            call_id as i64,
            c_json.as_ptr(),
            out_stats.as_mut_ptr() as *mut i8,
        )
    };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(unsafe {
        CStr::from_ptr(out_stats.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    })
}

pub fn call_get_info(call_id: i32, required_info: &str) -> Result<String, String> {
    use std::ffi::{CStr, CString};
    let c_required_info = CString::new(required_info).map_err(|_| "Invalid string".to_string())?;
    let mut out_info = [0u8; 4096];

    let res = unsafe {
        crate::sip_ffi::pjw_call_get_info(
            call_id as i64,
            c_required_info.as_ptr(),
            out_info.as_mut_ptr() as *mut i8,
        )
    };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(unsafe {
        CStr::from_ptr(out_info.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    })
}

pub fn call_gen_string_replaces(call_id: i32) -> Result<String, String> {
    use std::ffi::CStr;
    let mut out_replaces = [0u8; 4096];

    let res = unsafe { crate::sip_ffi::pjw_call_gen_string_replaces(call_id as i64, out_replaces.as_mut_ptr() as *mut i8) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(unsafe {
        CStr::from_ptr(out_replaces.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    })
}

pub fn call_send_tcp_msg(call_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_call_send_tcp_msg(call_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn dtmf_aggregation_on(inter_digit_timer: i32) -> Result<(), String> {
    use std::ffi::CStr;
    let res = unsafe { crate::sip_ffi::pjw_dtmf_aggregation_on(inter_digit_timer) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn dtmf_aggregation_off() -> Result<(), String> {
    use std::ffi::CStr;
    let res = unsafe { crate::sip_ffi::pjw_dtmf_aggregation_off() };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn get_codecs() -> Result<String, String> {
    use std::ffi::CStr;
    let mut out_codecs = [0u8; 4096];

    let res = unsafe { crate::sip_ffi::pjw_get_codecs(out_codecs.as_mut_ptr() as *mut i8) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(unsafe {
        CStr::from_ptr(out_codecs.as_ptr() as *const i8)
            .to_string_lossy()
            .into_owned()
    })
}

pub fn set_codecs(codec_info: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_codec_info = CString::new(codec_info).map_err(|_| "Invalid string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_set_codecs(c_codec_info.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn set_opus_config(json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_set_opus_config(c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn notify(subscriber_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_notify(subscriber_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn notify_xfer(subscriber_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_notify_xfer(subscriber_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn register_pkg(event: &str, accept: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_event = CString::new(event).map_err(|_| "Invalid string".to_string())?;
    let c_accept = CString::new(accept).map_err(|_| "Invalid string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_register_pkg(c_event.as_ptr(), c_accept.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn subscription_create(transport_id: i32, json: &str) -> Result<i64, String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;
    let mut out_subscription_id: i64 = 0;

    let res = unsafe {
        crate::sip_ffi::pjw_subscription_create(
            transport_id as i64,
            c_json.as_ptr(),
            &mut out_subscription_id,
        )
    };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(out_subscription_id)
}

pub fn subscription_subscribe(subscription_id: i32, json: &str) -> Result<(), String> {
    use std::ffi::{CStr, CString};
    let c_json = CString::new(json).map_err(|_| "Invalid JSON string".to_string())?;

    let res = unsafe { crate::sip_ffi::pjw_subscription_subscribe(subscription_id as i64, c_json.as_ptr()) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}

pub fn set_log_level(log_level: i32) -> Result<i32, String> {
    use std::ffi::CStr;
    let res = unsafe { crate::sip_ffi::pjw_log_level(log_level as i64) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(0)
}

pub fn set_flags(flags: u32) -> i32 {
    unsafe { crate::sip_ffi::pjw_set_flags(flags) }
}

pub fn enable_telephone_event() -> i32 {
    unsafe { crate::sip_ffi::pjw_enable_telephone_event() }
}

pub fn disable_telephone_event() -> i32 {
    unsafe { crate::sip_ffi::pjw_disable_telephone_event() }
}

pub fn shutdown(clean_up: i32) -> Result<(), String> {
    use std::ffi::CStr;
    let res = unsafe { crate::sip_ffi::__pjw_shutdown(clean_up) };

    if res != 0 {
        let error = unsafe {
            CStr::from_ptr(crate::sip_ffi::pjw_get_error())
                .to_string_lossy()
                .into_owned()
        };
        return Err(error);
    }

    Ok(())
}
