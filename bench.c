/*
 * bench.c - HTTP/2 concurrent load-testing tool
 *
 * Event-driven HTTP/2 benchmarking client for measuring server throughput
 * and latency under high concurrency. Built for thesis work on HTTP/2
 * efficiency in high-concurrency environments.
 *
 * Architecture:
 *   - N worker threads, each running its own epoll loop
 *   - Each worker owns C TCP+TLS connections, each multiplexing many
 *     HTTP/2 streams (nghttp2) over a single socket
 *   - A per-worker token-bucket rate limiter throttles new stream creation
 *     to approximate (global_rate / N) * multiplier requests/sec
 *   - Per-worker latency samples are aggregated lock-free-ish under a
 *     mutex at the end (and periodically for the live view)
 *
 * Build:
 *   gcc -Wall -O3 -march=native -o bench bench.c -lnghttp2 -lssl -lcrypto -lpthread -lrt
 *
 * Usage:
 *   ./bench --url https://host:443/path --duration 30 --rate 1000 \
 *            --threads 4 --conns-per-worker 4 --rate-multiplier 1.0
 *
 * Intended for controlled, authorized measurement of servers you own or
 * are explicitly permitted to test (e.g. your own thesis testbed).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <math.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <nghttp2/nghttp2.h>

/* ------------------------------------------------------------------- */
/* Constants                                                            */
/* ------------------------------------------------------------------- */

#define MAX_URL_LEN            2048
#define MAX_HOST_LEN            256
#define IO_BUF_SIZE            65536
#define MAX_LATENCY_SAMPLES   200000   /* per worker, ring-buffered      */
#define MAX_EPOLL_EVENTS         256
#define STATUS_BUCKETS            6    /* 1xx 2xx 3xx 4xx 5xx other      */

/* ------------------------------------------------------------------- */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------- */

typedef struct connection connection_t;
typedef struct worker worker_t;
typedef struct global_config global_config_t;

/* ------------------------------------------------------------------- */
/* Error categories                                                     */
/* ------------------------------------------------------------------- */

typedef enum {
    ERR_NONE = 0,
    ERR_CONNECT,
    ERR_TLS_HANDSHAKE,
    ERR_ALPN_MISMATCH,
    ERR_HTTP2_SESSION,
    ERR_STREAM_RESET,
    ERR_TIMEOUT,
    ERR_SEND,
    ERR_RECV,
    ERR_OTHER,
    ERR_CATEGORY_COUNT
} error_category_t;

static const char *error_category_name(error_category_t e) {
    switch (e) {
        case ERR_NONE:           return "none";
        case ERR_CONNECT:        return "connect";
        case ERR_TLS_HANDSHAKE:  return "tls_handshake";
        case ERR_ALPN_MISMATCH:  return "alpn_mismatch";
        case ERR_HTTP2_SESSION:  return "http2_session";
        case ERR_STREAM_RESET:   return "stream_reset";
        case ERR_TIMEOUT:        return "timeout";
        case ERR_SEND:           return "send";
        case ERR_RECV:           return "recv";
        default:                 return "other";
    }
}

/* ------------------------------------------------------------------- */
/* Statistics                                                           */
/* ------------------------------------------------------------------- */

typedef struct {
    pthread_mutex_t lock;

    uint64_t requests_sent;
    uint64_t responses_received;

    uint64_t status_buckets[STATUS_BUCKETS]; /* 1xx..5xx, other */
    uint64_t errors[ERR_CATEGORY_COUNT];

    /* Latency samples in microseconds, collected from all workers.
     * Sized generously; if exceeded we stop sampling (throughput /
     * error counters remain exact regardless). */
    double  *latencies_us;
    size_t   latency_cap;
    size_t   latency_count;

    uint64_t bytes_received;
} stats_t;

static void stats_init(stats_t *s, size_t cap) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
    s->latency_cap = cap;
    s->latencies_us = calloc(cap, sizeof(double));
    if (!s->latencies_us) {
        fprintf(stderr, "fatal: cannot allocate latency buffer\n");
        exit(1);
    }
}

