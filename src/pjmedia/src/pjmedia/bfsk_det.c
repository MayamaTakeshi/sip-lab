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


typedef struct {
    int sample_rate;
    int samples_per_frame;
    float target_frequency;
    float threshold;
    float target_magnitude;
} goertzel_detector_t;

// Function to pre-calculate the expected magnitude of the target frequency
float precalc_magnitude(float freq, int num_samples, int rate) {
    float t = 0;
    float tstep = 1.0f / rate;
    float samples[num_samples];

    for (int i = 0; i < num_samples; i++) {
        samples[i] = sinf(2 * M_PI * freq * t);
        t += tstep;
    }

    // Apply Goertzel on the generated sine samples
    int k = (int)(0.5f + ((float)num_samples * freq) / rate);
    float w = (2 * M_PI * k) / num_samples;
    float c = cosf(w);
    float s = sinf(w);
    float coeff = 2.0f * c;

    float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;

    for (int i = 0; i < num_samples; i++) {
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }

    float real = q1 - q2 * c;
    float imaginary = q2 * s;
    float mag_squared = real * real + imaginary * imaginary;

    return mag_squared;
}

// Goertzel function to detect frequency in a buffer
int goertzel_detect(goertzel_detector_t *detector, const int16_t *samples) {
    int num_samples = detector->samples_per_frame;

    // Normalize input samples to [-1, 1] range
    float normalized_samples[num_samples];
    for (int i = 0; i < num_samples; i++) {
        normalized_samples[i] = samples[i] / 32768.0f;
    }

    // Goertzel algorithm for target frequency
    int k = (int)(0.5f + ((float)num_samples * detector->target_frequency) / detector->sample_rate);
    float w = (2 * M_PI * k) / num_samples;
    float c = cosf(w);
    float s = sinf(w);
    float coeff = 2.0f * c;

    float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;

    for (int i = 0; i < num_samples; i++) {
        q0 = coeff * q1 - q2 + normalized_samples[i];
        q2 = q1;
        q1 = q0;
    }

    float real = q1 - q2 * c;
    float imaginary = q2 * s;
    float mag_squared = real * real + imaginary * imaginary;

    // Normalize the result based on the target magnitude
    float power_ratio = mag_squared / detector->target_magnitude;

    // Return if the power ratio exceeds the threshold
    printf("%x power_ratio: %f\n", (void*)detector, power_ratio);
    return power_ratio > detector->threshold;
}

// Initialize the Goertzel detector
void init_goertzel_detector(goertzel_detector_t *detector, float freq, int sample_rate, int samples_per_frame, float threshold) {
    detector->target_frequency = freq;
    detector->sample_rate = sample_rate;
    detector->samples_per_frame = samples_per_frame;
    detector->threshold = threshold;
    detector->target_magnitude = precalc_magnitude(freq, samples_per_frame, sample_rate);
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

    goertzel_detector_t *goertzel_zero;
    goertzel_detector_t *goertzel_one;

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

    det->goertzel_zero = PJ_POOL_ZALLOC_T(pool, goertzel_detector_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    det->goertzel_one = PJ_POOL_ZALLOC_T(pool, goertzel_detector_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    int sample_rate = clock_rate;

    init_goertzel_detector(det->goertzel_zero, det->freq_zero, sample_rate, samples_per_frame, threshold);
    init_goertzel_detector(det->goertzel_one, det->freq_one, sample_rate, samples_per_frame, threshold);

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

    int zero = goertzel_detect(dport->goertzel_zero, frame->buf);
    int  one = goertzel_detect(dport->goertzel_one,  frame->buf);

    printf("zero_in_progress=%i zero=%i\n", dport->zero_in_progress, zero);
    printf(" one_in_progress=%i  one=%i\n", dport->one_in_progress, one);

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
