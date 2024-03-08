#include <arpa/inet.h>

#include "sip.hpp"

#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <algorithm>
#include <boost/circular_buffer.hpp>
#include <vector>

#include <pthread.h>

#include <ctime>

#include "idmanager.hpp"
#include "event_templates.hpp"

#include "dtmfdet.h"
#include "fax_port.h"
#include "flite_port.h"

#include <ctime>

#define _BSD_SOURCE
#include <sys/time.h>

#include <sys/syscall.h>

#include "log.hpp"

#include "rapidjson/document.h"

#include "siplab_constants.h"

using namespace rapidjson;
using namespace std;

#define EVT_DATA_SEP "|"

#define IDS_MAX (2000)

#define MAX_JSON_INPUT 4096

IdManager g_transport_ids(IDS_MAX);
IdManager g_account_ids(IDS_MAX);
IdManager g_request_ids(IDS_MAX);
IdManager g_call_ids(IDS_MAX);
IdManager g_subscription_ids(IDS_MAX);
IdManager g_subscriber_ids(IDS_MAX);
IdManager g_dialog_ids(IDS_MAX);

#define AF pj_AF_INET()
#define DEFAULT_ILBC_MODE (30)
#define DEFAULT_CODEC_QUALITY (5)

static pjsip_endpoint *g_sip_endpt;
static pj_caching_pool cp;
static pj_pool_t *g_pool;
static pjmedia_endpt *g_med_endpt;

// static pj_thread_t *g_thread = NULL;
// static pj_bool_t g_thread_quit_flag;

static deque<string> g_events;
static pthread_mutex_t g_mutex;

static char pjw_errorstring[4096];

void clear_error() { pjw_errorstring[0] = 0; }

void set_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(pjw_errorstring, sizeof(pjw_errorstring), format, args);
  va_end(args);
}

char *pjw_get_error() { return pjw_errorstring; }

int check_uri(const char *uri) { return (strstr(uri, "sip:") != NULL); }

bool parse_json(Document &document, const char *json, char *buffer,
                long unsigned int len) {
  if (strlen(json) > len - 1) {
    set_error("JSON too large");
    return false;
  }

  strcpy(buffer, json);
  if (document.ParseInsitu(buffer).HasParseError()) {
    set_error("Failed to parse JSON");
    return false;
  }

  if (!document.IsObject()) {
    set_error("Invalid JSON root. Must be an object");
    return false;
  }

  return true;
}

bool param_is_valid(const char *param, const char **valid_params) {
  char **valid_param = (char **)valid_params;
  while (*valid_param[0]) {
    // printf("checking param=%s valid_param=%s\n", param, *valid_param);
    if (strcmp(param, *valid_param) == 0) {
      return true;
    }
    valid_param++;
  }
  return false;
}

bool validate_params(Document &document, const char **valid_params) {
  for (Value::ConstMemberIterator itr = document.MemberBegin();
       itr != document.MemberEnd(); ++itr) {
    const char *param = itr->name.GetString();
    if (!param_is_valid(param, valid_params)) {
      set_error("Invalid param %s", param);
      return false;
    }
  }

  return true;
}

#define FOUND_INVALID -1
#define NOT_FOUND_NOT_OPTIONAL 0
#define NOT_FOUND_OPTIONAL 1
#define FOUND 2

int json_get_string_param(Document &document, const char *param, bool optional,
                          char **dest) {
  printf("json_get_string_param %s\n", param);
  if (!document.HasMember(param)) {
    if (optional) {
      return NOT_FOUND_OPTIONAL;
    }
    set_error("Parameter %s is required", param);
    return NOT_FOUND_NOT_OPTIONAL;
  }

  if (!document[param].IsString()) {
    set_error("Parameter %s must be a string", param);
    return FOUND_INVALID;
  }
  *dest = (char *)document[param].GetString();
  return FOUND;
}

int json_get_int_param(Document &document, const char *param, bool optional,
                       int *dest) {
  if (!document.HasMember(param)) {
    if (optional) {
      return NOT_FOUND_OPTIONAL;
    }
    set_error("Parameter %s is required", param);
    return NOT_FOUND_NOT_OPTIONAL;
  }

  if (!document[param].IsInt()) {
    set_error("Parameter %s must be an integer", param);
    return FOUND_INVALID;
  }
  *dest = document[param].GetInt();
  return FOUND;
}

int json_get_uint_param(Document &document, const char *param, bool optional,
                        unsigned *dest) {
  if (!document.HasMember(param)) {
    if (optional) {
      return NOT_FOUND_OPTIONAL;
    }
    set_error("Parameter %s is required", param);
    return NOT_FOUND_NOT_OPTIONAL;
  }

  if (!document[param].IsUint()) {
    set_error("Parameter %s must be an unsigned integer", param);
    return FOUND_INVALID;
  }
  *dest = document[param].GetUint();
  return FOUND;
}

int json_get_bool_param(Document &document, const char *param, bool optional,
                        bool *dest) {
  if (!document.HasMember(param)) {
    if (optional) {
      return NOT_FOUND_OPTIONAL;
    }
    set_error("Parameter %s is required", param);
    return NOT_FOUND_NOT_OPTIONAL;
  }

  if (!document[param].IsBool()) {
    set_error("Parameter %s must be a boolean", param);
    return FOUND_INVALID;
  }
  *dest = document[param].GetBool();
  return FOUND;
}

bool json_get_and_check_uri(Document &document, const char *param,
                            bool optional, char **dest) {
  if (!json_get_string_param(document, param, optional, dest)) {
    return false;
  }

  if (*dest && *dest[0]) {
    if (!check_uri(*dest)) {
      set_error("Invalid %s", param);
      return false;
    }
  }

  return true;
}

static pjsip_method info_method = {PJSIP_OTHER_METHOD, {(char*)"INFO", 4}};
static pjsip_method message_method = {PJSIP_OTHER_METHOD, {(char*)"MESSAGE", 7}};

static pj_str_t trying_reason = pj_str((char*)"Trying");

#define PJW_LOCK() pthread_mutex_lock(&g_mutex)
#define PJW_UNLOCK() pthread_mutex_unlock(&g_mutex)

char *trim(char *dest, char *src) {
  while (*src == ' ' || *src == '\t')
    src++;
  strcpy(dest, src);

  int len = strlen(dest);
  char *end = dest + len - 1;

  while (*end == ' ' || *end == '\t')
    *end-- = 0;
  return dest;
}

pj_thread_desc g_main_thread_descriptor;
pj_thread_t *g_main_thread = NULL;

pj_thread_desc g_poll_thread_descriptor;
pj_thread_t *g_poll_thread = NULL;

int ms_timestamp();

bool g_shutting_down;

int g_dtmf_inter_digit_timer = 0;

pj_str_t g_sip_ipaddress;

unsigned g_flags = 0;

pjsip_route_hdr route_set;
pjsip_route_hdr *route;
const pj_str_t hname = pj_str((char *)"Route");

#define CONF_PORTS 1024
//#define CLOCK_RATE 16000
#define CLOCK_RATE 8000
#define CHANNEL_COUNT 1
#define PTIME 20
#define SAMPLES_PER_FRAME (CLOCK_RATE*PTIME/1000)
#define BITS_PER_SAMPLE 16

#define MAXDIGITS 256

#define DTMF_MODE_RFC2833 0
#define DTMF_MODE_INBAND 1

struct Subscriber {
  int id;
  int transport_id;
  pjsip_evsub *evsub;
  pjsip_dialog *dlg;
  bool created_by_refer;
};

struct Transport {
  int id;
  pjsip_transport_type_e type;
  pjsip_transport *sip_transport;
  pjsip_tpfactory *tpfactory;
  char address[32];
  int port;
  char tag[64];

  char domain[100];
  char username[100];
  char password[100];
};

struct Subscription {
  int id;
  pjsip_evsub *evsub;
  pjsip_dialog *dlg;
  char event[200];
  char accept[200];
  bool initialized;
};

struct ConfBridgePort {
    unsigned slot;
    pjmedia_port *port;
};

struct AudioEndpoint {
  pjmedia_transport *med_transport;
  pjmedia_stream *med_stream;

  char DigitBuffers[2][MAXDIGITS + 1];
  int DigitBufferLength[2];
  int last_digit_timestamp[2];

  pj_str_t mode;

  ConfBridgePort stream_cbp;
  ConfBridgePort wav_player_cbp;
  ConfBridgePort wav_writer_cbp;
  ConfBridgePort tonegen_cbp;
  ConfBridgePort dtmfdet_cbp;
  ConfBridgePort fax_cbp;
  ConfBridgePort flite_cbp;
};

struct VideoEndpoint {
  pjmedia_transport *med_transport;
  pjmedia_stream *med_stream;
};

struct MrcpEndpoint {
  pj_activesock_t *asock;
};

#define ENDPOINT_TYPE_AUDIO 1
#define ENDPOINT_TYPE_VIDEO 2
#define ENDPOINT_TYPE_T38 3
#define ENDPOINT_TYPE_MRCP 4
#define ENDPOINT_TYPE_MSRP 5

int media_type_name_to_type_id(const char *type_name) {
  if (strcmp("audio", type_name) == 0) {
    return ENDPOINT_TYPE_AUDIO;
  } else if (strcmp("video", type_name) == 0) {
    return ENDPOINT_TYPE_AUDIO;
  } else if (strcmp("t38", type_name) == 0) {
    return ENDPOINT_TYPE_T38;
  } else if (strcmp("mrcp", type_name) == 0) {
    return ENDPOINT_TYPE_MRCP;
  } else if (strcmp("msrp", type_name) == 0) {
    return ENDPOINT_TYPE_MSRP;
  }

  return -1;
}

const char *media_type_id_to_media_type_name(int type) {
  switch(type) {
  case ENDPOINT_TYPE_AUDIO:
    return "audio";
  case ENDPOINT_TYPE_VIDEO:
    return "video";
  case ENDPOINT_TYPE_T38:
    return "t38";
  case ENDPOINT_TYPE_MRCP:
    return "mrcp";
  case ENDPOINT_TYPE_MSRP:
    return "msrp";
  default:
    return "unknown";
  }
}


#define MAX_ATTRS 32

struct MediaEndpoint {
  int type;
  pj_str_t media;
  pj_str_t transport;
  pj_str_t addr;
  int port;
  int field_count;
  bool secure;
  char *field[MAX_ATTRS];

  union {
    AudioEndpoint *audio;
    VideoEndpoint *video;
    MrcpEndpoint *mrcp;
  } endpoint;
};

struct Request {
  int id;
  pjsip_rx_data *pending_rdata;
  bool is_uac;
};

struct Call {
  int id;
  pjsip_inv_session *inv;

  int media_count;
  MediaEndpoint *media[PJMEDIA_MAX_SDP_MEDIA];

  Transport *transport;

  bool outgoing;

  pjsip_evsub *
      xfer_sub; // Xfer server subscription, if this call was triggered by xfer.

  int pending_request;
  pjsip_rx_data *pending_rdata;

  bool inv_initial_answer_required;

  pjmedia_sdp_session *local_sdp;
  pjmedia_sdp_session *remote_sdp;

  pjmedia_sdp_session *active_local_sdp;
  pjmedia_sdp_session *active_remote_sdp;

  bool local_sdp_answer_already_set;

  pjmedia_conf *conf;
  pjmedia_master_port *master_port;
  pjmedia_port *null_port;
};

#define MAX_TCP_DATA 4096

struct AsockUserData {
  pj_sock_t sock;
  pjsip_endpoint *sip_endpt;
  MediaEndpoint *media_endpt;
  Call *call;
  char buf[MAX_TCP_DATA];
  pj_size_t len;
};

struct Pair_Call_CallId {
  Call *pCall;
  long id;
  bool operator==(const Pair_Call_CallId &pcc) const {
    if (this->pCall == pcc.pCall)
      return true;
    return false;
  };
};
typedef boost::circular_buffer<Pair_Call_CallId> Pair_Call_CallId_Buf;
Pair_Call_CallId_Buf g_LastCalls(1000);

typedef map<std::string, long> TransportMap;
TransportMap g_TransportMap;
// int g_TlsTransportId = -100;
// int g_TcpTransportId = -100;

typedef set<pair<string, string>> PackageSet;
PackageSet g_PackageSet;

#define DEFAULT_EXPIRES 600

void handle_events() {
  pj_time_val tv = {0, 1};
  pjsip_endpt_handle_events(g_sip_endpt, &tv);
}

void init_tpselector(Transport *t, pjsip_tpselector *sel) {
  unsigned flag;

  pj_bzero(sel, sizeof(*sel));

  flag = pjsip_transport_get_flag_from_type(t->type);

  if (flag & PJSIP_TRANSPORT_DATAGRAM) {
    sel->type = PJSIP_TPSELECTOR_TRANSPORT;
    sel->u.transport = t->sip_transport;
  } else {
    sel->type = PJSIP_TPSELECTOR_LISTENER;
    sel->u.listener = t->tpfactory;
  }
}

bool start_digit_buffer_thread();

/* Callback to be called when SDP negotiation is done in the call: */
static void on_media_update(pjsip_inv_session *inv, pj_status_t status);

/* Callback to be called when invite session's state has changed: */
static void on_state_changed(pjsip_inv_session *inv, pjsip_event *e);

/* Callback to be called when dialog has forked: */
static void on_forked(pjsip_inv_session *inv, pjsip_event *e);

/* Callback to be called to handle incoming requests */
static pj_bool_t on_rx_request(pjsip_rx_data *rdata);

/* Callback to be called when responses are received */
static pj_bool_t on_rx_response(pjsip_rx_data *rdata);

/* Callback to be called when media offer is received (in REINVITEs but also in
 * late negotiaion scenario) */
// static void on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session
// *offer);
static void on_rx_offer2(pjsip_inv_session *inv,
                         struct pjsip_inv_on_rx_offer_cb_param *param);

/* Callback to be called when REINVITE is received */
static pj_status_t on_rx_reinvite(pjsip_inv_session *inv,
                                  const pjmedia_sdp_session *offer,
                                  pjsip_rx_data *rdata);

/* Callback to be called when Redirect is received */
static pjsip_redirect_op on_redirected(pjsip_inv_session *inv,
                                       const pjsip_uri *target,
                                       const pjsip_event *e);

/* Callback to be called when DTMF is received */
static void on_dtmf(pjmedia_stream *stream, void *user_data, int digit);

/* Callback for Registration Status */
static void on_registration_status(pjsip_regc_cbparam *param);

/* static void on_tsx_state_changed(pjsip_inv_session *inv, pjsip_transaction *tsx,
                                 pjsip_event *e); */

static void client_on_evsub_state(pjsip_evsub *sub, pjsip_event *event);
static void on_client_refresh(pjsip_evsub *sub);
void on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code,
                  pj_str_t **p_st_text, pjsip_hdr *res_hdr,
                  pjsip_msg_body **p_body);

static void server_on_evsub_state(pjsip_evsub *sub, pjsip_event *event);
static void server_on_evsub_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata,
                                       int *p_st_code, pj_str_t **p_st_text,
                                       pjsip_hdr *res_hdr,
                                       pjsip_msg_body **p_body);

bool dlg_create(pjsip_dialog **dlg, Transport *transport, const char *from_uri,
                const char *to_uri, const char *request_uri, const char *realm,
                const char *username, const char *password,
                const char *local_contact);

static int call_create(Transport *t, unsigned flags, pjsip_dialog *dlg,
                       const char *proxy_uri, Document &document);

bool subscription_subscribe_no_headers(Subscription *s, int expires);
bool subscription_subscribe(Subscription *s, int expires, Document &document);

static pjmedia_transport *create_media_transport(const pj_str_t *addr,
                                                 pj_uint16_t *allocated_port);
void close_media_transport(pjmedia_transport *med_transport);
pjsip_transport *create_udp_transport(pjsip_endpoint *sip_endpt,
                                      pj_str_t *ipaddr, int *allocated_port);
pjsip_transport *allocate_udp_transport(pjsip_endpoint *sip_endpt,
                                        pj_str_t *ipaddr, int port);

pjsip_tpfactory *create_tls_tpfactory(pjsip_endpoint *sip_endpt,
                                      pj_str_t *ipaddr, int *allocated_port);
pjsip_tpfactory *allocate_tls_tpfactory(pjsip_endpoint *sip_endpt,
                                        pj_str_t *ipaddr, int port);

pjsip_tpfactory *create_tcp_factory(pjsip_endpoint *sip_endpt, pj_str_t *ipaddr,
                                    int *allocated_port);
pjsip_tpfactory *allocate_tcp_tpfactory(pjsip_endpoint *sip_endpt,
                                        pj_str_t *ipaddr, int port);

bool set_proxy(pjsip_dialog *dlg, const char *proxy_uri);

void build_transport_tag(char *dest, const char *type, const char *address,
                         int port);
void build_transport_tag_from_pjsip_transport(char *dest, pjsip_transport *t);
void build_local_contact(char *dest, pjsip_transport *transport,
                         const char *contact_username);
void build_local_contact_from_tpfactory(char *dest, pjsip_tpfactory *tpfactory,
                                        const char *contact_username,
                                        pjsip_transport_type_e type);

// pj_bool_t add_additional_headers(pj_pool_t *pool, pjsip_tx_data *tdata, const
// char *additional_headers);
pj_bool_t add_headers(pj_pool_t *pool, pjsip_tx_data *tdata,
                      Document &document);

pj_bool_t add_headers_for_account(pjsip_regc *regc, Document &document);

pj_bool_t get_content_type_and_subtype_from_headers(Document &document,
                                                    char *type, char *subtype);

bool build_subscribe_info(ostringstream *oss, pjsip_rx_data *rdata,
                          Subscriber *s);
// bool build_notify_info(pjsip_rx_data *rdata, Subscription *s);

bool add_header_info(ostringstream *oss, pjsip_rx_data *rdata,
                     const char *headers_names, bool fail_on_not_found);

void process_in_dialog_refer(pjsip_dialog *dlg, pjsip_rx_data *rdata);

// void process_response(pjsip_rx_data *rdata, const char *entity_name, int
// entity_id);

void process_in_dialog_subscribe(pjsip_dialog *dlg, pjsip_rx_data *rdata);

// static pj_bool_t stop_media_operation(Call *call);
static void build_stream_stat(ostringstream &oss, pjmedia_rtcp_stat *stat,
                              pjmedia_stream_info *stream_info);

bool prepare_tonegen(Call *call, AudioEndpoint *ae);
bool prepare_dtmfdet(Call *call, AudioEndpoint *ae);
bool prepare_wav_player(Call *c, AudioEndpoint *ae, const char *file, unsigned flags, bool end_of_file_event);
bool prepare_wav_writer(Call *c, AudioEndpoint *ae, const char *file);
bool prepare_fax(Call *c, AudioEndpoint *ae, bool is_sender, const char *file,
                 unsigned flags);
bool prepare_flite(Call *c, AudioEndpoint *ae, const char *voice);

void prepare_error_event(ostringstream *oss, char *scope, char *details);
// void prepare_pjsipcall_error_event(ostringstream *oss, char *scope, char
// *function, pj_status_t s);
void append_status(ostringstream *oss, pj_status_t s);

bool is_media_active(Call *c, MediaEndpoint *me);
void close_media_endpoint(Call *call, MediaEndpoint *me);

void close_media(Call *c);

bool process_media(Call *c, pjsip_dialog *dlg, Document &document, bool answer);

typedef pj_status_t (*audio_endpoint_stop_op_t)(Call *call, AudioEndpoint *ae);

pj_status_t audio_endpoint_stop_play_wav(Call *call, AudioEndpoint *ae);
pj_status_t audio_endpoint_stop_record_wav(Call *call, AudioEndpoint *ae);
pj_status_t audio_endpoint_stop_fax(Call *call, AudioEndpoint *ae);
pj_status_t audio_endpoint_stop_flite(Call *call, AudioEndpoint *ae);

static pjsip_module mod_tester = {
    NULL,
    NULL,                           /* prev, next.		*/
    {(char *)"mod_tester", 10},     /* Name.			*/
    -1,                             /* Id			*/
    PJSIP_MOD_PRIORITY_APPLICATION, /* Priority			*/
    // PJSIP_MOD_PRIORITY_DIALOG_USAGE, /* Priority			*/
    // PJSIP_MOD_PRIORITY_TSX_LAYER - 6, /* Priority */
    NULL,            /* load()			*/
    NULL,            /* start()			*/
    NULL,            /* stop()			*/
    NULL,            /* unload()			*/
    &on_rx_request,  /* on_rx_request()		*/
    &on_rx_response, /* on_rx_response()		*/
    NULL,            /* on_tx_request.		*/
    NULL,            /* on_tx_response()		*/
    NULL,            /* on_tsx_state()		*/
};

struct Timer {
  pj_timer_entry timer_entry;
  pj_time_val delay;
  pj_bool_t in_use;
  unsigned id;
};

Timer _timer;

void dispatch_event(const char *evt);

const char *translate_pjsip_inv_state(int state) {
  switch (state) {
  case PJSIP_INV_STATE_NULL:
    return "null";
  case PJSIP_INV_STATE_CALLING:
    return "calling";
  case PJSIP_INV_STATE_INCOMING:
    return "incoming";
  case PJSIP_INV_STATE_EARLY:
    return "early";
  case PJSIP_INV_STATE_CONNECTING:
    return "connecting";
  case PJSIP_INV_STATE_CONFIRMED:
    return "confirmed";
  case PJSIP_INV_STATE_DISCONNECTED:
    return "disconnected";
  default:
    return "unknown";
  }
}

static pj_bool_t create_transport_srtp(pjmedia_transport *med_transport, pjmedia_transport **srtp) {
	pjmedia_srtp_setting opt;
	pjmedia_srtp_setting_default(&opt);
	printf("calling pjmedia_transport_srtp_create\n");
	return pjmedia_transport_srtp_create(g_med_endpt, med_transport, &opt, srtp);
}

static int
find_endpoint_by_inband_dtmf_media_stream(Call *call,
                                          pjmedia_stream *med_stream) {
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (ENDPOINT_TYPE_AUDIO == me->type) {
      AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;
      if (ae->med_stream == med_stream) {
        return i;
      }
    }
  }
  return -1;
}

pj_status_t setup_call_conf(Call *call) {
  pj_status_t status;
  status = pjmedia_conf_create(call->inv->pool,
                                 CONF_PORTS,
                                 CLOCK_RATE,
                                 CHANNEL_COUNT,
                                 SAMPLES_PER_FRAME,
                                 BITS_PER_SAMPLE,
                                 PJMEDIA_CONF_NO_DEVICE,
                                 &call->conf);

  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_conf_create failed\n");
    return false;
  }

  status = pjmedia_null_port_create(call->inv->pool, CLOCK_RATE, CHANNEL_COUNT, SAMPLES_PER_FRAME, BITS_PER_SAMPLE, &call->null_port);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_null_port_created failed\n");
    return false;
  }
    
  pjmedia_port *conf_port = NULL;

  conf_port = pjmedia_conf_get_master_port(call->conf);
  if (conf_port == NULL) {
    addon_log(L_DBG, "pjmedia_conf_get_master_port failed\n");
    return false;
  }
    
  status = pjmedia_master_port_create(call->inv->pool, call->null_port, conf_port, 0, &call->master_port);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_master_port_create failed\n");
    return false;
  }
    
  status = pjmedia_master_port_start(call->master_port);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_master_port_start failed\n");
    return false;
  }

  return PJ_SUCCESS;
}

void release_call_conf(Call *call) {
    pj_status_t status;

    if (call->master_port) {
        status = pjmedia_master_port_stop(call->master_port);
        if(status != PJ_SUCCESS) {
            addon_log(L_DBG, "pjmedia_master_port_stop failed\n");
        }
        pjmedia_master_port_destroy(call->master_port, 0);
        if(status != PJ_SUCCESS) {
            addon_log(L_DBG, "pjmedia_master_port_destroy failed\n");
        }
        call->master_port = NULL;
    }

    if (call->conf) {
        status = pjmedia_conf_destroy(call->conf);
        if(status != PJ_SUCCESS) {
            addon_log(L_DBG, "pjmedia_conf_destroy failed\n");
        }
        call->conf = NULL;
    }

    if (call->null_port) {
        status = pjmedia_port_destroy(call->null_port);
        if(status != PJ_SUCCESS) {
            addon_log(L_DBG, "pjmedia_port_destroy(null_port) failed\n");
        }
        call->null_port = NULL;
    }
}

static int find_endpoint_by_inband_dtmf_media_port(Call *call,
                                                   pjmedia_port *port) {
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (ENDPOINT_TYPE_AUDIO == me->type) {
      AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;
      if (ae->dtmfdet_cbp.port && (pjmedia_port *)ae->dtmfdet_cbp.port == port) {
        return i;
      }
    }
  }
  return -1;
}

static void on_inband_dtmf(pjmedia_port *port, void *user_data, char digit) {
  if (g_shutting_down)
    return;

  long call_id;
  if (!g_call_ids.get_id((long)user_data, call_id)) {
    addon_log(
        L_DBG,
        "on_inband_dtmf: Failed to get call_id. Event will not be notified.\n");
    return;
  }

  char d = tolower(digit);
  if (d == '*')
    d = 'e';
  if (d == '#')
    d = 'f';

  Call *call = (Call *)user_data;

  int media_id = find_endpoint_by_inband_dtmf_media_port(call, port);
  assert(media_id >= 0);

  MediaEndpoint *me = (MediaEndpoint *)call->media[media_id];
  AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

  int mode = DTMF_MODE_INBAND;

  if (g_dtmf_inter_digit_timer) {

    PJW_LOCK();
    int *pLen = &ae->DigitBufferLength[mode];

    if (*pLen > MAXDIGITS) {
      PJW_UNLOCK();
      addon_log(L_DBG, "No more space for digits in inband buffer\n");
      return;
    }

    ae->DigitBuffers[mode][*pLen] = d;
    (*pLen)++;
    ae->last_digit_timestamp[mode] = ms_timestamp();
    PJW_UNLOCK();
  } else {
    char evt[1024];
    make_evt_dtmf(evt, sizeof(evt), call_id, 1, &d, mode, media_id);
    dispatch_event(evt);
  }
}

static void on_fax_result(pjmedia_port *port, void *user_data, int result) {
  if (g_shutting_down)
    return;

  long call_id;
  if (!g_call_ids.get_id((long)user_data, call_id)) {
    printf(
        "on_fax_result: Failed to get call_id. Event will not be notified.\n");
    return;
  }

  char evt[1024];
  make_evt_fax_result(evt, sizeof(evt), call_id, result);
  dispatch_event(evt);
}

static void on_end_of_file(pjmedia_port *port, void *user_data) {
  if (g_shutting_down)
    return;

  long call_id;
  if (!g_call_ids.get_id((long)user_data, call_id)) {
    printf(
        "on_end_of_file: Failed to get call_id. Event will not be notified.\n");
    return;
  }

  char evt[1024];
  make_evt_end_of_file(evt, sizeof(evt), call_id);
  dispatch_event(evt);
}

void dispatch_event(const char *evt) {
  addon_log(L_DBG, "dispach_event called with evt=%s\n", evt);
  // g_event_sink(evt);

  g_events.push_back(evt);
}

const char *get_media_mode(pjmedia_sdp_attr **attrs, int count) {
  int i;
  for (i = 0; i < count; ++i) {
    pjmedia_sdp_attr *a = attrs[i];
    if (pj_strcmp2(&a->name, "sendrecv") == 0) {
      return "sendrecv";
    } else if (pj_strcmp2(&a->name, "sendonly") == 0) {
      return "sendonly";
    } else if (pj_strcmp2(&a->name, "recvonly") == 0) {
      return "recvonly";
    } else if (pj_strcmp2(&a->name, "inactive") == 0) {
      return "inactive";
    }
  }
  return "unknown";
}

// Adapted from
/*
  https://www.pjsip.org/pjlib/docs/html/page_pjlib_ioqueue_tcp_test.htm
  https://github.com/lroyd/zhangxy/blob/490748c745c99af147aeea18123dd15aac2d0f6c/ioqueue/test/ioq_udp.c#L535
  https://github.com/gnolizuh/Real-Time-Monitor-agent/blob/9c179ef76526f0ddd04c00f8eafd6e5421b1b7d4/Monitor/Monitor/Com.cpp

  https://www.pjsip.org/pjlib/docs/html/group__PJ__ACTIVESOCK.htm#ga2374729a4261eb7a1e780110bcef2e37
  https://www.pjsip.org/pjlib/docs/html/structpj__activesock__cb.htm
*/

static pj_bool_t on_data_read(pj_activesock_t *asock, void *data,
                              pj_size_t size, pj_status_t status,
                              pj_size_t *remainder);
