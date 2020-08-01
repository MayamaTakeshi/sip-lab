#include "chainlink.h"
#include "chainlink_dtmfdet.h"

#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>

#define INT16_MAX 0x7fff
#define INT16_MIN (-INT16_MAX - 1) 
#include <spandsp.h>
#include <spandsp/expose.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('L', 'd', 't', 'd')
#define THIS_FILE   "chainlink_dtmfdet.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

static pj_status_t dtmfdet_get_frame(pjmedia_port *this_port, 
					pjmedia_frame *frame);
static pj_status_t dtmfdet_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t dtmfdet_on_destroy(pjmedia_port *this_port);

struct dtmfdet
{
	struct chainlink link;
	dtmf_rx_state_t state;
    	void (*dtmf_cb)(pjmedia_port*, void*, char);
    	void *dtmf_cb_user_data;
};

static void dtmfdet_digit_callback(void *user_data, int code, int level, int delay)
{
	struct dtmfdet *dport = (struct dtmfdet*) user_data;
	if(!code) return;

    	TRACE_((THIS_FILE, "dtmfdet digit detected: %c", code)); 

	if(!dport->dtmf_cb) return;
	
	dport->dtmf_cb((pjmedia_port*)dport,
			dport->dtmf_cb_user_data,
			code);
}
	
PJ_DEF(pj_status_t) chainlink_dtmfdet_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				void (*cb)(pjmedia_port*, 
					void *user_data, 
					char digit), 
				void *user_data,
				pjmedia_port **p_port)
{
    struct dtmfdet *dd;
    const pj_str_t name = pj_str("dtmfdet");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    dd = PJ_POOL_ZALLOC_T(pool, struct dtmfdet);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&dd->link.port.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    dd->link.port.get_frame = &dtmfdet_get_frame;
    dd->link.port.put_frame = &dtmfdet_put_frame;
    dd->link.port.on_destroy = &dtmfdet_on_destroy;

    dd->dtmf_cb = cb;
    dd->dtmf_cb_user_data = user_data;

    dtmf_rx_init(&dd->state, NULL, NULL);
    dtmf_rx_set_realtime_callback(&dd->state,
					&dtmfdet_digit_callback,
					(void*)dd);	 

    TRACE_((THIS_FILE, "dtmfdet created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    *p_port = &dd->link.port; 
    return PJ_SUCCESS;
}

static pj_status_t dtmfdet_get_frame(pjmedia_port *this_port,
					pjmedia_frame *frame) {
	PJ_ASSERT_RETURN(this_port && frame, PJ_EINVAL);

	struct dtmfdet *dport = (struct dtmfdet*)this_port;
	return dport->link.next->get_frame(dport->link.next, frame);
}

static pj_status_t dtmfdet_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;

    struct dtmfdet *dport = (struct dtmfdet*) this_port;
    dtmf_rx(&dport->state, (const pj_int16_t*)frame->buf,
		PJMEDIA_PIA_SPF(&dport->link.port.info));

    return dport->link.next->put_frame(dport->link.next, frame);
}

/*
 * Destroy port.
 */
static pj_status_t dtmfdet_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}


