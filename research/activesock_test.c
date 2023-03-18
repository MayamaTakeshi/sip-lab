#include <pjlib-util.h>
#include <pjlib.h>
#include <pjmedia-codec.h>
#include <pjmedia.h>
#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjsip_ua.h>

static pj_caching_pool cp;
static pj_pool_t *pool;
static pj_ioqueue_t *ioqueue;

#define MAX_TCP_DATA 4096

struct AsockUserData {
  pj_sock_t sock;
  Call *call;
  char buf[MAX_TCP_DATA];
  char len;
};

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
  if(!ud) {
    printf("no asock user data\n");
    return PJ_FALSE;
  }

  printf("%.*s\n", size, data);
  if (size == 0) {
    printf("size is zero\n");
    return PJ_FALSE;
  }

  assert(size + ud->len < MAX_TCP_DATA);

  memcpy(&ud->buf[ud->len], data, size);
  ud->len = size + ud->len;
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

    if(sep+4+body_len < ud->buf+ud->len) {
      // msg incomplete
      *remainder = 0;
      return PJ_TRUE;
    }

    msg_size = sep+4+body_len - ud->buf;
  }

  printf("on_data_read msg_size=%d\n", msg_size);
  printf("on_data_read msg=%s\n", evt);

  int remain_len = ud->len - msg_size;
  memcpy(ud->buf, &ud->buf[msg_size], remain_len);
  ud->len = remain_len;
  ud->buf[ud->len] = '\0';
  printf("on_data_read buf len=%d\n",  ud->len);
  
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
  if(!ud) {
     printf("no asock user data\n");
    return PJ_FALSE;
  }

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
  if(!ud) {
    printf("no asock user data\n");
    return PJ_FALSE;
  }

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

static pj_activesock_t* create_tcp_socket() {
  pj_ioqueue_key_t **skey;

  skey = (pj_ioqueue_key_t **)pj_pool_alloc(pool, sizeof(pj_ioqueue_key_t *));
  pj_sock_t *sock = (pj_sock_t *)pj_pool_alloc(pool, sizeof(pj_sock_t));

  pj_status_t rc;
  pj_sockaddr_in addr, client_add, rmt_addr;
  int client_addr_len;

  pj_activesock_t *asock = NULL;

  unsigned allocated_port = 0;

  AsockUserData *ud = NULL;

  pj_int32_t optval = 1;

  rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, sock);
  if (rc != PJ_SUCCESS || *sock == PJ_INVALID_SOCKET) {
    printf("....unable to create socket, rc=%d\n", rc);
    return NULL;
  }

  pj_sockaddr_in_init(&addr, ipaddr, 0);

  rc = pj_sock_setsockopt(*sock, PJ_SOL_SOCKET, PJ_SO_REUSEADDR, &optval, sizeof(optval));
  if (rc != PJ_SUCCESS) {
      printf("pj_sock_setsockopt() failed %d", rc);
      goto on_error;
  }

  // Bind server socket.
  pj_sockaddr_in_set_port(&addr, 10000);
  rc = pj_sock_bind(*sock, &addr, sizeof(addr));
  if (rc != PJ_SUCCESS) {
    printf("pj_sock_biind failed %d", rc);
    return 0;
  }

  if (pj_sock_listen(*sock, 5)) {
    prinf("...ERROR in pj_sock_listen() %d\n", rc);
    return 0;
  }

  rc = pj_activesock_create(pool, *sock, pj_SOCK_STREAM(), NULL, ioqueue,
                            &activesock_cb, NULL, &asock);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_create failed %d\n", rc);
    return 0;
  }

  ud = (AsockUserData*)pj_pool_zalloc(pool, sizeof(AsockUserData));
  ud->sock = *sock;

  rc = pj_activesock_set_user_data(asock, ud);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed %d\n", rc);
    return 0;
  }

  rc = pj_activesock_start_accept(asock, pool);
  if (rc != PJ_SUCCESS) {
    printf("pj_activesock_start_accept failed %d\n", rc);
    return 0;
  }

  return asock;
}

bool create_tcp_client() {
  printf("create_tcp_client\n");

  char evt[4096];
  pj_status_t status;

  pj_sock_t *sock = (pj_sock_t *)pj_pool_alloc(pool, sizeof(pj_sock_t));

  pj_activesock_t *asock = NULL;

  AsockUserData *ud = NULL;

  status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, sock);
  if (status != PJ_SUCCESS || *sock == PJ_INVALID_SOCKET) {
    printf("pj_sock_socket\n");
    return false;
  }

  status = pj_activesock_create(pool, *sock, pj_SOCK_STREAM(), NULL, ioqueue, &activesock_cb, NULL, &asock);
  if (status != PJ_SUCCESS) {
    printf("pj_activesock_create failed\n");
    return false;
  }

  ud = (AsockUserData*)pj_pool_zalloc(pool, sizeof(AsockUserData));
  ud->sock = *sock;

  status = pj_activesock_set_user_data(asock, ud);
  if (status != PJ_SUCCESS) {
    printf("pj_activesock_set_user_data failed\n");
    return false;
  }

  pj_sockaddr remaddr;

  status = pj_sockaddr_init(pj_AF_INET(), &remaddr, remote_addr, remote_media->desc.port);
  if (status != PJ_SUCCESS) {
    printntif("pj_sockaddr_init failed\n");
    return false;
  }

  status = pj_activesock_start_connect(asock, pool, &remaddr, sizeof(remaddr));
  if (status != PJ_SUCCESS && status != PJ_EPENDING) {
    printf("pj_activesock_start_connect failed\n");
    return false;
  }

  return true;
}


int main() {
  pj_status_t status;

  status = pj_init();
  if (status != PJ_SUCCESS) {
    printf( "pj_init failed\n");
    return 1;
  }

  status = pjlib_util_init();
  if (status != PJ_SUCCESS) {
    printf( "pj_lib_util_init failed\n");
    return 1;
  }

  pj_log_set_level(9);

  pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

  g_pool = pj_pool_create(&cp.factory, "tester", 1000, 1000, NULL);

  status = pj_ioqueue_create(g_ pool, 4, &ioqueue);
  if (status != PJ_SUCCESS) {
    printf( "pj_ioqueue_create failed\n");
    return 1;
  }

  pj_activesock_t *server = create_tcp_socket();
  if(!server) {
    return 1;
  }

  bool ok = create_tcp_client();
  if(!ok) {
    return 1;
  }

  return 0;
}