static void stats_destroy(stats_t *s) {
    pthread_mutex_destroy(&s->lock);
    free(s->latencies_us);
}

static int status_bucket_index(int status) {
    if (status >= 100 && status < 200) return 0;
    if (status >= 200 && status < 300) return 1;
    if (status >= 300 && status < 400) return 2;
    if (status >= 400 && status < 500) return 3;
    if (status >= 500 && status < 600) return 4;
    return 5;
}

static void stats_record_response(stats_t *s, int status, double latency_us,
                                   size_t bytes) {
    pthread_mutex_lock(&s->lock);
    s->responses_received++;
    s->status_buckets[status_bucket_index(status)]++;
    s->bytes_received += bytes;
    if (s->latency_count < s->latency_cap) {
        s->latencies_us[s->latency_count++] = latency_us;
    }
    pthread_mutex_unlock(&s->lock);
}

static void stats_record_error(stats_t *s, error_category_t e) {
    pthread_mutex_lock(&s->lock);
    s->errors[e]++;
    pthread_mutex_unlock(&s->lock);
}

static void stats_record_sent(stats_t *s) {
    pthread_mutex_lock(&s->lock);
    s->requests_sent++;
    pthread_mutex_unlock(&s->lock);
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Returns percentile value in microseconds. Caller must hold no lock;
 * this takes a private copy under the lock to avoid blocking workers
 * for long during a sort. */
static double stats_percentile(stats_t *s, double pct, double *sorted_copy_out,
                                size_t *n_out, int reuse_sorted) {
    static double *cached = NULL;
    static size_t cached_n = 0;

    if (!reuse_sorted) {
        pthread_mutex_lock(&s->lock);
        size_t n = s->latency_count;
        double *copy = malloc(n * sizeof(double));
        if (copy) memcpy(copy, s->latencies_us, n * sizeof(double));
        pthread_mutex_unlock(&s->lock);

        if (!copy || n == 0) {
            free(copy);
            free(cached);
            cached = NULL;
            cached_n = 0;
            if (n_out) *n_out = 0;
            return 0.0;
        }
        qsort(copy, n, sizeof(double), cmp_double);
        free(cached);
        cached = copy;
        cached_n = n;
    }

    if (n_out) *n_out = cached_n;
    if (sorted_copy_out) *sorted_copy_out = 0; /* unused, kept for API symmetry */
    if (cached_n == 0) return 0.0;

    size_t idx = (size_t)ceil((pct / 100.0) * (double)cached_n) - 1;
    if (idx >= cached_n) idx = cached_n - 1;
    return cached[idx];
}

/* ------------------------------------------------------------------- */
/* Rate limiter (token bucket, per worker)                              */
/* ------------------------------------------------------------------- */

typedef struct {
    double tokens;
    double rate_per_sec;   /* tokens added per second */
    double capacity;
    struct timespec last_refill;
} token_bucket_t;

static void tb_init(token_bucket_t *tb, double rate_per_sec) {
    tb->rate_per_sec = rate_per_sec;
    tb->capacity = rate_per_sec > 1.0 ? rate_per_sec : 1.0;
    tb->tokens = tb->capacity;
    clock_gettime(CLOCK_MONOTONIC, &tb->last_refill);
}

static void tb_refill(token_bucket_t *tb) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - tb->last_refill.tv_sec) +
                      (now.tv_nsec - tb->last_refill.tv_nsec) / 1e9;
    if (elapsed <= 0) return;
    tb->tokens += elapsed * tb->rate_per_sec;
    if (tb->tokens > tb->capacity) tb->tokens = tb->capacity;
    tb->last_refill = now;
}