static pj_bool_t on_data_sent(pj_activesock_t *asock,
                              pj_ioqueue_op_key_t *op_key, pj_ssize_t sent);
static pj_bool_t on_accept_complete(pj_activesock_t *asock, pj_sock_t newsock,
                                    const pj_sockaddr_t *src_addr,
                                    int src_addr_len);
static pj_bool_t on_connect_complete(pj_activesock_t *asock,
                                     pj_status_t status);

static pj_activesock_cb activesock_cb = {&on_data_read, NULL,
                                         &on_data_sent, &on_accept_complete,
                                         NULL,          &on_connect_complete};

static pj_bool_t on_data_read(pj_activesock_t *asock, void *data,
                              pj_size_t size, pj_status_t status,
                              pj_size_t *remainder) {
  printf("on_data_read\n");
  if(status != PJ_SUCCESS) {
    printf("on_data_read failed\n");
    return PJ_FALSE;
  }
 
  AsockUserData *ud = (AsockUserData*)pj_activesock_get_user_data(asock);
  if(!ud) return PJ_FALSE;

  if (size == 0) {
    // TODO: destroy the activesock.
    return PJ_FALSE;
  }

  assert(size + ud->len + 1 < MAX_TCP_DATA);

  memcpy(&ud->buf[ud->len], data, size);
  ud->len = ud->len + size;
  ud->buf[ud->len] = '\0';

  char *sep = strstr(ud->buf, "\r\n\r\n");
  if(!sep) {
    // msg incomplete
    *remainder = 0;
    return PJ_TRUE;
  }

  int msg_size;

  char *hdr_cl = strcasestr(ud->buf, "content-length:");
  if(!hdr_cl) {
    // no body, only headers
    msg_size = sep + 4 - ud->buf;
  } else {
    assert(hdr_cl < sep);
    char *end_of_line = strstr(hdr_cl, "\r\n");

    char num_str[8];
    char *start = hdr_cl+16;
    int len = end_of_line - start;
    strncpy(num_str, start, len);
    num_str[len] = '\0';
    int body_len = atoi(num_str);

    assert(body_len > 0 && body_len < 4096);

    if(ud->buf+ud->len < sep+4+body_len) {
      //printf("tcp data: msg incomplete %i %i\n", ud->buf+ud->len, sep+4+body_len);
      *remainder = 0;
      return PJ_TRUE;
    }

    msg_size = sep+4+body_len - ud->buf;
  }

  printf("on_data_read msg_size=%d\n", msg_size);

  char evt[4096];
  make_evt_tcp_msg(evt, sizeof(evt), ud->call->id, media_type_id_to_media_type_name(ud->media_endpt->type), (char*)ud->buf, msg_size);
  printf("on_data_read msg=%s\n", evt);
  dispatch_event(evt);

  int remain_len = ud->len - msg_size;
  memcpy(ud->buf, &ud->buf[msg_size], remain_len);
  ud->len = remain_len;
  ud->buf[ud->len] = '\0';
  
  *remainder = 0;
  return PJ_TRUE;
}

static pj_bool_t on_data_sent(pj_activesock_t *asock,
                              pj_ioqueue_op_key_t *op_key, pj_ssize_t sent) {
  printf("on_data_sent\n");
  return PJ_TRUE;
}

static pj_bool_t on_accept_complete(pj_activesock_t *asock, pj_sock_t newsock,
                                    const pj_sockaddr_t *src_addr,
                                    int src_addr_len) {
  printf("on_accept_complete\n");

  pj_activesock_t *new_asock = NULL;

  AsockUserData *ud = (AsockUserData*)pj_activesock_get_user_data(asock);
  if(!ud) return PJ_FALSE;

  pj_ioqueue_t *ioqueue = pjsip_endpt_get_ioqueue(ud->sip_endpt);

  pj_pool_t *pool = ud->call->inv->dlg->pool; 

  pj_status_t rc =
      pj_activesock_create(pool, newsock, pj_SOCK_STREAM(), NULL, ioqueue,
                           &activesock_cb, NULL, &new_asock);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_create for newsock failed %d\n", rc);
    return PJ_FALSE;
  }

  ud->sock = newsock;

  rc = pj_activesock_set_user_data(new_asock, ud);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed %d\n", rc);
    return PJ_FALSE;
  }

  rc = pj_activesock_start_read(new_asock, pool, 1000, 0);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_start_read() failed with %d\n", rc);
    return PJ_FALSE;
  }
  printf("pj_activesock_start_read() success\n");

  // Now replace the asock in the media_endpoint
  if(ud->media_endpt->type == ENDPOINT_TYPE_MRCP) {
    ud->media_endpt->endpoint.mrcp->asock = new_asock;  
  }

  // Now unset user data in asock
  rc = pj_activesock_set_user_data(asock, NULL);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed %d\n", rc);
    return PJ_FALSE;
  }

  // Now close the server asock
  rc = pj_activesock_close(asock);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_close failed %d\n", rc);
  }

  printf("on_accept_complete finished with success\n");
  return PJ_FALSE; // we don't want to accept any more connections.
}

static pj_bool_t on_connect_complete(pj_activesock_t *asock,
                                     pj_status_t status) {
  printf("on_connect_complete\n");

  AsockUserData *ud = (AsockUserData*)pj_activesock_get_user_data(asock);
  if(!ud) return PJ_FALSE;

  pj_pool_t *pool = ud->call->inv->pool; 

  pj_sockaddr addr;
  int salen = sizeof(salen);

  pj_status_t s = pj_sock_getsockname(ud->sock, &addr, &salen);
  if (s != PJ_SUCCESS) {
    printf("on_connect_complete pj_sock_getsockname failed %d\n", s);
  } else {
    char buf[1024];
    pj_sockaddr_print(&addr, buf, sizeof(buf), 1);
    printf("on_connect_complete local: %s\n", buf);
  }

  s =  pj_sock_getpeername(ud->sock, &addr, &salen);
  if (s != PJ_SUCCESS) {
    printf("on_connect_complete pj_sock_getpeername failed %d\n", s);
  } else {
    char buf[1024];
    pj_sockaddr_print(&addr, buf, sizeof(buf), 1);
    printf("on_connect_complete remote: %s\n", buf);
  }

  s = pj_activesock_start_read(asock, pool, 1000, 0);
  if (s != PJ_SUCCESS) {
    printf("pj_activesock_start_read() failed with %d\n", s);
    return PJ_FALSE;
  }
  printf("pj_activesock_start_read() success\n");

  return PJ_TRUE;
}

static pj_activesock_t* create_tcp_socket(pjsip_endpoint *sip_endpt, pj_str_t *ipaddr, pj_uint16_t  *out_port, MediaEndpoint *media_endpt, Call *call) {
  pj_ioqueue_t *ioqueue = pjsip_endpt_get_ioqueue(sip_endpt);

  pj_pool_t *pool = call->inv->dlg->pool; 

  pj_sock_t *sock = (pj_sock_t *)pj_pool_alloc(pool, sizeof(pj_sock_t));

  pj_status_t rc;
  pj_sockaddr_in addr;

  pj_activesock_t *asock = NULL;

  unsigned allocated_port = 0;

  AsockUserData *ud = NULL;

  pj_int32_t optval = 1;

  rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, sock);
  if (rc != PJ_SUCCESS || *sock == PJ_INVALID_SOCKET) {
    set_error("....unable to create socket, rc=%d\n", rc);
    goto on_error;
  }

  pj_sockaddr_in_init(&addr, ipaddr, 0);

  rc = pj_sock_setsockopt(*sock, PJ_SOL_SOCKET, PJ_SO_REUSEADDR, &optval, sizeof(optval));
  if (rc != PJ_SUCCESS) {
      set_error("pj_sock_setsockopt() failed", rc);
      goto on_error;
  }

  // Bind server socket.
  for (int port=10000 ; port<65535 ; port++) {
	pj_sockaddr_in_set_port(&addr, port);
	rc = pj_sock_bind(*sock, &addr, sizeof(addr));
	if (rc == PJ_SUCCESS) {
        allocated_port = port;
	    break;
    }
  }

  if(allocated_port == 0) {
    set_error("...ERROR could not bind to port\n");
    goto on_error;
  }

  // Server socket listen().
  if (pj_sock_listen(*sock, 5)) {
    set_error("...ERROR in pj_sock_listen() %d\n", rc);
    goto on_error;
  }

  rc = pj_activesock_create(pool, *sock, pj_SOCK_STREAM(), NULL, ioqueue,
                            &activesock_cb, NULL, &asock);
  if (rc != PJ_SUCCESS) {
    set_error("pj_activesock_create failed %d\n", rc);
    goto on_error;
  }

  ud = (AsockUserData*)pj_pool_zalloc(pool, sizeof(AsockUserData));
  ud->sock = *sock;
  ud->sip_endpt = sip_endpt;
  ud->media_endpt = media_endpt;
  ud->call = call;

  rc = pj_activesock_set_user_data(asock, ud);
  if (rc != PJ_SUCCESS) {
    set_error("pj_activesock_set_user_data failed %d\n", rc);
    goto on_error;
  }

  rc = pj_activesock_start_accept(asock, pool);
  if (rc != PJ_SUCCESS) {
    set_error("pj_activesock_start_accept failed %d\n", rc);
    goto on_error;
  }

  *out_port = allocated_port;
  return asock;

on_error:
  return 0;
}

int __pjw_init() {
  addon_log(L_DBG, "pjw_init thread_id=%i\n", syscall(SYS_gettid));

  g_shutting_down = false;

  pj_status_t status;

  status = pj_init();
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pj_init failed\n");
    return 1;
  }

  status = pjlib_util_init();
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pj_lib_util_init failed\n");
    return 1;
  }

  pthread_mutex_init(&g_mutex, NULL);

  pj_log_set_level(0);

  pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

  char *sip_endpt_name = (char *)"mysip";

  status = pjsip_endpt_create(&cp.factory, sip_endpt_name, &g_sip_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_endpt_create failed\n");
    return 1;
  }

  g_pool = pj_pool_create(&cp.factory, "tester", 1000, 1000, NULL);

  /* Create event manager */
  status = pjmedia_event_mgr_create(g_pool, 0, NULL);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_event_mgr_create  failed\n");
    return 1;
  }

  const pj_str_t msg_tag = {(char *)"MESSAGE", 7};
  const pj_str_t STR_MIME_TEXT_PLAIN = {(char *)"text/plain", 10};
  const pj_str_t STR_MIME_APP_ISCOMPOSING = {
      (char *)"application/im-iscomposing+xml", 30};

  /* Register support for MESSAGE method. */
  status = pjsip_endpt_add_capability(g_sip_endpt, &mod_tester, PJSIP_H_ALLOW,
                                      NULL, 1, &msg_tag);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_endpt_add_capability PJSIP_H_ALLOW failed\n");
    return 1;
  }

  /* Register support for "application/im-iscomposing+xml" content */
  pjsip_endpt_add_capability(g_sip_endpt, &mod_tester, PJSIP_H_ACCEPT, NULL, 1,
                             &STR_MIME_APP_ISCOMPOSING);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_endpt_add_capability PJSIP_H_ACCEPT for "
                     "MIME_APP_ISCOMPOSING failed\n");
    return 1;
  }

  /* Register support for "text/plain" content */
  pjsip_endpt_add_capability(g_sip_endpt, &mod_tester, PJSIP_H_ACCEPT, NULL, 1,
                             &STR_MIME_TEXT_PLAIN);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_endpt_add_capability PJSIP_H_ACCEPT for "
                     "MIME_TEXT_PLAIN failed\n");
    return 1;
  }

  status = pjsip_tsx_layer_init_module(g_sip_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_tsx_layer_init_module failed\n");
    return 1;
  }

  status = pjsip_ua_init_module(g_sip_endpt, NULL);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_ua_init_module failed\n");
    return 1;
  }

  status = pjsip_evsub_init_module(g_sip_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_evsub_init_module failed\n");
    return 1;
  }

  status = pjsip_xfer_init_module(g_sip_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_xfer_init_module failed\n");
    return 1;
  }

  status = pjsip_replaces_init_module(g_sip_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_replaces_init_module failed\n");
    return 1;
  }

  pjsip_inv_callback inv_cb;
  pj_bzero(&inv_cb, sizeof(inv_cb));
  inv_cb.on_state_changed = &on_state_changed;
  inv_cb.on_new_session = &on_forked;
  inv_cb.on_media_update = &on_media_update;
  inv_cb.on_rx_offer2 =
      &on_rx_offer2; // we need this for delayed_media scenarios.
  inv_cb.on_rx_reinvite = &on_rx_reinvite;
  // inv_cb.on_tsx_state_changed = &on_tsx_state_changed;
  inv_cb.on_redirected = &on_redirected;

  status = pjsip_inv_usage_init(g_sip_endpt, &inv_cb);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_inv_usage_init failed\n");
    return 1;
  }

  status = pjsip_100rel_init_module(g_sip_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_100rel_init_module failed\n");
    return 1;
  }

  status = pjsip_endpt_register_module(g_sip_endpt, &mod_tester);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjsip_endpt_register_module failed\n");
    return 1;
  }
#if PJ_HAS_THREADS
  status = pjmedia_endpt_create2(&cp.factory, NULL, 1, &g_med_endpt);
#else
  status = pjmedia_endpt_create2(
      &cp.factory, pjsip_endpt_get_ioqueue(g_sip_endpt), 0, &g_med_endpt);
#endif
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_endpt_create failed\n");
    return 1;
  }

#if defined(PJMEDIA_HAS_G711_CODEC) && PJMEDIA_HAS_G711_CODEC != 0
  status = pjmedia_codec_g711_init(g_med_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_g711_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_GSM_CODEC) && PJMEDIA_HAS_GSM_CODEC != 0
  status = pjmedia_codec_gsm_init(g_med_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_gsm_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_l16_CODEC) && PJMEDIA_HAS_l16_CODEC != 0
  status = pjmedia_codec_l16_init(g_med_endpt, 0);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_l16_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_ILBC_CODEC) && PJMEDIA_HAS_ILBC_CODEC != 0
  status = pjmedia_codec_ilbc_init(g_med_endpt, DEFAULT_ILBC_MODE);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_ilbc_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_SPEEX_CODEC) && PJMEDIA_HAS_SPEEX_CODEC != 0
  status = pjmedia_codec_speex_init(g_med_endpt, 0, DEFAULT_CODEC_QUALITY, -1);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_speex_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_G722_CODEC) && PJMEDIA_HAS_G722_CODEC != 0
  status = pjmedia_codec_g722_init(g_med_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_g722_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_OPUS_CODEC) && PJMEDIA_HAS_OPUS_CODEC != 0
  status = pjmedia_codec_opus_init(g_med_endpt);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pjmedia_codec_opus_init failed\n");
    return 1;
  }
#endif

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
  status = pjmedia_srtp_init_lib(g_med_endpt);

  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "Error initializing SRTP library\n");
    return 1;
  }
#endif

  status = pjmedia_codec_bcg729_init(g_med_endpt);
  if (status != PJ_SUCCESS) {
    printf("pjmedia_codec_bcg729_init failed\n");
    return 1;
  }

  status = pj_thread_register("main_thread", g_main_thread_descriptor,
                              &g_main_thread);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "pj_thread_register(main_thread) failed\n");
    exit(1);
  } else {
    // addon_log(L_DBG, "pj_thread_register(main_thread) success\n");
    ;
  }

  if (!start_digit_buffer_thread()) {
    addon_log(L_DBG, "start_digit_buffer_thread() failed\n");
    return 1;
  }

  return 0;
}

int __pjw_poll(char *out_evt) {
  if (!pj_thread_is_registered()) {
    pj_status_t status;
    status = pj_thread_register("poll_thread", g_poll_thread_descriptor,
                                &g_poll_thread);
    if (status != PJ_SUCCESS) {
      addon_log(L_DBG, "pj_thread_register(poll_thread) failed\n");
      exit(1);
    } else {
      // addon_log(L_DBG, "pj_thread_register(poll_thread) success\n");
      ;
    }
  }

  string evt;
  PJW_LOCK();
  handle_events();
  if (!g_events.empty()) {
    evt = g_events[0];
    //printf("__pjw_poll got evt=%s\n", evt.c_str());
    g_events.pop_front();
  }
  PJW_UNLOCK();
  if (evt != "") {
    strcpy(out_evt, evt.c_str());
    return 0;
  }
  return -1;
}

pjsip_transport *allocate_udp_transport(pjsip_endpoint *sip_endpt,
                                        pj_str_t *ipaddr, int port) {
  pj_status_t status;
  pjsip_transport *transport;

  pj_sockaddr addr;
  pj_sockaddr_init(AF, &addr, ipaddr, port);

  if (AF == pj_AF_INET()) {
    status =
        pjsip_udp_transport_start(sip_endpt, &addr.ipv4, NULL, 1, &transport);
  } else if (AF == pj_AF_INET6()) {
    status =
        pjsip_udp_transport_start6(sip_endpt, &addr.ipv6, NULL, 1, &transport);
  } else {
    status = PJ_EAFNOTSUP;
  }

  if (status == PJ_SUCCESS) {
    return transport;
  }
  return NULL;
}

pjsip_transport *create_udp_transport(pjsip_endpoint *sip_endpt,
                                      pj_str_t *ipaddr, int *allocated_port) {
  // pj_status_t status;
  pjsip_transport *transport;

  int port = 5060;
  for (int i = 0; i < 1000; ++i) {
    port += i;
    transport = allocate_udp_transport(sip_endpt, ipaddr, port);
    if (transport) {
      *allocated_port = port;
      return transport;
    }
  }

  return NULL;
}

pjsip_tpfactory *allocate_tcp_tpfactory(pjsip_endpoint *sip_endpt,
                                        pj_str_t *ipaddr, int port) {
  printf("allocate_tcp_tpfactory ipaddr=%.*s port=%i\n", (int)ipaddr->slen,
         ipaddr->ptr, port);
  pj_status_t status;
  pjsip_tpfactory *tpfactory;
  pj_sockaddr local_addr;
  // pjsip_host_port a_name;

  int af;
  af = pj_AF_INET();
  pj_sockaddr_init(af, &local_addr, NULL, 0);

  pj_sockaddr_set_port(&local_addr, (pj_uint16_t)port);

  status = pj_sockaddr_set_str_addr(af, &local_addr, ipaddr);
  if (status != PJ_SUCCESS) {
    return NULL;
  }

  status = pjsip_tcp_transport_start2(sip_endpt, &local_addr.ipv4, NULL, 1,
                                      &tpfactory);
  if (status != PJ_SUCCESS) {
    printf("status=%i\n", status);
    return NULL;
  }

  return tpfactory;
}

pjsip_tpfactory *create_tcp_tpfactory(pjsip_endpoint *sip_endpt,
                                      pj_str_t *ipaddr, int *allocated_port) {
  // pj_status_t status;
  pjsip_tpfactory *tpfactory;

  int port = 6060;
  for (int i = 0; i < 1000; ++i) {
    port += i;
    tpfactory = allocate_tcp_tpfactory(sip_endpt, ipaddr, port);
    if (tpfactory) {
      *allocated_port = port;
      return tpfactory;
    }
  }

  return NULL;
}

pjsip_tpfactory *allocate_tls_tpfactory(pjsip_endpoint *sip_endpt,
                                        pj_str_t *ipaddr, int port) {
  addon_log(L_DBG, "allocate_tls_tpfactory ipaddr=%.*s port=%i\n", ipaddr->slen,
            ipaddr->ptr, port);
  pj_status_t status;
  pjsip_tpfactory *tpfactory;
  pj_sockaddr local_addr;
  // pjsip_host_port a_name;

  int af;
  af = pj_AF_INET();
  pj_sockaddr_init(af, &local_addr, NULL, 0);

  pj_sockaddr_set_port(&local_addr, (pj_uint16_t)port);

  status = pj_sockaddr_set_str_addr(af, &local_addr, ipaddr);
  if (status != PJ_SUCCESS) {
    return NULL;
  }

  pjsip_tls_setting tls_opt;
  pjsip_tls_setting_default(&tls_opt);

  status = pjsip_tls_transport_start2(sip_endpt, &tls_opt, &local_addr, NULL, 1,
                                      &tpfactory);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "status=%i\n", status);
    return NULL;
  }

  return tpfactory;
}

pjsip_tpfactory *create_tls_tpfactory(pjsip_endpoint *sip_endpt,
                                      pj_str_t *ipaddr, int *allocated_port) {
  // pj_status_t status;
  pjsip_tpfactory *tpfactory;

  int port = 6060;
  for (int i = 0; i < 1000; ++i) {
    port += i;
    tpfactory = allocate_tls_tpfactory(sip_endpt, ipaddr, port);
    if (tpfactory) {
      *allocated_port = port;
      return tpfactory;
    }
  }

  return NULL;
}

// int pjw_transport_create(const char *sip_ipaddr, int port,
// pjsip_transport_type_e type, int *out_t_id, int *out_port)
int pjw_transport_create(const char *json, int *out_t_id, char *out_t_address,
                         int *out_port) {
  PJW_LOCK();
  clear_error();

  char *addr;
  pj_str_t address; // = pj_str((char*)sip_ipaddr);
  int port = 0;
  pjsip_transport_type_e type = PJSIP_TRANSPORT_UDP;

  pj_status_t status;
  Transport *transport = NULL;
  const char *tp;
  long t_id;

  char buffer[MAX_JSON_INPUT];

  const char *valid_params[] = {"address", "port", "type", ""};

  Document document;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  // address
  if (!document.HasMember("address")) {
    set_error("Parameter address is required");
    goto out;
  }

  if (!document["address"].IsString()) {
    set_error("Parameter address must be a string");
    goto out;
  }
  addr = (char *)document["address"].GetString();
  address = pj_str((char *)addr);

  // port
  if (document.HasMember("port")) {
    if (!document["port"].IsInt()) {
      set_error("Parameter port must be an integer");
      goto out;
    }
    port = document["port"].GetInt();
  }

  tp = "udp";

  // type
  if (document.HasMember("type")) {
    if (!document["type"].IsString()) {
      set_error("Parameter type must be a string");
      goto out;
    }
    tp = (char *)document["type"].GetString();
    if (strcmp(tp, "udp") == 0) {
      type = PJSIP_TRANSPORT_UDP;
    } else if (strcmp(tp, "tcp") == 0) {
      type = PJSIP_TRANSPORT_TCP;
    } else if (strcmp(tp, "tls") == 0) {
      type = PJSIP_TRANSPORT_TLS;
    } else {
      set_error(
          "Invalid type %s. It must be one of 'UDP' (default), 'TCP' or 'TLS'",
          tp);
      goto out;
    }
  }

  if (type == PJSIP_TRANSPORT_UDP) {
    pjsip_transport *sip_transport = NULL;

    if (port != 0) {
      sip_transport = allocate_udp_transport(g_sip_endpt, &address, port);
    } else {
      sip_transport = create_udp_transport(g_sip_endpt, &address, &port);
    }

    if (!sip_transport) {
      set_error("Unable to start UDP transport");
      goto out;
    }

    transport = new Transport;
    transport->type = PJSIP_TRANSPORT_UDP;
    transport->sip_transport = sip_transport;

    if (!g_transport_ids.add((long)transport, t_id)) {
      status = pjsip_udp_transport_pause(sip_transport,
                                         PJSIP_UDP_TRANSPORT_DESTROY_SOCKET);
      printf("pjsip_dup_transport_pause status=%i\n", status);
      set_error("Failed to allocate id");
      goto out;
    }
  } else if (type == PJSIP_TRANSPORT_TCP) {
    pjsip_tpfactory *tpfactory;
    // int af;

    if (port != 0) {
      tpfactory = allocate_tcp_tpfactory(g_sip_endpt, &address, port);
    } else {
      tpfactory = create_tcp_tpfactory(g_sip_endpt, &address, &port);
    }

    if (!tpfactory) {
      set_error("Unable to start TCP transport");
      goto out;
    }

    transport = new Transport;
    transport->type = PJSIP_TRANSPORT_TCP;
    transport->tpfactory = tpfactory;

    if (!g_transport_ids.add((long)transport, t_id)) {
      status = (tpfactory->destroy)(tpfactory);

      set_error("Failed to allocate id");
      goto out;
    }
  } else {
    pjsip_tpfactory *tpfactory;
    // int af;

    if (port != 0) {
      tpfactory = allocate_tls_tpfactory(g_sip_endpt, &address, port);
    } else {
      tpfactory = create_tls_tpfactory(g_sip_endpt, &address, &port);
    }

    if (!tpfactory) {
      set_error("Unable to start TLS transport");
      goto out;
    }

    transport = new Transport;
    transport->type = PJSIP_TRANSPORT_TLS;
    transport->tpfactory = tpfactory;

    if (!g_transport_ids.add((long)transport, t_id)) {
      status = (tpfactory->destroy)(tpfactory);

      set_error("Failed to allocate id");
      goto out;
    }
  }

  transport->id = t_id;
  strcpy(transport->address, addr);
  transport->port = port;

  build_transport_tag(transport->tag, tp, addr, port);
  g_TransportMap.insert(make_pair(transport->tag, t_id));

  *out_t_id = t_id;
  strcpy(out_t_address, addr);
  *out_port = port;
out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

int pjw_transport_get_info(int t_id, char *out_sip_ipaddr, int *out_port) {
  PJW_LOCK();
  clear_error();

  long val;
  Transport *t;

  int port;
  int len;

  if (!g_transport_ids.get(t_id, val)) {
    set_error("Invalid transport_id");
    goto out;
  }
  t = (Transport *)val;

  port = t->sip_transport->local_name.port;
  len = t->sip_transport->local_name.host.slen;
  strncpy(out_sip_ipaddr, t->sip_transport->local_name.host.ptr, len);
  out_sip_ipaddr[len] = 0;
  *out_port = port;

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

// int pjw_account_create(int t_id, const char *domain, const char *server,
// const char *username, const char *password, const char *additional_headers,
// const char *c_to_url, int expires, int *out_acc_id)
int pjw_account_create(int t_id, const char *json, int *out_acc_id) {
  PJW_LOCK();
  clear_error();

  long val;

  pj_status_t status;
  pjsip_regc *regc;

  char *domain;
  char *server;
  char *username;
  char *password;
  char *c_to_uri = NULL;
  int expires = 60;

  pj_str_t server_uri;
  pj_str_t from_uri;
  pj_str_t to_uri;
  pj_str_t contact;

  long acc_id;

  Transport *t;

  int local_port;
  int len;
  char local_addr[100];

  char temp[400];
  char *p;

  pjsip_cred_info cred;
  pjsip_tpselector sel;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"domain", "server",  "username", "password",
                                "to_url", "expires", "headers",  ""};

  if (!g_transport_ids.get(t_id, val)) {
    set_error("Invalid transport id");
    goto out;
  }
  t = (Transport *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "domain", false, &domain) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "server", false, &server) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "username", false, &username) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "password", false, &password) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "to_uri", true, &c_to_uri) <= 0) {
    goto out;
  }

  if (json_get_int_param(document, "expires", true, &expires) <= 0) {
    goto out;
  }

  if(expires == 0) {
    expires = PJSIP_REGC_EXPIRATION_NOT_SPECIFIED;
  }

  status = pjsip_regc_create(g_sip_endpt, NULL, on_registration_status, &regc);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_regc_create failed with status=%i", status);
    goto out;
  }

  if (!add_headers_for_account(regc, document)) {
    goto out;
  }

  if (!g_account_ids.add((long)regc, acc_id)) {
    set_error("Failed to allocate id");
    goto out;
  }

  if (t->type == PJSIP_TRANSPORT_UDP) {
    local_port = t->sip_transport->local_name.port;
    len = t->sip_transport->local_name.host.slen;
    strncpy(local_addr, t->sip_transport->local_name.host.ptr, len);
  } else {
    local_port = t->tpfactory->addr_name.port;
    len = t->tpfactory->addr_name.host.slen;
    strncpy(local_addr, t->tpfactory->addr_name.host.ptr, len);
  }
  local_addr[len] = 0;

  p = temp;

  len = sprintf(p, "sip:%s", server);
  printf("server_uri=%s\n", p);
  server_uri = pj_str(p);
  p += len + 2;

  len = sprintf(p, "<sip:%s@%s>", username, domain);
  printf("from_uri=%s\n", p);
  from_uri = pj_str(p);
  p += len + 2;

  to_uri = from_uri;

  if (c_to_uri && c_to_uri[0]) {
    printf("c_to_uri=%s\n", c_to_uri);
    to_uri = pj_str((char *)c_to_uri);
  }

  len = sprintf(p, "sip:%s@%s:%u", username, local_addr, local_port);
  printf("contact=%s\n", p);
  contact = pj_str(p);
  p += len + 2;

  status = pjsip_regc_init(regc, &server_uri, &from_uri, &to_uri, 1, &contact,
                           expires);
  if (status != PJ_SUCCESS) {
    status = pjsip_regc_destroy(regc);
    // ToDo: log status
    set_error("pjsip_regc_init failed");
    goto out;
  }

  pj_bzero(&cred, sizeof(cred));
  cred.realm = pj_str((char *)"*");
  cred.scheme = pj_str((char *)"digest");
  cred.username = pj_str((char *)username);
  cred.data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
  cred.data = pj_str((char *)password);
  status = pjsip_regc_set_credentials(regc, 1, &cred);
  if (status != PJ_SUCCESS) {
    status = pjsip_regc_destroy(regc);
    // ToDo: log status
    set_error("pjsip_regc_set_credentials failed");
    goto out;
  }

  pj_bzero(&sel, sizeof(sel));
  if (t->type == PJSIP_TRANSPORT_UDP) {
    sel.type = PJSIP_TPSELECTOR_TRANSPORT;
    sel.u.transport = t->sip_transport;
  } else {
    sel.type = PJSIP_TPSELECTOR_LISTENER;
    sel.u.listener = t->tpfactory;
  }

  status = pjsip_regc_set_transport(regc, &sel);
  if (status != PJ_SUCCESS) {
    status = pjsip_regc_destroy(regc);
    // ToDo: log status
    set_error("pjsip_regc_set_transport failed");
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  *out_acc_id = acc_id;
  return 0;
}

