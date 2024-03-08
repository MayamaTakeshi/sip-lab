#ifndef __FLITE_PORT_H__
#define __FLITE_PORT_H__

#include <pjmedia/port.h>

PJ_BEGIN_DECL

enum pjmedia_filte_option
{
  PJMEDIA_SPEECH_NO_LOOP = 1
};

PJ_DEF(pj_status_t) pjmedia_flite_port_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				const char *voice,
				pjmedia_port **p_port);

PJ_DEF(pj_status_t) pjmedia_flite_port_set_eof_cb(pjmedia_port *port,
                               void *user_data,
                               void (*cb)(pjmedia_port *port,
                                          void *usr_data));

PJ_DEF(pj_status_t) pjmedia_flite_port_speak( pjmedia_port *port,
                                          const char *text,
                                          unsigned options);

PJ_END_DECL

#endif	/* __FLITE_PORT_H__ */
