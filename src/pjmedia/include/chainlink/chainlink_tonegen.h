#ifndef __CHAINLINK_TONEGEN_PORT_H__
#define __CHAINLINK_TONEGEN_PORT_H__

#include <pjmedia/port.h>
#include <pjmedia/tonegen.h>

PJ_BEGIN_DECL

/**
 * Create an instance of tone generator with the specified parameters.
 * When the tone generator is first created, it will be loaded with the
 * default digit map.
 *
 * @param pool		    Pool to allocate memory for the port structure.
 * @param clock_rate	    Sampling rate.
 * @param channel_count	    Number of channels. Currently only mono and stereo
 *			    are supported.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Number of bits per sample. This version of PJMEDIA
 *			    only supports 16bit per sample.
 * @param options	    Option flags. Application may specify 
 *			    PJMEDIA_TONEGEN_LOOP to play the tone in a loop.
 * @param p_port	    Pointer to receive the port instance.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate
 *			    error code.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_create(pj_pool_t *pool,
					    unsigned clock_rate,
					    unsigned channel_count,
					    unsigned samples_per_frame,
					    unsigned bits_per_sample,
					    unsigned options,
					    pjmedia_port **p_port);


/**
 * Create an instance of tone generator with the specified parameters.
 * When the tone generator is first created, it will be loaded with the
 * default digit map.
 *
 * @param pool		    Pool to allocate memory for the port structure.
 * @param name		    Optional name for the tone generator.
 * @param clock_rate	    Sampling rate.
 * @param channel_count	    Number of channels. Currently only mono and stereo
 *			    are supported.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Number of bits per sample. This version of PJMEDIA
 *			    only supports 16bit per sample.
 * @param options	    Option flags. Application may specify 
 *			    PJMEDIA_TONEGEN_LOOP to play the tone in a loop.
 * @param p_port	    Pointer to receive the port instance.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate
 *			    error code.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_create2(pj_pool_t *pool,
					     const pj_str_t *name,
					     unsigned clock_rate,
					     unsigned channel_count,
					     unsigned samples_per_frame,
					     unsigned bits_per_sample,
					     unsigned options,
					     pjmedia_port **p_port);


/**
 * Check if the tone generator is still busy producing some tones.
 *
 * @param tonegen	    The tone generator instance.
 *
 * @return		    Non-zero if busy.
 */
PJ_DECL(pj_bool_t) chainlink_tonegen_is_busy(pjmedia_port *tonegen);


/**
 * Instruct the tone generator to stop current processing.
 *
 * @param tonegen	    The tone generator instance.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_stop(pjmedia_port *tonegen);


/**
 * Rewind the playback. This will start the playback to the first
 * tone in the playback list.
 *
 * @param tonegen	    The tone generator instance.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_rewind(pjmedia_port *tonegen);


/**
 * Instruct the tone generator to play single or dual frequency tones 
 * with the specified duration. The new tones will be appended to currently
 * playing tones, unless #pjmedia_tonegen_stop() is called before calling
 * this function. The playback will begin as soon as  the first get_frame()
 * is called to the generator.
 *
 * @param tonegen	    The tone generator instance.
 * @param count		    The number of tones in the array.
 * @param tones		    Array of tones to be played.
 * @param options	    Option flags. Application may specify 
 *			    PJMEDIA_TONEGEN_LOOP to play the tone in a loop.
 *
 * @return		    PJ_SUCCESS on success, or PJ_ETOOMANY if
 *			    there are too many digits in the queue.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_play(pjmedia_port *tonegen,
					  unsigned count,
					  const pjmedia_tone_desc tones[],
					  unsigned options);

/**
 * Instruct the tone generator to play multiple MF digits with each of
 * the digits having individual ON/OFF duration. Each of the digit in the
 * digit array must have the corresponding descriptor in the digit map.
 * The new tones will be appended to currently playing tones, unless 
 * #pjmedia_tonegen_stop() is called before calling this function. 
 * The playback will begin as soon as the first get_frame() is called 
 * to the generator.
 *
 * @param tonegen	    The tone generator instance.
 * @param count		    Number of digits in the array.
 * @param digits	    Array of MF digits.
 * @param options	    Option flags. Application may specify 
 *			    PJMEDIA_TONEGEN_LOOP to play the tone in a loop.
 *
 * @return		    PJ_SUCCESS on success, or PJ_ETOOMANY if
 *			    there are too many digits in the queue, or
 *			    PJMEDIA_RTP_EINDTMF if invalid digit is
 *			    specified.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_play_digits(pjmedia_port *tonegen,
						 unsigned count,
						 const pjmedia_tone_digit digits[],
						 unsigned options);


/**
 * Get the digit-map currently used by this tone generator.
 *
 * @param tonegen	    The tone generator instance.
 * @param m		    On output, it will be filled with the pointer to
 *			    the digitmap currently used by the tone generator.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_get_digit_map(pjmedia_port *tonegen,
						   const pjmedia_tone_digit_map **m);


/**
 * Set digit map to be used by the tone generator.
 *
 * @param tonegen	    The tone generator instance.
 * @param m		    Digitmap to be used by the tone generator.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) chainlink_tonegen_set_digit_map(pjmedia_port *tonegen,
						   pjmedia_tone_digit_map *m);


PJ_END_DECL

/**
 * @}
 */


#endif	/* __CHAINLINK_TONEGEN_PORT_H__ */

