#ifndef __EVENT_TEMPLATES__
#define __EVENT_TEMPLATES__


int make_evt_incoming_call(char *dest, int size, long transport_id, long call_id, int sip_msg_len, const char *sip_msg);

int make_evt_request(char *dest, int size, const char *entity_type, long id, int sip_msg_len, const char *sip_msg);

int make_evt_response(char *dest, int size, const char *entity_type, long id, int mname_len, const char *mname, int sip_msg_len, const char *sip_msg);

int make_evt_media_status(char *dest, int size, long call_id, const char *status, const char *local_media_mode, const char *remote_media_mode);

int make_evt_dtmf(char *dest, int size, long call_id, int digits_len, const char *digits, int mode);

int make_evt_call_ended(char *dest, int size, long call_id, int sip_msg_len, const char *sip_msg);

int make_evt_non_dialog_request(char *dest, int size, int sip_msg_len, const char *sip_msg);

int make_evt_internal_error(char *dest, int size, const char *msg);

int make_evt_reinvite(char *dest, int size, long call_id, int sip_msg_len, char *sip_msg);

int make_evt_registration_status(char *dest, int size, long account_id, int code, const char *reason, int expires);

int make_evt_fax_result(char *dest, int size, long call_id, int result);

#endif


