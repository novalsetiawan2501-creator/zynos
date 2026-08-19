/*
 * http2bench.c - Multi-threaded HTTP/2 capacity benchmark for internal servers.
 *
 * Build:
 *   gcc -o http2bench http2bench.c -lnghttp2 -lpthread -lssl -lcrypto \
 *       -Wall -O3 -march=native -mtune=native -flto -funroll-loops \
 *       -fomit-frame-pointer -pipe -D_GNU_SOURCE -DNDEBUG
 *
 * Requires: libnghttp2-dev, libssl-dev (OpenSSL >= 1.1.1 for ALPN cb).
 *
 * -----------------------------------------------------------------------
 * NOTES / DEVIATIONS FROM THE ORIGINAL SPEC (read before you rely on this):
 *
 *  - sendfile()/splice() are SERVER-side zero-copy primitives for handing a
 *    file or a peer socket's bytes to the network stack without a userspace
 *    copy. A benchmarking CLIENT sends small generated GET/HEAD requests and
 *    only needs to read+discard a response body, so there's nothing to
 *    sendfile/splice here. nghttp2_session_mem_send() already coalesces
 *    every pending frame (HEADERS, DATA, WINDOW_UPDATE, ...) into one
 *    contiguous buffer per flush - that's the same job writev() would be
 *    doing over separate iovecs, so a single send()/SSL_write() per flush
 *    is the non-redundant realization of "zero-copy header writes" here.
 *    Response bodies are never buffered: chunks are counted and discarded
 *    on arrival, not copied into a growable buffer.
 *  - "SETTINGS_INITIAL_WINDOW_SIZE = 0" would actually STALL every stream
 *    (a zero window means the peer is told to send zero DATA bytes) - that's
 *    the opposite of "disable flow control". There is no wire-level way to
 *    disable HTTP/2 flow control; the closest legitimate lever is sending a
 *    very large initial window (2^31-1) plus periodic WINDOW_UPDATEs. That's
 *    what --no-flow-control does here. This is called out again at the flag
 *    definition.
 *  - "Work-stealing queue" is simplified to static per-thread partitioning
 *    of connections (each thread owns a fixed connection set). A full
 *    lock-free work-stealing deque adds real complexity for negligible
 *    benefit in a fixed fan-out burst-fill workload like this one, so it's
 *    left as a static split. Said plainly rather than silently dropped.
 *  - Latency percentiles are computed from a per-thread log2-bucketed
 *    histogram (64 buckets, merged at report time), not a full stored
 *    sample array - this keeps the hot path allocation-free at 100k+ rps.
 *    Reported P50/P95/P99 are bucket-boundary approximations, not exact.
 *  - HPACK "precomputed static table" is what nghttp2 already does
 *    internally (RFC 7541 static table + dynamic table). This tool
 *    precomputes/reuses its *own* nghttp2_nv header arrays so the same
 *    pointers are resubmitted request after request without rebuilding or
 *    allocating strings on every call.
 *  - h2c (plaintext HTTP/2, prior-knowledge) is supported directly for
 *    http:// URLs, which is common for internal capacity testing without
 *    TLS overhead. https:// URLs use TLS + ALPN "h2".
 * -----------------------------------------------------------------------
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <nghttp2/nghttp2.h>

/* ------------------------------------------------------------------ */
/* Config / constants                                                  */
/* ------------------------------------------------------------------ */

#define MAX_THREADS            64
#define DEFAULT_MAX_STREAMS    200
#define RECV_BUF_SIZE          (64 * 1024)   /* 64KB pre-allocated buffers */
#define MAX_EVENTS             1024
#define HIST_BUCKETS           64            /* log2(us) buckets, ~1us..2^64us */
#define STATS_INTERVAL_MS      50
#define UA_POOL_SIZE           6

static const char *UA_POOL[UA_POOL_SIZE] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:120.0) Gecko/20100101 Firefox/120.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/120.0",
    "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:120.0) Gecko/20100101 Firefox/120.0",
};

typedef enum { METHOD_GET, METHOD_HEAD } http_method_t;

typedef struct {
    char        url[512];
    char        scheme[8];
    char        host[256];
    char        path[256];
    int         port;
    bool        is_tls;

    int         threads;
    int         connections;
    int         duration_sec;
    int         warmup_sec;
    long        burst_target;
    int         streams_per_conn_fallback;
    bool        no_flow_control;
    http_method_t method;
    bool        no_tls_verify;
    bool        minimal_headers;
} config_t;

static config_t g_cfg = {
    .port = 0,
    .threads = 0,
    .connections = 200,
    .duration_sec = 30,
    .warmup_sec = 10,
    .burst_target = 50000,
    .streams_per_conn_fallback = DEFAULT_MAX_STREAMS,
    .no_flow_control = false,
    .method = METHOD_GET,
    .no_tls_verify = true,   /* default ENABLED per spec (skip verify) */
    .minimal_headers = false,
};

/* ------------------------------------------------------------------ */
/* Global atomic stats                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    volatile long total_requests;
    volatile long total_success;
    volatile long total_errors;

    volatile long err_timeout;
    volatile long err_reset;
    volatile long err_refused;
    volatile long err_other;

    volatile long status_2xx;
    volatile long status_3xx;
    volatile long status_4xx;
    volatile long status_5xx;
    volatile long status_other;

    volatile long bytes_received;

    volatile int  active_connections;
    volatile int  active_streams;

    volatile long window_0_100ms;    /* requests completed 0-100ms   */
    volatile long window_100_500ms;  /* requests completed 100-500ms */
    volatile long window_500ms_1s;   /* requests completed 500ms-1s  */

    volatile long peak_rps;
} global_stats_t;