// int pjw_account_register(long acc_id, pj_bool_t autoreg)
int pjw_account_register(long acc_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  pjsip_regc *regc;

  pj_status_t status;
  pjsip_tx_data *tdata;

  char buffer[MAX_JSON_INPUT];

  bool auto_refresh = false;

  Document document;

  const char *valid_params[] = {"auto_refresh", ""};

  if (!g_account_ids.get(acc_id, val)) {
    set_error("Invalid account_id");
    goto out;
  }
  regc = (pjsip_regc *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_bool_param(document, "auto_refresh", true, &auto_refresh) <= 0) {
    goto out;
  }

  status = pjsip_regc_register(regc, auto_refresh, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_regc_register failed");
    goto out;
  }

  status = pjsip_regc_send(regc, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_regc_send failed");
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

int pjw_account_unregister(long acc_id) {
  PJW_LOCK();
  clear_error();

  long val;

  pjsip_regc *regc;

  pj_status_t status;
  pjsip_tx_data *tdata;

  if (!g_account_ids.get(acc_id, val)) {
    set_error("Invalid account_id");
    goto out;
  }
  regc = (pjsip_regc *)val;

  status = pjsip_regc_unregister(regc, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_regc_unregister failed with status=%i", status);
    goto out;
  }

  status = pjsip_regc_send(regc, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_regc_send failed with status=%i", status);
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

int pjw_call_respond(long call_id, const char *json) {
  addon_log(L_DBG, "pjw_call_respond: call_id=%lu json=%s\n", call_id, json);
  PJW_LOCK();
  clear_error();

  long val;

  int code;
  char *reason;

  pj_str_t r; // pj_str((char*)reason);

  pj_status_t status;

  pjsip_tx_data *tdata;

  Call *call;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"code", "reason", "headers", "media", ""};

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;
  // addon_log(L_DBG, "pending_invite=%d\n", call->pending_invite);

  if (call->pending_request == -1) {
    set_error("no pending request to be answered");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_int_param(document, "code", true, &code) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "reason", true, &reason) <= 0) {
    goto out;
  }

  r = pj_str((char *)reason);

  if (call->pending_request != PJSIP_INVITE_METHOD) {
    pjsip_transaction *tsx = pjsip_rdata_get_tsx(call->pending_rdata);
    if (!tsx)
      goto out;
    assert(tsx);

    pjsip_tx_data *tdata;

    status = pjsip_dlg_create_response(call->inv->dlg, call->pending_rdata,
                                       code, NULL, &tdata);

    assert(status == PJ_SUCCESS);

    status = pjsip_dlg_send_response(call->inv->dlg, tsx, tdata);

    assert(status == PJ_SUCCESS);

    if (code >= 200) {
      status = pjsip_rx_data_free_cloned(call->pending_rdata);
      if (status != PJ_SUCCESS) {
        set_error("pjsip_rx_data_free_cloned failed with status=%i", status);
        goto out;
      }
      call->pending_rdata = 0;

      call->pending_request = -1;
    }

    goto out;
  }

  if (183 == code || (code >= 200 && code < 300)) {
    // process_media above set call->local_sdp based on document.

    if (call->pending_rdata && call->pending_rdata->msg_info.msg->body &&
        call->pending_rdata->msg_info.msg->body->len) {
      if(!call->local_sdp_answer_already_set) {
        if (!process_media(call, call->inv->dlg, document, true)) {
          goto out;
        }

        status = pjsip_inv_set_sdp_answer(call->inv, call->local_sdp);
        call->local_sdp_answer_already_set = true;
      }
    } else {
      printf("delayed media. we need to send the offer\n");
      if (!process_media(call, call->inv->dlg, document, false)) {
        goto out;
      }

      status = pjmedia_sdp_neg_create_w_local_offer(
          call->inv->dlg->pool, call->local_sdp, &call->inv->neg);
      if (status != PJ_SUCCESS) {
        set_error("pjmedia_sdp_neg_create_w_local_offer failed");
        goto out;
      }
    }
  }

  if (call->inv_initial_answer_required) {
    status = pjsip_inv_initial_answer(call->inv, call->pending_rdata, code, &r,
                                      call->local_sdp, &tdata);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_inv_initial_answer failed with status=%i", status);
      goto out;
    }

    call->inv_initial_answer_required = false;

    if (code >= 200 && code < 300) {
      status = pjsip_rx_data_free_cloned(call->pending_rdata);
      if (status != PJ_SUCCESS) {
        set_error("pjsip_rx_data_free_cloned failed with status=%i", status);
        goto out;
      }
      call->pending_rdata = 0;

      if (code >= 200 && code < 300) {
        call->pending_request = -1;
      }
    }
  } else {
    status = pjsip_inv_answer(call->inv, code, &r,
                              NULL, // local_sdp,
                              &tdata);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_inv_answer failed with status=%i", status);
      goto out;
    }

    if (code >= 200 && code < 300) {
      if(call->pending_rdata) {
        status = pjsip_rx_data_free_cloned(call->pending_rdata);
        if (status != PJ_SUCCESS) {
          set_error("pjsip_rx_data_free_cloned failed with status=%i", status);
          goto out;
        }
        call->pending_rdata = 0;
      }

      if (code >= 200 && code < 300) {
        call->pending_request = -1;
      }
    }

    if (!add_headers(call->inv->dlg->pool, tdata, document)) {
      goto out;
    }
  }

  status = pjsip_inv_send_msg(call->inv, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_send_msg failed with status=%i", status);
    goto out;
  }

  if(code >= 200 && code < 300) {
    call->local_sdp_answer_already_set = false;
  }
out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

// int pjw_call_terminate(long call_id, int code, const char *reason, const char
// *additional_headers)
int pjw_call_terminate(long call_id, const char *json) {
  addon_log(L_DBG, "call_terminate call_id=%d\n", call_id);
  PJW_LOCK();
  clear_error();

  long val;
  pjsip_tx_data *tdata;
  pj_status_t status;
  int code = 0;
  char *reason = (char *)"";
  pj_str_t r; // = pj_str((char*)reason);

  Call *call;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"code", "reason", "headers", ""};

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_int_param(document, "code", true, &code) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "reason", true, &reason) <= 0) {
    goto out;
  }

  r = pj_str((char *)reason);

  status = pjsip_inv_end_session(call->inv, code, &r, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_end_session failed");
    goto out;
  }

  if (!tdata) {
    // if tdata was not set by pjsip_inv_end_session, it means we didn't receive
    // any response yet (100 Trying) and we cannot send CANCEL in this
    // situation. So we just can return here without calling pjsip_inv_send_msg.
    goto out;
  }

  if (!add_headers(call->inv->dlg->pool, tdata, document)) {
    goto out;
  }

  status = pjsip_inv_send_msg(call->inv, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_send_msg failed with status=%i", status);
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

void request_callback(void *token, pjsip_event *event) {
  addon_log(L_DBG, "request_callback\n");

  Request *request = (Request *)token;

  pjsip_rx_data *rdata = event->body.tsx_state.src.rdata;
  pj_str_t mname = rdata->msg_info.cseq->method.name;

  char evt[2048];
  make_evt_response(evt, sizeof(evt), "request", request->id, mname.slen,
                    mname.ptr, rdata->msg_info.len, rdata->msg_info.msg_buf);
  dispatch_event(evt);
}

int pjw_request_create(long t_id, const char *json, long *out_request_id,
                       char *out_sip_call_id) {
  PJW_LOCK();
  clear_error();

  long val;
  Transport *t;

  long request_id;

  Request *request;

  char *method = NULL;
  char *from_uri = NULL;
  char *to_uri = NULL;
  char *request_uri = NULL;

  char local_contact[1024];
  char call_id[1024];

  pj_str_t request_uri_s;
  pj_str_t from_uri_s;
  pj_str_t to_uri_s;

  pj_str_t local_contact_s;
  pj_str_t call_id_s;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"method",      "from_uri", "to_uri",
                                "request_uri", "headers",  ""};

  const pjsip_method *m;
  pjsip_tx_data *tdata;
  pj_status_t status;

  if (!g_transport_ids.get(t_id, val)) {
    set_error("Invalid transport_id");
    goto out;
  }
  t = (Transport *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (!json_get_string_param(document, "method", false, &method)) {
    goto out;
  }

  if (strcmp(method, "REGISTER") == 0) {
    m = &pjsip_register_method;
  } else if (strcmp(method, "OPTIONS") == 0) {
    m = &pjsip_options_method;
  } else if (strcmp(method, "INFO") == 0) {
    m = &info_method;
  } else if (strcmp(method, "MESSAGE") == 0) {
    m = &message_method;
  } else {
    set_error("Unsupported method");
    goto out;
  }

  if (!json_get_and_check_uri(document, "from_uri", false, &from_uri)) {
    goto out;
  }
  from_uri_s = pj_str(from_uri);

  if (!json_get_and_check_uri(document, "to_uri", false, &to_uri)) {
    goto out;
  }
  to_uri_s = pj_str(to_uri);

  request_uri = to_uri;
  if (!json_get_and_check_uri(document, "request_uri", true, &request_uri)) {
    goto out;
  }
  request_uri_s = pj_str(request_uri);

  if (t->type == PJSIP_TRANSPORT_UDP) {
    build_local_contact(local_contact, t->sip_transport, "sip-lab");
  } else {
    build_local_contact_from_tpfactory(local_contact, t->tpfactory, "sip-lab",
                                       t->type);
  }
  local_contact_s = pj_str(local_contact);

  call_id_s = pj_str(call_id);
  pj_generate_unique_string_lower(&call_id_s);

  status = pjsip_endpt_create_request(g_sip_endpt, m, &request_uri_s,
                                      &from_uri_s, &to_uri_s, &local_contact_s,
                                      &call_id_s, -1, NULL, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_endpt_create_request failed");
    goto out;
  }

  if (!add_headers(tdata->pool, tdata, document)) {
    goto out;
  }

  request = (Request *)pj_pool_zalloc(tdata->pool, sizeof(Request));
  request->is_uac = true;

  status = pjsip_endpt_send_request(g_sip_endpt, tdata, -1, (void *)request,
                                    request_callback);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_endpt_send_request failed");
    goto out;
  }

  if (!g_request_ids.add((long)request, request_id)) {
    set_error("Failed to allocate id");
    goto out;
  }

  request->id = request_id;
out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  *out_request_id = request_id;
  strncpy(out_sip_call_id, call_id_s.ptr, call_id_s.slen);
  out_sip_call_id[call_id_s.slen] = 0;
  return 0;
}

int pjw_request_respond(long request_id, const char *json) {
  addon_log(L_DBG, "pjw_request_respond: request_id=%lu json=%s\n", request_id,
            json);
  PJW_LOCK();
  clear_error();

  long val;

  int code;
  char *reason;

  pj_str_t r; // pj_str((char*)reason);

  pj_status_t status;

  pjsip_tx_data *tdata;

  pjsip_response_addr res_addr;

  Request *request;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"code", "reason", "headers", ""};

  if (!g_request_ids.get(request_id, val)) {
    set_error("Invalid request_id");
    goto out;
  }
  request = (Request *)val;

  if (request->is_uac) {
    set_error("Cannot respond to our own request");
    goto out;
  }

  if (!request->pending_rdata) {
    set_error("Final response already sent");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_int_param(document, "code", true, &code) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "reason", true, &reason) <= 0) {
    goto out;
  }

  r = pj_str((char *)reason);

  status = pjsip_endpt_create_response(g_sip_endpt, request->pending_rdata,
                                       code, &r, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_endpt_create_response failed");
    goto out;
  }

  if (!add_headers(tdata->pool, tdata, document)) {
    goto out;
  }

  status =
      pjsip_get_response_addr(tdata->pool, request->pending_rdata, &res_addr);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_get_response_addr failed");
    goto out;
  }

  status = pjsip_endpt_send_response(g_sip_endpt, &res_addr, tdata, NULL, NULL);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_endpt_send_response failed");
    goto out;
  }

  if (code >= 200 && request->pending_rdata) {
    status = pjsip_rx_data_free_cloned(request->pending_rdata);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_rx_data_free_cloned failed with status=%i", status);
      goto out;
    }
    request->pending_rdata = NULL;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

int pjw_call_create(long t_id, const char *json, long *out_call_id,
                    char *out_sip_call_id) {
  PJW_LOCK();
  clear_error();

  // int n;
  long val;
  Transport *t;
  // char *start;
  // char *end;
  char local_contact[400];
  // char *p;
  // int len;
  const char *contact_username = "sip";
  int call_id;

  char *from_uri = NULL;
  char *to_uri = NULL;
  char *request_uri = NULL;
  char *proxy_uri = NULL;

  char *realm = NULL;
  char *username = NULL;
  char *password = NULL;

  unsigned flags = 0;

  pjsip_dialog *dlg;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"from_uri",  "to_uri", "request_uri",
                                "proxy_uri", "auth",   "delayed_media",
                                "headers",   "media",  ""};

  if (!g_transport_ids.get(t_id, val)) {
    set_error("Invalid transport_id");
    goto out;
  }
  t = (Transport *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (!json_get_and_check_uri(document, "from_uri", false, &from_uri)) {
    goto out;
  }

  if (!json_get_and_check_uri(document, "to_uri", false, &to_uri)) {
    goto out;
  }

  request_uri = to_uri;
  if (!json_get_and_check_uri(document, "request_uri", true, &request_uri)) {
    goto out;
  }

  if (!json_get_and_check_uri(document, "proxy_uri", true, &proxy_uri)) {
    goto out;
  }

  if (document.HasMember("auth")) {
    if (!document["auth"].IsObject()) {
      set_error("Parameter auth must be an object");
      goto out;
    } else {
      const Value &auth = document["auth"];

      for (Value::ConstMemberIterator itr = auth.MemberBegin();
           itr != auth.MemberEnd(); ++itr) {
        const char *name = itr->name.GetString();
        if (strcmp(name, "realm") == 0) {
          if (!itr->value.IsString()) {
            set_error("%s must be a string", itr->name.GetString());
            goto out;
          }
          realm = (char *)itr->value.GetString();
        } else if (strcmp(name, "username") == 0) {
          if (!itr->value.IsString()) {
            set_error("%s must be a string", itr->name.GetString());
            goto out;
          }
          username = (char *)itr->value.GetString();
          contact_username = username;
        } else if (strcmp(name, "password") == 0) {
          if (!itr->value.IsString()) {
            set_error("%s must be a string", itr->name.GetString());
            goto out;
          }
          password = (char *)itr->value.GetString();
        } else {
          set_error("Unknown auth paramter %s", itr->name.GetString());
          goto out;
        }
      }
    }
  }

  if (document.HasMember("delayed_media")) {
    if (!document["delayed_media"].IsBool()) {
      set_error("Parameter delayed_media must be a boolean");
      goto out;
    } else {
      if (document["delayed_media"].GetBool()) {
        flags = flags | CALL_FLAG_DELAYED_MEDIA;
      }
    }
  }

  if (t->type == PJSIP_TRANSPORT_UDP) {
    build_local_contact(local_contact, t->sip_transport, contact_username);
  } else {
    build_local_contact_from_tpfactory(local_contact, t->tpfactory,
                                       contact_username, t->type);
  }

  if (!dlg_create(&dlg, t, from_uri, to_uri, request_uri, realm, username,
                  password, local_contact)) {
    goto out;
  }

  call_id = call_create(t, flags, dlg, proxy_uri, document);
  if (call_id < 0) {
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  *out_call_id = call_id;
  strncpy(out_sip_call_id, dlg->call_id->id.ptr, dlg->call_id->id.slen);
  out_sip_call_id[dlg->call_id->id.slen] = 0;
  return 0;
}

bool dlg_set_transport(pjsip_transport *sip_transport, pjsip_dialog *dlg) {
  // Maybe we don't need to allocation sel from the pool
  pjsip_tpselector *sel =
      (pjsip_tpselector *)pj_pool_zalloc(dlg->pool, sizeof(pjsip_tpselector));
  // pjsip_tpselector sel;
  // pj_bzero(&sel, sizeof(sel));
  sel->type = PJSIP_TPSELECTOR_TRANSPORT;
  sel->u.transport = sip_transport;
  pj_status_t status = pjsip_dlg_set_transport(dlg, sel);
  if (status != PJ_SUCCESS) {
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("pjsip_dlg_set_transport failed");
    return false;
  }
  return true;
}

bool dlg_set_transport_by_t(Transport *t, pjsip_dialog *dlg) {
  // Maybe we don't need to allocation sel from the pool
  pjsip_tpselector *sel =
      (pjsip_tpselector *)pj_pool_zalloc(dlg->pool, sizeof(pjsip_tpselector));
  // pjsip_tpselector sel;
  // pj_bzero(&sel, sizeof(sel));
  if (t->type == PJSIP_TRANSPORT_UDP) {
    sel->type = PJSIP_TPSELECTOR_TRANSPORT;
    sel->u.transport = t->sip_transport;
  } else {
    sel->type = PJSIP_TPSELECTOR_LISTENER;
    sel->u.listener = t->tpfactory;
  }
  pj_status_t status = pjsip_dlg_set_transport(dlg, sel);
  if (status != PJ_SUCCESS) {
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("pjsip_dlg_set_transport failed");
    return false;
  }
  return true;
}

void build_transport_tag(char *dest, const char *type, const char *address,
                         int port) {
  sprintf(dest, "%s:%s:%d", type, address, port);
}

void build_transport_tag_from_pjsip_transport(char *dest, pjsip_transport *t) {
  char address[16];
  const char *type;
  int port;
  int tport = t->local_name.port;

  assert(t->local_name.host.slen < 16);
  strncpy(address, t->local_name.host.ptr, t->local_name.host.slen);
  address[t->local_name.host.slen] = 0;

  if (t->key.type == PJSIP_TRANSPORT_UDP) {
    type = "udp";
    port = tport ? tport : 5060;
  } else if (t->key.type == PJSIP_TRANSPORT_TCP) {
    type = "tcp";
    port = tport ? tport : 5060;
  } else {
    type = "tls";
    port = tport ? tport : 5061;
  }

  build_transport_tag(dest, type, address, port);
}

void build_local_contact(char *dest, pjsip_transport *t,
                         const char *contact_username) {
  char *p = dest;
  int len;
  p += sprintf(p, "<sip:%s@", contact_username);
  len = t->local_name.host.slen;
  memcpy(p, t->local_name.host.ptr, len);
  p += len;
  if (t->local_name.port) {
    p += sprintf(p, ":%d", t->local_name.port);
  }
  if (t->key.type == PJSIP_TRANSPORT_UDP) {
    p += sprintf(p, ">");
  } else if (t->key.type == PJSIP_TRANSPORT_TCP) {
    p += sprintf(p, ";transport=tcp>");
  } else {
    p += sprintf(p, ";transport=tls>");
  }
}

void build_local_contact_from_tpfactory(char *dest, pjsip_tpfactory *tpfactory,
                                        const char *contact_username,
                                        pjsip_transport_type_e type) {
  char *p = dest;
  int len;
  p += sprintf(p, "<sip:%s@", contact_username);
  len = tpfactory->addr_name.host.slen;
  memcpy(p, tpfactory->addr_name.host.ptr, len);
  p += len;
  if (tpfactory->addr_name.port) {
    p += sprintf(p, ":%d", tpfactory->addr_name.port);
  }
  if (type == PJSIP_TRANSPORT_TCP) {
    p += sprintf(p, ";transport=tcp>");
  } else {
    p += sprintf(p, ";transport=tls>");
  }
}

bool set_proxy(pjsip_dialog *dlg, const char *proxy_uri) {
  // Very important: although this function only requires dlg and the proxy_uri,
  // it cannot be called before the function that creates the initial request is
  // called. If we call pjsip_inv_create_uac after this function is called,
  // assertion failure will happen.  This is the reason why we didn't put the
  // call to this function inside function dlg_create.

  pj_status_t status;

  if (!proxy_uri || !proxy_uri[0])
    return true; // nothing to do proxy_uri was not set

  // proxy_uri must contain ";lr".
  char *buf = (char *)pj_pool_zalloc(dlg->pool, 500);
  // char buf[500];
  strcpy(buf, proxy_uri);
  if (!strstr(proxy_uri, ";lr")) {
    strcat(buf, ";lr");
  }
  // addon_log(L_DBG, ">>%s<<\n",buf);

  //	pjsip_route_hdr route_set;
  //	pjsip_route_hdr *route;
  //	const pj_str_t hname = { "Route", 5 };

  pj_list_init(&route_set);

  route = (pjsip_route_hdr *)pjsip_parse_hdr(dlg->pool, &hname, (char *)buf,
                                             strlen(buf), NULL);
  if (!route) {
    status = pjsip_dlg_terminate(dlg); // ToDo:
    printf("pjsip_dlg_terminate status=%i\n", status);
    set_error("pjsip_parse_hdr failed");
    return false;
  }

  pj_list_push_back(&route_set, route);

  pjsip_dlg_set_route_set(dlg, &route_set);

  return true;
}

bool dlg_create(pjsip_dialog **dlg, Transport *transport, const char *from_uri,
                const char *to_uri, const char *request_uri, const char *realm,
                const char *username, const char *password,
                const char *local_contact) {
  // obs: local contact must exists in the stack somewhere. It cannot be
  // allocated dynamically because we don't have a dlg nor a dlg->pool yet.

  pj_status_t status;
  pjsip_dialog *p_dlg;

  pj_str_t from = pj_str((char *)from_uri);
  pj_str_t to = pj_str((char *)to_uri);
  pj_str_t request = pj_str((char *)request_uri);

  pj_str_t contact = pj_str((char *)local_contact);

  status = pjsip_dlg_create_uac(pjsip_ua_instance(), &from, &contact, &to,
                                &request, &p_dlg);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_dlg_create_uac failed");
    return false;
  }

  if (realm && realm[0]) {
    pjsip_cred_info cred[1];
    cred[0].scheme = pj_str((char *)"digest");
    cred[0].realm = pj_str((char *)realm);
    cred[0].username = pj_str((char *)username);
    cred[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
    cred[0].data = pj_str((char *)password);
    status = pjsip_auth_clt_set_credentials(&p_dlg->auth_sess, 1, cred);
    if (status != PJ_SUCCESS) {
      status = pjsip_dlg_terminate(p_dlg); // ToDo:
      set_error("pjsip_auth_clt_set_credentials failed");
      return false;
    }
  }

  *dlg = p_dlg;
  return true;
}

int call_create(Transport *t, unsigned flags, pjsip_dialog *dlg,
                const char *proxy_uri, Document &document) {
  pjsip_inv_session *inv;
  // in_addr addr;
  // addr.s_addr = t->local_addr.ipv4.sin_addr.s_addr;
  // pj_str_t str_addr = pj_str( inet_ntoa(addr) );
  pj_status_t status;

  Call *call = (Call *)pj_pool_alloc(dlg->pool, sizeof(Call));
  pj_bzero(call, sizeof(Call));

  call->transport = t;
  call->outgoing = true;

  pjmedia_sdp_session *sdp = 0;

  if (!process_media(call, dlg, document, false)) {
    close_media(call);
    return -1;
  }

  if (!(flags & CALL_FLAG_DELAYED_MEDIA)) {
    sdp = call->local_sdp;
  }

  status = pjsip_inv_create_uac(dlg, sdp, 0, &inv);
  if (status != PJ_SUCCESS) {
    close_media(call);
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("pjsip_inv_create_uac failed");
    return -1;
  }

  call->inv = inv;

  if (!set_proxy(dlg, proxy_uri)) {
    close_media(call);
    status = pjsip_dlg_terminate(dlg); // ToDo:
    return -1;
  }

  long call_id;
  if (!g_call_ids.add((long)call, call_id)) {
    close_media(call);
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("Failed to allocate id");
    return -1;
  }
  call->id = call_id;
  call->pending_request = -1;
  pjsip_tx_data *tdata;
  status = pjsip_inv_invite(inv, &tdata);
  if (status != PJ_SUCCESS) {
    g_call_ids.remove(call_id, (long&)call);
    close_media(call);
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("pjsip_inv_invite failed");
    return -1;
  }

  if (!add_headers(dlg->pool, tdata, document)) {
    g_call_ids.remove(call_id, (long&)call);
    close_media(call);                 // Todo:
    status = pjsip_dlg_terminate(dlg); // ToDo:
    return -1;
  }

  if (!dlg_set_transport_by_t(t, dlg)) {
    return -1;
  }
  addon_log(L_DBG, "inv=%p tdata=%p\n", (void*)inv, (void*)tdata);

  status = pjsip_inv_send_msg(inv, tdata);
  addon_log(L_DBG, "status=%d\n", status);
  if (status != PJ_SUCCESS) {
    g_call_ids.remove(call_id, (long&)call);
    close_media(call); // Todo:
    // The below code cannot be called here it will cause seg fault
    // status = pjsip_dlg_terminate(dlg); //ToDo:
    set_error("pjsip_inv_send_msg failed");
    return -1;
  }

  // Without this, on_rx_response will not be called
  status = pjsip_dlg_add_usage(dlg, &mod_tester, call);
  if (status != PJ_SUCCESS) {
    g_call_ids.remove(call_id, (long&)call);
    close_media(call);                 // Todo:
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("pjsip_dlg_add_usage failed");
    return -1;
  }
  // addon_log(L_DBG, "pjsip_dlg_add_usage OK\n");

  status = setup_call_conf(call);
  if (status != PJ_SUCCESS) {
    set_error("setup_call_conf failed");
    return -1;
  }

  return call_id;
}

pj_status_t audio_endpoint_send_dtmf(Call *call, AudioEndpoint *ae,
                                     const char *digits, const int mode) {
#define ON_DURATION 200
#define OFF_DURATION 50

  pj_status_t status;

  if (DTMF_MODE_RFC2833 == mode) {
    printf("rfc2833\n");
    if (!ae->med_stream) {
      set_error("Unable to send DTMF: Media not ready");
      return -1;
    }
    pj_str_t ds = pj_str((char *)digits);
    status = pjmedia_stream_dial_dtmf(ae->med_stream, &ds);
    if (status != PJ_SUCCESS) {
      set_error("pjmedia_stream_dial_dtmf failed.");
      return status;
    }

    return PJ_SUCCESS;
  } else {
    printf("inband\n");
    if (!prepare_tonegen(call, ae)) {
      set_error("prepare_tonegen failed.");
      return -1;
    }

    int len = strlen(digits);

    for (int i = 0; i < len; ++i) {
      pjmedia_tone_digit tone;
      tone.digit = digits[i];
      tone.on_msec = ON_DURATION;
      tone.off_msec = OFF_DURATION;
      tone.volume = 0;
      status = pjmedia_tonegen_play_digits((pjmedia_port *)ae->tonegen_cbp.port, 1,
                                             &tone, 0);
      if (status != PJ_SUCCESS) {
        set_error("pjmedia_tonegen_play_digits failed.");
        return status;
      }
    }

    return PJ_SUCCESS;
  }
}

pj_status_t send_dtmf(Call *call, const char *digits, int mode) {
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (me->type != ENDPOINT_TYPE_AUDIO)
      continue;

    if(me->port == 0)
      continue;

    AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

    pj_status_t status = audio_endpoint_send_dtmf(call, ae, digits, mode);
    if (status != PJ_SUCCESS)
      return status;
  }

  return PJ_SUCCESS;
}

