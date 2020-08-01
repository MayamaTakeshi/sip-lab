#include "chainlink.h"
#include "chainlink_wire_port.h"

#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>

#define SIGNATURE 	PJMEDIA_SIGNATURE('L', 'w', 'i', 'r')

static pj_status_t wire_get_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t wire_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t wire_on_destroy(pjmedia_port *this_port);


PJ_DEF(pj_status_t) chainlink_wire_port_create( pj_pool_t *pool,
					      unsigned sampling_rate,
					      unsigned channel_count,
					      unsigned samples_per_frame,
					      unsigned bits_per_sample,
					      pjmedia_port **p_port )
{
    struct chainlink *link;
    const pj_str_t name = pj_str("wire-port");

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    link = PJ_POOL_ZALLOC_T(pool, struct chainlink);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&link->port.info, &name, SIGNATURE, sampling_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    link->port.get_frame = &wire_get_frame;
    link->port.put_frame = &wire_put_frame;
    link->port.on_destroy = &wire_on_destroy;


    *p_port = &link->port;
    
    return PJ_SUCCESS;
}



/*
 * Put frame to file.
 */
static pj_status_t wire_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
	//printf("wire_put_frame %x\n", this_port);
	PJ_ASSERT_RETURN(this_port && frame, PJ_EINVAL);

	struct chainlink *link = (struct chainlink*)this_port;
	PJ_ASSERT_RETURN(link->next, PJ_EINVAL);
	PJ_ASSERT_RETURN(link->next->put_frame, PJ_EINVAL);

	return link->next->put_frame(link->next, frame);
	
}


/*
 * Get frame from file.
 */
static pj_status_t wire_get_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
	//printf("wire_get_frame %x\n", this_port);
	PJ_ASSERT_RETURN(this_port && frame, PJ_EINVAL);

	struct chainlink *link = (struct chainlink*)this_port;
	PJ_ASSERT_RETURN(link->next, PJ_EINVAL);
	PJ_ASSERT_RETURN(link->next->get_frame, PJ_EINVAL);

	pj_status_t s = link->next->get_frame(link->next, frame);
	//pj_perror(0, "", s, "");
	//printf("frame type=%i size=%i timestamp=%i bit_info=%i\n", frame->type, frame->size, frame->timestamp, frame->bit_info);	
	return s;
}


/*
 * Destroy port.
 */
static pj_status_t wire_on_destroy(pjmedia_port *this_port)
{
    PJ_UNUSED_ARG(this_port);
    return PJ_SUCCESS;
}
