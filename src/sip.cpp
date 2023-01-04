#include <arpa/inet.h>

#include "sip.hpp"

#include <string>
#include <iostream>
#include <sstream>
#include <deque>
#include <map>
#include <set>

#include <boost/circular_buffer.hpp>
#include <algorithm>

#include <pthread.h>

#include <ctime>

#include "idmanager.hpp"
//#include "packetdumper.hpp"
#include "event_templates.hpp"

//Customized media ports that can be chained
#include "chainlink.h"
#include "chainlink_wire_port.h"
#include "chainlink_dtmfdet.h"
#include "chainlink_tonegen.h"
#include "chainlink_wav_port.h"
#include "chainlink_fax.h"

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
IdManager g_call_ids(IDS_MAX);
IdManager g_subscription_ids(IDS_MAX);
IdManager g_subscriber_ids(IDS_MAX);
IdManager g_dialog_ids(IDS_MAX);

//PacketDumper *g_PacketDumper = 0;

#define AF	pj_AF_INET()
#define DEFAULT_ILBC_MODE	(30)
#define DEFAULT_CODEC_QUALITY	(5)

#define UNKNOWN  0 
#define SENDRECV 1
#define SENDONLY 2
#define RECVONLY 3
#define INACTIVE 4

static pjsip_endpoint *g_sip_endpt;
static pj_caching_pool cp;
static pj_pool_t *g_pool;
static pjmedia_endpt *g_med_endpt;

//static pj_thread_t *g_thread = NULL;
//static pj_bool_t g_thread_quit_flag;

static deque<string> g_events; 
static pthread_mutex_t g_mutex;

static char pjw_errorstring[4096];

void clear_error() {
	pjw_errorstring[0] = 0;
}

/*
void set_error(char *err) {
	//printf("set_error: %s\n", err);
	strcpy(pjw_errorstring, err);
}
*/

void set_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(pjw_errorstring, sizeof(pjw_errorstring), format, args);
    va_end(args);
}

char *pjw_get_error() {
	return pjw_errorstring;
}

int check_uri(const char *uri) {
        return (strstr(uri, "sip:") != NULL);
}

bool parse_json(Document &document, const char *json, char *buffer, long unsigned int len) {
    if(strlen(json) > len -1) {
        set_error("JSON too large");
        return false;
    }

    strcpy(buffer, json);
    if (document.ParseInsitu(buffer).HasParseError()) {
        set_error("Failed to parse JSON");
        return false;
    }

    if(!document.IsObject()) {
        set_error("Invalid JSON root. Must be an object");
        return false;
    }

    return true;
}


bool param_is_valid(const char *param, const char **valid_params) {
    char **valid_param = (char**)valid_params;
    while(*valid_param[0]) {
        //printf("checking param=%s valid_param=%s\n", param, *valid_param);
        if(strcmp(param, *valid_param) == 0) {
            return true;
        }
        valid_param++;
    }
    return false;
}

bool validate_params(Document &document, const char **valid_params) {
    for (Value::ConstMemberIterator itr = document.MemberBegin();
        itr != document.MemberEnd(); ++itr)
    {
        const char *param = itr->name.GetString();
        if(!param_is_valid(param, valid_params)) {
            set_error("Invalid param %s", param);
            return false;
        }
    }

    return true;
}

bool json_get_string_param(Document &document, const char *param, bool optional, char **dest) {
    printf("json_get_string_param %s\n", param);
    if(!document.HasMember(param)) {
        if(optional) {
            return true;
        } 
        set_error("Parameter %s is required", param);
        return false;
    }

    if(!document[param].IsString()) {
        set_error("Parameter %s must be a string", param);
        return false;
    }
    *dest = (char*)document[param].GetString();
    return true;
}

bool json_get_int_param(Document &document, const char *param, bool optional, int *dest) {
    if(!document.HasMember(param)) {
        if(optional) {
            return true;
        } 
        set_error("Parameter %s is required", param);
        return false;
    }

    if(!document[param].IsInt()) {
        set_error("Parameter %s must be an integer", param);
        return false;
    }
    *dest = document[param].GetInt();
    return true;
}

bool json_get_uint_param(Document &document, const char *param, bool optional, unsigned *dest) {
    if(!document.HasMember(param)) {
        if(optional) {
            return true;
        } 
        set_error("Parameter %s is required", param);
        return false;
    }

    if(!document[param].IsUint()) {
        set_error("Parameter %s must be an unsigned integer", param);
        return false;
    }
    *dest = document[param].GetUint();
    return true;
}

bool json_get_bool_param(Document &document, const char *param, bool optional, bool *dest) {
    if(!document.HasMember(param)) {
        if(optional) {
            return true;
        } 
        set_error("Parameter %s is required", param);
        return false;
    }

    if(!document[param].IsBool()) {
        set_error("Parameter %s must be a boolean", param);
        return false;
    }
    *dest = document[param].GetBool();
    return true;
}


bool json_get_and_check_uri(Document &document, const char *param, bool optional, char **dest) {
    if(!json_get_string_param(document, param, optional, dest)) {
        return false;
    }

    if(*dest && *dest[0]) {
        if(!check_uri(*dest)) {
                set_error("Invalid %s", param);
                return false;
        }
    }

    return true;
}



#define PJW_LOCK()	pthread_mutex_lock(&g_mutex)
#define PJW_UNLOCK()	pthread_mutex_unlock(&g_mutex)

char *trim(char *dest, char *src){
        while(*src == ' ' || *src == '\t') src++;
        strcpy(dest, src);

        int len = strlen(dest);
        char *end = dest + len -1;

        while(*end == ' ' || *end == '\t') *end-- = 0;
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
const pj_str_t hname = pj_str((char*)"Route");

#define MAXDIGITS 256 
#define INITIAL_DIGITBUFFERLENGTH 1

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

struct Call {
	int id;
	pjsip_inv_session *inv;
	pjmedia_transport *med_transport;
	pjmedia_stream *med_stream;
	pj_bool_t local_hold;
	pj_bool_t remote_hold;
	pjmedia_master_port *master_port;
	pjmedia_port *media_port; //will contain Null Port, WAV File Player etc.

	pjmedia_port *null_port;
	chainlink *wav_writer;
	chainlink *wav_player;
	chainlink *tonegen;
	chainlink *dtmfdet;
	chainlink *fax;

	Transport *transport;

	bool outgoing;

	char DigitBuffers[2][MAXDIGITS + 1];
	int DigitBufferLength[2];
	int last_digit_timestamp[2];

	pjsip_evsub *xfer_sub; // Xfer server subscription, if this call was triggered by xfer.

	pjsip_rx_data *initial_invite_rdata;
};


struct Pair_Call_CallId {
	Call *pCall;
	long id;
	bool operator==(const Pair_Call_CallId &pcc) const{
		if(this->pCall == pcc.pCall) return true; 
		return false;
	};
};
typedef boost::circular_buffer<Pair_Call_CallId> Pair_Call_CallId_Buf;
Pair_Call_CallId_Buf g_LastCalls(1000);	
	

typedef map<pjsip_transport*, int> SipTransportMap;
SipTransportMap g_SipTransportMap;
int g_TlsTransportId = -100;
int g_TcpTransportId = -100;

typedef set< pair<string,string> > PackageSet;
PackageSet g_PackageSet;

#define DEFAULT_EXPIRES 600

/*
static void pool_callback(pj_pool_t *pool, pj_size_t size)
{
	PJ_CHECK_STACK();
	PJ_UNUSED_ARG(pool);
	PJ_UNUSED_ARG(size);

	PJ_THROW(PJ_NO_MEMORY_EXCEPTION);
}
*/

void handle_events(){
	//unsigned count = 0;
	//pj_time_val tv = {0, 500};
	//pj_time_val tv = {0,10};
	//pj_time_val tv = {0,100};
	pj_time_val tv = {0,1};
	//pj_time_val_normalize(&tv);
    //pj_status_t status;
	//status = pjsip_endpt_handle_events(g_sip_endpt, &tv);
	pjsip_endpt_handle_events(g_sip_endpt, &tv);
}

/*
static int worker_thread(void *arg)
{
	//addon_log(LOG_LEVEL_DEBUG, "Starting worker_thread\n");

	pj_thread_set_prio(pj_thread_this(),
			pj_thread_get_prio_min(pj_thread_this()));

	enum { TIMEOUT = 10 };

	PJ_UNUSED_ARG(arg);

	while(!g_thread_quit_flag){
		PJW_LOCK();
		//addon_log(LOG_LEVEL_DEBUG, ".");
		handle_events();
		PJW_UNLOCK();

		usleep(10);
	}
	return 0;
}
*/

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
static void on_media_update( pjsip_inv_session *inv,
				  pj_status_t status);

/* Callback to be called when invite session's state has changed: */
static void on_state_changed( pjsip_inv_session *inv, 
				   pjsip_event *e);

/* Callback to be called when dialog has forked: */
static void on_forked(pjsip_inv_session *inv, pjsip_event *e);

/* Callback to be called to handle incoming requests */
static pj_bool_t on_rx_request( pjsip_rx_data *rdata );

/* Callback to be called when responses are received */
static pj_bool_t on_rx_response( pjsip_rx_data *rdata );

/* Callback to be called when media offer is received (in REINVITEs but also in late negotiaion scenario) */
//static void on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session *offer);
static void on_rx_offer2(pjsip_inv_session *inv, struct pjsip_inv_on_rx_offer_cb_param *param);


/* Callback to be called when REINVITE is received */
//static pj_status_t on_rx_reinvite(pjsip_inv_session *inv, const pjmedia_sdp_session *offer, pjsip_rx_data *rdata);

/* Callback to be called when Redirect is received */
static pjsip_redirect_op on_redirected(pjsip_inv_session *inv, const pjsip_uri *target, const pjsip_event *e);

/* Callback to be called when DTMF is received */
static void on_dtmf(pjmedia_stream *stream, void *user_data, int digit);

/* Callback for Registration Status */
static void on_registration_status(pjsip_regc_cbparam *param);

static void on_tsx_state_changed(pjsip_inv_session *inv,
					    pjsip_transaction *tsx,
					    pjsip_event *e);

static void client_on_evsub_state( pjsip_evsub *sub, pjsip_event *event);
static void on_client_refresh( pjsip_evsub *sub );
void on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);

static void server_on_evsub_state( pjsip_evsub *sub, pjsip_event *event);
static void server_on_evsub_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);

bool dlg_create(pjsip_dialog **dlg, Transport *transport, const char *from_uri, const char *to_uri, const char *request_uri, const char *realm, const char *username, const char *password, const char *local_contact); 

static int call_create(Transport *t, unsigned flags, pjsip_dialog *dlg, const char *proxy_uri, Document &document);

bool subscription_subscribe_no_headers(Subscription *s, int expires);
bool subscription_subscribe(Subscription *s, int expires, Document &document);

static pjmedia_transport *create_media_transport(const pj_str_t *addr);
void close_media_transport(pjmedia_transport *med_transport);
pjsip_transport *create_udp_transport(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int *allocated_port);
pjsip_transport *allocate_udp_transport(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int port); 

pjsip_tpfactory *create_tls_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int *allocated_port);
pjsip_tpfactory *allocate_tls_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int port); 

pjsip_tpfactory *create_tcp_factory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int *allocated_port);
pjsip_tpfactory *allocate_tcp_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int port); 

bool set_proxy(pjsip_dialog *dlg, const char *proxy_uri);

void build_local_contact(char *dest, pjsip_transport *transport, const char *contact_username);
void build_local_contact_from_tpfactory(char *dest, pjsip_tpfactory *tpfactory, const char *contact_username, pjsip_transport_type_e type);

//pj_bool_t add_additional_headers(pj_pool_t *pool, pjsip_tx_data *tdata, const char *additional_headers); 
pj_bool_t add_headers(pj_pool_t *pool, pjsip_tx_data *tdata, Document &document); 

pj_bool_t add_headers_for_account(pjsip_regc* regc, Document &document); 

pj_bool_t get_content_type_and_subtype_from_headers(Document &document, char *type, char *subtype);

bool build_subscribe_info(ostringstream *oss, pjsip_rx_data *rdata, Subscriber *s);
//bool build_notify_info(pjsip_rx_data *rdata, Subscription *s);

bool add_header_info(ostringstream *oss, pjsip_rx_data *rdata, const char *headers_names, bool fail_on_not_found); 

void process_in_dialog_refer(pjsip_dialog *dlg, pjsip_rx_data *rdata);

//void process_response(pjsip_rx_data *rdata, const char *entity_name, int entity_id);

void process_in_dialog_subscribe(pjsip_dialog *dlg, pjsip_rx_data *rdata);

static pj_bool_t set_ports(Call *call, pjmedia_port *stream_port, pjmedia_port *media_port);
//static pj_bool_t stop_media_operation(Call *call);
static void build_stream_stat(ostringstream &oss, pjmedia_rtcp_stat *stat, pjmedia_stream_info *stream_info);

bool init_media_ports(Call *c, unsigned sampling_rate, unsigned channel_count, unsigned samples_per_frame, unsigned bits_per_sample); 
void connect_media_ports(Call *c);

bool prepare_wire(pj_pool_t *pool, chainlink **link, unsigned sampling_rate, unsigned channel_count, unsigned samples_per_frame, unsigned bits_per_sample);

bool prepare_tonegen(Call *c); 
bool prepare_wav_player(Call *c, const char *file);
bool prepare_wav_writer(Call *c, const char *file); 

bool prepare_fax(Call *c, bool is_sender, const char *file, unsigned flags); 

void prepare_error_event(ostringstream *oss, char *scope, char *details);
//void prepare_pjsipcall_error_event(ostringstream *oss, char *scope, char *function, pj_status_t s);
void append_status(ostringstream *oss, pj_status_t s);

static pjsip_module mod_tester =
{
	NULL, NULL,			    /* prev, next.		*/
	{ (char*)"mod_tester", 10 },	    /* Name.			*/
	-1,				    /* Id			*/
	//PJSIP_MOD_PRIORITY_APPLICATION, /* Priority			*/
	PJSIP_MOD_PRIORITY_DIALOG_USAGE, /* Priority			*/
	NULL,			    /* load()			*/
	NULL,			    /* start()			*/
	NULL,			    /* stop()			*/
	NULL,			    /* unload()			*/
	&on_rx_request,		    /* on_rx_request()		*/
	&on_rx_response,	    /* on_rx_response()		*/
	NULL,			    /* on_tx_request.		*/
	NULL,			    /* on_tx_response()		*/
	NULL,			    /* on_tsx_state()		*/
};


struct Timer
{
	pj_timer_entry timer_entry;
	pj_time_val delay;
	pj_bool_t in_use;
	unsigned id;
};

Timer _timer;

void dispatch_event(const char * evt);

const char *translate_pjsip_inv_state(int state)
{
	switch(state)
	{
	case PJSIP_INV_STATE_NULL: return "null";
	case PJSIP_INV_STATE_CALLING: return "calling";
	case PJSIP_INV_STATE_INCOMING: return "incoming";
    	case PJSIP_INV_STATE_EARLY: return "early";
    	case PJSIP_INV_STATE_CONNECTING: return "connecting";
	case PJSIP_INV_STATE_CONFIRMED: return "confirmed";
	case PJSIP_INV_STATE_DISCONNECTED: return "disconnected";
	default: return "unknown";
	}
}

static void on_inband_dtmf(pjmedia_port *port, void *user_data, char digit){
	if(g_shutting_down) return;

	long call_id;
	if( !g_call_ids.get_id((long)user_data, call_id) ){
		addon_log(LOG_LEVEL_DEBUG, "on_inband_dtmf: Failed to get call_id. Event will not be notified.\n");	
		return;
	}

	char d = tolower(digit);
	if(d == '*') d = 'e';
	if(d == '#') d = 'f';

	Call *c = (Call*)user_data;

	int mode = DTMF_MODE_INBAND;

	if(g_dtmf_inter_digit_timer) {

		PJW_LOCK();
		int *pLen = &c->DigitBufferLength[mode];

		if(*pLen > MAXDIGITS) {
			PJW_UNLOCK();
			addon_log(LOG_LEVEL_DEBUG, "No more space for digits in inband buffer\n");
			return;
		}

		c->DigitBuffers[mode][*pLen] = d;
		(*pLen)++;
		c->last_digit_timestamp[mode] = ms_timestamp();
		PJW_UNLOCK();
	} else {
		/*
		ostringstream oss;

		oss << "event=dtmf" << EVT_DATA_SEP << "call=" << call_id << EVT_DATA_SEP << "digits=" << d << EVT_DATA_SEP << "mode=" << DTMF_MODE_INBAND;
		dispatch_event(oss.str().c_str());
		*/

		char evt[256];
		make_evt_dtmf(evt, sizeof(evt), call_id, 1, &d, mode);
		dispatch_event(evt);
	}
}

static void on_fax_result(pjmedia_port *port, void *user_data, int result){
   if(g_shutting_down) return;

   long call_id;
   if( !g_call_ids.get_id((long)user_data, call_id) ){
       printf("on_fax_result: Failed to get call_id. Event will not be notified.\n");
       return;
   }

   char evt[256];
   make_evt_fax_result(evt, sizeof(evt), call_id, result);
   dispatch_event(evt);
}


void dispatch_event(const char * evt)
{
	//addon_log(LOG_LEVEL_DEBUG, "dispach_event called\n");
	//g_event_sink(evt);

	g_events.push_back(evt);
}

static char *get_media_mode_str(int mode) {
	if(mode == SENDRECV) return (char*)"sendrecv";
	if(mode == SENDONLY) return (char*)"sendonly";
	if(mode == RECVONLY) return (char*)"recvonly";
	if(mode == INACTIVE) return (char*)"inactive";
	if(mode == UNKNOWN) return (char*)"unknown";
    return (char*)"unexpected";
}

static int get_media_mode(pjmedia_sdp_attr **attrs, int count) {
        int i;
        for(i=0 ; i<count ; ++i) {
                pjmedia_sdp_attr *a = attrs[i];
                if(pj_strcmp2(&a->name, "sendrecv")==0) {
                        return SENDRECV;
                } else if(pj_strcmp2(&a->name, "sendonly")==0) {
                        return SENDONLY;
                } else if(pj_strcmp2(&a->name, "recvonly")==0) {
                        return RECVONLY;
				} else if(pj_strcmp2(&a->name, "inactive")==0) {
                        return INACTIVE;
				}
        }
        return UNKNOWN;
}

