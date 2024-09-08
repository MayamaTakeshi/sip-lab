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

#include <math.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('b', 'f', 'd', 't')
#define THIS_FILE   "bfsk_det.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

#define EXTINCTION_THRESHOLD 0.001 


// Goertzel algorithm implementation
double goertzel(int16_t *samples, int num_samples, int target_freq, int sample_rate) {
    double normalized_freq = (double)target_freq / sample_rate;
    double omega = 2.0 * M_PI * normalized_freq;
    double coeff = 2.0 * cos(omega);

    double s_prev = 0.0;
    double s_prev2 = 0.0;

    for (int i = 0; i < num_samples; i++) {
        double s = samples[i] + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }

    double power = (s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2) / num_samples;

    return power;
}


static pj_status_t bfsk_det_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port);

struct bfsk_det
{
    struct pjmedia_port	base;
    int clock_rate;
    int freq_zero;
    int freq_one;

    int current_bit;

    void (*bfsk_cb)(pjmedia_port*, void*, int);
    void *bfsk_cb_user_data;
};

/*
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
*/
	
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
				pjmedia_port **p_port)
{
    printf("pjmedia_bfsk_det_create\n");
    struct bfsk_det *det;

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

    det->base.put_frame = &bfsk_det_put_frame;
    det->base.on_destroy = &bfsk_det_on_destroy;

    printf("p5\n");

    det->bfsk_cb = cb;
    det->bfsk_cb_user_data = user_data;

    printf("p6\n");

    det->clock_rate = clock_rate;
    det->freq_zero = freq_zero;
    det->freq_one = freq_one;
    det->current_bit = -1;

    printf("p7\n");

    TRACE_((THIS_FILE, "bfsk_det created: clock_rate=%u channel_count=%u samples_per_frame=%u bits_per_frame=%u", clock_rate, 
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

    int size = PJMEDIA_PIA_SPF(&dport->base.info);
    int bps = PJMEDIA_PIA_BITS(&dport->base.info);

    printf("p=%x, size=%i clock_rate=%i bits_per_sample=%i\n", frame->buf, size, dport->clock_rate, bps);

    int16_t * samples = (int16_t*)frame->buf;

    printf("Buffer contents:\n");
    for (int i = 0; i < size; i++) {
        printf("%04x ", samples[i] & 0xFFFF);
    }
    printf("\n");

    double zero_power = goertzel(frame->buf, size, dport->freq_zero, dport->clock_rate);
    double one_power = goertzel(frame->buf, size, dport->freq_one, dport->clock_rate);

    printf("zero_power=%f one_power=%f\n", zero_power, one_power);

    // Calculate total signal power (sum of both detected frequencies)
    double total_power = zero_power + one_power;

    // Check for signal extinction
    if (total_power < EXTINCTION_THRESHOLD) {
        if(dport->current_bit != -1) {
            dport->bfsk_cb((pjmedia_port*)dport, dport->bfsk_cb_user_data, dport->current_bit);
            dport->current_bit = -1;
        }
    } else {
        int dominant = zero_power > one_power ? 0 : 1;
        if(dport->current_bit != -1) {
            if(dominant != dport->current_bit) {
                dport->bfsk_cb((pjmedia_port*)dport, dport->bfsk_cb_user_data, dport->current_bit);
                dport->current_bit = dominant;
            }
        } else {
            dport->current_bit = dominant;
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}