// int pjw_call_send_dtmf(long call_id, const char *digits, int mode)
int pjw_call_send_dtmf(long call_id, const char *json) {
#define MAX_LENGTH                                                             \
  31 // pjsip allows for 31 digits (inband allows for 32 digits)

  PJW_LOCK();
  clear_error();

  long val;
  char *digits;
  int mode = 0;
  ;

  Call *call;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"digits", "mode", ""};

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (json_get_string_param(document, "digits", false, &digits) <= 0) {
    goto out;
  }

  if (json_get_int_param(document, "mode", false, &mode) <= 0) {
    goto out;
  }

  if (mode != DTMF_MODE_RFC2833 && mode != DTMF_MODE_INBAND) {
    set_error("Invalid DTMF mode. It must be eiter 0 (RFC2833) or 1 (INBAND).");
    goto out;
  }

  int len;

  char adjusted_digits[MAX_LENGTH + 1]; // use the greater size

  len = strlen(digits);

  if (len > MAX_LENGTH) {
    set_error("DTMF string too long");
    goto out;
  }

  for (int i = 0; i < len; ++i) {
    if (!(digits[i] >= '0' && digits[i] <= '9') &&
        !(digits[i] >= 'a' && digits[i] <= 'f') &&
        !(digits[i] >= 'A' && digits[i] <= 'F') && !(digits[i] == '*') &&
        !(digits[i] == '#')) {
      set_error("Invalid character");
      goto out;
    }
    char d = digits[i];
    if (d == 'e' || d == 'E') {
      adjusted_digits[i] = '*';
    } else if (d == 'f' || d == 'F') {
      adjusted_digits[i] = '#';
    } else {
      adjusted_digits[i] = (char)tolower(d);
    }
  }
  adjusted_digits[len] = 0;
  // addon_log(L_DBG, "adjusted_digits >>%s<<\n", adjusted_digits);

  send_dtmf(call, adjusted_digits, mode);

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_status_t audio_endpoint_remove_port(Call *call, ConfBridgePort *cbp) {
  printf("audio_endpoint_remove_port\n");
  pj_status_t status;

  if(cbp->port) {
    /* 
    no need to call pjmedia_conf_disconnect_port because pjmedia_conf_remove_port calls:
      pjmedia_conf_disconnect_port_from_sources(conf, port);
      pjmedia_conf_disconnect_port_from_sinks(conf, port);
    */

    status = pjmedia_conf_remove_port(call->conf, cbp->slot);
    if (status != PJ_SUCCESS) {
      set_error("pjmedia_conf_remove_port failed");
      return false;
    }
    cbp->slot = 0;

    status = pjmedia_port_destroy(cbp->port);
    if (status != PJ_SUCCESS) {
      set_error("pjmedia_port_destroy failed");
      return false;
    }
    cbp->port = NULL;
  }

  printf("success\n");
  return PJ_SUCCESS;
}


int pjw_call_reinvite(long call_id, const char *json) {
  addon_log(L_DBG, "pjw_call_reinvite call_id=%d\n", call_id);

  PJW_LOCK();
  clear_error();

  unsigned flags = 0;

  long val;
  Call *call;
  pjsip_inv_session *inv;

  pj_status_t status;

  pjsip_tx_data *tdata;
  // pjmedia_sdp_session *sdp = 0;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"delayed_media", "media", ""};

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  inv = call->inv;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (document.HasMember("delayed_media")) {
    if (!document["delayed_media"].IsBool()) {
      set_error("Parameter delayed_media must be a boolean");
      goto out;
    } else {
      if (document["delayed_media"].GetBool()) {
        flags = flags | CALL_FLAG_DELAYED_MEDIA;
      }
    }
  }

  if (!process_media(call, inv->dlg, document, false)) {
    goto out;
  }

  /*
  char buf[2048];
  pjmedia_sdp_print(sdp, buf, 2048);
  printf("local sdp:\n");
  printf("%s\n", buf);
  */

  if (!(flags & CALL_FLAG_DELAYED_MEDIA)) {
    // The below call to create_local_sdp  causes subsequent callt
    // pjsip_inv_reinvite() to fail as  the function wants inv->invite_tsx to be
    // NULL but it will not.

    status = pjsip_inv_set_local_sdp(call->inv, call->local_sdp);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_inv_set_local_sdp failed");
      goto out;
    }
  }

  {
    // assert(inv->invite_tsx==NULL);
    pjmedia_sdp_neg_state state = pjmedia_sdp_neg_get_state(call->inv->neg);
    printf("neg state: %d\n", state);
  }

  // status = pjsip_inv_reinvite(call->inv, NULL, sdp, &tdata);
  status = pjsip_inv_reinvite(call->inv, NULL, NULL, &tdata);
  printf("status=%d\n", status);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_reinvite failed");
    goto out;
  }

  status = pjsip_inv_send_msg(call->inv, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_send_msg failed");
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

// To send INFO and other requests inside dialog
int pjw_call_send_request(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  char *method = NULL;
  char *body = NULL;
  char *ct_type = NULL;
  char *ct_subtype = NULL;

  pj_str_t s_method;
  pjsip_tx_data *tdata;
  pj_status_t status;
  pjsip_method meth;

  pjsip_msg_body *msg_body;

  pj_str_t s_ct_type;
  pj_str_t s_ct_subtype;
  pj_str_t s_body;

  Call *call = NULL;

  long val;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"method",     "body",    "ct_type",
                                "ct_subtype", "headers", ""};

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (json_get_string_param(document, "method", false, &method) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "body", true, &body) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "ct_type", true, &ct_type) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "ct_subtype", true, &ct_subtype) <= 0) {
    goto out;
  }

  if (strcmp(method, "INVITE") == 0 || strcmp(method, "UPDATE") == 0 ||
      strcmp(method, "PRACK") == 0 || strcmp(method, "BYE") == 0) {
    set_error("Invalid method");
    goto out;
  }

  if (body) {
    if (!ct_type || !ct_subtype) {
      set_error("If a body is specified, you must pass ct_type (Content-Type "
                "type) and ct_subtype (Content-Type subtype)");
      goto out;
    }
  }

  s_method = pj_str((char *)method);

  pjsip_method_init_np(&meth, &s_method);

  status = pjsip_dlg_create_request(call->inv->dlg, &meth, -1, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_dlg_create_request failed with status=%i", status);
    goto out;
  }

  if (!add_headers(call->inv->dlg->pool, tdata, document)) {
    goto out;
  }

  if (body && body[0]) {
    s_ct_type = pj_str((char *)ct_type);
    s_ct_subtype = pj_str((char *)ct_subtype);
    s_body = pj_str((char *)body);

    msg_body =
        pjsip_msg_body_create(tdata->pool, &s_ct_type, &s_ct_subtype, &s_body);

    if (!msg_body) {
      set_error("pjsip_msg_body_create failed");
      goto out;
    }
    tdata->msg->body = msg_body;
  }

  status = pjsip_dlg_send_request(call->inv->dlg, tdata, -1, NULL);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_dlg_send_request failed with status=%i", status);
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

int count_media_by_type(Call *call, int type) {
  int total = 0;
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (type == me->type)
      total++;
  }
  return total;
}

// int pjw_call_start_record_wav(long call_id, const char *file)
int pjw_call_start_record_wav(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call;
  pj_status_t status;

  unsigned  media_id = 0;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int ae_count;

  char *file;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"file", "media_id", ""};

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  ae_count = count_media_by_type(call, ENDPOINT_TYPE_AUDIO);

  if (ae_count == 0) {
    set_error("No audio endpoint");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "file", false, &file) <= 0) {
    goto out;
  }

  if (!file[0]) {
    set_error("file cannot be blank string");
    goto out;
  }

  if (ae_count > 1) {
    if (json_get_uint_param(document, "media_id", false, &media_id) <= 0) {
      goto out;
    }
  }

  if ((int)media_id >= call->media_count) {
    set_error("invalid media_id");
    goto out;
  }

  me = (MediaEndpoint *)call->media[media_id];
  if (ENDPOINT_TYPE_AUDIO != me->type) {
    set_error("media_endpoint is not audio endpoint");
    goto out;
  }

  ae = (AudioEndpoint *)me->endpoint.audio;

  if(!ae->stream_cbp.port) {
    set_error("stream port is not ready yet");
    goto out;
  }

  // stop/destroy existing writer
  status = audio_endpoint_stop_record_wav(call, ae);
  if(status != PJ_SUCCESS) {
    goto out;
  }

  if (!prepare_wav_writer(call, ae, file)) {
    set_error("prepare_wav_writer failed");
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_status_t audio_endpoint_start_play_wav(Call *call, AudioEndpoint *ae,
                                          const char *file, unsigned flags, bool end_of_file_event) {
  pj_status_t status;

  if(!ae->stream_cbp.port) {
    set_error("stream port is not ready yet");
    return -1;
  }

  // First stop and destroy existing wav port.
  status = audio_endpoint_stop_play_wav(call, ae);
  if(status != PJ_SUCCESS) {
    return -1;
  }

  if (!prepare_wav_player(call, ae, file, flags, end_of_file_event)) {
    return -1;
  }

  return PJ_SUCCESS;
}

int pjw_call_start_play_wav(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int ae_count;

  unsigned media_id = 0;

  char *file;

  unsigned flags = 0;

  bool end_of_file_event;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"file", "media_id", "end_of_file_event", ""};

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  ae_count = count_media_by_type(call, ENDPOINT_TYPE_AUDIO);

  if (ae_count == 0) {
    set_error("No audio endpoint");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "file", false, &file) <= 0) {
    goto out;
  }

  if (!file[0]) {
    set_error("file cannot be blank string");
    goto out;
  }

  if (json_get_bool_param(document, "end_of_file_event", true, &end_of_file_event) <= 0) {
    goto out;
  }

  if (ae_count > 1) {
    if (json_get_uint_param(document, "media_id", false, &media_id) <= 0) {
      goto out;
    }
  }

  if ((int)media_id >= call->media_count) {
    set_error("invalid media_id");
    goto out;
  }

  me = (MediaEndpoint *)call->media[media_id];
  if (ENDPOINT_TYPE_AUDIO != me->type) {
    set_error("media_endpoint is not audio endpoint");
    goto out;
  }

  ae = (AudioEndpoint *)me->endpoint.audio;

  audio_endpoint_start_play_wav(call, ae, file, flags, end_of_file_event);

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_status_t audio_endpoint_start_speech_synth(Call *call, AudioEndpoint *ae, const char * voice, const char *text) {
  pj_status_t status;

  if(!ae->stream_cbp.port) {
    set_error("stream port is not ready yet");
    return -1;
  }

  // First stop and destroy existing flite port.
  status = audio_endpoint_stop_flite(call, ae);
  if(status != PJ_SUCCESS) {
    return -1;
  }

  if (!prepare_flite(call, ae, voice)) {
    return -1;
  }

  pjmedia_flite_port_speak(ae->flite_cbp.port, text, 0);

  return PJ_SUCCESS;
}

int pjw_call_start_speech_synth(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int ae_count;

  unsigned media_id = 0;

  char *voice;

  char *text;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"voice", "text", "media_id", ""};

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  ae_count = count_media_by_type(call, ENDPOINT_TYPE_AUDIO);

  if (ae_count == 0) {
    set_error("No audio endpoint");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "voice", false, &voice) <= 0) {
    goto out;
  }

  if (!voice[0]) {
    set_error("voice cannot be blank string");
    goto out;
  }

  if (json_get_string_param(document, "text", false, &text) <= 0) {
    goto out;
  }

  if (!text[0]) {
    set_error("text cannot be blank string");
    goto out;
  }

  if (ae_count > 1) {
    if (json_get_uint_param(document, "media_id", false, &media_id) <= 0) {
      goto out;
    }
  }

  if ((int)media_id >= call->media_count) {
    set_error("invalid media_id");
    goto out;
  }

  me = (MediaEndpoint *)call->media[media_id];
  if (ENDPOINT_TYPE_AUDIO != me->type) {
    set_error("media_endpoint is not audio endpoint");
    goto out;
  }

  ae = (AudioEndpoint *)me->endpoint.audio;

  audio_endpoint_start_speech_synth(call, ae, voice, text);

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_status_t audio_endpoint_stop_flite(Call *call, AudioEndpoint *ae) {
  return audio_endpoint_remove_port(call, &ae->flite_cbp);
}

pj_status_t call_stop_audio_endpoints_op(Call *call,
                                         audio_endpoint_stop_op_t op) {
  addon_log(L_DBG, "call_stop_audio_endpoints_op media_count=%d\n",
            call->media_count);
  pj_status_t status;
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (ENDPOINT_TYPE_AUDIO != me->type)
      continue;

    AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

    status = op(call, ae);
    if (status != PJ_SUCCESS) {
      return status;
    }
  }

  return PJ_SUCCESS;
}

pj_status_t audio_endpoint_stop_play_wav(Call *call, AudioEndpoint *ae) {
  return audio_endpoint_remove_port(call, &ae->wav_player_cbp);
}

int pjw_call_stop_play_wav(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  Call *call;

  pj_status_t status;

  long val;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int res;

  unsigned media_id = 0;

  char buffer[MAX_JSON_INPUT];

  Document document;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  res = json_get_uint_param(document, "media_id", true, &media_id);
  if (res <= 0) {
    goto out;
  }

  if (NOT_FOUND_OPTIONAL == res) {
    // Stop play wav in all audio endpoints
    status = call_stop_audio_endpoints_op(call, audio_endpoint_stop_play_wav);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  } else {
    // Stop play wav on specified media_id

    if ((int)media_id >= call->media_count) {
      set_error("invalid media_id");
      goto out;
    }

    me = (MediaEndpoint *)call->media[media_id];
    if (ENDPOINT_TYPE_AUDIO != me->type) {
      set_error("invalid media_id non audio");
      goto out;
    }

    ae = (AudioEndpoint *)me->endpoint.audio;

    status = audio_endpoint_stop_play_wav(call, ae);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_status_t audio_endpoint_stop_record_wav(Call *call, AudioEndpoint *ae) {
  return audio_endpoint_remove_port(call, &ae->wav_writer_cbp);
}

int pjw_call_stop_record_wav(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call = (Call *)val;
  pj_status_t status;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int res;

  unsigned media_id = 0;

  char buffer[MAX_JSON_INPUT];

  Document document;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  res = json_get_uint_param(document, "media_id", true, &media_id);
  if (res <= 0) {
    goto out;
  }

  if (NOT_FOUND_OPTIONAL == res) {
    // Stop record wav in all audio endpoints
    status = call_stop_audio_endpoints_op(call, audio_endpoint_stop_record_wav);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  } else {
    // Stop record wav on specified media_id

    if ((int)media_id >= call->media_count) {
      set_error("invalid media_id");
      goto out;
    }

    me = (MediaEndpoint *)call->media[media_id];
    if (ENDPOINT_TYPE_AUDIO != me->type) {
      set_error("invalid media_id non audio");
      goto out;
    }

    ae = (AudioEndpoint *)me->endpoint.audio;

    status = audio_endpoint_stop_record_wav(call, ae);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_status_t audio_endpoint_stop_fax(Call *call, AudioEndpoint *ae) {
  return audio_endpoint_remove_port(call, &ae->fax_cbp);
}

int pjw_call_start_fax(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call;
  pj_status_t status;

  bool is_sender;
  char *file;
  unsigned flags = 0;
  bool flag;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int ae_count;

  unsigned media_id = 0;

  char buffer[MAX_JSON_INPUT];

  const char *valid_params[] = {"is_sender", "file", "transmit_on_idle",
                                "media_id", ""};

  Document document;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  ae_count = count_media_by_type(call, ENDPOINT_TYPE_AUDIO);

  if (ae_count == 0) {
    set_error("No audio endpoint");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "file", false, &file) <= 0) {
    goto out;
  }

  if (!file[0]) {
    set_error("file cannot be blank string");
    goto out;
  }

  if (ae_count > 1) {
    if (json_get_uint_param(document, "media_id", false, &media_id) <= 0) {
      goto out;
    }
  }

  if ((int)media_id >= call->media_count) {
    set_error("invalid media_id");
    goto out;
  }

  me = (MediaEndpoint *)call->media[media_id];
  if (ENDPOINT_TYPE_AUDIO != me->type) {
    set_error("media_endpoint is not audio endpoint");
    goto out;
  }

  ae = (AudioEndpoint *)me->endpoint.audio;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_bool_param(document, "is_sender", false, &is_sender) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "file", false, &file) <= 0) {
    goto out;
  }

  if (!file[0]) {
    set_error("file cannot be blank string");
    goto out;
  }

  flag = false;
  if (json_get_bool_param(document, "transmit_on_idle", true, &flag) <= 0) {
    goto out;
  } else {
    if (flag)
      flags |= FAX_FLAG_TRANSMIT_ON_IDLE;
  }

  // First stop and destroy existing fax port.
  status = audio_endpoint_stop_fax(call, ae);
  if(status != PJ_SUCCESS) {
    set_error("audio_endpoint_stop_fax failed");
    return -1;
  }

  if (!prepare_fax(call, ae, is_sender, file, flags)) {
    set_error("prepare_fax failed");
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

int pjw_call_stop_fax(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call;

  pj_status_t status;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  int res;

  unsigned media_id = 0;

  char buffer[MAX_JSON_INPUT];

  Document document;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  res = json_get_uint_param(document, "media_id", true, &media_id);
  if (res <= 0) {
    goto out;
  }

  if (NOT_FOUND_OPTIONAL == res) {
    // Stop fax in all audio endpoints
    status = call_stop_audio_endpoints_op(call, audio_endpoint_stop_fax);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  } else {
    // Stop fax on specified media_id

    if ((int)media_id >= call->media_count) {
      set_error("invalid media_id");
      goto out;
    }

    me = (MediaEndpoint *)call->media[media_id];
    if (ENDPOINT_TYPE_AUDIO != me->type) {
      set_error("invalid media_id non audio");
      goto out;
    }

    ae = (AudioEndpoint *)me->endpoint.audio;

    status = audio_endpoint_stop_fax(call, ae);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

int pjw_call_get_stream_stat(long call_id, const char *json, char *out_stats) {
  PJW_LOCK();
  clear_error();

  long val;
  Call *call;

  char buffer[MAX_JSON_INPUT];

  Document document;

  pj_status_t status;
  pjmedia_rtcp_stat stat;
  pjmedia_stream_info stream_info;

  ostringstream oss;

  MediaEndpoint *me;
  AudioEndpoint *ae;
  VideoEndpoint *ve;

  unsigned media_id = 0;

  pjmedia_stream *med_stream = NULL;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!json_get_uint_param(document, "media_id", false, &media_id)) {
    goto out;
  }

  if ((int)media_id >= call->media_count) {
    set_error("invalid media_id");
    goto out;
  }

  me = (MediaEndpoint *)call->media[media_id];
  if (ENDPOINT_TYPE_AUDIO == me->type) {
    ae = (AudioEndpoint *)me->endpoint.audio;

    if (!ae->med_stream) {
      set_error("Could not get stream stats. No media session");
      goto out;
    }

    med_stream = ae->med_stream;
  } else if (ENDPOINT_TYPE_VIDEO == me->type) {
    ve = (VideoEndpoint *)me->endpoint.video;

    if (!ve->med_stream) {
      set_error("Could not get stream stats. No media session");
      goto out;
    }

    med_stream = ve->med_stream;
  } else {
    set_error("non streaming media endpoint");
    goto out;
  }

  status = pjmedia_stream_get_stat(med_stream, &stat);
  if (status != PJ_SUCCESS) {
    set_error("Could not get stream stats. Call to "
              "pjmedia_stream_get_stream_stat failed with status=%i",
              status);
    goto out;
  }

  status = pjmedia_stream_get_info(med_stream, &stream_info);
  if (status != PJ_SUCCESS) {
    set_error("Could not get stream info. Call to pjmedia_stream_get_info "
              "failed with status=%i",
              status);
    goto out;
  }

  build_stream_stat(oss, &stat, &stream_info);
  strcpy(out_stats, oss.str().c_str());

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

bool media_endpoint_present_in_session_media(
    MediaEndpoint *me, const pjmedia_sdp_session *local_sdp) {
  printf("media_endpoint_present_in_session_media:\n");
  for (unsigned i = 0; i < local_sdp->media_count; i++) {
    pjmedia_sdp_media *media = local_sdp->media[i];
    printf("port: %d %d\n", me->port, media->desc.port);
    printf("media: %.*s %.*s\n", (int)me->media.slen, me->media.ptr,
           (int)media->desc.media.slen, media->desc.media.ptr);
    if (me->port && (me->port == media->desc.port) &&
        (pj_strcmp(&me->media, &media->desc.media) == 0) &&
        (pj_strcmp(&me->transport, &media->desc.transport) == 0) &&
        (pj_strcmp(&me->addr, &media->conn->addr) == 0)) {
      printf("  true\n");
      return true;
    }
  }
  printf("  false\n");
  return false;
}

int find_sdp_media_by_media_endpt(const pjmedia_sdp_session *sdp,
                                  pjmedia_sdp_media **media_out,
                                  MediaEndpoint *me) {
  printf("find_sdp_media_by_media_endpt %p\n", (void*)me);
  for (unsigned int i = 0; i < sdp->media_count; i++) {
    pjmedia_sdp_media *media = sdp->media[i];
    printf("i=%d me->port=%i media->desc.port=%i me->media=%.*s media->desc.media=%.*s me->transport=%.*s media->desc.transport=%.*s\n", i, me->port, media->desc.port, (int)me->media.slen, me->media.ptr, (int)media->desc.media.slen, media->desc.media.ptr, (int)me->transport.slen, me->transport.ptr, (int)media->desc.transport.slen, media->desc.transport.ptr);

    if ((me->port == media->desc.port) &&
        (pj_strcmp(&me->media, &media->desc.media) == 0) &&
        (pj_strcmp(&me->transport, &media->desc.transport) == 0)) {
      *media_out = media;
      printf("found\n");
      return i;
    }
  }
  printf("not found\n");
  return -1;
}

bool is_media_in_active_media(MediaEndpoint *me, MediaEndpoint **active_media,
                              unsigned count) {
  printf("is_media_in_active_media me=%p\n", (void*)me);
  for (unsigned i = 0; i < count; i++) {
    MediaEndpoint *current = active_media[i];
    printf("i=%d current=%p\n", i, (void*)current);
    if (current == me) {
      printf("yes\n");
      return true;
    }
  }
  printf("no\n");
  return false;
}

void gen_media_json(char *dest, int len, Call *call,
                    const pjmedia_sdp_session *local_sdp,
                    const pjmedia_sdp_session *remote_sdp) {
  printf("gen_media_json call_id=%d media_count=%d\n", call->id,
         call->media_count);
  char *p = dest;

  p += sprintf(p, "[");

  for (int i = 0; i < call->media_count; i++) {
    if (i > 0)
      p += sprintf(p, ",");

    MediaEndpoint *me = (MediaEndpoint *)call->media[i];

    pjmedia_sdp_media *dummy;
    int idx = find_sdp_media_by_media_endpt(local_sdp, &dummy, me);

    pjmedia_sdp_media *local_media = local_sdp->media[idx];
    pjmedia_sdp_media *remote_media = remote_sdp->media[idx];

    if(!me->port) {
      switch (me->type) {
        case ENDPOINT_TYPE_AUDIO:
          p += sprintf(p, "{\"type\": \"audio\", \"protocol\": \"%.*s\", \"port\": 0}", (int)me->transport.slen, me->transport.ptr);
          break;
        case ENDPOINT_TYPE_MRCP:
          p += sprintf(p, "{\"type\": \"mrcp\", \"protocol\": \"%.*s\", \"port\": 0}", (int)me->transport.slen, me->transport.ptr);
          break;
        default:  
          p += sprintf(p, "{\"type\": \"unknown\", \"port\": 0}");
      } 
      continue;
    }

    switch (me->type) {
    case ENDPOINT_TYPE_AUDIO: {
      const char *local_mode =
          get_media_mode(local_media->attr, local_media->attr_count);
      const char *remote_mode =
          get_media_mode(remote_media->attr, remote_media->attr_count);

      pjmedia_sdp_conn *local_conn = local_sdp->conn;
      pjmedia_sdp_conn *remote_conn = remote_sdp->conn;

      if (local_media->conn) {
        local_conn = local_media->conn;
      }

      if (remote_media->conn) {
        remote_conn = remote_media->conn;
      }

      pj_str_t *local_addr = &local_conn->addr;
      pj_str_t *remote_addr = &remote_conn->addr;

      p += sprintf(p,
                   "{\"type\": \"audio\", \"protocol\": \"%.*s\", \"local\": {\"addr\": \"%.*s\", "
                   "\"port\": %d, \"mode\": \"%s\"}, \"remote\": {\"addr\": "
                   "\"%.*s\", \"port\": %d, \"mode\": \"%s\"}, \"fmt\": [",
                   (int)me->transport.slen, me->transport.ptr,
                   (int)local_addr->slen, local_addr->ptr, local_media->desc.port,
                   local_mode, (int)remote_addr->slen, remote_addr->ptr,
                   remote_media->desc.port, remote_mode);

      for (unsigned i = 0; i < local_media->desc.fmt_count; i++) {
        if (i > 0)
          p += sprintf(p, ",");
        pj_str_t *fmt = &local_media->desc.fmt[i];
        pjmedia_sdp_attr *attr = pjmedia_sdp_attr_find2(
            local_media->attr_count, local_media->attr, "rtpmap", fmt);
        if (attr) {
          p += sprintf(p, "\"%.*s\"", (int)attr->value.slen, attr->value.ptr);
        } else {
          p += sprintf(p, "\"%.*s\"", (int)fmt->slen, fmt->ptr);
        }
      }
      p += sprintf(p, "]}");
      break;
    }
    case ENDPOINT_TYPE_MRCP: {
      p += sprintf(p,
                   "{\"type\": \"mrcp\", \"protocol\": \"%.*s\", \"local\": {\"port\": %d}, "
                   "\"remote\": {\"port\": %d}}",
                   (int)me->transport.slen, me->transport.ptr,
                   local_sdp->media[idx]->desc.port,
                   remote_sdp->media[idx]->desc.port);
      break;
    }
    default: {
      p += sprintf(p, "{\"type\": \"unknown\"}");

      break;
    }
    }
  }

  p += sprintf(p, "]");
}

bool start_tcp_media(Call *call, MediaEndpoint *me,
                          const pjmedia_sdp_session *local_sdp,
                          const pjmedia_sdp_session *remote_sdp, int idx) {
  char evt[4096];
  pj_status_t status;

  pj_pool_t *pool = call->inv->pool; 

  MrcpEndpoint *e = (MrcpEndpoint *)me->endpoint.mrcp;

  if(e->asock) {
     printf("start_tcp_media asock already set\n");
     return true;
  }

  pjmedia_sdp_media *remote_media = remote_sdp->media[idx];
  pj_str_t *remote_addr;
  if(remote_media->conn) {
    remote_addr = &remote_media->conn->addr;
  } else {
    remote_addr = &remote_sdp->conn->addr;
  }
  printf("start_tcp_media remote port: %d, remote addr: %.*s\n", remote_media->desc.port, (int)remote_addr->slen, remote_addr->ptr);

  pj_sock_t *sock = (pj_sock_t *)pj_pool_alloc(pool, sizeof(pj_sock_t));

  pj_activesock_t *asock = NULL;

  AsockUserData *ud = NULL;

  status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, sock);
  if (status != PJ_SUCCESS || *sock == PJ_INVALID_SOCKET) {
    make_evt_media_update(evt, sizeof(evt), call->id, "unable to create tcp socket)", "");
    dispatch_event(evt);
    return false;
  }

  pj_ioqueue_t *ioqueue = pjsip_endpt_get_ioqueue(g_sip_endpt);

  status = pj_activesock_create(pool, *sock, pj_SOCK_STREAM(), NULL, ioqueue, &activesock_cb, NULL, &asock);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id, "pj_activesock_create failed", "");
    dispatch_event(evt);
    return false;
  }

  ud = (AsockUserData*)pj_pool_zalloc(pool, sizeof(AsockUserData));
  ud->sock = *sock;
  ud->sip_endpt = g_sip_endpt;
  ud->media_endpt = me;
  ud->call = call;

  status = pj_activesock_set_user_data(asock, ud);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id, "pj_activesock_set_user_data", "");
    dispatch_event(evt);
    return false;
  }

  pj_sockaddr remaddr;

  status = pj_sockaddr_init(pj_AF_INET(), &remaddr, remote_addr, remote_media->desc.port);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id, "pj_sockaddr_init failed", "");
    dispatch_event(evt);
    return false;
  }

  status = pj_activesock_start_connect(asock, pool, &remaddr, sizeof(remaddr));
  if (status != PJ_SUCCESS && status != PJ_EPENDING) {
    make_evt_media_update(evt, sizeof(evt), call->id, "pj_activesock_start_connect failed", "");
    dispatch_event(evt);
    return false;
  }

  e->asock = asock;
  printf("start_tcp_media asock set\n");

  return true;
}


