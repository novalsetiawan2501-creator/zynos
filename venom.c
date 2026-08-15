/*
 * venom.c — HTTP/2 load testing tool (C + nghttp2 + pthread + OpenSSL)
 *
 * Untuk internal load testing / capacity planning terhadap server/website
 * milik sendiri. Gunakan hanya pada endpoint yang memang berhak Anda uji.
 *
 * Arsitektur:
 *   - N thread (pthread), tiap thread mengelola sekumpulan koneksi HTTP/2
 *     persisten (connection pool) ke target yang sama.
 *   - Tiap koneksi mempertahankan sejumlah "worker" = request (stream HTTP/2)
 *     yang berjalan bersamaan secara multiplexed di atas satu koneksi
 *     (mekanisme asli HTTP/2, bukan thread OS terpisah — karena satu
 *     nghttp2_session tidak thread-safe, semua I/O suatu koneksi selalu
 *     ditangani oleh satu thread pemiliknya saja).
 *   - "Warm polling": semua koneksi (TCP+TLS+HTTP/2 handshake) disiapkan
 *     dan menunggu (barrier) sebelum pengukuran RPS/latensi dimulai, supaya
 *     overhead pembentukan koneksi tidak mengotori hasil ukur.
 *   - Keep-alive + connection pooling melekat pada semantik HTTP/2 itu
 *     sendiri: koneksi yang sama dipakai berulang kali untuk request baru.
 *   - Tanpa rate limiting internal secara default (mode throughput maksimal).
 *     Berikan -r/--rate > 0 untuk membatasi ke target RPS tertentu.
 *
 * Kompilasi (lihat instruksi lengkap yang menyertai file ini):
 *   gcc -O2 -o venom venom.c -lnghttp2 -lpthread -lssl -lcrypto
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <limits.h>
#include <poll.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <nghttp2/nghttp2.h>

/* ---------- helper macro ---------- */
#define MAKE_NV(NAME, VALUE)                                                 \
  {                                                                          \
    (uint8_t *)(NAME), (uint8_t *)(VALUE), strlen(NAME), strlen(VALUE),      \
        NGHTTP2_NV_FLAG_NONE                                                 \
  }

/* ---------- tipe data ---------- */
typedef struct {
  char scheme[8];
  char host[256];
  char path[2048];
  int port;
  int use_tls;
} target_t;

typedef struct {
  target_t target;
  int rate;         /* target total RPS, 0 = tanpa batas (throughput maksimal) */
  int n_threads;
  int n_conns;       /* koneksi paralel per thread */
  int n_workers;     /* request (stream) bersamaan per koneksi */
  int duration_sec;  /* 0 = jalan sampai Ctrl+C */
  char proxy_file[256]; /* file proxy (IP:PORT per baris) */
} config_t;

typedef struct {
  struct timespec submit_time;
  int status_code;
} stream_data_t;

typedef enum {
  ST_CONNECTING = 0,
  ST_TLS_HANDSHAKE,
  ST_READY,
  ST_ERROR
} conn_state_t;

typedef struct {
  int fd;
  SSL *ssl;
  nghttp2_session *session;
  conn_state_t state;
  int tls_want_write;
  int active_streams;
  int thread_id;
  int conn_id;
  int counted_ready;
} connection_t;

typedef struct {
  int thread_id;
} thread_arg_t;

typedef struct {
  atomic_long sent;
  atomic_long completed;
  atomic_long success;
  atomic_long failed;
  atomic_long latency_sum_us;
  atomic_long latency_count;
  atomic_long latency_min_us;
  atomic_long latency_max_us;
  atomic_int active_connections;
} global_stats_t;

/* ---------- global state ---------- */
static config_t g_config;
static global_stats_t g_stats;
static SSL_CTX *g_ssl_ctx;
static atomic_int g_running = 1;
static atomic_int g_warmup_done = 0;
static atomic_long g_rate_tokens = 0;
static struct sockaddr_storage g_target_addr;
static socklen_t g_target_addrlen;

/* ================= PROXY SUPPORT ================= */
static char **g_proxies = NULL;
static int g_proxy_count = 0;
static atomic_int g_proxy_idx = 0;

