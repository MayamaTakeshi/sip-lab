/* 
 * Copyright (C) 2024 MayamaTakeshi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjsip/sip_transport_ws.h>
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_errno.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <http.h>

#define THIS_FILE       "sip_transport_ws.c"

#define POOL_INIT       512
#define POOL_INC        512
#define POOL_RDATA_INIT PJSIP_POOL_RDATA_LEN
#define POOL_RDATA_INC  PJSIP_POOL_RDATA_INC

/* Global transport type IDs */
pjsip_transport_type_e PJSIP_TRANSPORT_WS;
pjsip_transport_type_e PJSIP_TRANSPORT_WSS;

/* Forward declarations */
struct ws_listener;
struct ws_transport;

/*
 * WebSocket listener (server mode). Each call to pjsip_ws_transport_start()
 * creates one listener.
 */
struct ws_listener
{
    PJ_DECL_LIST_MEMBER(struct ws_listener);
    pj_pool_t          *pool;
    pjsip_endpoint     *endpt;
    pj_websock_t       *listener_ws;
    pjsip_transport    *base;
    pj_bool_t           is_wss;
    pjsip_transport_type_e type_id;
};

/*
 * WebSocket transport for an individual WS connection.
 */
struct ws_transport
{
    pjsip_transport     base;
    pj_pool_t          *pool;
    pj_pool_t          *pool_rdata;
    pj_websock_t       *ws;
    pj_sockaddr         remote_addr;
    pj_bool_t           is_connected;
    pj_bool_t           is_wss;
    pj_bool_t           has_connect_result;
    pj_status_t         connect_status;
};

/* Global linked list of active listeners */
static struct ws_listener ws_listeners;

/* Prototypes */
static pj_status_t ws_send_msg(pjsip_transport *transport,
                                pjsip_tx_data *tdata,
                                const pj_sockaddr_t *rem_addr,
                                int addr_len,
                                void *token,
                                pjsip_transport_callback callback);
static pj_status_t ws_do_shutdown(pjsip_transport *transport);
static pj_status_t ws_destroy(pjsip_transport *transport);

static pj_bool_t on_ws_accept_complete(pj_websock_t *c,
                                        const pj_sockaddr_t *src_addr,
                                        int src_addr_len);
static pj_bool_t on_ws_connect_complete(pj_websock_t *c,
                                         pj_status_t status);
static pj_bool_t on_ws_rx_msg(pj_websock_t *c,
                               pj_websock_rx_data *msg,
                               pj_status_t status);
static pj_bool_t on_ws_tx_msg(pj_websock_t *c,
                               pj_websock_tx_data *msg,
                               pj_ssize_t sent);
static void on_ws_state_change(pj_websock_t *c, int state);

static void ws_perror(const char *sender, const char *title,
                       pj_status_t status)
{
    PJ_PERROR(3, (sender, status, "%s", title));
}

/* Listener transport stub callbacks */
static pj_status_t lis_tp_send_msg(pjsip_transport *transport,
                                    pjsip_tx_data *tdata,
                                    const pj_sockaddr_t *rem_addr,
                                    int addr_len,
                                    void *token,
                                    pjsip_transport_callback callback)
{
    PJ_UNUSED_ARG(transport);
    PJ_UNUSED_ARG(tdata);
    PJ_UNUSED_ARG(rem_addr);
    PJ_UNUSED_ARG(addr_len);
    PJ_UNUSED_ARG(token);
    PJ_UNUSED_ARG(callback);
    return PJ_RETURN_OS_ERROR(PJ_ENOTSUP);
}

static pj_status_t lis_tp_shutdown(pjsip_transport *transport)
{
    PJ_UNUSED_ARG(transport);
    return PJ_SUCCESS;
}

static pj_status_t lis_tp_destroy(pjsip_transport *transport)
{
    if (transport->pool)
        pjsip_endpt_release_pool(transport->endpt, transport->pool);
    return PJ_SUCCESS;
}

/*
 * Initialize WS transport module.
 */
