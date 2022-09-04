#include "chainlink.h"
#include "chainlink_fax.h"

#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/pool.h>
#include <pj/string.h>

#include "siplab_constants.h"

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#define SIGNATURE   PJMEDIA_SIGNATURE('L', 'f', 'a', 'x')
#define THIS_FILE   "chainlink_fax.c"

#if 0
#   define TRACE_(expr)	PJ_LOG(4,expr)
#else
#   define TRACE_(expr)
#endif

#define FAX_DATA_CHUNK 320
#define T38_DATA_CHUNK 160


#define FAX_FLAG_TRANSMIT_ON_IDLE 1

enum
{
    /*! No compression */
    T30_SUPPORT_NO_COMPRESSION = 0x01,
    /*! T.1 1D compression */
    T30_SUPPORT_T4_1D_COMPRESSION = 0x02,
    /*! T.4 2D compression */
    T30_SUPPORT_T4_2D_COMPRESSION = 0x04,
    /*! T.6 2D compression */
    T30_SUPPORT_T6_COMPRESSION = 0x08,
    /*! T.85 monochrome JBIG compression */
    T30_SUPPORT_T85_COMPRESSION = 0x10,
    /*! T.43 colour JBIG compression */
    T30_SUPPORT_T43_COMPRESSION = 0x20,
    /*! T.45 run length colour compression */
    T30_SUPPORT_T45_COMPRESSION = 0x40,
    /*! T.81 + T.30 Annex E colour JPEG compression */
    T30_SUPPORT_T81_COMPRESSION = 0x80,
    /*! T.81 + T.30 Annex K colour sYCC-JPEG compression */
    T30_SUPPORT_SYCC_T81_COMPRESSION = 0x100,
    /*! T.88 monochrome JBIG2 compression */
    T30_SUPPORT_T88_COMPRESSION = 0x200
};


static pj_status_t fax_get_frame(pjmedia_port *this_port, 
					pjmedia_frame *frame);
static pj_status_t fax_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame);
static pj_status_t fax_on_destroy(pjmedia_port *this_port);

struct fax_device
{
	struct chainlink link;
	fax_state_t fax;
	void (*fax_cb)(pjmedia_port*, void*, int);
	void *fax_cb_user_data;
	int is_sender;

    pj_lock_t	       *lock;
};

static int phase_b_handler(t30_state_t* s, void* user_data, int result)
{
	printf("fax phase_b_handler user_data=%p result=%i\n", user_data, result);
    return T30_ERR_OK;
}

static int phase_d_handler(t30_state_t* s, void* user_data, int result)
{
	printf("fax phase_b_handler user_data=%p result=%i\n", user_data, result);
    return T30_ERR_OK;
}

static void phase_e_handler(t30_state_t* s, void* user_data, int result)
{
	printf("fax phase_e_handler user_data=%p result=%i\n", user_data, result);

    if (!user_data) {
        printf("not user_data\n");    
        return;
    }

    struct fax_device *fd = (struct fax_device*)user_data;
	if(!fd->fax_cb) {
        printf("not fax_cb\n");
        return;
    }

    printf("sending result event\n");
	fd->fax_cb((pjmedia_port*)fd, fd->fax_cb_user_data, result);
}

static int document_handler(t30_state_t* s, void* user_data, int result)
{
	printf("fax document_handler user_data=%p result=%i\n", user_data, result);

    if (!user_data) return 0;

    struct fax_device *fd = (struct fax_device*)user_data;
	if(!fd->fax_cb) return 0;

	//fd->fax_cb((pjmedia_port*)fd, fd->fax_cb_user_data, result);

    return 0;
}