static int load_proxies(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: gagal buka proxy file: %s\n", filename);
        return -1;
    }

    char line[256];
    int count = 0;
    char **tmp = NULL;
    while (fgets(line, sizeof(line), fp)) {
        char *p = strchr(line, '\n');
        if (p) *p = '\0';
        if (strlen(line) < 7) continue;
        tmp = realloc(tmp, sizeof(char *) * (count + 1));
        tmp[count] = strdup(line);
        count++;
    }
    fclose(fp);

    if (count == 0) {
        fprintf(stderr, "Error: tidak ada proxy valid di file\n");
        return -1;
    }

    g_proxies = tmp;
    g_proxy_count = count;
    printf("[PROXY] Loaded %d proxies dari %s\n", count, filename);
    return 0;
}

static char *get_next_proxy(void) {
    if (g_proxy_count == 0) return NULL;
    int idx = atomic_fetch_add(&g_proxy_idx, 1) % g_proxy_count;
    return g_proxies[idx];
}

/* ---------- forward declarations ---------- */
static int parse_url(const char *url, target_t *t);
static int resolve_target(target_t *t);
static SSL_CTX *create_ssl_ctx(void);
static void *rate_ticker_fn(void *arg);
static int try_consume_token(void);
static void update_min_max(long lat_us);
static int submit_request(connection_t *conn);
static int init_connection(connection_t *conn);
static void cleanup_connection_resources(connection_t *conn);
static void reset_connection(connection_t *conn);
static void handle_connecting(connection_t *conn);
static void do_tls_handshake(connection_t *conn);
static void setup_http2_session(connection_t *conn);
static void conn_tick(connection_t *conn, short revents);
static void update_poll_events(connection_t *conn, struct pollfd *pfd);
static void *thread_main(void *arg);
static void handle_sigint(int sig);
static void print_usage(const char *prog);

/* ================= URL & DNS ================= */

static int parse_url(const char *url, target_t *t) {
  const char *p = url;
  if (strncmp(p, "https://", 8) == 0) {
    t->use_tls = 1;
    t->port = 443;
    strcpy(t->scheme, "https");
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    t->use_tls = 0;
    t->port = 80;
    strcpy(t->scheme, "http");
    p += 7;
  } else {
    fprintf(stderr, "Error: URL harus diawali http:// atau https://\n");
    return -1;
  }

  const char *slash = strchr(p, '/');
  const char *host_end = slash ? slash : p + strlen(p);
  const char *colon = memchr(p, ':', (size_t)(host_end - p));

  size_t hostlen;
  if (colon) {
    hostlen = (size_t)(colon - p);
    t->port = atoi(colon + 1);
  } else {
    hostlen = (size_t)(host_end - p);
  }
  if (hostlen == 0 || hostlen >= sizeof(t->host)) {
    fprintf(stderr, "Error: host tidak valid pada URL\n");
    return -1;
  }
  memcpy(t->host, p, hostlen);
  t->host[hostlen] = '\0';

  if (slash) {
    strncpy(t->path, slash, sizeof(t->path) - 1);
    t->path[sizeof(t->path) - 1] = '\0';
  } else {
    strcpy(t->path, "/");
  }
  return 0;
}

static int resolve_target(target_t *t) {
  char portstr[8];
  snprintf(portstr, sizeof(portstr), "%d", t->port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *res = NULL;
  int rv = getaddrinfo(t->host, portstr, &hints, &res);
  if (rv != 0 || !res) {
    fprintf(stderr, "Error: getaddrinfo gagal: %s\n", gai_strerror(rv));
    return -1;
  }
  memcpy(&g_target_addr, res->ai_addr, res->ai_addrlen);
  g_target_addrlen = res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

/* ================= TLS ================= */

static SSL_CTX *create_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    const char *ciphersuites =
        "TLS_AES_128_GCM_SHA256:"
        "TLS_AES_256_GCM_SHA384:"
        "TLS_CHACHA20_POLY1305_SHA256";

    SSL_CTX_set_ciphersuites(ctx, ciphersuites);

    static const unsigned char alpn_protos[] = {2, 'h', '2'};
    SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos));

    return ctx;
}

