/*
 * tester.c - Internal HTTP/2 server capacity benchmarking tool.
 *
 * Compile: gcc -O2 -o tester tester.c -lnghttp2 -lssl -lcrypto -lpthread
 *
 * DESIGN NOTES (read before use):
 * - Intended for benchmarking servers you own/control. TLS certificate
 *   verification is disabled (SSL_VERIFY_NONE) to support self-signed certs
 *   commonly used on internal/dev servers. Re-enable verification if you
 *   point this at anything with a real CA-issued cert.
 * - User-Agent is a single fixed, self-identifying string
 *   (see UA_STRING below) rather than randomized per-request. Spoofing a
 *   random mix of browser/OS User-Agents serves no benchmarking purpose --
 *   it only disguises synthetic load as organic traffic, which is not
 *   appropriate for a tool whose stated purpose is capacity testing of your
 *   own infrastructure. If your server logs/dashboards need a way to filter
 *   out benchmark traffic, this fixed UA makes that trivial.
 * - "-w" (worker pool size) is implemented as the max number of concurrent
 *   in-flight requests allowed per pooled connection (i.e. how many
 *   "virtual users" share one multiplexed HTTP/2 connection at once).
 * - Connections are pooled and reused for the whole test (HTTP/2
 *   multiplexing). If a pooled connection drops mid-test it is closed and
 *   not automatically replaced -- effective concurrency will drop for the
 *   remainder of the run. That's a known simplification, not a bug.
 * - Only https:// URLs are supported (HTTP/2 here always negotiates via
 *   TLS ALPN, matching the "-h2 via OpenSSL/ALPN" requirement).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <nghttp2/nghttp2.h>

#define UA_STRING "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"
#define MAX_INFLIGHT 256
#define EPOLL_MAX_EVENTS 64
#define RECV_BUF_SIZE 16384
#define GRACE_PERIOD_SEC 2.0

/* ---------- Types ---------- */

typedef enum {
    CONN_CONNECTING,
    CONN_TLS_HANDSHAKE,
    CONN_ACTIVE,
    CONN_CLOSED
} conn_state_t;

typedef struct {
    int in_use;
    struct timespec start;
    int status_code;
} stream_ctx_t;

typedef struct connection {
    int fd;
    SSL *ssl;
    nghttp2_session *session;
    conn_state_t state;
    int active_streams;
    int epoll_registered_events; /* 0 = not registered yet */
    stream_ctx_t inflight[MAX_INFLIGHT];
} connection_t;

typedef struct {
    int id;
    pthread_t thread;
    int epoll_fd;
    connection_t *conns;
    int num_conns;
} worker_t;

typedef struct {
    int duration;
    int rate;
    int threads;
    int conns_per_thread;
    double multiplier;
    int worker_pool_size;
    char authority[300];
} config_t;

typedef struct {
    char host[256];
    char path[2048];
    int port;
} url_t;

/* ---------- Globals ---------- */

static config_t g_cfg;
static url_t g_target;
static SSL_CTX *g_ssl_ctx = NULL;

static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;
static SSL_SESSION *g_cached_session = NULL;

static volatile sig_atomic_t g_running = 1;

static atomic_ulong g_total_requests = 0;
static atomic_ulong g_success = 0;
static atomic_ulong g_errors = 0;
static atomic_ulong g_latency_sum_us = 0;
static atomic_ulong g_latency_count = 0;

/* ---------- URL parsing ---------- */

static int parse_url(const char *url, url_t *out) {
    if (strncmp(url, "https://", 8) != 0) return -1;
    const char *p = url + 8;
    if (*p == '\0') return -1;
    const char *slash = strchr(p, '/');
    const char *hostend = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(hostend - p));

    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen == 0 || hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        out->port = atoi(colon + 1);
        if (out->port <= 0 || out->port > 65535) return -1;
    } else {
        size_t hlen = (size_t)(hostend - p);
        if (hlen == 0 || hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        out->port = 443;
    }

    if (slash) {
        if (strlen(slash) >= sizeof(out->path)) return -1;
        strcpy(out->path, slash);
    } else {
        strcpy(out->path, "/");
    }
    return 0;
}