int __pjw_init()
{
	addon_log(LOG_LEVEL_DEBUG, "pjw_init thread_id=%i\n", syscall(SYS_gettid));

	g_shutting_down = false;

	pj_status_t status;

	status = pj_init();
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pj_init failed\n");
		return 1; 
	}

	status = pjlib_util_init();
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pj_lib_util_init failed\n");
		return 1;
	}

	pthread_mutex_init(&g_mutex,NULL);

	pj_log_set_level(0);

	pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

	char *sip_endpt_name = (char*)"mysip";
	
	status = pjsip_endpt_create(&cp.factory, sip_endpt_name, &g_sip_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_endpt_create failed\n");
		return 1;
	}

    g_pool = pj_pool_create(&cp.factory, "tester", 1000, 1000, NULL);

    /* Create event manager */
    status = pjmedia_event_mgr_create(g_pool, 0, NULL);
    if(status != PJ_SUCCESS)
    {
        addon_log(LOG_LEVEL_DEBUG, "pjmedia_event_mgr_create  failed\n");
        return 1;
    }

	const pj_str_t msg_tag = { (char*)"MESSAGE", 7 };
	const pj_str_t STR_MIME_TEXT_PLAIN = { (char*)"text/plain", 10 };
	const pj_str_t STR_MIME_APP_ISCOMPOSING = { (char*)"application/im-iscomposing+xml", 30 };

	/* Register support for MESSAGE method. */
	status = pjsip_endpt_add_capability(g_sip_endpt, &mod_tester, PJSIP_H_ALLOW,
		NULL, 1, &msg_tag);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_endpt_add_capability PJSIP_H_ALLOW failed\n");
		return 1;
	}

	/* Register support for "application/im-iscomposing+xml" content */
	pjsip_endpt_add_capability(g_sip_endpt, &mod_tester, PJSIP_H_ACCEPT,
		NULL, 1, &STR_MIME_APP_ISCOMPOSING);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_endpt_add_capability PJSIP_H_ACCEPT for MIME_APP_ISCOMPOSING failed\n");
		return 1;
	}

	/* Register support for "text/plain" content */
	pjsip_endpt_add_capability(g_sip_endpt, &mod_tester, PJSIP_H_ACCEPT,
		NULL, 1, &STR_MIME_TEXT_PLAIN);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_endpt_add_capability PJSIP_H_ACCEPT for MIME_TEXT_PLAIN failed\n");
		return 1;
	}

	status = pjsip_tsx_layer_init_module(g_sip_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_tsx_layer_init_module failed\n");
		return 1;
	}

	status = pjsip_ua_init_module(g_sip_endpt, NULL);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_ua_init_module failed\n");
		return 1;
	}

	status = pjsip_evsub_init_module(g_sip_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_evsub_init_module failed\n");
		return 1;
	}

	status = pjsip_xfer_init_module(g_sip_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_xfer_init_module failed\n");
		return 1;
	}

	status = pjsip_replaces_init_module(g_sip_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_replaces_init_module failed\n");
		return 1;
	}

	pjsip_inv_callback inv_cb;
	pj_bzero(&inv_cb, sizeof(inv_cb));
	inv_cb.on_state_changed = &on_state_changed;
	inv_cb.on_new_session = &on_forked;
	inv_cb.on_media_update = &on_media_update;
	inv_cb.on_rx_offer = NULL;
    inv_cb.on_rx_offer2 = &on_rx_offer2;
	//inv_cb.on_rx_reinvite = &on_rx_reinvite;
	inv_cb.on_tsx_state_changed = &on_tsx_state_changed;
	inv_cb.on_redirected = &on_redirected;

	status = pjsip_inv_usage_init(g_sip_endpt, &inv_cb);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_inv_usage_init failed\n");
		return 1;
	}

	status = pjsip_100rel_init_module(g_sip_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_100rel_init_module failed\n");
		return 1;
	}
	
	status = pjsip_endpt_register_module(g_sip_endpt, &mod_tester);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjsip_endpt_register_module failed\n");
		return 1;
	}
#if PJ_HAS_THREADS
	status = pjmedia_endpt_create2(&cp.factory, NULL, 1, &g_med_endpt);
#else
	status = pjmedia_endpt_create2(&cp.factory,
					pjsip_endpt_get_ioqueue(g_sip_endpt),
					0, &g_med_endpt);
#endif
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_endpt_create failed\n");
		return 1;
	}

#if defined(PJMEDIA_HAS_G711_CODEC) && PJMEDIA_HAS_G711_CODEC!=0
	status = pjmedia_codec_g711_init(g_med_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_g711_init failed\n");
		return 1;
	}
#endif

#if defined(PJMEDIA_HAS_GSM_CODEC) && PJMEDIA_HAS_GSM_CODEC!=0
	status = pjmedia_codec_gsm_init(g_med_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_gsm_init failed\n");
		return 1;
	}
#endif

#if defined(PJMEDIA_HAS_l16_CODEC) && PJMEDIA_HAS_l16_CODEC!=0
	status = pjmedia_codec_l16_init(g_med_endpt, 0);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_l16_init failed\n");
		return 1;
	}
#endif

#if defined(PJMEDIA_HAS_ILBC_CODEC) && PJMEDIA_HAS_ILBC_CODEC!=0
	status = pjmedia_codec_ilbc_init(g_med_endpt, DEFAULT_ILBC_MODE);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_ilbc_init failed\n");
		return 1;
	}
#endif

#if defined(PJMEDIA_HAS_SPEEX_CODEC) && PJMEDIA_HAS_SPEEX_CODEC!=0
	status = pjmedia_codec_speex_init(g_med_endpt,
				0,
				DEFAULT_CODEC_QUALITY,
				-1);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_speex_init failed\n");
		return 1;
	}
#endif

#if defined(PJMEDIA_HAS_G722_CODEC) && PJMEDIA_HAS_G722_CODEC!=0
	status = pjmedia_codec_g722_init(g_med_endpt);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_g722_init failed\n");
		return 1;
	}
#endif

#if defined(PJMEDIA_HAS_OPUS_CODEC) && PJMEDIA_HAS_OPUS_CODEC!=0
    status = pjmedia_codec_opus_init(g_med_endpt);
    if(status != PJ_SUCCESS)
    {
        addon_log(LOG_LEVEL_DEBUG, "pjmedia_codec_opus_init failed\n");
        return 1;
    }
#endif

    status = pjmedia_codec_bcg729_init(g_med_endpt);
    if(status != PJ_SUCCESS)
    {
        printf("pjmedia_codec_bcg729_init failed\n");
        return 1;
    }

	status = pj_thread_register("main_thread", g_main_thread_descriptor, &g_main_thread);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "pj_thread_register(main_thread) failed\n");
		exit(1);
	} else {
		//addon_log(LOG_LEVEL_DEBUG, "pj_thread_register(main_thread) success\n");
		;
	}

	if(!start_digit_buffer_thread()) {
		addon_log(LOG_LEVEL_DEBUG, "start_digit_buffer_thread() failed\n");
		return 1;
	}

	return 0;
}

int __pjw_poll(char *out_evt){
	if(!pj_thread_is_registered()) {
		pj_status_t status;
		status = pj_thread_register("poll_thread", g_poll_thread_descriptor, &g_poll_thread);
		if(status != PJ_SUCCESS)
		{
			addon_log(LOG_LEVEL_DEBUG, "pj_thread_register(poll_thread) failed\n");
			exit(1);
		} else {
			//addon_log(LOG_LEVEL_DEBUG, "pj_thread_register(poll_thread) success\n");
			;
		}
	}

	string evt;
	PJW_LOCK();
	handle_events();
	if(!g_events.empty()){
		evt = g_events[0];
		g_events.pop_front();
	}
	PJW_UNLOCK();
	if(evt != "") {
		strcpy(out_evt, evt.c_str());
		return 0;
	} 
	return -1;
}

pjsip_transport *allocate_udp_transport(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int port) {
	pj_status_t status;
	pjsip_transport *transport;

	pj_sockaddr addr;
	pj_sockaddr_init(AF, &addr, ipaddr, port);

	if (AF == pj_AF_INET()) {
		status = pjsip_udp_transport_start( sip_endpt, 
						&addr.ipv4, 
						NULL, 
						1, &transport);
	} else if (AF == pj_AF_INET6()) {
		status = pjsip_udp_transport_start6( sip_endpt, 
						&addr.ipv6, 
						NULL,
						1, &transport);
	} else {
		status = PJ_EAFNOTSUP;
	}

	if (status == PJ_SUCCESS) {
		return transport;
	}
	return NULL;
}

pjsip_transport *create_udp_transport(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int *allocated_port)
{
	//pj_status_t status;
	pjsip_transport *transport;

	int port = 5060;
	for(int i=0 ; i<1000 ; ++i)
	{
		port += i;		
		transport = allocate_udp_transport(sip_endpt, ipaddr, port);
		if (transport) {
			*allocated_port = port;
			return transport;
		}
	}

	return NULL;
}

pjsip_tpfactory *allocate_tcp_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int port) {
	printf("allocate_tcp_tpfactory ipaddr=%.*s port=%i\n", (int)ipaddr->slen, ipaddr->ptr, port);
	pj_status_t status;
	pjsip_tpfactory *tpfactory;
	pj_sockaddr local_addr;
	//pjsip_host_port a_name;

	int af;
        af = pj_AF_INET();
        pj_sockaddr_init(af, &local_addr, NULL, 0);

        pj_sockaddr_set_port(&local_addr, (pj_uint16_t)port);

        status = pj_sockaddr_set_str_addr(af, &local_addr, ipaddr);
        if (status != PJ_SUCCESS) {
		return NULL;
	}

    status = pjsip_tcp_transport_start2(sip_endpt, &local_addr.ipv4, NULL, 1, &tpfactory);
    if (status != PJ_SUCCESS) {
        printf("status=%i\n", status);
        return NULL;
    }

	return tpfactory;
}

pjsip_tpfactory *create_tcp_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int *allocated_port)
{
	//pj_status_t status;
	pjsip_tpfactory *tpfactory;

	int port = 6060;
	for(int i=0 ; i<1000 ; ++i)
	{
		port += i;		
		tpfactory = allocate_tcp_tpfactory(sip_endpt, ipaddr, port);
		if (tpfactory) {
			*allocated_port = port;
			return tpfactory;
		}
	}

	return NULL;
}

pjsip_tpfactory *allocate_tls_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int port) {
	addon_log(LOG_LEVEL_DEBUG, "allocate_tls_tpfactory ipaddr=%.*s port=%i\n", ipaddr->slen, ipaddr->ptr, port);
	pj_status_t status;
	pjsip_tpfactory *tpfactory;
	pj_sockaddr local_addr;
	//pjsip_host_port a_name;

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
		
        status = pjsip_tls_transport_start2(sip_endpt, &tls_opt, &local_addr, NULL, 1, &tpfactory);
        if (status != PJ_SUCCESS) {
		addon_log(LOG_LEVEL_DEBUG, "status=%i\n", status);
		return NULL;
        }

	return tpfactory;
}

pjsip_tpfactory *create_tls_tpfactory(pjsip_endpoint* sip_endpt, pj_str_t *ipaddr, int *allocated_port)
{
	//pj_status_t status;
	pjsip_tpfactory *tpfactory;

	int port = 6060;
	for(int i=0 ; i<1000 ; ++i)
	{
		port += i;		
		tpfactory = allocate_tls_tpfactory(sip_endpt, ipaddr, port);
		if (tpfactory) {
			*allocated_port = port;
			return tpfactory;
		}
	}

	return NULL;
}

//int pjw_transport_create(const char *sip_ipaddr, int port, pjsip_transport_type_e type, int *out_t_id, int *out_port)
int pjw_transport_create(const char *json, int *out_t_id, char *out_t_address, int *out_port)
{
	PJW_LOCK();
	clear_error();

    char *addr;
	pj_str_t address; // = pj_str((char*)sip_ipaddr);
    int port = 0;
    pjsip_transport_type_e type = PJSIP_TRANSPORT_UDP;

	pj_status_t status;
	Transport *t = NULL;
	long t_id;

    char buffer[MAX_JSON_INPUT];

    const char *valid_params[] = {"address", "port", "type", ""};

    Document document;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }
        
    if(!validate_params(document, valid_params)) {
        goto out;
    }

    // address
    if(!document.HasMember("address")) {
        set_error("Parameter address is required");
        goto out;
    }

    if(!document["address"].IsString()) {
        set_error("Parameter address must be a string");
        goto out;
    }
    addr = (char*)document["address"].GetString();
    address = pj_str((char*)addr);

    // port
    if(document.HasMember("port")) {
        if(!document["port"].IsInt()) {
            set_error("Parameter port must be an integer");
            goto out;
        }
        port = document["port"].GetInt();
    }

    // type
    if(document.HasMember("type")) {
        if(!document["type"].IsString()) {
            set_error("Parameter type must be a string");
            goto out;
        }
        const char *t = document["type"].GetString();
        if(stricmp(t, "UDP") == 0) {
            type = PJSIP_TRANSPORT_UDP;
        } else if(stricmp(t, "TCP") == 0) {
            type = PJSIP_TRANSPORT_TCP;
        } else if(stricmp(t, "TLS") == 0) {
            type = PJSIP_TRANSPORT_TLS;
        } else {
            set_error("Invalid type %s. It must be one of 'UDP' (default), 'TCP' or 'TLS'", type);
            goto out;
        }
    }

	if(type == PJSIP_TRANSPORT_UDP) {
		pjsip_transport *sip_transport = NULL;

		if(port != 0) {
			sip_transport = allocate_udp_transport(g_sip_endpt, &address, port);
		} else {
			sip_transport = create_udp_transport(g_sip_endpt, &address, &port);
		}

		if(!sip_transport)
		{
			set_error("Unable to start UDP transport");
			goto out;
		}

		t = new Transport;
		t->type = PJSIP_TRANSPORT_UDP;
		t->sip_transport = sip_transport;

		if(!g_transport_ids.add((long)t, t_id)){
			status = pjsip_udp_transport_pause(sip_transport,PJSIP_UDP_TRANSPORT_DESTROY_SOCKET);
            printf("pjsip_dup_transport_pause status=%i\n", status);
			set_error("Failed to allocate id");
		    goto out;	
		}

		g_SipTransportMap.insert(make_pair(sip_transport, t_id));
    } else if(type == PJSIP_TRANSPORT_TCP) {
		pjsip_tpfactory *tpfactory;
		//int af;


		if(port != 0) {
			tpfactory = allocate_tcp_tpfactory(g_sip_endpt, &address, port);
		} else {
			tpfactory = create_tcp_tpfactory(g_sip_endpt, &address, &port);
		}

		if(!tpfactory)
		{
			set_error("Unable to start TCP transport");
            goto out;
		}

		t = new Transport;
		t->type = PJSIP_TRANSPORT_TCP;
		t->tpfactory = tpfactory;

		if(!g_transport_ids.add((long)t, t_id)){
			status = (tpfactory->destroy)(tpfactory);

			set_error("Failed to allocate id");
            goto out;
		}

		g_TcpTransportId = t_id; 
	} else {
		pjsip_tpfactory *tpfactory;
		//int af;


		if(port != 0) {
			tpfactory = allocate_tls_tpfactory(g_sip_endpt, &address, port);
		} else {
			tpfactory = create_tls_tpfactory(g_sip_endpt, &address, &port);
		}

		if(!tpfactory)
		{
			set_error("Unable to start TLS transport");
            goto out;
		}

		t = new Transport;
		t->type = PJSIP_TRANSPORT_TLS;
		t->tpfactory = tpfactory;

		if(!g_transport_ids.add((long)t, t_id)) {
			status = (tpfactory->destroy)(tpfactory);

			set_error("Failed to allocate id");
            goto out;
		}

		g_TlsTransportId = t_id; 
	}

	t->id = t_id;

    /*
	if(g_PacketDumper) {
		g_PacketDumper->add_endpoint( inet_addr(addr), htons(port) );
	}
    */

	*out_t_id = t_id;
    strcpy(out_t_address, addr);
	*out_port = port;
out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}
	return 0;
}	

int pjw_transport_get_info(int t_id, char *out_sip_ipaddr, int *out_port)
{
	PJW_LOCK();
	clear_error();

	long val;
	Transport *t;

    int port;
    int len;

	if(!g_transport_ids.get(t_id, val)){
		set_error("Invalid transport_id");
		goto out;
	}
    t = (Transport*)val;

	port = t->sip_transport->local_name.port;
	len = t->sip_transport->local_name.host.slen;
	strncpy(out_sip_ipaddr, t->sip_transport->local_name.host.ptr, len);
	out_sip_ipaddr[len] = 0;
	*out_port = port;

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}
	return 0;
}

//int pjw_account_create(int t_id, const char *domain, const char *server, const char *username, const char *password, const char *additional_headers, const char *c_to_url, int expires, int *out_acc_id)
int pjw_account_create(int t_id, const char *json, int *out_acc_id)
{
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

    const char *valid_params[] = {"domain", "server", "username", "password", "to_url", "expires", "headers", ""};

	if(!g_transport_ids.get(t_id, val)){
		set_error("Invalid transport id");
        goto out;
	}
	t = (Transport*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }
    
    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_string_param(document, "domain", false, &domain)) {
        goto out;
    }

    if(!json_get_string_param(document, "server", false, &server)) {
        goto out;
    }

    if(!json_get_string_param(document, "username", false, &username)) {
        goto out;
    }

    if(!json_get_string_param(document, "password", false, &password)) {
        goto out;
    }

    if(!json_get_string_param(document, "to_uri", true, &c_to_uri)) {
        goto out;
    }

    if(!json_get_int_param(document, "expires", true, &expires)) {
        goto out;
    }

	status = pjsip_regc_create(g_sip_endpt, NULL, on_registration_status, &regc);
	if(status != PJ_SUCCESS)
	{
		set_error("pjsip_regc_create failed with status=%i", status);
		goto out;
	}

    if(!add_headers_for_account(regc, document)) {
        goto out;
	}

	if(!g_account_ids.add((long)regc, acc_id)){
		set_error("Failed to allocate id");
		goto out;
	}

	if(t->type == PJSIP_TRANSPORT_UDP) {
		local_port = t->sip_transport->local_name.port;
	 	len= t->sip_transport->local_name.host.slen;
		strncpy(local_addr, t->sip_transport->local_name.host.ptr, len);
	} else {
		local_port = t->tpfactory->addr_name.port;
	 	len= t->tpfactory->addr_name.host.slen;
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

	if(c_to_uri && c_to_uri[0]) {
        printf("c_to_uri=%s\n", c_to_uri);
		to_uri = pj_str((char*)c_to_uri);
	}

	len = sprintf(p, "sip:%s@%s:%u", username, local_addr, local_port);
    printf("contact=%s\n", p);
	contact = pj_str(p);
	p += len + 2;

	status = pjsip_regc_init(regc,
				 &server_uri,
				 &from_uri,
				 &to_uri,
				 1, &contact,
				 expires);
	if(status != PJ_SUCCESS)
	{
		status = pjsip_regc_destroy(regc); 
		//ToDo: log status
		set_error("pjsip_regc_init failed");
        goto out;
	} 

	pj_bzero(&cred, sizeof(cred));
	cred.realm = pj_str((char*)"*");
	cred.scheme = pj_str((char*)"digest");
	cred.username = pj_str((char*)username);
	cred.data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	cred.data = pj_str((char*)password);
	status = pjsip_regc_set_credentials(regc, 1, &cred);
	if(status != PJ_SUCCESS)
	{
		status = pjsip_regc_destroy(regc); 
		//ToDo: log status
		set_error("pjsip_regc_set_credentials failed");
        goto out;
	}
	
	pj_bzero(&sel, sizeof(sel));
	if(t->type == PJSIP_TRANSPORT_UDP) {
		sel.type = PJSIP_TPSELECTOR_TRANSPORT;
		sel.u.transport = t->sip_transport;
	} else {
		sel.type = PJSIP_TPSELECTOR_LISTENER;
		sel.u.listener = t->tpfactory;
	}

	status = pjsip_regc_set_transport(regc, &sel);
	if(status != PJ_SUCCESS)
	{
		status = pjsip_regc_destroy(regc); 
		//ToDo: log status
		set_error("pjsip_regc_set_transport failed");
		goto out;
	} 

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	*out_acc_id = acc_id;
	return 0;
}

