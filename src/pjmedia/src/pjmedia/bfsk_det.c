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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Converted by ChatGPT from https://github.com/hackergrrl/goertzel/blob/master/index.js

typedef struct {
    double freq;
    double sampleRate;
    int samplesPerFrame;
    double targetMagnitude;  // Store the precomputed magnitude
} goertzel_t;

double precalcMagnitude(double freq, int numSamples, double rate) {
    double t = 0.0;
    double tstep = 1.0 / rate;
    double *samples = (double *)malloc(numSamples * sizeof(double));
    for (int i = 0; i < numSamples; i++) {
        samples[i] = sin(2 * M_PI * freq * t);
        t += tstep;
    }

    int k = (int)(0.5 + (numSamples * freq) / rate);
    double w = (2 * M_PI / numSamples) * k;
    double c = cos(w);
    double s = sin(w);
    double coeff = 2.0 * c;

    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    for (int i = 0; i < numSamples; i++) {
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }

    double real = q1 - q2 * c;
    double imaginary = q2 * s;
    double magSquared = real * real + imaginary * imaginary;

    free(samples);
    return magSquared;
}

#define THRESHOLD 0.9

void goertzel_det_init(goertzel_t *g, int freq, int sample_rate, int samples_per_frame) {
    g->freq = freq;
    g->sampleRate = sample_rate;
    g->samplesPerFrame = samples_per_frame;

    assert(g->sampleRate >= g->freq * 2);

    g->samplesPerFrame = (int)floor(g->samplesPerFrame);

    // Precompute the target magnitude and store it in the struct
    g->targetMagnitude = precalcMagnitude(g->freq, g->samplesPerFrame, g->sampleRate);
    printf("Target Magnitude: %f\n", g->targetMagnitude);
}

double goertzel_mag(goertzel_t *g, double *samples, int length) {
    printf("goertzel_mag\m");
    int k = (int)(0.5 + (length * g->freq) / g->sampleRate);
    double w = (2 * M_PI / length) * k;
    double c = cos(w);
    double s = sin(w);
    double coeff = 2.0 * c;

    printf("k = %d, w = %f, coeff = %f\n", k, w, coeff);

    double q0 = 0.0, q1 = 0.0, q2 = 0.0;

    for (int i = 0; i < length; i++) {
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }

    printf("coeff = %f, q0 = %f, q1 = %f, q2 = %f\n", coeff, q0, q1, q2);

    double real = q1 - q2 * c;
    double imaginary = q2 * s;
    double magSquared = real * real + imaginary * imaginary;

    return magSquared / g->targetMagnitude;
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

    int zero_in_progress;
    int one_in_progress;

    goertzel_t *goertzel_zero;
    goertzel_t *goertzel_one;

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

    float threshold = 0.1;

    det->goertzel_zero = PJ_POOL_ZALLOC_T(pool, goertzel_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    det->goertzel_one = PJ_POOL_ZALLOC_T(pool, goertzel_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    int sample_rate = clock_rate;

    goertzel_det_init(det->goertzel_zero, det->freq_zero, sample_rate, samples_per_frame);
    goertzel_det_init(det->goertzel_one, det->freq_one, sample_rate, samples_per_frame);

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
    printf("bfsk_det put_frame\n");
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct bfsk_det *dport = (struct bfsk_det*) this_port;

    int size = PJMEDIA_PIA_SPF(&dport->base.info);
    int bps = PJMEDIA_PIA_BITS(&dport->base.info);

    printf("p=%x, size=%i clock_rate=%i bits_per_sample=%i\n", frame->buf, size, dport->clock_rate, bps);

    int16_t * samples = (int16_t*)frame->buf;

    printf("Buffer contents:\n");
    for (int i = 0; i < size; i++) {
        printf("%02x", samples[i] & 0xFF);
        printf("%02x", samples[i] >> 8 & 0xFF);
    }
    printf("\n");

    double zero_power = goertzel_mag(dport->goertzel_zero, frame->buf, size);
    double  one_power = goertzel_mag(dport->goertzel_one,  frame->buf, size);

    int zero = zero_power > THRESHOLD;
    int  one =  one_power > THRESHOLD;

    printf("zero_power=%f zero_in_progress=%i zero=%i\n", zero_power, dport->zero_in_progress, zero);
    printf(" one_power=%f  one_in_progress=%i  one=%i\n", one_power, dport->one_in_progress, one);


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

static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}
