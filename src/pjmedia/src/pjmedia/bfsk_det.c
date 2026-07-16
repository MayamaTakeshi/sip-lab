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
#include <stdio.h>
#include <stdlib.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('b', 'f', 'd', 't')
#define THIS_FILE   "bfsk_det.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

/*
 * Goertzel-based BFSK detector.
 *
 * CHUNK_SIZE is the number of samples per Goertzel analysis block.
 * Frequency resolution = sample_rate / CHUNK_SIZE.
 *
 * Trade-off:
 *   - Larger CHUNK_SIZE → better frequency resolution, but longer analysis
 *     window.  If the window is longer than one bit period, adjacent bits
 *     bleed into each other and detection fails.
 *   - Smaller CHUNK_SIZE → poorer frequency resolution, more false detects.
 *
 * Default signal_duration (on_msec / off_msec) used by send_bfsk is 10 ms.
 * At 8000 Hz that is 80 samples per bit period.  We therefore need
 * CHUNK_SIZE well below 80 so that we see several chunks per tone burst.
 * 40 samples = 5 ms per chunk → resolution ~200 Hz, fits ~2 chunks per 10 ms
 * tone.  This is a reasonable compromise for typical FSK tone pairs that have
 * at least a few hundred Hz separation (e.g. 500/2000 Hz used in tests).
 */
#define CHUNK_SIZE 40

/*
 * THRESHOLD: normalized ratio of detected magnitude to reference magnitude.
 * A value of 0.3 means the incoming tone must be at least 30% as strong as
 * a full-scale sine wave at that frequency.  Tune upward to reduce false
 * positives in noisy environments, downward to detect weaker signals.
 */
#define THRESHOLD 0.3

/* Converted by ChatGPT from https://github.com/hackergrrl/goertzel/blob/master/index.js */

typedef struct {
    int freq;
    int sampleRate;
    int samplesPerFrame;
    double targetMagnitude;
} goertzel_t;

/*
 * Compute the expected Goertzel magnitude for a full-scale sine wave at
 * the given frequency and sample rate, using CHUNK_SIZE samples.  This is
 * used as a normalization reference so that goertzel_mag() returns a
 * dimensionless ratio in [0, 1] for a matching full-amplitude tone.
 */
static double precalcMagnitude(int freq, double rate) {
    double t = 0.0;
    double tstep = 1.0 / rate;
    double samples[CHUNK_SIZE];
    for (int i = 0; i < CHUNK_SIZE; i++) {
        samples[i] = sin(2 * M_PI * freq * t);
        t += tstep;
    }

    int k = (int)(0.5 + ((double)CHUNK_SIZE * freq) / rate);
    double w = (2 * M_PI / CHUNK_SIZE) * k;
    double c = cos(w);
    double s = sin(w);
    double coeff = 2.0 * c;

    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    for (int i = 0; i < CHUNK_SIZE; i++) {
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }

    double real = q1 - q2 * c;
    double imaginary = q2 * s;
    return real * real + imaginary * imaginary;
}

static void goertzel_det_init(goertzel_t *g, int freq, int sample_rate) {
    g->freq = freq;
    g->sampleRate = sample_rate;
    g->samplesPerFrame = CHUNK_SIZE;

    assert(g->sampleRate >= g->freq * 2);

    g->targetMagnitude = precalcMagnitude(g->freq, g->sampleRate);
}

/*
 * Compute the Goertzel magnitude of the given PCM block and return it as a
 * fraction of the reference magnitude.  Input is raw 16-bit little-endian
 * PCM; CHUNK_SIZE samples (2*CHUNK_SIZE bytes) are consumed.
 */
static float goertzel_mag(goertzel_t *g, void *samples) {
    float float_samples[CHUNK_SIZE];
    uint8_t *byteBuffer = (uint8_t *)samples;
    for (int i = 0; i < CHUNK_SIZE; i++) {
        int16_t sample = (int16_t)(byteBuffer[i * 2] | (byteBuffer[i * 2 + 1] << 8));
        float_samples[i] = sample / 32767.0f;
    }

    int k = (int)(0.5f + ((float)CHUNK_SIZE * g->freq) / g->sampleRate);
    float w = (2.0f * (float)M_PI / CHUNK_SIZE) * k;
    float c = cosf(w);
    float s = sinf(w);
    float coeff = 2.0f * c;

    float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;
    for (int i = 0; i < CHUNK_SIZE; i++) {
        q0 = coeff * q1 - q2 + float_samples[i];
        q2 = q1;
        q1 = q0;
    }

    float real = q1 - q2 * c;
    float imaginary = q2 * s;
    float magSquared = real * real + imaginary * imaginary;

    return (float)(magSquared / g->targetMagnitude);
}


