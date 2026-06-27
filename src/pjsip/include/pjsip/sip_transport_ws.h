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
#ifndef __PJSIP_TRANSPORT_WS_H__
#define __PJSIP_TRANSPORT_WS_H__

/**
 * @file sip_transport_ws.h
 * @brief SIP WebSocket Transport.
 */

#include <pjsip/sip_transport.h>
#include <websock.h>

PJ_BEGIN_DECL

/* Dynamic transport type IDs for WS and WSS */
PJ_DECL_DATA(pjsip_transport_type_e) PJSIP_TRANSPORT_WS;
PJ_DECL_DATA(pjsip_transport_type_e) PJSIP_TRANSPORT_WSS;

/**
 * Initialize WebSocket transport module. Must be called once before
 * any other pjsip_ws_* functions. Registers the WS and WSS transport
 * types with PJSIP.
 *
 * @return PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_ws_transport_init(void);

/**
 * Start a WebSocket listener (server mode). Creates a WebSocket listener
 * on the specified address and port. When a WebSocket connection with
 * SIP sub-protocol is accepted, a new PJSIP transport is automatically
 * created and registered.
 *
 * @param endpt         The SIP endpoint.
 * @param ws_endpt      The WebSocket endpoint.
 * @param bind_addr     Address and port to listen on.
 * @param secure        Use TLS (WSS) if PJ_TRUE, plain WS if PJ_FALSE.
 * @param p_listener_tp Optional pointer to receive the listener transport.
 *
 * @return PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_ws_transport_start(
    pjsip_endpoint *endpt,
    pj_websock_endpoint *ws_endpt,
    const pj_sockaddr *bind_addr,
    pj_bool_t secure,
    pjsip_transport **p_listener_tp);

/**
 * Create a WebSocket connection transport (client mode). Connects to the
 * specified WebSocket URL and registers a PJSIP transport for it.
 *
 * @param endpt         The SIP endpoint.
 * @param ws_endpt      The WebSocket endpoint.
 * @param ws_url        WebSocket URL (e.g. "ws://127.0.0.1:8080/sip").
 * @param p_transport   Pointer to receive the created transport.
 *
 * @return PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_ws_transport_connect(
    pjsip_endpoint *endpt,
    pj_websock_endpoint *ws_endpt,
    const char *ws_url,
    pjsip_transport **p_transport);

PJ_END_DECL

#endif  /* __PJSIP_TRANSPORT_WS_H__ */
