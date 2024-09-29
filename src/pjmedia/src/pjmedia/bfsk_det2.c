/* $Id: bfsk_det2.c 0000 2024-09-29 09:41:55Z mayamatakeshi $ */
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

#include <pjmedia/bfsk_det2.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>

#include <spandsp.h>
#include <spandsp/expose.h>
#include <spandsp/tone_detect.h>

#include <math.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('b', 'f', 'd', '2')
#define THIS_FILE   "bfsk_det2.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static pj_status_t bfsk_det2_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t bfsk_det2_on_destroy(pjmedia_port *this_port);

struct bfsk_det2
{
    struct pjmedia_port	base;
    int clock_rate;
    int freq_zero;
    int freq_one;

    int zero_in_progress;
    int one_in_progress;

    int32_t threshold;
    int32_t energy;

    goertzel_descriptor_t desc_zero;
    goertzel_descriptor_t desc_one;

    goertzel_state_t *goertzel_zero;
    goertzel_state_t *goertzel_one;

    void (*bfsk_cb)(pjmedia_port*, void*, int);
    void *bfsk_cb_user_data;
};

#define DTMF_SAMPLES_PER_BLOCK                  102

PJ_DEF(pj_status_t) pjmedia_bfsk_det2_create( pj_pool_t *pool,
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
				pjmedia_port **p_port)
{
    //printf("pjmedia_bfsk_det2_create\n");
    struct bfsk_det2 *det;

    const pj_str_t name = pj_str("bfsk_det2");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    det = PJ_POOL_ZALLOC_T(pool, struct bfsk_det2);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&det->base.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    det->base.put_frame = &bfsk_det2_put_frame;
    det->base.on_destroy = &bfsk_det2_on_destroy;

    det->bfsk_cb = cb;
    det->bfsk_cb_user_data = user_data;

    det->clock_rate = clock_rate;
    det->freq_zero = freq_zero;
    det->freq_one = freq_one;

    make_goertzel_descriptor(&det->desc_zero, freq_zero, DTMF_SAMPLES_PER_BLOCK);
    make_goertzel_descriptor(&det->desc_one, freq_one, DTMF_SAMPLES_PER_BLOCK);

    goertzel_init(&det->goertzel_zero, &det->desc_zero);
    goertzel_init(&det->goertzel_one, &det->desc_one);

    int sample_rate = clock_rate;

    //printf("bfsk_det2: clock_rate=%u channel_count=%u samples_per_frame=%u bits_per_frame=%u", clock_rate, channel_count, samples_per_frame, bits_per_sample);

    goertzel_det_init(&det->goertzel_zero, det->freq_zero, sample_rate);
    goertzel_det_init(&det->goertzel_one, det->freq_one, sample_rate);

    *p_port = &det->base; 
    return PJ_SUCCESS;
}

static pj_status_t bfsk_det2_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
    //printf("bfsk_det2 put_frame\n");
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct bfsk_det2 *dport = (struct bfsk_det2*) this_port;

    int size = frame->size;
    int bps = PJMEDIA_PIA_BITS(&dport->base.info);

    //printf("p=%x, size=%i clock_rate=%i bits_per_sample=%i\n", frame->buf, size, dport->clock_rate, bps);

    int16_t * samples = (int16_t*)frame->buf;
    int16_t * num_samples = frame->size/2;

    /*
    printf("Buffer contents:\n");
    for (int i = 0; i < size; i++) {
        printf("%02x", samples[i] & 0xFF);
        printf("%02x", samples[i] >> 8 & 0xFF);
    }
    printf("\n");
    */

#if defined(SPANDSP_USE_FIXED_POINT)
    int32_t row_energy[4];
    int32_t col_energy[4];
    int16_t xamp;
    float famp;
#else
    float row_energy[4];
    float col_energy[4];
    float xamp;
    float famp;
#endif

    float v1;
    int i;
    int j;
    int sample;
    int limit;
    uint8_t hit;

    for (sample = 0;  sample < num_samples;  sample = limit)
    {
        limit = num_samples;
        for (j = sample;  j < limit;  j++)
        {
            xamp = samples[j];
            xamp = goertzel_preadjust_amp(xamp);
#if defined(SPANDSP_USE_FIXED_POINT)
            dport->energy += ((int32_t) xamp*xamp);
#else
            dport->energy += xamp*xamp;
#endif
            goertzel_samplex(&dport->goertzel_zero, xamp);
            goertzel_samplex(&dport->goertzel_one, xamp);
        }
    }
    int32_t zero_power = goertzel_result(&dport->goertzel_zero);
    int32_t one_power = goertzel_result(&dport->goertzel_one);

    int zero = zero_power > dport->threshold;
    int  one =  one_power > dport->threshold;

    printf("zero_power=%f zero_in_progress=%i zero=%i threshold=%f\n", zero_power, dport->zero_in_progress, zero, dport->threshold);
    printf(" one_power=%f  one_in_progress=%i  one=%i threshold=%f\n", one_power, dport->one_in_progress, one, dport->threshold);

    // Check for zero signal extinction
    if(dport->zero_in_progress && zero == 0) {
        printf("notifying bit=0\n");
        dport->bfsk_cb((pjmedia_port*)dport, dport->bfsk_cb_user_data, 0);
        dport->zero_in_progress = 0;
    } else {
        dport->zero_in_progress = zero;
    }

    // Check for one signal extinction
    if(dport->one_in_progress && one == 0) {
        printf("notifying bit=1\n");
        dport->bfsk_cb((pjmedia_port*)dport, dport->bfsk_cb_user_data, 1);
        dport->one_in_progress = 0;
    } else {
        dport->one_in_progress = one;
    }

    return PJ_SUCCESS;
}

static pj_status_t bfsk_det2_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}