bool restart_media_stream(Call *call, MediaEndpoint *me,
                          const pjmedia_sdp_session *local_sdp,
                          const pjmedia_sdp_session *remote_sdp, int idx) {
  char evt[4096];
  pjmedia_stream_info stream_info;

  AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

  pj_status_t status;

  if(ae->stream_cbp.port) {
    if(ae->tonegen_cbp.port) {
      status = pjmedia_conf_disconnect_port(call->conf, ae->tonegen_cbp.slot, ae->stream_cbp.slot);
      if (status != PJ_SUCCESS) {
        make_evt_media_update(evt, sizeof(evt), call->id,
                            "setup_failed (pjmedia_conf_disconnect_port for tonegen failed)", "");
        dispatch_event(evt);
        return false;
      }
    }

    if(ae->wav_player_cbp.port) {
      status = pjmedia_conf_disconnect_port(call->conf, ae->wav_player_cbp.slot, ae->stream_cbp.slot);
      if (status != PJ_SUCCESS) {
        make_evt_media_update(evt, sizeof(evt), call->id,
                           "setup_failed (pjmedia_conf_disconnect_port for wav_player failed)", "");
        dispatch_event(evt);
        return false;
      }
    }

    if(ae->dtmfdet_cbp.port) {
      status = pjmedia_conf_disconnect_port(call->conf, ae->stream_cbp.slot, ae->dtmfdet_cbp.slot);
      if (status != PJ_SUCCESS) {
        make_evt_media_update(evt, sizeof(evt), call->id,
                           "setup_failed (pjmedia_conf_disconnect_port for dtmfdet failed)", "");
        dispatch_event(evt);
        return false;
      }
    }

    if(ae->fax_cbp.port) {
      status = pjmedia_conf_disconnect_port(call->conf, ae->stream_cbp.slot, ae->fax_cbp.slot);
      if (status != PJ_SUCCESS) {
        make_evt_media_update(evt, sizeof(evt), call->id,
                           "setup_failed (pjmedia_conf_disconnect_port fax dst failed)", "");
        dispatch_event(evt);
        return false;
      }

      status = pjmedia_conf_disconnect_port(call->conf, ae->fax_cbp.slot, ae->stream_cbp.slot);
      if (status != PJ_SUCCESS) {
        make_evt_media_update(evt, sizeof(evt), call->id,
                           "setup_failed (pjmedia_conf_disconnect_port for fax src failed)", "");
        dispatch_event(evt);
        return false;
      }
    }

    status = pjmedia_conf_remove_port(call->conf, ae->stream_cbp.slot);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                           "setup_failed (pjmedia_conf_remove_port failed)", "");
      return false;
    }
    ae->stream_cbp.slot = 0;

    status = pjmedia_port_destroy(ae->stream_cbp.port);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                           "setup_failed (pjmedia_port_destroy failed)", "");
      return false;
    }
    ae->stream_cbp.port = NULL;
  } 

  status =
      pjmedia_stream_info_from_sdp(&stream_info, call->inv->dlg->pool,
                                   g_med_endpt, local_sdp, remote_sdp, idx);
  if (status != PJ_SUCCESS) {
    printf("local  media_count=%d\n", local_sdp->media_count);
    printf("remote media_count=%d\n", remote_sdp->media_count);
    printf("pjmedia_stream_info_from_sdp failed idx=%d\n", idx);
    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_stream_info_from_sdp failed)",
                          "");
    dispatch_event(evt);
    return false;
  }

  if (ae->med_stream) {
    status = pjmedia_stream_destroy(ae->med_stream);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                            "setup_failed (pjmedia_destroy failed)", "");
      dispatch_event(evt);
      return false;
    }
  }

  status =
      pjmedia_stream_create(g_med_endpt, call->inv->dlg->pool, &stream_info,
                            ae->med_transport, NULL, &ae->med_stream);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_stream_create failed)", "");
    dispatch_event(evt);
    return false;
  }

  status = pjmedia_stream_start(ae->med_stream);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_stream_start failed)", "");
    dispatch_event(evt);
    return false;
  }

  status = pjmedia_stream_set_dtmf_callback(ae->med_stream, &on_dtmf, call);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_stream_set_dtmf_callback)", "");
    dispatch_event(evt);
    return false;
  }

  /* Start the UDP media transport */
  status = pjmedia_transport_media_start(ae->med_transport, call->inv->pool, local_sdp, remote_sdp, idx);
  if (status != PJ_SUCCESS) {
    printf("status=%i\n", status);
    char err[1024];
    pj_strerror(status, err, sizeof(err));
    printf("pjmedia_transport_media_start status: %s\n", err);

    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_transport_media_start failed)", "");
    dispatch_event(evt);
    return false;
  }

  status = pjmedia_stream_get_port(ae->med_stream, &ae->stream_cbp.port);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_stream_get_port failed)", "");
    dispatch_event(evt);
    return false;
  }

  status = pjmedia_conf_add_port(call->conf, call->inv->pool, ae->stream_cbp.port, NULL, &ae->stream_cbp.slot);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_conf_add_port failed)", "");
    dispatch_event(evt);
    return false;
  }

  if(!ae->dtmfdet_cbp.port) {
    if(!prepare_dtmfdet(call, ae)) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (prepare_dtmfdet failed)", "");
      dispatch_event(evt);
      return false;
    }
  }

  if(ae->tonegen_cbp.port) {
    status = pjmedia_conf_connect_port(call->conf, ae->tonegen_cbp.slot, ae->stream_cbp.slot, 0);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_conf_connect_port for tonegen failed)", "");
      dispatch_event(evt);
      return false;
    }
  }

  if(ae->wav_player_cbp.port) {
    status = pjmedia_conf_connect_port(call->conf, ae->wav_player_cbp.slot, ae->stream_cbp.slot, 0);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_conf_connect_port for wav_player failed)", "");
      dispatch_event(evt);
      return false;
    }
  }

  if(ae->dtmfdet_cbp.port) {
    status = pjmedia_conf_connect_port(call->conf, ae->stream_cbp.slot, ae->dtmfdet_cbp.slot, 0);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_conf_connect_port for dtmfdet failed)", "");
      dispatch_event(evt);
      return false;

    }
  }

  if(ae->fax_cbp.port) {
    status = pjmedia_conf_connect_port(call->conf, ae->stream_cbp.slot, ae->fax_cbp.slot, 0);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_conf_connect_port for fax dst failed)", "");
      dispatch_event(evt);
      return false;

    }

    status = pjmedia_conf_connect_port(call->conf, ae->fax_cbp.slot, ae->stream_cbp.slot, 0);
    if (status != PJ_SUCCESS) {
      make_evt_media_update(evt, sizeof(evt), call->id,
                          "setup_failed (pjmedia_conf_connect_port for fax src failed)", "");
      dispatch_event(evt);
      return false;
    }
  }

  return true;
}

MediaEndpoint *find_media_endpt_by_sdp_media(Call *call,
                                             pjmedia_sdp_media *local_media,
                                             bool in_use_chart[]) {
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = call->media[i];
    if (in_use_chart[i])
      continue;

    if (pj_strcmp2(&local_media->desc.media, "audio") == 0) {
      if (ENDPOINT_TYPE_AUDIO == me->type) {
        if (pj_strcmp(&local_media->desc.transport, &me->transport)) {
          in_use_chart[i] = true;
          return me;
        }
      }
    } else if (pj_strcmp2(&local_media->desc.media, "video") == 0) {
      if (ENDPOINT_TYPE_VIDEO == me->type) {
        if (pj_strcmp(&local_media->desc.transport, &me->transport)) {
          in_use_chart[i] = true;
          return me;
        } 
      }
    } else if (pj_strcmp2(&local_media->desc.media, "application") == 0) {
      if (ENDPOINT_TYPE_MRCP == me->type) {
        if (pj_strcmp(&local_media->desc.transport, &me->transport)) {
          in_use_chart[i] = true;
          return me;
        }
      }
    } else {
      printf("local_media->desc.media=%.*s\n", (int)local_media->desc.media.slen,
             local_media->desc.media.ptr);
      assert(0);
      // missing media type support implementation
    }
  }

  return NULL;
}

static void on_media_update(pjsip_inv_session *inv, pj_status_t status) {
  addon_log(L_DBG, "on_media_update\n");
  char evt[4096];

  if (g_shutting_down)
    return;

  Call *call = (Call *)inv->dlg->mod_data[mod_tester.id];

  long call_id;
  if (!g_call_ids.get_id((long)call, call_id)) {
    addon_log(L_DBG, "on_media_update: Failed to get call_id. Event will not "
                     "be notified.\n");
    return;
  }
  printf("call_id=%li\n", call_id);

  const pjmedia_sdp_session *local_sdp;
  const pjmedia_sdp_session *remote_sdp;

  ostringstream oss;

  if (status != PJ_SUCCESS) {
    // negotiation failed
    make_evt_media_update(evt, sizeof(evt), call_id, "negotiation_failed", "");
    dispatch_event(evt);
    return;
  }

  status = pjmedia_sdp_neg_get_active_local(inv->neg, &local_sdp);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(
        evt, sizeof(evt), call_id,
        "setup_failed (pjmedia_sdp_neg_get_active_local failed)", "");
    dispatch_event(evt);
    return;
  }

  status = pjmedia_sdp_neg_get_active_remote(inv->neg, &remote_sdp);
  if (status != PJ_SUCCESS) {
    make_evt_media_update(
        evt, sizeof(evt), call_id,
        "setup_failed (pjmedia_sdp_neg_get_active_remote failed)", "");
    dispatch_event(evt);
    return;
  }

  char b[2048];
  pjmedia_sdp_print(local_sdp, b, sizeof(b));
  addon_log(L_DBG, "on_media_update call_id=%d active local_sdp: %s\n",
            call->id, b);

  pjmedia_sdp_print(remote_sdp, b, sizeof(b));
  addon_log(L_DBG, "on_media_update call_id=%d active remote_sdp: %s\n",
            call->id, b);

  // update media endpoint based on sdp media

  for (unsigned i = 0; i < local_sdp->media_count; i++) {
    MediaEndpoint *me = call->media[i];
    if (!local_sdp->media[i]->desc.port) {
      close_media_endpoint(call, me);
    } else {
      if (me->type == ENDPOINT_TYPE_AUDIO) {
        if (!restart_media_stream(call, me, local_sdp, remote_sdp, i)) {
          return;
        }
      } else if(me->type == ENDPOINT_TYPE_MRCP) {
        if(call->outgoing) {
          if(!start_tcp_media(call, me, local_sdp, remote_sdp, i)) {
            return;
          }
        }
      }
    }
  }

  char media[4096];
  gen_media_json(media, sizeof(media), call, local_sdp, remote_sdp);

  make_evt_media_update(evt, sizeof(evt), call_id, "ok", media);
  dispatch_event(evt);
}

static void on_state_changed(pjsip_inv_session *inv, pjsip_event *e) {
  addon_log(L_DBG, "on_state_changed\n");

  // The below is just to document know-how for future improvements
  /*
  addon_log(L_DBG, "on_state_changed e->type=%i\n", e->type);
  if(e->type == PJSIP_EVENT_TSX_STATE && e->body.tsx_state.type ==
  PJSIP_EVENT_RX_MSG) {
          // Read http://trac.pjsip.org/repos/wiki/SIP_Message_Buffer_Event
          addon_log(L_DBG, "Msg=%s\n",
  e->body.tsx_state.src.rdata->msg_info.msg_buf);
  }
  */

  printf("e->type=%d\n", e->type);

  /*
      pj_str_t *method_name = &rdata->msg_info.msg->line.req.method.name;
      addon_log(L_DBG, "on_rx_request %.*s\n", method_name->slen,
     method_name->ptr);
  */

  if (g_shutting_down)
    return;

  Call *call = (Call *)inv->dlg->mod_data[mod_tester.id];

  printf("inv->state=%d\n", inv->state);

  if (PJSIP_INV_STATE_DISCONNECTED == inv->state) {
    addon_log(L_DBG, "call will terminate call=%p\n", (void*)call);
    pj_status_t status;

    long call_id;
    if (!g_call_ids.get_id((long)call, call_id)) {
      addon_log(L_DBG, "on_state_changed: Failed to get call_id. Event will "
                       "not be notified.\n");
      return;
    }

    close_media(call);

    for (int i = 0; i < call->media_count; i++) {
      addon_log(L_DBG, "processing media[%d]\n", i);
      MediaEndpoint *me = call->media[i];
      if (ENDPOINT_TYPE_AUDIO == me->type) {
        AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;
        addon_log(L_DBG, "processing media[%d] as AudioEndpoint\n", i);

        if (ae->med_stream) {
          addon_log(L_DBG, "calling pjmedia_stream_destroy");
          status = pjmedia_stream_destroy(ae->med_stream);
          if (status != PJ_SUCCESS) {
            addon_log(L_DBG, "pjmedia_stream_destroy failed\n");
          }
        }
      }
    }

    release_call_conf(call);

    long val;
    if (!g_call_ids.remove(call_id, val)) {
      addon_log(L_DBG, "g_call_ids.remove failed\n");
    }

    Pair_Call_CallId pcc;
    pcc.pCall = call;
    pcc.id = call_id;
    g_LastCalls.push_back(pcc);


    char evt[2048];
    int sip_msg_len = 0;
    char *sip_msg = (char *)"";
    if (e->type == PJSIP_EVENT_TSX_STATE) {
      sip_msg_len = e->body.rx_msg.rdata->msg_info.len;
      sip_msg = e->body.rx_msg.rdata->msg_info.msg_buf;
    }

    make_evt_call_ended(evt, sizeof(evt), call_id, sip_msg_len, sip_msg);
    dispatch_event(evt);
  }
}

static void on_forked(pjsip_inv_session *inv, pjsip_event *e) {
  if (g_shutting_down)
    return;
}

static pjmedia_transport *create_media_transport(const pj_str_t *addr,
                                                 pj_uint16_t *allocated_port) {
  printf("create_media_transport\n");
  pjmedia_transport *med_transport;
  pj_status_t status;
  for (int i = 0; i < 1000; ++i) {
    int port = 10000 + (i * 2);
    //printf("trying port=%i\n", port);
    status = pjmedia_transport_udp_create3(g_med_endpt, AF, NULL, addr, port, 0,
                                           &med_transport);
    if (status == PJ_SUCCESS) {
      pjmedia_transport_info tpinfo;
      pjmedia_transport_info_init(&tpinfo);
      status = pjmedia_transport_get_info(med_transport, &tpinfo);
      //printf("create_media_transport port=%i created %p\n", port,  (void*)med_transport);
      *allocated_port = port;
      return med_transport;
    } else {
      char err[1024];
      pj_strerror(status, err, sizeof(err));

      printf("pjmedia_transport_udp_create3 status=%i (%s)\n", status, err);
    }
  }
  printf("no port available\n");
  return NULL;
}

static void process_subscribe_request(pjsip_rx_data *rdata) {
  char evt[2048];
  ostringstream oss_err;
  ostringstream oss;
  pjsip_dialog *dlg = NULL;
  pjsip_evsub_user user_cb;
  pjsip_evsub *evsub;
  pj_status_t status;
  Subscriber *subscriber;
  long subscriber_id;
  char local_contact[1000];
  pjsip_tx_data *tdata;
  pjsip_transport *t = rdata->tp_info.transport;

  memset(&user_cb, 0, sizeof(user_cb));
  // user_cb.on_evsub_state = server_on_evsub_state;
  user_cb.on_rx_refresh = server_on_evsub_rx_refresh;

  build_local_contact(local_contact, rdata->tp_info.transport, "sip-tester");
  pj_str_t url = pj_str(local_contact);

  status =
      pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(), rdata, &url, &dlg);
  if (status != PJ_SUCCESS) {
    make_evt_internal_error(evt, sizeof(evt), "error p1");
    dispatch_event(evt);
    goto out;
  }

  status = pjsip_evsub_create_uas(dlg, &user_cb, rdata, 0, &evsub);
  if (status != PJ_SUCCESS) {
    make_evt_internal_error(evt, sizeof(evt), "error p2");
    dispatch_event(evt);
    goto out;
  }

  subscriber = (Subscriber *)pj_pool_zalloc(dlg->pool, sizeof(Subscriber));
  if (!g_subscriber_ids.add((long)subscriber, subscriber_id)) {
    make_evt_internal_error(evt, sizeof(evt), "error p3");
    dispatch_event(evt);
    goto out;
  }
  subscriber->id = subscriber_id;
  subscriber->evsub = evsub;
  subscriber->dlg = dlg;

  pjsip_evsub_set_mod_data(evsub, mod_tester.id, subscriber);

  status = dlg_set_transport(t, dlg);
  if (!status) {
    make_evt_internal_error(evt, sizeof(evt), "dlg_set_transport failed");
    dispatch_event(evt);
    goto out;
  }

  status = pjsip_evsub_accept(evsub, rdata, 200, NULL);
  if (status != PJ_SUCCESS) {
    make_evt_internal_error(evt, sizeof(evt), "error p5");
    dispatch_event(evt);
    goto out;
  }

  status = pjsip_evsub_notify(
      evsub, (pjsip_evsub_state)PJSIP_EVSUB_STATE_ACTIVE, NULL, NULL, &tdata);
  if (status != PJ_SUCCESS) {
    make_evt_internal_error(evt, sizeof(evt), "error p6");
    dispatch_event(evt);
    goto out;
  }

  status = pjsip_evsub_send_request(evsub, tdata);
  if (status != PJ_SUCCESS) {
    make_evt_internal_error(evt, sizeof(evt), "error p7");
    dispatch_event(evt);
    goto out;
  }

out:
  if (status != PJ_SUCCESS) {
    // pj_str_t s_reason = pj_str(pjw_errorstring);
    if (dlg) {
      status = pjsip_dlg_create_response(dlg, rdata, 500, NULL, &tdata);
      if (status == PJ_SUCCESS) {
        pjsip_dlg_send_response(dlg, pjsip_rdata_get_tsx(rdata), tdata);
      }
    } else {
      pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, NULL, NULL, NULL);
    }
  } else {
    make_evt_request(evt, sizeof(evt), "subscriber", subscriber_id,
                     rdata->msg_info.len, rdata->msg_info.msg_buf);
    dispatch_event(evt);
  }
}

pj_status_t process_invite(Call *call, pjsip_inv_session *inv,
                           pjsip_rx_data *rdata) {
  pj_status_t status;
  pj_str_t reason;
  pjsip_rx_data *cloned_rdata = 0;

  status = pjsip_rx_data_clone(rdata, 0, &cloned_rdata);

  if (status != PJ_SUCCESS) {
    reason =
        pj_str((char *)"Internal Server Error (pjsip_rx_data_clone failed)");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return -1;
  }

  if (!(g_flags & FLAG_NO_AUTO_100_TRYING)) {
    pjsip_tx_data *tdata;
    // First response to an INVITE must be created with
    // pjsip_inv_initial_answer(). Subsequent responses to the same transaction
    // MUST use pjsip_inv_answer(). Create 100 response
    status = pjsip_inv_initial_answer(inv, rdata, 100, NULL, NULL, &tdata);
    if (status != PJ_SUCCESS) {
      reason = pj_str(
          (char *)"Internal Server Error (pjsip_inv_initial_answer failed)");
      pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL,
                                    NULL);
      return -1;
    }

    // Send 100 response
    status = pjsip_inv_send_msg(inv, tdata);
    if (status != PJ_SUCCESS) {
      reason =
          pj_str((char *)"Internal Server Error (pjsip_inv_send_msg failed)");
      pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL,
                                    NULL);
      return -1;
    }

    call->inv_initial_answer_required = false;
  } else {
    call->inv_initial_answer_required = true;
  }

  call->pending_rdata = cloned_rdata;

  return PJ_SUCCESS;
}

static pj_bool_t on_rx_request(pjsip_rx_data *rdata) {
  // This is not called for CANCEL
  printf("on_rx_request\n");
  char evt[2048];

  pj_str_t *method_name = &rdata->msg_info.msg->line.req.method.name;
  addon_log(L_DBG, "on_rx_request %.*s\n", method_name->slen, method_name->ptr);
  if (g_shutting_down)
    return PJ_TRUE;

  pj_status_t status;
  pj_str_t reason;

  pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
  // pjsip_dialog *dlg = pjsip_ua_find_dialog(&rdata->msg_info.cid->id,
  // &rdata->msg_info.to->tag, &rdata->msg_info.from->tag, PJ_FALSE);

  if (dlg) {
    printf("method inside dlg\n");
    if (rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD) {
      return PJ_TRUE;
    }

    void *user_data = (Call *)dlg->mod_data[mod_tester.id];

    long call_id;
    if (!g_call_ids.get_id((long)user_data, call_id)) {
      // not CAll. It might be subscriptoin
      return PJ_TRUE;
    }

    Call *call = (Call *)user_data;

    if (rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD) {
      pjsip_rx_data *cloned_rdata = 0;
      if (pjsip_rx_data_clone(rdata, 0, &cloned_rdata) != PJ_SUCCESS) {
        const pj_str_t reason = pj_str(
            (char *)"Internal Server Error (pjsip_rx_data_clone failed)");
        pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL,
                                      NULL);
        return PJ_TRUE;
      }

      call->pending_rdata = cloned_rdata;

      for (int i = 0; i < PJSIP_MAX_MODULE; i++) {
        cloned_rdata->endpt_info.mod_data[i] = rdata->endpt_info.mod_data[i];
      }

      make_evt_request(evt, sizeof(evt), "call", call->id, rdata->msg_info.len,
                       rdata->msg_info.msg_buf);
      call->pending_request = rdata->msg_info.msg->line.req.method.id;

      dispatch_event(evt);

      return PJ_TRUE;
    }
  }

  if (dlg &&
      (pj_strcmp2(&rdata->msg_info.msg->line.req.method.name, "REFER") == 0)) {
    // Refer within dialog
    // We cannot call process_in_dialog_refer from this callback (so we copied
    // the way it is done by pjsua, using on_tsx_state_changed)
    // process_in_dialog_refer(&oss, dlg, rdata);
    // addon_log(L_DBG, "received REFER on_rx_request\n");
    return PJ_TRUE;
  }

  if (pj_strcmp2(&rdata->msg_info.msg->line.req.method.name, "SUBSCRIBE") ==
      0) {
    if (dlg) {
      process_in_dialog_subscribe(dlg, rdata);
    } else {
      process_subscribe_request(rdata);
    }
    return PJ_TRUE;
  }

  if (rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD) {
    // Here we handle out-of-dialog requests like REGISTER, OPTIONS, MESSAGE
    // etc.

    pjsip_transport *t = rdata->tp_info.transport;
    Request *request =
        (Request *)pj_pool_zalloc(rdata->tp_info.pool, sizeof(Request));
    request->is_uac = false;
    request->pending_rdata = rdata;

    long request_id;
    if (!g_request_ids.add((long)request, request_id)) {
      addon_log(L_DBG,
                "Failed to allocate request_id. Event will not be notified\n");
      return PJ_TRUE;
    }
    request->id = request_id;

    char tag[64];
    build_transport_tag_from_pjsip_transport(tag, t);

    long transport_id;

    // printf("tag=%s\n", tag);

    TransportMap::iterator iter = g_TransportMap.find(tag);
    if (iter != g_TransportMap.end()) {
      transport_id = iter->second;
    } else {
      transport_id = -1;
    }

    pjsip_rx_data *cloned_rdata = 0;
    if (pjsip_rx_data_clone(rdata, 0, &cloned_rdata) != PJ_SUCCESS) {
      const pj_str_t reason =
          pj_str((char *)"Internal Server Error (pjsip_rx_data_clone failed)");
      pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL,
                                    NULL);
      return PJ_TRUE;
    }
    request->pending_rdata = cloned_rdata;

    // Automatically sending a '100 Trying' (but we might add an option when
    // creating transports to disable this)
    status = pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 100,
                                           &trying_reason, NULL, NULL);
    if (status != PJ_SUCCESS) {
      addon_log(L_DBG, "on_rx_request pjsip_endpt_respond_stateless failed");
    }

    make_evt_non_dialog_request(evt, sizeof(evt), transport_id, request_id,
                                rdata->msg_info.len, rdata->msg_info.msg_buf);
    dispatch_event(evt);
    return PJ_TRUE;
  }

  unsigned options = 0;
  status =
      pjsip_inv_verify_request(rdata, &options, NULL, NULL, g_sip_endpt, NULL);
  if (status != PJ_SUCCESS) {
    reason = pj_str((char *)"Unable to handle this REQUEST");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return PJ_TRUE;
  }

  char local_contact[1000];
  build_local_contact(local_contact, rdata->tp_info.transport, "sip-lab");
  pj_str_t url = pj_str(local_contact);

  status =
      pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(), rdata, &url, &dlg);

  if (status != PJ_SUCCESS) {
    reason = pj_str((char *)"Internal Server Error "
                            "(pjsip_dlg_create_uas_and_inc_lock failed)");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return PJ_TRUE;
  }

  pjsip_inv_session *inv;
  status = pjsip_inv_create_uas(dlg, rdata, NULL, 0, &inv);
  if (status != PJ_SUCCESS) {
    reason =
        pj_str((char *)"Internal Server Error (pjsip_inv_create_uas failed)");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return PJ_TRUE;
  }

  pjsip_transport *t = rdata->tp_info.transport;
  Call *call = (Call *)pj_pool_alloc(inv->pool, sizeof(Call));
  pj_bzero(call, sizeof(Call));

  if (!dlg_set_transport(t, dlg)) {
    reason = pj_str((char *)"Internal Server Error (set_transport failed)");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return PJ_TRUE;
  }

  status = process_invite(call, inv, rdata);
  if (status != PJ_SUCCESS) {
    return PJ_TRUE;
  }

  call->inv = inv;

  if (!inv->dlg) {
    return PJ_TRUE;
  }

  status = setup_call_conf(call);
  if (status != PJ_SUCCESS) {
    printf("setup_call_conf failed\n");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return PJ_TRUE;
  }

  // TODO: check if this is really necessary as we are calling
  // pjsip_dlg_add_usage subsequently
  inv->dlg->mod_data[mod_tester.id] = call;

  // Without this, on_rx_response will not be called
  status = pjsip_dlg_add_usage(dlg, &mod_tester, call);
  if (status != PJ_SUCCESS) {
    reason =
        pj_str((char *)"Internal Server Error (pjsip_dlg_add_usage failed)");
    pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
    return PJ_TRUE;
  }

  long call_id;
  if (!g_call_ids.add((long)call, call_id)) {
    addon_log(L_DBG,
              "Failed to allocate call_id. Event will not be notifield\n");
    return PJ_TRUE;
  }

  char tag[64];
  build_transport_tag_from_pjsip_transport(tag, t);

  long transport_id;

  // printf("tag=%s\n", tag);

  TransportMap::iterator iter = g_TransportMap.find(tag);
  if (iter != g_TransportMap.end()) {
    transport_id = iter->second;
  } else {
    transport_id = -1;
  }

  // printf("transport_id=%d\n", transport_id);

  long val;
  if (g_transport_ids.get(transport_id, val)) {
    Transport *transport = (Transport *)val;
    call->transport = transport;
  } else {
    printf("could not resolve transport id=%li\n", transport_id);
    exit(1);
  }

  call->id = call_id;

  make_evt_incoming_call(evt, sizeof(evt), transport_id, call_id,
                         rdata->msg_info.len, rdata->msg_info.msg_buf);
  dispatch_event(evt);
  call->pending_request = PJSIP_INVITE_METHOD;

  return PJ_TRUE;
}