static global_stats_t g_stats;

static volatile sig_atomic_t g_stop = 0;

/* ------------------------------------------------------------------ */
/* Latency histogram (log2 microsecond buckets)                        */
/* ------------------------------------------------------------------ */

typedef struct {
    long buckets[HIST_BUCKETS];
    long count;
} lat_hist_t;

static inline void hist_add(lat_hist_t *h, uint64_t latency_us) {
    int b = 0;
    uint64_t v = latency_us ? latency_us : 1;
    while (v > 1 && b < HIST_BUCKETS - 1) { v >>= 1; b++; }
    h->buckets[b]++;
    h->count++;
}

static void hist_merge(lat_hist_t *dst, const lat_hist_t *src) {
    for (int i = 0; i < HIST_BUCKETS; i++) dst->buckets[i] += src->buckets[i];
    dst->count += src->count;
}

/* approximate percentile: returns lower bound (us) of the bucket containing it */
static uint64_t hist_percentile(const lat_hist_t *h, double pct) {
    if (h->count == 0) return 0;
    long target = (long)(h->count * pct);
    long cum = 0;
    for (int i = 0; i < HIST_BUCKETS; i++) {
        cum += h->buckets[i];
        if (cum >= target) return (uint64_t)1 << i;
    }
    return (uint64_t)1 << (HIST_BUCKETS - 1);
}

/* ------------------------------------------------------------------ */
/* Time helpers                                                        */
/* ------------------------------------------------------------------ */

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline double now_sec(void) { return (double)now_ns() / 1e9; }

/* ------------------------------------------------------------------ */
/* Per-request / per-stream context (pre-allocated pool, no malloc)    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t start_ns;
    int32_t  stream_id;
    int      status_code;
    bool     in_use;
} request_ctx_t;

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    CONN_CONNECTING,
    CONN_TLS_HANDSHAKE,
    CONN_H2_SETUP,
    CONN_READY,
    CONN_CLOSING,
    CONN_DONE
} conn_state_t;

struct thread_ctx;

typedef struct connection {
    int                 fd;
    SSL                *ssl;          /* NULL for h2c plaintext */
    nghttp2_session    *session;
    conn_state_t        state;
    struct thread_ctx  *tctx;

    uint8_t            *recv_buf;     /* pre-allocated 64KB */
    int32_t             max_concurrent_streams;
    bool                settings_seen;
    int                 active_streams;

    request_ctx_t      *req_pool;     /* pre-allocated request slots for this conn */
    int                 req_pool_size;
    int                 req_pool_next;

    bool                want_epollout;

    /* Pending-send state: nghttp2_session_mem_send() hands back a pointer
     * into ITS OWN internal buffer and considers that data "delivered" as
     * soon as it returns it - it will NOT re-hand you the unsent tail if
     * you call it again. On a partial write (EAGAIN mid-buffer) we must
     * remember our offset into *this exact* buffer and resume from there
     * on the next writable event, only asking nghttp2 for a new chunk once
     * this one is fully flushed. Losing this bookkeeping silently corrupts
     * the HTTP/2 byte stream under load - exactly the kind of thing that
     * only shows up once you're pushing enough concurrent streams to fill
     * the socket send buffer, i.e. exactly what this tool is for. */
    const uint8_t      *send_buf;
    size_t              send_len;
    size_t              send_off;
} connection_t;

/* ------------------------------------------------------------------ */
/* Per-thread context                                                  */
/* ------------------------------------------------------------------ */

typedef struct thread_ctx {
    int             id;
    int             cpu_core;
    int             epfd;
    connection_t   *conns;
    int             n_conns;
    long            burst_quota;      /* requests this thread should fire at T0 */
    long            local_requests;
    long            local_success;
    long            local_errors;
    lat_hist_t      hist;
    pthread_t       thread;
    SSL_CTX        *ssl_ctx;          /* shared, read-only after init */
    uint64_t        end_time_ns;      /* warmup+duration deadline */
    uint64_t        bench_start_ns;   /* T0 of burst phase */
} thread_ctx_t;

static pthread_barrier_t g_burst_barrier;

/* ------------------------------------------------------------------ */
/* Pre-serialized header sets (built once, reused - no per-request malloc) */
/* ------------------------------------------------------------------ */

typedef struct {
    nghttp2_nv nva[8];
    size_t     nvlen;
} header_set_t;

static header_set_t g_header_sets[UA_POOL_SIZE];
static char g_scheme_hdr[8];
static char g_authority_hdr[256];
static char g_path_hdr[256];
static char g_method_hdr[8];

#define NV(NAME, VALUE, FLAGS) \
    { (uint8_t *)(NAME), (uint8_t *)(VALUE), strlen(NAME), strlen(VALUE), (FLAGS) }