/* Returns true and consumes a token if available. */
static bool tb_try_consume(token_bucket_t *tb) {
    tb_refill(tb);
    if (tb->tokens >= 1.0) {
        tb->tokens -= 1.0;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------- */
/* Global configuration                                                 */
/* ------------------------------------------------------------------- */

struct global_config {
    char     url[MAX_URL_LEN];
    char     scheme[8];
    char     host[MAX_HOST_LEN];
    char     path[MAX_URL_LEN];
    int      port;

    int      duration_sec;
    double   target_rate;        /* requests / sec, global               */
    int      num_threads;
    int      conns_per_worker;
    double   rate_multiplier;

    volatile sig_atomic_t stop;  /* set by SIGINT or timer                */
};

/* ------------------------------------------------------------------- */
/* Per-stream request state                                             */
/* ------------------------------------------------------------------- */

typedef struct {
    int32_t         stream_id;
    struct timespec t_start;
    int             status;
    size_t          bytes;
    connection_t   *conn;
} stream_ctx_t;

/* ------------------------------------------------------------------- */
/* Connection state                                                     */
/* ------------------------------------------------------------------- */

struct connection {
    int              fd;
    SSL             *ssl;
    nghttp2_session *session;
    worker_t        *worker;

    unsigned char    io_buf[IO_BUF_SIZE];

    bool             want_read;
    bool             want_write;
    bool             handshake_done;
    bool             dead;

    int              epoll_registered;
};

/* ------------------------------------------------------------------- */
/* Worker thread state                                                  */
/* ------------------------------------------------------------------- */

struct worker {
    int               id;
    pthread_t         thread;
    int               epoll_fd;
    int               wake_fd;      /* eventfd used to break epoll_wait   */
    global_config_t  *cfg;
    stats_t          *stats;
    SSL_CTX          *ssl_ctx;

    connection_t    **conns;
    int               num_conns;

    token_bucket_t    limiter;

    struct timespec   t_end;        /* absolute deadline                  */
};

/* ------------------------------------------------------------------- */
/* URL parsing (minimal: https://host[:port]/path)                      */
/* ------------------------------------------------------------------- */

static int parse_url(const char *url, global_config_t *cfg) {
    const char *p = url;

    if (strncmp(p, "https://", 8) != 0) {
        fprintf(stderr, "error: only https:// URLs are supported\n");
        return -1;
    }
    strncpy(cfg->scheme, "https", sizeof(cfg->scheme) - 1);
    p += 8;

    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);

    const char *colon = memchr(p, ':', host_end - p);
    if (colon) {
        size_t hostlen = colon - p;
        if (hostlen >= sizeof(cfg->host)) return -1;
        memcpy(cfg->host, p, hostlen);
        cfg->host[hostlen] = '\0';
        cfg->port = atoi(colon + 1);
    } else {
        size_t hostlen = host_end - p;
        if (hostlen >= sizeof(cfg->host)) return -1;
        memcpy(cfg->host, p, hostlen);
        cfg->host[hostlen] = '\0';
        cfg->port = 443;
    }

    if (slash) {
        strncpy(cfg->path, slash, sizeof(cfg->path) - 1);
    } else {
        strncpy(cfg->path, "/", sizeof(cfg->path) - 1);
    }

    if (cfg->host[0] == '\0' || cfg->port <= 0) {
        fprintf(stderr, "error: could not parse host/port from URL\n");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------- */
/* Socket / TLS setup                                                   */
/* ------------------------------------------------------------------- */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int connect_tcp(const char *host, int port) {
    struct addrinfo hints, *res, *rp;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        if (errno == EINPROGRESS) break; /* nonblocking connect started later */

        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* ALPN protocol list: h2 only, since this tool measures HTTP/2. */
static const unsigned char alpn_protos[] = { 2, 'h', '2' };

static SSL_CTX *create_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos));

    /* Research/benchmarking against a controlled test target: verification
     * is left off by default since test servers commonly use self-signed
     * certs. Enable verification here if testing against a public host. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    return ctx;
}

/* ------------------------------------------------------------------- */
/* nghttp2 <-> BIO glue: send/recv callbacks                            */
/* ------------------------------------------------------------------- */

static ssize_t nghttp2_send_cb(nghttp2_session *session, const uint8_t *data,
                                size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    connection_t *c = user_data;
    if (c->dead) return NGHTTP2_ERR_CALLBACK_FAILURE;

    int rv = SSL_write(c->ssl, data, (int)length);
    if (rv <= 0) {
        int err = SSL_get_error(c->ssl, rv);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return rv;
}

static ssize_t nghttp2_recv_cb(nghttp2_session *session, uint8_t *buf,
                                size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    connection_t *c = user_data;
    if (c->dead) return NGHTTP2_ERR_CALLBACK_FAILURE;

    int rv = SSL_read(c->ssl, buf, (int)length);
    if (rv <= 0) {
        int err = SSL_get_error(c->ssl, rv);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            return 0; /* EOF */
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return rv;
}

static int on_header_cb(nghttp2_session *session, const nghttp2_frame *frame,
                         const uint8_t *name, size_t namelen,
                         const uint8_t *value, size_t valuelen,
                         uint8_t flags, void *user_data) {
    (void)flags; (void)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS) return 0;

    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        stream_ctx_t *sctx = nghttp2_session_get_stream_user_data(
            session, frame->hd.stream_id);
        if (sctx) {
            char tmp[8] = {0};
            size_t n = valuelen < 7 ? valuelen : 7;
            memcpy(tmp, value, n);
            sctx->status = atoi(tmp);
        }
    }
    return 0;
}

static int on_data_chunk_cb(nghttp2_session *session, uint8_t flags,
                             int32_t stream_id, const uint8_t *data,
                             size_t len, void *user_data) {
    (void)flags; (void)data; (void)user_data;
    stream_ctx_t *sctx = nghttp2_session_get_stream_user_data(session, stream_id);
    if (sctx) sctx->bytes += len;
    return 0;
}

static double timespec_diff_us(const struct timespec *start,
                                const struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1e6 +
           (end->tv_nsec - start->tv_nsec) / 1e3;
}

static int on_stream_close_cb(nghttp2_session *session, int32_t stream_id,
                               uint32_t error_code, void *user_data) {
    connection_t *c = user_data;
    stream_ctx_t *sctx = nghttp2_session_get_stream_user_data(session, stream_id);
    if (!sctx) return 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double lat_us = timespec_diff_us(&sctx->t_start, &now);

    if (error_code != 0 || sctx->status == 0) {
        stats_record_error(c->worker->stats, ERR_STREAM_RESET);
    } else {
        stats_record_response(c->worker->stats, sctx->status, lat_us, sctx->bytes);
    }

    free(sctx);
    return 0;
}

/* ------------------------------------------------------------------- */
/* Connection lifecycle                                                 */
/* ------------------------------------------------------------------- */

static void conn_close(connection_t *c) {
    if (!c) return;
    if (c->dead) return;
    c->dead = true;

    if (c->epoll_registered) {
        epoll_ctl(c->worker->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
        c->epoll_registered = 0;
    }
    if (c->session) {
        nghttp2_session_del(c->session);
        c->session = NULL;
    }
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

static void conn_free(connection_t *c) {
    if (!c) return;
    conn_close(c);
    free(c);
}

static void conn_update_epoll(connection_t *c) {
    struct epoll_event ev;
    ev.data.ptr = c;
    ev.events = EPOLLET; /* edge-triggered */
    if (c->want_read) ev.events |= EPOLLIN;
    if (c->want_write) ev.events |= EPOLLOUT;

    if (!c->epoll_registered) {
        if (epoll_ctl(c->worker->epoll_fd, EPOLL_CTL_ADD, c->fd, &ev) == 0) {
            c->epoll_registered = 1;
        }
    } else {
        epoll_ctl(c->worker->epoll_fd, EPOLL_CTL_MOD, c->fd, &ev);
    }
}

static int conn_init_nghttp2(connection_t *c) {
    nghttp2_session_callbacks *cbs;
    if (nghttp2_session_callbacks_new(&cbs) != 0) return -1;

    nghttp2_session_callbacks_set_send_callback(cbs, nghttp2_send_cb);
    nghttp2_session_callbacks_set_recv_callback(cbs, nghttp2_recv_cb);
    nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_cb);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_cb);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_stream_close_cb);

    int rv = nghttp2_session_client_new(&c->session, cbs, c);
    nghttp2_session_callbacks_del(cbs);
    if (rv != 0) return -1;

    nghttp2_settings_entry iv[] = {
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 2000 },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 15564991 },
        { NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384 },
        { NGHTTP2_SETTINGS_ENABLE_PUSH, 0 },
    };
    nghttp2_submit_settings(c->session, NGHTTP2_FLAG_NONE, iv,
                             sizeof(iv) / sizeof(iv[0]));
    return 0;
}