static pj_bool_t on_rx_response(pjsip_rx_data *rdata) {
  addon_log(L_DBG, "on_rx_response\n");
  // Very important: this callback notifies reception of any SIP response
  // received by the endpoint, no matter if the endpoint was the one
  // that sent the request or not (for example, if the app is running
  // in a loop and breaks and restarts immediately, it will get responses
  // destined to its previous incarnation. So we must check if the
  // response is associated with a dialog, otherwise: crash.
  if (g_shutting_down)
    return PJ_TRUE;

  char evt[2048];
  pj_str_t mname;

  pjsip_cseq_hdr *cseq = rdata->msg_info.cseq;
  char method[100];
  int len = cseq->method.name.slen;
  strncpy(method, cseq->method.name.ptr, len);
  method[len] = 0;

  ostringstream oss;

  pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
  if (!dlg) {
    // addon_log(L_DBG, "No dialog associated with rdata\n");
    return PJ_TRUE;
  }

  Call *call = (Call *)dlg->mod_data[mod_tester.id];

  if (strcmp(method, "SUBSCRIBE") == 0) {
    return PJ_TRUE;
  }

  long call_id;

  if (call) {
    // addon_log(L_DBG, "call:%p\n", (void*)call);
    if (!g_call_ids.get_id((long)call, call_id)) {
      // addon_log(L_DBG, "The call is not present in g_call_ids.\n");
      //  It means the call terminated and was removed from g_call_ids\n");
      // So let's try to find it at g_LastCalls

      boost::circular_buffer<Pair_Call_CallId>::iterator iter;
      Pair_Call_CallId pcc;
      pcc.pCall = call;
      iter = find(g_LastCalls.begin(), g_LastCalls.end(), pcc);
      if (iter == g_LastCalls.end()) {
        oss.seekp(0);
        oss << "event=internal_error" << EVT_DATA_SEP
            << "details=Failed to get call_id";
        addon_log(L_DBG, "on_rx_response failed to resolve call_id\n");
        return true;
      }
      call_id = iter->id;
    }
  } else {
    addon_log(L_DBG, "Ignoring response for mod_data not set to a call\n");
    return PJ_TRUE;
  }

  mname = rdata->msg_info.cseq->method.name;
  make_evt_response(evt, sizeof(evt), "call", call_id, mname.slen, mname.ptr,
                    rdata->msg_info.len, rdata->msg_info.msg_buf);
  dispatch_event(evt);

  return PJ_TRUE;
}

static pjsip_redirect_op on_redirected(pjsip_inv_session *inv,
                                       const pjsip_uri *target,
                                       const pjsip_event *e) {
  PJ_UNUSED_ARG(e);
  return PJSIP_REDIRECT_ACCEPT;
}

static void on_rx_offer2(pjsip_inv_session *inv,
                         struct pjsip_inv_on_rx_offer_cb_param *param) {
  addon_log(L_DBG, "on_rx_offer2\n");
  if (g_shutting_down)
    return;

  Call *call = (Call *)inv->dlg->mod_data[mod_tester.id];

  printf("on_rx_offer2 call_id=%d\n", call->id);

  pj_status_t status;

  pjmedia_sdp_neg_state state = pjmedia_sdp_neg_get_state(inv->neg);
  printf("neg state: %d\n", state);
  if (PJMEDIA_SDP_NEG_STATE_NULL == state ||
      PJMEDIA_SDP_NEG_STATE_DONE == state) {
    call->remote_sdp = (pjmedia_sdp_session *)param->offer;
    status = pjmedia_sdp_neg_set_remote_offer(inv->dlg->pool, inv->neg,
                                              param->offer);
    if (status != PJ_SUCCESS) {
      printf("error: pjmedia_sdp_neg_set_remote_offer failed\n");
      exit(1);
      return;
    }
  } else {
    // this is delayed media scenario
    // So we must generate SDP answer based on media set when call was created.

    status = pjsip_inv_set_sdp_answer(call->inv, call->local_sdp);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_inv_set_sdp_answer failed");
      close_media(call);
      return;
    }
  }

  // The below cannot be used: in case of delayed media scenarios, on_rx_offer
  // and on_rx_offer2 will be called when the '200 OK' is received for an INVITE
  // without SDP. So we will send this event from on_rx_reinvite
  /*
  make_evt_reinvite(evt, sizeof(evt), call_id, rdata->msg_info.len,
  rdata->msg_info.msg_buf); dispatch_event(evt);
  */

  return;
}

static pj_status_t on_rx_reinvite(pjsip_inv_session *inv,
                                  const pjmedia_sdp_session *offer,
                                  pjsip_rx_data *rdata) {
  printf("on_rx_reinvite\n");

  char evt[2048];

  Call *call = (Call *)inv->dlg->mod_data[mod_tester.id];
  printf("on_rx_reinvite call_id=%d\n", call->id);

  pj_status_t status = process_invite(call, inv, rdata);
  if (status != PJ_SUCCESS) {
    return PJ_SUCCESS;
  }

  make_evt_reinvite(evt, sizeof(evt), call->id, rdata->msg_info.len,
                    rdata->msg_info.msg_buf);
  dispatch_event(evt);
  call->pending_request = PJSIP_INVITE_METHOD;

  return PJ_SUCCESS;
}

static void on_dtmf(pjmedia_stream *stream, void *user_data, int digit) {
  //printf("on_dtmf %d\n", digit);
  if (g_shutting_down)
    return;

  char evt[256];

  long call_id;
  if (!g_call_ids.get_id((long)user_data, call_id)) {
    addon_log(L_DBG,
              "on_dtmf: Failed to get call_id. Event will not be notified.\n");
    return;
  }

  char d = (char)tolower((char)digit);
  if (d == '*')
    d = 'e';
  if (d == '#')
    d = 'f';

  Call *call = (Call *)user_data;

  int media_id = find_endpoint_by_inband_dtmf_media_stream(call, stream);
  assert(media_id >= 0);

  MediaEndpoint *me = (MediaEndpoint *)call->media[media_id];
  AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

  int mode = DTMF_MODE_RFC2833;

  if (g_dtmf_inter_digit_timer) {

    PJW_LOCK();
    int *pLen = &ae->DigitBufferLength[mode];

    if (*pLen > MAXDIGITS) {
      PJW_UNLOCK();
      addon_log(L_DBG, "No more space for digits in rfc2833 buffer\n");
      return;
    }

    ae->DigitBuffers[mode][*pLen] = d;
    (*pLen)++;
    ae->last_digit_timestamp[mode] = ms_timestamp();
    PJW_UNLOCK();
  } else {
    make_evt_dtmf(evt, sizeof(evt), call_id, 1, &d, mode, media_id);
    dispatch_event(evt);
  }
}

static void on_registration_status(pjsip_regc_cbparam *param) {
  // addon_log(L_DBG, "on_registration_status\n");
  if (g_shutting_down)
    return;

  char evt[1024];

  long acc_id;
  if (!g_account_ids.get_id((long)param->regc, acc_id)) {
    addon_log(L_DBG, "on_registration_status: Failed to get account_id. Event "
                     "will not be notified.\n");
    return;
  }

  char reason[100];
  int len = param->reason.slen;
  strncpy(reason, param->reason.ptr, len);
  reason[len] = 0;

  make_evt_registration_status(evt, sizeof(evt), acc_id, param->code, reason,
                               param->expiration);
  dispatch_event(evt);
}

int pjw_get_codecs(char *out_codecs) {
  clear_error();

  pjmedia_codec_mgr *codec_mgr;
  pjmedia_codec_info codec_info[100];
  unsigned count = sizeof(codec_info);
  unsigned prio[100];
  pj_status_t status;
  ostringstream oss;

  PJW_LOCK();

  codec_mgr = pjmedia_endpt_get_codec_mgr(g_med_endpt);
  if (!codec_mgr) {
    set_error("pjmedia_endpt_get_codec_mgr failed");
    goto out;
  }

  status = pjmedia_codec_mgr_enum_codecs(codec_mgr, &count, codec_info, prio);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_codec_mgr_enum_codecs failed");
    goto out;
  }

  for (unsigned i = 0; i < count; ++i) {
    pjmedia_codec_info *info = &codec_info[i];
    if (i != 0)
      oss << " ";
    oss.write(info->encoding_name.ptr, info->encoding_name.slen);
    oss << "/" << info->clock_rate;
    oss << "/" << info->channel_cnt;
    oss << ":" << prio[i];
  }

out:
  PJW_UNLOCK();

  if (pjw_errorstring[0]) {
    return -1;
  }

  strcpy(out_codecs, oss.str().c_str());
  return 0;
}

int pjw_set_codecs(const char *in_codec_info) {
  clear_error();

  // char error[1000];
  pjmedia_codec_mgr *codec_mgr;
  pj_status_t status;
  char codec_info[1024];
  pj_str_t codec_id;

  char *token_comma;

  char *saveptr;

  PJW_LOCK();

  codec_mgr = pjmedia_endpt_get_codec_mgr(g_med_endpt);
  if (!codec_mgr) {
    set_error("pjmedia_endpt_get_codec_mgr failed");
    goto out;
  }

  codec_id = pj_str((char *)"");
  status = pjmedia_codec_mgr_set_codec_priority(codec_mgr, &codec_id, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_codec_mgr_set_codec_priority(zero all) failed.");
    goto out;
  }

  strcpy(codec_info, in_codec_info);

  printf("in_codec_info='%s'\n", in_codec_info);
  printf("   codec_info='%s'\n", codec_info);

  token_comma = strtok_r(codec_info, ",", &saveptr);

  while (token_comma != NULL) {
    printf("Token: '%s'\n", token_comma);

    char *colon_position = strchr(token_comma, ':');
    if (colon_position == NULL) {
      set_error("malformed argument codec_info");
      break;
    }

    *colon_position = '\0'; // Replace colon with null terminator
    char *codec_id_s = token_comma;
    char *prio = colon_position + 1;

    printf("codec_id=%s prio=%s\n", codec_id_s, prio);

    codec_id = pj_str(codec_id_s);

    status =
        pjmedia_codec_mgr_set_codec_priority(codec_mgr, &codec_id, atoi(prio));
    if (status != PJ_SUCCESS) {
      set_error("pjmedia_codec_mgr_set_codec_priority failed");
      break;
    }

    token_comma = strtok_r(NULL, ",", &saveptr);
  }

out:
  PJW_UNLOCK();

  if (pjw_errorstring[0]) {
    // Try to put default priority to all codecs
    codec_id = pj_str((char *)"");
    status = pjmedia_codec_mgr_set_codec_priority(codec_mgr, &codec_id, 128);
    return -1;
  }

  return 0;
}

int pjw_disable_telephone_event() {
	PJW_LOCK();

	int val;

	val = PJ_FALSE;
	pjmedia_endpt_set_flag(g_med_endpt,
		       PJMEDIA_ENDPT_HAS_TELEPHONE_EVENT_FLAG,
		       &val);

	PJW_UNLOCK();
	return 0;
}

int pjw_enable_telephone_event() {
	PJW_LOCK();

	int val;

	val = PJ_TRUE;
	pjmedia_endpt_set_flag(g_med_endpt,
		       PJMEDIA_ENDPT_HAS_TELEPHONE_EVENT_FLAG,
		       &val);

	PJW_UNLOCK();
	return 0;
}

int __pjw_shutdown() {
  // addon_log(L_DBG, "pjw_shutdown thread_id=%i\n", syscall(SYS_gettid));
  PJW_LOCK();

  g_shutting_down = true;

  // disable auto cleanup

  /*
      map<long, long>::iterator iter;
      iter = g_call_ids.id_map.begin();
      while(iter != g_call_ids.id_map.end()){
              Call *call = (Call*)iter->second;

              addon_log(L_DBG, "Terminating call %d\n", iter->first);

              pjsip_tx_data *tdata;
              pj_status_t status;
              status = pjsip_inv_end_session(call->inv,
                                                603,
                                                NULL,
                                                &tdata); //Copied from pjsua
              if(status != PJ_SUCCESS){
                      //ignore
                      char err[256];
                      pj_strerror(status, err, sizeof(err));
                      addon_log(L_DBG, "pjsip_inv_end_session failed statut=%i
     (%s)\n", status, err);
                      ++iter;
                      continue;
              }

              if(!tdata)
              {
                      //if tdata was not set by pjsip_inv_end_session, it means
     we didn't receive any response yet (100 Trying) and we cannot send CANCEL
     in this situation. So we just can return here without calling
     pjsip_inv_send_msg.
                      ++iter;
                      addon_log(L_DBG, "no tdata\n");
                      continue;
              }

              status = pjsip_inv_send_msg(call->inv, tdata);
              if(status != PJ_SUCCESS){
                      addon_log(L_DBG, "pjsip_inv_send_msg failed\n");
              }
              ++iter;
      }

      iter = g_account_ids.id_map.begin();
      while(iter != g_account_ids.id_map.end()){
              pjsip_regc *regc = (pjsip_regc*)iter->second;

              addon_log(L_DBG, "Unregistering account %d\n", iter->first);

              pjsip_tx_data *tdata;
              pj_status_t status;

              status = pjsip_regc_unregister(regc, &tdata);
              if(status != PJ_SUCCESS)
              {
                      addon_log(L_DBG, "pjsip_regc_unregister failed\n");
              }

              status = pjsip_regc_send(regc, tdata);
              if(status != PJ_SUCCESS)
              {
                      addon_log(L_DBG, "pjsip_regc_send failed\n");
              }
              ++iter;
      }

      Subscription *subscription;
      iter = g_subscription_ids.id_map.begin();
      while(iter != g_subscription_ids.id_map.end()){
              addon_log(L_DBG, "Unsubscribing subscription %d\n", iter->first);

              subscription = (Subscription*)iter->second;
              if(!subscription_subscribe(subscription, 0, NULL)) {
                      addon_log(L_DBG, "Unsubscription failed failed\n");
              }
              ++iter;
      }

      PJW_UNLOCK();

      //uint32_t wait = 100000 * (g_call_ids.id_map.size() +
     g_account_ids.id_map.size()));
      //wait += 1000000; //Wait one whole second to permit packet capture to get
     any final packets

      timeval tv_start;
      timeval tv_end;
      gettimeofday(&tv_start, NULL);
      gettimeofday(&tv_end, NULL);

      unsigned int start = tv_start.tv_sec * 1000 + (tv_start.tv_usec / 1000);
      unsigned int end = tv_end.tv_sec * 1000 + (tv_end.tv_usec / 1000);

      int DELAY = 2000; // 1000 ms delay
      while(end - start < DELAY) {
              pj_time_val tv = {0, 500};
              pj_status_t status;
              status = pjsip_endpt_handle_events(g_sip_endpt, &tv);

              gettimeofday(&tv_end, NULL);
              end = tv_end.tv_sec * 1000 + (tv_end.tv_usec / 1000);
              //time(&end);
      }

  */

  return 0;
}

// Copied from streamutil.c (pjsip sample)
static const char *good_number(char *buf, pj_int32_t val) {
  if (val < 1000) {
    pj_ansi_sprintf(buf, "%d", val);
  } else if (val < 1000000) {
    pj_ansi_sprintf(buf, "%d.%dK", val / 1000, (val % 1000) / 100);
  } else {
    pj_ansi_sprintf(buf, "%d.%02dM", val / 1000000, (val % 1000000) / 10000);
  }

  return buf;
}

static void build_stream_stat(ostringstream &oss, pjmedia_rtcp_stat *stat,
                              pjmedia_stream_info *stream_info) {
  char temp[200];
  char duration[80], last_update[80];

  // char bps[16];
  // char ipbps[16];
  char packets[16];
  // char bytes[16];
  // char ipbytes[16];

  pj_time_val now;

  pj_gettimeofday(&now);

  oss << "{ ";

  PJ_TIME_VAL_SUB(now, stat->start);
  sprintf(duration, "\"Duration\": \"%02ld:%02ld:%02ld.%03ld\"", now.sec / 3600,
          (now.sec % 3600) / 60, (now.sec % 60), now.msec);

  oss << duration;

  sprintf(temp, ", \"CodecInfo\": \"%.*s/%d/%d\"",
          (int)stream_info->fmt.encoding_name.slen,
          stream_info->fmt.encoding_name.ptr, stream_info->fmt.clock_rate,
          stream_info->fmt.channel_cnt);

  oss << temp << ",";

  oss << " \"RX\": { "; // Opening RX

  if (stat->rx.update_cnt == 0)
    strcpy(last_update, "\"LastUpdate\": \"\"");
  else {
    sprintf(last_update, "\"LastUpdate\": \"%ld.%ld\"", stat->rx.update.sec,
            stat->rx.update.msec);
  }

  oss << last_update;

  oss << ", \"Packets\": " << good_number(packets, stat->rx.pkt);
  oss << ", \"Loss\": " << stat->rx.loss;
  oss << ", \"Dup\": " << stat->rx.dup;
  oss << ", \"Reorder\": " << stat->rx.reorder;

  oss << ", \"LossPeriod\": {";
  oss << "\"Min\": " << stat->rx.loss_period.min / 1000.0;
  oss << ", \"Mean\": " << stat->rx.loss_period.mean / 1000.0;
  oss << ", \"Max\": " << stat->rx.loss_period.max / 1000.0;
  oss << ", \"Last\": " << stat->rx.loss_period.last / 1000.0;
  oss << ", \"StandardDeviation\": "
      << pj_math_stat_get_stddev(&stat->rx.loss_period) / 1000.0 << " }";

  oss << ", \"Jitter\": { ";
  oss << "\"Min\": " << stat->rx.jitter.min / 1000.0;
  oss << ", \"Mean\": " << stat->rx.jitter.mean / 1000.0;
  oss << ", \"Max\": " << stat->rx.jitter.max / 1000.0;
  oss << ", \"Last\": " << stat->rx.jitter.last / 1000.0;
  oss << ", \"StandardDeviation\": "
      << pj_math_stat_get_stddev(&stat->rx.jitter) / 1000.0 << " }";

  oss << " }"; // Closing RX

  oss << ", \"TX\": { "; // Opening TX

  if (stat->tx.update_cnt == 0)
    strcpy(last_update, "\"LastUpdate\": \"\"");
  else {
    sprintf(last_update, "\"LastUpdate\": \"%ld.%ld\"", stat->tx.update.sec,
            stat->tx.update.msec);
  }

  oss << last_update;

  oss << ", \"Packets\": " << good_number(packets, stat->tx.pkt);
  oss << ", \"Loss\": " << stat->tx.loss;
  oss << ", \"Dup\": " << stat->tx.dup;
  oss << ", \"Reorder\": " << stat->tx.reorder;

  oss << ", \"LossPeriod\": { ";
  oss << "\"Min\": " << stat->tx.loss_period.min / 1000.0;
  oss << ", \"Mean\": " << stat->tx.loss_period.mean / 1000.0;
  oss << ", \"Max\": " << stat->tx.loss_period.max / 1000.0;
  oss << ", \"Last\":" << stat->tx.loss_period.last / 1000.0;
  oss << ", \"StandardDeviation\": "
      << pj_math_stat_get_stddev(&stat->tx.loss_period) / 1000.0 << " }";

  oss << ", \"Jitter\": { ";
  oss << "\"Min\": " << stat->tx.jitter.min / 1000.0;
  oss << ", \"Mean\": " << stat->tx.jitter.mean / 1000.0;
  oss << ", \"Max\": " << stat->tx.jitter.max / 1000.0;
  oss << ", \"Last\": " << stat->tx.jitter.last / 1000.0;
  oss << ", \"StandardDeviation\": "
      << pj_math_stat_get_stddev(&stat->tx.jitter) / 1000.0 << " }";

  oss << " }"; // Closing TX

  oss << ", \"RTT\": { "; // Opening RTT

  oss << "\"Min\": " << stat->rtt.min / 1000.0;
  oss << ", \"Mean\": " << stat->rtt.mean / 1000.0;
  oss << ", \"Max\": " << stat->rtt.max / 1000.0;
  oss << ", \"Last\": " << stat->rtt.last / 1000.0;
  oss << ", \"StandardDeviation\": "
      << pj_math_stat_get_stddev(&stat->rtt) / 1000.0;
  oss << " }"; // Closing RTT
  oss << " }";
}

void close_media_transport(pjmedia_transport *med_transport) {
  printf("close_media_transport %p\n", (void*)med_transport);
  pjmedia_transport_info tpinfo;
  pjmedia_transport_info_init(&tpinfo);
  pj_status_t status = pjmedia_transport_get_info(med_transport, &tpinfo);
  if (status != PJ_SUCCESS)
    return;

  status = pjmedia_transport_media_stop(med_transport);
  if (status != PJ_SUCCESS) {
    addon_log(
        L_DBG,
        "Critical Error: pjmedia_transport_media_stop failed. status=%d\n",
        status);
  }

  status = pjmedia_transport_close(med_transport);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG,
              "Critical Error: pjmedia_transport_close failed. status=%d\n",
              status);
  }
}

bool has_attribute_mode(MediaEndpoint *me) {
  for (int i = 0; i < me->field_count; i++) {
    char *val = me->field[i];
    if ((strcmp(val, "a=sendrecv") == 0) || (strcmp(val, "a=sendonly") == 0) ||
        (strcmp(val, "a=recvonly") == 0) || (strcmp(val, "a=inactive") == 0)) {
      return true;
    }
  }
  return false;
}

void remove_mode_attributes(pjmedia_sdp_media *m) {
  for (unsigned i = 0; i < m->attr_count; i++) {
    pjmedia_sdp_attr *attr = m->attr[i];
    if ((pj_strcmp2(&attr->name, "sendrecv") == 0) ||
        (pj_strcmp2(&attr->name, "sendonly") == 0) ||
        (pj_strcmp2(&attr->name, "recvonly") == 0) ||
        (pj_strcmp2(&attr->name, "inactive") == 0)) {

      pj_array_erase(m->attr, sizeof(pjmedia_sdp_attr *), m->attr_count, i);
      m->attr_count--;
    }
  }
}

bool update_media_fields(MediaEndpoint *me, pj_pool_t *pool, Value &descr) {
  me->field_count = 0;

  if (descr.HasMember("fields")) {
    if (!descr["fields"].IsArray()) {
      set_error("update_media_fields failed. Media param fields must "
                "be array");
      return false;
    }
    const Value &fields = descr["fields"];
    for (rapidjson::SizeType i = 0; i < fields.Size(); i++) {
      if (!fields[i].IsString()) {
        set_error("Invalid fields item at idx=%i. It must be a string", i);
        return false;
      }
      const char *s = fields[i].GetString();

      char *val = (char *)pj_pool_alloc(pool, strlen(s) + 1);
      strcpy(val, s);

      me->field[me->field_count++] = val;
    }
  }
  return true;
}

bool create_media_endpoint(Call *call, Document &document, Value &descr,
                           pjsip_dialog *dlg, char *address,
                           MediaEndpoint **out) {
  printf("create_media_endpoint call_id=%d\n", call->id);
  MediaEndpoint *med_endpt =
      (MediaEndpoint *)pj_pool_zalloc(dlg->pool, sizeof(MediaEndpoint));
  if (!med_endpt) {
    set_error("failed to allocate med_endpt");
    return false;
  }

  const char *type = (const char *)descr["type"].GetString();
  const pj_str_t str_addr = pj_str(address);

  pj_bool_t must_not_be_used = PJ_FALSE;

  if (descr.HasMember("port")) {
    if (!descr["port"].IsInt()) {
      set_error("Parameter port must be an integer");
      return false;
    }
    int port = descr["port"].GetInt();
    if(port == 0) {
      must_not_be_used = PJ_TRUE;
    }
  }

  if (strcmp("audio", type) == 0) {
    pj_uint16_t allocated_port;
    pjmedia_transport *med_transport = NULL;
    if(must_not_be_used) {
      allocated_port = 0;
    } else {
      med_transport = create_media_transport(&str_addr, &allocated_port);
      if (!med_transport) {
        set_error("create_media_transport failed");
        return false;
      }
    }

    AudioEndpoint *audio_endpt =
        (AudioEndpoint *)pj_pool_zalloc(dlg->pool, sizeof(AudioEndpoint));
    audio_endpt->med_transport = med_transport;

    med_endpt->type = ENDPOINT_TYPE_AUDIO;
    pj_strdup2(dlg->pool, &med_endpt->media, "audio");
    pj_strdup(dlg->pool, &med_endpt->addr, &str_addr);

    if (descr.HasMember("secure")) {
      if (!descr["secure"].IsBool()) {
        set_error("Parameter secure must be a boolean");
        return false;
      }
      med_endpt->secure = descr["secure"].GetBool();
    }
    
    if(med_endpt->secure){
      pjmedia_transport *srtp;
      pj_status_t status = create_transport_srtp(audio_endpt->med_transport, &srtp);
      if(status != PJ_SUCCESS) {
        set_error("create_transport_srtp failed");
        return false;
      }
      audio_endpt->med_transport = srtp;

      status = pjmedia_transport_media_create(audio_endpt->med_transport, dlg->pool, 0, NULL, 0);
      if(status != PJ_SUCCESS) {
        set_error("pjmedia_transport_media_create failed"); 
        return false;
      }
      pj_strdup2(dlg->pool, &med_endpt->transport, "RTP/SAVP");
    } else {
      pj_strdup2(dlg->pool, &med_endpt->transport, "RTP/AVP");
    }

    med_endpt->port = allocated_port;
    printf("med_endtp->port=%i\n", med_endpt->port);
    med_endpt->endpoint.audio = audio_endpt;
  } else if (strcmp("mrcp", type) == 0) {
    MrcpEndpoint *mrcp_endpt = (MrcpEndpoint *)pj_pool_zalloc(dlg->pool, sizeof(MrcpEndpoint));
    pj_uint16_t allocated_port;
    pj_activesock_t *asock = NULL;
    if(must_not_be_used) {
      allocated_port = 0;
    } else {
      if (call->outgoing) {
        allocated_port = 9; // client must use port 9
      } else {
        pj_str_t ipaddr = pj_str(address);
        asock = create_tcp_socket(g_sip_endpt, &ipaddr, &allocated_port, med_endpt, call);
        if(!asock) {
          set_error("create_media_transport MrcpEndpoint failed");
          return false;
        }
        mrcp_endpt->asock = asock;
      }
    }

    med_endpt->type = ENDPOINT_TYPE_MRCP;
    pj_strdup2(dlg->pool, &med_endpt->media, "application");
    pj_strdup(dlg->pool, &med_endpt->addr, &str_addr);
    pj_strdup2(dlg->pool, &med_endpt->transport, "TCP/MRCPv2");
    med_endpt->port = allocated_port;
    med_endpt->endpoint.mrcp= mrcp_endpt;
  } else {
    // for all other cases, med_endpt->type will be zero, so wil not be used.
  }

  *out = med_endpt;
  printf("create_media_endpoint call_id=%d type=%d\n", call->id,
         med_endpt->type);

  if (!update_media_fields(med_endpt, dlg->pool, descr)) {
    return false;
  }

  return true;
}

MediaEndpoint *find_media_by_json_descr(Call *call, Value &descr,
                                        bool in_use_chart[], int *idx) {
  const char *type_name = (const char *)descr["type"].GetString();

  int type_id = media_type_name_to_type_id(type_name);

  for (int i = 0; i < call->media_count; i++) {
    if (in_use_chart[i])
      continue;
    MediaEndpoint *me = call->media[i];

    if (me->type == type_id) {
      in_use_chart[i] = true;
      *idx = i;
      return me;
    }
  }

  return NULL;
}

MediaEndpoint *find_media_endpt_by_json_descr(Call *call, Value &descr,
                                              bool in_use_chart[]) {
  const char *type = (const char *)descr["type"].GetString();

  for (int i = 0; i < call->media_count; i++) {
    if (in_use_chart[i])
      continue;
    MediaEndpoint *me = call->media[i];

    if (strcmp("audio", type) == 0) {
      if (ENDPOINT_TYPE_AUDIO == me->type) {
        in_use_chart[i] = true;
        return me;
      }
    } else if (strcmp("video", type) == 0) {
      if (ENDPOINT_TYPE_VIDEO == me->type) {
        in_use_chart[i] = true;
        return me;
      }
    } else if (strcmp("mrcp", type) == 0) {
      if (ENDPOINT_TYPE_MRCP == me->type) {
        in_use_chart[i] = true;
        return me;
      }
    } else {
      assert(0);
      // missing media type support implementation
    }
  }

  return NULL;
}

