/* adapted from https://github.com/signalwire/freeswitch/blob/master/src/mod/asr_tts/mod_flite/mod_flite.c */

#include "flite_port.h"
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/pool.h>
#include <pj/string.h>

#include "siplab_constants.h"

#include <flite/flite.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('f', 'l', 'i', 't')
#define THIS_FILE   "flite_port.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

static pj_status_t flite_get_frame(pjmedia_port *port, 
					pjmedia_frame *frame);
static pj_status_t flite_on_destroy(pjmedia_port *port);


cst_voice *register_cmu_us_awb(void);
void unregister_cmu_us_awb(cst_voice * v);

cst_voice *register_cmu_us_kal(void);
void unregister_cmu_us_kal(cst_voice * v);

cst_voice *register_cmu_us_rms(void);
void unregister_cmu_us_rms(cst_voice * v);

cst_voice *register_cmu_us_slt(void);
void unregister_cmu_us_slt(cst_voice * v);

cst_voice *register_cmu_us_kal16(void);
void unregister_cmu_us_kal16(cst_voice * v);

static int initialized = 0;

static struct {
	cst_voice *awb;
	cst_voice *kal;
	cst_voice *rms;
	cst_voice *slt;
	cst_voice *kal16;
} globals;

struct flite_t {
    struct pjmedia_port base;
	void (*flite_cb)(pjmedia_port*, void*, int);
	void *flite_cb_user_data;
	cst_voice *v;
    int written_samples;
	cst_wave *w;
	char *buffer;
};

#define free_wave(w) if (w) {delete_wave(w) ; w = NULL; }
#define FLITE_BLOCK_SIZE 1024 * 32

PJ_DEF(pj_status_t) pjmedia_flite_port_create( pj_pool_t *pool,
				unsigned clock_rate,
				unsigned channel_count,
				unsigned samples_per_frame,
				unsigned bits_per_sample,
				void (*cb)(pjmedia_port*, 
					void *user_data, 
					int result), 
				void *user_data,
				const char *voice,
				pjmedia_port **p_port)
{
    struct flite_t *flite;
    const pj_str_t name = pj_str("flite_data");

    if(!initialized) {
        flite_init();
        globals.awb = register_cmu_us_awb();
        globals.kal = register_cmu_us_kal();
        globals.rms = register_cmu_us_rms();
        globals.slt = register_cmu_us_slt();
        globals.kal16 = register_cmu_us_kal16();
        initialized = 1;
    }

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    flite = PJ_POOL_ZALLOC_T(pool, struct flite_t);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&flite->base.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    flite->base.get_frame = &flite_get_frame;
    flite->base.on_destroy = &flite_on_destroy;

	if (!strcasecmp(voice, "awb")) {
		flite->v = globals.awb;
	} else if (!strcasecmp(voice, "kal")) {
/*  "kal" is 8kHz and the native rate is set to 16kHz
 *  so kal talks a little bit too fast ...
 *  for now: "symlink" kal to kal16
 */		flite->v = globals.kal16;
	} else if (!strcasecmp(voice, "rms")) {
		flite->v = globals.rms;
	} else if (!strcasecmp(voice, "slt")) {
		flite->v = globals.slt;
	} else if (!strcasecmp(voice, "kal16")) {
		flite->v = globals.kal16;
	} else {
		TRACE_((THIS_FILE, "Invalid voice"));
        return 0;
	}

    flite->flite_cb = cb;
    flite->flite_cb_user_data = user_data;

    TRACE_((THIS_FILE, "flite_device created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    *p_port = &flite->base; 
    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_flite_port_speak( pjmedia_port *port,
                                          const char *text,
                                          unsigned options) {
    struct flite_t *flite = (struct flite_t*)port;
    if(flite->w) {
        free_wave(flite->w);
    }
    
    flite->w = flite_text_to_wave(text, flite->v);
    if (flite->w->sample_rate != PJMEDIA_PIA_SRATE(&port->info)) {
		cst_wave_resample(flite->w, PJMEDIA_PIA_SRATE(&port->info));
    }
    flite->written_samples = 0;

    return PJ_SUCCESS;
}

// called when pjmedia needs data to be sent out
static pj_status_t flite_get_frame(pjmedia_port *port,
					pjmedia_frame *frame) {

	PJ_ASSERT_RETURN(port && frame, PJ_EINVAL);

    struct flite_t *flite = (struct flite_t*)port;

    if(!flite->w) {
        printf("flite no data\n");
        frame->type = PJMEDIA_FRAME_TYPE_NONE;
        return PJ_SUCCESS;
    }

    if (flite->written_samples >= flite->w->num_samples) {
        printf("flite no more data\n");
        free_wave(flite->w);
        frame->type = PJMEDIA_FRAME_TYPE_NONE;
        return PJ_SUCCESS;
    }

    memcpy(frame->buf, flite->w->samples + flite->written_samples, PJMEDIA_PIA_SPF(&port->info)*2);
    flite->written_samples += PJMEDIA_PIA_SPF(&port->info);
    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
    printf("flite data written samples=i\n", PJMEDIA_PIA_SPF(&port->info));

    return PJ_SUCCESS;
}

/*
 * Destroy port.
 */
static pj_status_t flite_on_destroy(pjmedia_port *port)
{
	printf("flite_on_destroy\n");

    struct flite_t *flite = (struct flite_t*)port;
    free_wave(flite->w);

    return PJ_SUCCESS;
}