//int pjw_account_register(long acc_id, pj_bool_t autoreg)
int pjw_account_register(long acc_id, const char *json)
{
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

	if(!g_account_ids.get(acc_id, val)){
		set_error("Invalid account_id");
        goto out;
	}
    regc = (pjsip_regc*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_bool_param(document, "auto_refresh", true, &auto_refresh)) {
        goto out;
    }

	status = pjsip_regc_register(regc, auto_refresh, &tdata);
	if(status != PJ_SUCCESS)
	{
		set_error("pjsip_regc_register failed");
		goto out;
	}

	status = pjsip_regc_send(regc, tdata);
	if(status != PJ_SUCCESS)
	{
		set_error("pjsip_regc_send failed");
		goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}

int pjw_account_unregister(long acc_id)
{
	PJW_LOCK();
	clear_error();

	long val;

	pjsip_regc *regc;

	pj_status_t status;
	pjsip_tx_data *tdata;

	if(!g_account_ids.get(acc_id, val)){
		set_error("Invalid account_id");
        goto out;
	}
	regc = (pjsip_regc*)val;

	status = pjsip_regc_unregister(regc, &tdata);
	if(status != PJ_SUCCESS)
	{
		set_error("pjsip_regc_unregister failed with status=%i", status);
        goto out;
	}

	status = pjsip_regc_send(regc, tdata);
	if(status != PJ_SUCCESS)
	{
		set_error("pjsip_regc_send failed with status=%i", status);
        goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}

//int pjw_call_respond(long call_id, int code, const char *reason, const char *additional_headers)
int pjw_call_respond(long call_id, const char *json)
{
	printf("pjw_call_respond: call_id=%lu json=%s\n", call_id, json);
	PJW_LOCK();
	clear_error();

	long val;

    int code;
    char *reason;

	pj_str_t r;// pj_str((char*)reason);

	pj_status_t status;

	pjsip_tx_data *tdata;

	Call *call;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"code", "reason", "headers", ""};

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	if(call->outgoing) {
		set_error("You cannot respond an outgoing call");
		goto out;
	}

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }
    
    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_int_param(document, "code", true, &code)) {
        goto out;
    }

    if(!json_get_string_param(document, "reason", true, &reason)) {
        goto out;
    }

	r = pj_str((char*)reason);

	if(call->initial_invite_rdata) {
		status = pjsip_inv_initial_answer(call->inv, call->initial_invite_rdata,
						code,
						&r,
						NULL,
						&tdata);
		if(status != PJ_SUCCESS) {
			set_error("pjsip_inv_initial_answer failed with status=%i", status);
            goto out;
		}

		status = pjsip_rx_data_free_cloned(call->initial_invite_rdata);
		if(status != PJ_SUCCESS) {
			set_error("pjsip_rx_data_free_cloned failed with status=%i", status);
            goto out;
		}
		call->initial_invite_rdata = 0;
	} else {
		status = pjsip_inv_answer(call->inv,
					  code,
					  &r,
					  NULL,
					  &tdata); 

		if(status != PJ_SUCCESS){
			set_error("pjsip_inv_answer failed with status=%i", status);
            goto out;
		}

		if(!add_headers(call->inv->dlg->pool, tdata, document)) {
			goto out;
		}
	}

	status = pjsip_inv_send_msg(call->inv, tdata);
	if(status != PJ_SUCCESS){
		set_error("pjsip_inv_send_msg failed with status=%i", status);
        goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]) {
		return -1;
	}
	return 0;
}

//int pjw_call_terminate(long call_id, int code, const char *reason, const char *additional_headers)
int pjw_call_terminate(long call_id, const char *json)
{
	PJW_LOCK();
	clear_error();

	long val;
	pjsip_tx_data *tdata;
	pj_status_t status;
    int code = 0;
    char *reason = (char*)"";
	pj_str_t r;// = pj_str((char*)reason);

    Call *call;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"code", "reason", "headers", ""};

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
		goto out;
	}
	call = (Call*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }
    
    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_int_param(document, "code", true, &code)) {
        goto out;
    }

    if(!json_get_string_param(document, "reason", true, &reason)) {
        goto out;
    }

	r = pj_str((char*)reason);

	status = pjsip_inv_end_session(call->inv,
					  code,
					  &r,
					  &tdata); 
	if(status != PJ_SUCCESS){
		set_error("pjsip_inv_end_session failed");
        goto out;
	}

	if(!tdata)
	{
		//if tdata was not set by pjsip_inv_end_session, it means we didn't receive any response yet (100 Trying) and we cannot send CANCEL in this situation. So we just can return here without calling pjsip_inv_send_msg.
        goto out;
	}

	if(!add_headers(call->inv->dlg->pool, tdata, document)) {
        goto out;
	}

	status = pjsip_inv_send_msg(call->inv, tdata);
	if(status != PJ_SUCCESS){
		set_error("pjsip_inv_send_msg failed with status=%i", status);
        goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}


//int pjw_call_create(long t_id, unsigned flags, const char *from_uri, const char *to_uri, const char *request_uri, const char *proxy_uri, const char *additional_headers, const char *realm, const char *username, const char *password, long *out_call_id, char *out_sip_call_id)
int pjw_call_create(long t_id, const char *json, long *out_call_id, char *out_sip_call_id)
{
	PJW_LOCK();
	clear_error();

	//int n;
	long val;
	Transport *t;
	//char *start;
	//char *end;
	char local_contact[400];
	//char *p;
	//int len;
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

    const char *valid_params[] = {"from_uri", "to_uri", "request_uri", "proxy_uri", "auth", "delayed_media", "headers", ""};

	if(!g_transport_ids.get(t_id, val)){
		set_error("Invalid transport_id");
		goto out;
	}
	t = (Transport*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }
        
    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "from_uri", false, &from_uri)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "to_uri", false, &to_uri)) {
        goto out;
    }

    request_uri = to_uri;
    if(!json_get_and_check_uri(document, "request_uri", true, &request_uri)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "proxy_uri", true, &proxy_uri)) {
        goto out;
    }

    if(document.HasMember("auth")) {
        if(!document["auth"].IsObject()) {
            set_error("Parameter auth must be an object");
            goto out;
        } else {
            const Value& auth = document["auth"];

            for (Value::ConstMemberIterator itr = auth.MemberBegin(); itr != auth.MemberEnd(); ++itr) {
                const char *name = itr->name.GetString();
                if(strcmp(name, "realm") == 0) {
                    if(!itr->value.IsString()) {
                        set_error("%s must be a string", itr->name.GetString());
                        goto out;
                    }
                    realm = (char*)itr->value.GetString();
                } else if(strcmp(name, "username") == 0) {
                    if(!itr->value.IsString()) {
                        set_error("%s must be a string", itr->name.GetString());
                        goto out;
                    }
                    username = (char*)itr->value.GetString();
                    contact_username = username;
                } else if(strcmp(name, "password") == 0) {
                    if(!itr->value.IsString()) {
                        set_error("%s must be a string", itr->name.GetString());
                        goto out;
                    }
                    password = (char*)itr->value.GetString();
                } else {
                    set_error("Unknown auth paramter %s", itr->name.GetString());
                    goto out;
                }
            }
        }
    }

    if(document.HasMember("delayed_media")) {
        if(!document["delayed_media"].IsBool()) {
            set_error("Parameter delayed_media must be a boolean");
            goto out;
        } else {
            if(document["delayed_media"].GetBool()) {
                flags = flags | CALL_FLAG_DELAYED_MEDIA;
            }
        }
    }

	if(t->type == PJSIP_TRANSPORT_UDP) {
		build_local_contact(local_contact, t->sip_transport, contact_username);
	} else {
		build_local_contact_from_tpfactory(local_contact, t->tpfactory, contact_username, t->type);
	}

	if(!dlg_create(&dlg, t, from_uri, to_uri, request_uri, realm, username, password, local_contact)) {
		goto out;
	}
	
	call_id = call_create(t, flags, dlg, proxy_uri, document); 
	if(call_id < 0) {
		goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	*out_call_id = call_id;
	strncpy(out_sip_call_id, dlg->call_id->id.ptr, dlg->call_id->id.slen);
	out_sip_call_id[dlg->call_id->id.slen] = 0;
	return 0;
}


bool dlg_set_transport(pjsip_transport *sip_transport, pjsip_dialog *dlg) {
	//Maybe we don't need to allocation sel from the pool
	pjsip_tpselector *sel = (pjsip_tpselector*)pj_pool_zalloc(dlg->pool, sizeof(pjsip_tpselector));
	//pjsip_tpselector sel;
	//pj_bzero(&sel, sizeof(sel));
	sel->type = PJSIP_TPSELECTOR_TRANSPORT;
	sel->u.transport = sip_transport;
	pj_status_t status = pjsip_dlg_set_transport(dlg, sel);
	if(status != PJ_SUCCESS)
	{
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("pjsip_dlg_set_transport failed");
		return false;
	}
	return true;
}

bool dlg_set_transport_by_t(Transport *t, pjsip_dialog *dlg) {
	//Maybe we don't need to allocation sel from the pool
	pjsip_tpselector *sel = (pjsip_tpselector*)pj_pool_zalloc(dlg->pool, sizeof(pjsip_tpselector));
	//pjsip_tpselector sel;
	//pj_bzero(&sel, sizeof(sel));
	if(t->type == PJSIP_TRANSPORT_UDP) {
		sel->type = PJSIP_TPSELECTOR_TRANSPORT;
		sel->u.transport = t->sip_transport;
	} else {
		sel->type = PJSIP_TPSELECTOR_LISTENER;
		sel->u.listener = t->tpfactory;
	}
	pj_status_t status = pjsip_dlg_set_transport(dlg, sel);
	if(status != PJ_SUCCESS)
	{
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("pjsip_dlg_set_transport failed");
		return false;
	}
	return true;
}


void build_local_contact(char *dest, pjsip_transport *t, const char *contact_username) {
	char *p = dest;
	int len;
	p += sprintf(p, "<sip:%s@", contact_username);
	len = t->local_name.host.slen;
	memcpy(p, t->local_name.host.ptr, len);
	p += len;
	if(t->local_name.port) {
		p += sprintf(p, ":%d",t->local_name.port);
	}
	if(t->key.type == PJSIP_TRANSPORT_UDP) {
		p += sprintf(p,">");
	} else if(t->key.type == PJSIP_TRANSPORT_TCP) {
		p += sprintf(p,";transport=tcp>");
	} else {
		p += sprintf(p,";transport=tls>");
	}
}

void build_local_contact_from_tpfactory(char *dest, pjsip_tpfactory *tpfactory, const char *contact_username, pjsip_transport_type_e type) {
	char *p = dest;
	int len;
	p += sprintf(p, "<sip:%s@", contact_username);
	len = tpfactory->addr_name.host.slen;
	memcpy(p, tpfactory->addr_name.host.ptr, len);
	p += len;
	if(tpfactory->addr_name.port) {
		p += sprintf(p, ":%d",tpfactory->addr_name.port);
	}
    if(type == PJSIP_TRANSPORT_TCP) {
	    p += sprintf(p,";transport=tcp>");
    } else {
	    p += sprintf(p,";transport=tls>");
    }
}


bool set_proxy(pjsip_dialog *dlg, const char *proxy_uri) {
	//Very important: although this function only requires dlg and the proxy_uri, it cannot be called before the function that creates the initial request is called. If we call pjsip_inv_create_uac after this function is called, assertion failure will happen.  This is the reason why we didn't put the call to this function inside function dlg_create.

	pj_status_t status;
	
	if(!proxy_uri || !proxy_uri[0]) return true; //nothing to do proxy_uri was not set
 
	//proxy_uri must contain ";lr". 
	char *buf = (char*)pj_pool_zalloc(dlg->pool, 500);
	//char buf[500];
	strcpy(buf,proxy_uri);
	if(!strstr(proxy_uri,";lr")){
		strcat(buf,";lr");
	}	
	//addon_log(LOG_LEVEL_DEBUG, ">>%s<<\n",buf);

//	pjsip_route_hdr route_set;
//	pjsip_route_hdr *route;
//	const pj_str_t hname = { "Route", 5 };

	pj_list_init(&route_set);

	route = (pjsip_route_hdr*)pjsip_parse_hdr( dlg->pool, &hname, 
				 (char*)buf, strlen(buf),
				 NULL);
	if(!route){
		status = pjsip_dlg_terminate(dlg); //ToDo:
        printf("pjsip_dlg_terminate status=%i\n", status);
		set_error("pjsip_parse_hdr failed");
		return false;
	}

	pj_list_push_back(&route_set, route);

	pjsip_dlg_set_route_set(dlg, &route_set);

	return true;
}



bool dlg_create(pjsip_dialog **dlg, Transport *transport, const char *from_uri, const char *to_uri, const char *request_uri, const char *realm, const char *username, const char *password, const char *local_contact) {
	//obs: local contact must exists in the stack somewhere. It cannot be allocated dynamically because we don't have a dlg nor a dlg->pool yet.

	pj_status_t status;
	pjsip_dialog *p_dlg;

	pj_str_t from = pj_str((char*)from_uri);
	pj_str_t to = pj_str((char*)to_uri);
	pj_str_t request = pj_str((char*)request_uri);

	pj_str_t contact = pj_str((char*)local_contact);

	status = pjsip_dlg_create_uac(pjsip_ua_instance(),
				&from,
				&contact,
				&to,
				&request,
				&p_dlg);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_dlg_create_uac failed");
		return false;
	}

	if(realm && realm[0]){	
		pjsip_cred_info cred[1];
		cred[0].scheme = pj_str((char*)"digest");
		cred[0].realm = pj_str((char*)realm);
		cred[0].username = pj_str((char*)username);
		cred[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
		cred[0].data = pj_str((char*)password);
		status = pjsip_auth_clt_set_credentials(&p_dlg->auth_sess, 1, cred);
		if(status != PJ_SUCCESS) {
			status = pjsip_dlg_terminate(p_dlg); //ToDo:
			set_error("pjsip_auth_clt_set_credentials failed");
			return false;
		}
	}

	*dlg = p_dlg;
	return true;
}


int call_create(Transport *t, unsigned flags, pjsip_dialog *dlg, const char *proxy_uri, Document &document)
{
	pjsip_inv_session *inv;
	//in_addr addr;
	//addr.s_addr = t->local_addr.ipv4.sin_addr.s_addr;
	//pj_str_t str_addr = pj_str( inet_ntoa(addr) );
	in_addr in_a;
	if(t->type == PJSIP_TRANSPORT_UDP) {
		in_a = (in_addr&)t->sip_transport->local_addr.ipv4.sin_addr.s_addr;
	} else {
		in_a = (in_addr&)t->tpfactory->local_addr.ipv4.sin_addr.s_addr;
	}

	pj_str_t str_addr = pj_str( inet_ntoa(in_a) );
	pjmedia_transport *med_transport = create_media_transport(&str_addr);

	pj_status_t status;

	if(!med_transport)
	{
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("create_media_transport failed");
		return -1;
	}

	pjmedia_transport_info med_tpinfo;
	pjmedia_transport_info_init(&med_tpinfo);
	pjmedia_transport_get_info(med_transport, &med_tpinfo);
	
	pjmedia_sdp_session *sdp = 0;

	if(!(flags & CALL_FLAG_DELAYED_MEDIA)) {
		status = pjmedia_endpt_create_sdp( g_med_endpt, 
				dlg->pool, 
				1,
				&med_tpinfo.sock_info,
				&sdp);
		if(status != PJ_SUCCESS) {
			close_media_transport(med_transport);
			status = pjsip_dlg_terminate(dlg); //ToDo:
			set_error("pjmedia_endpt_create_sdp failed");
			return -1; 
		}
	}

	status = pjsip_inv_create_uac(dlg, sdp, 0, &inv);
	if(status != PJ_SUCCESS) {
		close_media_transport(med_transport);
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("pjsip_inv_create_uac failed");
		return -1;
	}

	if(!set_proxy(dlg, proxy_uri)) {
		return -1;
	}

	Call *call = (Call*)pj_pool_alloc(dlg->pool, sizeof(Call));
	pj_bzero(call, sizeof(Call));


	call->inv = inv;
	call->med_transport = med_transport;

	long call_id;
	if(!g_call_ids.add((long)call, call_id)){
		close_media_transport(med_transport);
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("Failed to allocate id");
		return -1;
	}
	call->transport = t;
	call->id = call_id;
	pjsip_tx_data *tdata;
	status = pjsip_inv_invite(inv, &tdata);
	if(status != PJ_SUCCESS) {
		g_call_ids.remove(call_id, (long &)call);
		close_media_transport(med_transport);
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("pjsip_inv_invite failed");
		return -1;
	}

	

	if(!add_headers(dlg->pool, tdata, document)) {
		g_call_ids.remove(call_id, (long &)call);
		close_media_transport(med_transport); //Todo:
		status = pjsip_dlg_terminate(dlg); //ToDo:	
		return -1;
	}

	if(!dlg_set_transport_by_t(t, dlg)){
		return -1;
	}
	addon_log(LOG_LEVEL_DEBUG, "inv=%x tdata=%x\n",inv,tdata);

	status = pjsip_inv_send_msg(inv, tdata);
	addon_log(LOG_LEVEL_DEBUG, "status=%d\n",status);
	pj_perror(0, "", status, "");
	if(status != PJ_SUCCESS) {
		g_call_ids.remove(call_id, (long &)call);
		close_media_transport(med_transport); //Todo:
		//The below code cannot be called here it will cause seg fault
		//status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("pjsip_inv_send_msg failed");
		return -1;
	}

	//Without this, on_rx_response will not be called
	status = pjsip_dlg_add_usage(dlg, &mod_tester, call);
	if(status != PJ_SUCCESS) {
		g_call_ids.remove(call_id, (long &)call);
		close_media_transport(med_transport); //Todo:
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("pjsip_dlg_add_usage failed");
		return  -1;
	}
	//addon_log(LOG_LEVEL_DEBUG, "pjsip_dlg_add_usage OK\n");

	call->outgoing = true;

	return call_id;
}

//int pjw_call_send_dtmf(long call_id, const char *digits, int mode)
int pjw_call_send_dtmf(long call_id, const char *json)
{
#define ON_DURATION 200
#define OFF_DURATION 50

#define MAX_LENGTH 31 // pjsip allows for 31 digits (inband allows for 32 digits)

	PJW_LOCK();
	clear_error();

	long val;
    char *digits;
    int mode = 0;;

    int len;

	char adjusted_digits[MAX_LENGTH+1]; // use the greater size

	pj_str_t ds;
	pj_status_t status;

    Call *call;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"digits", "mode", ""};


    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
		goto out;
	}
	call = (Call*)val;

    if(!json_get_string_param(document, "digits", false, &digits)) {
        goto out;
    }

    if(!json_get_int_param(document, "mode", false, &mode)) {
        goto out;
    }

    if(mode != DTMF_MODE_RFC2833 && mode != DTMF_MODE_INBAND) {
        set_error("Invalid DTMF mode. It must be eiter 0 (RFC2833) or 1 (INBAND).");
        goto out;
    }

	len = strlen(digits);

	if(len > MAX_LENGTH) {
		set_error("DTMF string too long");
        goto out;
	}

	if(!call->med_stream)
	{
		set_error("Unable to send DTMF: Media not ready");
        goto out;
	}

	for(int i=0; i<len ; ++i) {
		if( !(digits[i] >= '0' && digits[i] <= '9') &&
		    !(digits[i] >= 'a' && digits[i] <= 'f') &&
		    !(digits[i] >= 'A' && digits[i] <= 'F') &&
		    !(digits[i] == '*') &&
		    !(digits[i] == '#') )
		{
			set_error("Invalid character");
            goto out;
		}
		char d = digits[i];	
		if(d == 'e' || d == 'E') {
			adjusted_digits[i] = '*';
		} else if(d == 'f' || d == 'F') {
			adjusted_digits[i] = '#';
		} else {
			adjusted_digits[i] = (char)tolower(d);
		}
	}
	adjusted_digits[len] = 0;
	//addon_log(LOG_LEVEL_DEBUG, ">>%s<<\n", adjusted_digits);

	ds = pj_str((char*)adjusted_digits);

	if(DTMF_MODE_RFC2833 == mode) {
		status = pjmedia_stream_dial_dtmf(call->med_stream, &ds);
		if(status != PJ_SUCCESS)
		{
			set_error("pjmedia_stream_dial_dtmf failed.");
            goto out;
		}
	} else {
		if(!prepare_tonegen(call)) {
			set_error("prepare_tonegen failed.");
            goto out;
		}

		for(int i=0; i<len ; ++i) {
			pjmedia_tone_digit tone;
			tone.digit = adjusted_digits[i];
			tone.on_msec = ON_DURATION;
			tone.off_msec = OFF_DURATION;
			tone.volume = 0;
			status = chainlink_tonegen_play_digits((pjmedia_port*)call->tonegen, 1, &tone, 0);
			if(status != PJ_SUCCESS) {
				set_error("chainlink_tonegen_play_digits failed.");
                goto out;
			}
		}
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}		

