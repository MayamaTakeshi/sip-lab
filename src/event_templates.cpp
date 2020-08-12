#include <stdio.h>
#include <string.h>

int make_evt_incoming_call(char *dest, int size, long transport_id, long call_id, int sip_msg_len, char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"incoming_call\", \"transport_id\": %i, \"call_id\": %i}\n%.*s", transport_id, call_id, sip_msg_len, sip_msg);
}

int make_evt_request(char *dest, int size, char *entity_type, long id, int sip_msg_len, char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"request\", \"%s_id\": %i}\n%.*s", entity_type, id, sip_msg_len, sip_msg);
}

int make_evt_response(char *dest, int size, char *entity_type, long id, int mname_len, char *mname, int sip_msg_len, char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"response\", \"%s_id\": %i, \"method\": \"%.*s\"}\n%.*s", entity_type, id, mname_len, mname, sip_msg_len, sip_msg);
}

int make_evt_media_status(char *dest, int size, long call_id, char *status, char *local_mode, char *remote_mode) {
	if(strcmp(status, "setup_ok") == 0) {
		return snprintf(dest, size, "{\"event\": \"media_status\", \"call_id\": %i, \"status\": \"%s\", \"local_mode\": \"%s\", \"remote_mode\": \"%s\"}", call_id, status, local_mode, remote_mode);
	} else {
		return snprintf(dest, size, "{\"event\": \"media_status\", \"call_id\": %i, \"status\": \"%s\"}", call_id, status);
	}
}

int make_evt_dtmf(char *dest, int size, long call_id, int digits_len, char *digits, int mode) {
	return snprintf(dest, size, "{\"event\": \"dtmf\", \"call_id\": %i, \"digits\": \"%.*s\", \"mode\": %i}", call_id, digits_len, digits, mode);
}

int make_evt_call_ended(char *dest, int size, long call_id, int sip_msg_len, char *sip_msg) {
	printf("sip_msg_len=%i sip_msg=%x\n", sip_msg_len, sip_msg);
	if(sip_msg_len > 500 && sip_msg_len < 2000 && sip_msg) {
		/* sip_msg_len sometimes show up as a large value like sip_msg_len=11560297 which seems to be a bug in pjsip */
		return snprintf(dest, size, "{\"event\": \"call_ended\", \"call_id\": %i}\n%.*s", call_id, sip_msg_len, sip_msg); 
	} else {
		return snprintf(dest, size, "{\"event\": \"call_ended\", \"call_id\": %i}", call_id); 
	}
}

int make_evt_non_dialog_request(char *dest, int size, int sip_msg_len, char *sip_msg) {
	return snprintf(dest, size, "{\"event\": \"non_dialog_request\"}\n%.*s", sip_msg_len, sip_msg);
}

int make_evt_internal_error(char *dest, int size, char *msg) {
	return snprintf(dest, size, "{\"event\": \"internal_error\", \"error\": \"%s\"}", msg);
}

/*
int make_evt_reinvite(char *dest, int size, long call_id, char *type) {
	return snprintf(dest, size, "{\"event\": \"reinvite\", \"call_id\": %i, \"type\": \"%s\"}", call_id, type); 
}
*/

int make_evt_registration_status(char *dest, int size, long account_id, int code, char *reason, int expires) {
	return snprintf(dest, size, "{\"event\": \"registration_status\", \"account_id\": %i, \"code\": %i, \"reason\": \"%s\", \"expires\": %i}", account_id, code, reason, expires);
}