PJ_DEF(pj_status_t) pjsip_ws_transport_init(void)
{
    pj_status_t status;

    pj_list_init(&ws_listeners);

    status = pjsip_transport_register_type(PJSIP_TRANSPORT_RELIABLE,
                                           "WS", 5060,
                                           &PJSIP_TRANSPORT_WS);
    if (status != PJ_SUCCESS)
        return status;

    status = pjsip_transport_register_type(
                PJSIP_TRANSPORT_RELIABLE | PJSIP_TRANSPORT_SECURE,
                "WSS", 5061,
                &PJSIP_TRANSPORT_WSS);

    return status;
}

/****************************************************************************
 * WS Transport operations
 */

static void sockaddr_to_host_port(pj_pool_t *pool,
                                   pjsip_host_port *host_port,
                                   const pj_sockaddr *addr)
{
    host_port->host.ptr = (char*)pj_pool_alloc(pool, PJ_INET6_ADDRSTRLEN + 4);
    pj_sockaddr_print(addr, host_port->host.ptr, PJ_INET6_ADDRSTRLEN + 4, 0);
    host_port->host.slen = (int)pj_ansi_strlen(host_port->host.ptr);
    host_port->port = pj_sockaddr_get_port(addr);
}

static void ws_init_shutdown(struct ws_transport *ws_tp, pj_status_t status)
{
    pjsip_tp_state_callback state_cb;

    if (ws_tp->base.is_shutdown || ws_tp->base.is_destroying)
        return;

    pjsip_transport_add_ref(&ws_tp->base);

    state_cb = pjsip_tpmgr_get_state_cb(ws_tp->base.tpmgr);
    if (state_cb) {
        pjsip_transport_state_info state_info;
        pj_bzero(&state_info, sizeof(state_info));
        state_info.status = status;
        (*state_cb)(&ws_tp->base, PJSIP_TP_STATE_DISCONNECTED, &state_info);
    }

    if (ws_tp->base.is_shutdown || ws_tp->base.is_destroying) {
        pjsip_transport_dec_ref(&ws_tp->base);
        return;
    }

    pjsip_transport_shutdown(&ws_tp->base);
    pjsip_transport_dec_ref(&ws_tp->base);
}

static pj_status_t ws_send_msg(pjsip_transport *transport,
                                pjsip_tx_data *tdata,
                                const pj_sockaddr_t *rem_addr,
                                int addr_len,
                                void *token,
                                pjsip_transport_callback callback)
{
    struct ws_transport *ws_tp = (struct ws_transport *)transport;

    PJ_UNUSED_ARG(rem_addr);
    PJ_UNUSED_ARG(addr_len);
    PJ_UNUSED_ARG(token);
    PJ_UNUSED_ARG(callback);

    if (!ws_tp->is_connected)
        return PJ_RETURN_OS_ERROR(OSERR_ENOTCONN);

    pj_ssize_t size = tdata->buf.cur - tdata->buf.start;
    pj_status_t status = pj_websock_send(ws_tp->ws,
                                          PJ_WEBSOCK_OP_TEXT,
                                          PJ_TRUE, PJ_TRUE,
                                          tdata->buf.start, size);
    if (status == PJ_SUCCESS || status == PJ_EPENDING)
        return PJ_SUCCESS;
    return status;
}

static pj_status_t ws_do_shutdown(pjsip_transport *transport)
{
    struct ws_transport *ws_tp = (struct ws_transport *)transport;

    if (ws_tp->ws) {
        pj_websock_close(ws_tp->ws, PJ_WEBSOCK_SC_NORMAL_CLOSURE, NULL);
        ws_tp->ws = NULL;
    }
    return PJ_SUCCESS;
}

static pj_status_t ws_destroy(pjsip_transport *transport)
{
    struct ws_transport *ws_tp = (struct ws_transport *)transport;

    if (ws_tp->ws) {
        pj_websock_close(ws_tp->ws, PJ_WEBSOCK_SC_NORMAL_CLOSURE, NULL);
        ws_tp->ws = NULL;
    }
    if (ws_tp->pool_rdata) {
        pjsip_endpt_release_pool(ws_tp->base.endpt, ws_tp->pool_rdata);
        ws_tp->pool_rdata = NULL;
    }
    if (ws_tp->pool) {
        pj_pool_t *pool = ws_tp->pool;
        ws_tp->pool = NULL;
        pjsip_endpt_release_pool(ws_tp->base.endpt, pool);
    }
    return PJ_SUCCESS;
}