pjmedia_sdp_media *create_sdp_media(MediaEndpoint *me, pjsip_dialog *dlg) {
  pj_status_t status;
  pjmedia_sdp_media *media;

  if(me->port == 0) {
    // media not in use
    media = (pjmedia_sdp_media *)pj_pool_zalloc(dlg->pool,
                                                sizeof(pjmedia_sdp_media));
    if (!media) {
      set_error("create pjmedia_sdp_media for mrcp endpoint failed");
      return NULL;
    }

    pj_strdup(dlg->pool, &media->desc.media, &me->media);

    pj_strdup(dlg->pool, &media->desc.transport, &me->transport);

    media->desc.port = me->port;
    pj_strdup2(
        dlg->pool, &media->desc.fmt[media->desc.fmt_count++],
        "0");

    media->conn =
        (pjmedia_sdp_conn *)pj_pool_zalloc(dlg->pool, sizeof(pjmedia_sdp_conn));
    pj_strdup2(dlg->pool, &media->conn->net_type, "IN");
    pj_strdup2(dlg->pool, &media->conn->addr_type, "IP4");
    pj_strdup(dlg->pool, &media->conn->addr, &me->addr);

    return media;
  }

  if (ENDPOINT_TYPE_AUDIO == me->type) {
    AudioEndpoint *audio = (AudioEndpoint *)me->endpoint.audio;
    pjmedia_transport_info med_tpinfo;
    pjmedia_transport_info_init(&med_tpinfo);
    pjmedia_transport_get_info(audio->med_transport, &med_tpinfo);

    status = pjmedia_endpt_create_audio_sdp(g_med_endpt, dlg->pool,
                                            &med_tpinfo.sock_info, 0, &media);
    if (status != PJ_SUCCESS) {
      set_error("pjmedia_endpt_create_audio_sdp failed");
      return NULL;
    }

    if (has_attribute_mode(me)) {
      remove_mode_attributes(media);
    }

  } else if (ENDPOINT_TYPE_MRCP == me->type) {
    media = (pjmedia_sdp_media *)pj_pool_zalloc(dlg->pool,
                                                sizeof(pjmedia_sdp_media));
    if (!media) {
      set_error("create pjmedia_sdp_media for mrcp endpoint failed");
      return NULL;
    }

    pj_strdup(dlg->pool, &media->desc.media, &me->media);

    pj_strdup(dlg->pool, &media->desc.transport, &me->transport);

    media->desc.port = me->port;
    pj_strdup2(
        dlg->pool, &media->desc.fmt[media->desc.fmt_count++],
        "1"); // must be "1" (https://www.rfc-editor.org/rfc/rfc6787.html)

    media->conn =
        (pjmedia_sdp_conn *)pj_pool_zalloc(dlg->pool, sizeof(pjmedia_sdp_conn));
    pj_strdup2(dlg->pool, &media->conn->net_type, "IN");
    pj_strdup2(dlg->pool, &media->conn->addr_type, "IP4");
    pj_strdup(dlg->pool, &media->conn->addr, &me->addr);
  } else {
    printf("unsupported me->type %d\n", me->type);
    assert(0);
  }

  for (int i = 0; i < me->field_count; i++) {
    char *val = me->field[i];
    if(val[1] != '=') {
      set_error("Invalid media field specification");
      return NULL; 
    }

    if(val[0] == 'a') {
      pjmedia_sdp_attr *attr = pjmedia_sdp_attr_create(dlg->pool, &val[2], NULL);
      pjmedia_sdp_media_add_attr(media, attr);
    } else {
      set_error("unsupported media field");
      return NULL;
    }
  }

  return media;
}

bool process_media(Call *call, pjsip_dialog *dlg, Document &document, bool answer) {
  printf("process_media call_id=%d\n", call->id);

  bool in_use_chart[PJMEDIA_MAX_SDP_MEDIA] = {false};

  pjmedia_sdp_session *sdp;

  pj_sockaddr origin;
  pj_sockaddr_init(pj_AF_INET(), &origin, NULL, 0);

  pj_status_t status = pjmedia_endpt_create_base_sdp(g_med_endpt, dlg->pool,
                                                     NULL, &origin, &sdp);

  if (status != PJ_SUCCESS) {
    set_error("pjmedia_endpt_create_base_sdp failed");
    return false;
  }

  Transport *t = call->transport;

  Document::AllocatorType &allocator = document.GetAllocator();

  if (!document.HasMember("media")) {
    // no media. So create a default [{"type": "audio"}]
    Value audio(kObjectType);
    audio.AddMember("type", Value().SetString("audio"), allocator);

    Value media(kArrayType);
    media.PushBack(audio, allocator);
    document.AddMember("media", media, allocator);
  } else {
    if (document["media"].IsString()) {
      Value media(kArrayType);

      std::string str = document["media"].GetString();

      std::stringstream ss(str);
      std::string item;
      while (std::getline(ss, item, ',')) {
        Value mediaItem(kObjectType);
        mediaItem.AddMember(
            "type", Value().SetString(item.c_str(), item.length(), allocator),
            allocator);

        media.PushBack(mediaItem, allocator);
      }

      document["media"] = media;
    } else if (document["media"].IsArray()) {
      Value media = document["media"].GetArray();
      assert(media.Size() <= PJMEDIA_MAX_SDP_MEDIA);

      for (Value::ValueIterator itr = media.Begin(); itr != media.End();
           ++itr) {
        if (itr->IsString()) {
          const char *type = itr->GetString();
          int len = itr->GetStringLength();
          Value mediaItem(kObjectType);
          mediaItem.AddMember("type", Value().SetString(type, len, allocator),
                              allocator);

          *itr = mediaItem;
        } else if (itr->IsObject()) {
          printf("itr is object\n");
          // do nothing
        } else {
          set_error("Param media item must be either object or string");
          return false;
        }
      }
      document["media"] = media;
    } else {
      set_error("Param media must be either array or string");
      return false;
    }
  }

  Value media = document["media"].GetArray();
  assert(media.Size() <= PJMEDIA_MAX_SDP_MEDIA);

  const pjmedia_sdp_session *rem_sdp = NULL;
  if(answer) {
    if(call->inv && call->inv->neg) {
      status = pjmedia_sdp_neg_get_neg_remote(call->inv->neg, &rem_sdp);
      if(status != PJ_SUCCESS) {
        addon_log(L_DBG, "Internal Server Error (pjmedia_sdp_neg_get_neg_remote failed)");
        return false;
      }
    }
  }

  for (SizeType i = 0; i < media.Size(); i++) {
    Value descr = media[i].GetObject();

    int idx;
    MediaEndpoint *me = find_media_by_json_descr(call, descr, in_use_chart, &idx);

    if (me) {
      addon_log(L_DBG, "i=%d media found\n", i);
      if (me->port && descr.HasMember("port")) {
        // me was active but it must be deactivated
        MediaEndpoint *new_me;

        if(!create_media_endpoint(call, document, descr, dlg, (char*)"0.0.0.0", &new_me))
          return false;
        addon_log(L_DBG, "i=%d media port=0 created %p\n", i, (void*)me);

        pjmedia_sdp_media *media = create_sdp_media(new_me, dlg);
        if (!media)
          return false;

        sdp->media[sdp->media_count++] = media;
      } else if(!me->port && !descr.HasMember("port")) {
        // me was not active but it is activated now
        if (!create_media_endpoint(call, document, descr, dlg, t->address, &me))
          return false;
        addon_log(L_DBG, "i=%d media created %p\n", i, (void*)me);
        call->media[idx] = me;

        if (!update_media_fields(me, dlg->pool, descr)) {
          return false;
        }

        pjmedia_sdp_media *media = create_sdp_media(me, dlg);
        if (!media)
          return false;

        sdp->media[sdp->media_count++] = media;
      } else {
        if (!update_media_fields(me, dlg->pool, descr)) {
          return false;
        }

        pjmedia_sdp_media *media = create_sdp_media(me, dlg);
        if (!media)
          return false;

        sdp->media[sdp->media_count++] = media;
      }
    } else {
      addon_log(L_DBG, "i=%d media not found\n", i);
      if (!create_media_endpoint(call, document, descr, dlg, t->address, &me))
        return false;
      addon_log(L_DBG, "i=%d media created %p\n", i, (void*)me);
      call->media[call->media_count++] = me;
      in_use_chart[call->media_count - 1] =
          true; // added elements must be set as in use

      if (!update_media_fields(me, dlg->pool, descr)) {
        return false;
      }

      pjmedia_sdp_media *media = create_sdp_media(me, dlg);
      if (!media)
        return false;

      sdp->media[sdp->media_count++] = media;
    }

    if(me->secure && me->endpoint.audio) {
      pj_status_t status = pjmedia_transport_encode_sdp(me->endpoint.audio->med_transport, dlg->pool, sdp, rem_sdp, i);
      if(status != PJ_SUCCESS) {
        addon_log(L_DBG, "pjmedia_transport_encode_sdp failed");
        return false;
      }

      // The below must be done after pjmedia_transport_encode_sdp() because although at this point med_transport is a transport_srtp, it calls the transport_encode_sdp of the underlying transpor_udp and it will fail when it sees "RTP/SAVP" instead of "RTP/AVP"
      // So we change from RTP/AVP to RTP/SAVP after we add the crypto lines.
      pj_strdup2(dlg->pool, &sdp->media[i]->desc.transport, "RTP/SAVP");
    }
  }

  call->local_sdp = sdp;

  char b[2048];
  pjmedia_sdp_print(call->local_sdp, b, sizeof(b));
  addon_log(L_DBG, "process_media call_id=%d call->local_sdp: %s\n", call->id,
            b);

  return true;
}

bool is_media_active(Call *c, MediaEndpoint *me) {
  // check if media from call->media_neg is on call->media
  for (int i = 0; i < c->media_count; ++i) {
    if (me == c->media[i])
      return true;
  }
  return false;
}

void close_media_endpoint(Call *call, MediaEndpoint *me) {
  printf("close_media_endpoint %p\n", (void*)me);
  if(!me) return;

  if (ENDPOINT_TYPE_AUDIO == me->type) {
    AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

    audio_endpoint_remove_port(call, &ae->stream_cbp);
    audio_endpoint_remove_port(call, &ae->wav_player_cbp);
    audio_endpoint_remove_port(call, &ae->wav_writer_cbp);
    audio_endpoint_remove_port(call, &ae->tonegen_cbp);
    audio_endpoint_remove_port(call, &ae->dtmfdet_cbp);
    audio_endpoint_remove_port(call, &ae->fax_cbp);

    close_media_transport(ae->med_transport);
    ae->med_transport = NULL;
  } else if (ENDPOINT_TYPE_MRCP == me->type) {
    if(me->endpoint.mrcp->asock) {
      pj_activesock_t *asock = me->endpoint.mrcp->asock;
      pj_status_t status;

      status = pj_activesock_set_user_data(asock, NULL);

      // This is critical so we will force a crash otherwise an activesock callback might access an invalid pointer
      assert(status == PJ_SUCCESS);

      status = pj_activesock_close(me->endpoint.mrcp->asock);
      if(status != PJ_SUCCESS) {
        //Failed but there is nothing to do (but no harm)
        printf("pj_activesock_close failed\n");
      }
    }
    me->endpoint.mrcp->asock = NULL;
  }
  me->port = 0;
}

void close_media(Call *c) {
  printf("close_media call_id=%li\n", c->id);
  for (int i = 0; i < c->media_count; ++i) {
    MediaEndpoint *me = c->media[i];
    close_media_endpoint(c, me);
  }
  c->media_count = 0;
}

bool prepare_tonegen(Call *c, AudioEndpoint *ae) {
  printf("prepare_tone_gen call.id=%i\n", c->id);
  pj_status_t status;

  if(ae->tonegen_cbp.port) {
    printf("already prepared\n");
    return true;
  }

  status = pjmedia_tonegen_create(
        c->inv->pool, PJMEDIA_PIA_SRATE(&ae->stream_cbp.port->info),
        PJMEDIA_PIA_CCNT(&ae->stream_cbp.port->info),
        PJMEDIA_PIA_SPF(&ae->stream_cbp.port->info),
        PJMEDIA_PIA_BITS(&ae->stream_cbp.port->info), 0, &ae->tonegen_cbp.port);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_tonegen_create failed");
    return false;
  }

  status = pjmedia_conf_add_port(c->conf, c->inv->pool, ae->tonegen_cbp.port, NULL, &ae->tonegen_cbp.slot);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_add_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->tonegen_cbp.slot, ae->stream_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  return true;
}

bool prepare_wav_player(Call *c, AudioEndpoint *ae, const char *file, unsigned flags, bool end_of_file_event) {
  pj_status_t status;

  unsigned wav_ptime;
  wav_ptime = PJMEDIA_PIA_SPF(&ae->stream_cbp.port->info) * 1000 /
              PJMEDIA_PIA_SRATE(&ae->stream_cbp.port->info);

  status = pjmedia_wav_player_port_create(
                  c->inv->pool, 
                  file,
                  wav_ptime,
                  0,                  /* flags        */
                  -1,                  /* buf size     */
                  &ae->wav_player_cbp.port
                  );

  if (status != PJ_SUCCESS) {
    set_error("pjmedia_wav_player_port_create failed");
    return false;
  }

  if (end_of_file_event) {
    status = pjmedia_wav_player_set_eof_cb2(ae->wav_player_cbp.port, (void*)c, on_end_of_file);
    if (status != PJ_SUCCESS) {
      set_error("pjmedia_wav_player_set_eof_cb2 failed");
      return false;
    }
  }

  status = pjmedia_conf_add_port(c->conf, c->inv->pool, ae->wav_player_cbp.port, NULL, &ae->wav_player_cbp.slot);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_add_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->wav_player_cbp.slot, ae->stream_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  return true;
}

bool prepare_wav_writer(Call *c, AudioEndpoint *ae, const char *file) {
  pj_status_t status;

  status = pjmedia_wav_writer_port_create(
      c->inv->pool, file, PJMEDIA_PIA_SRATE(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_CCNT(&ae->stream_cbp.port->info), PJMEDIA_PIA_SPF(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_BITS(&ae->stream_cbp.port->info), PJMEDIA_FILE_WRITE_PCM, 0,
      (pjmedia_port **)&ae->wav_writer_cbp.port);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_wav_write_port_create failed");
    return false;
  }
  
  status = pjmedia_conf_add_port(c->conf, c->inv->pool, ae->wav_writer_cbp.port, NULL, &ae->wav_writer_cbp.slot);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_add_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->stream_cbp.slot, ae->wav_writer_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  return true;
}

bool prepare_dtmfdet(Call *c, AudioEndpoint *ae) {
  pj_status_t status;
  status = pjmedia_dtmfdet_create(
      c->inv->pool, 
      PJMEDIA_PIA_SRATE(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_CCNT(&ae->stream_cbp.port->info), PJMEDIA_PIA_SPF(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_BITS(&ae->stream_cbp.port->info), 
      on_inband_dtmf, c, &ae->dtmfdet_cbp.port);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_dtmfdet_create failed");
      return false;
  }
  
  status = pjmedia_conf_add_port(c->conf, c->inv->pool, ae->dtmfdet_cbp.port, NULL, &ae->dtmfdet_cbp.slot);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_add_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->stream_cbp.slot, ae->dtmfdet_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  return true;
}

bool prepare_fax(Call *c, AudioEndpoint *ae, bool is_sender, const char *file,
                 unsigned flags) {
  pj_status_t status;

  status = pjmedia_fax_port_create(
      c->inv->pool,
      PJMEDIA_PIA_SRATE(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_CCNT(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_SPF(&ae->stream_cbp.port->info),
      PJMEDIA_PIA_BITS(&ae->stream_cbp.port->info),
      on_fax_result, c, is_sender, file,
      flags, &ae->fax_cbp.port);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_fax_port_create failed");
    return false;
  }

  status = pjmedia_conf_add_port(c->conf, c->inv->pool, ae->fax_cbp.port, NULL, &ae->fax_cbp.slot);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_add_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->fax_cbp.slot, ae->stream_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->stream_cbp.slot, ae->fax_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  return true;
}

bool prepare_flite(Call *c, AudioEndpoint *ae, const char *voice) {
  printf("prepare_flite call.id=%i\n", c->id);
  pj_status_t status;

  if(ae->flite_cbp.port) {
    printf("already prepared\n");
    return true;
  }

  status = pjmedia_flite_port_create(
        c->inv->pool, PJMEDIA_PIA_SRATE(&ae->stream_cbp.port->info),
        PJMEDIA_PIA_CCNT(&ae->stream_cbp.port->info),
        PJMEDIA_PIA_SPF(&ae->stream_cbp.port->info),
        PJMEDIA_PIA_BITS(&ae->stream_cbp.port->info), NULL, 0, voice, &ae->flite_cbp.port);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_flite_port_create failed");
    return false;
  }

  status = pjmedia_conf_add_port(c->conf, c->inv->pool, ae->flite_cbp.port, NULL, &ae->flite_cbp.slot);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_add_port failed");
    return false;
  }

  status = pjmedia_conf_connect_port(c->conf, ae->flite_cbp.slot, ae->stream_cbp.slot, 0);
  if (status != PJ_SUCCESS) {
    set_error("pjmedia_conf_connect_port failed");
    return false;
  }

  return true;
}


void on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code,
                  pj_str_t **p_st_text, pjsip_hdr *res_hdr,
                  pjsip_msg_body **p_body) {
  // addon_log(L_DBG, "on_rx_notify\n");

  char evt[2048];

  if (g_shutting_down)
    return;

  ostringstream oss;
  Subscription *s;

  s = (Subscription *)pjsip_evsub_get_mod_data(sub, mod_tester.id);
  if (!s) {
    addon_log(L_DBG, "Subscription not set at mod_data. Ignoring\n");
    return;
  }

  make_evt_request(evt, sizeof(evt), "subscription", s->id, rdata->msg_info.len,
                   rdata->msg_info.msg_buf);
  dispatch_event(evt);
}

static void on_client_refresh(pjsip_evsub *sub) {
  Subscription *subscription;
  // pj_status_t status;

  subscription = (Subscription *)pjsip_evsub_get_mod_data(sub, mod_tester.id);

  if (!subscription) {
    set_error("on_client_refresh: pjsip_evsub_get_mod_data returned 0");
    goto out;
  }

  if (!subscription_subscribe_no_headers(subscription, -1)) {
    goto out;
  }

  // addon_log(L_DBG, "on_client_refresh: SUBSCRIBE dispatched\n");

out:
  if (pjw_errorstring[0]) {
    dispatch_event(pjw_errorstring);
  }
}

static void client_on_evsub_state(pjsip_evsub *sub, pjsip_event *event) {
  printf("client_on_ev_sub_state\n");
  if (g_shutting_down)
    return;

  char evt[2048];
  pj_str_t mname;

  // addon_log(L_DBG, "client_on_evsub_state: %s\n",
  // pjsip_evsub_get_state_name(sub));

  PJ_UNUSED_ARG(event);

  pjsip_rx_data *rdata;
  Subscription *subscription =
      (Subscription *)pjsip_evsub_get_mod_data(sub, mod_tester.id);
  if (!subscription) {
    // addon_log(L_DBG, "mod_data set to NULL (it means subscription doesn't
    // exist anymore). Ignoring\n");
    addon_log(
        L_DBG,
        "mod_data set to NULL (we don't know what this means yet. Ignoring\n");
    return;
  }

  pjsip_generic_string_hdr *refer_sub;
  const pj_str_t REFER_SUB = {(char *)"Refer-Sub", 9};
  ostringstream oss;

  // When subscription is accepted (got 200/OK)
  int state = pjsip_evsub_get_state(sub);
  if (state == PJSIP_EVSUB_STATE_ACCEPTED) {

    pj_assert(event->type == PJSIP_EVENT_TSX_STATE &&
              event->body.tsx_state.type == PJSIP_EVENT_RX_MSG);

    rdata = event->body.tsx_state.src.rdata;

    mname = rdata->msg_info.cseq->method.name;
    make_evt_response(evt, sizeof(evt), "subscription", subscription->id,
                      mname.slen, mname.ptr, rdata->msg_info.len,
                      rdata->msg_info.msg_buf);
    dispatch_event(evt);

    // Find Refer-Sub header
    refer_sub = (pjsip_generic_string_hdr *)pjsip_msg_find_hdr_by_name(
        rdata->msg_info.msg, &REFER_SUB, NULL);

    if (refer_sub && pj_strcmp2(&refer_sub->hvalue, "false") == 0) {
      pjsip_evsub_terminate(sub, PJ_TRUE);
    }

  } else if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACTIVE ||
             pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
    // Here we catch incoming NOTIFY

    // addon_log(L_DBG, "NOTIFY\n");

    // When subscription is terminated
    if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
      printf("PJSIP_EVSUB_STATE_TERMINATED\n");
      pjsip_evsub_set_mod_data(sub, mod_tester.id, NULL);
    }

    return;

    rdata = event->body.tsx_state.src.rdata;

    // Transport *t;
    // build_basic_request_info(&oss, rdata, &t);

    long subscription_id;
    if (!g_subscription_ids.get_id((long)subscription, subscription_id)) {
      char error_msg[] = "failed to get subscription_id";
      make_evt_internal_error(evt, sizeof(evt), error_msg);
      dispatch_event(evt);
      return;
    }

    make_evt_request(evt, sizeof(evt), "subscription", subscription_id,
                     rdata->msg_info.len, rdata->msg_info.msg_buf);
    dispatch_event(evt);

    if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
      printf("PJSIP_EVSUB_STATE_TERMINATED\n");
      pjsip_evsub_set_mod_data(sub, mod_tester.id, NULL);
    }

  } else {
    // It is not message. Just ignore.
    return;
  }
}

static void server_on_evsub_state(pjsip_evsub *sub, pjsip_event *event) {
  printf("server_on_evsub_state\n");
  Subscriber *s;
  // pj_status_t status;
  // pjsip_tx_data *tdata;

  // addon_log(L_DBG, "server_on_evsub_state\n");
  if (!sub) {
    addon_log(L_DBG, "server_on_evesub_state: sub not set. Ignoring\n");
    return;
  }
  // addon_log(L_DBG, "state= %d\n", pjsip_evsub_get_state(sub));
  // addon_log(L_DBG, "server_on_evsub_state %s\n",
  // pjsip_evsub_get_state_name(sub));

  if (g_shutting_down)
    return;

  PJ_UNUSED_ARG(event);

  s = (Subscriber *)pjsip_evsub_get_mod_data(sub, mod_tester.id);
  if (!s) {
    addon_log(
        L_DBG,
        "server_on_evsub_state: Subscriber not set as mod_data. Ignoring\n");
    return;
  }

  /*
   * When subscription is terminated, clear the xfer_sub member of
   * the inv_data.
   */

  if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
    printf("PJSIP_EVSUB_STATE_TERMINATED\n");
    pjsip_evsub_set_mod_data(sub, mod_tester.id, NULL);
  }
}

// Called when incoming SUBSCRIBE (or any method that establishes a subscription
// like REFER) is received
static void server_on_evsub_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata,
                                       int *p_st_code, pj_str_t **p_st_text,
                                       pjsip_hdr *res_hdr,
                                       pjsip_msg_body **p_body) {
  addon_log(L_DBG, "server_on_evsub_rx_refresh\n");
  char evt[2048];

  pjw_errorstring[0] = 0;

  pj_status_t status;
  pjsip_tx_data *tdata;

  ostringstream oss;
  Subscriber *s;
  // Transport *t;

  if (g_shutting_down)
    return;

  s = (Subscriber *)pjsip_evsub_get_mod_data(sub, mod_tester.id);
  assert(s);

  make_evt_request(evt, sizeof(evt), "subscriber", s->id, rdata->msg_info.len,
                   rdata->msg_info.msg_buf);
  dispatch_event(evt);

  if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
    pj_str_t reason = {(char *)"noresource", 10};
    status = pjsip_evsub_notify(sub, PJSIP_EVSUB_STATE_TERMINATED, NULL,
                                &reason, &tdata);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_evsub_notify failed");
      goto out;
    }
  } else {
    status = pjsip_evsub_current_notify(sub, &tdata);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_evsub_current_notify failed");
      goto out;
    }
  }

  status = pjsip_evsub_send_request(sub, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_send_request failed");
    goto out;
  }
  addon_log(L_DBG, "server_on_evsub_rx_refresh: NOTIFY containing subscription "
                   "state should have been sent\n");

out:
  if (pjw_errorstring[0]) {
    make_evt_internal_error(evt, sizeof(evt), pjw_errorstring);
    dispatch_event(evt);
  }
}

// Adapted (mostly copied) from pjsua function on_call_transfered
void process_in_dialog_refer(pjsip_dialog *dlg, pjsip_rx_data *rdata) {
  addon_log(L_DBG, "process_in_dialog_refer\n");

  char evt[2048];

  pj_status_t status;
  // pjsip_tx_data *tdata;
  // int new_call;
  const pj_str_t str_refer_to = {(char *)"Refer-To", 8};
  const pj_str_t str_refer_sub = {(char *)"Refer-Sub", 9};
  // const pj_str_t str_ref_by = { (char*)"Referred-By", 11 };
  pjsip_generic_string_hdr *refer_to;
  pjsip_generic_string_hdr *refer_sub;
  // pjsip_hdr *ref_by_hdr;
  pj_bool_t no_refer_sub = PJ_FALSE;
  // char *uri;
  // pj_str_t tmp;
  pjsip_status_code code;
  pjsip_evsub *sub;

  Call *call = (Call *)dlg->mod_data[mod_tester.id];

  long call_id;
  if (!g_call_ids.get_id((long)call, call_id)) {
    addon_log(L_DBG, "process_in_dialog_refer: Failed to get call_id. Event "
                     "will not be notified.\n");
    return;
  }

  /* Find the Refer-To header */
  refer_to = (pjsip_generic_string_hdr *)pjsip_msg_find_hdr_by_name(
      rdata->msg_info.msg, &str_refer_to, NULL);

  if (refer_to == NULL) {
    /* Invalid Request.
     * No Refer-To header!
     */
    pjsip_dlg_respond(dlg, rdata, 400, NULL, NULL, NULL);
    // dispatch_event("event=broken_refer");
    return;
  }

  /* Find optional Refer-Sub header */
  refer_sub = (pjsip_generic_string_hdr *)pjsip_msg_find_hdr_by_name(
      rdata->msg_info.msg, &str_refer_sub, NULL);

  if (refer_sub) {
    if (!pj_strnicmp2(&refer_sub->hvalue, "true", 4) == 0) {
      // Header Refer-sub (Refer Subscription) set to something other than true
      // (probably to 'false'). This means the requester doesn't want to be
      // subscribed to refer events
      //  For details look for ietf draft : Suppression of Session Initiation
      //  Protocol REFER Method Implicit Subscription
      //  draft-ietf-sip-refer-with-norefersub-04
      no_refer_sub = PJ_TRUE;
    }
  }

  code = PJSIP_SC_ACCEPTED;

  if (no_refer_sub) {
    /*
     * Always answer with 2xx.
     */
    pjsip_tx_data *tdata;
    const pj_str_t str_false = {(char *)"false", 5};
    pjsip_hdr *hdr;

    status = pjsip_dlg_create_response(dlg, rdata, code, NULL, &tdata);
    if (status != PJ_SUCCESS) {
      make_evt_internal_error(evt, sizeof(evt),
                              "REFER response creation failure");
      dispatch_event(evt);
      return;
    }

    /* Add Refer-Sub header */
    hdr = (pjsip_hdr *)pjsip_generic_string_hdr_create(
        tdata->pool, &str_refer_sub, &str_false);
    pjsip_msg_add_hdr(tdata->msg, hdr);

    /* Send answer */
    status = pjsip_dlg_send_response(dlg, pjsip_rdata_get_tsx(rdata), tdata);
    if (status != PJ_SUCCESS) {
      make_evt_internal_error(evt, sizeof(evt),
                              "REFER response transmission failure");
      dispatch_event(evt);
      return;
    }

    /* Don't have subscription */
    sub = NULL;

  } else {
    struct pjsip_evsub_user xfer_cb;
    pjsip_hdr hdr_list;

    /* Init callback */
    pj_bzero(&xfer_cb, sizeof(xfer_cb));
    xfer_cb.on_evsub_state = &server_on_evsub_state;

    /* Init additional header list to be sent with REFER response */
    pj_list_init(&hdr_list);

    /* Create transferee event subscription */
    status = pjsip_xfer_create_uas(call->inv->dlg, &xfer_cb, rdata, &sub);
    if (status != PJ_SUCCESS) {
      make_evt_internal_error(evt, sizeof(evt), "xfer_uas_creation failure");
      dispatch_event(evt);
      pjsip_dlg_respond(call->inv->dlg, rdata, 500, NULL, NULL, NULL);
      return;
    }

    /* If there's Refer-Sub header and the value is "true", send back
     * Refer-Sub in the response with value "true" too.
     */
    if (refer_sub) {
      const pj_str_t str_true = {(char *)"true", 4};
      pjsip_hdr *hdr;

      hdr = (pjsip_hdr *)pjsip_generic_string_hdr_create(
          call->inv->dlg->pool, &str_refer_sub, &str_true);
      pj_list_push_back(&hdr_list, hdr);
    }

    /* Accept the REFER request, send 2xx. */
    pjsip_xfer_accept(sub, rdata, code, &hdr_list);
  }

  if (sub) {
    // If the REFER caused subscription of the referer...
    Subscriber *subscriber =
        (Subscriber *)pj_pool_zalloc(dlg->pool, sizeof(Subscriber));
    subscriber->evsub = sub;
    subscriber->created_by_refer = true;

    long subscriber_id;
    if (!g_subscriber_ids.add((long)subscriber, subscriber_id)) {
      make_evt_internal_error(evt, sizeof(evt),
                              "Failed to allocate id for subscriber");
      dispatch_event(evt);
      return;
    }
    subscriber->id = subscriber_id;
    pjsip_evsub_set_mod_data(sub, mod_tester.id, subscriber);

    make_evt_request(evt, sizeof(evt), "subscriber", subscriber_id,
                     rdata->msg_info.len, rdata->msg_info.msg_buf);
    dispatch_event(evt);
  }
}

