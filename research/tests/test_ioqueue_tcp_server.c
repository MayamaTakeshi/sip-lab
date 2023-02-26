#include <stdio.h>
#include <pjlib.h>
#include <pjmedia.h>

#include <pjlib-util.h>
#include <pjlib.h>
#include <pjmedia-codec.h>
#include <pjmedia.h>
#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjsip_ua.h>

pj_thread_desc g_main_thread_descriptor;
pj_thread_t *g_main_thread = NULL;

static pj_caching_pool cp;
static pj_pool_t *g_pool;

static pjsip_endpoint *g_sip_endpt;

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
  printf("%.*s\n", size, data);
  if (size == 0) {
    printf("destroy the activesock\n");
    return PJ_FALSE;
  }
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

  pj_ioqueue_t *ioqueue = (pj_ioqueue_t *)pj_activesock_get_user_data(asock);

  pj_status_t rc =
      pj_activesock_create(g_pool, newsock, pj_SOCK_STREAM(), NULL, ioqueue,
                           &activesock_cb, NULL, &new_asock);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_create for newsock failed %d\n", rc);
    return PJ_FALSE;
  }

  rc = pj_activesock_set_user_data(new_asock, ioqueue);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed %d\n", rc);
    return PJ_FALSE;
  }

  rc = pj_activesock_start_read(new_asock, g_pool, 1000, 0);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_start_read() failed with %d\n", rc);
    return PJ_FALSE;
  }
  printf("pj_activesock_start_read() success\n");

  pj_ioqueue_key_t **skey;

  skey = (pj_ioqueue_key_t **)pj_pool_alloc(g_pool, sizeof(pj_ioqueue_key_t *));

  char *msg = "hello from server\n";
  pj_ssize_t size = strlen(msg);

  rc = pj_activesock_send(new_asock, skey, msg, &size, 0);
  if (rc != PJ_SUCCESS) {
    printf("server pj_activesock_send() failed with %d\n", rc);
  }

  return PJ_TRUE;
}

static pj_bool_t on_connect_complete(pj_activesock_t *asock,
                                     pj_status_t status) {
  printf("on_connect_complete\n");

  pj_ioqueue_key_t **skey;
  pj_ioqueue_t *ioqueue = (pj_ioqueue_t *)pj_activesock_get_user_data(asock);

  skey = (pj_ioqueue_key_t **)pj_pool_alloc(g_pool, sizeof(pj_ioqueue_key_t *));

  char *msg = "hello from client\n";
  pj_ssize_t size = strlen(msg);

  pj_status_t rc = pj_activesock_send(asock, skey, msg, &size, 0);
  if (rc != PJ_SUCCESS) {
    printf("client pj_activesock_send() failed with %d\n", rc);
  }

  rc = pj_activesock_start_read(asock, g_pool, 1000, 0);
  if (rc != PJ_SUCCESS) {
    printf("client pj_activesock_start_read() failed with %d\n", rc);
    return PJ_FALSE;
  }
  printf("client pj_activesock_start_read() success\n");

  return PJ_TRUE;
}

