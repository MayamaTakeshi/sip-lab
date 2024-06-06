/* $Id: ws_speech_port.c 0000 2024-03-17 mayamatakeshi $ */
/* 
 * Copyright (C) 2008-2009 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <ws_speech_port.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/log.h>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#define SIGNATURE   PJMEDIA_SIGNATURE('w', 's', 's', 'p')
#define THIS_FILE   "ws_speech_port.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

static pj_status_t put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);

static pj_status_t get_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);

static pj_status_t on_destroy(pjmedia_port *this_port);

#define SPEECH_BUFFER_SIZE 8192
#define MINIMAl_BUFFERING 3

struct ws_speech_t
{
    struct pjmedia_port	base;

    struct pj_websock_endpoint *ws_endpt;

    void (*cb)(pjmedia_port*, void*, enum ws_speech_event, char*);
    void *cb_user_data;

    char buffer[SPEECH_BUFFER_SIZE];
    short buffer_top;
    int buffering_count;

    char transcript[4096];

    pj_websock_t *wc;
    bool connected;

    int sample_rate;

    char *ss_engine;
    char *ss_voice;
    char *ss_language;
    char *ss_text;
    int ss_times;

    char *sr_engine;
    char *sr_language;
};


static pj_bool_t on_connect_complete(pj_websock_t *c, pj_status_t status)
{
    printf("ws_speech_port on_connect_complete\n");

    char buf[1000];
    PJ_PERROR(4, (THIS_FILE, status, "%s() %s", __FUNCTION__,
                  pj_websock_print(c, buf, sizeof(buf))));

    struct ws_speech_t *port = (struct ws_speech_t*)pj_websock_get_userdata(c);
    if (status == PJ_SUCCESS) {
        port->cb((pjmedia_port*)port, port->cb_user_data, WS_SPEECH_EVENT_CONNECTED, "");
    } else {
        port->cb((pjmedia_port*)port, port->cb_user_data, WS_SPEECH_EVENT_CONNECTION_ERROR, "");
    }

    if(port->ss_engine) {
        rapidjson::Document document;
        document.SetObject();

        // Obtain the allocator from the document
        rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

        // Add the "cmd" member to the document
        document.AddMember("cmd", "start_speech_synth", allocator);

        // Create the "args" object
        rapidjson::Value args(rapidjson::kObjectType);

        // Add members to the "args" object
        args.AddMember("sampleRate", port->sample_rate, allocator);
        args.AddMember("engine", rapidjson::Value(port->ss_engine, allocator), allocator);
        args.AddMember("voice", rapidjson::Value(port->ss_voice, allocator), allocator);
        args.AddMember("language", rapidjson::Value(port->ss_language, allocator), allocator);
        args.AddMember("text", rapidjson::Value(port->ss_text, allocator), allocator);
        args.AddMember("times", port->ss_times, allocator);

        // Add the "args" object to the document
        document.AddMember("args", args, allocator);

        // Stringify the JSON document
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
         
        printf("\nsending cmd: %.*s\n", buffer.GetLength(), buffer.GetString());
        pj_websock_send(c, PJ_WEBSOCK_OP_TEXT, PJ_TRUE, PJ_TRUE, (void*)buffer.GetString(), buffer.GetLength());
    }

    if(port->sr_engine) {
        rapidjson::Document document;
        document.SetObject();

        // Obtain the allocator from the document
        rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

        // Add the "cmd" member to the document
        document.AddMember("cmd", "start_speech_recog", allocator);

        // Create the "args" object
        rapidjson::Value args(rapidjson::kObjectType);

        // Add members to the "args" object
        args.AddMember("sampleRate", port->sample_rate, allocator);
        args.AddMember("engine", rapidjson::Value(port->sr_engine, allocator), allocator);
        args.AddMember("language", rapidjson::Value(port->sr_language, allocator), allocator);

        // Add the "args" object to the document
        document.AddMember("args", args, allocator);

        // Stringify the JSON document
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
         
        printf("\nsending cmd: %.*s\n", buffer.GetLength(), buffer.GetString());
        pj_websock_send(c, PJ_WEBSOCK_OP_TEXT, PJ_TRUE, PJ_TRUE, (void*)buffer.GetString(), buffer.GetLength());
    }

    port->connected = true;

    return PJ_TRUE;
}

static pj_bool_t on_rx_msg(pj_websock_t *c,
                           pj_websock_rx_data *msg,
                           pj_status_t status)
{
    pj_websock_frame_hdr *hdr;
    char *data;
    char buf[1000];

    struct ws_speech_t *port = (struct ws_speech_t*)pj_websock_get_userdata(c);

    if (status != PJ_SUCCESS) {
        PJ_PERROR(2, (THIS_FILE, status, "#Disconnect with %s",
                      pj_websock_print(c, buf, sizeof(buf))));

        port->cb((pjmedia_port*)port, port->cb_user_data, WS_SPEECH_EVENT_DISCONNECTED, "");
        return PJ_FALSE;
    }

    hdr = &msg->hdr;
    data = (char *)msg->data;

    if (hdr->opcode == PJ_WEBSOCK_OP_TEXT) {
       printf( 
                   "RX from %s:\n"
                   "TEXT %s %llu/%llu/%llu [%.*s]",
                   pj_websock_print(c, buf, sizeof(buf)),
                   hdr->mask ? "(masked)" : "", hdr->len, msg->has_read,
                   msg->data_len, (int)msg->data_len, data);

        /* echo response */
        // pj_websock_send(c, hdr->opcode, PJ_TRUE, PJ_FALSE, data, hdr->len);
    } else if (hdr->opcode == PJ_WEBSOCK_OP_BIN) {
        printf("PJ_WEBSOCK_OP_BIN. top=%i data_len=%i\n", port->buffer_top, msg->data_len);
        if(port->buffer_top + msg->data_len < SPEECH_BUFFER_SIZE) {
            memcpy(port->buffer + port->buffer_top, data, msg->data_len);
            port->buffer_top += msg->data_len;
            port->buffering_count++;
        }
    } else if (hdr->opcode == PJ_WEBSOCK_OP_PING) {
        PJ_LOG(4, (THIS_FILE, "RX from %s PING",
                   pj_websock_print(c, buf, sizeof(buf))));
        /* response pong */
        pj_websock_send(c, PJ_WEBSOCK_OP_PONG, PJ_TRUE, PJ_TRUE, NULL, 0);
    } else if (hdr->opcode == PJ_WEBSOCK_OP_PONG) {
        PJ_LOG(4, (THIS_FILE, "RX from %s PONG",
                   pj_websock_print(c, buf, sizeof(buf))));
    } else if (hdr->opcode == PJ_WEBSOCK_OP_CLOSE) {
        PJ_LOG(4, (THIS_FILE, "RX from %s CLOSE",
                   pj_websock_print(c, buf, sizeof(buf))));
        pj_websock_close(c, PJ_WEBSOCK_SC_GOING_AWAY, NULL);
        port->cb((pjmedia_port*)port, port->cb_user_data, WS_SPEECH_EVENT_DISCONNECTED, "");
        return PJ_FALSE; /* Must return false to stop read any more */
    }

    return PJ_TRUE;
}

