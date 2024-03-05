#ifndef __FAX_PORT_H__
#define __FAX_PORT_H__

#include <pjmedia/port.h>

PJ_BEGIN_DECL

PJ_DECL(pj_status_t) pjmedia_fax_port_create( pj_pool_t *pool,
                unsigned clock_rate,
                unsigned channel_count,
                unsigned samples_per_frame,
                unsigned bits_per_sample,
                void (*cb)(pjmedia_port*,
                    void *user_data,
                    int result),
                void *user_data,
                int is_sender,
                const char *file,
                unsigned flags,
                pjmedia_port **p_port);


PJ_END_DECL

#endif	/* __FAX_PORT_H__ */
