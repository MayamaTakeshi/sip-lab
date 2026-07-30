#include "chainlink/chainlink.h"
#include "chainlink/chainlink_xcorr_det.h"

#include <pjmedia/wav_port.h>
#include <pjmedia/alaw_ulaw.h>
#include <pjmedia/errno.h>
#include <pjmedia/wave.h>
#include <pj/assert.h>
#include <pj/file_access.h>
#include <pj/file_io.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <math.h>
#include <string.h>

#define SIGNATURE       PJMEDIA_SIGNATURE('L', 'x', 'c', 'd')
#define THIS_FILE       "chainlink_xcorr_det.c"
#define XCORR_DECIMATION 8

#if 1
#   define TRACE_(x)	PJ_LOG(4,x)
#else
#   define TRACE_(x)
#endif

struct xcorr_det
{
    struct chainlink link;

    pj_pool_t *pool;

    pj_int16_t *ref_samples;
    unsigned    ref_len;
    double      ref_mean;
    double      ref_energy;

    double     *ref_decimated;
    unsigned    ref_decimated_len;
    double      ref_decimated_mean;
    double      ref_decimated_energy;

    pj_int16_t *ring_buffer;
    unsigned    ring_bufsize;
    unsigned    ring_pos;
    unsigned    samples_ingested;
    unsigned    prefill_len;

    double      threshold;
    unsigned    cooldown_frames;
    unsigned    cooldown_counter;
    unsigned    check_stride;
    unsigned    check_count;

    void      (*match_cb)(pjmedia_port*, void*);
    void       *match_cb_user_data;

    pj_bool_t   ref_loaded;

    double     *dec_buf;
};

static pj_status_t xcorr_get_frame(pjmedia_port *this_port,
                                   pjmedia_frame *frame);
static pj_status_t xcorr_put_frame(pjmedia_port *this_port,
                                   pjmedia_frame *frame);
static pj_status_t xcorr_on_destroy(pjmedia_port *this_port);