static void on_state_change(pj_websock_t *c, int state)
{
    char buf[1000];
    printf("%s() %s %s", __FUNCTION__, pj_websock_print(c, buf, sizeof(buf)), pj_websock_state_str(state));
}


static pj_status_t speech_on_event(pjmedia_event *event,
                                 void *user_data)
{
    struct ws_speech_t *port = (struct ws_speech_t*)user_data;

    if (event->type == PJMEDIA_EVENT_CALLBACK) {
        if (port->cb)
            (*port->cb)(&port->base, port->cb_user_data, WS_SPEECH_EVENT_TRANSCRIPT, port->transcript);
    }
    
    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_ws_speech_port_create(pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
                struct pj_websock_endpoint *ws_endpt,
                const char *server_url,
                const char *ss_engine,
                const char *ss_voice,
                const char *ss_language,
                const char *ss_text,
                int ss_times,
                const char *sr_engine,
                const char *sr_language,
                void (*cb)(pjmedia_port*, void *user_data, enum ws_speech_event, char *data),
                void *cb_user_data,
				pjmedia_port **p_port)
{
    printf("pjmedia_ws_speech_port_create clock_rate=%i samples_per_frame=%i bits_per_sample=%i\n", clock_rate, samples_per_frame, bits_per_sample);
    struct ws_speech_t *port;
    const pj_str_t name = pj_str("ws_speech");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count == 1 && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    PJ_ASSERT_RETURN(cb, PJ_EINVAL);

    port = PJ_POOL_ZALLOC_T(pool, struct ws_speech_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    port->sample_rate = clock_rate;

    if(ss_engine) {
      port->ss_engine = (char*)pj_pool_alloc(pool, strlen(ss_engine) + 1);
      pj_ansi_strcpy(port->ss_engine, ss_engine);
    }

    if(ss_voice) {
      port->ss_voice = (char*)pj_pool_alloc(pool, strlen(ss_voice) + 1);
      pj_ansi_strcpy(port->ss_voice, ss_voice);
    }

    if(ss_language) {
      port->ss_language = (char*)pj_pool_alloc(pool, strlen(ss_language) + 1);
      pj_ansi_strcpy(port->ss_language, ss_language);
    }

    if(ss_text) {
      port->ss_text = (char*)pj_pool_alloc(pool, strlen(ss_text) + 1);
      pj_ansi_strcpy(port->ss_text, ss_text);
    }

    port->ss_times = ss_times;

    if(sr_engine) {
      port->sr_engine = (char*)pj_pool_alloc(pool, strlen(sr_engine) + 1);
      pj_ansi_strcpy(port->sr_engine, sr_engine);
    }

    if(sr_language) {
      port->sr_language = (char*)pj_pool_alloc(pool, strlen(sr_language) + 1);
      pj_ansi_strcpy(port->sr_language, sr_language);
    }

    pjmedia_port_info_init(&port->base.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    port->base.put_frame = &put_frame;
    port->base.get_frame = &get_frame;
    port->base.on_destroy = &on_destroy;

    port->ws_endpt = ws_endpt,

    port->cb = cb;
    port->cb_user_data = cb_user_data;

    pj_websock_http_hdr hdr;
    pj_websock_cb ws_cb;
    pj_bzero(&cb, sizeof(ws_cb));
    ws_cb.on_connect_complete = on_connect_complete;
    ws_cb.on_rx_msg = on_rx_msg;
    ws_cb.on_tx_msg = NULL;
    ws_cb.on_state_change = on_state_change;

    {
        hdr.key = pj_str("Sec-WebSocket-Protocol");
        hdr.val = pj_str("pjsip");
        pj_websock_connect(port->ws_endpt, server_url, &ws_cb, port, &hdr, 1, &port->wc);
    }

    printf("ws_speech port created: %u/%u/%u/%u", clock_rate, channel_count, samples_per_frame, bits_per_sample);

    *p_port = &port->base; 
    return PJ_SUCCESS;
}

static pj_status_t put_frame(pjmedia_port *this_port, pjmedia_frame *frame) {
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct ws_speech_t *port = (struct ws_speech_t*) this_port;

    if(port->wc && port->connected) {
        pj_websock_send(port->wc, PJ_WEBSOCK_OP_BIN, PJ_TRUE, PJ_TRUE, frame->buf, frame->size); 
    }

    return PJ_SUCCESS;
}

static pj_status_t get_frame(pjmedia_port *this_port, pjmedia_frame *frame) {
    printf("pjmedia_ws_speech_port get_frame\n");
	PJ_ASSERT_RETURN(this_port && frame, PJ_EINVAL);

    struct ws_speech_t *port = (struct ws_speech_t*)this_port;

    if(!port->wc) {
        //printf("no data\n");
        frame->type = PJMEDIA_FRAME_TYPE_NONE;
        return PJ_SUCCESS;
    }

    int len = PJMEDIA_PIA_SPF(&this_port->info)*2;

    if(port->buffering_count >= MINIMAl_BUFFERING && port->buffer_top > 0 && port->buffer_top >= len) {
        printf("get_frame top=%i\n", port->buffer_top);
        memcpy(frame->buf, port->buffer, len);
        port->buffer_top -= len;
        memcpy(port->buffer, port->buffer + len, port->buffer_top);
        frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
    } else {
        frame->type = PJMEDIA_FRAME_TYPE_NONE;
    }
    return PJ_SUCCESS;
}

static pj_status_t on_destroy(pjmedia_port *this_port)
{
    struct ws_speech_t *port = (struct ws_speech_t*) this_port;

    return PJ_SUCCESS;
}