static connection_t *conn_create(worker_t *w) {
    connection_t *c = calloc(1, sizeof(connection_t));
    if (!c) return NULL;
    c->worker = w;
    c->fd = -1;

    c->fd = connect_tcp(w->cfg->host, w->cfg->port);
    if (c->fd < 0) {
        stats_record_error(w->stats, ERR_CONNECT);
        free(c);
        return NULL;
    }
    set_nonblocking(c->fd);

    c->ssl = SSL_new(w->ssl_ctx);
    if (!c->ssl) {
        stats_record_error(w->stats, ERR_TLS_HANDSHAKE);
        close(c->fd);
        free(c);
        return NULL;
    }
    SSL_set_fd(c->ssl, c->fd);
    SSL_set_tlsext_host_name(c->ssl, w->cfg->host);
    SSL_set1_host(c->ssl, w->cfg->host);
    SSL_set_connect_state(c->ssl);

    c->want_read = true;
    c->want_write = true;
    conn_update_epoll(c);

    return c;
}

/* Drives the TLS handshake; once complete, verifies ALPN==h2 and starts
 * the HTTP/2 session. Returns 0 on success/in-progress, -1 on fatal error. */
static int conn_do_handshake(connection_t *c) {
    int rv = SSL_do_handshake(c->ssl);
    if (rv == 1) {
        const unsigned char *alpn = NULL;
        unsigned int alpn_len = 0;
        SSL_get0_alpn_selected(c->ssl, &alpn, &alpn_len);
        if (!alpn || alpn_len != 2 || memcmp(alpn, "h2", 2) != 0) {
            stats_record_error(c->worker->stats, ERR_ALPN_MISMATCH);
            return -1;
        }
        if (conn_init_nghttp2(c) != 0) {
            stats_record_error(c->worker->stats, ERR_HTTP2_SESSION);
            return -1;
        }
        c->handshake_done = true;
        c->want_read = true;
        c->want_write = nghttp2_session_want_write(c->session);
        return 0;
    }

    int err = SSL_get_error(c->ssl, rv);
    if (err == SSL_ERROR_WANT_READ) {
        c->want_read = true; c->want_write = false;
        return 0;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        c->want_read = false; c->want_write = true;
        return 0;
    }
    stats_record_error(c->worker->stats, ERR_TLS_HANDSHAKE);
    return -1;
}