static void build_header_sets(void) {
    strncpy(g_scheme_hdr, g_cfg.scheme, sizeof(g_scheme_hdr) - 1);
    strncpy(g_authority_hdr, g_cfg.host, sizeof(g_authority_hdr) - 1);
    if (g_cfg.port != (g_cfg.is_tls ? 443 : 80)) {
        char portbuf[16];
        snprintf(portbuf, sizeof(portbuf), ":%d", g_cfg.port);
        strncat(g_authority_hdr, portbuf, sizeof(g_authority_hdr) - strlen(g_authority_hdr) - 1);
    }
    strncpy(g_path_hdr, g_cfg.path[0] ? g_cfg.path : "/", sizeof(g_path_hdr) - 1);
    strncpy(g_method_hdr, g_cfg.method == METHOD_HEAD ? "HEAD" : "GET", sizeof(g_method_hdr) - 1);

    for (int i = 0; i < UA_POOL_SIZE; i++) {
        header_set_t *hs = &g_header_sets[i];
        size_t n = 0;
        hs->nva[n++] = (nghttp2_nv) NV(":method", g_method_hdr, NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
        hs->nva[n++] = (nghttp2_nv) NV(":scheme", g_scheme_hdr, NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
        hs->nva[n++] = (nghttp2_nv) NV(":authority", g_authority_hdr, NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
        hs->nva[n++] = (nghttp2_nv) NV(":path", g_path_hdr, NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
        if (!g_cfg.minimal_headers) {
            hs->nva[n++] = (nghttp2_nv) NV("user-agent", UA_POOL[i], NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
            hs->nva[n++] = (nghttp2_nv) NV("accept", "*/*", NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
            hs->nva[n++] = (nghttp2_nv) NV("accept-encoding", "identity", NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
        }
        hs->nvlen = n;
    }
}

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void apply_tcp_tuning(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    int bufsz = 8 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));
#ifdef TCP_QUICKACK
    setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif
#ifdef SO_BUSY_POLL
    int busy = 50;
    setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy, sizeof(busy));
#endif
}

static int connect_nonblocking(const char *host, int port) {
    struct addrinfo hints, *res, *rp;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        set_nonblocking(fd);
        apply_tcp_tuning(fd);
        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0 || errno == EINPROGRESS) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* ------------------------------------------------------------------ */
/* nghttp2 callbacks                                                    */
/* ------------------------------------------------------------------ */

static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
    (void)session; (void)flags; (void)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS) return 0;
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        request_ctx_t *req = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (req) {
            char buf[8] = {0};
            size_t n = valuelen < sizeof(buf) - 1 ? valuelen : sizeof(buf) - 1;
            memcpy(buf, value, n);
            req->status_code = atoi(buf);
        }
    }
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                        int32_t stream_id, const uint8_t *data,
                                        size_t len, void *user_data) {
    (void)session; (void)flags; (void)stream_id; (void)data;
    thread_ctx_t *t = user_data;
    (void)t;
    __sync_fetch_and_add(&g_stats.bytes_received, (long)len);
    /* body intentionally discarded - never copied */
    return 0;
}

static void classify_status(int status) {
    if (status >= 200 && status < 300) __sync_fetch_and_add(&g_stats.status_2xx, 1);
    else if (status >= 300 && status < 400) __sync_fetch_and_add(&g_stats.status_3xx, 1);
    else if (status >= 400 && status < 500) __sync_fetch_and_add(&g_stats.status_4xx, 1);
    else if (status >= 500 && status < 600) __sync_fetch_and_add(&g_stats.status_5xx, 1);
    else __sync_fetch_and_add(&g_stats.status_other, 1);
}

static void submit_next_request(connection_t *c);

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                     uint32_t error_code, void *user_data) {
    thread_ctx_t *t = user_data;
    request_ctx_t *req = nghttp2_session_get_stream_user_data(session, stream_id);

    connection_t *c = NULL;
    for (int i = 0; i < t->n_conns; i++) {
        if (t->conns[i].session == session) { c = &t->conns[i]; break; }
    }
    if (c) c->active_streams--;
    __sync_fetch_and_add(&g_stats.active_streams, -1);

    if (req) {
        uint64_t lat_ns = now_ns() - req->start_ns;
        uint64_t lat_us = lat_ns / 1000;
        hist_add(&t->hist, lat_us);

        t->local_requests++;
        __sync_fetch_and_add(&g_stats.total_requests, 1);

        if (error_code == NGHTTP2_NO_ERROR && req->status_code >= 200 && req->status_code < 400) {
            t->local_success++;
            __sync_fetch_and_add(&g_stats.total_success, 1);
        } else {
            t->local_errors++;
            __sync_fetch_and_add(&g_stats.total_errors, 1);
            if (error_code == NGHTTP2_REFUSED_STREAM) __sync_fetch_and_add(&g_stats.err_refused, 1);
            else if (error_code == NGHTTP2_CANCEL || error_code == NGHTTP2_STREAM_CLOSED)
                __sync_fetch_and_add(&g_stats.err_reset, 1);
            else __sync_fetch_and_add(&g_stats.err_other, 1);
        }
        if (req->status_code > 0) classify_status(req->status_code);

        if (t->bench_start_ns) {
            uint64_t since_t0_ms = (now_ns() - t->bench_start_ns) / 1000000ULL;
            if (since_t0_ms <= 100) __sync_fetch_and_add(&g_stats.window_0_100ms, 1);
            else if (since_t0_ms <= 500) __sync_fetch_and_add(&g_stats.window_100_500ms, 1);
            else if (since_t0_ms <= 1000) __sync_fetch_and_add(&g_stats.window_500ms_1s, 1);
        }

        req->in_use = false;
    }

    /* pipeline refill: immediately submit a new request on this stream slot */
    if (c && !g_stop && now_ns() < t->end_time_ns) {
        submit_next_request(c);
    }
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    thread_ctx_t *t = user_data;
    if (frame->hd.type == NGHTTP2_SETTINGS && !(frame->hd.flags & NGHTTP2_FLAG_ACK)) {
        for (int i = 0; i < t->n_conns; i++) {
            if (t->conns[i].session == session) {
                t->conns[i].settings_seen = true;
                int32_t neg = nghttp2_session_get_remote_settings(session, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
                t->conns[i].max_concurrent_streams =
                    (neg > 0 && neg < 1000000) ? neg : g_cfg.streams_per_conn_fallback;
                break;
            }
        }
    }
    return 0;
}

static int on_invalid_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                                           int lib_error_code, void *user_data) {
    (void)session; (void)frame; (void)lib_error_code; (void)user_data;
    __sync_fetch_and_add(&g_stats.err_other, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Request submission (pre-allocated pool, no malloc in hot path)      */
/* ------------------------------------------------------------------ */

static int g_ua_rr = 0; /* round-robin index into header sets, racy-but-fine */

static void submit_next_request(connection_t *c) {
    if (c->state != CONN_READY) return;
    if (c->active_streams >= c->max_concurrent_streams) return;

    /* pull a free slot from the pool (ring, no malloc) */
    request_ctx_t *req = NULL;
    for (int tries = 0; tries < c->req_pool_size; tries++) {
        int idx = c->req_pool_next;
        c->req_pool_next = (c->req_pool_next + 1) % c->req_pool_size;
        if (!c->req_pool[idx].in_use) { req = &c->req_pool[idx]; break; }
    }
    if (!req) return; /* pool exhausted, will retry on next close */

    int hs_idx = __sync_fetch_and_add(&g_ua_rr, 1) % UA_POOL_SIZE;
    header_set_t *hs = &g_header_sets[hs_idx];

    req->in_use = true;
    req->status_code = 0;
    req->start_ns = now_ns();

    int32_t sid = nghttp2_submit_request(c->session, NULL, hs->nva, hs->nvlen, NULL, req);
    if (sid < 0) {
        req->in_use = false;
        __sync_fetch_and_add(&g_stats.err_other, 1);
        return;
    }
    req->stream_id = sid;
    c->active_streams++;
    __sync_fetch_and_add(&g_stats.active_streams, 1);
}

/* ------------------------------------------------------------------ */
/* I/O pump: flush pending nghttp2 writes, read available data          */
/* ------------------------------------------------------------------ */

static int conn_raw_send(connection_t *c, const uint8_t *data, size_t len) {
    if (c->ssl) return SSL_write(c->ssl, data, (int)len);
    return (int)send(c->fd, data, len, MSG_NOSIGNAL);
}

static int conn_raw_recv(connection_t *c, uint8_t *buf, size_t len) {
    if (c->ssl) return SSL_read(c->ssl, buf, (int)len);
    return (int)recv(c->fd, buf, len, 0);
}

static void conn_close(connection_t *c) {
    if (c->state == CONN_DONE) return;
    if (c->session) nghttp2_session_del(c->session);
    c->session = NULL;
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    c->state = CONN_DONE;
    __sync_fetch_and_add(&g_stats.active_connections, -1);
}

static bool conn_flush_send(connection_t *c) {
    for (;;) {
        /* refill only once the previous chunk is fully on the wire */
        if (c->send_off >= c->send_len) {
            ssize_t len = nghttp2_session_mem_send(c->session, &c->send_buf);
            if (len < 0) { conn_close(c); return false; }
            if (len == 0) { c->want_epollout = false; return true; } /* nothing pending */
            c->send_len = (size_t)len;
            c->send_off = 0;
        }
        while (c->send_off < c->send_len) {
            int n = conn_raw_send(c, c->send_buf + c->send_off, c->send_len - c->send_off);
            if (n > 0) { c->send_off += (size_t)n; continue; }
            bool would_block;
            if (c->ssl) {
                int e = SSL_get_error(c->ssl, n);
                would_block = (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ);
            } else {
                would_block = (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
            }
            if (would_block) { c->want_epollout = true; return true; } /* resume here next time */
            conn_close(c);
            return false;
        }
    }
}

static bool conn_pump_recv(connection_t *c) {
    for (;;) {
        int n = conn_raw_recv(c, c->recv_buf, RECV_BUF_SIZE);
        if (n > 0) {
            ssize_t rv = nghttp2_session_mem_recv(c->session, c->recv_buf, n);
            if (rv < 0) { conn_close(c); return false; }
            continue;
        }
        if (n == 0) { conn_close(c); return false; }
        if (c->ssl) {
            int e = SSL_get_error(c->ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) break;
            conn_close(c); return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            conn_close(c); return false;
        }
    }
    if (!conn_flush_send(c)) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Connection state machine step (called on epoll readiness)            */
/* ------------------------------------------------------------------ */

static bool init_h2_session(connection_t *c);

static void conn_step(connection_t *c) {
    if (c->state == CONN_CONNECTING) {
        int err = 0; socklen_t len = sizeof(err);
        getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) { conn_close(c); return; }
        if (c->ssl) {
            c->state = CONN_TLS_HANDSHAKE;
        } else {
            c->state = CONN_H2_SETUP;
            if (!init_h2_session(c)) return;
        }
    }

    if (c->state == CONN_TLS_HANDSHAKE) {
        int rc = SSL_connect(c->ssl);
        if (rc == 1) {
            c->state = CONN_H2_SETUP;
            if (!init_h2_session(c)) return;
        } else {
            int e = SSL_get_error(c->ssl, rc);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return; /* keep waiting */
            conn_close(c);
            return;
        }
    }

    if (c->state == CONN_H2_SETUP || c->state == CONN_READY) {
        if (!conn_pump_recv(c)) return;
        if (c->state == CONN_H2_SETUP && c->settings_seen) {
            c->state = CONN_READY;
        }
    }
}

static bool init_h2_session(connection_t *c) {
    nghttp2_session_callbacks *cb;
    nghttp2_session_callbacks_new(&cb);
    nghttp2_session_callbacks_set_on_header_callback(cb, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cb, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(cb, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(cb, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(cb, on_invalid_frame_recv_callback);

    int rc = nghttp2_session_client_new(&c->session, cb, c->tctx);
    nghttp2_session_callbacks_del(cb);
    if (rc != 0) { conn_close(c); return false; }

    nghttp2_settings_entry iv[3];
    int niv = 0;
    iv[niv].settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
    iv[niv].value = g_cfg.streams_per_conn_fallback; niv++;
    iv[niv].settings_id = NGHTTP2_SETTINGS_ENABLE_PUSH;
    iv[niv].value = 0; niv++;
    if (g_cfg.no_flow_control) {
        /* see file header note: true "disable" isn't possible on the wire;
           we instead advertise the max legal window so flow control never
           becomes the bottleneck. */
        iv[niv].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
        iv[niv].value = 0x7fffffff; niv++;
    }
    nghttp2_submit_settings(c->session, NGHTTP2_FLAG_NONE, iv, niv);

    if (g_cfg.no_flow_control) {
        nghttp2_session_set_local_window_size(c->session, NGHTTP2_FLAG_NONE, 0, 0x7fffffff);
    }

    c->max_concurrent_streams = g_cfg.streams_per_conn_fallback;
    return conn_flush_send(c);
}

/* ------------------------------------------------------------------ */
/* SSL_CTX setup                                                       */
/* ------------------------------------------------------------------ */

static SSL_CTX *make_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    if (g_cfg.no_tls_verify) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(ctx);
    }
    static const unsigned char alpn_h2[] = { 2, 'h', '2' };
    SSL_CTX_set_alpn_protos(ctx, alpn_h2, sizeof(alpn_h2)); /* client only needs to advertise, not select */
    return ctx;
}

/* ------------------------------------------------------------------ */
/* Connection open (async connect, TLS attach)                          */
/* ------------------------------------------------------------------ */

static bool conn_open(connection_t *c, thread_ctx_t *t) {
    memset(c, 0, sizeof(*c));
    c->tctx = t;
    c->fd = connect_nonblocking(g_cfg.host, g_cfg.port);
    if (c->fd < 0) return false;
    c->state = CONN_CONNECTING;
    c->recv_buf = malloc(RECV_BUF_SIZE); /* one-time setup allocation, not in hot path */
    /* size for whichever is larger - the configured fallback, or generous
     * headroom in case the server negotiates a much higher
     * MAX_CONCURRENT_STREAMS than our fallback; pool memory is cheap
     * (~24 bytes/slot) so it's worth over-provisioning here rather than
     * silently under-using the concurrency the server actually offers. */
    c->req_pool_size = (g_cfg.streams_per_conn_fallback > 1024 ? g_cfg.streams_per_conn_fallback : 1024) + 64;
    c->req_pool = calloc(c->req_pool_size, sizeof(request_ctx_t));

    if (g_cfg.is_tls) {
        c->ssl = SSL_new(t->ssl_ctx);
        SSL_set_tlsext_host_name(c->ssl, g_cfg.host);
        SSL_set_fd(c->ssl, c->fd);
        SSL_set_connect_state(c->ssl);
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = c;
    epoll_ctl(t->epfd, EPOLL_CTL_ADD, c->fd, &ev);
    __sync_fetch_and_add(&g_stats.active_connections, 1);
    return true;
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                       */
/* ------------------------------------------------------------------ */

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void *worker_main(void *arg) {
    thread_ctx_t *t = arg;
    pin_to_core(t->cpu_core);

    t->epfd = epoll_create1(0);
    t->conns = calloc(t->n_conns, sizeof(connection_t));

    for (int i = 0; i < t->n_conns; i++) {
        if (!conn_open(&t->conns[i], t)) {
            fprintf(stderr, "[thread %d] connection %d failed to open\n", t->id, i);
        }
    }

    /* ---- warmup: pump epoll until every connection is READY or warmup elapses ---- */
    uint64_t warmup_deadline = now_ns() + (uint64_t)g_cfg.warmup_sec * 1000000000ULL;
    for (;;) {
        bool all_ready = true;
        for (int i = 0; i < t->n_conns; i++) {
            if (t->conns[i].state != CONN_READY && t->conns[i].state != CONN_DONE) { all_ready = false; break; }
        }
        if (all_ready) break;
        if (now_ns() > warmup_deadline) break;

        struct epoll_event events[MAX_EVENTS];
        int n = epoll_wait(t->epfd, events, MAX_EVENTS, 5);
        for (int i = 0; i < n; i++) {
            connection_t *c = events[i].data.ptr;
            if (c->state != CONN_DONE) conn_step(c);
        }
    }

    /* ---- synchronized burst start ---- */
    pthread_barrier_wait(&g_burst_barrier);
    t->bench_start_ns = now_ns();
    t->end_time_ns = t->bench_start_ns + (uint64_t)g_cfg.duration_sec * 1000000000ULL;

    /* initial burst: fan out this thread's burst quota across its ready connections */
    long fired = 0;
    while (fired < t->burst_quota) {
        bool any = false;
        for (int i = 0; i < t->n_conns && fired < t->burst_quota; i++) {
            connection_t *c = &t->conns[i];
            if (c->state == CONN_READY && c->active_streams < c->max_concurrent_streams) {
                submit_next_request(c);
                fired++;
                any = true;
            }
        }
        if (!any) break;
    }
    for (int i = 0; i < t->n_conns; i++) {
        if (t->conns[i].state == CONN_READY) conn_flush_send(&t->conns[i]);
    }

    /* ---- steady-state pipeline-fill loop (busy-poll, edge-triggered) ---- */
    while (!g_stop && now_ns() < t->end_time_ns) {
        struct epoll_event events[MAX_EVENTS];
        int n = epoll_wait(t->epfd, events, MAX_EVENTS, 0); /* timeout 0: busy poll */
        for (int i = 0; i < n; i++) {
            connection_t *c = events[i].data.ptr;
            if (c->state == CONN_DONE) continue;
            conn_step(c);
            if (c->state == CONN_READY) {
                /* top off any stream slack (covers slots freed since last close) */
                while (c->active_streams < c->max_concurrent_streams && now_ns() < t->end_time_ns) {
                    int before = c->active_streams;
                    submit_next_request(c);
                    if (c->active_streams == before) break; /* pool exhausted */
                }
                conn_flush_send(c);
            }
        }
    }

    /* ---- graceful shutdown ---- */
    for (int i = 0; i < t->n_conns; i++) {
        connection_t *c = &t->conns[i];
        if (c->state == CONN_READY) {
            nghttp2_submit_goaway(c->session, NGHTTP2_FLAG_NONE, 0, NGHTTP2_NO_ERROR, NULL, 0);
            conn_flush_send(c);
        }
        conn_close(c);
        free(c->recv_buf);
        free(c->req_pool);
    }
    close(t->epfd);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Stats printer thread                                                */
/* ------------------------------------------------------------------ */

static long read_rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) { sscanf(line + 6, "%ld", &rss); break; }
    }
    fclose(f);
    return rss;
}

static void *stats_printer(void *arg) {
    (void)arg;
    long last_total = 0;
    uint64_t last_ns = now_ns();

    while (!g_stop) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = STATS_INTERVAL_MS * 1000000L };
        nanosleep(&ts, NULL);

        long total = g_stats.total_requests;
        uint64_t t_ns = now_ns();
        double dt = (double)(t_ns - last_ns) / 1e9;
        long rps = dt > 0 ? (long)((total - last_total) / dt) : 0;
        if (rps > g_stats.peak_rps) g_stats.peak_rps = rps;

        long done = g_stats.total_success + g_stats.total_errors;
        double success_pct = done > 0 ? 100.0 * g_stats.total_success / done : 0.0;

        printf("\r[%6ldrps] total=%-8ld ok=%-8ld err=%-6ld success=%5.1f%% "
               "conns=%-4d streams=%-5d rss=%ldMB   ",
               rps, total, g_stats.total_success, g_stats.total_errors,
               success_pct, g_stats.active_connections, g_stats.active_streams,
               read_rss_kb() / 1024);
        fflush(stdout);

        last_total = total;
        last_ns = t_ns;
    }
    printf("\n");
    return NULL;
}

/* ------------------------------------------------------------------ */
/* URL parsing                                                          */
/* ------------------------------------------------------------------ */

static bool parse_url(const char *url) {
    strncpy(g_cfg.url, url, sizeof(g_cfg.url) - 1);
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) { strcpy(g_cfg.scheme, "https"); g_cfg.is_tls = true; p += 8; }
    else if (strncmp(p, "http://", 7) == 0) { strcpy(g_cfg.scheme, "http"); g_cfg.is_tls = false; p += 7; }
    else return false;

    const char *slash = strchr(p, '/');
    const char *hostend = slash ? slash : p + strlen(p);

    const char *colon = memchr(p, ':', hostend - p);
    if (colon) {
        size_t hlen = colon - p;
        strncpy(g_cfg.host, p, hlen); g_cfg.host[hlen] = 0;
        g_cfg.port = atoi(colon + 1);
    } else {
        size_t hlen = hostend - p;
        strncpy(g_cfg.host, p, hlen); g_cfg.host[hlen] = 0;
        g_cfg.port = g_cfg.is_tls ? 443 : 80;
    }
    strncpy(g_cfg.path, slash ? slash : "/", sizeof(g_cfg.path) - 1);
    return true;
}

/* ------------------------------------------------------------------ */
/* Signal handling                                                      */
/* ------------------------------------------------------------------ */

static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* ------------------------------------------------------------------ */
/* Report export                                                       */
/* ------------------------------------------------------------------ */

static void write_reports(double elapsed_sec, const lat_hist_t *merged) {
    uint64_t p50 = hist_percentile(merged, 0.50);
    uint64_t p95 = hist_percentile(merged, 0.95);
    uint64_t p99 = hist_percentile(merged, 0.99);

    FILE *csv = fopen("http2bench_report.csv", "w");
    if (csv) {
        fprintf(csv, "metric,value\n");
        fprintf(csv, "duration_sec,%.2f\n", elapsed_sec);
        fprintf(csv, "total_requests,%ld\n", g_stats.total_requests);
        fprintf(csv, "total_success,%ld\n", g_stats.total_success);
        fprintf(csv, "total_errors,%ld\n", g_stats.total_errors);
        fprintf(csv, "avg_rps,%.1f\n", elapsed_sec > 0 ? g_stats.total_requests / elapsed_sec : 0.0);
        fprintf(csv, "peak_rps,%ld\n", g_stats.peak_rps);
        fprintf(csv, "p50_us,%llu\n", (unsigned long long)p50);
        fprintf(csv, "p95_us,%llu\n", (unsigned long long)p95);
        fprintf(csv, "p99_us,%llu\n", (unsigned long long)p99);
        fprintf(csv, "status_2xx,%ld\n", g_stats.status_2xx);
        fprintf(csv, "status_3xx,%ld\n", g_stats.status_3xx);
        fprintf(csv, "status_4xx,%ld\n", g_stats.status_4xx);
        fprintf(csv, "status_5xx,%ld\n", g_stats.status_5xx);
        fprintf(csv, "err_timeout,%ld\n", g_stats.err_timeout);
        fprintf(csv, "err_reset,%ld\n", g_stats.err_reset);
        fprintf(csv, "err_refused,%ld\n", g_stats.err_refused);
        fprintf(csv, "err_other,%ld\n", g_stats.err_other);
        fprintf(csv, "window_0_100ms,%ld\n", g_stats.window_0_100ms);
        fprintf(csv, "window_100_500ms,%ld\n", g_stats.window_100_500ms);
        fprintf(csv, "window_500ms_1s,%ld\n", g_stats.window_500ms_1s);
        fprintf(csv, "bytes_received,%ld\n", g_stats.bytes_received);
        fclose(csv);
    }

    FILE *js = fopen("http2bench_report.json", "w");
    if (js) {
        fprintf(js,
            "{\n"
            "  \"url\": \"%s\",\n"
            "  \"duration_sec\": %.2f,\n"
            "  \"threads\": %d,\n"
            "  \"connections\": %d,\n"
            "  \"total_requests\": %ld,\n"
            "  \"total_success\": %ld,\n"
            "  \"total_errors\": %ld,\n"
            "  \"avg_rps\": %.1f,\n"
            "  \"peak_rps\": %ld,\n"
            "  \"latency_us\": { \"p50\": %llu, \"p95\": %llu, \"p99\": %llu },\n"
            "  \"status\": { \"2xx\": %ld, \"3xx\": %ld, \"4xx\": %ld, \"5xx\": %ld, \"other\": %ld },\n"
            "  \"errors\": { \"timeout\": %ld, \"reset\": %ld, \"refused\": %ld, \"other\": %ld },\n"
            "  \"first_second_windows\": { \"0_100ms\": %ld, \"100_500ms\": %ld, \"500ms_1s\": %ld },\n"
            "  \"bytes_received\": %ld\n"
            "}\n",
            g_cfg.url, elapsed_sec, g_cfg.threads, g_cfg.connections,
            g_stats.total_requests, g_stats.total_success, g_stats.total_errors,
            elapsed_sec > 0 ? g_stats.total_requests / elapsed_sec : 0.0, g_stats.peak_rps,
            (unsigned long long)p50, (unsigned long long)p95, (unsigned long long)p99,
            g_stats.status_2xx, g_stats.status_3xx, g_stats.status_4xx, g_stats.status_5xx, g_stats.status_other,
            g_stats.err_timeout, g_stats.err_reset, g_stats.err_refused, g_stats.err_other,
            g_stats.window_0_100ms, g_stats.window_100_500ms, g_stats.window_500ms_1s,
            g_stats.bytes_received);
        fclose(js);
    }

    printf("\n=== http2bench report ===\n");
    printf("target:        %s\n", g_cfg.url);
    printf("duration:      %.2fs (threads=%d, connections=%d)\n", elapsed_sec, g_cfg.threads, g_cfg.connections);
    printf("total requests: %ld  (success=%ld, errors=%ld)\n",
           g_stats.total_requests, g_stats.total_success, g_stats.total_errors);
    printf("avg rps:       %.1f\n", elapsed_sec > 0 ? g_stats.total_requests / elapsed_sec : 0.0);
    printf("peak rps:      %ld\n", g_stats.peak_rps);
    printf("latency:       p50=%lluus  p95=%lluus  p99=%lluus\n",
           (unsigned long long)p50, (unsigned long long)p95, (unsigned long long)p99);
    printf("status codes:  2xx=%ld 3xx=%ld 4xx=%ld 5xx=%ld other=%ld\n",
           g_stats.status_2xx, g_stats.status_3xx, g_stats.status_4xx, g_stats.status_5xx, g_stats.status_other);
    printf("errors:        timeout=%ld reset=%ld refused=%ld other=%ld\n",
           g_stats.err_timeout, g_stats.err_reset, g_stats.err_refused, g_stats.err_other);
    printf("first second:  0-100ms=%ld  100-500ms=%ld  500ms-1s=%ld\n",
           g_stats.window_0_100ms, g_stats.window_100_500ms, g_stats.window_500ms_1s);
    printf("reports written: http2bench_report.csv, http2bench_report.json\n");
}

/* ------------------------------------------------------------------ */
/* CLI                                                                  */
/* ------------------------------------------------------------------ */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --url <target> [options]\n"
        "  --url <target>              required, http:// or https://\n"
        "  --threads <num>             default: auto-detect CPU cores\n"
        "  --connections <num>         default: 200\n"
        "  --duration <seconds>        default: 30\n"
        "  --warmup <seconds>          default: 10\n"
        "  --burst <num>               default: 50000\n"
        "  --streams-per-conn <num>    default: auto-negotiate, fallback 200\n"
        "  --no-flow-control           advertise max HTTP/2 window (see notes in source)\n"
        "  --method <GET|HEAD>         default: GET\n"
        "  --no-tls-verify             skip cert verification (default: ENABLED)\n"
        "  --minimal-headers           strip UA/accept/accept-encoding (default: DISABLED)\n",
        prog);
}

