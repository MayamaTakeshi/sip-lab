#ifndef __POCKETSPHINX_PORT_H__
#define __POCKETSPHINX_PORT_H__

#include <pjmedia/port.h>

PJ_BEGIN_DECL

PJ_DEF(pj_status_t) pjmedia_pocketsphinx_port_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
                void (*cb)(pjmedia_port*, void *user_data, char *transcript),
                void *cb_user_data,
				pjmedia_port **p_port);

PJ_END_DECL

#endif	/* __POCKETSPHINX_PORT_H__ */