/****************************************************************************
 * WebSocket callbacks
 */

static pj_bool_t on_ws_connect_complete(pj_websock_t *c, pj_status_t status)
{
    struct ws_transport *ws_tp = (struct ws_transport *)pj_websock_get_userdata(c);
    if (!ws_tp)
        return PJ_TRUE;

    ws_tp->has_connect_result = PJ_TRUE;
    if (status == PJ_SUCCESS) {
        ws_tp->is_connected = PJ_TRUE;
        PJ_LOG(4, (ws_tp->base.obj_name, "WebSocket connection established"));
    } else {
        ws_tp->ws = NULL;
        ws_tp->connect_status = status;
        ws_perror(ws_tp->base.obj_name, "WebSocket connection failed", status);
        ws_init_shutdown(ws_tp, status);
    }
    return PJ_TRUE;
}

static pj_bool_t on_ws_rx_msg(pj_websock_t *c, pj_websock_rx_data *msg,
                               pj_status_t status)
{
    struct ws_transport *ws_tp = (struct ws_transport *)pj_websock_get_userdata(c);
    if (!ws_tp)
        return PJ_FALSE;

    if (status != PJ_SUCCESS || msg->hdr.opcode == PJ_WEBSOCK_OP_CLOSE) {
        ws_tp->is_connected = PJ_FALSE;
        ws_tp->ws = NULL;
        pjsip_transport_shutdown(&ws_tp->base);
        return PJ_FALSE;
    }

    if (msg->hdr.opcode == PJ_WEBSOCK_OP_TEXT ||
        msg->hdr.opcode == PJ_WEBSOCK_OP_BIN)
    {
        char *data = (char *)msg->data;
        pj_size_t len = (pj_size_t)msg->data_len;

        if (len >= PJSIP_MAX_PKT_LEN) {
            PJ_LOG(4, (ws_tp->base.obj_name,
                       "WS recv buffer overflow, truncating"));
            len = PJSIP_MAX_PKT_LEN - 1;
        }

        pj_pool_reset(ws_tp->pool_rdata);
        pjsip_rx_data *rdata = PJ_POOL_ZALLOC_T(ws_tp->pool_rdata,
                                                 pjsip_rx_data);

        rdata->tp_info.pool = ws_tp->pool_rdata;
        rdata->tp_info.transport = &ws_tp->base;
        rdata->tp_info.tp_data = NULL;
        rdata->tp_info.op_key.rdata = rdata;
        pj_ioqueue_op_key_init(&rdata->tp_info.op_key.op_key,
                                sizeof(pj_ioqueue_op_key_t));

        rdata->pkt_info.len = (int)len;
        pj_memcpy(rdata->pkt_info.packet, data, len);
        rdata->pkt_info.zero = 0;
        pj_gettimeofday(&rdata->pkt_info.timestamp);
        pj_sockaddr_cp(&rdata->pkt_info.src_addr, &ws_tp->remote_addr);
        rdata->pkt_info.src_addr_len = sizeof(pj_sockaddr);
        pj_sockaddr_print(&rdata->pkt_info.src_addr,
                          rdata->pkt_info.src_name,
                          sizeof(rdata->pkt_info.src_name), 0);
        rdata->pkt_info.src_port = pj_sockaddr_get_port(
                                        &rdata->pkt_info.src_addr);
        rdata->tp_info.transport = &ws_tp->base;
        pjsip_tpmgr_receive_packet(ws_tp->base.tpmgr, rdata);
    } else if (msg->hdr.opcode == PJ_WEBSOCK_OP_PING) {
        pj_websock_send(c, PJ_WEBSOCK_OP_PONG, PJ_TRUE, PJ_TRUE, NULL, 0);
    }

    return PJ_TRUE;
}

static pj_bool_t on_ws_tx_msg(pj_websock_t *c, pj_websock_tx_data *msg,
                               pj_ssize_t sent)
{
    PJ_UNUSED_ARG(c);
    PJ_UNUSED_ARG(msg);
    PJ_UNUSED_ARG(sent);
    return PJ_TRUE;
}

