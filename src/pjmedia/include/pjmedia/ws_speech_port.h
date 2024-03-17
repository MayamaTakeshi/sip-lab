#ifndef __WS_SPEECH_PORT_H__
#define __WS_SPEECH_PORT_H__

#include <pjmedia/port.h>
#include "websock.h"

PJ_BEGIN_DECL

enum ws_speech_event
{
  WS_SPEECH_EVENT_EOF,
  WS_SPEECH_EVENT_TRANSCRIPT,
  WS_SPEECH_EVENT_CONNECTED,
  WS_SPEECH_EVENT_CONNECTION_ERROR,
  WS_SPEECH_EVENT_DISCONNECTED
};

PJ_DEF(pj_status_t) pjmedia_ws_speech_port_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
                pj_websock_endpoint *ws_endpt,
                const char *server_url,
                const char *voice,
                const char *text,
                void (*cb)(pjmedia_port*, void *user_data, enum ws_speech_event, char *data),
                void *cb_user_data,
                unsigned flags,
                pj_bool_t end_of_speech_event,
				pjmedia_port **p_port);

PJ_END_DECL

#endif	/* __WS_SPEECH_PORT_H__ */