/* ---------- SSL context ---------- */

static SSL_CTX *create_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        fprintf(stderr, "SSL_CTX_new failed\n");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);

    static const unsigned char alpn_protos[] = {2, 'h', '2'};
    if (SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos)) != 0) {
        fprintf(stderr, "SSL_CTX_set_alpn_protos failed\n");
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* Internal/dev servers commonly use self-signed certs. Adjust this if
     * you point the tool at something with a CA-issued certificate. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

/* ---------- nghttp2 callbacks ---------- */

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data) {
    (void)session;
    (void)flags;
    connection_t *conn = (connection_t *)user_data;
    int rv = SSL_write(conn->ssl, data, (int)length);
    if (rv > 0) return rv;
    int err = SSL_get_error(conn->ssl, rv);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
    (void)flags;
    (void)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        stream_ctx_t *sctx = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (sctx) {
            char buf[16];
            size_t n = valuelen < sizeof(buf) - 1 ? valuelen : sizeof(buf) - 1;
            memcpy(buf, value, n);
            buf[n] = '\0';
            sctx->status_code = atoi(buf);
        }
    }
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                                   void *user_data) {
    (void)session;
    (void)frame;
    (void)user_data;
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                        int32_t stream_id, const uint8_t *data,
                                        size_t len, void *user_data) {
    (void)session;
    (void)flags;
    (void)stream_id;
    (void)data;
    (void)len;
    (void)user_data;
    return 0; /* body content not needed for benchmarking */
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                     uint32_t error_code, void *user_data) {
    connection_t *conn = (connection_t *)user_data;
    stream_ctx_t *sctx = nghttp2_session_get_stream_user_data(session, stream_id);

    atomic_fetch_add(&g_total_requests, 1UL);

    if (sctx) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long lat_us = (now.tv_sec - sctx->start.tv_sec) * 1000000L +
                      (now.tv_nsec - sctx->start.tv_nsec) / 1000L;
        if (lat_us < 0) lat_us = 0;

        if (error_code == 0 && sctx->status_code >= 200 && sctx->status_code < 400) {
            atomic_fetch_add(&g_success, 1UL);
        } else {
            atomic_fetch_add(&g_errors, 1UL);
        }
        atomic_fetch_add(&g_latency_sum_us, (unsigned long)lat_us);
        atomic_fetch_add(&g_latency_count, 1UL);
        sctx->in_use = 0;
    } else {
        atomic_fetch_add(&g_errors, 1UL);
    }

    if (conn->active_streams > 0) conn->active_streams--;
    return 0;
}

static int error_callback(nghttp2_session *session, const char *msg, size_t len,
                           void *user_data) {
    (void)session;
    (void)user_data;
    fprintf(stderr, "[nghttp2] %.*s\n", (int)len, msg);
    return 0;
}

/* ---------- Connection helpers ---------- */

static stream_ctx_t *alloc_stream_ctx(connection_t *conn) {
    for (int i = 0; i < MAX_INFLIGHT; i++) {
        if (!conn->inflight[i].in_use) {
            conn->inflight[i].in_use = 1;
            conn->inflight[i].status_code = 0;
            return &conn->inflight[i];
        }
    }
    return NULL;
}

static int epoll_update(int epfd, connection_t *conn, uint32_t events) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = conn;

    if (conn->epoll_registered_events == 0) {
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn->fd, &ev) < 0) {
            perror("epoll_ctl(ADD)");
            return -1;
        }
    } else if ((int)events != conn->epoll_registered_events) {
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
            perror("epoll_ctl(MOD)");
            return -1;
        }
    }
    conn->epoll_registered_events = (int)events;
    return 0;
}

static int connect_nonblocking(const char *host, int port) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(gai));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("fcntl(O_NONBLOCK)");
            close(fd);
            fd = -1;
            continue;
        }

        int rv = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rv == 0 || (rv < 0 && errno == EINPROGRESS)) {
            break; /* in-progress or immediate success, both fine */
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) fprintf(stderr, "connect_nonblocking: failed to connect to %s:%d\n", host, port);
    return fd;
}

