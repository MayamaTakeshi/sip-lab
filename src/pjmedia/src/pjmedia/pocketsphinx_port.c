/* $Id: pocketsphinx_port.c 0000 2024-03-09 mayamatakeshi $ */
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

#include <pocketsphinx_port.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>

#include <pocketsphinx.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('p', 'i', 'n', 'x')
#define THIS_FILE   "pocketsphinx_port.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

static pj_status_t pocketsphinx_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t pocketsphinx_on_destroy(pjmedia_port *this_port);

struct pocketsphinx_t
{
    struct pjmedia_port	base;

    ps_decoder_t *decoder;
    ps_config_t *config;
    ps_endpointer_t *ep;

    unsigned in_spf;
    unsigned out_spf;

    short *samples;
    unsigned sample_count;

    pj_bool_t subscribed;
    void (*cb)(pjmedia_port*, void*, char*);
    void *cb_user_data;
};

static pj_status_t speech_on_event(pjmedia_event *event,
                                 void *user_data)
{
    struct pocketsphinx_t *port = (struct pocketsphinx_t*)user_data;

    if (event->type == PJMEDIA_EVENT_CALLBACK) {
        if (port->cb)
            (*port->cb)(&port->base, port->base.port_data.pdata, "transcript");
    }
    
    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_pocketsphinx_port_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
                void (*cb)(pjmedia_port*, void *user_data, char digit),
                void *cb_user_data,
				pjmedia_port **p_port)
{
    struct pocketsphinx_t *port;
    const pj_str_t name = pj_str("pocketsphinx");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count == 1 && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    port = PJ_POOL_ZALLOC_T(pool, struct pocketsphinx_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&port->base.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    port->base.put_frame = &pocketsphinx_put_frame;
    port->base.on_destroy = &pocketsphinx_on_destroy;

    port->cb = cb;
    port->cb_user_data = cb_user_data;

    port->config = ps_config_init(NULL);
    ps_default_search_args(port->config);
    
    if ((port->decoder = ps_init(port->config)) == NULL) {
        TRACE_((THIS_FILE, "pocketsphinx port: decoder init failed\n"));
        return !PJ_SUCCESS;
    }

    if ((port->ep = ps_endpointer_init(0, 0.0, 0, clock_rate, 0)) == NULL) {
        TRACE_((THIS_FILE, "pocketsphinx port: endpointer init failed\n"));
        return !PJ_SUCCESS;
    }

    port->in_spf = samples_per_frame;
    port->out_spf = ps_endpointer_frame_size(port->ep);

    port->samples = (short*) pj_pool_alloc(pool, port->out_spf * sizeof(short));
    if (port->samples == NULL) {
        TRACE_(("Failed to allocate buffer for samples\n"));
        return !PJ_SUCCESS;
    }

    TRACE_((THIS_FILE, "pocketsphinx port created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    *p_port = &port->base; 
    return PJ_SUCCESS;
}

void feed(struct pocketsphinx_t *port, short *frame) {
    const int16 *speech;
    int prev_in_speech = ps_endpointer_in_speech(port->ep);
    speech = ps_endpointer_process(port->ep, frame);
    if (speech != NULL) {
        const char *hyp;
        if (!prev_in_speech) {
            printf("Speech start at %.2f\n", ps_endpointer_speech_start(port->ep));
            ps_start_utt(port->decoder);
        }
        if (ps_process_raw(port->decoder, speech, port->out_spf, FALSE, FALSE) < 0) {
            TRACE_(("ps_process_raw() failed\n"));
            return;
        }
        if ((hyp = ps_get_hyp(port->decoder, NULL)) != NULL) {
            printf("PARTIAL RESULT: %s\n", hyp);
        }
        if (!ps_endpointer_in_speech(port->ep)) {
            printf(stdout, "Speech end at %.2f\n", ps_endpointer_speech_end(port->ep));
            ps_end_utt(port->decoder);
            if ((hyp = ps_get_hyp(port->decoder, NULL)) != NULL) {
                printf("%s\n", hyp);
            }
        }
    }
}

unsigned feed_one(struct pocketsphinx_t *port, pjmedia_frame *frame){
    unsigned used_samples = port->out_spf - port->sample_count;
    memcpy((short*)port->samples + port->sample_count, frame->buf, used_samples * sizeof(short));
    feed(port, port->samples);
    port->sample_count = 0;
    return used_samples;
}

void feed_all(struct pocketsphinx_t *port, pjmedia_frame *frame) {
    unsigned samples = frame->size / 2;
    unsigned used_samples = 0;
   if(port->sample_count > 0) {
       used_samples = feed_one(port, frame); 
    }

    short *out_frame = frame->buf + used_samples;
    samples -= used_samples;
    unsigned count = 0;
    while(samples >= port->out_spf) {
        feed(port, out_frame);
        count++;
        out_frame += (count * port->out_spf);
        samples -= port->out_spf;
    }

    if(samples) {
        memcpy(port->samples, out_frame, samples * sizeof(short));
        port->sample_count = samples;
    }
}

static pj_status_t pocketsphinx_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct pocketsphinx_t *port = (struct pocketsphinx_t*) this_port;

    if(port->in_spf == port->out_spf) {
        feed(port, (short*)frame->buf);
        return;
    }

    if(port->in_spf > port->out_spf) {
        feed_all(port, frame);
        return;
    }

    unsigned samples = frame->size / 2;
    if(samples + port->sample_count >= port->out_spf) {
        // enough to feed once
        feed_one(port, frame);
        return;
    }
            
    // not enough to feed. 
    memcpy((short*)port->samples + port->sample_count, frame->buf, samples * sizeof(short));
    port->sample_count += samples;

    return PJ_SUCCESS;
}

/*
 * Destroy port.
 */
static pj_status_t pocketsphinx_on_destroy(pjmedia_port *this_port)
{
    struct pocketsphinx_t *port = (struct pocketsphinx_t*) this_port;

    ps_endpointer_free(port->ep);
    ps_free(port->decoder);
    ps_config_free(port->config);
 
    return PJ_SUCCESS;
}


