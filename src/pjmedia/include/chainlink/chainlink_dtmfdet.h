#ifndef __CHAINLINK_DTMFDET_H__
#define __CHAINLINK_DTMFDET_H__

/**
 * @file chainlink_dtmfdet.h
 * @brief DTMF Detection port.
 */
#include <pjmedia/port.h>



/**
 * @defgroup PJMEDIA_DTMFDET_PORT DTMF Detection 
 * @ingroup PJMEDIA_PORT
 * @brief DTMF Detection port
 * @{
 */


PJ_BEGIN_DECL


/**
 * Create DTMF Detection port
 *
 * @param pool			Pool to allocate memory.
 * @param sampling_rate		Sampling rate of the port.
 * @param channel_count		Number of channels.
 * @param samples_per_frame	Number of samples per frame.
 * @param bits_per_sample	Number of bits per sample.
 * @param cb			Callback to be called upon detection of digits.o * @user_data			User data to be passed in the callback
 * @param p_port		Pointer to receive the port instance.
 *
 * @return			PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) chainlink_dtmfdet_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				void (*cb)(pjmedia_port*, 
					void *user_data, 
					char digit), 
				void *user_data,
				pjmedia_port **p_port);



PJ_END_DECL

/**
 * @}
 */


#endif	/* __CHAINLINK_DTMFDET_H__ */