/* Returns: 0 = handshake complete, 1 = still in progress, -1 = error */
static int do_tls_handshake(connection_t *conn, int epfd) {
    int rv = SSL_connect(conn->ssl);
    if (rv == 1) return 0;

    int err = SSL_get_error(conn->ssl, rv);
    if (err == SSL_ERROR_WANT_READ) {
        epoll_update(epfd, conn, EPOLLIN);
        return 1;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        epoll_update(epfd, conn, EPOLLOUT);
        return 1;
    }
    fprintf(stderr, "TLS handshake failed: %s\n", ERR_reason_error_string(ERR_get_error()));
    return -1;
}

static int setup_h2_session(connection_t *conn) {
    /* Require h2 via ALPN -- if the server didn't negotiate it, refuse. */
    const unsigned char *alpn = NULL;
    unsigned int alpnlen = 0;
    SSL_get0_alpn_selected(conn->ssl, &alpn, &alpnlen);
    if (!alpn || alpnlen != 2 || memcmp(alpn, "h2", 2) != 0) {
        fprintf(stderr, "Server did not negotiate HTTP/2 (h2) via ALPN\n");
        return -1;
    }

    nghttp2_session_callbacks *cbs;
    if (nghttp2_session_callbacks_new(&cbs) != 0) {
        fprintf(stderr, "nghttp2_session_callbacks_new failed\n");
        return -1;
    }
    nghttp2_session_callbacks_set_send_callback(cbs, send_callback);
    nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_stream_close_callback);
    nghttp2_session_callbacks_set_error_callback(cbs, error_callback);

    int rv = nghttp2_session_client_new(&conn->session, cbs, conn);
    nghttp2_session_callbacks_del(cbs);
    if (rv != 0) {
        fprintf(stderr, "nghttp2_session_client_new: %s\n", nghttp2_strerror(rv));
        return -1;
    }

    nghttp2_settings_entry iv[1] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, MAX_INFLIGHT}};
    rv = nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, iv, 1);
    if (rv != 0) {
        fprintf(stderr, "nghttp2_submit_settings: %s\n", nghttp2_strerror(rv));
        return -1;
    }
    return 0;
}