/* ================= rate limiter (token bucket) ================= */

static void *rate_ticker_fn(void *arg) {
  (void)arg;
  const long tick_ms = 10;
  long per_tick = (long)g_config.rate * tick_ms / 1000;
  if (per_tick < 1) per_tick = 1;
  long burst_cap = g_config.rate / 5; /* ~200ms burst */
  if (burst_cap < per_tick * 2) burst_cap = per_tick * 2;

  struct timespec ts = {.tv_sec = 0, .tv_nsec = tick_ms * 1000000L};
  while (atomic_load(&g_running)) {
    nanosleep(&ts, NULL);
    long cur = atomic_load(&g_rate_tokens);
    long want = cur + per_tick;
    if (want > burst_cap) want = burst_cap;
    atomic_store(&g_rate_tokens, want);
  }
  return NULL;
}

static int try_consume_token(void) {
  if (g_config.rate <= 0) return 1; /* mode tanpa batas */
  long cur = atomic_load(&g_rate_tokens);
  while (cur > 0) {
    if (atomic_compare_exchange_weak(&g_rate_tokens, &cur, cur - 1)) return 1;
  }
  return 0;
}

/* ================= statistik ================= */

static void update_min_max(long lat_us) {
  long cur_min = atomic_load(&g_stats.latency_min_us);
  while (lat_us < cur_min &&
         !atomic_compare_exchange_weak(&g_stats.latency_min_us, &cur_min, lat_us)) {
  }
  long cur_max = atomic_load(&g_stats.latency_max_us);
  while (lat_us > cur_max &&
         !atomic_compare_exchange_weak(&g_stats.latency_max_us, &cur_max, lat_us)) {
  }
}

/* ================= nghttp2 callbacks ================= */

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data) {
  (void)session;
  (void)flags;
  connection_t *conn = (connection_t *)user_data;
  int rv = SSL_write(conn->ssl, data, (int)length);
  if (rv <= 0) {
    int err = SSL_get_error(conn->ssl, rv);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
      return NGHTTP2_ERR_WOULDBLOCK;
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  return rv;
}

static ssize_t recv_callback(nghttp2_session *session, uint8_t *buf,
                              size_t length, int flags, void *user_data) {
  (void)session;
  (void)flags;
  connection_t *conn = (connection_t *)user_data;
  int rv = SSL_read(conn->ssl, buf, (int)length);
  if (rv > 0) return rv;
  int err = SSL_get_error(conn->ssl, rv);
  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
    return NGHTTP2_ERR_WOULDBLOCK;
  if (err == SSL_ERROR_ZERO_RETURN) return NGHTTP2_ERR_EOF;
  return NGHTTP2_ERR_CALLBACK_FAILURE;
}

static int on_header_callback(nghttp2_session *session,
                               const nghttp2_frame *frame, const uint8_t *name,
                               size_t namelen, const uint8_t *value,
                               size_t valuelen, uint8_t flags,
                               void *user_data) {
  (void)flags;
  (void)user_data;
  if (frame->hd.type == NGHTTP2_HEADERS &&
      frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
    stream_data_t *sd =
        nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
    if (sd && namelen == 7 && memcmp(name, ":status", 7) == 0) {
      char buf[16];
      size_t n = valuelen < sizeof(buf) - 1 ? valuelen : sizeof(buf) - 1;
      memcpy(buf, value, n);
      buf[n] = '\0';
      sd->status_code = atoi(buf);
    }
  }
  return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session,
                                        uint8_t flags, int32_t stream_id,
                                        const uint8_t *data, size_t len,
                                        void *user_data) {
  (void)session;
  (void)flags;
  (void)stream_id;
  (void)data;
  (void)len;
  (void)user_data;
  return 0; /* body dibuang, hanya status code yang diukur */
}

static int on_stream_close_callback(nghttp2_session *session,
                                     int32_t stream_id, uint32_t error_code,
                                     void *user_data) {
  connection_t *conn = (connection_t *)user_data;
  stream_data_t *sd = nghttp2_session_get_stream_user_data(session, stream_id);
  if (sd) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long lat_us = (now.tv_sec - sd->submit_time.tv_sec) * 1000000L +
                  (now.tv_nsec - sd->submit_time.tv_nsec) / 1000L;

    atomic_fetch_add(&g_stats.completed, 1);
    if (error_code == 0 && sd->status_code >= 200 && sd->status_code < 400) {
      atomic_fetch_add(&g_stats.success, 1);
    } else {
      atomic_fetch_add(&g_stats.failed, 1);
    }
    atomic_fetch_add(&g_stats.latency_sum_us, lat_us);
    atomic_fetch_add(&g_stats.latency_count, 1);
    update_min_max(lat_us);

    free(sd);
    if (conn->active_streams > 0) conn->active_streams--;
  }

  if (atomic_load(&g_running) && atomic_load(&g_warmup_done) &&
      conn->state == ST_READY) {
    submit_request(conn);
  }
  return 0;
}