//int pjw_call_reinvite(long call_id, int hold, unsigned flags)
int pjw_call_reinvite(long call_id, const char *json)
{
	PJW_LOCK();
	clear_error();

    bool hold = false;
    unsigned flags;

	long val;
    Call *call;

	pj_status_t status;

	const pjmedia_sdp_session *old_sdp = NULL;

	pjsip_tx_data *tdata;
	pjmedia_sdp_session *sdp = 0;

    char buffer[MAX_JSON_INPUT];

    Document document;
    
    const char *valid_params[] = {"hold", "delayed_media", ""};

	if(!g_call_ids.get(call_id, val)){
	    set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_bool_param(document, "hold", true, &hold)) {
        goto out;
    }

    if(document.HasMember("delayed_media")) {
        if(!document["delayed_media"].IsBool()) {
            set_error("Parameter delayed_media must be a boolean");
            goto out;
        } else {
            if(document["delayed_media"].GetBool()) {
                flags = flags | CALL_FLAG_DELAYED_MEDIA;
            }
        }
    }

	call->local_hold = hold;

	if(!(flags & CALL_FLAG_DELAYED_MEDIA)) {
		pjmedia_transport_info tpinfo;
		pjmedia_transport_info_init(&tpinfo);
		pjmedia_transport_get_info(call->med_transport,&tpinfo);

		status = pjmedia_endpt_create_sdp(g_med_endpt,
						call->inv->pool,
						1,
						&tpinfo.sock_info,
						&sdp);
		if(status != PJ_SUCCESS){
			set_error("pjmedia_endpt_create_sdp failed");
            goto out;
		}
	
		pjmedia_sdp_attr *attr;

		pjmedia_sdp_media_remove_all_attr(sdp->media[0], "sendrecv");
		pjmedia_sdp_media_remove_all_attr(sdp->media[0], "sendonly");
		pjmedia_sdp_media_remove_all_attr(sdp->media[0], "recvonly");
		pjmedia_sdp_media_remove_all_attr(sdp->media[0], "inactive");

		if(call->local_hold){
			if(call->remote_hold) {
				attr = pjmedia_sdp_attr_create(call->inv->pool, "inactive", NULL);
			} else {
				attr = pjmedia_sdp_attr_create(call->inv->pool, "sendonly", NULL);
			}
		} else if(call->remote_hold) {
			attr = pjmedia_sdp_attr_create(call->inv->pool, "recvonly", NULL);
		} else {
			attr = pjmedia_sdp_attr_create(call->inv->pool, "sendrecv", NULL);
		}

		pjmedia_sdp_media_add_attr(sdp->media[0], attr);

		old_sdp = NULL;

		status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &old_sdp);
		if (status != PJ_SUCCESS || old_sdp == NULL){
			set_error("pjmedia_sdp_neg_get_active failed");
            goto out;
		}

		sdp->origin.version = old_sdp->origin.version + 1;
	}

	status = pjsip_inv_reinvite(call->inv, NULL, sdp, &tdata);
	if(status != PJ_SUCCESS){
		set_error("pjsip_inv_reinvite failed");
        goto out;
	}

	status = pjsip_inv_send_msg(call->inv, tdata);
	if(status != PJ_SUCCESS){
		set_error("pjsip_inv_send_msg failed");
        goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}

//To send INFO and other requests inside dialog
//int pjw_call_send_request(long call_id, const char *method_name, const char *additional_headers, const char *body, const char *ct_type, const char *ct_subtype)
int pjw_call_send_request(long call_id, const char *json)
{
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
    
    const char *valid_params[] = {"method", "body", "ct_type", "ct_subtype", "headers", ""};

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
		goto out;
	}
	call = (Call*)val;

    if(!json_get_string_param(document, "method", false, &method)) {
        goto out;
    }

    if(!json_get_string_param(document, "body", true, &body)) {
        goto out;
    }

    if(!json_get_string_param(document, "ct_type", true, &ct_type)) {
        goto out;
    }

    if(!json_get_string_param(document, "ct_subtype", true, &ct_subtype)) {
        goto out;
    }

	if(strcmp(method,"INVITE")==0 || strcmp(method,"UPDATE")==0 || strcmp(method,"PRACK")==0 || strcmp(method,"BYE")==0) {
		set_error("Invalid method");
		goto out;
	}

	if(body) {
		if(!ct_type || !ct_subtype) {
			set_error("If a body is specified, you must pass ct_type (Content-Type type) and ct_subtype (Content-Type subtype)");
			goto out;
		}
	}

	s_method = pj_str((char*)method);	

	pjsip_method_init_np(&meth, &s_method);

	status = pjsip_dlg_create_request(call->inv->dlg, &meth, -1, &tdata);
	if (status != PJ_SUCCESS) {
		set_error("pjsip_dlg_create_request failed with status=%i", status);
		goto out;
	}

	if(!add_headers(call->inv->dlg->pool, tdata, document)) {
		goto out;
	}

	if(body && body[0]) {
		s_ct_type = pj_str((char*)ct_type);
		s_ct_subtype = pj_str((char*)ct_subtype);
		s_body = pj_str((char*)body);

		msg_body = pjsip_msg_body_create(tdata->pool, &s_ct_type, &s_ct_subtype, &s_body);

		if(!msg_body) {
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
	if(pjw_errorstring[0]) {
		return -1;
	}

	return 0;
}

//int pjw_call_start_record_wav(long call_id, const char *file)
int pjw_call_start_record_wav(long call_id, const char *json)
{
	PJW_LOCK();
    clear_error();

	long val;
    Call *call;
	pj_status_t status;
	pjmedia_port *stream_port;

    char *file;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"file", ""};

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	if(!call->med_stream)
	{
		set_error("Media not ready");
		goto out;
	}

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_string_param(document, "file", false, &file)) {
        goto out;
    }
 
    if(!file[0]) {
        set_error("file cannot be blank string");
        goto out;
    }
 
	status = pjmedia_stream_get_port(call->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS)
	{
		set_error("pjmedia_stream_get_port failed with status=%i", status);
        goto out;
	}

	if(!prepare_wav_writer(call, file)) {
		set_error("prepare_wav_writer failed");
        goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}


//int pjw_call_start_play_wav(long call_id, const char *file)
int pjw_call_start_play_wav(long call_id, const char *json)
{
	PJW_LOCK();
    clear_error();

	long val;
    Call *call;
	pj_status_t status;
	pjmedia_port *stream_port;

    char *file;

    char buffer[MAX_JSON_INPUT];

    Document document;
    
    const char *valid_params[] = {"file", ""};

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	if(!call->med_stream)
	{
		set_error("Media not ready");
		goto out;
	}

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_string_param(document, "file", false, &file)) {
        goto out;
    }
  
    if(!file[0]) {
        set_error("file cannot be blank string");
        goto out;
    }

	status = pjmedia_stream_get_port(call->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS)
	{
		set_error("pjmedia_stream_get_port failed with status=%i", status);
        goto out;
	}

	if(!prepare_wav_player(call, file)){
		set_error("prepare_wav_player failed");
        goto out; 
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}

int pjw_call_stop_play_wav(long call_id)
{
	PJW_LOCK();
    clear_error();

    Call *call;

	pjmedia_port *stream_port;
	pj_status_t status;

	long val;

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	status = pjmedia_stream_get_port(call->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS) {
		set_error("pjmedia_stream_get_port failed with status=%i", status);
        goto out;
	}	

	if(!prepare_wire(call->inv->pool, &call->wav_player, PJMEDIA_PIA_SRATE(&stream_port->info), PJMEDIA_PIA_CCNT(&stream_port->info), PJMEDIA_PIA_SPF(&stream_port->info), PJMEDIA_PIA_BITS(&stream_port->info))) {
		set_error("prepare_wire failed.");
        goto out;
	}

	connect_media_ports(call);
	
out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}

int pjw_call_stop_record_wav(long call_id)
{
	PJW_LOCK();
    clear_error();

	long val;
	Call *call = (Call*)val;
	pjmedia_port *stream_port;
	pj_status_t status;

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	status = pjmedia_stream_get_port(call->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS) {
		set_error("pjmedia_stream_get_port failed.");
        goto out;
	}	

	if(!prepare_wire(call->inv->pool, &call->wav_writer, PJMEDIA_PIA_SRATE(&stream_port->info), PJMEDIA_PIA_CCNT(&stream_port->info), PJMEDIA_PIA_SPF(&stream_port->info), PJMEDIA_PIA_BITS(&stream_port->info))) {
		set_error("prepare_wire failed.");
        goto out;
	}

	connect_media_ports(call);
	
out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}

//int pjw_call_start_fax(long call_id, bool is_sender, const char *file)
int pjw_call_start_fax(long call_id, const char *json)
{
	PJW_LOCK();
    clear_error();

	long val;
    Call *call;
	pj_status_t status;
	pjmedia_port *stream_port;

    bool is_sender;
    char *file;
    unsigned flags = 0;
    bool flag;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"is_sender", "file", "transmit_on_idle", ""};

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	if(!call->med_stream)
	{
		set_error("Media not ready");
		goto out;
	}

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_bool_param(document, "is_sender", false, &is_sender)) {
        goto out;
    }

    if(!json_get_string_param(document, "file", false, &file)) {
        goto out;
    }
  
    if(!file[0]) {
        set_error("file cannot be blank string");
        goto out;
    }


    flag = false;
    if(!json_get_bool_param(document, "transmit_on_idle", true, &flag)) {
        goto out;
    } else {
        if(flag) flags |= FAX_FLAG_TRANSMIT_ON_IDLE;
    }
    

	status = pjmedia_stream_get_port(call->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS)
	{
		set_error("pjmedia_stream_get_port failed with status=%i", status);
        goto out;
	}

	if(!prepare_fax(call, is_sender, file, flags)){
		set_error("prepare_fax failed");
        goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}


int pjw_call_stop_fax(long call_id)
{
	PJW_LOCK();
    clear_error();

	long val;
    Call *call;

	pjmedia_port *stream_port;
	pj_status_t status;

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}

	call = (Call*)val;

	status = pjmedia_stream_get_port(call->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS) {
		set_error("pjmedia_stream_get_port failed.");
        goto out;
	}	

	if(!prepare_wire(call->inv->pool, &call->fax, PJMEDIA_PIA_SRATE(&stream_port->info), PJMEDIA_PIA_CCNT(&stream_port->info), PJMEDIA_PIA_SPF(&stream_port->info), PJMEDIA_PIA_BITS(&stream_port->info))) {
		set_error("prepare_wire failed.");
        goto out;
	}

	connect_media_ports(call);

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}


int pjw_call_get_stream_stat(long call_id, char *out_stats){
	PJW_LOCK();
    clear_error();

	long val;
    Call *call;

	pj_status_t status;
	pjmedia_rtcp_stat stat;
	pjmedia_stream_info stream_info;

	ostringstream oss;

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
        goto out;
	}
	call = (Call*)val;

	if(!call->med_stream){
		set_error("Could not get stream stats. No media session");
        goto out;
	}

	status = pjmedia_stream_get_stat(call->med_stream, &stat);
	if(status != PJ_SUCCESS){
		set_error("Could not get stream stats. Call to pjmedia_stream_get_stream_stat failed with status=%i", status);
        goto out;
	}

	status = pjmedia_stream_get_info(call->med_stream, &stream_info);
	if(status != PJ_SUCCESS){
		set_error("Could not get stream info. Call to pjmedia_stream_get_info failed with status=%i", status);
        goto out;
	}

	build_stream_stat(oss, &stat, &stream_info);
	strcpy(out_stats, oss.str().c_str());

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}

	return 0;
}


static void on_media_update( pjsip_inv_session *inv, pj_status_t status){
	addon_log(LOG_LEVEL_DEBUG, "on_media_update\n");
	char evt[256];

	if(g_shutting_down) return;

	Call *call = (Call*)inv->dlg->mod_data[mod_tester.id];

	long call_id;
	if( !g_call_ids.get_id((long)call, call_id) ){
		addon_log(LOG_LEVEL_DEBUG, "on_media_update: Failed to get call_id. Event will not be notified.\n");	
		return;
	}

	pjmedia_stream_info stream_info;
	const pjmedia_sdp_session *local_sdp;
	const pjmedia_sdp_session *remote_sdp;

	ostringstream oss;

	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "negotiation_failed", "", "");
		dispatch_event(evt);
		return;
	}

	if(call->master_port){
		status = pjmedia_master_port_stop(call->master_port);
		if(status != PJ_SUCCESS){
			make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
			dispatch_event(evt);
			return;
		}
	}

	status = pjmedia_sdp_neg_get_active_local(inv->neg, &local_sdp);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}		

	status = pjmedia_sdp_neg_get_active_remote(inv->neg, &remote_sdp);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	status = pjmedia_stream_info_from_sdp(
					&stream_info,
					inv->dlg->pool,
					g_med_endpt,
					local_sdp,
					remote_sdp,
					0);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	if(call->med_stream){
		status = pjmedia_stream_destroy(call->med_stream);
		if(status != PJ_SUCCESS){
			make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
			dispatch_event(evt);
			return;
		}
	}

	status = pjmedia_stream_create(
				g_med_endpt,
				inv->dlg->pool,
				&stream_info,
				call->med_transport, 
				NULL,
				&call->med_stream);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	status = pjmedia_stream_start(call->med_stream);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	status = pjmedia_stream_set_dtmf_callback(call->med_stream,
						&on_dtmf,
						call);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

    /* Start the UDP media transport */
    pjmedia_transport_media_start(call->med_transport, 0, 0, 0, 0);

	pjmedia_port *stream_port;
	status = pjmedia_stream_get_port(call->med_stream, &stream_port);
	if(status != PJ_SUCCESS){
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	if(!init_media_ports(call,
				PJMEDIA_PIA_SRATE(&stream_port->info),
				PJMEDIA_PIA_CCNT(&stream_port->info),
				PJMEDIA_PIA_SPF(&stream_port->info),
				PJMEDIA_PIA_BITS(&stream_port->info))) {
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	if(!set_ports(call, stream_port, (pjmedia_port*)call->dtmfdet))
	{
		make_evt_media_status(evt, sizeof(evt), call_id, "setup_failed", "", "");
		dispatch_event(evt);
		return;
	}

	int local_media_mode = get_media_mode(local_sdp->media[0]->attr, local_sdp->media[0]->attr_count); 
	int remote_media_mode = get_media_mode(remote_sdp->media[0]->attr, remote_sdp->media[0]->attr_count); 

	char *local_mode = get_media_mode_str(local_media_mode);
	char *remote_mode = get_media_mode_str(remote_media_mode);

	make_evt_media_status(evt, sizeof(evt), call_id, "setup_ok", local_mode, remote_mode);
	dispatch_event(evt);
}

static pj_bool_t set_ports(Call *call, pjmedia_port *stream_port, pjmedia_port *media_port)
{
	pj_status_t status;

	if(!call->master_port)
	{
		status = pjmedia_master_port_create(call->inv->pool,
						stream_port,
						media_port,
						0,
						&call->master_port);
		if(status != PJ_SUCCESS){
			return PJ_FALSE;
		}

		status = pjmedia_master_port_start(call->master_port);
		if(status != PJ_SUCCESS){
			return PJ_FALSE;
		}

		call->media_port = media_port;
		return PJ_TRUE;
	}

	status = pjmedia_master_port_stop(call->master_port);
	if(status != PJ_SUCCESS){
		return PJ_FALSE;
	}

	status = pjmedia_master_port_set_uport(call->master_port,
					stream_port);
	if(status != PJ_SUCCESS){
		return PJ_FALSE;
	}

	if(call->media_port)
	{
		if(call->media_port != media_port){
			status = pjmedia_port_destroy(call->media_port);
			if(status != PJ_SUCCESS){
				return PJ_FALSE;
			}
		}
		call->media_port = NULL;
	}

	status = pjmedia_master_port_set_dport(call->master_port,
					media_port);
	if(status != PJ_SUCCESS){
		return PJ_FALSE;
	}

	call->media_port = media_port;
	
	status = pjmedia_master_port_start(call->master_port);
	if(status != PJ_SUCCESS){
		return PJ_FALSE;
	}

	return PJ_TRUE;
}

static void on_state_changed( pjsip_inv_session *inv, 
				   pjsip_event *e){
	addon_log(LOG_LEVEL_DEBUG, "on_state_changed\n");

	// The below is just to document know-how for future improvements
	/*
	addon_log(LOG_LEVEL_DEBUG, "on_state_changed e->type=%i\n", e->type);
	if(e->type == PJSIP_EVENT_TSX_STATE && e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		// Read http://trac.pjsip.org/repos/wiki/SIP_Message_Buffer_Event
		addon_log(LOG_LEVEL_DEBUG, "Msg=%s\n", e->body.tsx_state.src.rdata->msg_info.msg_buf);
	}
	*/

	if(g_shutting_down) return;

	if(PJSIP_INV_STATE_DISCONNECTED == inv->state)
	{
		Call *call = (Call*)inv->dlg->mod_data[mod_tester.id];
		//addon_log(LOG_LEVEL_DEBUG, "call will terminate call=%x\n",call);
		pj_status_t status;

		long call_id;
		if( !g_call_ids.get_id((long)call, call_id) ){
			addon_log(LOG_LEVEL_DEBUG, "on_state_changed: Failed to get call_id. Event will not be notified.\n");	
			return;
		}

		if(call->master_port)
		{
			status = pjmedia_master_port_stop(call->master_port);
			if(status != PJ_SUCCESS)
			{
				addon_log(LOG_LEVEL_DEBUG, "pjmedia_master_port_stop failed\n");
			}

			status = pjmedia_master_port_destroy(call->master_port, PJ_FALSE);
			if(status != PJ_SUCCESS)
			{
				addon_log(LOG_LEVEL_DEBUG, "pjmedia_master_port_destroy failed\n");
			}
		}

		if(call->media_port)
		{
			status = pjmedia_port_destroy(call->media_port);
			if(status != PJ_SUCCESS)
			{
				addon_log(LOG_LEVEL_DEBUG, "pjmedia_port_destroy failed\n");
			}
		}

		if(call->med_stream)
		{
			status = pjmedia_stream_destroy(call->med_stream);
			if(status != PJ_SUCCESS)
			{
				addon_log(LOG_LEVEL_DEBUG, "pjmedia_stream_destroy failed\n");
			}
		}

		if(call->med_transport)
		{
			close_media_transport(call->med_transport);
		}

		long val;
		if(!g_call_ids.remove(call_id, val))
		{
			addon_log(LOG_LEVEL_DEBUG, "g_call_ids.remove failed\n");
		}
		
		Pair_Call_CallId pcc;
		pcc.pCall = call;
		pcc.id = call_id;
		g_LastCalls.push_back(pcc);	

		//delete call->last_responses;
	
		/*
		ostringstream oss;
		oss << "event=termination" << EVT_DATA_SEP << "call=" << call_id;
		dispatch_event(oss.str().c_str());
		*/

		char evt[2048];
		int sip_msg_len = 0;
		char *sip_msg = (char*)"";
		if(e->type == PJSIP_EVENT_TSX_STATE) {
			sip_msg_len = e->body.rx_msg.rdata->msg_info.len;
			sip_msg = e->body.rx_msg.rdata->msg_info.msg_buf;
		}
		make_evt_call_ended(evt, sizeof(evt), call_id, sip_msg_len, sip_msg);
		dispatch_event(evt);
	}
}

static void on_forked(pjsip_inv_session *inv, pjsip_event *e){
	if(g_shutting_down) return;
}

static pjmedia_transport *create_media_transport(const pj_str_t *addr)
{
	pjmedia_transport *med_transport;
	pj_status_t status;
	for(int i=0; i<1000 ; ++i)
	{
		int port = 10000 + (i*2);
		status = pjmedia_transport_udp_create3(g_med_endpt, AF, 
					NULL, 
					addr,
					port, 0, &med_transport);
		if( status == PJ_SUCCESS ) {
			pjmedia_transport_info tpinfo;
			pjmedia_transport_info_init(&tpinfo);
			status = pjmedia_transport_get_info(med_transport, &tpinfo);
            /*
			if( status == PJ_SUCCESS ) {
				if(g_PacketDumper){
					g_PacketDumper->add_endpoint( tpinfo.sock_info.rtp_addr_name.ipv4.sin_addr.s_addr, tpinfo.sock_info.rtp_addr_name.ipv4.sin_port );
					g_PacketDumper->add_endpoint( tpinfo.sock_info.rtcp_addr_name.ipv4.sin_addr.s_addr, tpinfo.sock_info.rtcp_addr_name.ipv4.sin_port );
				}
			}
            */
			return med_transport;
		}
	}
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

	memset(&user_cb, 0, sizeof(user_cb));
	user_cb.on_evsub_state = server_on_evsub_state;
	user_cb.on_rx_refresh = server_on_evsub_rx_refresh;

	build_local_contact(local_contact, rdata->tp_info.transport, "sip-tester");
	pj_str_t url = pj_str(local_contact);

	status = pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(),
					rdata,
					&url,
					&dlg);
	if(status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "error p1");
		dispatch_event(evt);
		goto out;
	}

	status = pjsip_evsub_create_uas(dlg,
					&user_cb,
					rdata,
					0,
					&evsub);
	if(status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "error p2");
		dispatch_event(evt);
		goto out;
	} 

	subscriber = (Subscriber*)pj_pool_zalloc(dlg->pool, sizeof(Subscriber));
	if(!g_subscriber_ids.add((long)subscriber, subscriber_id)){
		make_evt_internal_error(evt, sizeof(evt), "error p3");
		dispatch_event(evt);
		goto out;	
	}
	subscriber->id = subscriber_id;
	subscriber->evsub = evsub;
	subscriber->dlg = dlg;

	pjsip_evsub_set_mod_data(evsub, mod_tester.id, subscriber);

	status = pjsip_evsub_accept(evsub, rdata, 200, NULL);
	if(status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "error p5");
		dispatch_event(evt);
		goto out;
	}

	status = pjsip_evsub_notify(evsub, (pjsip_evsub_state)PJSIP_EVSUB_STATE_ACTIVE, NULL, NULL, &tdata);
	if(status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "error p6");
		dispatch_event(evt);
		goto out;
	}

	status = pjsip_evsub_send_request(evsub,tdata);
	if(status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "error p7");
		dispatch_event(evt);
		goto out;
	}

