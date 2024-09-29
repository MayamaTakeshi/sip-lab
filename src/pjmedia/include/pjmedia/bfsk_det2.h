#ifndef __PJMEDIA_BFSK_DET2_H__
#define __PJMEDIA_BFSK_DET2_H__

#include <pjmedia/port.h>

PJ_BEGIN_DECL

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
                pjmedia_port **p_port);

PJ_END_DECL

#endif	/* __PJMEDIA_BFSK_DET2_H__ */