static void on_ws_state_change(pj_websock_t *c, int state)
{
    if (state == PJ_WEBSOCK_STATE_CLOSED ||
        state == PJ_WEBSOCK_STATE_CLOSING)
    {
        struct ws_transport *ws_tp = (struct ws_transport *)
                                        pj_websock_get_userdata(c);
        if (ws_tp) {
            pjsip_transport_shutdown(&ws_tp->base);
        }
    }
}

/****************************************************************************
 * Create WS transport (called from accept and connect completion)
 */

static pj_status_t ws_create_transport(
                        struct ws_listener *listener,
                        pj_pool_t *pool,
                        pj_websock_t *ws,
                        pj_bool_t is_connected,
                        pj_bool_t is_server,
                        const pj_sockaddr *remote_addr,
                        const pj_sockaddr *local_addr,
                        int local_port,
                        const pj_str_t *local_host)
{
    struct ws_transport *ws_tp;
    pj_status_t status;

    if (pool == NULL) {
        return PJ_EINVAL;
    }

    ws_tp = PJ_POOL_ZALLOC_T(pool, struct ws_transport);
    ws_tp->pool = pool;
    ws_tp->ws = ws;
    ws_tp->is_connected = is_connected;
    ws_tp->remote_addr = *remote_addr;

    ws_tp->pool_rdata = pjsip_endpt_create_pool(
                            listener->endpt,
                            "ws_rdata%p",
                            POOL_RDATA_INIT,
                            POOL_RDATA_INC);
    if (!ws_tp->pool_rdata) {
        return PJ_ENOMEM;
    }

    pj_ansi_snprintf(ws_tp->base.obj_name, sizeof(ws_tp->base.obj_name),
                     is_server ? "ws_srv:%p" : "ws_cli:%p", ws_tp);
    ws_tp->base.pool = pool;
    ws_tp->base.endpt = listener->endpt;
    ws_tp->base.tpmgr = pjsip_endpt_get_tpmgr(listener->endpt);
    ws_tp->base.send_msg = &ws_send_msg;
    ws_tp->base.do_shutdown = &ws_do_shutdown;
    ws_tp->base.destroy = &ws_destroy;

    ws_tp->is_wss = listener->is_wss;
    ws_tp->base.type_name = (char*)(listener->is_wss ? "WSS" : "WS");
    ws_tp->base.flag = PJSIP_TRANSPORT_RELIABLE |
                       (listener->is_wss ? PJSIP_TRANSPORT_SECURE : 0);
    ws_tp->base.info = (char*)"WebSocket Transport";
    ws_tp->base.key.type = listener->type_id;

    pj_bzero(&ws_tp->base.key.rem_addr, sizeof(pj_sockaddr));
    pj_sockaddr_cp(&ws_tp->base.key.rem_addr, remote_addr);
    ws_tp->base.addr_len = sizeof(pj_sockaddr);

    pj_bzero(&ws_tp->base.local_addr, sizeof(pj_sockaddr));
    pj_sockaddr_cp(&ws_tp->base.local_addr, local_addr);

    /* Local name */
    pj_strdup(pool, &ws_tp->base.local_name, local_host);
    ws_tp->base.local_name.port = local_port;

    /* Remote name */
    {
        char remote_buf[PJ_INET6_ADDRSTRLEN];
        pj_sockaddr_print(remote_addr, remote_buf,
                          sizeof(remote_buf), 0);
        ws_tp->base.remote_name.host = pj_str(
                                (char*)pj_pool_alloc(pool, 
                                                     PJ_INET6_ADDRSTRLEN));
        pj_strcpy2(&ws_tp->base.remote_name.host, remote_buf);
        ws_tp->base.remote_name.port = pj_sockaddr_get_port(remote_addr);
    }

    ws_tp->base.dir = is_server ? PJSIP_TP_DIR_INCOMING :
                                   PJSIP_TP_DIR_OUTGOING;

    status = pj_lock_create_recursive_mutex(pool, "ws",
                                            &ws_tp->base.lock);
    if (status != PJ_SUCCESS) {
        if (ws_tp->pool_rdata) {
            pjsip_endpt_release_pool(listener->endpt, ws_tp->pool_rdata);
            ws_tp->pool_rdata = NULL;
        }
        return status;
    }

    status = pj_atomic_create(pool, 0, &ws_tp->base.ref_cnt);
    if (status != PJ_SUCCESS) {
        pj_lock_destroy(ws_tp->base.lock);
        ws_tp->base.lock = NULL;
        if (ws_tp->pool_rdata) {
            pjsip_endpt_release_pool(listener->endpt, ws_tp->pool_rdata);
            ws_tp->pool_rdata = NULL;
        }
        return status;
    }

    status = pjsip_transport_register(ws_tp->base.tpmgr, &ws_tp->base);
    if (status != PJ_SUCCESS) {
        pj_atomic_destroy(ws_tp->base.ref_cnt);
        ws_tp->base.ref_cnt = NULL;
        pj_lock_destroy(ws_tp->base.lock);
        ws_tp->base.lock = NULL;
        if (ws_tp->pool_rdata) {
            pjsip_endpt_release_pool(listener->endpt, ws_tp->pool_rdata);
            ws_tp->pool_rdata = NULL;
        }
        return status;
    }

    pj_websock_set_userdata(ws, ws_tp);

    return PJ_SUCCESS;
}

