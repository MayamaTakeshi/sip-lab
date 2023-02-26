#include <stdio.h>
#include <pjlib.h>
#include <pjmedia.h>

pj_thread_desc g_main_thread_descriptor;
pj_thread_t *g_main_thread = NULL;

static pj_caching_pool cp;
static pj_pool_t *pool;

int main() {
    pj_status_t status = pj_init();
    if(status != PJ_SUCCESS) {
        printf("pj_init failed. status=%i\n", status);
    }

    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    pool = pj_pool_create(&cp.factory, "tester", 1000, 1000, NULL);

    // The above lines are always required. Do not remove them.

    const char *sdp_str =
        "m=audio 5004 RTP/AVP 0 8\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"
        "k=clear:password\r\n"
        "b=AS:128\r\n"
        "a=encrypt:1 AES_CM_128_HMAC_SHA1_80 inline:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n";

    pjmedia_sdp_session *sdp = NULL;

    status = pjmedia_sdp_parse(pool, sdp_str, strlen(sdp_str), &sdp);
    if(status != PJ_SUCCESS) {
      printf("pjmedia_sdp_parse failed. status=%\n", status);
      return 1;
    }

    if (status == PJ_SUCCESS) {
        char buf[2048];
        int len = pjmedia_sdp_print(sdp, buf, sizeof(buf));
        if(len < 0) {
           printf("pjmedia_sdp_print failed.\n");
           return 1;
        }
        buf[len] = 0;
        printf("sdp:\n%s\n", buf);
    } else {
        printf("SDP parsing failed. status=%i\n", status);
        return 1;
    }

    return 0;
}