/* static void on_tsx_state_changed(pjsip_inv_session *inv, pjsip_transaction *tsx,
                                 pjsip_event *e) {
  addon_log(L_DBG, "on_tsx_state change method=%.*s.\n", tsx->method.name.slen,
            tsx->method.name.ptr);
  if (g_shutting_down)
    return;

  char evt[2048];

  if (!inv->dlg)
    return;

  Call *call = (Call *)inv->dlg->mod_data[mod_tester.id];

  if (call == NULL) {
    return;
  }

  printf("call_id=%d\n", call->id);

  if (call->inv == NULL) {
    // Shouldn't happen. It happens only when we don't terminate the
    // server subscription caused by REFER after the call has been
    // transfered (and this call has been disconnected), and we
    // receive another REFER for this call.
    //
    return;
  }

  // ostringstream oss;
  // Transport *t;
  if (tsx->role == PJSIP_ROLE_UAS && tsx->state == PJSIP_TSX_STATE_TRYING) {
    if (pjsip_method_cmp(&tsx->method, pjsip_get_refer_method()) == 0) {
      // 
      // Incoming REFER request.
      //

      process_in_dialog_refer(call->inv->dlg, e->body.tsx_state.src.rdata);
    } else {
      assert(call->inv == inv);
      addon_log(L_DBG, "call->inv->dlg=%d\n", call->inv->dlg);
      addon_log(L_DBG, "call->inv->dlg->ua-id=%d\n",
                pjsip_rdata_get_tsx(e->body.tsx_state.src.rdata)
                    ->mod_data[call->inv->dlg->ua->id]);

      pjsip_rx_data *cloned_rdata = 0;
      if (pjsip_rx_data_clone(e->body.tsx_state.src.rdata, 0, &cloned_rdata) !=
          PJ_SUCCESS) {
        const pj_str_t reason = pj_str(
            (char *)"Internal Server Error (pjsip_rx_data_clone failed)");
        pjsip_endpt_respond_stateless(g_sip_endpt, e->body.tsx_state.src.rdata,
                                      500, &reason, NULL, NULL);
        return;
      }
      call->pending_rdata = cloned_rdata;

      make_evt_request(evt, sizeof(evt), "call", call->id,
                       e->body.tsx_state.src.rdata->msg_info.len,
                       e->body.tsx_state.src.rdata->msg_info.msg_buf);
      dispatch_event(evt);
    }
  } else {
    addon_log(L_DBG, "doing nothiing");
  }
} */

int pjw_call_get_info(long call_id, const char *required_info, char *out_info) {
  PJW_LOCK();

  long val;

  char info[1000];

  if (!g_call_ids.get(call_id, val)) {
    PJW_UNLOCK();
    set_error("Invalid call_id");
    return -1;
  }

  Call *call = (Call *)val;

  if (strcmp(required_info, "Call-ID") == 0) {
    strncpy(info, call->inv->dlg->call_id->id.ptr,
            call->inv->dlg->call_id->id.slen);
    info[call->inv->dlg->call_id->id.slen] = 0;
    // TODO: update to use multiple media
    /*
        } else if(strcmp(required_info, "RemoteMediaEndPoint") == 0) {
                if(!call->med_stream) {
                        PJW_UNLOCK();
                        set_error("Unable to get RemoteMediaEndPoint: Media not
       ready"); return -1;
                }
                pjmedia_stream_info session_info;
                if(pjmedia_stream_get_info(call->med_stream, &session_info) !=
       PJ_SUCCESS) { PJW_UNLOCK(); set_error("Unable to get RemoteMediaEndPoint:
       call to pjmedia_stream_info failed"); return -1;
                }
                pj_str_t str_addr = pj_str( inet_ntoa(
       (in_addr&)session_info.rem_addr.ipv4.sin_addr.s_addr ) ); pj_uint16_t
       port = session_info.rem_addr.ipv4.sin_port; sprintf(info, "%.*s:%u",
       (int)str_addr.slen, str_addr.ptr, ntohs(port));
    */
  } else {
    PJW_UNLOCK();
    set_error("Unsupported info");
    return -1;
  }

  PJW_UNLOCK();
  strcpy(out_info, info);
  return 0;
}

bool notify(pjsip_evsub *evsub, const char *content_type, const char *body,
            int subscription_state, const char *reason, Document &document) {
  // pj_str_t s_content_type;
  // pj_str_t s_body;
  pj_str_t s_reason;

  char *temp;
  int bodylen;

  char *tok;

  pjsip_msg_body *msg_body;

  char *content_type_buffer;
  pj_str_t s_content_type_type;
  pj_str_t s_content_type_subtype;
  // pj_str_t s_content_type_param;

  // char *blank_string;

  pjsip_tx_data *tdata;
  pj_status_t status;

  s_reason = pj_str((char *)reason);

  status = pjsip_evsub_notify(evsub, (pjsip_evsub_state)subscription_state,
                              NULL, &s_reason, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_evsub_notify failed");
    return false;
  }

  bodylen = strlen(body);
  temp = (char *)pj_pool_alloc(tdata->pool, bodylen + 1);
  strcpy(temp, body);

  msg_body = PJ_POOL_ZALLOC_T(tdata->pool, pjsip_msg_body);

  content_type_buffer =
      (char *)pj_pool_alloc(tdata->pool, strlen(content_type) + 1);
  strcpy(content_type_buffer, content_type);

  tok = strtok(content_type_buffer, "/");
  if (!tok) {
    set_error("Invalid Content-Type header (while checking type)");
    return false;
  }
  s_content_type_type = pj_str(tok);

  // tok = strtok(NULL, ";");
  tok = strtok(NULL, "");
  if (!tok) {
    set_error("Invalid Content-Type header (while checking subtype)");
    return false;
  }
  s_content_type_subtype = pj_str(tok);

  if (!add_headers(tdata->pool, tdata, document)) {
    return false;
  }

  msg_body->content_type.type = s_content_type_type;
  msg_body->content_type.subtype = s_content_type_subtype;
  // msg_body->content_type.param = s_content_type_param;
  msg_body->data = temp;
  msg_body->len = bodylen;
  msg_body->print_body = &pjsip_print_text_body;
  msg_body->clone_data = &pjsip_clone_text_data;

  tdata->msg->body = msg_body;

  status = pjsip_evsub_send_request(evsub, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_evsub_send_request failed");
    return false;
  }

  return true;
}

int pjw_notify(long subscriber_id, const char *json) {
  PJW_LOCK();
  clear_error();

  char *content_type = NULL;
  char *body = NULL;
  int subscription_state;
  char *reason = NULL;

  long val;

  Subscriber *subscriber;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"content_type", "body", "subscription_state",
                                "reason", ""};

  if (!g_subscriber_ids.get(subscriber_id, val)) {
    set_error("Invalid subscriber_id");
    goto out;
  }
  subscriber = (Subscriber *)val;

  if (subscriber->created_by_refer) {
    set_error("Invalid subscriber_id: subscription was generated by REFER (use "
              "notify_xfer instead)");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "content_type", false, &content_type) <=
      0) {
    goto out;
  }

  if (json_get_string_param(document, "body", false, &body) <= 0) {
    goto out;
  }

  if (json_get_int_param(document, "subscription_state", false,
                         &subscription_state) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "reason", true, &reason) <= 0) {
    goto out;
  }

  if (!notify(subscriber->evsub, content_type, body, subscription_state, reason,
              document)) {
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

// int pjw_notify_xfer(long subscriber_id, int subscription_state, int
// xfer_status_code, const char *xfer_status_text) {
int pjw_notify_xfer(long subscriber_id, const char *json) {
  PJW_LOCK();
  clear_error();

  pjsip_tx_data *tdata;
  pj_status_t status;

  int subscription_state;
  int code;
  char *reason;

  long val;

  Subscriber *subscriber;
  pj_str_t r;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"subscription_state", "code", "reason", ""};

  if (!g_subscriber_ids.get(subscriber_id, val)) {
    set_error("Invalid subscriber_id");
    goto out;
  }
  subscriber = (Subscriber *)val;

  if (!subscriber->created_by_refer) {
    set_error("Subscriber was not created by REFER");
    goto out;
  }

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_int_param(document, "subscription_state", false,
                         &subscription_state) <= 0) {
    goto out;
  }

  if (json_get_int_param(document, "code", false, &code) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "reason", false, &reason) <= 0) {
    goto out;
  }

  r = pj_str((char *)reason);

  status = pjsip_xfer_notify(subscriber->evsub,
                             (pjsip_evsub_state)subscription_state, code, &r,
                             &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_xfer_notify failed with status=%i", status);
    goto out;
  }

  status = pjsip_xfer_send_request(subscriber->evsub, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_xfer_send_request failed with status=%i", status);
    goto out;
  }

out:
  PJW_UNLOCK();

  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

pj_bool_t add_headers(pj_pool_t *pool, pjsip_tx_data *tdata,
                      Document &document) {
  if (!document.HasMember("headers")) {
    return PJ_TRUE;
  }

  if (!document["headers"].IsObject()) {
    set_error("Parameter headers must be an object");
    return PJ_FALSE;
  }

  Value headers = document["headers"].GetObject();

  for (Value::ConstMemberIterator itr = headers.MemberBegin();
       itr != headers.MemberEnd(); ++itr) {
    if (!itr->value.IsString()) {
      set_error("Value of header must be string");
      return PJ_FALSE;
    }
    printf("%s => '%s'\n", itr->name.GetString(), itr->value.GetString());

    const char *name = itr->name.GetString();
    if (!itr->value.IsString()) {
      set_error("Parameter headers key '%s' found with non-string value", name);
      return PJ_FALSE;
    }

    const char *value = itr->value.GetString();

    pj_str_t hname = pj_str((char *)name);
    pjsip_hdr *hdr = (pjsip_hdr *)pjsip_parse_hdr(pool, &hname, (char *)value,
                                                  strlen(value), NULL);

    if (!hdr) {
      set_error("Failed to parse header '%s' => '%s'", name, value);
      return PJ_FALSE;
    }
    pjsip_hdr *clone_hdr = (pjsip_hdr *)pjsip_hdr_clone(pool, hdr);
    pjsip_msg_add_hdr(tdata->msg, clone_hdr);
  }
  return PJ_TRUE;
}

pj_bool_t add_headers_for_account(pjsip_regc *regc, Document &document) {
  pjsip_hdr hdr_list;
  pj_list_init(&hdr_list);

  char pool_buf[4096];
  pj_pool_t *pool;
  pool = pj_pool_create_on_buf(NULL, pool_buf, sizeof(pool_buf));

  if (!document.HasMember("headers")) {
    return PJ_TRUE;
  }

  if (!document["headers"].IsObject()) {
    set_error("Parameter headers must be an object");
    return PJ_FALSE;
  }

  Value headers = document["headers"].GetObject();

  for (Value::ConstMemberIterator itr = headers.MemberBegin();
       itr != headers.MemberEnd(); ++itr) {
    printf("%s => '%s'\n", itr->name.GetString(), itr->value.GetString());

    const char *name = itr->name.GetString();
    if (!itr->value.IsString()) {
      set_error("Parameter headers key '%s' found with non-string value", name);
      return PJ_FALSE;
    }

    const char *value = itr->value.GetString();

    pj_str_t hname = pj_str((char *)name);
    pjsip_hdr *hdr = (pjsip_hdr *)pjsip_parse_hdr(pool, &hname, (char *)value,
                                                  strlen(value), NULL);

    if (!hdr) {
      set_error("Failed to parse header %s", name);
      return PJ_FALSE;
    }

    pj_list_push_back(&hdr_list, hdr);
  }

  pjsip_regc_add_headers(regc, &hdr_list);
  return PJ_TRUE;
}

pj_bool_t get_content_type_and_subtype_from_headers(Document &document,
                                                    char *type, char *subtype) {
  if (!document.HasMember("headers")) {
    set_error("Parameter headers absent");
    return PJ_FALSE;
  }

  if (!document["headers"].IsObject()) {
    set_error("Parameter headers must be an object");
    return PJ_FALSE;
  }

  Value headers = document["headers"].GetObject();

  if (!headers.HasMember("Content-Type")) {
    set_error("Parameter headers doesn't contain key Content-Type");
    return PJ_FALSE;
  }

  const char *content_type = headers["Content-Type"].GetString();

  const char *slash;
  int index;

  slash = strchr(content_type, '/');
  if (!slash) {
    set_error("Invalid header Content-Type");
    return PJ_FALSE;
  }

  index = (int)(slash - content_type);

  strncpy(type, content_type, index - 1);
  strcpy(subtype, content_type + index);
  addon_log(L_DBG, "Checking parsing of Content-Type. type=%s: subtype=%s\n",
            type, subtype);
  return PJ_TRUE;
}

bool register_pkg(const char *event, const char *accept) {
  PackageSet::iterator iter =
      g_PackageSet.find(make_pair(string(event), string(accept)));
  if (g_PackageSet.end() == iter) {
    g_PackageSet.insert(make_pair(string(event), string(accept)));
    pj_status_t status;
    pj_str_t s_event = pj_str((char *)event);
    pj_str_t s_accept = pj_str((char *)accept);
    status = pjsip_evsub_register_pkg(&mod_tester, &s_event, DEFAULT_EXPIRES, 1,
                                      &s_accept);
    if (status != PJ_SUCCESS) {
      set_error("pjsip_evsub_register_pkg failed");
      return false;
    }
  }
  return true;
}

int pjw_register_pkg(const char *event, const char *accept) {
  PJW_LOCK();

  clear_error();

  // int n;

  if (!register_pkg(event, accept)) {
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

// int pjw_subscription_create(long transport_id, const char *event, const char
// *accept, const char *from_uri, const char *to_uri, const char *request_uri,
// const char *proxy_uri, const char *realm, const char *username, const char
// *password, long *out_subscription_id) {
int pjw_subscription_create(long transport_id, const char *json,
                            long *out_subscription_id) {
  PJW_LOCK();
  clear_error();

  char *event = NULL;
  char *accept = NULL;

  char *from_uri = NULL;
  char *to_uri = NULL;
  char *request_uri = NULL;
  char *proxy_uri = NULL;

  char *realm = NULL;
  char *username = NULL;
  char *password = NULL;

  long subscription_id;
  Subscription *subscription;
  Transport *t;
  long val;
  const char *contact_username = "sip";

  char local_contact[400];
  // char *start;

  pjsip_dialog *dlg = NULL;
  pjsip_evsub *evsub = NULL;
  pjsip_evsub_user user_cb;

  pj_str_t s_event;

  pj_status_t status;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {
      "event",       "accept",    "from_uri", "to_uri",
      "request_uri", "proxy_uri", "auth",     ""};

  if (!g_transport_ids.get(transport_id, val)) {
    set_error("Invalid transport_id");
    goto out;
  }
  t = (Transport *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_string_param(document, "event", false, &event) <= 0) {
    goto out;
  }

  if (json_get_string_param(document, "accept", false, &accept) <= 0) {
    goto out;
  }

  if (!json_get_and_check_uri(document, "from_uri", false, &from_uri)) {
    goto out;
  }

  if (!json_get_and_check_uri(document, "to_uri", false, &to_uri)) {
    goto out;
  }

  request_uri = to_uri;
  if (!json_get_and_check_uri(document, "request_uri", true, &request_uri)) {
    goto out;
  }

  if (!json_get_and_check_uri(document, "proxy_uri", true, &proxy_uri)) {
    goto out;
  }

  if (document.HasMember("auth")) {
    if (!document["auth"].IsObject()) {
      set_error("Parameter auth must be an object");
      goto out;
    } else {
      const Value &auth = document["auth"];

      for (Value::ConstMemberIterator itr = auth.MemberBegin();
           itr != auth.MemberEnd(); ++itr) {
        const char *name = itr->name.GetString();
        if (strcmp(name, "realm") == 0) {
          if (!itr->value.IsString()) {
            set_error("%s must be a string", itr->name.GetString());
            goto out;
          }
          realm = (char *)itr->value.GetString();
        } else if (strcmp(name, "username") == 0) {
          if (!itr->value.IsString()) {
            set_error("%s must be a string", itr->name.GetString());
            goto out;
          }
          username = (char *)itr->value.GetString();
          contact_username = username;
        } else if (strcmp(name, "password") == 0) {
          if (!itr->value.IsString()) {
            set_error("%s must be a string", itr->name.GetString());
            goto out;
          }
          password = (char *)itr->value.GetString();
        } else {
          set_error("Unknown auth paramter %s", itr->name.GetString());
          goto out;
        }
      }
    }
  }

  build_local_contact(local_contact, t->sip_transport, contact_username);

  if (!dlg_create(&dlg, t, from_uri, to_uri, request_uri, realm, username,
                  password, local_contact)) {
    goto out;
  }

  if (!register_pkg(event, accept)) {
    goto out;
  }

  memset(&user_cb, 0, sizeof(user_cb));
  user_cb.on_evsub_state = client_on_evsub_state;
  user_cb.on_client_refresh = on_client_refresh;
  user_cb.on_rx_notify = on_rx_notify;

  s_event = pj_str((char *)event);

  status = pjsip_evsub_create_uac(dlg, &user_cb, &s_event,
                                  PJSIP_EVSUB_NO_EVENT_ID, &evsub);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_evsub_create_uac failed with status=%i", status);
    goto out;
  }

  if (!dlg_set_transport_by_t(t, dlg)) {
    goto out;
  }

  if (!set_proxy(dlg, proxy_uri)) {
    goto out;
  }

  subscription =
      (Subscription *)pj_pool_zalloc(dlg->pool, sizeof(Subscription));

  if (!g_subscription_ids.add((long)subscription, subscription_id)) {
    status = pjsip_dlg_terminate(dlg); // ToDo:
    set_error("Failed to allocate id");
    goto out;
  }
  subscription->id = subscription_id;
  subscription->evsub = evsub;
  subscription->dlg = dlg;
  strcpy(subscription->event, event);
  strcpy(subscription->accept, accept);

  pjsip_evsub_set_mod_data(evsub, mod_tester.id, subscription);

  printf("subscription=%p\n", (void*)subscription);

  *out_subscription_id = subscription_id;
out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

bool subscription_subscribe_no_headers(Subscription *s, int expires) {
  pj_status_t status;
  pjsip_tx_data *tdata;

  status = pjsip_evsub_initiate(s->evsub, NULL, expires, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_evsub_initiate failed");
    return false;
  }

  status = pjsip_evsub_send_request(s->evsub, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_send_msg failed");
    return false;
  }

  // Without this, on_rx_response will not be called
  status = pjsip_dlg_add_usage(s->dlg, &mod_tester, s);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_dlg_add_usage failed");
    return false;
  }

  return true;
}

bool subscription_subscribe(Subscription *s, int expires, Document &document) {
  pj_status_t status;
  pjsip_tx_data *tdata;

  status = pjsip_evsub_initiate(s->evsub, NULL, expires, &tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_evsub_initiate failed");
    return false;
  }

  if (!add_headers(s->dlg->pool, tdata, document)) {
    return false;
  }

  status = pjsip_evsub_send_request(s->evsub, tdata);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_inv_send_msg failed");
    return false;
  }

  // Without this, on_rx_response will not be called
  status = pjsip_dlg_add_usage(s->dlg, &mod_tester, s);
  if (status != PJ_SUCCESS) {
    set_error("pjsip_dlg_add_usage failed");
    return false;
  }

  return true;
}

// int pjw_subscription_subscribe(long subscription_id, int expires, const char
// *additional_headers) {
int pjw_subscription_subscribe(long subscription_id, const char *json) {
  PJW_LOCK();
  clear_error();

  int expires;

  Subscription *subscription;

  long val;

  char buffer[MAX_JSON_INPUT];

  Document document;

  const char *valid_params[] = {"expires", "headers", ""};

  if (!g_subscription_ids.get(subscription_id, val)) {
    set_error("Invalid subscription_id");
    goto out;
  }
  subscription = (Subscription *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (!validate_params(document, valid_params)) {
    goto out;
  }

  if (json_get_int_param(document, "expires", true, &expires) <= 0) {
    goto out;
  }

  if (!subscription_subscribe(subscription, expires, document)) {
    goto out;
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }
  return 0;
}

void process_in_dialog_subscribe(pjsip_dialog *dlg, pjsip_rx_data *rdata) {
  return;
}

int pjw_call_gen_string_replaces(long call_id, char *out_replaces) {
  PJW_LOCK();

  clear_error();

  // int n;
  long val;
  Call *call;
  pjsip_dialog *dlg;
  // int len;
  char *p;
  char buf[2000];
  pjsip_uri *uri;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }

  call = (Call *)val;

  p = buf;
  p += sprintf(p, "%s", "<");

  dlg = call->inv->dlg;

  uri = (pjsip_uri *)pjsip_uri_get_uri(dlg->remote.info->uri);
  p += pjsip_uri_print(PJSIP_URI_IN_REQ_URI, uri, p, buf - p);
  p += pj_ansi_sprintf(
      p,
      "%.*s"
      ";to-tag=%.*s"
      ";from-tag=%.*s",
      (int)dlg->call_id->id.slen, dlg->call_id->id.ptr,
      (int)dlg->remote.info->tag.slen, dlg->remote.info->tag.ptr,
      (int)dlg->local.info->tag.slen, dlg->local.info->tag.ptr);

out:
  PJW_UNLOCK();

  if (pjw_errorstring[0]) {
    return -1;
  }
  strcpy(out_replaces, buf);
  return 0;
}

pj_status_t tcp_endpoint_send_msg(Call *call, MediaEndpoint *me, char *msg, pj_ssize_t size) {
  pj_status_t status;
  pj_activesock_t *asock = NULL;

  if (ENDPOINT_TYPE_MRCP == me->type) {
    MrcpEndpoint *e = (MrcpEndpoint*)me->endpoint.mrcp;
    asock = e->asock;
  } else {
    set_error("cannot send tcp msg. invalid media");
    return -1;
  }

  if(asock) {
    pj_ioqueue_op_key_t *send_key;
    send_key = (pj_ioqueue_op_key_t*)pj_pool_alloc(call->inv->pool, sizeof(pj_ioqueue_op_key_t));
    char *data = (char*)pj_pool_alloc(call->inv->pool, size);
    memcpy(data, msg, size);
    printf("tcp_endpoint_send_msg send_key %p\n", (void*)send_key);
    //status = pj_activesock_send(asock, send_key, data, &size, 0);
    status = pj_activesock_send(asock, send_key, data, &size, PJ_IOQUEUE_ALWAYS_ASYNC);
    if (status != PJ_SUCCESS) {
      return status;
    }
    printf("tcp_endpoint_send_msg success for call_id=%d\n", call->id);
    return PJ_SUCCESS;
  }

  set_error("asock not ready");
  return -1;
}


pj_status_t call_send_tcp_msg(Call *call, char *msg, pj_ssize_t size) {
  addon_log(L_DBG, "call_send_tcp_msg=%d\n",
            call->media_count);
  pj_status_t status;
  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (ENDPOINT_TYPE_MRCP == me->type) {
      status = tcp_endpoint_send_msg(call, me, msg, size);
      if(status != PJ_SUCCESS) {
        return status;
      }
    }
  }

  return PJ_SUCCESS;
}

int pjw_call_send_tcp_msg(long call_id, const char *json) {
  PJW_LOCK();
  clear_error();

  Call *call;

  pj_status_t status;

  long val;

  MediaEndpoint *me;
  int res;

  unsigned media_id = 0;

  char buffer[MAX_JSON_INPUT];

  char *msg;
  pj_ssize_t size;

  Document document;

  if (!g_call_ids.get(call_id, val)) {
    set_error("Invalid call_id");
    goto out;
  }
  call = (Call *)val;

  if (!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
    goto out;
  }

  if (json_get_string_param(document, "msg", false, &msg) <= 0) {
    goto out;
  }
  size = strlen(msg);

  res = json_get_uint_param(document, "media_id", true, &media_id);
  if (res <= 0) {
    goto out;
  }

  if (NOT_FOUND_OPTIONAL == res) {
    // Send msg to all TCP endpoints (MRCP, etc)
    status = call_send_tcp_msg(call, msg, size);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  } else {
    // Send msg to specified media_id

    if ((int)media_id >= call->media_count) {
      set_error("invalid media_id");
      goto out;
    }

    me = (MediaEndpoint *)call->media[media_id];
    status = tcp_endpoint_send_msg(call, me, msg, size);
    if (status != PJ_SUCCESS) {
      goto out;
    }
  }

out:
  PJW_UNLOCK();
  if (pjw_errorstring[0]) {
    return -1;
  }

  return 0;
}

int pjw_log_level(long log_level) {
  PJW_LOCK();

  pj_log_set_level(log_level);

  PJW_UNLOCK();
  return 0;
}

int pjw_set_flags(unsigned flags) {
  PJW_LOCK();

  g_flags = flags;

  PJW_UNLOCK();
  return 0;
}

static int g_now;

void check_digit_buffer(Call *call, int mode) {
  // addon_log(L_DBG, "check_digit_buffer g_now=%i for call_id=%i and mode=%i
  // timestamp=%i len=%i\n", g_now, c->id, mode, c->last_digit_timestamp[mode],
  // c->DigitBufferLength[mode]);
  char evt[1024];

  for (int i = 0; i < call->media_count; i++) {
    MediaEndpoint *me = (MediaEndpoint *)call->media[i];
    if (ENDPOINT_TYPE_AUDIO != me->type)
      continue;

    AudioEndpoint *ae = (AudioEndpoint *)me->endpoint.audio;

    if (ae->last_digit_timestamp[mode] > 0 &&
        g_now - ae->last_digit_timestamp[mode] > g_dtmf_inter_digit_timer) {
      int *pLen = &ae->DigitBufferLength[mode];
      ae->DigitBuffers[mode][*pLen] = 0;
      make_evt_dtmf(evt, sizeof(evt), call->id, *pLen, ae->DigitBuffers[mode],
                    mode, i);
      dispatch_event(evt);
      *pLen = 0;
      ae->last_digit_timestamp[mode] = 0;
    }
  }
}

void check_digit_buffers(long id, long val) {
  Call *c = (Call *)val;

  check_digit_buffer(c, DTMF_MODE_RFC2833);
  check_digit_buffer(c, DTMF_MODE_INBAND);
}

static int digit_buffer_thread(void *arg) {
  // addon_log(L_DBG, "Starting digit_buffer_thread\n");

  pj_thread_set_prio(pj_thread_this(),
                     pj_thread_get_prio_min(pj_thread_this()));

  PJ_UNUSED_ARG(arg);

  while (!g_shutting_down) {
    PJW_LOCK();
    if (g_dtmf_inter_digit_timer > 0) {
      g_now = ms_timestamp();
      g_call_ids.iterate(check_digit_buffers);
    }
    PJW_UNLOCK();

    pj_thread_sleep(100);
  }
  return 0;
}

bool start_digit_buffer_thread() {
  pj_status_t status;
  pj_pool_t *pool =
      pj_pool_create(&cp.factory, "digit_buffer_checker", 1000, 1000, NULL);
  pj_thread_t *t;
  status = pj_thread_create(pool, "digit_buffer_checker", &digit_buffer_thread,
                            NULL, 0, 0, &t);
  if (status != PJ_SUCCESS) {
    addon_log(L_DBG, "start_digit_buffer_thread failed\n");
    return false;
  }

  return true;
}

/* provides timestamp in milliseconds */
int ms_timestamp() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    return -1;
  }
  return ((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
}

int pjw_dtmf_aggregation_on(int inter_digit_timer) {
  PJW_LOCK();

  if (inter_digit_timer <= 0) {
    PJW_UNLOCK();
    set_error("Invalid argument: inter_digit_timer must be greater than zero");
    return -1;
  }

  g_dtmf_inter_digit_timer = inter_digit_timer;

  PJW_UNLOCK();
  return 0;
}

int pjw_dtmf_aggregation_off() {
  PJW_LOCK();

  g_dtmf_inter_digit_timer = 0;

  PJW_UNLOCK();
  return 0;
}