/* ================= request submission ================= */

static int submit_request(connection_t *conn) {
    if (!try_consume_token()) return 0;

    stream_data_t *sd = calloc(1, sizeof(stream_data_t));
    if (!sd) return 0;
    clock_gettime(CLOCK_MONOTONIC, &sd->submit_time);

    nghttp2_nv hdrs[] = {
        MAKE_NV(":method", "GET"),
        MAKE_NV(":scheme", g_config.target.scheme),
        MAKE_NV(":authority", g_config.target.host),
        MAKE_NV(":path", g_config.target.path),
        MAKE_NV("user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36"),
        MAKE_NV("sec-ch-ua", "\"Not(A:Brand\";v=\"99\", \"Google Chrome\";v=\"133\", \"Chromium\";v=\"133\""),
        MAKE_NV("sec-ch-ua-mobile", "?0"),
        MAKE_NV("sec-ch-ua-platform", "\"Windows\""),
        MAKE_NV("accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7"),
        MAKE_NV("accept-encoding", "gzip, deflate, br, zstd"),
        MAKE_NV("accept-language", "en-US,en;q=0.9"),
        MAKE_NV("upgrade-insecure-requests", "1"),
        MAKE_NV("cache-control", "max-age=0"),
        MAKE_NV("priority", "u=0, i"),
        MAKE_NV("sec-fetch-site", "none"),
        MAKE_NV("sec-fetch-mode", "navigate"),
        MAKE_NV("sec-fetch-user", "?1"),
        MAKE_NV("sec-fetch-dest", "document"),
    };

    int32_t stream_id = nghttp2_submit_request(
        conn->session, NULL, hdrs, sizeof(hdrs) / sizeof(hdrs[0]), NULL, sd);
    if (stream_id < 0) {
        free(sd);
        return 0;
    }
    conn->active_streams++;
    atomic_fetch_add(&g_stats.sent, 1);
    return 1;
}

/* ================= siklus hidup koneksi ================= */