static int submit_request(connection_t *conn) {
    stream_ctx_t *sctx = alloc_stream_ctx(conn);
    if (!sctx) return -1; /* pool full, caller should not have picked this conn */

    clock_gettime(CLOCK_MONOTONIC, &sctx->start);

    nghttp2_nv nva[5];
    nva[0] = (nghttp2_nv){(uint8_t *)":method", (uint8_t *)"GET", 7, 3, NGHTTP2_NV_FLAG_NONE};
    nva[1] = (nghttp2_nv){(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE};
    nva[2] = (nghttp2_nv){(uint8_t *)":authority", (uint8_t *)g_cfg.authority, 10,
                           strlen(g_cfg.authority), NGHTTP2_NV_FLAG_NONE};
    nva[3] = (nghttp2_nv){(uint8_t *)":path", (uint8_t *)g_target.path, 5,
                           strlen(g_target.path), NGHTTP2_NV_FLAG_NONE};
    nva[4] = (nghttp2_nv){(uint8_t *)"user-agent", (uint8_t *)UA_STRING, 10,
                           strlen(UA_STRING), NGHTTP2_NV_FLAG_NONE};

    int32_t sid = nghttp2_submit_request(conn->session, NULL, nva, 5, NULL, sctx);
    if (sid < 0) {
        fprintf(stderr, "nghttp2_submit_request: %s\n", nghttp2_strerror((int)sid));
        sctx->in_use = 0;
        return -1;
    }
    conn->active_streams++;
    return 0;
}

static int flush_session(connection_t *conn, int epfd) {
    int rv = nghttp2_session_send(conn->session);
    if (rv != 0) {
        fprintf(stderr, "nghttp2_session_send: %s\n", nghttp2_strerror(rv));
        return -1;
    }
    uint32_t events = EPOLLIN;
    if (nghttp2_session_want_write(conn->session)) events |= EPOLLOUT;
    return epoll_update(epfd, conn, events);
}

static int do_recv(connection_t *conn) {
    uint8_t buf[RECV_BUF_SIZE];
    for (;;) {
        int n = SSL_read(conn->ssl, buf, sizeof(buf));
        if (n > 0) {
            ssize_t rv = nghttp2_session_mem_recv(conn->session, buf, (size_t)n);
            if (rv < 0) {
                fprintf(stderr, "nghttp2_session_mem_recv: %s\n", nghttp2_strerror((int)rv));
                return -1;
            }
            continue;
        }
        int err = SSL_get_error(conn->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
        if (err == SSL_ERROR_ZERO_RETURN) return -1; /* peer closed cleanly */
        return -1; /* real error */
    }
}

static int open_connection(connection_t *conn) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = connect_nonblocking(g_target.host, g_target.port);
    if (conn->fd < 0) return -1;

    conn->ssl = SSL_new(g_ssl_ctx);
    if (!conn->ssl) {
        fprintf(stderr, "SSL_new failed\n");
        close(conn->fd);
        conn->fd = -1;
        return -1;
    }
    SSL_set_fd(conn->ssl, conn->fd);
    SSL_set_connect_state(conn->ssl);
    SSL_set_tlsext_host_name(conn->ssl, g_target.host); /* SNI */

    pthread_mutex_lock(&g_session_mutex);
    if (g_cached_session) SSL_set_session(conn->ssl, g_cached_session); /* resumption */
    pthread_mutex_unlock(&g_session_mutex);

    conn->state = CONN_CONNECTING;
    conn->epoll_registered_events = 0;
    return 0;
}

static void close_connection(connection_t *conn, int epfd) {
    if (conn->state == CONN_CLOSED) return;

    if (conn->epoll_registered_events != 0 && conn->fd >= 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL);
    }
    if (conn->session) {
        nghttp2_session_del(conn->session);
        conn->session = NULL;
    }
    if (conn->ssl) {
        if (conn->state == CONN_ACTIVE && SSL_session_reused(conn->ssl) == 0) {
            SSL_SESSION *s = SSL_get1_session(conn->ssl); /* cache for resumption */
            if (s) {
                pthread_mutex_lock(&g_session_mutex);
                if (g_cached_session) SSL_SESSION_free(g_cached_session);
                g_cached_session = s;
                pthread_mutex_unlock(&g_session_mutex);
            }
        }
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    conn->state = CONN_CLOSED;
}

static void handle_event(worker_t *w, connection_t *conn, uint32_t events) {
    if (events & (EPOLLERR | EPOLLHUP)) {
        close_connection(conn, w->epoll_fd);
        return;
    }

    if (conn->state == CONN_CONNECTING) {
        int err = 0;
        socklen_t elen = sizeof(err);
        if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
            fprintf(stderr, "connect() failed: %s\n", strerror(err ? err : errno));
            close_connection(conn, w->epoll_fd);
            return;
        }
        conn->state = CONN_TLS_HANDSHAKE;
        /* fall through to attempt the handshake immediately */
    }

    if (conn->state == CONN_TLS_HANDSHAKE) {
        int rv = do_tls_handshake(conn, w->epoll_fd);
        if (rv < 0) {
            close_connection(conn, w->epoll_fd);
            return;
        }
        if (rv == 1) return; /* still handshaking */

        if (setup_h2_session(conn) < 0) {
            close_connection(conn, w->epoll_fd);
            return;
        }
        conn->state = CONN_ACTIVE;
        flush_session(conn, w->epoll_fd);
        return;
    }

    if (conn->state == CONN_ACTIVE) {
        if (events & EPOLLIN) {
            if (do_recv(conn) < 0) {
                close_connection(conn, w->epoll_fd);
                return;
            }
        }
        if (nghttp2_session_want_write(conn->session) || (events & EPOLLOUT)) {
            if (flush_session(conn, w->epoll_fd) < 0) {
                close_connection(conn, w->epoll_fd);
                return;
            }
        }
    }
}