/****************************************************************************
 * Accept completion callback
 */

static pj_bool_t on_ws_accept_complete(pj_websock_t *c,
                                        const pj_sockaddr_t *src_addr,
                                        int src_addr_len)
{
    pj_websock_t *parent = pj_websock_get_parent(c);
    if (!parent)
        return PJ_TRUE;

    struct ws_listener *listener = (struct ws_listener *)
                                    pj_websock_get_userdata(parent);
    if (!listener)
        return PJ_TRUE;

    pj_pool_t *pool = pjsip_endpt_create_pool(listener->endpt,
                                               "ws_srv_tp%p",
                                               POOL_INIT, POOL_INC);
    if (!pool)
        return PJ_TRUE;

    /* Build local address from listener */
    pj_sockaddr local_addr;
    pj_sockaddr_cp(&local_addr, &listener->base->local_addr);

    /* src_addr holds the remote address of the accepted connection */
    pj_sockaddr tmp_src;
    pj_bzero(&tmp_src, sizeof(tmp_src));
    pj_sockaddr_cp(&tmp_src, src_addr);
    PJ_UNUSED_ARG(src_addr_len);

    pj_status_t status = ws_create_transport(
                            listener, pool, c, PJ_TRUE, PJ_TRUE,
                            &tmp_src, &local_addr,
                            listener->base->local_name.port,
                            &listener->base->local_name.host);
    if (status != PJ_SUCCESS) {
        pjsip_endpt_release_pool(listener->endpt, pool);
        return PJ_TRUE;
    }

    /* The ws_create_transport sets ws_tp as userdata on c */
    struct ws_transport *ws_tp = (struct ws_transport *)
                                    pj_websock_get_userdata(c);

    /* Set up callbacks for the accepted connection */
    pj_websock_cb child_cb;
    pj_bzero(&child_cb, sizeof(child_cb));
    child_cb.on_rx_msg = &on_ws_rx_msg;
    child_cb.on_tx_msg = &on_ws_tx_msg;
    child_cb.on_state_change = &on_ws_state_change;
    pj_websock_set_callbacks(c, &child_cb);

    PJ_LOG(4, (listener->base->obj_name,
               "WebSocket incoming connection accepted, "
               "transport=%s", ws_tp->base.obj_name));

    return PJ_TRUE;
}

/****************************************************************************
 * Public API
 */

