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

/*
 * BFSK detector variant 2: uses spandsp's Goertzel implementation.
 *
 * spandsp's goertzel_result() returns the squared DFT magnitude for the
 * configured frequency.  For a full-scale sine wave of N samples the
 * expected result is approximately:
 *
 *   ((N * 32768.0 / sqrt(2)) ^ 2  [float build]
 *
 * We derive a per-instance threshold from the reference level at creation
 * time so the detector scales correctly with block size and sample rate.
 */

#include <pjmedia/bfsk_det2.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/log.h>

#include <spandsp.h>
#include <spandsp/expose.h>
#include <spandsp/tone_detect.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('b', 'f', 'd', '2')
#define THIS_FILE   "bfsk_det2.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

/*
 * Number of samples fed to each Goertzel block.
 * 102 samples @ 8000 Hz = ~12.75 ms per block, ~78 Hz resolution.
 * This matches spandsp's DTMF detector block size and is a good trade-off
 * between latency and frequency selectivity for telephony FSK.
 */
#define SAMPLES_PER_BLOCK 102

/*
 * Detection threshold expressed as a fraction of the full-scale reference
 * magnitude.  0.05 = the tone must carry at least 5% of full-scale energy
 * to be declared present.  Increase toward 1.0 for noisier environments.
 */
#define THRESHOLD_RATIO 0.05f

static pj_status_t bfsk_det2_put_frame(pjmedia_port *this_port, 
                                       pjmedia_frame *frame);
static pj_status_t bfsk_det2_on_destroy(pjmedia_port *this_port);

struct bfsk_det2
{
    struct pjmedia_port base;
    int clock_rate;
    int freq_zero;
    int freq_one;

    /*
     * *_in_progress: set when the corresponding tone was above threshold in
     * the previous block.  A bit is reported on the trailing edge.
     * Mutual exclusion is enforced by the dominant-frequency logic.
     */
    int zero_in_progress;
    int one_in_progress;

    /* spandsp Goertzel descriptors (static configuration) and states. */
    goertzel_descriptor_t desc_zero;
    goertzel_descriptor_t desc_one;
    goertzel_state_t      state_zero;
    goertzel_state_t      state_one;

    /*
     * Threshold in the same units as goertzel_result().
     * Computed at create time as THRESHOLD_RATIO * reference_magnitude.
     */
#if defined(SPANDSP_USE_FIXED_POINT)
    int32_t threshold;
#else
    float threshold;
#endif

    void (*bfsk_cb)(pjmedia_port*, void*, int);
    void *bfsk_cb_user_data;
};

/*
 * Compute the Goertzel result for a synthetic full-scale pure sine wave
 * at the given frequency so we can derive a calibrated threshold.
 */
#if defined(SPANDSP_USE_FIXED_POINT)
static int32_t compute_reference_magnitude(int freq, int sample_rate)
#else
static float compute_reference_magnitude(int freq, int sample_rate)
#endif
{
    goertzel_descriptor_t desc;
    goertzel_state_t      state;
    double t = 0.0;
    double tstep = 1.0 / sample_rate;
    int i;

    make_goertzel_descriptor(&desc, (float)freq, SAMPLES_PER_BLOCK);
    goertzel_init(&state, &desc);

    for (i = 0; i < SAMPLES_PER_BLOCK; i++) {
        int16_t s = (int16_t)(32767.0 * sin(2.0 * M_PI * freq * t));
        goertzel_sample(&state, s);
        t += tstep;
    }

    return goertzel_result(&state);
}

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
    struct bfsk_det2 *det;
    const pj_str_t name = pj_str("bfsk_det2");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
                     samples_per_frame && bits_per_sample == 16 && 
                     p_port != NULL, PJ_EINVAL);

    det = PJ_POOL_ZALLOC_T(pool, struct bfsk_det2);
    PJ_ASSERT_RETURN(det != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&det->base.info, &name, SIGNATURE, clock_rate,
                           channel_count, bits_per_sample, samples_per_frame);

    det->base.put_frame = &bfsk_det2_put_frame;
    det->base.on_destroy = &bfsk_det2_on_destroy;

    det->bfsk_cb = cb;
    det->bfsk_cb_user_data = user_data;

    det->clock_rate = clock_rate;
    det->freq_zero = freq_zero;
    det->freq_one = freq_one;

    /* Initialise spandsp Goertzel descriptors and states. */
    make_goertzel_descriptor(&det->desc_zero, (float)freq_zero, SAMPLES_PER_BLOCK);
    make_goertzel_descriptor(&det->desc_one,  (float)freq_one,  SAMPLES_PER_BLOCK);

    goertzel_init(&det->state_zero, &det->desc_zero);
    goertzel_init(&det->state_one,  &det->desc_one);

    /*
     * Derive threshold from the reference magnitude of each frequency.
     * Use the average of the two reference levels so a single threshold
     * applies to both detectors.
     */
#if defined(SPANDSP_USE_FIXED_POINT)
    {
        int32_t ref0 = compute_reference_magnitude(freq_zero, (int)clock_rate);
        int32_t ref1 = compute_reference_magnitude(freq_one,  (int)clock_rate);
        det->threshold = (int32_t)(THRESHOLD_RATIO * (float)((ref0 + ref1) / 2));
    }
#else
    {
        float ref0 = compute_reference_magnitude(freq_zero, (int)clock_rate);
        float ref1 = compute_reference_magnitude(freq_one,  (int)clock_rate);
        det->threshold = THRESHOLD_RATIO * ((ref0 + ref1) / 2.0f);
    }
#endif

    *p_port = &det->base;
    return PJ_SUCCESS;
}

static pj_status_t bfsk_det2_put_frame(pjmedia_port *this_port, 
                                       pjmedia_frame *frame)
{
    if (frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct bfsk_det2 *dport = (struct bfsk_det2*) this_port;

    int16_t *samples    = (int16_t*)frame->buf;
    int      num_samples = (int)(frame->size / sizeof(int16_t));
    int      pos;

    /*
     * Feed samples into the Goertzel filters one SAMPLES_PER_BLOCK chunk at
     * a time.  Any trailing samples that don't fill a complete block are
     * discarded — this matches how spandsp's own tone detectors behave and
     * avoids carrying partial state across frames.
     */
    for (pos = 0; pos + SAMPLES_PER_BLOCK <= num_samples; pos += SAMPLES_PER_BLOCK) {
        int j;
        for (j = 0; j < SAMPLES_PER_BLOCK; j++) {
            goertzel_sample(&dport->state_zero, samples[pos + j]);
            goertzel_sample(&dport->state_one,  samples[pos + j]);
        }

#if defined(SPANDSP_USE_FIXED_POINT)
        int32_t zero_power = goertzel_result(&dport->state_zero);
        int32_t  one_power = goertzel_result(&dport->state_one);
#else
        float zero_power = goertzel_result(&dport->state_zero);
        float  one_power = goertzel_result(&dport->state_one);
#endif

        /* goertzel_result() calls goertzel_reset() internally, so the
         * state is ready for the next block without further action. */

        /*
         * FSK: at most one frequency should be active at a time.
         * When both exceed the threshold pick the stronger one.
         */
        int zero, one;
        if (zero_power >= dport->threshold || one_power >= dport->threshold) {
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

        TRACE_((THIS_FILE, "zero_power=%f one_power=%f threshold=%f zero=%d one=%d",
                (double)zero_power, (double)one_power, (double)dport->threshold,
                zero, one));

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

static pj_status_t bfsk_det2_on_destroy(pjmedia_port *this_port)
{
    PJ_UNUSED_ARG(this_port);
    return PJ_SUCCESS;
}