static connection_t *pick_active_conn(worker_t *w, int *rr) {
    for (int i = 0; i < w->num_conns; i++) {
        int idx = (*rr + i) % w->num_conns;
        connection_t *c = &w->conns[idx];
        if (c->state == CONN_ACTIVE && c->active_streams < g_cfg.worker_pool_size) {
            *rr = (idx + 1) % w->num_conns;
            return c;
        }
    }
    return NULL;
}

/* ---------- Worker thread ---------- */

static void *worker_main(void *arg) {
    worker_t *w = (worker_t *)arg;

    w->epoll_fd = epoll_create1(0);
    if (w->epoll_fd < 0) {
        perror("epoll_create1");
        return NULL;
    }

    w->conns = calloc((size_t)g_cfg.conns_per_thread, sizeof(connection_t));
    if (!w->conns) {
        fprintf(stderr, "worker %d: calloc failed\n", w->id);
        close(w->epoll_fd);
        return NULL;
    }
    w->num_conns = g_cfg.conns_per_thread;

    for (int i = 0; i < w->num_conns; i++) {
        connection_t *c = &w->conns[i];
        if (open_connection(c) < 0) {
            fprintf(stderr, "worker %d: failed to open connection %d\n", w->id, i);
            c->state = CONN_CLOSED;
            continue;
        }
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLOUT; /* wait for connect() completion first */
        ev.data.ptr = c;
        if (epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, c->fd, &ev) < 0) {
            perror("epoll_ctl(ADD connect)");
            close_connection(c, w->epoll_fd);
        } else {
            c->epoll_registered_events = EPOLLOUT;
        }
    }

    double per_thread_rate = (g_cfg.rate * g_cfg.multiplier) / (double)g_cfg.threads;
    if (per_thread_rate <= 0) per_thread_rate = 1;
    double interval_ns = 1e9 / per_thread_rate;

    struct timespec next_send;
    clock_gettime(CLOCK_MONOTONIC, &next_send);
    int rr = 0;

    struct epoll_event events[EPOLL_MAX_EVENTS];

    while (g_running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long wait_us = (next_send.tv_sec - now.tv_sec) * 1000000L +
                       (next_send.tv_nsec - now.tv_nsec) / 1000L;
        int timeout_ms = 50;
        if (wait_us > 0) {
            timeout_ms = (int)(wait_us / 1000);
            if (timeout_ms > 50) timeout_ms = 50;
        } else {
            timeout_ms = 0;
        }

        int n = epoll_wait(w->epoll_fd, events, EPOLL_MAX_EVENTS, timeout_ms);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            handle_event(w, (connection_t *)events[i].data.ptr, events[i].events);
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        long diff_ns = (now.tv_sec - next_send.tv_sec) * 1000000000L +
                       (now.tv_nsec - next_send.tv_nsec);
        while (diff_ns >= 0) {
            connection_t *conn = pick_active_conn(w, &rr);
            if (conn) {
                if (submit_request(conn) == 0) {
                    flush_session(conn, w->epoll_fd);
                }
            }
            /* advance schedule regardless, so we don't spin-loop when the
             * whole pool is saturated */
            long add_ns = (long)interval_ns;
            next_send.tv_nsec += add_ns;
            while (next_send.tv_nsec >= 1000000000L) {
                next_send.tv_nsec -= 1000000000L;
                next_send.tv_sec++;
            }
            diff_ns = (now.tv_sec - next_send.tv_sec) * 1000000000L +
                      (now.tv_nsec - next_send.tv_nsec);
            if (!conn) break; /* pool saturated: don't burst-catch-up once free */
        }
    }

    /* Graceful shutdown: drain in-flight streams briefly before closing. */
    struct timespec grace_start;
    clock_gettime(CLOCK_MONOTONIC, &grace_start);
    for (;;) {
        int any_pending = 0;
        for (int i = 0; i < w->num_conns; i++) {
            if (w->conns[i].state == CONN_ACTIVE && w->conns[i].active_streams > 0) {
                any_pending = 1;
                break;
            }
        }
        if (!any_pending) break;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - grace_start.tv_sec) +
                          (double)(now.tv_nsec - grace_start.tv_nsec) / 1e9;
        if (elapsed > GRACE_PERIOD_SEC) break;

        int n = epoll_wait(w->epoll_fd, events, EPOLL_MAX_EVENTS, 200);
        for (int i = 0; i < n; i++) {
            handle_event(w, (connection_t *)events[i].data.ptr, events[i].events);
        }
    }

    for (int i = 0; i < w->num_conns; i++) {
        close_connection(&w->conns[i], w->epoll_fd);
    }
    free(w->conns);
    close(w->epoll_fd);
    return NULL;
}

