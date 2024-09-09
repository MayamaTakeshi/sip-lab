#ifndef __WS_SPEECH_PORT_H__
#define __WS_SPEECH_PORT_H__

#include <pjmedia/port.h>
#include "websock.h"

PJ_BEGIN_DECL

enum ws_speech_event
{
  WS_SPEECH_EVENT_CONNECTED,
  WS_SPEECH_EVENT_CONNECTION_ERROR,
  WS_SPEECH_EVENT_DISCONNECTED,
  WS_SPEECH_EVENT_TEXT_MSG
};

PJ_DEF(pj_status_t) pjmedia_ws_speech_port_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
                pj_websock_endpoint *ws_endpt,
                const char *server_url,
                const char *uuid,
                const char *ss_engine,
                const char *ss_voice,
                const char *ss_language,
                const char *ss_text,
                int ss_times,
                const char *sr_engine,
                const char *sr_language,
                void (*cb)(pjmedia_port*, void *user_data, enum ws_speech_event, char *data, int len),
                void *cb_user_data,
				pjmedia_port **p_port);

PJ_END_DECL

#endif	/* __WS_SPEECH_PORT_H__ */
