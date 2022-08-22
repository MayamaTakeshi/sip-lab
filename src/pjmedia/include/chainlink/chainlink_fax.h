#ifndef __CHAINLINK_FAX_H__
#define __CHAINLINK_FAX_H__

#include <pjmedia/port.h>

PJ_BEGIN_DECL

PJ_DECL(pj_status_t) chainlink_fax_port_create( pj_pool_t *pool,
                unsigned clock_rate,
                unsigned channel_count,
                unsigned samples_per_frame,
                unsigned bits_per_sample,
                void (*cb)(pjmedia_port*,
                    void *user_data,
                    int result),
                void *user_data,
                int is_caller,
                int is_sender,
                const char *file,
                pjmedia_port **p_port);


PJ_END_DECL

#endif	/* __CHAINLINK_FAX_H__ */