/* ---------- Progress reporting ---------- */

static void *progress_thread(void *arg) {
    (void)arg;
    time_t start = time(NULL);

    while (g_running) {
        for (int i = 0; i < 5 && g_running; i++) sleep(1);
        if (!g_running) break;

        unsigned long total = atomic_load(&g_total_requests);
        unsigned long success = atomic_load(&g_success);
        unsigned long errors = atomic_load(&g_errors);
        unsigned long lat_sum = atomic_load(&g_latency_sum_us);
        unsigned long lat_cnt = atomic_load(&g_latency_count);
        double avg_ms = lat_cnt ? (lat_sum / 1000.0) / (double)lat_cnt : 0.0;
        long elapsed = (long)(time(NULL) - start);

        printf("[%lds] Requests: %lu | Success: %lu | Errors: %lu | Avg: %.2fms\n",
               elapsed, total, success, errors, avg_ms);
        fflush(stdout);
    }
    return NULL;
}

/* ---------- Signal handling ---------- */

static void sigint_handler(int signo) {
    (void)signo;
    g_running = 0;
}

/* ---------- CLI / main ---------- */

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s -u <https-url> [options]\n"
            "  -u  Target HTTPS URL (required)\n"
            "  -d  Test duration in seconds (default 60)\n"
            "  -r  Target requests/sec across all threads (default 100)\n"
            "  -t  Number of I/O threads (default 4)\n"
            "  -c  Parallel pooled connections per thread (default 10)\n"
            "  -m  Rate multiplier (default 1)\n"
            "  -w  Max concurrent in-flight requests per pooled connection (default 2)\n",
            prog);
}