out:
	if(status != PJ_SUCCESS) {
		//pj_str_t s_reason = pj_str(pjw_errorstring);
		if(dlg) {
			status = pjsip_dlg_create_response(dlg, rdata, 500, NULL, &tdata);
			if(status == PJ_SUCCESS) {
				pjsip_dlg_send_response(dlg, pjsip_rdata_get_tsx(rdata), tdata);		
			}
		}else {
			pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, NULL, NULL, NULL);
		}
	} else { 
		make_evt_request(evt, sizeof(evt), "subscriber", subscriber_id, rdata->msg_info.len, rdata->msg_info.msg_buf);
		dispatch_event(evt);
	}
}

static pj_bool_t on_rx_request( pjsip_rx_data *rdata ){
	char evt[2048];

	pj_str_t *method_name = &rdata->msg_info.msg->line.req.method.name;
	addon_log(LOG_LEVEL_DEBUG, "on_rx_request %.*s\n", method_name->slen, method_name->ptr);
	if(g_shutting_down) return PJ_TRUE;

	pj_status_t status;
	pj_str_t reason;

	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);

	addon_log(LOG_LEVEL_DEBUG, "dlg=%x\n", dlg);
	
	if(dlg){
		if(pj_strcmp2(&rdata->msg_info.msg->line.req.method.name, "REFER") != 0){
			return PJ_TRUE;
		}
	}

	//Just for future reference. The code below prints all headers in the request
	/*
	pjsip_hdr *hdr;
	pjsip_hdr *hdr_list = &rdata->msg_info.msg->hdr;
	for(hdr=hdr_list->next; hdr!=hdr_list ; hdr=hdr->next){
		char buf[1000];
		int len = pjsip_hdr_print_on(hdr, buf, sizeof(buf));
		buf[len] = 0;
		addon_log(LOG_LEVEL_DEBUG, "Header: %s\n", buf);
	}
	*/

	if(dlg && (pj_strcmp2(&rdata->msg_info.msg->line.req.method.name, "REFER") == 0)) {
		//Refer within dialog
		//We cannot call process_in_dialog_refer from this callback (so we copied the way it is done by pjsua, using on_tsx_state_changed)
		//process_in_dialog_refer(&oss, dlg, rdata);
		//addon_log(LOG_LEVEL_DEBUG, "received REFER on_rx_request\n");
		return PJ_TRUE;
	}

	if(dlg && (pj_strcmp2(&rdata->msg_info.msg->line.req.method.name, "INFO") == 0)) {
		return PJ_TRUE;
	}

	if(pj_strcmp2(&rdata->msg_info.msg->line.req.method.name, "SUBSCRIBE") == 0){
		if(dlg) {
			process_in_dialog_subscribe(dlg,rdata);		
		} else {
			process_subscribe_request(rdata);
		}
		return PJ_TRUE;
	}

	Transport *transport;

	if(rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD)
	{
		/*
		ostringstream oss;
		build_basic_request_info(&oss, rdata, &transport);
		*/
	
		reason = pj_str((char*)"OK");

		pjsip_hdr hdr_list;
		pj_list_init(&hdr_list);

		if(rdata->msg_info.msg->line.req.method.id == PJSIP_REGISTER_METHOD) {
    			pjsip_hdr *hdr_from_request;

			//Add Contact header from Request, if present
			pj_str_t STR_CONTACT = {(char*)"Contact" , 7 };
			hdr_from_request = (pjsip_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg,
								&STR_CONTACT,
								NULL);
			if(hdr_from_request) {
				pjsip_hdr *clone_hdr = (pjsip_hdr*) pjsip_hdr_clone(rdata->tp_info.pool, hdr_from_request);
				pj_list_push_back(&hdr_list, clone_hdr);
			}

			//Add Expires header from Request, if present
			pj_str_t STR_EXPIRES = {(char*)"Expires" , 7 };
    			hdr_from_request = (pjsip_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg,
								&STR_EXPIRES,
								NULL);
			if(hdr_from_request) {
				pjsip_hdr *clone_hdr = (pjsip_hdr*) pjsip_hdr_clone(rdata->tp_info.pool, hdr_from_request);
				pj_list_push_back(&hdr_list, clone_hdr);
			}

		}

		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 200, &reason, &hdr_list, NULL);

		make_evt_non_dialog_request(evt, sizeof(evt), rdata->msg_info.len, rdata->msg_info.msg_buf);
		dispatch_event(evt);
		return PJ_TRUE;
	}

	unsigned options = 0;
	status = pjsip_inv_verify_request(rdata, &options, NULL, NULL, g_sip_endpt, NULL);
	if(status != PJ_SUCCESS) {
		reason = pj_str((char*)"Unable to handle this INVITE");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	char local_contact[1000];
	build_local_contact(local_contact, rdata->tp_info.transport, "sip-lab");
	pj_str_t url = pj_str(local_contact);

	status = pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(),
					rdata,
					&url,
					&dlg);

	if(status != PJ_SUCCESS) {
		reason = pj_str((char*)"Internal Server Error (pjsip_dlg_create_uas_and_inc_lock failed)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	pjsip_transport *t = rdata->tp_info.transport;
	pj_str_t str_addr = pj_str( inet_ntoa( (in_addr&)t->local_addr.ipv4.sin_addr.s_addr ) );
	pjmedia_transport *med_transport = create_media_transport(&str_addr);

	if(!med_transport)
	{
		reason = pj_str((char*)"Internal Server Error (could not create media transport)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	pjmedia_transport_info med_tpinfo;
	pjmedia_transport_info_init(&med_tpinfo);
	pjmedia_transport_get_info(med_transport, &med_tpinfo);

	pjmedia_sdp_session *sdp;
	status = pjmedia_endpt_create_sdp( g_med_endpt, rdata->tp_info.pool, 1,
			&med_tpinfo.sock_info,
			&sdp);
	if(status != PJ_SUCCESS) {
		close_media_transport(med_transport);
		reason = pj_str((char*)"Internal Server Error (pjmedia_endprt_create_sdp failed)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	pjsip_inv_session *inv;
	status = pjsip_inv_create_uas(dlg, rdata, sdp, 0, &inv);
	if(status != PJ_SUCCESS) {
		close_media_transport(med_transport);
		reason = pj_str((char*)"Internal Server Error (pjsip_inv_create_uas failed)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	if(!dlg_set_transport(t, dlg)) {
		close_media_transport(med_transport);
		reason = pj_str((char*)"Internal Server Error (set_transport failed)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	pjsip_rx_data *cloned_rdata	= 0;

	if(!(g_flags & FLAG_NO_AUTO_100_TRYING)) {
		pjsip_tx_data *tdata;
		//First response to an INVITE must be created with pjsip_inv_initial_answer(). Subsequent responses to the same transaction MUST use pjsip_inv_answer().
		//Create 100 response
		status = pjsip_inv_initial_answer(inv, rdata,
						100,
						NULL, NULL, &tdata);
		if(status != PJ_SUCCESS) {
			close_media_transport(med_transport);
			reason = pj_str((char*)"Internal Server Error (pjsip_inv_initial_answer failed)");
			pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
			return PJ_TRUE;
		}

		//Send 100 response
		status = pjsip_inv_send_msg(inv, tdata);
		if(status != PJ_SUCCESS) {
			close_media_transport(med_transport);
			reason = pj_str((char*)"Internal Server Error (pjsip_inv_send_msg failed)");
			pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
			return PJ_TRUE;
		}
	} else {
		status = pjsip_rx_data_clone(rdata, 0, &cloned_rdata);

		if(status != PJ_SUCCESS) {
			close_media_transport(med_transport);
			reason = pj_str((char*)"Internal Server Error (pjsip_rx_data_clone failed)");
			pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
			return PJ_TRUE;
		}
	}

	Call *call = (Call*)pj_pool_alloc(inv->pool, sizeof(Call));
	pj_bzero(call, sizeof(Call));

	call->initial_invite_rdata = cloned_rdata;

	if(status != PJ_SUCCESS) {
		close_media_transport(med_transport);
		reason = pj_str((char*)"Internal Server Error (pjsip_rx_data_clone failed)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	call->inv = inv;
	call->med_transport = med_transport;

	if(!inv->dlg) {
		return PJ_TRUE;
	}

	inv->dlg->mod_data[mod_tester.id] = call;

	//Without this, on_rx_response will not be called
	status = pjsip_dlg_add_usage(dlg, &mod_tester, call);
	if(status != PJ_SUCCESS) {
		close_media_transport(med_transport);
		reason = pj_str((char*)"Internal Server Error (pjsip_dlg_add_usage failed)");
		pjsip_endpt_respond_stateless(g_sip_endpt, rdata, 500, &reason, NULL, NULL);
		return PJ_TRUE;
	}

	long call_id;
	if(!g_call_ids.add((long)call, call_id)){
		addon_log(LOG_LEVEL_DEBUG, "Failed to allocate call_id. Event will not be notifield\n");
		return PJ_TRUE;
	}

	call->transport = transport; 
	call->id = call_id;

	int transport_id;

	SipTransportMap::iterator iter = g_SipTransportMap.find(t);
	if( iter != g_SipTransportMap.end() ){
		transport_id = iter->second;
	} else {
		if(t->key.type == PJSIP_TRANSPORT_TCP) {
			transport_id = g_TcpTransportId;
		} else if(t->key.type == PJSIP_TRANSPORT_TLS) {
			transport_id = g_TlsTransportId;
		} else {
			transport_id = -1;
		}
	}
		
	make_evt_incoming_call(evt, sizeof(evt), transport_id, call_id, rdata->msg_info.len, rdata->msg_info.msg_buf);
	dispatch_event(evt);
	
	return PJ_TRUE;
}

static pj_bool_t on_rx_response( pjsip_rx_data *rdata ){
	//addon_log(LOG_LEVEL_DEBUG, "on_rx_response\n");
	//Very important: this callback notifies reception of any SIP response
	//received by the endpoint, no matter if the endpoint was the one
	//that sent the request or not (for example, if the app is running
	//in a loop and breaks and restarts immediately, it will get responses
	//destined to its previous incarnation. So we must check if the 
	//response is associated with a dialog, otherwise: crash.
	if(g_shutting_down) return PJ_TRUE;	

	char evt[2048];
	pj_str_t mname;

	pjsip_cseq_hdr *cseq = rdata->msg_info.cseq;
	char method[100];
	int len = cseq->method.name.slen; 
	strncpy(method, cseq->method.name.ptr, len);
	method[len] = 0;

	ostringstream oss;

	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	if(!dlg){
		//addon_log(LOG_LEVEL_DEBUG, "No dialog associated with rdata\n");
		return PJ_TRUE;
	}

	if(strcmp(method, "SUBSCRIBE") == 0) { 
		Subscription *subscription = (Subscription*)dlg->mod_data[mod_tester.id]; 
		int code = rdata->msg_info.msg->line.status.code;
		if(!subscription->initialized && code >= 200 && code <= 299) {
			//Status code 2XX will cause pjsip_evsub to be called. 
			subscription->initialized = true;	
			return PJ_FALSE;
		}


		long subscription_id;

		if(subscription) {
			if( !g_subscription_ids.get_id((long)subscription, subscription_id) ){
				/*
				oss.seekp(0);
				oss << "event=internal_error" << EVT_DATA_SEP << "details=Failed to get subscription_id";
				*/

				make_evt_internal_error(evt, sizeof(evt), "failed to get subscription_id");
				dispatch_event(evt); 
				return true; 
			}
		} else {
			addon_log(LOG_LEVEL_DEBUG, "Ignoring response for mod_data not set to a subscription\n");
			return PJ_TRUE;
		}

		mname = rdata->msg_info.cseq->method.name;
		make_evt_response(evt, sizeof(evt), "subscription", subscription_id, mname.slen, mname.ptr, rdata->msg_info.len, rdata->msg_info.msg_buf);
		dispatch_event(evt);

		return PJ_TRUE;
	}


	Call *call = (Call*)dlg->mod_data[mod_tester.id]; 
	long call_id;

	if(call) {
		//addon_log(LOG_LEVEL_DEBUG, "call:%x\n",call);
		if( !g_call_ids.get_id((long)call, call_id) ){
			//addon_log(LOG_LEVEL_DEBUG, "The call is not present in g_call_ids.\n");
			// It means the call terminated and was removed from g_call_ids\n");
			//So let's try to find it at g_LastCalls

			boost::circular_buffer<Pair_Call_CallId>::iterator iter;
			Pair_Call_CallId pcc;
			pcc.pCall = call;
			iter = find(g_LastCalls.begin(), g_LastCalls.end(), pcc);
			if(iter == g_LastCalls.end())
			{
				oss.seekp(0);
				oss << "event=internal_error" << EVT_DATA_SEP << "details=Failed to get call_id";
				addon_log(LOG_LEVEL_DEBUG, "on_rx_response failed to resolve call_id\n");
				return true; 
			}
			call_id = iter->id;
		}
	} else {
		addon_log(LOG_LEVEL_DEBUG, "Ignoring response for mod_data not set to a call\n");
		return PJ_TRUE;
	}

	mname = rdata->msg_info.cseq->method.name;
	make_evt_response(evt, sizeof(evt), "call", call_id, mname.slen, mname.ptr, rdata->msg_info.len, rdata->msg_info.msg_buf);
	dispatch_event(evt);

	return PJ_TRUE;
}

static pjsip_redirect_op on_redirected(pjsip_inv_session *inv, const pjsip_uri *target, const pjsip_event *e) {
	PJ_UNUSED_ARG(e);
	return PJSIP_REDIRECT_ACCEPT;
}

static void on_rx_offer2(pjsip_inv_session *inv, struct pjsip_inv_on_rx_offer_cb_param *param) {
	addon_log(LOG_LEVEL_DEBUG, "on_rx_offer2\n");
	if(g_shutting_down) return;

    /*
	bool is_reinvite = false;

	if(inv->state == PJSIP_INV_STATE_CONFIRMED) {
		is_reinvite = true;
	}
    */

	char evt[2048];

	Call *call = (Call*)inv->dlg->mod_data[mod_tester.id];

	pj_status_t status;

    const pjmedia_sdp_session *offer = param->offer;
    const pjsip_rx_data *rdata = param->rdata;

	pjmedia_sdp_conn *conn;
	conn = offer->media[0]->conn;
	if(!conn) conn = offer->conn;	
	
	pjmedia_transport_info tpinfo;
	pjmedia_transport_info_init(&tpinfo);
	pjmedia_transport_get_info(call->med_transport,&tpinfo);

	pjmedia_sdp_session *answer;
	status = pjmedia_endpt_create_sdp(g_med_endpt,
					inv->pool,
					1,
					&tpinfo.sock_info,
					&answer);
	if(status != PJ_SUCCESS){
		make_evt_internal_error(evt, sizeof(evt), "on_rx_offer: pjmedia_endpt_create_sdp failed");
		dispatch_event(evt); 
		return; 
	}

	int remote_mode = get_media_mode(offer->media[0]->attr, offer->media[0]->attr_count);
	if(remote_mode == SENDONLY) {
		call->remote_hold = 1;
	} else if(remote_mode == INACTIVE) {
		call->remote_hold = 1;
	} else if(remote_mode == SENDRECV) {
		call->remote_hold = 0;
	} else if(remote_mode == RECVONLY) {
		call->remote_hold = 0;
	} else {
		call->remote_hold = 0;
	}

	//char *mode = get_media_mode_str(remote_mode);
	
	pjmedia_sdp_attr *attr;

	// Remove existing directions attributes
	pjmedia_sdp_media_remove_all_attr(answer->media[0], "sendrecv");
	pjmedia_sdp_media_remove_all_attr(answer->media[0], "sendonly");
	pjmedia_sdp_media_remove_all_attr(answer->media[0], "recvonly");
	pjmedia_sdp_media_remove_all_attr(answer->media[0], "inactive");

	if(call->local_hold) {
		// Keep call on-hold by setting 'sendonly' attribute.
		// (See RFC 3264 Section 8.4 and RFC 4317 Section 3.1)
		if(call->remote_hold) {
			attr = pjmedia_sdp_attr_create(inv->pool, "inactive", NULL);
		} else {
			attr = pjmedia_sdp_attr_create(inv->pool, "sendonly", NULL);
		}
	} else if(call->remote_hold) {
		attr = pjmedia_sdp_attr_create(inv->pool, "recvonly", NULL);
	} else {
		attr = pjmedia_sdp_attr_create(inv->pool, "sendrecv", NULL);
	}
	pjmedia_sdp_media_add_attr(answer->media[0], attr);

	status = pjsip_inv_set_sdp_answer(inv, answer);
	if(status != PJ_SUCCESS){
		make_evt_internal_error(evt, sizeof(evt), "on_rx_offer: pjsip_inv_set_sdp_answer failed");
		dispatch_event(evt); 
		return;
	}

	long call_id;
	if( !g_call_ids.get_id((long)call, call_id) ){
		make_evt_internal_error(evt, sizeof(evt), "on_rx_offer: Failed to get call_id.\n");
		dispatch_event(evt); 
		return;
	}

    // The below cannot be used: in case of delayed media scenarios, on_rx_offer and on_rx_offer2 will be called when the '200 OK'
    // is received for an INVITE without SDP.
    /*
    make_evt_reinvite(evt, sizeof(evt), call_id, rdata->msg_info.len, rdata->msg_info.msg_buf);
    dispatch_event(evt);
    */

	return;
}

static void on_dtmf(pjmedia_stream *stream, void *user_data, int digit){
	if(g_shutting_down) return;

	char evt[256];

	long call_id;
	if( !g_call_ids.get_id((long)user_data, call_id) ){
		addon_log(LOG_LEVEL_DEBUG, "on_dtmf: Failed to get call_id. Event will not be notified.\n");	
		return;
	}

	char d = (char)tolower((char)digit);
	if(d == '*') d = 'e';
	if(d == '#') d = 'f';

	Call *c = (Call*)user_data;

	int mode = DTMF_MODE_RFC2833;

	if(g_dtmf_inter_digit_timer) {

		PJW_LOCK();
		int *pLen = &c->DigitBufferLength[mode];

		if(*pLen > MAXDIGITS) {
			PJW_UNLOCK();
			addon_log(LOG_LEVEL_DEBUG, "No more space for digits in rfc2833 buffer\n");
			return;
		}

		c->DigitBuffers[mode][*pLen] = d;
		(*pLen)++;
		c->last_digit_timestamp[mode] = ms_timestamp();
		PJW_UNLOCK();
	} else {
		make_evt_dtmf(evt, sizeof(evt), call_id, 1, &d, mode);
		dispatch_event(evt);
	}
}

static void on_registration_status(pjsip_regc_cbparam *param){
	//addon_log(LOG_LEVEL_DEBUG, "on_registration_status\n");
	if(g_shutting_down) return;

	char evt[1024];

	long acc_id;
	if( !g_account_ids.get_id((long)param->regc, acc_id) ){
		addon_log(LOG_LEVEL_DEBUG, "on_registration_status: Failed to get account_id. Event will not be notified.\n");	
		return;
	}

	char reason[100];
	int len = param->reason.slen;
	strncpy(reason, param->reason.ptr, len);
	reason[len] = 0;

	make_evt_registration_status(evt, sizeof(evt), acc_id, param->code, reason, param->expiration);
	dispatch_event(evt); 
}

int pjw_packetdump_start(const char *dev, const char *file){
    /*
	PJW_LOCK();

	if(g_PacketDumper) delete g_PacketDumper;

	g_PacketDumper = new PacketDumper();
	if(!g_PacketDumper->init(dev, file)){
		PJW_UNLOCK();
		set_error("Failed to start packetdumping");
		return -1;
	}

	PJW_UNLOCK();
    */
	return 0;
}

int pjw_packetdump_stop(){
    /*
	PJW_LOCK();

	if(g_PacketDumper) delete g_PacketDumper;
	g_PacketDumper = NULL;

	PJW_UNLOCK();
    */
	return 0;
}

int pjw_get_codecs(char *out_codecs)
{
	clear_error();

	pjmedia_codec_mgr *codec_mgr;
	pjmedia_codec_info codec_info[100];
	unsigned count = sizeof(codec_info);
	unsigned prio[100];
	pj_status_t status;
	ostringstream oss;	

	PJW_LOCK();
	
	codec_mgr = pjmedia_endpt_get_codec_mgr(g_med_endpt);
	if(!codec_mgr) {
		set_error("pjmedia_endpt_get_codec_mgr failed");
		goto out;
	}

	status = pjmedia_codec_mgr_enum_codecs(codec_mgr,
						&count,
						codec_info,
						prio);
	if(status != PJ_SUCCESS) {
		set_error("pjmedia_codec_mgr_enum_codecs failed");
		goto out;
	}		

	for(unsigned i=0; i<count; ++i) {
		pjmedia_codec_info *info = &codec_info[i];
		if(i != 0) oss << " ";
		oss.write(info->encoding_name.ptr, info->encoding_name.slen);
		oss << "/" << info->clock_rate;
		oss << "/" << info->channel_cnt;
		oss << ":" << prio[i];
	}

out:
	PJW_UNLOCK();

	if(pjw_errorstring[0]){
		return -1;
	}

	strcpy(out_codecs, oss.str().c_str());
	return 0;
}

int pjw_set_codecs(const char *in_codec_info) {
	clear_error();

	//char error[1000];
	pjmedia_codec_mgr *codec_mgr;
	pj_status_t status;
	char codec_info[1000]; 
	pj_str_t codec_id;
	char *tok;

	PJW_LOCK();
	
	codec_mgr = pjmedia_endpt_get_codec_mgr(g_med_endpt);
	if(!codec_mgr) {
		set_error("pjmedia_endpt_get_codec_mgr failed");
		goto out;
	}

	codec_id = pj_str((char*)"");
	status = pjmedia_codec_mgr_set_codec_priority(codec_mgr, &codec_id, 0);
	if(status != PJ_SUCCESS) {
		set_error("pjmedia_codec_mgr_set_codec_priority(zero all) failed.");
		goto out;
	}
	
	strcpy(codec_info, in_codec_info);
	
	tok = strtok(codec_info, ":");
	while(tok) {
		if(!tok) {
			set_error("malformed argument codec_info");
			goto out;
		}
		char *prio = strtok(NULL, " ");
		if(!prio) {
			set_error("malformed argument codec_info");
			goto out;
		}

		codec_id = pj_str(tok);
		status = pjmedia_codec_mgr_set_codec_priority(codec_mgr, &codec_id, atoi(prio));
		if(status != PJ_SUCCESS) {
			set_error("pjmedia_codec_mgr_set_codec_priority failed");
			goto out;
		}
		tok = strtok(NULL, " ");
	}

out:
	PJW_UNLOCK();

	if(pjw_errorstring[0]){
		//Try to put default priority to all codecs
		codec_id = pj_str((char*)"");
		status = pjmedia_codec_mgr_set_codec_priority(codec_mgr, &codec_id, 128);
		return -1;
	}

	return 0;
}

int __pjw_shutdown()
{
	//addon_log(LOG_LEVEL_DEBUG, "pjw_shutdown thread_id=%i\n", syscall(SYS_gettid));
	PJW_LOCK();

	g_shutting_down = true;

    //disable auto cleanup

    /*
	map<long, long>::iterator iter;
	iter = g_call_ids.id_map.begin();
	while(iter != g_call_ids.id_map.end()){
		Call *call = (Call*)iter->second;

		addon_log(LOG_LEVEL_DEBUG, "Terminating call %d\n", iter->first);

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
			addon_log(LOG_LEVEL_DEBUG, "pjsip_inv_end_session failed statut=%i (%s)\n", status, err);
			++iter;
			continue;
		}

		if(!tdata)
		{
			//if tdata was not set by pjsip_inv_end_session, it means we didn't receive any response yet (100 Trying) and we cannot send CANCEL in this situation. So we just can return here without calling pjsip_inv_send_msg.
			++iter;
			addon_log(LOG_LEVEL_DEBUG, "no tdata\n");
			continue;
		}

		status = pjsip_inv_send_msg(call->inv, tdata);
		if(status != PJ_SUCCESS){
			addon_log(LOG_LEVEL_DEBUG, "pjsip_inv_send_msg failed\n");
		}
		++iter;
	}

	iter = g_account_ids.id_map.begin();
	while(iter != g_account_ids.id_map.end()){
		pjsip_regc *regc = (pjsip_regc*)iter->second;

		addon_log(LOG_LEVEL_DEBUG, "Unregistering account %d\n", iter->first);

		pjsip_tx_data *tdata;
		pj_status_t status;

		status = pjsip_regc_unregister(regc, &tdata);
		if(status != PJ_SUCCESS)
		{
			addon_log(LOG_LEVEL_DEBUG, "pjsip_regc_unregister failed\n");
		}

		status = pjsip_regc_send(regc, tdata);
		if(status != PJ_SUCCESS)
		{
			addon_log(LOG_LEVEL_DEBUG, "pjsip_regc_send failed\n");
		}
		++iter;
	}

	Subscription *subscription;
	iter = g_subscription_ids.id_map.begin();
	while(iter != g_subscription_ids.id_map.end()){
		addon_log(LOG_LEVEL_DEBUG, "Unsubscribing subscription %d\n", iter->first);

		subscription = (Subscription*)iter->second;	
		if(!subscription_subscribe(subscription, 0, NULL)) {
			addon_log(LOG_LEVEL_DEBUG, "Unsubscription failed failed\n");
		}
		++iter;
	}

	PJW_UNLOCK();

	//uint32_t wait = 100000 * (g_call_ids.id_map.size() + g_account_ids.id_map.size()));
	//wait += 1000000; //Wait one whole second to permit packet capture to get any final packets

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

//Copied from streamutil.c (pjsip sample)
static const char *good_number(char *buf, pj_int32_t val)
{
    if (val < 1000) {
	pj_ansi_sprintf(buf, "%d", val);
    } else if (val < 1000000) {
	pj_ansi_sprintf(buf, "%d.%dK", 
			val / 1000,
			(val % 1000) / 100);
    } else {
	pj_ansi_sprintf(buf, "%d.%02dM", 
			val / 1000000,
			(val % 1000000) / 10000);
    }

    return buf;
}

static void build_stream_stat(ostringstream &oss, pjmedia_rtcp_stat *stat, pjmedia_stream_info *stream_info)
{
    char temp[200];
    char duration[80], last_update[80];

    //char bps[16];
    //char ipbps[16];
    char packets[16];
    //char bytes[16];
    //char ipbytes[16];

    pj_time_val now;

    pj_gettimeofday(&now);

	oss << "{ ";

    PJ_TIME_VAL_SUB(now, stat->start);
    sprintf(duration, "\"Duration\": \"%02ld:%02ld:%02ld.%03ld\"",
	    now.sec / 3600,
	    (now.sec % 3600) / 60,
	    (now.sec % 60),
	    now.msec);

	oss <<  duration;

    sprintf(temp, ", \"CodecInfo\": \"%.*s/%d/%d\"",
    (int)stream_info->fmt.encoding_name.slen,
    stream_info->fmt.encoding_name.ptr,
	stream_info->fmt.clock_rate,
    stream_info->fmt.channel_cnt);

	oss << temp << ",";

	oss << " \"RX\": { "; //Opening RX

    if (stat->rx.update_cnt == 0)
	strcpy(last_update, "\"LastUpdate\": \"\"");
    else {
	sprintf(last_update, "\"LastUpdate\": \"%ld.%ld\"",
		stat->rx.update.sec,
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
	oss << ", \"StandardDeviation\": " << pj_math_stat_get_stddev(&stat->rx.loss_period) / 1000.0 << " }";

	oss << ", \"Jitter\": { ";
	oss << "\"Min\": " << stat->rx.jitter.min / 1000.0;
	oss << ", \"Mean\": " << stat->rx.jitter.mean / 1000.0;
	oss << ", \"Max\": " << stat->rx.jitter.max / 1000.0;
	oss << ", \"Last\": " << stat->rx.jitter.last / 1000.0;
	oss << ", \"StandardDeviation\": " << pj_math_stat_get_stddev(&stat->rx.jitter) / 1000.0 << " }";

	oss << " }"; //Closing RX

	
	oss << ", \"TX\": { "; //Opening TX

    if (stat->tx.update_cnt == 0)
	strcpy(last_update, "\"LastUpdate\": \"\"");
    else {
	sprintf(last_update, "\"LastUpdate\": \"%ld.%ld\"",
		stat->tx.update.sec,
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
	oss << ", \"StandardDeviation\": " << pj_math_stat_get_stddev(&stat->tx.loss_period) / 1000.0 << " }";

	oss << ", \"Jitter\": { ";
	oss << "\"Min\": " << stat->tx.jitter.min / 1000.0;
	oss << ", \"Mean\": " << stat->tx.jitter.mean / 1000.0;
	oss << ", \"Max\": " << stat->tx.jitter.max / 1000.0;
	oss << ", \"Last\": " << stat->tx.jitter.last / 1000.0;
	oss << ", \"StandardDeviation\": " << pj_math_stat_get_stddev(&stat->tx.jitter) / 1000.0 << " }";

	oss << " }"; //Closing TX

	oss << ", \"RTT\": { "; // Opening RTT

	oss << "\"Min\": " << stat->rtt.min / 1000.0;
	oss << ", \"Mean\": " << stat->rtt.mean / 1000.0;
	oss << ", \"Max\": " << stat->rtt.max / 1000.0;
	oss << ", \"Last\": " << stat->rtt.last / 1000.0;
	oss << ", \"StandardDeviation\": " << pj_math_stat_get_stddev(&stat->rtt) / 1000.0;
	oss << " }"; //Closing RTT
	oss << " }";
}

void close_media_transport(pjmedia_transport *med_transport) {
	pjmedia_transport_info tpinfo;
	pjmedia_transport_info_init(&tpinfo);
	pj_status_t status = pjmedia_transport_get_info(med_transport, &tpinfo);
	if( status != PJ_SUCCESS ) return;

    /*
	if(g_PacketDumper){
		g_PacketDumper->remove_endpoint( tpinfo.sock_info.rtp_addr_name.ipv4.sin_addr.s_addr, tpinfo.sock_info.rtp_addr_name.ipv4.sin_port );
		g_PacketDumper->remove_endpoint( tpinfo.sock_info.rtcp_addr_name.ipv4.sin_addr.s_addr, tpinfo.sock_info.rtcp_addr_name.ipv4.sin_port );
	}
    */

    status = pjmedia_transport_media_stop(med_transport);
    if( status != PJ_SUCCESS ) {
        addon_log(LOG_LEVEL_DEBUG, "Critical Error: pjmedia_transport_media_stop failed. status=%d\n", status);
    }

	status = pjmedia_transport_close(med_transport);
	if( status != PJ_SUCCESS ) {
		addon_log(LOG_LEVEL_DEBUG, "Critical Error: pjmedia_transport_close failed. status=%d\n", status);
	}
}


bool init_media_ports(Call *c, unsigned sampling_rate, unsigned channel_count, unsigned samples_per_frame, unsigned bits_per_sample) {
	pj_status_t status;

	if(!c->null_port) {
		status = pjmedia_null_port_create(c->inv->pool,
						sampling_rate,
						channel_count,
						samples_per_frame,
						bits_per_sample,
						&c->null_port);
		if(status != PJ_SUCCESS) return false;
	}

	if(!c->wav_writer) {
		if(!prepare_wire(c->inv->pool, &c->wav_writer, sampling_rate, channel_count, samples_per_frame, bits_per_sample)) {
			return false;
		}
	}

	if(!c->wav_player) {
		if(!prepare_wire(c->inv->pool, &c->wav_player, sampling_rate, channel_count, samples_per_frame, bits_per_sample)) {
			return false;
		}
	}

	if(!c->tonegen) {
		if(!prepare_wire(c->inv->pool, &c->tonegen, sampling_rate, channel_count, samples_per_frame, bits_per_sample)) {
			return false;
		}
	}

	if(!c->dtmfdet) {
		status = chainlink_dtmfdet_create(c->inv->pool,
						sampling_rate,
						channel_count,
						samples_per_frame,
						bits_per_sample,
						on_inband_dtmf,
						c,
						(pjmedia_port**)&c->dtmfdet);
		if(status != PJ_SUCCESS) return false;
	}

   if(!c->fax) {
       if(!prepare_wire(c->inv->pool, &c->fax, sampling_rate, channel_count, samples_per_frame, bits_per_sample)) {
           return false;
       }
   }

	connect_media_ports(c);
	return true;
}

void connect_media_ports(Call *c) {
	((chainlink*)c->dtmfdet)->next = (pjmedia_port*)c->tonegen;
	((chainlink*)c->tonegen)->next = (pjmedia_port*)c->wav_player;
	((chainlink*)c->wav_player)->next = (pjmedia_port*)c->wav_writer;
    ((chainlink*)c->wav_writer)->next = (pjmedia_port*)c->fax;
    ((chainlink*)c->fax)->next = c->null_port;
}

bool prepare_tonegen(Call *c) {
	pj_status_t status;

	chainlink *link = (chainlink*)c->tonegen;

	pjmedia_port *stream_port;
	status = pjmedia_stream_get_port(c->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS)
	{
		return false;
	}

	if(link->port.info.signature == CHAINLINK_WIRE_PORT_SIGNATURE) {
		status = chainlink_tonegen_create(c->inv->pool,
						PJMEDIA_PIA_SRATE(&stream_port->info),
						PJMEDIA_PIA_CCNT(&stream_port->info),
						PJMEDIA_PIA_SPF(&stream_port->info),
						PJMEDIA_PIA_BITS(&stream_port->info),
						0,
						(pjmedia_port**)&c->tonegen);
		if(status != PJ_SUCCESS) return false;
	}
	
	connect_media_ports(c);
	return true;
}

bool prepare_wav_player(Call *c, const char *file) {
	pj_status_t status;

	chainlink *link = (chainlink*)c->wav_player;

	pjmedia_port *stream_port;
	status = pjmedia_stream_get_port(c->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS) return false;

	unsigned wav_ptime;
	wav_ptime = PJMEDIA_PIA_SPF(&stream_port->info) * 1000 / PJMEDIA_PIA_SRATE(&stream_port->info);

	status = pjmedia_port_destroy((pjmedia_port*)link);
	if(status != PJ_SUCCESS) return false;

	status = chainlink_wav_player_port_create(c->inv->pool,
						file,
						wav_ptime,
						0,
						-1,
						(pjmedia_port**)&c->wav_player);
	if(status != PJ_SUCCESS) return false;
	
	connect_media_ports(c);
	return true;
}

bool prepare_wav_writer(Call *c, const char *file) {
	pj_status_t status;

	chainlink *link = (chainlink*)c->wav_writer;

	pjmedia_port *stream_port;
	status = pjmedia_stream_get_port(c->med_stream,
					&stream_port);
	if(status != PJ_SUCCESS) return false;

	status = pjmedia_port_destroy((pjmedia_port*)link);
	if(status != PJ_SUCCESS) return false;

	status = chainlink_wav_writer_port_create(c->inv->pool,
						file,
						PJMEDIA_PIA_SRATE(&stream_port->info),
						PJMEDIA_PIA_CCNT(&stream_port->info),
						PJMEDIA_PIA_SPF(&stream_port->info),
						PJMEDIA_PIA_BITS(&stream_port->info),
						PJMEDIA_FILE_WRITE_PCM,
						0,
						(pjmedia_port**)&c->wav_writer);
	if(status != PJ_SUCCESS) return false;
	
	connect_media_ports(c);
	return true;
}


bool prepare_fax(Call *c, bool is_sender, const char *file, unsigned flags) {
   pj_status_t status;

   chainlink *link = (chainlink*)c->fax;

   pjmedia_port *stream_port;
   status = pjmedia_stream_get_port(c->med_stream,
                   &stream_port);
   if(status != PJ_SUCCESS) return false;

   status = pjmedia_port_destroy((pjmedia_port*)link);
   if(status != PJ_SUCCESS) return false;

   status = chainlink_fax_port_create(c->inv->pool,
                       PJMEDIA_PIA_SRATE(&stream_port->info),
                       PJMEDIA_PIA_CCNT(&stream_port->info),
                       PJMEDIA_PIA_SPF(&stream_port->info),
                       PJMEDIA_PIA_BITS(&stream_port->info),
                       on_fax_result,
                       c,
                       is_sender,
                       file,
                       flags, 
                       (pjmedia_port**)&c->fax);
   if(status != PJ_SUCCESS) return false;

   connect_media_ports(c);
   return true;
}

bool prepare_wire(pj_pool_t *pool, chainlink **link, unsigned sampling_rate, unsigned channel_count, unsigned samples_per_frame, unsigned bits_per_sample) {
	pj_status_t status;

	if(*link) {
		//addon_log(LOG_LEVEL_DEBUG, "prepare_wire: link is set. It will be destroyed\n");
		pjmedia_port *port = (pjmedia_port*)*link;
		status = pjmedia_port_destroy(port);		
		*link = NULL;
		if(status != PJ_SUCCESS) return false;
	}

	status = chainlink_wire_port_create(pool,
						sampling_rate,
						channel_count,
						samples_per_frame,
						bits_per_sample,
						(pjmedia_port**)link);
	if(status != PJ_SUCCESS) return false;

	return true;
}

void on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body) {
	//addon_log(LOG_LEVEL_DEBUG, "on_rx_notify\n");

	char evt[2048];
	
	if(g_shutting_down) return;

	ostringstream oss;
	Subscription *s;

	s = (Subscription*)pjsip_evsub_get_mod_data(sub, mod_tester.id);
	if(!s) {
		addon_log(LOG_LEVEL_DEBUG, "Subscription not set at mod_data. Ignoring\n");
		return;
	}	

	make_evt_request(evt, sizeof(evt), "subscription", s->id, rdata->msg_info.len, rdata->msg_info.msg_buf);
	dispatch_event(evt);
}

static void on_client_refresh( pjsip_evsub *sub ) {
	Subscription *subscription;
	//pj_status_t status;

	subscription = (Subscription*) pjsip_evsub_get_mod_data(sub, mod_tester.id);

	if(!subscription) {
		set_error("on_client_refresh: pjsip_evsub_get_mod_data returned 0");
		goto out;
	}

	if(!subscription_subscribe_no_headers(subscription, -1)) {
		goto out;
	}

	//addon_log(LOG_LEVEL_DEBUG, "on_client_refresh: SUBSCRIBE dispatched\n");

out:
	if(pjw_errorstring[0]) {
		dispatch_event(pjw_errorstring);
	}
}

static void client_on_evsub_state( pjsip_evsub *sub, pjsip_event *event) {
	if(g_shutting_down) return;

	char evt[2048];
	pj_str_t mname;

	//addon_log(LOG_LEVEL_DEBUG, "client_on_evsub_state: %s\n", pjsip_evsub_get_state_name(sub));

	PJ_UNUSED_ARG(event);

	pjsip_rx_data *rdata;
	Subscription *subscription = (Subscription*)pjsip_evsub_get_mod_data(sub, mod_tester.id);
	if(!subscription) {
		//addon_log(LOG_LEVEL_DEBUG, "mod_data set to NULL (it means subscription doesn't exist anymore). Ignoring\n");
		addon_log(LOG_LEVEL_DEBUG, "mod_data set to NULL (we don't know what this means yet. Ignoring\n");
		return;
	}

	pjsip_generic_string_hdr *refer_sub;
	const pj_str_t REFER_SUB = { (char*)"Refer-Sub", 9 };
	ostringstream oss;

	// When subscription is accepted (got 200/OK)
	int state = pjsip_evsub_get_state(sub);
	if(state == PJSIP_EVSUB_STATE_ACCEPTED) {

		pj_assert(event->type == PJSIP_EVENT_TSX_STATE &&
			event->body.tsx_state.type == PJSIP_EVENT_RX_MSG);

		rdata = event->body.tsx_state.src.rdata;

		mname = rdata->msg_info.cseq->method.name;
		make_evt_response(evt, sizeof(evt), "subscription", subscription->id, mname.slen, mname.ptr, rdata->msg_info.len, rdata->msg_info.msg_buf);
		dispatch_event(evt);


		//Find Refer-Sub header
		refer_sub = (pjsip_generic_string_hdr*)
				pjsip_msg_find_hdr_by_name(rdata->msg_info.msg,
								&REFER_SUB,
								NULL);

		if(refer_sub && pj_strcmp2(&refer_sub->hvalue, "false") == 0) {
			pjsip_evsub_terminate(sub, PJ_TRUE);
		}

	} else if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACTIVE ||
		pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
		//Here we catch incoming NOTIFY 

		//addon_log(LOG_LEVEL_DEBUG, "NOTIFY\n");
	
		//When subscription is terminated
		if(pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
			pjsip_evsub_set_mod_data(sub, mod_tester.id, NULL);
		}		

		return;

		rdata = event->body.tsx_state.src.rdata;

		//Transport *t;
		//build_basic_request_info(&oss, rdata, &t);

		long subscription_id;
		if( !g_subscription_ids.get_id((long)subscription, subscription_id) ){
			/*
			addon_log(LOG_LEVEL_DEBUG, "FAILURE\n");
			oss.seekp(0);
			oss << "event=internal_error" << EVT_DATA_SEP << "details=Failed to get call_id";	
			dispatch_event(oss.str().c_str());
			*/

			char error_msg[] = "failed to get subscription_id";
			make_evt_internal_error(evt, sizeof(evt), error_msg);
			dispatch_event(evt);
			return;
		} 

		//addon_log(LOG_LEVEL_DEBUG, "dispatching NOTIFY event\n");
		//dispatch_event(oss.str().c_str());

		make_evt_request(evt, sizeof(evt), "subscription", subscription_id, rdata->msg_info.len, rdata->msg_info.msg_buf);
		dispatch_event(evt);

		if(pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
			pjsip_evsub_set_mod_data(sub, mod_tester.id, NULL);
		}
		
	} else {
		//It is not message. Just ignore.
		return;
	}
}

static void server_on_evsub_state( pjsip_evsub *sub, pjsip_event *event)
{
	Subscriber *s;
	//pj_status_t status;
	//pjsip_tx_data *tdata;
	
	//addon_log(LOG_LEVEL_DEBUG, "server_on_evsub_state\n");
	if(!sub) {
		addon_log(LOG_LEVEL_DEBUG, "server_on_evesub_state: sub not set. Ignoring\n");
		return;
	}
	//addon_log(LOG_LEVEL_DEBUG, "state= %d\n", pjsip_evsub_get_state(sub));
	//addon_log(LOG_LEVEL_DEBUG, "server_on_evsub_state %s\n", pjsip_evsub_get_state_name(sub));

	if(g_shutting_down) return; 

	PJ_UNUSED_ARG(event);

        s = (Subscriber*)pjsip_evsub_get_mod_data(sub, mod_tester.id);
        if (!s) {
		addon_log(LOG_LEVEL_DEBUG, "server_on_evsub_state: Subscriber not set as mod_data. Ignoring\n");
		return;
	}

	/*
	* When subscription is terminated, clear the xfer_sub member of
	* the inv_data.
	*/

	if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
		pjsip_evsub_set_mod_data(sub, mod_tester.id, NULL);
	}
}

//Called when incoming SUBSCRIBE (or any method taht establishes a subscription like REFER) is received
static void server_on_evsub_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	char evt[2048];

	pjw_errorstring[0] = 0;

	pj_status_t status;
	pjsip_tx_data *tdata;

	ostringstream oss;
	Subscriber *s;
	//Transport *t;

	if(g_shutting_down) return;
	addon_log(LOG_LEVEL_DEBUG, "server_on_evsub_rx_refresh\n");

	s = (Subscriber*)pjsip_evsub_get_mod_data(sub, mod_tester.id);
	if(!s) {
		set_error("pjsip_evsub_get_mod_data failed");	
		goto out;
	}

	make_evt_request(evt, sizeof(evt), "subscriber", s->id, rdata->msg_info.len, rdata->msg_info.msg_buf);
	dispatch_event(evt);

	if( pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
		pj_str_t reason = { (char*)"noresource", 10 };
		status = pjsip_evsub_notify(sub, 
						PJSIP_EVSUB_STATE_TERMINATED,
						NULL,
						&reason,
						&tdata);
		if(status != PJ_SUCCESS) {
			set_error("pjsip_evsub_notify failed");
			goto out;
		}
	} else {
		status = pjsip_evsub_current_notify(sub, &tdata);
		if(status != PJ_SUCCESS) {
			set_error("pjsip_evsub_current_notify failed");
			goto out;
		}
	}

	status = pjsip_evsub_send_request(sub,tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_send_request failed");
		goto out;
	}
	addon_log(LOG_LEVEL_DEBUG, "server_on_evsub_rx_refresh: NOTIFY containing subscription state should have been sent\n");

out:
	if(pjw_errorstring[0]) {
		make_evt_internal_error(evt, sizeof(evt), pjw_errorstring);
		dispatch_event(evt);
	}
}


//Adapted (mostly copied) from pjsua function on_call_transfered
void process_in_dialog_refer(pjsip_dialog *dlg, pjsip_rx_data *rdata)
{
    addon_log(LOG_LEVEL_DEBUG, "process_in_dialog_refer\n");

    char evt[2048];

    pj_status_t status;
    //pjsip_tx_data *tdata;
    //int new_call;
    const pj_str_t str_refer_to = { (char*)"Refer-To", 8};
    const pj_str_t str_refer_sub = { (char*)"Refer-Sub", 9 };
    //const pj_str_t str_ref_by = { (char*)"Referred-By", 11 };
    pjsip_generic_string_hdr *refer_to;
    pjsip_generic_string_hdr *refer_sub;
    //pjsip_hdr *ref_by_hdr;
    pj_bool_t no_refer_sub = PJ_FALSE;
    //char *uri;
    //pj_str_t tmp;
    pjsip_status_code code;
    pjsip_evsub *sub;

    Call *call = (Call*)dlg->mod_data[mod_tester.id]; 
    long call_id;
    if( !g_call_ids.get_id((long)call, call_id) ){
	addon_log(LOG_LEVEL_DEBUG, "process_in_dialog_refer: Failed to get call_id. Event will not be notified.\n");	
	return;
    }

    /* Find the Refer-To header */
    refer_to = (pjsip_generic_string_hdr*)
        pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_refer_to, NULL);

    if (refer_to == NULL) {
        /* Invalid Request.
         * No Refer-To header!
         */
        pjsip_dlg_respond( dlg, rdata, 400, NULL, NULL, NULL);
	//dispatch_event("event=broken_refer");
        return;
    }

    /* Find optional Referred-By header (to be copied onto outgoing INVITE
     * request.
     */
    /*
    ref_by_hdr = (pjsip_hdr*)
                 pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_ref_by,
                                            NULL);
    */
    /* if(ref_by_hdr) {
    	pjsip_generic_string_hdr *referred_by = (pjsip_generic_string_hdr*)ref_by_hdr;
    } */

    /* Find optional Refer-Sub header */
    refer_sub = (pjsip_generic_string_hdr*)
        pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_refer_sub, NULL);

    if (refer_sub) {
        if (!pj_strnicmp2(&refer_sub->hvalue, "true", 4)==0) {
		//Header Refer-sub (Refer Subscription) set to something other than true (probably to 'false').
		//This means the requester doesn't want to be subscribed to refer events
		// For details look for ietf draft : Suppression of Session Initiation Protocol REFER Method Implicit Subscription
		// draft-ietf-sip-refer-with-norefersub-04
		no_refer_sub = PJ_TRUE;
	}
    }

    code = PJSIP_SC_ACCEPTED;

    if (no_refer_sub) {
        /*
         * Always answer with 2xx.
         */
        pjsip_tx_data *tdata;
        const pj_str_t str_false = { (char*)"false", 5};
        pjsip_hdr *hdr;

        status = pjsip_dlg_create_response(dlg, rdata, code, NULL,
                                           &tdata);
        if (status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "REFER response creation failure");
		dispatch_event(evt);
		return;
        }

	/* Add Refer-Sub header */
	hdr = (pjsip_hdr*) 
	       pjsip_generic_string_hdr_create(tdata->pool, &str_refer_sub,
					      &str_false);
	pjsip_msg_add_hdr(tdata->msg, hdr);


	/* Send answer */
	status = pjsip_dlg_send_response(dlg, pjsip_rdata_get_tsx(rdata),
					 tdata);
	if (status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "REFER response transmission failure");
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
	status = pjsip_xfer_create_uas( call->inv->dlg, &xfer_cb, rdata, &sub);
	if (status != PJ_SUCCESS) {
		make_evt_internal_error(evt, sizeof(evt), "xfer_uas_creation failure");
		dispatch_event(evt);
	    	pjsip_dlg_respond( call->inv->dlg, rdata, 500, NULL, NULL, NULL);
	    	return;
	}

	/* If there's Refer-Sub header and the value is "true", send back
	 * Refer-Sub in the response with value "true" too.
	 */
	if (refer_sub) {
	    const pj_str_t str_true = { (char*)"true", 4 };
	    pjsip_hdr *hdr;

	    hdr = (pjsip_hdr*) 
		   pjsip_generic_string_hdr_create(call->inv->dlg->pool, 
						   &str_refer_sub,
						   &str_true);
	    pj_list_push_back(&hdr_list, hdr);

	}

	/* Accept the REFER request, send 2xx. */
	pjsip_xfer_accept(sub, rdata, code, &hdr_list);

    }

    if (sub) {
    	//If the REFER caused subscription of the referer...
	Subscriber *subscriber = (Subscriber*)pj_pool_zalloc(dlg->pool, sizeof(Subscriber));
	subscriber->evsub = sub;
	subscriber->created_by_refer = true;
	
	long subscriber_id;
	if(!g_subscriber_ids.add((long)subscriber, subscriber_id)) {
		make_evt_internal_error(evt, sizeof(evt), "Failed to allocate id for subscriber");
		dispatch_event(evt);
		return;
	}
	subscriber->id = subscriber_id;
	pjsip_evsub_set_mod_data(sub, mod_tester.id, subscriber);

	make_evt_request(evt, sizeof(evt), "subscriber", subscriber_id, rdata->msg_info.len, rdata->msg_info.msg_buf);	
	dispatch_event(evt);    
    }
}

static void on_tsx_state_changed(pjsip_inv_session *inv,
					    pjsip_transaction *tsx,
					    pjsip_event *e)
{
    if(g_shutting_down) return;	

    char evt[2048];

    Call *call;

    if(!inv->dlg) return;

    call = (Call*) inv->dlg->mod_data[mod_tester.id];

    if (call == NULL) {
	return;
    }

    if (call->inv == NULL) {
	/* Shouldn't happen. It happens only when we don't terminate the
	 * server subscription caused by REFER after the call has been
	 * transfered (and this call has been disconnected), and we
	 * receive another REFER for this call.
	 */
	return;
    }

    //ostringstream oss;
    //Transport *t;
    if (tsx->role==PJSIP_ROLE_UAS &&
	tsx->state==PJSIP_TSX_STATE_TRYING) {
	if(pjsip_method_cmp(&tsx->method, pjsip_get_refer_method())==0) {
		/*
		 * Incoming REFER request.
		 */

		process_in_dialog_refer(call->inv->dlg, e->body.tsx_state.src.rdata);
	} else {
		make_evt_request(evt, sizeof(evt), "call", call->id, e->body.tsx_state.src.rdata->msg_info.len, e->body.tsx_state.src.rdata->msg_info.msg_buf);
    		dispatch_event(evt);    

		pjsip_dlg_respond(call->inv->dlg, e->body.tsx_state.src.rdata, 200, NULL, NULL, NULL);
	}
    }
}

//int pjw_call_refer(long call_id, const char *dest_uri, const char *additional_headers, long *out_subscription_id)
int pjw_call_refer(long call_id, const char *json, long *out_subscription_id)
{
	PJW_LOCK();
	clear_error();

    char *dest_uri;

	long val;
	Call *call;
	pj_str_t s_dest_uri;

	long subscription_id;
	struct pjsip_evsub_user xfer_cb;
	pjsip_evsub *sub;
	pjsip_tx_data *tdata;
	pj_status_t status;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"dest_uri", "headers", ""};

	if(!g_call_ids.get(call_id, val)){
		set_error("Invalid call_id");
		goto out;
	}
	call = (Call*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "dest_uri", false, &dest_uri)) {
        goto out;
    }

	pj_bzero(&xfer_cb, sizeof(xfer_cb));
	xfer_cb.on_evsub_state = &client_on_evsub_state;
	xfer_cb.on_rx_notify = &on_rx_notify;

	status = pjsip_xfer_create_uac(call->inv->dlg, &xfer_cb, &sub);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_xfer_create_uac failed with status=%i", status);
		goto out;
	}

	s_dest_uri = pj_str((char*)dest_uri);
	status = pjsip_xfer_initiate(sub, &s_dest_uri, &tdata);

	if(!add_headers(call->inv->dlg->pool, tdata, document)) {
		goto out;
	}

	status = pjsip_xfer_send_request(sub, tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_xfer_send_request failed with status=%i", status);
		goto out;
	}

	*out_subscription_id = subscription_id;

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]) {
		return -1;
	}
	return 0;
}