static int create_tcp_server(pj_str_t *ip, int port, pj_pool_t *pool) {
  pj_ioqueue_key_t **skey;
  pj_ioqueue_t *ioqueue = pjsip_endpt_get_ioqueue(g_sip_endpt);

  skey = (pj_ioqueue_key_t **)pj_pool_alloc(pool, sizeof(pj_ioqueue_key_t *));
  pj_sock_t *sock = (pj_sock_t *)pj_pool_alloc(pool, sizeof(pj_sock_t));

  pj_status_t rc;
  pj_sockaddr_in client_add, rmt_addr;
  int client_addr_len;

  pj_activesock_t *asock = NULL;

  rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, sock);
  if (rc != PJ_SUCCESS || *sock == PJ_INVALID_SOCKET) {
    printf("....unable to create server socket, rc=%d\n", rc);
    goto on_error;
  }

  // enable SO_REUSEADDR to permit to start the app again immediately without have to wait for the the socket to be released
  int enabled = 1;
  rc = pj_sock_setsockopt(*sock, pj_SOL_SOCKET(), pj_SO_REUSEADDR(), &enabled, sizeof(enabled));
  if (rc != PJ_SUCCESS) {
    printf("pj_sock_setsockopt failed. status=%d\n", rc);
    goto on_error;
  }

  // Bind server socket.

  pj_sockaddr addr;
  pj_sockaddr_init(pj_AF_INET(), &addr, ip, port);

  if ((rc = pj_sock_bind(*sock, &addr, sizeof(addr))) != 0) {
    printf("pj_sock_bind failed. status=%d\n", rc);
    goto on_error;
  }

  // Server socket listen().
  if (pj_sock_listen(*sock, 5)) {
    printf("pj_sock_listen failed. status=%d\n", rc);
    goto on_error;
  }

  // We also need to follow
  // https://www.pjsip.org/pjlib/docs/html/group__PJ__ACTIVESOCK.htm
  // https://cpp.hotexamples.com/examples/-/-/pj_sockaddr_in_init/cpp-pj_sockaddr_in_init-function-examples.html

  rc = pj_activesock_create(pool, *sock, pj_SOCK_STREAM(), NULL, ioqueue,
                            &activesock_cb, NULL, &asock);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_create failed %d\n", rc);
    goto on_error;
  }

  rc = pj_activesock_set_user_data(asock, ioqueue);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed %d\n", rc);
    goto on_error;
  }

  rc = pj_activesock_start_accept(asock, pool);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_start_accept failed %d\n", rc);
    goto on_error;
  }

  printf("create_tcp_server success\n");
  return 0;

on_error:
  printf("create_tcp_server failure\n");
  return -1;
}


static int create_tcp_client(pj_str_t *ip, int port, pj_pool_t *pool) {
  pj_ioqueue_key_t **skey;
  pj_ioqueue_t *ioqueue = pjsip_endpt_get_ioqueue(g_sip_endpt);

  skey = (pj_ioqueue_key_t **)pj_pool_alloc(pool, sizeof(pj_ioqueue_key_t *));
  pj_sock_t *sock = (pj_sock_t *)pj_pool_alloc(pool, sizeof(pj_sock_t));

  pj_status_t rc;

  pj_activesock_t *asock = NULL;

  rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, sock);
  if (rc != PJ_SUCCESS || *sock == PJ_INVALID_SOCKET) {
    printf("....unable to create client socket, rc=%d\n", rc);
    goto on_error;
  }

  rc = pj_activesock_create(pool, *sock, pj_SOCK_STREAM(), NULL, ioqueue,
                            &activesock_cb, NULL, &asock);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_create failed %d\n", rc);
    goto on_error;
  }

  rc = pj_activesock_set_user_data(asock, ioqueue);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed %d\n", rc);
    goto on_error;
  }

  pj_sockaddr addr;
  pj_sockaddr_init(pj_AF_INET(), &addr, ip, port);

  rc = pj_activesock_start_connect(asock, pool, &addr, sizeof(addr)); 
  if (rc == PJ_SUCCESS) {
    printf("pj_activesock_start_connect immediate %d\n", rc);
  } else {
    printf("pj_activesock_start_connect pending %d\n", rc);
  }

  printf("create_tcp_client success\n");
  return 0;

on_error:
  printf("create_tcp_client failure\n");
  return -1;
}

int main() {
    pj_status_t status = pj_init();
    if(status != PJ_SUCCESS) {
        printf("pj_init failed. status=%i\n", status);
    }

    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    g_pool = pj_pool_create(&cp.factory, "tester", 1000, 1000, NULL);

    // The above lines are always required. Do not remove them.


    char *sip_endpt_name = (char *)"mysip";

    status = pjsip_endpt_create(&cp.factory, sip_endpt_name, &g_sip_endpt);
    if (status != PJ_SUCCESS) {
        printf("pjsip_endpt_create failed. status=%i\n", status);
        return 1;
    }

    pj_str_t ip = pj_str("127.0.0.1");

    int rc = create_tcp_server(&ip, 9000, g_pool);
    printf("create_tcp_server returned=%i\n", rc);

    int c = 0;
    while(1) {
        pj_time_val tv = {0, 1};
        pjsip_endpt_handle_events(g_sip_endpt, &tv);
        if(c == 1000) {
            rc = create_tcp_client(&ip, 9000, g_pool);
            printf("create_tcp_client returned=%i\n", rc);
        }
        c++;
    }

    return 0;
}