int main(int argc, char **argv) {
    char *url = NULL;

    g_cfg.duration = 60;
    g_cfg.rate = 100;
    g_cfg.threads = 4;
    g_cfg.conns_per_thread = 10;
    g_cfg.multiplier = 1.0;
    g_cfg.worker_pool_size = 2;

    int opt;
    while ((opt = getopt(argc, argv, "u:d:r:t:c:m:w:h")) != -1) {
        switch (opt) {
            case 'u': url = optarg; break;
            case 'd': g_cfg.duration = atoi(optarg); break;
            case 'r': g_cfg.rate = atoi(optarg); break;
            case 't': g_cfg.threads = atoi(optarg); break;
            case 'c': g_cfg.conns_per_thread = atoi(optarg); break;
            case 'm': g_cfg.multiplier = atof(optarg); break;
            case 'w': g_cfg.worker_pool_size = atoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (!url) {
        fprintf(stderr, "Error: -u is required\n");
        print_usage(argv[0]);
        return 1;
    }
    if (g_cfg.duration <= 0 || g_cfg.rate <= 0 || g_cfg.threads <= 0 ||
        g_cfg.conns_per_thread <= 0 || g_cfg.worker_pool_size <= 0 || g_cfg.multiplier <= 0) {
        fprintf(stderr, "Error: numeric parameters must be positive\n");
        return 1;
    }
    if (g_cfg.worker_pool_size > MAX_INFLIGHT) {
        fprintf(stderr, "Warning: -w clamped to %d\n", MAX_INFLIGHT);
        g_cfg.worker_pool_size = MAX_INFLIGHT;
    }

    if (parse_url(url, &g_target) != 0) {
        fprintf(stderr, "Error: only https://host[:port][/path] URLs are supported\n");
        return 1;
    }
    if (g_target.port == 443) {
        snprintf(g_cfg.authority, sizeof(g_cfg.authority), "%s", g_target.host);
    } else {
        snprintf(g_cfg.authority, sizeof(g_cfg.authority), "%s:%d", g_target.host, g_target.port);
    }

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal(SIGINT)");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    g_ssl_ctx = create_ssl_ctx();
    if (!g_ssl_ctx) {
        fprintf(stderr, "Failed to initialize SSL context\n");
        return 1;
    }

    printf("Starting benchmark: %s\n", url);
    printf("duration=%ds rate=%d(x%.1f) threads=%d conns/thread=%d in-flight/conn=%d\n",
           g_cfg.duration, g_cfg.rate, g_cfg.multiplier, g_cfg.threads,
           g_cfg.conns_per_thread, g_cfg.worker_pool_size);

    worker_t *workers = calloc((size_t)g_cfg.threads, sizeof(worker_t));
    if (!workers) {
        fprintf(stderr, "calloc failed for workers\n");
        SSL_CTX_free(g_ssl_ctx);
        return 1;
    }

    int started = 0;
    for (int i = 0; i < g_cfg.threads; i++) {
        workers[i].id = i;
        if (pthread_create(&workers[i].thread, NULL, worker_main, &workers[i]) != 0) {
            fprintf(stderr, "pthread_create failed for worker %d: %s\n", i, strerror(errno));
            break;
        }
        started++;
    }
    if (started == 0) {
        fprintf(stderr, "No worker threads could be started, aborting\n");
        free(workers);
        SSL_CTX_free(g_ssl_ctx);
        return 1;
    }

    pthread_t progress_tid;
    int have_progress = (pthread_create(&progress_tid, NULL, progress_thread, NULL) == 0);
    if (!have_progress) {
        fprintf(stderr, "pthread_create failed for progress thread: %s\n", strerror(errno));
    }

    time_t start = time(NULL);
    while (g_running && (time(NULL) - start) < g_cfg.duration) {
        sleep(1);
    }
    g_running = 0;

    if (have_progress) pthread_join(progress_tid, NULL);
    for (int i = 0; i < started; i++) {
        pthread_join(workers[i].thread, NULL);
    }
    free(workers);

    unsigned long total = atomic_load(&g_total_requests);
    unsigned long success = atomic_load(&g_success);
    unsigned long errors = atomic_load(&g_errors);
    unsigned long lat_sum = atomic_load(&g_latency_sum_us);
    unsigned long lat_cnt = atomic_load(&g_latency_count);
    double avg_ms = lat_cnt ? (lat_sum / 1000.0) / (double)lat_cnt : 0.0;
    long elapsed = (long)(time(NULL) - start);

    printf("\n===== FINAL REPORT =====\n");
    printf("Duration      : %lds\n", elapsed);
    printf("Total Requests: %lu\n", total);
    printf("Success       : %lu (%.2f%%)\n", success, total ? 100.0 * (double)success / (double)total : 0.0);
    printf("Errors        : %lu (%.2f%%)\n", errors, total ? 100.0 * (double)errors / (double)total : 0.0);
    printf("Avg Response  : %.2fms\n", avg_ms);
    printf("Throughput    : %.2f req/s\n", elapsed > 0 ? (double)total / (double)elapsed : 0.0);
    printf("=========================\n");

    pthread_mutex_lock(&g_session_mutex);
    if (g_cached_session) {
        SSL_SESSION_free(g_cached_session);
        g_cached_session = NULL;
    }
    pthread_mutex_unlock(&g_session_mutex);

    SSL_CTX_free(g_ssl_ctx);
    return 0;
}