PJ_DEF(pj_status_t) chainlink_fax_port_create( pj_pool_t *pool,
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
				pjmedia_port **p_port)
{
    struct fax_device *fd;
    const pj_str_t name = pj_str("fax_device");

    PJ_ASSERT_RETURN(pool && clock_rate && channel_count && 
		     samples_per_frame && bits_per_sample == 16 && 
		     p_port != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pool && p_port, PJ_EINVAL);

    fd = PJ_POOL_ZALLOC_T(pool, struct fax_device);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    pjmedia_port_info_init(&fd->link.port.info, &name, SIGNATURE, clock_rate,
			   channel_count, bits_per_sample, samples_per_frame);

    fd->link.port.get_frame = &fax_get_frame;
    fd->link.port.put_frame = &fax_put_frame;
    fd->link.port.on_destroy = &fax_on_destroy;

	fax_init(&fd->fax, is_sender);

	t30_state_t *t30 = fax_get_t30_state(&fd->fax);

	span_log_set_level(fax_get_logging_state(&fd->fax), SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
	span_log_set_level(t30_get_logging_state(t30), SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);

	char ident[] = "chainlink_fax";

    t30_set_tx_ident(t30, ident);
    t30_set_phase_b_handler(t30, &phase_b_handler, (void*)fd);
    t30_set_phase_d_handler(t30, &phase_d_handler, (void*)fd);
	t30_set_phase_e_handler(t30, &phase_e_handler, (void*)fd);
    //printf("setting document_handler with user_data=%p\n", (void*)fd);
	t30_set_document_handler(t30, &document_handler, (void*)fd);

	fd->is_sender = is_sender;

	pj_status_t status = pj_lock_create_simple_mutex(pool, "fax", &fd->lock);

    if (status != PJ_SUCCESS) {
		printf("failed to create lock\n");
		return status;
    }

    if (is_sender)
		t30_set_tx_file(t30,file,-1,-1);
    else
		t30_set_rx_file(t30,file,-1);

    t30_set_ecm_capability(t30,1);
    t30_set_supported_compressions(t30,T30_SUPPORT_T4_1D_COMPRESSION |
        T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);

    if(flags & FAX_FLAG_TRANSMIT_ON_IDLE) {
	    fax_set_transmit_on_idle(&fd->fax, 1);
    }

    fd->fax_cb = cb;
    fd->fax_cb_user_data = user_data;

    TRACE_((THIS_FILE, "fax_device created: %u/%u/%u/%u", clock_rate, 
	    channel_count, samples_per_frame, bits_per_sample));

    *p_port = &fd->link.port; 
    return PJ_SUCCESS;
}

// called when pjmedia needs data to be sent out
static pj_status_t fax_get_frame(pjmedia_port *this_port,
					pjmedia_frame *frame) {

	//printf("ENTER fax_get_frame frame_size=%i\n", frame->size);

	PJ_ASSERT_RETURN(this_port && frame, PJ_EINVAL);
	char *p = (char*)frame->buf;

	struct fax_device *fd = (struct fax_device*)this_port;
    pj_lock_acquire(fd->lock);

	int tx = 0;

	if ((tx = fax_tx(&fd->fax, (int16_t *)frame->buf, frame->size/2)) < 0) {
		printf("fax_tx reported an error\n");
		pj_lock_release(fd->lock);
		printf("EXIT fax_get_frame\n");
		return PJ_FALSE;
	}
    pj_lock_release(fd->lock);

    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
    frame->timestamp.u64 = 0;

	//printf("EXIT fax_get_frame\n");
    return PJ_SUCCESS;
}

// called when pjmedia has received data
static pj_status_t fax_put_frame(pjmedia_port *this_port, 
				  pjmedia_frame *frame)
{
    if(frame->type != PJMEDIA_FRAME_TYPE_AUDIO) return PJ_SUCCESS;
	//printf("ENTER fax_put_frame frame->buf=%x frame->size=%i\n", frame->buf, frame->size);

    struct fax_device *fd = (struct fax_device*) this_port;
    pj_lock_acquire(fd->lock);

    unsigned int pos = 0;

    while (pos < frame->size)
    {
		// feed the decoder with small chunks of data (16 bytes/ms)
		int len = frame->size - pos;
		if (len > FAX_DATA_CHUNK) len = FAX_DATA_CHUNK;

		/* Pass the new incoming audio frame to the fax_rx function */
		if (fax_rx(&fd->fax, (int16_t *)(frame->buf)+pos, len/2)) {
			printf("fax_rx reported an error\n");
			pj_lock_release(fd->lock);
			printf("EXIT fax_put_frame\n");
			return 0;
		}

		pos += len;
    }

	pj_lock_release(fd->lock);

	//printf("EXIT fax_put_frame\n");
    return fd->link.next->put_frame(fd->link.next, frame);
}

/*
 * Destroy port.
 */
static pj_status_t fax_on_destroy(pjmedia_port *this_port)
{
	printf("fax_on_destroy\n");

	struct fax_device *fd = (struct fax_device*)this_port;

    fax_release(&fd->fax);

	if(fd->lock) pj_lock_destroy(fd->lock);
    return PJ_SUCCESS;
}