static void parse_args(int argc, char **argv) {
    bool have_url = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--url") && i + 1 < argc) { parse_url(argv[++i]); have_url = true; }
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) g_cfg.threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--connections") && i + 1 < argc) g_cfg.connections = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--duration") && i + 1 < argc) g_cfg.duration_sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) g_cfg.warmup_sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--burst") && i + 1 < argc) g_cfg.burst_target = atol(argv[++i]);
        else if (!strcmp(argv[i], "--streams-per-conn") && i + 1 < argc) g_cfg.streams_per_conn_fallback = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-flow-control")) g_cfg.no_flow_control = true;
        else if (!strcmp(argv[i], "--method") && i + 1 < argc) {
            g_cfg.method = !strcasecmp(argv[++i], "HEAD") ? METHOD_HEAD : METHOD_GET;
        }
        else if (!strcmp(argv[i], "--no-tls-verify")) g_cfg.no_tls_verify = true;
        else if (!strcmp(argv[i], "--minimal-headers")) g_cfg.minimal_headers = true;
        else if (!strcmp(argv[i], "--help")) { usage(argv[0]); exit(0); }
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); usage(argv[0]); exit(1); }
    }
    if (!have_url) { fprintf(stderr, "--url is required\n"); usage(argv[0]); exit(1); }
    if (g_cfg.threads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        g_cfg.threads = n > 0 ? (int)n : 4;
        if (g_cfg.threads > MAX_THREADS) g_cfg.threads = MAX_THREADS;
    }
    if (g_cfg.connections < g_cfg.threads) g_cfg.connections = g_cfg.threads;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    parse_args(argc, argv);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    SSL_library_init();
    SSL_load_error_strings();

    build_header_sets();

    printf("http2bench: target=%s threads=%d connections=%d warmup=%ds duration=%ds burst=%ld\n",
           g_cfg.url, g_cfg.threads, g_cfg.connections, g_cfg.warmup_sec, g_cfg.duration_sec, g_cfg.burst_target);
    printf("(no-flow-control=%d no-tls-verify=%d minimal-headers=%d method=%s)\n",
           g_cfg.no_flow_control, g_cfg.no_tls_verify, g_cfg.minimal_headers,
           g_cfg.method == METHOD_HEAD ? "HEAD" : "GET");

    thread_ctx_t threads[MAX_THREADS];
    memset(threads, 0, sizeof(threads));

    SSL_CTX *shared_ctx = g_cfg.is_tls ? make_ssl_ctx() : NULL;
    if (g_cfg.is_tls && !shared_ctx) { fprintf(stderr, "failed to init TLS context\n"); return 1; }

    int base_conns = g_cfg.connections / g_cfg.threads;
    int rem_conns  = g_cfg.connections % g_cfg.threads;
    long base_burst = g_cfg.burst_target / g_cfg.threads;
    long rem_burst  = g_cfg.burst_target % g_cfg.threads;

    long ncores = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncores <= 0) ncores = g_cfg.threads;

    pthread_barrier_init(&g_burst_barrier, NULL, g_cfg.threads);

    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_printer, NULL);

    for (int i = 0; i < g_cfg.threads; i++) {
        threads[i].id = i;
        threads[i].cpu_core = (int)(i % ncores);
        threads[i].n_conns = base_conns + (i < rem_conns ? 1 : 0);
        threads[i].burst_quota = base_burst + (i < rem_burst ? 1 : 0);
        threads[i].ssl_ctx = shared_ctx;
        pthread_create(&threads[i].thread, NULL, worker_main, &threads[i]);
    }

    double t_start = now_sec();
    for (int i = 0; i < g_cfg.threads; i++) pthread_join(threads[i].thread, NULL);
    double elapsed = now_sec() - t_start;

    g_stop = 1;
    pthread_join(stats_tid, NULL);

    lat_hist_t merged = {0};
    for (int i = 0; i < g_cfg.threads; i++) hist_merge(&merged, &threads[i].hist);

    write_reports(elapsed, &merged);

    if (shared_ctx) SSL_CTX_free(shared_ctx);
    pthread_barrier_destroy(&g_burst_barrier);
    return 0;
}