static pj_status_t load_reference_wav(struct xcorr_det *xd,
                                      const char *filename)
{
    pjmedia_wave_hdr wave_hdr;
    pj_ssize_t size_to_read, size_read;
    pj_off_t pos;
    pj_oshandle_t fd;
    pj_status_t status;
    unsigned data_len, start_data;
    unsigned fmt_tag, nchan, bits_per_sample;

    if (!pj_file_exists(filename))
        return PJ_ENOTFOUND;

    status = pj_file_open(xd->pool, filename, PJ_O_RDONLY, &fd);
    if (status != PJ_SUCCESS)
        return status;

    size_read = size_to_read = sizeof(wave_hdr) - 8;
    status = pj_file_read(fd, &wave_hdr, &size_read);
    if (status != PJ_SUCCESS || size_read != size_to_read) {
        pj_file_close(fd);
        return PJMEDIA_ENOTVALIDWAVE;
    }

    pjmedia_wave_hdr_file_to_host(&wave_hdr);

    if (wave_hdr.riff_hdr.riff != PJMEDIA_RIFF_TAG ||
        wave_hdr.riff_hdr.wave != PJMEDIA_WAVE_TAG ||
        wave_hdr.fmt_hdr.fmt != PJMEDIA_FMT_TAG)
    {
        pj_file_close(fd);
        return PJMEDIA_ENOTVALIDWAVE;
    }

    fmt_tag = wave_hdr.fmt_hdr.fmt_tag;
    bits_per_sample = wave_hdr.fmt_hdr.bits_per_sample;
    nchan = wave_hdr.fmt_hdr.nchan;

    if (fmt_tag == PJMEDIA_WAVE_FMT_TAG_PCM && bits_per_sample == 16) {
        /* 16-bit PCM — read directly */
    } else if (fmt_tag == PJMEDIA_WAVE_FMT_TAG_ULAW && bits_per_sample == 8) {
        /* 8-bit μ-law — will decode below */
    } else {
        pj_file_close(fd);
        return PJMEDIA_EWAVEUNSUPP;
    }

    if (wave_hdr.fmt_hdr.len > 16) {
        size_to_read = wave_hdr.fmt_hdr.len - 16;
        pj_file_setpos(fd, size_to_read, PJ_SEEK_CUR);
    }

    for (;;) {
        pjmedia_wave_subchunk subchunk;
        size_read = 8;
        status = pj_file_read(fd, &subchunk, &size_read);
        if (status != PJ_SUCCESS || size_read != 8) {
            pj_file_close(fd);
            return PJMEDIA_EWAVETOOSHORT;
        }
        PJMEDIA_WAVE_NORMALIZE_SUBCHUNK(&subchunk);
        if (subchunk.id == PJMEDIA_DATA_TAG) {
            data_len = subchunk.len;
            break;
        }
        size_to_read = subchunk.len;
        pj_file_setpos(fd, size_to_read, PJ_SEEK_CUR);
    }

    pj_file_getpos(fd, &pos);
    start_data = (unsigned)pos;

    if (data_len > pj_file_size(filename) - start_data) {
        pj_file_close(fd);
        return PJMEDIA_EWAVEUNSUPP;
    }

    {
        unsigned bytes_per_sample = bits_per_sample / 8;
        unsigned sample_count = data_len / bytes_per_sample;
        unsigned chan_samples = sample_count / nchan;
        unsigned i, j;

        xd->ref_len = chan_samples;
        xd->ref_samples = (pj_int16_t*)pj_pool_alloc(xd->pool,
                            chan_samples * sizeof(pj_int16_t));

        if (fmt_tag == PJMEDIA_WAVE_FMT_TAG_PCM) {
            /* 16-bit PCM: read raw shorts */
            pj_int16_t *raw_buf = (pj_int16_t*)pj_pool_alloc(xd->pool,
                                    sample_count * sizeof(pj_int16_t));
            {
                pj_ssize_t bytes_to_read = sample_count * sizeof(pj_int16_t);
                pj_ssize_t bytes_read = bytes_to_read;
                status = pj_file_read(fd, raw_buf, &bytes_read);
                if (status != PJ_SUCCESS || bytes_read != bytes_to_read) {
                    pj_file_close(fd);
                    return PJMEDIA_EWAVETOOSHORT;
                }
            }
            for (i = 0; i < chan_samples; i++) {
                pj_int32_t sum = 0;
                for (j = 0; j < nchan; j++) {
                    sum += raw_buf[i * nchan + j];
                }
                xd->ref_samples[i] = (pj_int16_t)(sum / nchan);
            }
        } else {
            /* 8-bit μ-law: read raw bytes, decode to linear */
            pj_uint8_t *raw_buf = (pj_uint8_t*)pj_pool_alloc(xd->pool,
                                   sample_count * sizeof(pj_uint8_t));
            {
                pj_ssize_t bytes_to_read = sample_count * sizeof(pj_uint8_t);
                pj_ssize_t bytes_read = bytes_to_read;
                status = pj_file_read(fd, raw_buf, &bytes_read);
                if (status != PJ_SUCCESS || bytes_read != bytes_to_read) {
                    pj_file_close(fd);
                    return PJMEDIA_EWAVETOOSHORT;
                }
            }
            for (i = 0; i < chan_samples; i++) {
                pj_int32_t sum = 0;
                for (j = 0; j < nchan; j++) {
                    sum += pjmedia_ulaw2linear(raw_buf[i * nchan + j]);
                }
                xd->ref_samples[i] = (pj_int16_t)(sum / nchan);
            }
        }
    }

    pj_file_close(fd);

    {
        double sum = 0.0;
        unsigned i;
        for (i = 0; i < xd->ref_len; i++) {
            sum += (double)xd->ref_samples[i];
        }
        xd->ref_mean = sum / (double)xd->ref_len;

        xd->ref_energy = 0.0;
        for (i = 0; i < xd->ref_len; i++) {
            double d = (double)xd->ref_samples[i] - xd->ref_mean;
            xd->ref_energy += d * d;
        }
    }

    {
        unsigned dec_len = xd->ref_len / XCORR_DECIMATION;
        if (dec_len < 4) dec_len = 4;
        xd->ref_decimated_len = dec_len;

        xd->ref_decimated = (double*)pj_pool_alloc(xd->pool,
                              dec_len * sizeof(double));

        {
            double sum = 0.0;
            unsigned i;
            for (i = 0; i < dec_len; i++) {
                double s = 0.0;
                unsigned j;
                for (j = 0; j < XCORR_DECIMATION; j++) {
                    unsigned idx = i * XCORR_DECIMATION + j;
                    if (idx >= xd->ref_len) break;
                    s += (double)xd->ref_samples[idx];
                }
                s /= (double)XCORR_DECIMATION;
                xd->ref_decimated[i] = s;
                sum += s;
            }
            xd->ref_decimated_mean = sum / (double)dec_len;
        }

        xd->ref_decimated_energy = 0.0;
        {
            unsigned i;
            for (i = 0; i < dec_len; i++) {
                double d = xd->ref_decimated[i] - xd->ref_decimated_mean;
                xd->ref_decimated_energy += d * d;
            }
        }
    }

    xd->ref_loaded = PJ_TRUE;
    TRACE_((THIS_FILE, "reference loaded: %u samples (%u decimated), "
            "mean=%.2f energy=%.2f",
            xd->ref_len, xd->ref_decimated_len,
            xd->ref_mean, xd->ref_energy));

    return PJ_SUCCESS;
}