static int init_connection(connection_t *conn) {
  int fd = socket(g_target_addr.ss_family, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  /* ===== PROXY SUPPORT ===== */
  struct sockaddr_storage proxy_addr;
  socklen_t proxy_addrlen = 0;
  int use_proxy = 0;

  if (g_config.proxy_file[0] != '\0') {
      char *proxy_str = get_next_proxy();
      if (proxy_str) {
          char proxy_ip[64];
          int proxy_port;
          if (sscanf(proxy_str, "%63[^:]:%d", proxy_ip, &proxy_port) == 2) {
              struct addrinfo hints, *res;
              memset(&hints, 0, sizeof(hints));
              hints.ai_family = AF_UNSPEC;
              hints.ai_socktype = SOCK_STREAM;
              char portstr[8];
              snprintf(portstr, sizeof(portstr), "%d", proxy_port);
              if (getaddrinfo(proxy_ip, portstr, &hints, &res) == 0 && res) {
                  memcpy(&proxy_addr, res->ai_addr, res->ai_addrlen);
                  proxy_addrlen = res->ai_addrlen;
                  freeaddrinfo(res);
                  use_proxy = 1;
              }
          }
      }
  }

  struct sockaddr *addr = use_proxy ? (struct sockaddr *)&proxy_addr 
                                     : (struct sockaddr *)&g_target_addr;
  socklen_t addrlen = use_proxy ? proxy_addrlen : g_target_addrlen;

  int rv = connect(fd, addr, addrlen);
  if (rv < 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }

  conn->fd = fd;
  conn->ssl = NULL;
  conn->session = NULL;
  conn->state = ST_CONNECTING;
  conn->tls_want_write = 0;
  conn->active_streams = 0;
  return 0;
}

static void cleanup_connection_resources(connection_t *conn) {
  if (conn->session) {
    nghttp2_session_del(conn->session);
    conn->session = NULL;
  }
  if (conn->ssl) {
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
    conn->ssl = NULL;
  }
  if (conn->fd >= 0) {
    close(conn->fd);
    conn->fd = -1;
  }
  if (conn->counted_ready) {
    atomic_fetch_sub(&g_stats.active_connections, 1);
    conn->counted_ready = 0;
  }
}

static void reset_connection(connection_t *conn) {
  cleanup_connection_resources(conn);
  if (!atomic_load(&g_running)) return;
  if (init_connection(conn) < 0) conn->state = ST_ERROR;
}

static void setup_http2_session(connection_t *conn) {
  nghttp2_session_callbacks *cbs;
  nghttp2_session_callbacks_new(&cbs);
  nghttp2_session_callbacks_set_send_callback(cbs, send_callback);
  nghttp2_session_callbacks_set_recv_callback(cbs, recv_callback);
  nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_callback);
  nghttp2_session_callbacks_set_on_stream_close_callback(
      cbs, on_stream_close_callback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      cbs, on_data_chunk_recv_callback);

  int rv = nghttp2_session_client_new(&conn->session, cbs, conn);
  nghttp2_session_callbacks_del(cbs);
  if (rv != 0) {
    reset_connection(conn);
    return;
  }

  nghttp2_settings_entry iv[] = {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1000}};
  nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, iv, 1);
  nghttp2_session_send(conn->session);

  conn->state = ST_READY;
  conn->counted_ready = 1;
  atomic_fetch_add(&g_stats.active_connections, 1);
}

static void do_tls_handshake(connection_t *conn) {
  int rv = SSL_connect(conn->ssl);
  if (rv == 1) {
    setup_http2_session(conn);
    return;
  }
  int err = SSL_get_error(conn->ssl, rv);
  if (err == SSL_ERROR_WANT_WRITE) {
    conn->tls_want_write = 1;
  } else if (err == SSL_ERROR_WANT_READ) {
    conn->tls_want_write = 0;
  } else {
    reset_connection(conn);
  }
}

static void handle_connecting(connection_t *conn) {
  int err = 0;
  socklen_t len = sizeof(err);
  if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
    reset_connection(conn);
    return;
  }
  conn->ssl = SSL_new(g_ssl_ctx);
  if (!conn->ssl) {
    reset_connection(conn);
    return;
  }
  SSL_set_fd(conn->ssl, conn->fd);
  SSL_set_connect_state(conn->ssl);
  SSL_set_tlsext_host_name(conn->ssl, g_config.target.host);
  conn->state = ST_TLS_HANDSHAKE;
  do_tls_handshake(conn);
}

static void conn_tick(connection_t *conn, short revents) {
  switch (conn->state) {
  case ST_CONNECTING:
    if (revents & (POLLOUT | POLLERR | POLLHUP)) handle_connecting(conn);
    break;
  case ST_TLS_HANDSHAKE:
    do_tls_handshake(conn);
    break;
  case ST_READY: {
    if (revents & POLLIN) {
      if (nghttp2_session_recv(conn->session) != 0) {
        reset_connection(conn);
        return;
      }
    }
    if (nghttp2_session_want_write(conn->session)) {
      if (nghttp2_session_send(conn->session) != 0) {
        reset_connection(conn);
        return;
      }
    }
    if (atomic_load(&g_warmup_done) && atomic_load(&g_running)) {
      while (conn->active_streams < g_config.n_workers) {
        if (!submit_request(conn)) break;
      }
    }
    if (nghttp2_session_want_write(conn->session)) nghttp2_session_send(conn->session);
    break;
  }
  case ST_ERROR:
    reset_connection(conn);
    break;
  }
}