int pjw_call_get_info(long call_id, const char *required_info, char *out_info)
{
	PJW_LOCK();

	long val;

	char info[1000];

	if(!g_call_ids.get(call_id, val)){
		PJW_UNLOCK();
		set_error("Invalid call_id");
		return -1;
	}

	Call *call = (Call*)val;

	if(strcmp(required_info, "Call-ID") == 0) {
		strncpy(info, call->inv->dlg->call_id->id.ptr, call->inv->dlg->call_id->id.slen);
		info[call->inv->dlg->call_id->id.slen] = 0;
	} else if(strcmp(required_info, "RemoteMediaEndPoint") == 0) {
		if(!call->med_stream) {
			PJW_UNLOCK();
			set_error("Unable to get RemoteMediaEndPoint: Media not ready");
			return -1;
		}
		pjmedia_stream_info session_info;
		if(pjmedia_stream_get_info(call->med_stream, &session_info) != PJ_SUCCESS) {
			PJW_UNLOCK();
			set_error("Unable to get RemoteMediaEndPoint: call to pjmedia_stream_info failed");
			return -1;
		}	
		pj_str_t str_addr = pj_str( inet_ntoa( (in_addr&)session_info.rem_addr.ipv4.sin_addr.s_addr ) );
		pj_uint16_t port = session_info.rem_addr.ipv4.sin_port;
		sprintf(info, "%.*s:%u", (int)str_addr.slen, str_addr.ptr, ntohs(port));
	} else {
		PJW_UNLOCK();
		set_error("Unsupported info");
		return -1;
	}
	
	PJW_UNLOCK();
	strcpy(out_info, info);
	return 0;
}