/* Submits one GET request as a new HTTP/2 stream. */
static int conn_submit_request(connection_t *c) {
    global_config_t *cfg = c->worker->cfg;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", cfg->port);

    nghttp2_nv hdrs[] = {
        { (uint8_t *)":method", (uint8_t *)"GET", 7, 3,
          NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)":scheme", (uint8_t *)"https", 7, 5,
          NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)":authority", (uint8_t *)cfg->host, 10,
          strlen(cfg->host), NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)":path", (uint8_t *)cfg->path, 5,
          strlen(cfg->path), NGHTTP2_NV_FLAG_NONE },
    };

    stream_ctx_t *sctx = calloc(1, sizeof(stream_ctx_t));
    if (!sctx) return -1;
    clock_gettime(CLOCK_MONOTONIC, &sctx->t_start);
    sctx->conn = c;

    int32_t sid = nghttp2_submit_request(c->session, NULL, hdrs,
                                          sizeof(hdrs) / sizeof(hdrs[0]),
                                          NULL, sctx);
    if (sid < 0) {
        free(sctx);
        return -1;
    }
    sctx->stream_id = sid;
    stats_record_sent(c->worker->stats);

    c->want_write = true;
    return 0;
}

/* Pumps nghttp2 send/recv; returns -1 if the connection should be torn
 * down. */