static pj_status_t bfsk_det_put_frame(pjmedia_port *this_port, 
                                      pjmedia_frame *frame);
static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port);

struct bfsk_det
{
    struct pjmedia_port base;
    int clock_rate;
    int freq_zero;
    int freq_one;

    /*
     * *_in_progress tracks whether the corresponding tone was above threshold
     * in the previous chunk.  A bit is reported on the trailing edge (when
     * the tone drops below threshold) so that we emit exactly one event per
     * burst.  Only one of zero_in_progress / one_in_progress should be set
     * at a time; the dominant-frequency logic enforces this.
     */
    int zero_in_progress;
    int one_in_progress;

    goertzel_t *goertzel_zero;
    goertzel_t *goertzel_one;

    void (*bfsk_cb)(pjmedia_port*, void*, int);
    void *bfsk_cb_user_data;
};

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
    struct bfsk_det *det;
    const pj_str_t name = pj_str("bfsk_det");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
                     samples_per_frame && bits_per_sample == 16 && 
                     p_port != NULL, PJ_EINVAL);

    det = PJ_POOL_ZALLOC_T(pool, struct bfsk_det);
    PJ_ASSERT_RETURN(det != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&det->base.info, &name, SIGNATURE, clock_rate,
                           channel_count, bits_per_sample, samples_per_frame);

    det->base.put_frame = &bfsk_det_put_frame;
    det->base.on_destroy = &bfsk_det_on_destroy;

    det->bfsk_cb = cb;
    det->bfsk_cb_user_data = user_data;

    det->clock_rate = clock_rate;
    det->freq_zero = freq_zero;
    det->freq_one = freq_one;

    det->goertzel_zero = PJ_POOL_ZALLOC_T(pool, goertzel_t);
    PJ_ASSERT_RETURN(det->goertzel_zero != NULL, PJ_ENOMEM);

    det->goertzel_one = PJ_POOL_ZALLOC_T(pool, goertzel_t);
    PJ_ASSERT_RETURN(det->goertzel_one != NULL, PJ_ENOMEM);

    goertzel_det_init(det->goertzel_zero, det->freq_zero, (int)clock_rate);
    goertzel_det_init(det->goertzel_one,  det->freq_one,  (int)clock_rate);

    *p_port = &det->base;
    return PJ_SUCCESS;
}

static pj_status_t bfsk_det_put_frame(pjmedia_port *this_port, 
                                      pjmedia_frame *frame)
{
    if (frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct bfsk_det *dport = (struct bfsk_det*) this_port;

    int size = (int)frame->size;

    /* Process CHUNK_SIZE-sample blocks. */
    int num_chunks = size / 2 / CHUNK_SIZE;
    for (int i = 0; i < num_chunks; i++) {
        void *chunk = (uint8_t*)frame->buf + i * 2 * CHUNK_SIZE;
        float zero_power = goertzel_mag(dport->goertzel_zero, chunk);
        float  one_power = goertzel_mag(dport->goertzel_one,  chunk);

        /*
         * For FSK exactly one frequency should be active at a time.
         * If both exceed the threshold, treat the stronger one as active
         * and suppress the other to prevent simultaneous callbacks.
         */
        int zero, one;
        if (zero_power >= THRESHOLD || one_power >= THRESHOLD) {
            if (zero_power >= one_power) {
                zero = 1;
                one  = 0;
            } else {
                zero = 0;
                one  = 1;
            }
        } else {
            zero = 0;
            one  = 0;
        }

        TRACE_((THIS_FILE, "zero_power=%f one_power=%f zero=%d one=%d",
                zero_power, one_power, zero, one));

        /* Report bit=0 on trailing edge of zero tone. */
        if (dport->zero_in_progress && !zero) {
            dport->bfsk_cb((pjmedia_port*)dport, dport->bfsk_cb_user_data, 0);
            dport->zero_in_progress = 0;
        } else {
            dport->zero_in_progress = zero;
        }

        /* Report bit=1 on trailing edge of one tone. */
        if (dport->one_in_progress && !one) {
            dport->bfsk_cb((pjmedia_port*)dport, dport->bfsk_cb_user_data, 1);
            dport->one_in_progress = 0;
        } else {
            dport->one_in_progress = one;
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t bfsk_det_on_destroy(pjmedia_port *this_port)
{
    PJ_UNUSED_ARG(this_port);
    return PJ_SUCCESS;
}