bool notify(pjsip_evsub *evsub, const char *content_type, const char *body, int subscription_state, const char *reason, Document &document) {
	//pj_str_t s_content_type;
	//pj_str_t s_body;
	pj_str_t s_reason;

	char *temp;
	int bodylen;

	char *tok;

	pjsip_msg_body *msg_body;

	char *content_type_buffer;
	pj_str_t s_content_type_type;
	pj_str_t s_content_type_subtype;
	//pj_str_t s_content_type_param;

	//char *blank_string;

	pjsip_tx_data *tdata;
	pj_status_t status;

	s_reason = pj_str((char*)reason);

	status = pjsip_evsub_notify(evsub, (pjsip_evsub_state)subscription_state, NULL, &s_reason, &tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_evsub_notify failed");
		return false;
	}

	bodylen = strlen(body);
	temp = (char*)pj_pool_alloc(tdata->pool, bodylen+1);
	strcpy(temp,body);
	
	msg_body = PJ_POOL_ZALLOC_T(tdata->pool, pjsip_msg_body);

	content_type_buffer = (char*)pj_pool_alloc(tdata->pool, strlen(content_type) + 1);
	strcpy(content_type_buffer, content_type);

	tok = strtok(content_type_buffer, "/");
	if(!tok) {
		set_error("Invalid Content-Type header (while checking type)");
		return false;
	}
	s_content_type_type = pj_str(tok);

	//tok = strtok(NULL, ";");
	tok = strtok(NULL, "");
	if(!tok) {
		set_error("Invalid Content-Type header (while checking subtype)");
		return false;
	}
	s_content_type_subtype = pj_str(tok);

	if(!add_headers(tdata->pool, tdata, document)) {
        return false;
    }
	
	msg_body->content_type.type = s_content_type_type;
	msg_body->content_type.subtype = s_content_type_subtype;
	//msg_body->content_type.param = s_content_type_param;
	msg_body->data = temp;
	msg_body->len = bodylen;
	msg_body->print_body = &pjsip_print_text_body;
	msg_body->clone_data = &pjsip_clone_text_data;

	tdata->msg->body = msg_body;

	status = pjsip_evsub_send_request(evsub, tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_evsub_send_request failed");
		return false;
	}

	return true;
}

