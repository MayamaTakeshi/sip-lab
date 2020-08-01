#ifndef __EVENT_TEMPLATES__
#define __EVENT_TEMPLATES__


int make_evt_incoming_call(char *dest, int size, long transport_id, long call_id, int sip_msg_len, char *sip_msg);

int make_evt_request(char *dest, int size, char *entity_type, long id, int sip_msg_len, char *sip_msg);

int make_evt_response(char *dest, int size, char *entity_type, long id, int mname_len, char *mname, int sip_msg_len, char *sip_msg);

int make_evt_media_status(char *dest, int size, long call_id, char *status);

int make_evt_dtmf(char *dest, int size, long call_id, int digits_len, char *digits, int mode);

int make_evt_call_ended(char *dest, int size, long call_id, int sip_msg_len, char *sip_msg);

int make_evt_non_dialog_request(char *dest, int size, int sip_msg_len, char *sip_msg);

int make_evt_internal_error(char *dest, int size, char *msg);

int make_evt_reinvite(char *dest, int size, long call_id, char *type);

int make_evt_registration_status(char *dest, int size, long account_id, int code, char *reason, int expires);

#endif