static int conn_process_io(connection_t *c) {
    if (nghttp2_session_send(c->session) != 0) {
        stats_record_error(c->worker->stats, ERR_SEND);
        return -1;
    }
    int rv = nghttp2_session_recv(c->session);
    if (rv != 0 && rv != NGHTTP2_ERR_WOULDBLOCK) {
        stats_record_error(c->worker->stats, ERR_RECV);
        return -1;
    }

    c->want_read = true;
    c->want_write = nghttp2_session_want_write(c->session);
    return 0;
}

/* ------------------------------------------------------------------- */
/* Worker event loop                                                    */
/* ------------------------------------------------------------------- */

static double now_seconds_left(worker_t *w) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (w->t_end.tv_sec - now.tv_sec) +
           (w->t_end.tv_nsec - now.tv_nsec) / 1e9;
}

static void worker_maybe_issue_requests(worker_t *w) {
    for (int i = 0; i < w->num_conns; i++) {
        connection_t *c = w->conns[i];
        if (!c || c->dead || !c->handshake_done) continue;

        if (conn_submit_request(c) != 0) {
            continue;
        }
    }
}

static void *worker_main(void *arg) {
    worker_t *w = arg;

    w->epoll_fd = epoll_create1(0);
    if (w->epoll_fd < 0) {
        fprintf(stderr, "worker %d: epoll_create1 failed: %s\n",
                w->id, strerror(errno));
        return NULL;
    }
    w->wake_fd = eventfd(0, EFD_NONBLOCK);

    struct epoll_event wake_ev = { .events = EPOLLIN, .data.ptr = NULL };
    epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, w->wake_fd, &wake_ev);

    w->conns = calloc(w->cfg->conns_per_worker, sizeof(connection_t *));
    w->num_conns = w->cfg->conns_per_worker;

    for (int i = 0; i < w->num_conns; i++) {
        w->conns[i] = conn_create(w);
    }

    double per_worker_rate = (w->cfg->target_rate / w->cfg->num_threads) *
                              w->cfg->rate_multiplier;

    clock_gettime(CLOCK_MONOTONIC, &w->t_end);
    w->t_end.tv_sec += w->cfg->duration_sec;

    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (!w->cfg->stop && now_seconds_left(w) > 0) {
        int timeout_ms = 20; /* small tick so we can drive the rate limiter */
        int n = epoll_wait(w->epoll_fd, events, MAX_EPOLL_EVENTS, timeout_ms);

        for (int i = 0; i < n; i++) {
            connection_t *c = events[i].data.ptr;
            if (c == NULL) continue; /* wake_fd, nothing to do */
            if (c->dead) continue;

            if (!c->handshake_done) {
                if (conn_do_handshake(c) != 0) {
                    conn_close(c);
                    continue;
                }
                conn_update_epoll(c);
                continue;
            }

            if (conn_process_io(c) != 0) {
                conn_close(c);
                continue;
            }
            conn_update_epoll(c);
        }

        /* Reconnect any dead connections so the test keeps running at
         * roughly constant concurrency (keep-alive / session reuse for
         * the common case, reconnect only on failure). */
        for (int i = 0; i < w->num_conns; i++) {
            if (w->conns[i] && w->conns[i]->dead) {
                conn_free(w->conns[i]);
                w->conns[i] = conn_create(w);
            }
        }

        worker_maybe_issue_requests(w);
    }

    for (int i = 0; i < w->num_conns; i++) {
        conn_free(w->conns[i]);
    }
    free(w->conns);
    close(w->epoll_fd);
    close(w->wake_fd);

    return NULL;
}