static double compute_peak_xcorr(const double *sig, unsigned sig_len,
                                  const double *ref, unsigned ref_len,
                                  double ref_mean, double ref_energy,
                                  unsigned *out_best_offset)
{
    unsigned max_offset;
    unsigned offset;
    double best_corr = -1.0;
    unsigned best_off = 0;

    if (sig_len < ref_len) return -1.0;

    max_offset = sig_len - ref_len;

    for (offset = 0; offset <= max_offset; offset++) {
        double sum_sig = 0.0, sum_sig_sq = 0.0, sum_prod = 0.0;
        unsigned i;

        for (i = 0; i < ref_len; i++) {
            double s = sig[offset + i];
            sum_sig += s;
            sum_sig_sq += s * s;
            sum_prod += s * ref[i];
        }

        {
            double sig_mean = sum_sig / (double)ref_len;
            double sig_energy = sum_sig_sq - (double)ref_len * sig_mean * sig_mean;
            double prod_centered = sum_prod - (double)ref_len * sig_mean * ref_mean;

            if (sig_energy > 1e-10 && ref_energy > 1e-10) {
                double denom = sqrt(sig_energy * ref_energy);
                double corr = prod_centered / denom;
                if (corr > best_corr) {
                    best_corr = corr;
                    best_off = offset;
                }
            }
        }
    }

    if (out_best_offset) *out_best_offset = best_off;
    return best_corr;
}

PJ_DEF(pj_status_t) chainlink_xcorr_det_create(
    pj_pool_t *pool,
    unsigned clock_rate,
    unsigned channel_count,
    unsigned samples_per_frame,
    unsigned bits_per_sample,
    const char *ref_file,
    double threshold,
    unsigned cooldown_ms,
    unsigned check_stride,
    void (*cb)(pjmedia_port*, void *user_data),
    void *user_data,
    pjmedia_port **p_port)
{
    struct xcorr_det *xd;
    const pj_str_t name = pj_str("xcorr_det");
    pj_status_t status;

    PJ_ASSERT_RETURN(pool && ref_file && p_port, PJ_EINVAL);

    xd = PJ_POOL_ZALLOC_T(pool, struct xcorr_det);
    PJ_ASSERT_RETURN(xd != NULL, PJ_ENOMEM);

    xd->pool = pool;

    pjmedia_port_info_init(&xd->link.port.info, &name, SIGNATURE,
                           clock_rate, channel_count, bits_per_sample,
                           samples_per_frame);

    xd->link.port.get_frame = &xcorr_get_frame;
    xd->link.port.put_frame = &xcorr_put_frame;
    xd->link.port.on_destroy = &xcorr_on_destroy;

    xd->threshold = (threshold > 0.0) ? threshold : 0.5;
    if (xd->threshold > 1.0) xd->threshold = 1.0;

    if (cooldown_ms > 0 && clock_rate > 0) {
        unsigned frames_per_sec = clock_rate / samples_per_frame;
        xd->cooldown_frames = (cooldown_ms * frames_per_sec) / 1000;
        if (xd->cooldown_frames < 1) xd->cooldown_frames = 1;
    } else {
        xd->cooldown_frames = 0;
    }
    xd->cooldown_counter = 0;

    xd->check_stride = (check_stride > 0) ? check_stride : 4;
    xd->check_count = 0;

    xd->match_cb = cb;
    xd->match_cb_user_data = user_data;

    status = load_reference_wav(xd, ref_file);
    if (status != PJ_SUCCESS) {
        TRACE_((THIS_FILE, "failed to load reference WAV: %s", ref_file));
        return status;
    }

    {
        unsigned search_window = clock_rate / 2;
        unsigned ring_capacity = xd->ref_len + search_window
                                 + samples_per_frame * 4;
        xd->ring_bufsize = ring_capacity;
        xd->ring_buffer = (pj_int16_t*)pj_pool_alloc(pool,
                            ring_capacity * sizeof(pj_int16_t));
        pj_bzero(xd->ring_buffer, ring_capacity * sizeof(pj_int16_t));
    }
    xd->ring_pos = 0;
    xd->samples_ingested = 0;
    xd->prefill_len = xd->ref_len + clock_rate / 2;

    {
        unsigned dec_len = xd->prefill_len / XCORR_DECIMATION;
        xd->dec_buf = (double*)pj_pool_alloc(pool,
                       (dec_len + 4) * sizeof(double));
    }

    *p_port = &xd->link.port;
    return PJ_SUCCESS;
}