//int pjw_notify(long subscriber_id, const char *content_type, const char *body, int subscription_state, const char *reason, const char *additional_headers)
int pjw_notify(long subscriber_id, const char *json)
{
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

    const char *valid_params[] = {"content_type", "body", "subscription_state", "reason", ""};

	if(!g_subscriber_ids.get(subscriber_id, val)){
		set_error("Invalid subscriber_id");
		goto out;
	}
	subscriber = (Subscriber*)val;

	if(subscriber->created_by_refer) {
		set_error("Invalid subscriber_id: subscription was generated by REFER (use notify_xfer instead)");
		goto out;
	}

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_string_param(document, "content_type", false, &content_type)) {
        goto out;
    }

    if(!json_get_string_param(document, "body", false, &body)) {
        goto out;
    }

    if(!json_get_int_param(document, "subscription_state", false, &subscription_state)) {
        goto out;
    }

    if(!json_get_string_param(document, "reason", true, &reason)) {
        goto out;
    }

	if(!notify(subscriber->evsub, content_type, body, subscription_state, reason, document)){
		goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]) {
		return -1;
	}	
	return 0;
}


//int pjw_notify_xfer(long subscriber_id, int subscription_state, int xfer_status_code, const char *xfer_status_text) {
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

	if(!g_subscriber_ids.get(subscriber_id, val)){
		set_error("Invalid subscriber_id");
		goto out;
	}
	subscriber = (Subscriber*)val;

	if(!subscriber->created_by_refer) {
		set_error("Subscriber was not created by REFER");
		goto out;
	}

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_int_param(document, "subscription_state", false, &subscription_state)) {
        goto out;
    }

    if(!json_get_int_param(document, "code", false, &code)) {
        goto out;
    }

    if(!json_get_string_param(document, "reason", false, &reason)) {
        goto out;
    }

	r = pj_str((char*)reason);

    status = pjsip_xfer_notify( subscriber->evsub, 
                (pjsip_evsub_state)subscription_state,
                code, 
                &r,
                &tdata);
    if (status != PJ_SUCCESS) {
        set_error( "pjsip_xfer_notify failed with status=%i", status);
        goto out;
    }

    status = pjsip_xfer_send_request( subscriber->evsub, tdata);
    if (status != PJ_SUCCESS) {
        set_error("pjsip_xfer_send_request failed with status=%i", status);
        goto out;	
    }

out:
	PJW_UNLOCK();

	if(pjw_errorstring[0]) {
		return -1;
	}	

	return 0;
}

/*
pj_bool_t add_additional_headers(pj_pool_t *pool, pjsip_tx_data *tdata, const char *additional_headers) {

	if(additional_headers && additional_headers[0]){
		char buf[2048];
		strcpy(buf,additional_headers);
		char *saved;
		char *token = strtok_r(buf, "\n", &saved);
		while(token){
			char *name = strtok(token, ":");
			char *value = strtok(NULL, "\n"); 
			//addon_log(LOG_LEVEL_DEBUG, "Adding %s: %s\n", name, value);

			if(!name || !value) {
				set_error("Invalid additional_header");
				return PJ_FALSE;
			}

			pj_str_t hname = pj_str(name);
			pjsip_hdr *hdr = (pjsip_hdr*)pjsip_parse_hdr(pool,
						&hname,
						value,
						strlen(value),
						NULL);

			if(!hdr) {
				set_error("Failed to parse additional header to INVITE");
				return PJ_FALSE; 
			}					
			pjsip_hdr *clone_hdr = (pjsip_hdr*) pjsip_hdr_clone(pool, hdr);
			pjsip_msg_add_hdr(tdata->msg, clone_hdr); 

			token = strtok_r(NULL, "\n", &saved);
		}
	}
	return PJ_TRUE;
}
*/


pj_bool_t add_headers(pj_pool_t *pool, pjsip_tx_data *tdata, Document &document) {
    if(!document.HasMember("headers")) {
        return PJ_TRUE;
    }

    if(!document["headers"].IsObject()) {
        set_error("Parameter headers must be an object");
        return PJ_FALSE;
    }

    Value headers = document["headers"].GetObject();

    for (Value::ConstMemberIterator itr = headers.MemberBegin(); itr != headers.MemberEnd(); ++itr) {
        printf("%s => '%s'\n", itr->name.GetString(), itr->value.GetString());

        const char *name = itr->name.GetString();
        if(!itr->value.IsString()) {
            set_error("Parameter headers key '%s' found with non-string value", name);
            return PJ_FALSE;
        }

        const char *value = itr->value.GetString();

        pj_str_t hname = pj_str((char*)name);
        pjsip_hdr *hdr = (pjsip_hdr*)pjsip_parse_hdr(pool,
                    &hname,
                    (char*)value,
                    strlen(value),
                    NULL);

        if(!hdr) {
            set_error("Failed to parse header '%s' => '%s'", name, value);
            return PJ_FALSE; 
        }					
        pjsip_hdr *clone_hdr = (pjsip_hdr*) pjsip_hdr_clone(pool, hdr);
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

    if(!document.HasMember("headers")) {
        return PJ_TRUE;
    }

    if(!document["headers"].IsObject()) {
        set_error("Parameter headers must be an object");
        return PJ_FALSE;
    }

    Value headers = document["headers"].GetObject();

    for (Value::ConstMemberIterator itr = headers.MemberBegin(); itr != headers.MemberEnd(); ++itr) {
        printf("%s => '%s'\n", itr->name.GetString(), itr->value.GetString());

        const char *name = itr->name.GetString();
        if(!itr->value.IsString()) {
            set_error("Parameter headers key '%s' found with non-string value", name);
            return PJ_FALSE;
        }

        const char *value = itr->value.GetString();

        pj_str_t hname = pj_str((char*)name);
        pjsip_hdr *hdr = (pjsip_hdr*)pjsip_parse_hdr(pool,
                    &hname,
                    (char*)value,
                    strlen(value),
                    NULL);

        if(!hdr) {
            set_error("Failed to parse header %s", name);
            return PJ_FALSE; 
        }					
        
        pj_list_push_back(&hdr_list, hdr);
	}

	pjsip_regc_add_headers(regc, &hdr_list);
	return PJ_TRUE;
}

pj_bool_t get_content_type_and_subtype_from_headers(Document &document, char *type, char *subtype) {
    if(!document.HasMember("headers")) {
		set_error("Parameter headers absent");
        return PJ_FALSE;
    }

    if(!document["headers"].IsObject()) {
        set_error("Parameter headers must be an object");
        return PJ_FALSE;
    }

    Value headers = document["headers"].GetObject();

    if(!headers.HasMember("Content-Type")) {
        set_error("Parameter headers doesn't contain key Content-Type");
        return PJ_FALSE;
    }

    const char *content_type = headers["Content-Type"].GetString();

    const char *slash;
    int index;

    slash = strchr(content_type, '/');
    if(!slash) {
	    set_error("Invalid header Content-Type");
        return PJ_FALSE;
    }

    index = (int)(slash - content_type);

    strncpy(type, content_type, index-1);
    strcpy(subtype, content_type+index);
    addon_log(LOG_LEVEL_DEBUG, "Checking parsing of Content-Type. type=%s: subtype=%s\n", type, subtype);
    return PJ_TRUE;
}



bool register_pkg(const char *event, const char *accept) {
	PackageSet::iterator iter = g_PackageSet.find( make_pair( string(event), string(accept) ) );
	if(g_PackageSet.end() == iter) {
		g_PackageSet.insert( make_pair( string(event), string(accept) ) );
		pj_status_t status;
		pj_str_t s_event = pj_str((char*)event);
		pj_str_t s_accept = pj_str((char*)accept);
		status = pjsip_evsub_register_pkg(&mod_tester, &s_event, DEFAULT_EXPIRES, 1, &s_accept);
		if(status != PJ_SUCCESS) {
			set_error("pjsip_evsub_register_pkg failed");
			return false;
		}
	}
	return true;
}

int pjw_register_pkg(const char *event, const char *accept) {
	PJW_LOCK();

	clear_error();

	//int n;

	if(!register_pkg(event, accept)) {
		goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]) {
		return -1;
	}

	return 0;
}

//int pjw_subscription_create(long transport_id, const char *event, const char *accept, const char *from_uri, const char *to_uri, const char *request_uri, const char *proxy_uri, const char *realm, const char *username, const char *password, long *out_subscription_id) {
int pjw_subscription_create(long transport_id, const char *json, long *out_subscription_id) {
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
	//char *start;

	pjsip_dialog *dlg = NULL;
	pjsip_evsub *evsub = NULL;
	pjsip_evsub_user user_cb;

	pj_str_t s_event;

	pj_status_t status;

    char buffer[MAX_JSON_INPUT];

    Document document;

    const char *valid_params[] = {"event", "accept", "from_uri", "to_uri", "request_uri", "proxy_uri", "auth", ""};

	if(!g_transport_ids.get(transport_id, val)){
		set_error("Invalid transport_id");
		goto out;
	}
	t = (Transport*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_string_param(document, "event", false, &event)) {
        goto out;
    }

    if(!json_get_string_param(document, "accept", false, &accept)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "from_uri", false, &from_uri)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "to_uri", false, &to_uri)) {
        goto out;
    }

    request_uri = to_uri;
    if(!json_get_and_check_uri(document, "request_uri", true, &request_uri)) {
        goto out;
    }

    if(!json_get_and_check_uri(document, "proxy_uri", true, &proxy_uri)) {
        goto out;
    }

    if(document.HasMember("auth")) {
        if(!document["auth"].IsObject()) {
            set_error("Parameter auth must be an object");
            goto out;
        } else {
            const Value& auth = document["auth"];

            for (Value::ConstMemberIterator itr = auth.MemberBegin(); itr != auth.MemberEnd(); ++itr) {
                const char *name = itr->name.GetString();
                if(strcmp(name, "realm") == 0) {
                    if(!itr->value.IsString()) {
                        set_error("%s must be a string", itr->name.GetString());
                        goto out;
                    }
                    realm = (char*)itr->value.GetString();
                } else if(strcmp(name, "username") == 0) {
                    if(!itr->value.IsString()) {
                        set_error("%s must be a string", itr->name.GetString());
                        goto out;
                    }
                    username = (char*)itr->value.GetString();
                    contact_username = username;
                } else if(strcmp(name, "password") == 0) {
                    if(!itr->value.IsString()) {
                        set_error("%s must be a string", itr->name.GetString());
                        goto out;
                    }
                    password = (char*)itr->value.GetString();
                } else {
                    set_error("Unknown auth paramter %s", itr->name.GetString());
                    goto out;
                }
            }
        }
    }

	build_local_contact(local_contact, t->sip_transport, contact_username);

	if(!dlg_create(&dlg, t, from_uri, to_uri, request_uri, realm, username, password, local_contact)) {
		goto out;
	}

	if(!register_pkg(event, accept)) {
		goto out;
	}

	memset(&user_cb, 0, sizeof(user_cb));
	user_cb.on_evsub_state = client_on_evsub_state;
	user_cb.on_client_refresh = on_client_refresh;
	user_cb.on_rx_notify = on_rx_notify;

	s_event = pj_str((char*)event);

	status = pjsip_evsub_create_uac(dlg,
					&user_cb,
					&s_event,
					PJSIP_EVSUB_NO_EVENT_ID,
					&evsub);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_evsub_create_uac failed with status=%i", status);
		goto out;
	}	

	if(!dlg_set_transport_by_t(t, dlg)){
		goto out;
	}

	if(!set_proxy(dlg, proxy_uri)) {
		goto out;
	}

	subscription = (Subscription*)pj_pool_zalloc(dlg->pool, sizeof(Subscription));
	
	if(!g_subscription_ids.add((long)subscription, subscription_id)){
		status = pjsip_dlg_terminate(dlg); //ToDo:
		set_error("Failed to allocate id");
		goto out;
	}
	subscription->id = subscription_id;
	subscription->evsub = evsub;
	subscription->dlg = dlg;
	strcpy(subscription->event, event);
	strcpy(subscription->accept, accept);
	pjsip_evsub_set_mod_data(evsub, mod_tester.id, subscription);
	
	*out_subscription_id = subscription_id;
out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]){
		return -1;
	}
	return 0;
}

bool subscription_subscribe_no_headers(Subscription *s, int expires) {
	pj_status_t status;
	pjsip_tx_data *tdata;

	status = pjsip_evsub_initiate(s->evsub, 
					NULL,
					expires,	
					&tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_evsub_initiate failed");
		return false;	
	}

	status = pjsip_evsub_send_request(s->evsub, tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_inv_send_msg failed");
		return false;	
	}

	//Without this, on_rx_response will not be called
	status = pjsip_dlg_add_usage(s->dlg, &mod_tester, s);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_dlg_add_usage failed");
		return false;	
	}

	return true;
}


bool subscription_subscribe(Subscription *s, int expires, Document &document) {
	pj_status_t status;
	pjsip_tx_data *tdata;

	status = pjsip_evsub_initiate(s->evsub, 
					NULL,
					expires,	
					&tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_evsub_initiate failed");
		return false;	
	}

	if(!add_headers(s->dlg->pool, tdata, document)) {
		return false;
	}

	status = pjsip_evsub_send_request(s->evsub, tdata);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_inv_send_msg failed");
		return false;	
	}

	//Without this, on_rx_response will not be called
	status = pjsip_dlg_add_usage(s->dlg, &mod_tester, s);
	if(status != PJ_SUCCESS) {
		set_error("pjsip_dlg_add_usage failed");
		return false;	
	}

	return true;
}

//int pjw_subscription_subscribe(long subscription_id, int expires, const char *additional_headers) {
int pjw_subscription_subscribe(long subscription_id, const char *json) {
	PJW_LOCK();
	clear_error();

    int expires;

	Subscription *subscription;

	long val;

    char buffer[MAX_JSON_INPUT];

    Document document;
    
    const char *valid_params[] = {"expires", "headers", ""};

	if(!g_subscription_ids.get(subscription_id, val)){
		set_error("Invalid subscription_id");
		goto out;
	}
	subscription = (Subscription*)val;

    if(!parse_json(document, json, buffer, MAX_JSON_INPUT)) {
        goto out;
    }

    if(!validate_params(document, valid_params)) {
        goto out;
    }

    if(!json_get_int_param(document, "expires", true, &expires)) {
        goto out;
    }

	if(!subscription_subscribe(subscription, expires, document)) {
		goto out;
	}

out:
	PJW_UNLOCK();
	if(pjw_errorstring[0]) {
		return -1;
	}
	return 0;
}

void process_in_dialog_subscribe(pjsip_dialog *dlg, pjsip_rx_data *rdata) {
	char evt[2048];

	Subscriber *s;
	//Transport *t;

	s = (Subscriber*)dlg->mod_data[mod_tester.id];
	if(!s) {
		make_evt_internal_error(evt, sizeof(evt), "no subscriber in mod_data");
		dispatch_event(evt);
		return;
	}

	make_evt_request(evt, sizeof(evt), "subscriber", s->id, rdata->msg_info.len, rdata->msg_info.msg_buf);
	dispatch_event(evt);
}

int pjw_call_gen_string_replaces(long call_id, char *out_replaces) {
	PJW_LOCK();

	clear_error();

	//int n;
	long val;
	Call *call;
	pjsip_dialog *dlg;
	//int len;
	char *p;
	char buf[2000];
	pjsip_uri* uri;
	
	if(!g_call_ids.get(call_id, val)) {
		set_error("Invalid call_id");
		goto out;
	}

	call = (Call*)val;

	p = buf;
	p += sprintf(p, "%s", "<");

	dlg = call->inv->dlg;
		
	uri = (pjsip_uri*)pjsip_uri_get_uri(dlg->remote.info->uri);
	p += pjsip_uri_print(PJSIP_URI_IN_REQ_URI, uri, p, buf-p);
	p += pj_ansi_sprintf(p, 
				"%.*s"
				";to-tag=%.*s"
				";from-tag=%.*s",		
				(int)dlg->call_id->id.slen,
				dlg->call_id->id.ptr,
				(int)dlg->remote.info->tag.slen,
				dlg->remote.info->tag.ptr,
				(int)dlg->local.info->tag.slen,
				dlg->local.info->tag.ptr);

out:
	PJW_UNLOCK();

	if(pjw_errorstring[0]) {
		return -1;
	}
	strcpy(out_replaces, buf);
	return 0;
}	
	

int pjw_log_level(long log_level)
{
	PJW_LOCK();

	pj_log_set_level(log_level);
	
	PJW_UNLOCK();
	return 0;
}

int pjw_set_flags(unsigned flags)
{
	PJW_LOCK();

	g_flags = flags;

	PJW_UNLOCK();
	return 0;
}

static int g_now;


void check_digit_buffer(Call *c, int mode) {
	//addon_log(LOG_LEVEL_DEBUG, "check_digit_buffer g_now=%i for call_id=%i and mode=%i timestamp=%i len=%i\n", g_now, c->id, mode, c->last_digit_timestamp[mode], c->DigitBufferLength[mode]);
	char evt[256];

	if(c->last_digit_timestamp[mode] > 0 && g_now - c->last_digit_timestamp[mode] > g_dtmf_inter_digit_timer) {
		int *pLen = &c->DigitBufferLength[mode];
		c->DigitBuffers[mode][*pLen] = 0;
		make_evt_dtmf(evt, sizeof(evt), c->id, *pLen, c->DigitBuffers[mode], mode); 
		dispatch_event(evt);
		*pLen = 0;
		c->last_digit_timestamp[mode] = 0;
	}
}

void check_digit_buffers(long id, long val) {
	Call *c = (Call*)val;

	check_digit_buffer(c, DTMF_MODE_RFC2833);	
	check_digit_buffer(c, DTMF_MODE_INBAND);	
}

static int digit_buffer_thread(void *arg)
{
	//addon_log(LOG_LEVEL_DEBUG, "Starting digit_buffer_thread\n");

	pj_thread_set_prio(pj_thread_this(),
			pj_thread_get_prio_min(pj_thread_this()));

	PJ_UNUSED_ARG(arg);

	while(!g_shutting_down){
		PJW_LOCK();
		if(g_dtmf_inter_digit_timer > 0) {
			g_now = ms_timestamp();
			g_call_ids.iterate(check_digit_buffers);
		}
		PJW_UNLOCK();

		pj_thread_sleep(100);
	}
	return 0;
}


bool start_digit_buffer_thread(){
	pj_status_t status;
	pj_pool_t *pool = pj_pool_create(&cp.factory, "digit_buffer_checker", 1000, 1000, NULL);
	pj_thread_t *t;
	status = pj_thread_create(pool, "digit_buffer_checker", &digit_buffer_thread, NULL, 0, 0, &t);
	if(status != PJ_SUCCESS)
	{
		addon_log(LOG_LEVEL_DEBUG, "start_digit_buffer_thread failed\n");
		return false;
	}

	return true;
}
	
/* provides timestamp in milliseconds */
int ms_timestamp() {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
    {
            return -1;
    }
    return ((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
}

int pjw_dtmf_aggregation_on(int inter_digit_timer)
{
	PJW_LOCK();

	if(inter_digit_timer <= 0)
	{
		PJW_UNLOCK();
		set_error("Invalid argument: inter_digit_timer must be greater than zero");
		return -1;
	}

	g_dtmf_inter_digit_timer = inter_digit_timer;

	PJW_UNLOCK();
	return 0;
}

int pjw_dtmf_aggregation_off()
{
	PJW_LOCK();

	g_dtmf_inter_digit_timer = 0;

	PJW_UNLOCK();
	return 0;
}



