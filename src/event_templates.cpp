#include <stdio.h>
#include <string.h>

int make_evt_incoming_call(char *dest, int size, long transport_id, long call_id, int sip_msg_len, const char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"incoming_call\", \"transport_id\": %ld, \"call_id\": %ld}\n%.*s", transport_id, call_id, sip_msg_len, sip_msg);
}

int make_evt_request(char *dest, int size, const char *entity_type, long id, int sip_msg_len, const char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"request\", \"%s_id\": %ld}\n%.*s", entity_type, id, sip_msg_len, sip_msg);
}

int make_evt_response(char *dest, int size, const char *entity_type, long id, int mname_len, const char *mname, int sip_msg_len, const char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"response\", \"%s_id\": %ld, \"method\": \"%.*s\"}\n%.*s", entity_type, id, mname_len, mname, sip_msg_len, sip_msg);
}

int make_evt_media_update(char *dest, int size, long call_id, const char *status, const char *media) {
	if(strcmp(status, "ok") == 0) {
		return snprintf(dest, size, "{\"event\": \"media_update\", \"call_id\": %ld, \"status\": \"%s\", \"media\": %s}", call_id, status, media);
	} else {
		return snprintf(dest, size, "{\"event\": \"media_update\", \"call_id\": %ld, \"status\": \"%s\"}", call_id, status);
	}
}

int make_evt_dtmf(char *dest, int size, long call_id, int digits_len, const char *digits, int mode, int media_id) {
	return snprintf(dest, size, "{\"event\": \"dtmf\", \"call_id\": %ld, \"digits\": \"%.*s\", \"mode\": %i, \"media_id\": %i}", call_id, digits_len, digits, mode, media_id);
}

int make_evt_call_ended(char *dest, int size, long call_id, int sip_msg_len, const char *sip_msg) {
	printf("make_evt_call_ended sip_msg_len=%i sip_msg=%x\n", sip_msg_len, sip_msg);
    if(!sip_msg || sip_msg == (char*)0xc000000000000) {
        // received invalid pointer to sip_msg so do not add the message to the event
		return snprintf(dest, size, "{\"event\": \"call_ended\", \"call_id\": %ld}", call_id); 
    } else if(sip_msg_len > 500 && sip_msg_len < 2000 && sip_msg) {
		/* sip_msg_len sometimes show up as a large value like sip_msg_len=11560297 which seems to be a bug in pjsip */
		return snprintf(dest, size, "{\"event\": \"call_ended\", \"call_id\": %ld}\n%.*s", call_id, sip_msg_len, sip_msg); 
	} else {
		return snprintf(dest, size, "{\"event\": \"call_ended\", \"call_id\": %ld}", call_id); 
	}
}

int make_evt_non_dialog_request(char *dest, int size, int sip_msg_len, const char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"non_dialog_request\"}\n%.*s", sip_msg_len, sip_msg);
}

int make_evt_internal_error(char *dest, int size, const char *msg) {
	return snprintf(dest, size, "{\"event\": \"internal_error\", \"error\": \"%s\"}", msg);
}

int make_evt_reinvite(char *dest, int size, long call_id, int sip_msg_len, char *sip_msg) {
    return snprintf(dest, size, "{\"event\": \"reinvite\", \"call_id\": %i}\n%.*s", call_id, sip_msg_len, sip_msg); 
}

int make_evt_registration_status(char *dest, int size, long account_id, int code, const char *reason, int expires) {
	return snprintf(dest, size, "{\"event\": \"registration_status\", \"account_id\": %ld, \"code\": %i, \"reason\": \"%s\", \"expires\": %i}", account_id, code, reason, expires);
}

int make_evt_fax_result(char *dest, int size, long call_id, int result) {
   return snprintf(dest, size, "{\"event\": \"fax_result\", \"call_id\": %ld, \"result\": %i}", call_id, result);
}

