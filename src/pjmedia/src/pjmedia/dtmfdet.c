/* $Id: dtmfdet.c 0000 2009-05-01 11:26:53Z mayamatakeshi $ */
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

#include <pjmedia/dtmfdet.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>

#include <spandsp.h>
#include <spandsp/expose.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('d', 't', 'd', 't')
#define THIS_FILE   "dtmfdet.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

static pj_status_t dtmfdet_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t dtmfdet_on_destroy(pjmedia_port *this_port);

struct dtmfdet
{
	struct pjmedia_port	base;
	dtmf_rx_state_t state;
    	void (*dtmf_cb)(pjmedia_port*, void*, char);
    	void *dtmf_cb_user_data;
};

static void dtmfdet_digit_callback(void *user_data, int code, int level, int delay)
{
	struct dtmfdet *dport = (struct dtmfdet*) user_data;
	if(!code) return;

    	TRACE_((THIS_FILE, "dtmfdet digit detected: %c", code)); 

	if(!dport->dtmf_cb) return;
	
	dport->dtmf_cb((pjmedia_port*)dport,
			dport->dtmf_cb_user_data,
			code);
}
	
PJ_DEF(pj_status_t) pjmedia_dtmfdet_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				void (*cb)(pjmedia_port*, 
					void *user_data, 
					char digit), 
				void *user_data,
				pjmedia_port **p_port)
{
    struct dtmfdet *dtmfdet;
    const pj_str_t name = pj_str("dtmfdet");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    dtmfdet = PJ_POOL_ZALLOC_T(pool, struct dtmfdet);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&dtmfdet->base.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    dtmfdet->base.put_frame = &dtmfdet_put_frame;
    dtmfdet->base.on_destroy = &dtmfdet_on_destroy;

    dtmfdet->dtmf_cb = cb;
    dtmfdet->dtmf_cb_user_data = user_data;

    dtmf_rx_init(&dtmfdet->state, NULL, NULL);
    dtmf_rx_set_realtime_callback(&dtmfdet->state,
					&dtmfdet_digit_callback,
					(void*)dtmfdet);	 

    TRACE_((THIS_FILE, "dtmfdet created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    *p_port = &dtmfdet->base; 
    return PJ_SUCCESS;
}

static pj_status_t dtmfdet_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct dtmfdet *dport = (struct dtmfdet*) this_port;
    dtmf_rx(&dport->state, (const pj_int16_t*)frame->buf,
		PJMEDIA_PIA_SPF(&dport->base.info));

    return PJ_SUCCESS;

}

/*
 * Destroy port.
 */
static pj_status_t dtmfdet_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}