static pj_status_t xcorr_get_frame(pjmedia_port *this_port,
                                   pjmedia_frame *frame)
{
    struct xcorr_det *xd = (struct xcorr_det*)this_port;
    PJ_ASSERT_RETURN(xd->link.next, PJ_EINVAL);
    PJ_ASSERT_RETURN(xd->link.next->get_frame, PJ_EINVAL);
    return xd->link.next->get_frame(xd->link.next, frame);
}

static pj_status_t xcorr_put_frame(pjmedia_port *this_port,
                                   pjmedia_frame *frame)
{
    struct xcorr_det *xd = (struct xcorr_det*)this_port;

    if (frame->type == PJMEDIA_FRAME_TYPE_AUDIO && xd->ref_loaded) {
        unsigned i;
        unsigned samples = PJMEDIA_PIA_SPF(&xd->link.port.info);
        pj_int16_t *buf = (pj_int16_t*)frame->buf;

        for (i = 0; i < samples; i++) {
            xd->ring_buffer[xd->ring_pos] = buf[i];
            xd->ring_pos = (xd->ring_pos + 1) % xd->ring_bufsize;
        }
        xd->samples_ingested += samples;

        if (xd->cooldown_counter > 0) {
            xd->cooldown_counter--;
        } else {
            xd->check_count++;
            if (xd->check_count >= xd->check_stride) {
                xd->check_count = 0;

                if (xd->samples_ingested >= xd->prefill_len) {
                    unsigned sig_raw_len = xd->prefill_len;
                    unsigned sig_dec_len = sig_raw_len / XCORR_DECIMATION;
                    int oldest_idx = ((int)xd->ring_pos - (int)sig_raw_len
                                      + (int)xd->ring_bufsize)
                                     % (int)xd->ring_bufsize;
                    double *dec_buf = xd->dec_buf;
                    unsigned j;

                    for (j = 0; j < sig_dec_len; j++) {
                        double s = 0.0;
                        unsigned k;
                        for (k = 0; k < XCORR_DECIMATION; k++) {
                            int idx = (oldest_idx + j * XCORR_DECIMATION + k)
                                      % (int)xd->ring_bufsize;
                            s += (double)xd->ring_buffer[idx];
                        }
                        dec_buf[j] = s / (double)XCORR_DECIMATION;
                    }

                    {
                        unsigned best_offset;
                        double corr = compute_peak_xcorr(
                            dec_buf, sig_dec_len,
                            xd->ref_decimated, xd->ref_decimated_len,
                            xd->ref_decimated_mean,
                            xd->ref_decimated_energy,
                            &best_offset);

                        if (corr > xd->threshold) {
                            TRACE_((THIS_FILE,
                                    "envelope match detected: corr=%.4f "
                                    "thresh=%.4f  best_offset=%u",
                                    corr, xd->threshold, best_offset));

                            if (xd->match_cb) {
                                xd->match_cb((pjmedia_port*)xd,
                                             xd->match_cb_user_data);
                            }

                            if (xd->cooldown_frames > 0)
                                xd->cooldown_counter = xd->cooldown_frames;
                        }
                    }
                }
            }
        }
    }

    if (xd->link.next && xd->link.next->put_frame) {
        return xd->link.next->put_frame(xd->link.next, frame);
    }

    return PJ_SUCCESS;
}

static pj_status_t xcorr_on_destroy(pjmedia_port *this_port)
{
    return PJ_SUCCESS;
}
