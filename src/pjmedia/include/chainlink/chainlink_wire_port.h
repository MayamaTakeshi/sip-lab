#ifndef __CHAINLINK_WIRE_PORT_H__
#define __CHAINLINK_WIRE_PORT_H__

/**
 * @file chainlink_wire_port.h
 */
#include <pjmedia/port.h>



/**
 * @defgroup CHAINLINK_WIRE_PORT
 * @ingroup PJMEDIA_PORT
 * @{
 */


PJ_BEGIN_DECL

#define CHAINLINK_WIRE_PORT_SIGNATURE   PJMEDIA_SIGNATURE('L', 'w', 'i', 'r')

/**
 * Create wire port (a port that just forward calls to get_frame and put_frame to its adjacent ports
 *
 * @param pool			Pool to allocate memory.
 * @param sampling_rate		Sampling rate of the port.
 * @param channel_count		Number of channels.
 * @param samples_per_frame	Number of samples per frame.
 * @param bits_per_sample	Number of bits per sample.
 * @param p_port		Pointer to receive the port instance.
 *
 * @return			PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) chainlink_wire_port_create( pj_pool_t *pool,
				unsigned sampling_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				pjmedia_port **p_port);



PJ_END_DECL

/**
 * @}
 */


#endif	/* __CHAINLINK_WIRE_PORT_H__ */
