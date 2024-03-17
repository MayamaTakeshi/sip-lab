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

#define SPEECH_BUFFER_SIZE 25600

struct ws_speech_t
{
    struct pjmedia_port	base;

    struct pj_websock_endpoint *ws_endpt;

    void (*cb)(pjmedia_port*, void*, enum ws_speech_event, char*);
    void *cb_user_data;

    char speech_buffer[SPEECH_BUFFER_SIZE];
    short size; 

    char transcript[4096];

    pj_websock_t *wc;
};


static pj_bool_t on_connect_complete(pj_websock_t *c, pj_status_t status)
{
    char buf[1000];
    PJ_PERROR(4, (THIS_FILE, status, "%s() %s", __FUNCTION__,
                  pj_websock_print(c, buf, sizeof(buf))));

    struct ws_speech_t *port = (struct ws_speech_t*)pj_websock_get_userdata(c);
    if (status == PJ_SUCCESS) {
        port->cb((pjmedia_port*)port, port->cb_user_data, WS_SPEECH_EVENT_CONNECTED, "");
    } else {
        port->cb((pjmedia_port*)port, port->cb_user_data, WS_SPEECH_EVENT_CONNECTION_ERROR, "");
    }

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
        PJ_LOG(4, (THIS_FILE,
                   "RX from %s:\n"
                   "TEXT %s %llu/%llu/%llu [%.*s]",
                   pj_websock_print(c, buf, sizeof(buf)),
                   hdr->mask ? "(masked)" : "", hdr->len, msg->has_read,
                   msg->data_len, (int)msg->data_len, data));

        /* echo response */
        // pj_websock_send(c, hdr->opcode, PJ_TRUE, PJ_FALSE, data, hdr->len);
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
    PJ_LOG(4, (THIS_FILE, "%s() %s %s", __FUNCTION__,
               pj_websock_print(c, buf, sizeof(buf)),
               pj_websock_state_str(state)));
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
                char *server_url,
                void (*cb)(pjmedia_port*, void *user_data, enum ws_speech_event, char *transcript),
                void *cb_user_data,
				pjmedia_port **p_port)
{
    struct ws_speech_t *port;
    const pj_str_t name = pj_str("ws_speech");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count == 1 && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    PJ_ASSERT_RETURN(cb, PJ_EINVAL);

    port = PJ_POOL_ZALLOC_T(pool, struct ws_speech_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

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
    ws_cb.on_state_change = on_state_change;

    {
        hdr.key = pj_str("Sec-WebSocket-Protocol");
        hdr.val = pj_str("pjsip");
        pj_websock_connect(port->ws_endpt, server_url, &ws_cb, port, &hdr, 1, &port->wc);
    }

    TRACE_((THIS_FILE, "ws_speech port created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    *p_port = &port->base; 
    return PJ_SUCCESS;
}

static pj_status_t put_frame(pjmedia_port *this_port, pjmedia_frame *frame) {
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct ws_speech_t *port = (struct ws_speech_t*) this_port;

    if(port->wc) {
        //TODO: write binary data to socket
    }

    return PJ_SUCCESS;
}

static pj_status_t get_frame(pjmedia_port *this_port, pjmedia_frame *frame) {
	PJ_ASSERT_RETURN(this_port && frame, PJ_EINVAL);

    struct ws_speech_t *port = (struct ws_speech_t*)this_port;

    if(!port->wc) {
        //printf("no data\n");
        frame->type = PJMEDIA_FRAME_TYPE_NONE;
        return PJ_SUCCESS;
    }

    /*
    memcpy(frame->buf, flite->w->samples + flite->written_samples, PJMEDIA_PIA_SPF(&port->info)*2);
    flite->written_samples += PJMEDIA_PIA_SPF(&port->info);
    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
    */
    //printf("flite data written samples=%i\n", PJMEDIA_PIA_SPF(&port->info));

    return PJ_SUCCESS;
}

static pj_status_t on_destroy(pjmedia_port *this_port)
{
    struct ws_speech_t *port = (struct ws_speech_t*) this_port;

    return PJ_SUCCESS;
}