PJ_DEF(pj_status_t) pjsip_ws_transport_start(
                        pjsip_endpoint *endpt,
                        pj_websock_endpoint *ws_endpt,
                        const pj_sockaddr *bind_addr,
                        pj_bool_t secure,
                        pjsip_transport **p_listener_tp)
{
    pj_pool_t *pool;
    struct ws_listener *listener;
    pj_status_t status;

    PJ_ASSERT_RETURN(endpt && ws_endpt && bind_addr, PJ_EINVAL);

    pool = pjsip_endpt_create_pool(endpt, "ws_lis%p",
                                    POOL_INIT, POOL_INC);
    PJ_ASSERT_RETURN(pool, PJ_ENOMEM);

    listener = PJ_POOL_ZALLOC_T(pool, struct ws_listener);
    listener->pool = pool;
    listener->endpt = endpt;
    listener->is_wss = secure;
    listener->type_id = secure ? PJSIP_TRANSPORT_WSS :
                                 PJSIP_TRANSPORT_WS;

    /* Setup listener callbacks */
    pj_websock_cb ws_cb;
    pj_bzero(&ws_cb, sizeof(ws_cb));
    ws_cb.on_accept_complete = &on_ws_accept_complete;

    /* Start listening */
    status = pj_websock_listen(ws_endpt,
                secure ? PJ_WEBSOCK_TRANSPORT_TLS :
                         PJ_WEBSOCK_TRANSPORT_TCP,
                bind_addr, &ws_cb, listener, &listener->listener_ws);
    if (status != PJ_SUCCESS) {
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    /* Set allowed sub-protocol to "sip" */
    {
        pj_str_t sip_proto = pj_str((char*)"sip");
        pj_websock_set_support_subproto(listener->listener_ws,
                                        &sip_proto, 1);
    }

    /* Create a listener transport for PJSIP transport matching */
    pj_pool_t *lis_tp_pool = pjsip_endpt_create_pool(endpt,
                                                      "ws_lstnr%p",
                                                      POOL_INIT, POOL_INC);
    if (!lis_tp_pool) {
        pj_websock_close(listener->listener_ws,
                         PJ_WEBSOCK_SC_NORMAL_CLOSURE, NULL);
        pjsip_endpt_release_pool(endpt, pool);
        return PJ_ENOMEM;
    }

    pjsip_transport *lis_tp = PJ_POOL_ZALLOC_T(lis_tp_pool,
                                                pjsip_transport);
    lis_tp->pool = lis_tp_pool;
    lis_tp->endpt = endpt;
    lis_tp->tpmgr = pjsip_endpt_get_tpmgr(endpt);

    lis_tp->send_msg = &lis_tp_send_msg;
    lis_tp->do_shutdown = &lis_tp_shutdown;
    lis_tp->destroy = &lis_tp_destroy;

    status = pj_lock_create_recursive_mutex(lis_tp_pool, "ws_lstnr",
                                            &lis_tp->lock);
    if (status != PJ_SUCCESS) {
        pjsip_endpt_release_pool(endpt, lis_tp_pool);
        pj_websock_close(listener->listener_ws,
                         PJ_WEBSOCK_SC_NORMAL_CLOSURE, NULL);
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    status = pj_atomic_create(lis_tp_pool, 0, &lis_tp->ref_cnt);
    if (status != PJ_SUCCESS) {
        pj_lock_destroy(lis_tp->lock);
        lis_tp->lock = NULL;
        pjsip_endpt_release_pool(endpt, lis_tp_pool);
        pj_websock_close(listener->listener_ws,
                         PJ_WEBSOCK_SC_NORMAL_CLOSURE, NULL);
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    pj_ansi_snprintf(lis_tp->obj_name, sizeof(lis_tp->obj_name),
                     "ws_lstnr:%p", lis_tp);
    lis_tp->type_name = (char*)(secure ? "WSS" : "WS");
    lis_tp->flag = PJSIP_TRANSPORT_RELIABLE |
                   (secure ? PJSIP_TRANSPORT_SECURE : 0);
    lis_tp->info = (char*)"WebSocket Listener";
    lis_tp->key.type = listener->type_id;
    lis_tp->addr_len = sizeof(pj_sockaddr);

    {
        char addr_buf[PJ_INET6_ADDRSTRLEN];
        pj_sockaddr_print(bind_addr, addr_buf, sizeof(addr_buf), 0);
        char *host_buf = (char*)pj_pool_alloc(lis_tp_pool,
                                              PJ_INET6_ADDRSTRLEN);
        pj_ansi_snprintf(host_buf, PJ_INET6_ADDRSTRLEN, "%s", addr_buf);
        lis_tp->local_name.host = pj_str(host_buf);
        lis_tp->local_name.port = pj_sockaddr_get_port(bind_addr);
    }

    lis_tp->dir = PJSIP_TP_DIR_INCOMING;
    pj_sockaddr_cp(&lis_tp->local_addr, bind_addr);

    status = pjsip_transport_register(lis_tp->tpmgr, lis_tp);
    if (status != PJ_SUCCESS) {
        pj_atomic_destroy(lis_tp->ref_cnt);
        lis_tp->ref_cnt = NULL;
        pj_lock_destroy(lis_tp->lock);
        lis_tp->lock = NULL;
        pjsip_endpt_release_pool(endpt, lis_tp_pool);
        pj_websock_close(listener->listener_ws,
                         PJ_WEBSOCK_SC_NORMAL_CLOSURE, NULL);
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    listener->base = lis_tp;

    /* Add to global listener list */
    pj_list_push_back(&ws_listeners, listener);

    if (p_listener_tp)
        *p_listener_tp = lis_tp;

    PJ_LOG(4, (lis_tp->obj_name,
               "WebSocket %s listener started on port %d",
               secure ? "WSS" : "WS",
               pj_sockaddr_get_port(bind_addr)));

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjsip_ws_transport_connect(
                        pjsip_endpoint *endpt,
                        pj_websock_endpoint *ws_endpt,
                        const char *ws_url,
                        pjsip_transport **p_transport)
{
    pj_http_uri http_uri;
    pj_sockaddr remote_addr;
    pj_pool_t *pool;
    struct ws_transport *ws_tp;
    pj_str_t host;
    char str_host[PJ_MAX_HOSTNAME];
    pj_uint16_t remote_port;
    pj_status_t status;

    PJ_ASSERT_RETURN(endpt && ws_endpt && ws_url && p_transport,
                     PJ_EINVAL);

    /* Parse the WS URL */
    status = pj_http_uri_parse(ws_url, &http_uri);
    if (status != PJ_SUCCESS)
        return status;

    remote_port = pj_http_uri_port(&http_uri);
    pj_ansi_snprintf(str_host, sizeof(str_host), "%.*s:%u",
                     (int)http_uri.host.slen, http_uri.host.ptr,
                     remote_port);

    host = pj_str(str_host);
    status = pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &host, &remote_addr);
    if (status != PJ_SUCCESS)
        return status;

    pool = pjsip_endpt_create_pool(endpt, "ws_tp%p", POOL_INIT, POOL_INC);
    if (!pool)
        return PJ_ENOMEM;

    ws_tp = PJ_POOL_ZALLOC_T(pool, struct ws_transport);
    ws_tp->pool = pool;
    ws_tp->remote_addr = remote_addr;

    ws_tp->pool_rdata = pjsip_endpt_create_pool(endpt, "ws_rdata%p",
                                                 POOL_RDATA_INIT,
                                                 POOL_RDATA_INC);
    if (!ws_tp->pool_rdata) {
        pjsip_endpt_release_pool(endpt, pool);
        return PJ_ENOMEM;
    }

    /* Determine if secure (WSS) from URL scheme */
    pj_bool_t secure = PJ_FALSE;
    pj_str_t ws_scheme = pj_str((char*)"wss");
    if (pj_strnicmp(&http_uri.scheme, &ws_scheme, 3) == 0)
        secure = PJ_TRUE;

    pj_ansi_snprintf(ws_tp->base.obj_name, sizeof(ws_tp->base.obj_name),
                     "ws:%p", ws_tp);
    ws_tp->base.pool = pool;
    ws_tp->base.endpt = endpt;
    ws_tp->base.tpmgr = pjsip_endpt_get_tpmgr(endpt);
    ws_tp->base.send_msg = &ws_send_msg;
    ws_tp->base.do_shutdown = &ws_do_shutdown;
    ws_tp->base.destroy = &ws_destroy;

    ws_tp->is_wss = secure;
    ws_tp->base.type_name = (char*)(secure ? "WSS" : "WS");
    ws_tp->base.flag = PJSIP_TRANSPORT_RELIABLE |
                       (secure ? PJSIP_TRANSPORT_SECURE : 0);
    ws_tp->base.info = (char*)"WebSocket Transport";

    ws_tp->base.key.type = secure ? PJSIP_TRANSPORT_WSS :
                                    PJSIP_TRANSPORT_WS;
    pj_sockaddr_cp(&ws_tp->base.key.rem_addr, &remote_addr);
    ws_tp->base.addr_len = sizeof(pj_sockaddr);
    pj_sockaddr_cp(&ws_tp->base.local_addr, &remote_addr);

    /* Local name */
    {
        char *buf = (char*)pj_pool_alloc(pool, PJ_INET6_ADDRSTRLEN);
        pj_ansi_snprintf(buf, PJ_INET6_ADDRSTRLEN, "%s", "127.0.0.1");
        ws_tp->base.local_name.host = pj_str(buf);
        ws_tp->base.local_name.port = pj_sockaddr_get_port(&remote_addr);
    }

    /* Remote name */
    ws_tp->base.remote_name.host = http_uri.host;
    ws_tp->base.remote_name.port = remote_port;
    ws_tp->base.dir = PJSIP_TP_DIR_OUTGOING;

    status = pj_lock_create_recursive_mutex(pool, "ws",
                                            &ws_tp->base.lock);
    if (status != PJ_SUCCESS) {
        pjsip_endpt_release_pool(endpt, ws_tp->pool_rdata);
        ws_tp->pool_rdata = NULL;
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    status = pj_atomic_create(pool, 0, &ws_tp->base.ref_cnt);
    if (status != PJ_SUCCESS) {
        pj_lock_destroy(ws_tp->base.lock);
        ws_tp->base.lock = NULL;
        pjsip_endpt_release_pool(endpt, ws_tp->pool_rdata);
        ws_tp->pool_rdata = NULL;
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    status = pjsip_transport_register(ws_tp->base.tpmgr, &ws_tp->base);
    if (status != PJ_SUCCESS) {
        pj_atomic_destroy(ws_tp->base.ref_cnt);
        ws_tp->base.ref_cnt = NULL;
        pj_lock_destroy(ws_tp->base.lock);
        ws_tp->base.lock = NULL;
        pjsip_endpt_release_pool(endpt, ws_tp->pool_rdata);
        ws_tp->pool_rdata = NULL;
        pjsip_endpt_release_pool(endpt, pool);
        return status;
    }

    /* Set up callbacks */
    pj_websock_cb ws_cb;
    pj_bzero(&ws_cb, sizeof(ws_cb));
    ws_cb.on_connect_complete = &on_ws_connect_complete;
    ws_cb.on_rx_msg = &on_ws_rx_msg;
    ws_cb.on_accept_complete = &on_ws_accept_complete;
    ws_cb.on_tx_msg = &on_ws_tx_msg;
    ws_cb.on_state_change = &on_ws_state_change;

    ws_tp->has_connect_result = PJ_FALSE;
    ws_tp->is_connected = PJ_FALSE;
    ws_tp->ws = NULL;

    /* Set the SIP sub-protocol header */
    pj_websock_http_hdr sub_hdr;
    sub_hdr.key = pj_str("Sec-WebSocket-Protocol");
    sub_hdr.val = pj_str("sip");

    status = pj_websock_connect(ws_endpt, ws_url, &ws_cb, ws_tp,
                                &sub_hdr, 1, &ws_tp->ws);
    if (status != PJ_EPENDING && status != PJ_SUCCESS) {
        pjsip_transport_shutdown(&ws_tp->base);
        return status;
    }

    if (status == PJ_SUCCESS) {
        ws_tp->is_connected = PJ_TRUE;
        ws_tp->has_connect_result = PJ_TRUE;
    } else {
        /* Wait for connection with timeout */
        pj_time_val timeout;
        pj_gettimeofday(&timeout);
        timeout.sec += 5;

        while (!ws_tp->has_connect_result) {
            pj_time_val now;
            pj_gettimeofday(&now);
            if (now.sec > timeout.sec ||
                (now.sec == timeout.sec && now.msec > timeout.msec))
            {
                break;
            }
            pj_time_val tv = {0, 10};
            pjsip_endpt_handle_events(endpt, &tv);
        }
    }

    if (!ws_tp->is_connected) {
        pjsip_transport_shutdown(&ws_tp->base);
        return ws_tp->connect_status ? ws_tp->connect_status : PJ_ETIMEDOUT;
    }

    *p_transport = &ws_tp->base;
    return PJ_SUCCESS;
}