static void update_poll_events(connection_t *conn, struct pollfd *pfd) {
  pfd->fd = conn->fd;
  pfd->revents = 0;
  switch (conn->state) {
  case ST_CONNECTING:
    pfd->events = POLLOUT;
    break;
  case ST_TLS_HANDSHAKE:
    pfd->events = conn->tls_want_write ? POLLOUT : POLLIN;
    break;
  case ST_READY: {
    short ev = 0;
    if (nghttp2_session_want_read(conn->session)) ev |= POLLIN;
    if (nghttp2_session_want_write(conn->session)) ev |= POLLOUT;
    pfd->events = ev ? ev : POLLIN;
    break;
  }
  default:
    pfd->events = POLLIN;
    break;
  }
}

/* ================= thread worker ================= */

static void *thread_main(void *arg) {
  thread_arg_t *targ = (thread_arg_t *)arg;
  int n = g_config.n_conns;

  connection_t *conns = calloc((size_t)n, sizeof(connection_t));
  struct pollfd *pfds = calloc((size_t)n, sizeof(struct pollfd));
  if (!conns || !pfds) {
    fprintf(stderr, "[thread %d] gagal alokasi memori\n", targ->thread_id);
    free(conns);
    free(pfds);
    return NULL;
  }

  for (int i = 0; i < n; i++) {
    conns[i].fd = -1;
    conns[i].thread_id = targ->thread_id;
    conns[i].conn_id = i;
    if (init_connection(&conns[i]) < 0) conns[i].state = ST_ERROR;
    pfds[i].fd = conns[i].fd;
    pfds[i].events = POLLOUT;
  }

  while (atomic_load(&g_running)) {
    for (int i = 0; i < n; i++) update_poll_events(&conns[i], &pfds[i]);
    int nready = poll(pfds, (nfds_t)n, 15);
    if (nready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (int i = 0; i < n; i++) {
      if (pfds[i].revents || conns[i].state == ST_READY ||
          conns[i].state == ST_ERROR) {
        conn_tick(&conns[i], pfds[i].revents);
      }
    }
  }

  for (int i = 0; i < n; i++) cleanup_connection_resources(&conns[i]);
  free(conns);
  free(pfds);
  return NULL;
}

/* ================= signal & CLI ================= */

static void handle_sigint(int sig) {
  (void)sig;
  atomic_store(&g_running, 0);
}

static void print_usage(const char *prog) {
  printf("Penggunaan: %s -u <url> [opsi]\n\n", prog);
  printf("Wajib:\n");
  printf("  -u, --url <url>          URL target, contoh: https://example.com/\n\n");
  printf("Opsional:\n");
  printf("  -r, --rate <n>           Target total request/detik (0 = tanpa batas, default: 0)\n");
  printf("  -t, --thread <n>         Jumlah thread (default: 1)\n");
  printf("  -c, --connection <n>     Jumlah koneksi paralel per thread (default: 1)\n");
  printf("  -w, --worker <n>         Jumlah request/stream bersamaan per koneksi (default: 1)\n");
  printf("  -d, --duration <detik>   Durasi tes; 0 = jalan sampai Ctrl+C (default: 0)\n");
  printf("  -p, --proxy <file>       File proxy (IP:PORT per baris)\n");
  printf("  -h, --help               Tampilkan bantuan ini\n\n");
  printf("Contoh:\n");
  printf("  %s -u https://staging.situs-saya.com/ -t 4 -c 50 -w 20\n", prog);
  printf("  %s -u https://situs-saya.com/ -t 2 -c 20 -w 10 -r 5000 -d 60\n", prog);
  printf("  %s -u https://situs-saya.com/ -p proxy.txt -t 4 -c 20 -w 10 -r 5000 -d 60\n", prog);
}

/* ================= main ================= */

int main(int argc, char **argv) {
  g_config.n_threads = 1;
  g_config.n_conns = 1;
  g_config.n_workers = 1;
  g_config.rate = 0;
  g_config.duration_sec = 0;
  g_config.proxy_file[0] = '\0';
  int have_url = 0;

  static struct option long_opts[] = {
      {"url", required_argument, 0, 'u'},
      {"rate", required_argument, 0, 'r'},
      {"thread", required_argument, 0, 't'},
      {"connection", required_argument, 0, 'c'},
      {"worker", required_argument, 0, 'w'},
      {"duration", required_argument, 0, 'd'},
      {"proxy", required_argument, 0, 'p'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "u:r:t:c:w:d:p:h", long_opts, NULL)) != -1) {
    switch (opt) {
    case 'u':
      if (parse_url(optarg, &g_config.target) != 0) return 1;
      have_url = 1;
      break;
    case 'r':
      g_config.rate = atoi(optarg);
      break;
    case 't':
      g_config.n_threads = atoi(optarg);
      break;
    case 'c':
      g_config.n_conns = atoi(optarg);
      break;
    case 'w':
      g_config.n_workers = atoi(optarg);
      break;
    case 'd':
      g_config.duration_sec = atoi(optarg);
      break;
    case 'p':
      strncpy(g_config.proxy_file, optarg, sizeof(g_config.proxy_file) - 1);
      g_config.proxy_file[sizeof(g_config.proxy_file) - 1] = '\0';
      if (load_proxies(g_config.proxy_file) != 0) return 1;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!have_url) {
    fprintf(stderr, "Error: --url wajib diisi\n\n");
    print_usage(argv[0]);
    return 1;
  }
  if (g_config.n_threads < 1) g_config.n_threads = 1;
  if (g_config.n_conns < 1) g_config.n_conns = 1;
  if (g_config.n_workers < 1) g_config.n_workers = 1;

  if (!g_config.target.use_tls) {
    fprintf(stderr,
            "Peringatan: HTTP/2 cleartext (h2c) perlu dukungan upgrade khusus "
            "di server.\n           Disarankan gunakan https:// (HTTP/2 atas "
            "TLS/ALPN, sesuai kebanyakan server nyata).\n\n");
  }

  if (resolve_target(&g_config.target) != 0) return 1;

  g_ssl_ctx = create_ssl_ctx();
  if (!g_ssl_ctx) {
    fprintf(stderr, "Error: gagal inisialisasi SSL context\n");
    return 1;
  }

  atomic_store(&g_stats.latency_min_us, LONG_MAX);
  signal(SIGINT, handle_sigint);
  signal(SIGPIPE, SIG_IGN);

  int total_conns = g_config.n_threads * g_config.n_conns;
  printf("=== HTTP/2 Load Test ===\n");
  printf("Target     : %s://%s:%d%s\n", g_config.target.scheme,
         g_config.target.host, g_config.target.port, g_config.target.path);
  printf("Thread     : %d | Koneksi/thread: %d | Total koneksi: %d\n",
         g_config.n_threads, g_config.n_conns, total_conns);
  printf("Worker     : %d stream bersamaan/koneksi (kapasitas maks ~%d "
         "request in-flight)\n",
         g_config.n_workers, total_conns * g_config.n_workers);
  printf("Rate limit : %s\n",
         g_config.rate > 0 ? "aktif (lihat -r)" : "tidak ada (throughput maksimal)");
  printf("Proxy      : %s\n\n",
         g_config.proxy_file[0] != '\0' ? g_config.proxy_file : "tidak digunakan");

  pthread_t rate_thread;
  int has_rate_thread = 0;
  if (g_config.rate > 0) {
    pthread_create(&rate_thread, NULL, rate_ticker_fn, NULL);
    has_rate_thread = 1;
  }

  pthread_t *threads = calloc((size_t)g_config.n_threads, sizeof(pthread_t));
  thread_arg_t *targs = calloc((size_t)g_config.n_threads, sizeof(thread_arg_t));
  for (int i = 0; i < g_config.n_threads; i++) {
    targs[i].thread_id = i;
    pthread_create(&threads[i], NULL, thread_main, &targs[i]);
  }

  printf("Pre-heating %d koneksi (warm polling)...\n", total_conns);
  struct timespec warmup_start;
  clock_gettime(CLOCK_MONOTONIC, &warmup_start);
  while (atomic_load(&g_running)) {
    int ready = atomic_load(&g_stats.active_connections);
    if (ready >= total_conns) break;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - warmup_start.tv_sec > 30) {
      printf("Timeout warm-up: %d/%d koneksi siap, lanjut dengan yang tersedia.\n",
             ready, total_conns);
      break;
    }
    usleep(50000);
  }
  printf("Warm-up selesai. Mulai load test (Ctrl+C untuk berhenti)...\n\n");
  atomic_store(&g_warmup_done, 1);

  long last_completed = 0;
  long elapsed = 0;
  while (atomic_load(&g_running)) {
    sleep(1);
    elapsed++;
    long completed = atomic_load(&g_stats.completed);
    long success = atomic_load(&g_stats.success);
    long failed = atomic_load(&g_stats.failed);
    long lat_sum = atomic_load(&g_stats.latency_sum_us);
    long lat_cnt = atomic_load(&g_stats.latency_count);
    long lat_min = atomic_load(&g_stats.latency_min_us);
    long lat_max = atomic_load(&g_stats.latency_max_us);

    long rps = completed - last_completed;
    last_completed = completed;
    double success_rate = completed > 0 ? (100.0 * (double)success / (double)completed) : 0.0;
    double avg_ms = lat_cnt > 0 ? ((double)lat_sum / (double)lat_cnt / 1000.0) : 0.0;
    double min_ms = lat_cnt > 0 ? (double)lat_min / 1000.0 : 0.0;
    double max_ms = lat_cnt > 0 ? (double)lat_max / 1000.0 : 0.0;

    printf("\r[%4lds] RPS:%7ld  Total:%9ld  Sukses:%6.2f%%  Gagal:%7ld  "
           "Latensi avg/min/max (ms): %6.2f/%6.2f/%6.2f   ",
           elapsed, rps, completed, success_rate, failed, avg_ms, min_ms, max_ms);
    fflush(stdout);

    if (g_config.duration_sec > 0 && elapsed >= g_config.duration_sec) {
      atomic_store(&g_running, 0);
    }
  }

  printf("\n\n=== Hasil Akhir ===\n");
  {
    long completed = atomic_load(&g_stats.completed);
    long success = atomic_load(&g_stats.success);
    long failed = atomic_load(&g_stats.failed);
    long lat_cnt = atomic_load(&g_stats.latency_count);
    long lat_sum = atomic_load(&g_stats.latency_sum_us);
    long lat_min = atomic_load(&g_stats.latency_min_us);
    long lat_max = atomic_load(&g_stats.latency_max_us);
    printf("Total request selesai : %ld\n", completed);
    printf("Sukses (status 2xx/3xx): %ld (%.2f%%)\n", success,
           completed > 0 ? 100.0 * (double)success / (double)completed : 0.0);
    printf("Gagal                  : %ld\n", failed);
    printf("Rata-rata RPS          : %.1f\n",
           elapsed > 0 ? (double)completed / (double)elapsed : 0.0);
    printf("Latensi rata-rata      : %.2f ms\n",
           lat_cnt > 0 ? (double)lat_sum / (double)lat_cnt / 1000.0 : 0.0);
    printf("Latensi min / maks     : %.2f / %.2f ms\n",
           lat_cnt > 0 ? (double)lat_min / 1000.0 : 0.0,
           lat_cnt > 0 ? (double)lat_max / 1000.0 : 0.0);
  }

  for (int i = 0; i < g_config.n_threads; i++) pthread_join(threads[i], NULL);
  if (has_rate_thread) pthread_join(rate_thread, NULL);

  free(threads);
  free(targs);
  SSL_CTX_free(g_ssl_ctx);
  return 0;
}