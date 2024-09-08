/* $Id: bfsk_det.c 0000 2024-09-08 08:11:53Z mayamatakeshi $ */
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

#include <pjmedia/bfsk_det.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>

#include <spandsp.h>
#include <spandsp/expose.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('b', 'f', 'd', 't')
#define THIS_FILE   "bfsk_det.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

static pj_status_t bfsk_det_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port);

struct bfsk_det
{
    struct pjmedia_port	base;
    fsk_rx_state_t state;
    void (*bfsk_cb)(pjmedia_port*, void*, int);
    void *bfsk_cb_user_data;
};

static void bfsk_det_bit_callback(void *user_data, int bit)
{
    printf("bfsk_det_bit_callback got bit=%i\n", bit);
    if(bit != 0 && bit != 1) return;

    struct bfsk_det *dport = (struct bfsk_det*)user_data;

    TRACE_((THIS_FILE, "bfsk_det bit detected: %c", bit)); 

    if(!dport->bfsk_cb) return;

    dport->bfsk_cb((pjmedia_port*)dport,
        dport->bfsk_cb_user_data,
        bit);
}
	
PJ_DEF(pj_status_t) pjmedia_bfsk_det_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				void (*cb)(pjmedia_port*, 
					void *user_data, 
					int bit), 
				void *user_data,
                int freq_zero,
                int freq_one,
                int min_level,
                int baud_rate,
				pjmedia_port **p_port)
{
    printf("pjmedia_bfsk_det_create\n");
    struct bfsk_det *det;
    fsk_spec_t *fsk_spec;

    printf("p1\n");
    const pj_str_t name = pj_str("bfsk_det");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    printf("p2\n");

    det = PJ_POOL_ZALLOC_T(pool, struct bfsk_det);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);
    printf("p3\n");

    pjmedia_port_info_init(&det->base.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    printf("p4\n");

    det->base.put_frame = &bfsk_det_put_frame;
    det->base.on_destroy = &bfsk_det_on_destroy;

    printf("p5\n");

    det->bfsk_cb = cb;
    det->bfsk_cb_user_data = user_data;

    fsk_spec = PJ_POOL_ZALLOC_T(pool, fsk_spec_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);
    printf("p6\n");

    fsk_spec->name = "bfsk";
    fsk_spec->freq_zero = freq_zero;
    fsk_spec->freq_one = freq_one;
    fsk_spec->min_level = min_level;
    fsk_spec->baud_rate = baud_rate;

    int sync_mode = 0;

    printf("p7\n");
    fsk_rx_state_t *res = fsk_rx_init(&det->state, fsk_spec, sync_mode, &bfsk_det_bit_callback, (void*)det);
    if(res != &det->state) {
       printf("fsx_rx_init failed\n"); 
    }

    //fsk_rx_init(&(det->state), &preset_fsk_specs[FSK_V21CH2], FSK_FRAME_MODE_SYNC, &bfsk_det_bit_callback, (void*)det);

    printf("p8\n");
    TRACE_((THIS_FILE, "bfsk_det created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    printf("fsk_rx_init done\n");

    *p_port = &det->base; 
    return PJ_SUCCESS;
}

static pj_status_t bfsk_det_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
    printf("bfsk_det_put_frame\n");
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct bfsk_det *dport = (struct bfsk_det*) this_port;

    printf("p=%x, size=%i\n", frame->buf, PJMEDIA_PIA_SPF(&dport->base.info));
    fsk_rx(&dport->state, (const pj_int16_t *)frame->buf,
		PJMEDIA_PIA_SPF(&dport->base.info));

    return PJ_SUCCESS;
}

static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}