/* ------------------------------------------------------------------- */
/* Live monitor thread                                                  */
/* ------------------------------------------------------------------- */

typedef struct {
    global_config_t *cfg;
    stats_t *stats;
} monitor_arg_t;

static void *monitor_main(void *arg) {
    monitor_arg_t *m = arg;
    uint64_t last_responses = 0;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (!m->cfg->stop) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) +
                          (now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= m->cfg->duration_sec) break;

        pthread_mutex_lock(&m->stats->lock);
        uint64_t resp = m->stats->responses_received;
        uint64_t sent = m->stats->requests_sent;
        pthread_mutex_unlock(&m->stats->lock);

        uint64_t delta = resp - last_responses;
        last_responses = resp;

        fprintf(stderr,
                "\r[%5.1fs] sent=%-8llu recv=%-8llu inst_rps=%-6llu",
                elapsed,
                (unsigned long long)sent,
                (unsigned long long)resp,
                (unsigned long long)delta);
        fflush(stderr);

        sleep(1);
    }
    fprintf(stderr, "\n");
    return NULL;
}

/* ------------------------------------------------------------------- */
/* Signal handling                                                      */
/* ------------------------------------------------------------------- */

static global_config_t *g_cfg_for_signal = NULL;

static void handle_sigint(int sig) {
    (void)sig;
    if (g_cfg_for_signal) g_cfg_for_signal->stop = 1;
}

/* ------------------------------------------------------------------- */
/* Argument parsing                                                     */
/* ------------------------------------------------------------------- */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --url https://host[:port]/path [options]\n"
        "\n"
        "Options:\n"
        "  --url URL                 Target URL (HTTPS only, required)\n"
        "  --duration SEC            Test duration in seconds (default 10)\n"
        "  --rate REQ_PER_SEC        Target aggregate request rate (default 100)\n"
        "  --threads N               Worker thread count (default 4)\n"
        "  --conns-per-worker N      TCP connections per worker (default 2)\n"
        "  --rate-multiplier X       Multiplies the effective rate (default 1.0)\n"
        "  -h, --help                Show this help\n",
        prog);
}

