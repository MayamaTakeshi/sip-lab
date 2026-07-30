#ifndef __CHAINLINK_XCORR_DET_H__
#define __CHAINLINK_XCORR_DET_H__

#include <pjmedia/port.h>

PJ_BEGIN_DECL

PJ_DECL(pj_status_t) chainlink_xcorr_det_create(
    pj_pool_t *pool,
    unsigned clock_rate,
    unsigned channel_count,
    unsigned samples_per_frame,
    unsigned bits_per_sample,
    const char *ref_file,
    double threshold,
    unsigned cooldown_ms,
    unsigned check_stride,
    void (*cb)(pjmedia_port*, void *user_data),
    void *user_data,
    pjmedia_port **p_port);

PJ_END_DECL

#endif
