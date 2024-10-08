#include <stdio.h>
#include <string.h>

int make_evt_incoming_call(char *dest, int size, long transport_id,
                           long call_id, int sip_msg_len, const char *sip_msg) {
  return snprintf(dest, size,
                  "{\"event\": \"incoming_call\", \"transport_id\": %ld, "
                  "\"call_id\": %ld}\n%.*s",
                  transport_id, call_id, sip_msg_len, sip_msg);
}

int make_evt_request(char *dest, int size, const char *entity_type, long id,
                     int sip_msg_len, const char *sip_msg) {
  return snprintf(dest, size, "{\"event\": \"request\", \"%s_id\": %ld}\n%.*s",
                  entity_type, id, sip_msg_len, sip_msg);
}

int make_evt_response(char *dest, int size, const char *entity_type, long id,
                      int mname_len, const char *mname, int sip_msg_len,
                      const char *sip_msg) {
  return snprintf(
      dest, size,
      "{\"event\": \"response\", \"%s_id\": %ld, \"method\": \"%.*s\"}\n%.*s",
      entity_type, id, mname_len, mname, sip_msg_len, sip_msg);
}

int make_evt_media_update(char *dest, int size, long call_id,
                          const char *status, const char *media) {
  if (strcmp(status, "ok") == 0) {
    return snprintf(dest, size,
                    "{\"event\": \"media_update\", \"call_id\": %ld, "
                    "\"status\": \"%s\", \"media\": %s}",
                    call_id, status, media);
  } else {
    return snprintf(
        dest, size,
        "{\"event\": \"media_update\", \"call_id\": %ld, \"status\": \"%s\"}",
        call_id, status);
  }
}

int make_evt_dtmf(char *dest, int size, long call_id, int digits_len,
                  const char *digits, int mode, int media_id) {
  return snprintf(dest, size,
                  "{\"event\": \"dtmf\", \"call_id\": %ld, \"digits\": "
                  "\"%.*s\", \"mode\": %i, \"media_id\": %i}",
                  call_id, digits_len, digits, mode, media_id);
}

int make_evt_bfsk(char *dest, int size, long call_id, int bits_len,
                  const char *bits, int media_id) {
  return snprintf(dest, size,
                  "{\"event\": \"bfsk\", \"call_id\": %ld, \"bits\": "
                  "\"%.*s\", \"media_id\": %i}",
                  call_id, bits_len, bits, media_id);
}

int make_evt_call_ended(char *dest, int size, long call_id, int sip_msg_len,
                        const char *sip_msg) {
  printf("make_evt_call_ended sip_msg_len=%i sip_msg=%p\n", sip_msg_len,
         sip_msg);
  if (sip_msg_len > 500 && sip_msg_len < 2000 && sip_msg) {
    /* sip_msg_len sometimes show up as a large value like sip_msg_len=11560297
     * which seems to be a bug in pjsip */
    return snprintf(dest, size,
                    "{\"event\": \"call_ended\", \"call_id\": %ld}\n%.*s",
                    call_id, sip_msg_len, sip_msg);
  } else {
    return snprintf(dest, size, "{\"event\": \"call_ended\", \"call_id\": %ld}",
                    call_id);
  }
}

int make_evt_non_dialog_request(char *dest, int size, long transport_id,
                                long request_id, int sip_msg_len,
                                const char *sip_msg) {
  return snprintf(dest, size,
                  "{\"event\": \"non_dialog_request\", \"request_id\": %li, "
                  "\"transport_id\": %li}\n%.*s",
                  request_id, transport_id, sip_msg_len, sip_msg);
}

int make_evt_internal_error(char *dest, int size, const char *msg) {
  return snprintf(dest, size,
                  "{\"event\": \"internal_error\", \"error\": \"%s\"}", msg);
}

int make_evt_reinvite(char *dest, int size, long call_id, int sip_msg_len,
                      char *sip_msg) {
  return snprintf(dest, size,
                  "{\"event\": \"reinvite\", \"call_id\": %li}\n%.*s", call_id,
                  sip_msg_len, sip_msg);
}

int make_evt_registration_status(char *dest, int size, long account_id,
                                 int code, const char *reason, int expires) {
  return snprintf(dest, size,
                  "{\"event\": \"registration_status\", \"account_id\": %ld, "
                  "\"code\": %i, \"reason\": \"%s\", \"expires\": %i}",
                  account_id, code, reason, expires);
}

int make_evt_fax_result(char *dest, int size, long call_id, int result) {
  return snprintf(
      dest, size,
      "{\"event\": \"fax_result\", \"call_id\": %ld, \"result\": %i}", call_id,
      result);
}

int make_evt_end_of_file(char *dest, int size, long call_id) {
  return snprintf(
      dest, size,
      "{\"event\": \"end_of_file\", \"call_id\": %ld}", call_id);
}

int make_evt_speech_synth_complete(char *dest, int size, long call_id) {
  return snprintf(
      dest, size,
      "{\"event\": \"speech_synth_complete\", \"call_id\": %ld}", call_id);
}

int make_evt_speech(char *dest, int size, long call_id, char* transcript) {
  return snprintf(
      dest, size,
      "{\"event\": \"speech\", \"call_id\": %ld, \"transcript\": \"%s\"}", call_id, transcript);
}

int make_evt_tcp_msg(char *dest, int size, long call_id, const char *protocol, char *data, int data_len) {
  return snprintf(
      dest, size,
      "{\"event\": \"%s_msg\", \"call_id\": %ld}\n%.*s", protocol, call_id, data_len, data);
}

int make_evt_ws_speech_event(char *dest, int size, long call_id, char *data, int data_len) {
  return snprintf(
      dest, size,
      "{\"event\": \"ws_speech_event\", \"call_id\": %ld, \"data\": %.*s}", call_id, data_len, data);
}