static int parse_args(int argc, char **argv, global_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->duration_sec = 10;
    cfg->target_rate = 100;
    cfg->num_threads = 4;
    cfg->conns_per_worker = 2;
    cfg->rate_multiplier = 1.0;

    const char *url = NULL;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--url") == 0) && i + 1 < argc) {
            url = argv[++i];
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            cfg->duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            cfg->target_rate = atof(argv[++i]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            cfg->num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--conns-per-worker") == 0 && i + 1 < argc) {
            cfg->conns_per_worker = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rate-multiplier") == 0 && i + 1 < argc) {
            cfg->rate_multiplier = atof(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }

    if (!url) {
        fprintf(stderr, "error: --url is required\n");
        print_usage(argv[0]);
        return -1;
    }
    strncpy(cfg->url, url, sizeof(cfg->url) - 1);

    if (parse_url(cfg->url, cfg) != 0) return -1;

    if (cfg->duration_sec <= 0 || cfg->num_threads <= 0 ||
        cfg->conns_per_worker <= 0 || cfg->target_rate <= 0 ||
        cfg->rate_multiplier <= 0) {
        fprintf(stderr, "error: numeric parameters must be positive\n");
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------- */
/* Final report                                                         */
/* ------------------------------------------------------------------- */

static void print_report(global_config_t *cfg, stats_t *s) {
    size_t n;
    double p50 = stats_percentile(s, 50.0, NULL, &n, 0);
    double p95 = stats_percentile(s, 95.0, NULL, &n, 1);
    double p99 = stats_percentile(s, 99.0, NULL, &n, 1);

    double actual_rps = (double)s->responses_received / cfg->duration_sec;

    printf("\n================= RESULTS =================\n");
    printf("Target:            %s\n", cfg->url);
    printf("Duration:          %d s\n", cfg->duration_sec);
    printf("Threads:           %d\n", cfg->num_threads);
    printf("Conns/worker:      %d\n", cfg->conns_per_worker);
    printf("Target rate:       %.1f req/s (x%.2f multiplier)\n",
           cfg->target_rate, cfg->rate_multiplier);
    printf("---------------------------------------------\n");
    printf("Requests sent:     %llu\n", (unsigned long long)s->requests_sent);
    printf("Responses:         %llu\n", (unsigned long long)s->responses_received);
    printf("Achieved rate:     %.1f req/s\n", actual_rps);
    printf("Bytes received:    %llu\n", (unsigned long long)s->bytes_received);
    printf("---------------------------------------------\n");
    printf("Latency (n=%zu samples):\n", n);
    printf("  p50: %8.2f ms\n", p50 / 1000.0);
    printf("  p95: %8.2f ms\n", p95 / 1000.0);
    printf("  p99: %8.2f ms\n", p99 / 1000.0);
    printf("---------------------------------------------\n");
    printf("Status codes:\n");
    const char *labels[STATUS_BUCKETS] = { "1xx", "2xx", "3xx", "4xx", "5xx", "other" };
    for (int i = 0; i < STATUS_BUCKETS; i++) {
        if (s->status_buckets[i])
            printf("  %-6s %llu\n", labels[i],
                   (unsigned long long)s->status_buckets[i]);
    }
    printf("---------------------------------------------\n");
    printf("Errors:\n");
    for (int i = 1; i < ERR_CATEGORY_COUNT; i++) {
        if (s->errors[i])
            printf("  %-14s %llu\n", error_category_name(i),
                   (unsigned long long)s->errors[i]);
    }
    printf("=============================================\n");
}

/* ------------------------------------------------------------------- */
/* main                                                                 */
/* ------------------------------------------------------------------- */

int main(int argc, char **argv) {
    global_config_t cfg;
    if (parse_args(argc, argv, &cfg) != 0) return 1;

    g_cfg_for_signal = &cfg;
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    SSL_library_init();
    SSL_load_error_strings();

    stats_t stats;
    stats_init(&stats, (size_t)MAX_LATENCY_SAMPLES * cfg.num_threads);

    worker_t *workers = calloc(cfg.num_threads, sizeof(worker_t));
    if (!workers) {
        fprintf(stderr, "fatal: cannot allocate worker array\n");
        return 1;
    }

    printf("Starting HTTP/2 benchmark: %s\n", cfg.url);
    printf("duration=%ds rate=%.1freq/s threads=%d conns/worker=%d multiplier=%.2f\n",
           cfg.duration_sec, cfg.target_rate, cfg.num_threads,
           cfg.conns_per_worker, cfg.rate_multiplier);

    for (int i = 0; i < cfg.num_threads; i++) {
        workers[i].id = i;
        workers[i].cfg = &cfg;
        workers[i].stats = &stats;
        workers[i].ssl_ctx = create_ssl_ctx();
        if (!workers[i].ssl_ctx) {
            fprintf(stderr, "fatal: SSL_CTX_new failed for worker %d\n", i);
            return 1;
        }
        if (pthread_create(&workers[i].thread, NULL, worker_main, &workers[i]) != 0) {
            fprintf(stderr, "fatal: pthread_create failed for worker %d\n", i);
            return 1;
        }
    }

    monitor_arg_t marg = { .cfg = &cfg, .stats = &stats };
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor_main, &marg);

    for (int i = 0; i < cfg.num_threads; i++) {
        pthread_join(workers[i].thread, NULL);
    }
    cfg.stop = 1;
    pthread_join(monitor_thread, NULL);

    for (int i = 0; i < cfg.num_threads; i++) {
        SSL_CTX_free(workers[i].ssl_ctx);
    }
    free(workers);

    print_report(&cfg, &stats);
    stats_destroy(&stats);

    return 0;
}
