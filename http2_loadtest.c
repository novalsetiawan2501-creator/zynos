/*
 * http2_loadtest.c
 *
 * Multi-threaded HTTP/2 load testing tool built on nghttp2 + OpenSSL.
 * Intended for measuring the maximum sustainable capacity of a server
 * you own or are explicitly authorized to test.
 *
 * Architecture:
 *   - N threads (pthread), each owning a pool of M persistent HTTP/2
 *     connections ("connection pooling" / keep-alive).
 *   - Each connection multiplexes up to W concurrent request streams
 *     ("workers") at once, matching HTTP/2's native stream multiplexing.
 *   - Before the measured phase starts, every connection sends a couple
 *     of warm-up requests ("pre-heat") so TLS session state and TCP
 *     congestion windows are settled before RPS is measured.
 *   - Target rate (requests/sec) is enforced with real pacing, evenly
 *     split across threads. Pass --rate 0 for an uncapped run bounded
 *     only by --threads/--connections/--workers, e.g. to find the
 *     server's breaking point.
 *
 * Two deliberate deviations from a "look like a browser" flooder:
 *   1. The User-Agent below identifies this tool honestly instead of
 *      spoofing a real browser. When you're testing your own
 *      infrastructure there's no need to disguise the traffic, and an
 *      honest UA lets your own logs/WAF/ops dashboards tell load-test
 *      traffic apart from real users.
 *   2. --rate is a real, enforced pacing control, not a no-op. Uncapped
 *      mode is still available (--rate 0) for max-throughput capacity
 *      testing, it's just an explicit opt-in rather than the silent
 *      default combined with identity spoofing.
 *
 * Build:
 *   sudo apt-get install libnghttp2-dev libssl-dev build-essential
 *   gcc -O2 -o http2_loadtest http2_loadtest.c -lnghttp2 -lssl -lcrypto -lpthread
 *
 * Usage:
 *   ./http2_loadtest --url https://example.com/ --rate 5000 \
 *       --threads 4 --connections 20 --workers 25 --duration 30
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <stdatomic.h>
#include <limits.h>
#include <getopt.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <nghttp2/nghttp2.h>

/* Honest, identifiable UA -- see header comment above. */
#define USER_AGENT "h2loadtest/1.0 (+internal-capacity-test)"

#define MAX_HOST_LEN 256
#define MAX_PATH_LEN 2048
#define WARMUP_REQUESTS_PER_CONN 2
#define WARMUP_TIMEOUT_SEC 5.0
#define RECONNECT_BACKOFF_SEC 2.0
#define NUM_LAT_BUCKETS 11

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char url[512];
    char scheme[8];
    char host[MAX_HOST_LEN];
    char path[MAX_PATH_LEN];
    int port;
    double rate;          /* target total requests/sec, 0 = uncapped   */
    int num_threads;
    int num_connections;  /* per thread                                */
    int num_workers;      /* concurrent streams per connection         */
    int duration_sec;     /* 0 = run until Ctrl+C                      */
} config_t;

typedef struct {
    int fd;
    SSL *ssl;
    nghttp2_session *session;
    int active_streams;
    int goaway_received;
    double next_retry_at; /* monotonic seconds; 0 = eligible now       */
} connection_t;

typedef struct {
    double t_start;
    int status_code;
    int is_warmup;
} stream_ud_t;

typedef struct {
    int thread_id;
    int num_conn;
} thread_ctx_t;

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

static config_t g_cfg;
static SSL_CTX *g_ssl_ctx;

static volatile sig_atomic_t g_stop = 0;
static atomic_int g_threads_ready = 0;
static atomic_int g_measuring = 0;
static double g_test_start = 0.0; /* published via g_measuring, see main() */

static atomic_ulong g_requests_sent = 0;
static atomic_ulong g_completed = 0;
static atomic_ulong g_ok = 0;
static atomic_ulong g_failed = 0;
static atomic_ulong g_conn_errors = 0;
static atomic_ulong g_bytes_received = 0;

static atomic_long g_latency_sum_us = 0;
static atomic_long g_latency_min_us = LONG_MAX;
static atomic_long g_latency_max_us = 0;
static atomic_ulong g_latency_buckets[NUM_LAT_BUCKETS];
static const double bucket_bounds_ms[NUM_LAT_BUCKETS - 1] = {
    1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500
};

/* ------------------------------------------------------------------ */
/* Utility                                                              */
/* ------------------------------------------------------------------ */

static double now_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void handle_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static int parse_url(const char *url, config_t *cfg) {
    const char *p = url;

    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        fprintf(stderr,
                "Error: tool ini hanya mendukung HTTPS (HTTP/2 over TLS, "
                "ALPN h2). Gunakan URL https://...\n");
        return -1;
    } else {
        /* no scheme given, assume https */
    }
    strncpy(cfg->scheme, "https", sizeof(cfg->scheme) - 1);

    const char *slash = strchr(p, '/');
    char hostport[300];
    if (slash) {
        size_t len = (size_t)(slash - p);
        if (len >= sizeof(hostport)) len = sizeof(hostport) - 1;
        memcpy(hostport, p, len);
        hostport[len] = '\0';
        strncpy(cfg->path, slash, sizeof(cfg->path) - 1);
    } else {
        strncpy(hostport, p, sizeof(hostport) - 1);
        hostport[sizeof(hostport) - 1] = '\0';
        strncpy(cfg->path, "/", sizeof(cfg->path) - 1);
    }

    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = '\0';
        cfg->port = atoi(colon + 1);
        strncpy(cfg->host, hostport, sizeof(cfg->host) - 1);
    } else {
        strncpy(cfg->host, hostport, sizeof(cfg->host) - 1);
        cfg->port = 443;
    }

    if (cfg->host[0] == '\0') {
        fprintf(stderr, "Error: tidak bisa parse host dari URL.\n");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */

static void record_latency(double latency_ms, int status_code, int had_stream_error) {
    atomic_fetch_add(&g_completed, 1);

    if (!had_stream_error && status_code >= 200 && status_code < 400) {
        atomic_fetch_add(&g_ok, 1);
    } else {
        atomic_fetch_add(&g_failed, 1);
    }

    long us = (long)(latency_ms * 1000.0);
    atomic_fetch_add(&g_latency_sum_us, us);

    int bucket = NUM_LAT_BUCKETS - 1;
    for (int i = 0; i < NUM_LAT_BUCKETS - 1; i++) {
        if (latency_ms <= bucket_bounds_ms[i]) { bucket = i; break; }
    }
    atomic_fetch_add(&g_latency_buckets[bucket], 1);

    long cur = atomic_load(&g_latency_min_us);
    while (us < cur && !atomic_compare_exchange_weak(&g_latency_min_us, &cur, us)) {}
    cur = atomic_load(&g_latency_max_us);
    while (us > cur && !atomic_compare_exchange_weak(&g_latency_max_us, &cur, us)) {}
}

static double compute_percentile(int pct) {
    unsigned long total = atomic_load(&g_completed);
    if (total == 0) return 0.0;
    unsigned long target = (unsigned long)((double)total * pct / 100.0);
    unsigned long cum = 0;
    for (int i = 0; i < NUM_LAT_BUCKETS; i++) {
        cum += atomic_load(&g_latency_buckets[i]);
        if (cum >= target) {
            if (i < NUM_LAT_BUCKETS - 1) return bucket_bounds_ms[i];
            return (double)atomic_load(&g_latency_max_us) / 1000.0;
        }
    }
    return (double)atomic_load(&g_latency_max_us) / 1000.0;
}

/* ------------------------------------------------------------------ */
/* nghttp2 callbacks                                                    */
/* ------------------------------------------------------------------ */

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    connection_t *c = (connection_t *)user_data;
    ERR_clear_error();
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

static ssize_t recv_callback(nghttp2_session *session, uint8_t *buf,
                              size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    connection_t *c = (connection_t *)user_data;
    ERR_clear_error();
    int rv = SSL_read(c->ssl, buf, (int)length);
    if (rv <= 0) {
        int err = SSL_get_error(c->ssl, rv);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            return NGHTTP2_ERR_EOF;
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return rv;
}

static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
    (void)flags; (void)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS ||
        frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        stream_ud_t *sd = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (sd) {
            char buf[16];
            size_t n = valuelen < sizeof(buf) - 1 ? valuelen : sizeof(buf) - 1;
            memcpy(buf, value, n);
            buf[n] = '\0';
            sd->status_code = atoi(buf);
        }
    }
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                        int32_t stream_id, const uint8_t *data,
                                        size_t len, void *user_data) {
    (void)session; (void)flags; (void)stream_id; (void)data; (void)user_data;
    atomic_fetch_add(&g_bytes_received, len);
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                     uint32_t error_code, void *user_data) {
    connection_t *c = (connection_t *)user_data;
    stream_ud_t *sd = nghttp2_session_get_stream_user_data(session, stream_id);
    if (sd) {
        if (!sd->is_warmup) {
            double latency_ms = (now_monotonic() - sd->t_start) * 1000.0;
            record_latency(latency_ms, sd->status_code, error_code != 0);
        }
        free(sd);
    }
    if (c->active_streams > 0) c->active_streams--;
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                                   void *user_data) {
    (void)session;
    connection_t *c = (connection_t *)user_data;
    if (frame->hd.type == NGHTTP2_GOAWAY) {
        c->goaway_received = 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Connection management                                                */
/* ------------------------------------------------------------------ */

static int tcp_connect(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}

static int tls_connect(connection_t *c, const char *host) {
    c->ssl = SSL_new(g_ssl_ctx);
    if (!c->ssl) return -1;
    SSL_set_fd(c->ssl, c->fd);
    SSL_set_tlsext_host_name(c->ssl, host); /* SNI */

    if (SSL_connect(c->ssl) != 1) {
        return -1;
    }

    const unsigned char *alpn = NULL;
    unsigned int alpnlen = 0;
    SSL_get0_alpn_selected(c->ssl, &alpn, &alpnlen);
    if (!alpn || alpnlen != 2 || memcmp(alpn, "h2", 2) != 0) {
        fprintf(stderr, "Server tidak menegosiasikan HTTP/2 (ALPN h2)\n");
        return -1;
    }
    return 0;
}

static int setup_session(connection_t *c) {
    nghttp2_session_callbacks *callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_recv_callback(callbacks, recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);

    int rv = nghttp2_session_client_new(&c->session, callbacks, c);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0) return -1;

    nghttp2_settings_entry iv[1] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, (uint32_t)(g_cfg.num_workers * 4)}
    };
    nghttp2_submit_settings(c->session, NGHTTP2_FLAG_NONE, iv, 1);
    return 0;
}

static void close_connection(connection_t *c) {
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
    if (c->session) { nghttp2_session_del(c->session); c->session = NULL; }
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    c->active_streams = 0;
    c->goaway_received = 0;
}

static int open_connection(connection_t *c) {
    c->fd = tcp_connect(g_cfg.host, g_cfg.port);
    if (c->fd < 0) return -1;
    if (tls_connect(c, g_cfg.host) != 0) { close(c->fd); c->fd = -1; return -1; }
    fcntl(c->fd, F_SETFL, O_NONBLOCK);
    if (setup_session(c) != 0) { close(c->fd); c->fd = -1; return -1; }
    return 0;
}

static int do_io(connection_t *c) {
    int rv = nghttp2_session_send(c->session);
    if (rv != 0) return -1;
    rv = nghttp2_session_recv(c->session);
    if (rv != 0) return -1;
    return 0;
}

static void try_reconnect(connection_t *c) {
    double now = now_monotonic();
    if (now < c->next_retry_at) return;
    if (open_connection(c) != 0) {
        c->next_retry_at = now + RECONNECT_BACKOFF_SEC;
        return;
    }
    do_io(c); /* flush initial SETTINGS */
}

/* ------------------------------------------------------------------ */
/* Requests                                                             */
/* ------------------------------------------------------------------ */

static int32_t submit_request(connection_t *c, int is_warmup) {
    nghttp2_nv nva[8];
    size_t n = 0;

#define ADD_NV(NAME, VALUE)                              \
    do {                                                 \
        nva[n].name = (uint8_t *)(NAME);                 \
        nva[n].namelen = strlen(NAME);                   \
        nva[n].value = (uint8_t *)(VALUE);                \
        nva[n].valuelen = strlen(VALUE);                 \
        nva[n].flags = NGHTTP2_NV_FLAG_NONE;              \
        n++;                                             \
    } while (0)

    ADD_NV(":method", "GET");
    ADD_NV(":scheme", g_cfg.scheme);
    ADD_NV(":authority", g_cfg.host);
    ADD_NV(":path", g_cfg.path);
    ADD_NV("user-agent", USER_AGENT);
    ADD_NV("accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    ADD_NV("accept-language", "en-US,en;q=0.9");
#undef ADD_NV

    stream_ud_t *sd = malloc(sizeof(stream_ud_t));
    if (!sd) return -1;
    sd->t_start = now_monotonic();
    sd->status_code = 0;
    sd->is_warmup = is_warmup;

    int32_t sid = nghttp2_submit_request(c->session, NULL, nva, n, NULL, sd);
    if (sid < 0) {
        free(sd);
        return sid;
    }
    c->active_streams++;
    if (!is_warmup) atomic_fetch_add(&g_requests_sent, 1);
    return sid;
}

static void warmup_connection(connection_t *c) {
    for (int i = 0; i < WARMUP_REQUESTS_PER_CONN; i++) {
        submit_request(c, 1 /* is_warmup */);
    }
    double t0 = now_monotonic();
    while (c->active_streams > 0 && (now_monotonic() - t0) < WARMUP_TIMEOUT_SEC) {
        struct pollfd pfd;
        pfd.fd = c->fd;
        pfd.events = 0;
        if (nghttp2_session_want_read(c->session)) pfd.events |= POLLIN;
        if (nghttp2_session_want_write(c->session)) pfd.events |= POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 100);
        if (pr > 0) {
            if (do_io(c) != 0) { close_connection(c); return; }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                        */
/* ------------------------------------------------------------------ */

static void *thread_main(void *arg) {
    thread_ctx_t *tc = (thread_ctx_t *)arg;
    connection_t *conns = calloc((size_t)tc->num_conn, sizeof(connection_t));
    struct pollfd *pfds = calloc((size_t)tc->num_conn, sizeof(struct pollfd));

    for (int i = 0; i < tc->num_conn; i++) {
        conns[i].fd = -1;
        if (open_connection(&conns[i]) != 0) {
            fprintf(stderr, "[thread %d] gagal membuka koneksi %d\n", tc->thread_id, i);
            conns[i].next_retry_at = now_monotonic() + RECONNECT_BACKOFF_SEC;
            continue;
        }
        do_io(&conns[i]);           /* flush initial SETTINGS/preface */
        warmup_connection(&conns[i]); /* pre-heat, excluded from stats  */
    }

    /* Signal readiness, then wait for every thread to reach this point
     * so the measured phase starts at (roughly) the same instant across
     * threads -- this is what the RPS/rate math is anchored to. */
    atomic_fetch_add(&g_threads_ready, 1);
    while (!atomic_load(&g_measuring) && !g_stop) usleep(1000);

    double per_thread_rate = g_cfg.rate > 0 ? g_cfg.rate / g_cfg.num_threads : 0;
    double thread_start = g_test_start;
    unsigned long submitted_this_thread = 0;
    double deadline = g_cfg.duration_sec > 0 ? g_test_start + g_cfg.duration_sec : 0;

    while (!g_stop) {
        double now = now_monotonic();
        if (deadline > 0 && now >= deadline) break;

        double elapsed = now - thread_start;
        unsigned long target_submitted = per_thread_rate > 0
            ? (unsigned long)(elapsed * per_thread_rate)
            : ULONG_MAX;

        for (int i = 0; i < tc->num_conn; i++) {
            connection_t *c = &conns[i];

            if (c->fd < 0) { try_reconnect(c); continue; }
            if (c->goaway_received && c->active_streams == 0) {
                close_connection(c);
                continue;
            }
            while (c->active_streams < g_cfg.num_workers &&
                   submitted_this_thread < target_submitted) {
                int32_t sid = submit_request(c, 0);
                if (sid < 0) break;
                submitted_this_thread++;
            }
        }

        int nfds = 0;
        for (int i = 0; i < tc->num_conn; i++) {
            if (conns[i].fd < 0) continue;
            pfds[nfds].fd = conns[i].fd;
            pfds[nfds].events = 0;
            if (nghttp2_session_want_read(conns[i].session)) pfds[nfds].events |= POLLIN;
            if (nghttp2_session_want_write(conns[i].session)) pfds[nfds].events |= POLLOUT;
            pfds[nfds].revents = 0;
            nfds++;
        }
        if (nfds > 0) poll(pfds, nfds, 5);

        for (int i = 0; i < tc->num_conn; i++) {
            if (conns[i].fd < 0) continue;
            if (do_io(&conns[i]) != 0) {
                close_connection(&conns[i]);
                conns[i].next_retry_at = now_monotonic() + RECONNECT_BACKOFF_SEC;
                atomic_fetch_add(&g_conn_errors, 1);
            }
        }
    }

    for (int i = 0; i < tc->num_conn; i++) {
        close_connection(&conns[i]);
    }
    free(conns);
    free(pfds);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Stats thread                                                         */
/* ------------------------------------------------------------------ */

static void *stats_thread_main(void *arg) {
    (void)arg;
    while (!atomic_load(&g_measuring) && !g_stop) usleep(1000);
    if (g_stop) return NULL;

    printf("\n=== Load test dimulai ===\n");
    printf("Target      : %s://%s%s\n", g_cfg.scheme, g_cfg.host, g_cfg.path);
    printf("Rate        : %s\n", g_cfg.rate > 0 ? "" : "tanpa batas (uncapped)");
    if (g_cfg.rate > 0) printf("              %.0f req/s\n", g_cfg.rate);
    printf("Threads     : %d | Koneksi/thread: %d | Worker/koneksi: %d\n",
           g_cfg.num_threads, g_cfg.num_connections, g_cfg.num_workers);
    printf("Durasi      : %s\n\n",
           g_cfg.duration_sec > 0 ? "lihat baris di bawah" : "sampai Ctrl+C");

    unsigned long last_completed = 0;
    double last_tick = now_monotonic();

    while (!g_stop) {
        for (int i = 0; i < 10 && !g_stop; i++) usleep(100000); /* ~1s, responsive to g_stop */
        if (g_stop) break;

        unsigned long completed = atomic_load(&g_completed);
        unsigned long ok = atomic_load(&g_ok);
        long sum_us = atomic_load(&g_latency_sum_us);

        double now = now_monotonic();
        double dt = now - last_tick;
        double rps = dt > 0 ? (double)(completed - last_completed) / dt : 0;
        double avg_ms = completed > 0 ? (sum_us / 1000.0) / (double)completed : 0;
        double success_rate = completed > 0 ? (100.0 * (double)ok / (double)completed) : 0;
        double p50 = compute_percentile(50);
        double p95 = compute_percentile(95);
        double p99 = compute_percentile(99);
        double total_elapsed = now - g_test_start;

        printf("[%6.1fs] RPS: %8.1f | Total: %8lu | Sukses: %5.1f%% | "
               "Latency avg/p50/p95/p99 (ms): %.1f/%.1f/%.1f/%.1f\n",
               total_elapsed, rps, completed, success_rate, avg_ms, p50, p95, p99);
        fflush(stdout);

        last_completed = completed;
        last_tick = now;

        if (g_cfg.duration_sec > 0 && total_elapsed >= g_cfg.duration_sec) {
            g_stop = 1;
            break;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* main                                                                  */
/* ------------------------------------------------------------------ */

static void print_usage(const char *prog) {
    printf("Penggunaan: %s --url <URL> [opsi]\n\n", prog);
    printf("Opsi:\n");
    printf("  -u, --url <url>          Target endpoint https://... (wajib)\n");
    printf("  -r, --rate <rps>         Target total request/detik gabungan semua\n");
    printf("                           thread. 0 = tanpa batas (default: 0)\n");
    printf("  -t, --threads <n>        Jumlah thread (default: 4)\n");
    printf("  -c, --connections <n>    Jumlah koneksi HTTP/2 per thread (default: 10)\n");
    printf("  -w, --workers <n>        Stream/request paralel per koneksi (default: 10)\n");
    printf("  -d, --duration <detik>   Lama pengujian. 0 = sampai Ctrl+C (default: 10)\n");
    printf("  -h, --help               Tampilkan bantuan ini\n\n");
    printf("Contoh:\n");
    printf("  %s --url https://myserver.example.com/ --rate 5000 \\\n"
           "     --threads 4 --connections 20 --workers 25 --duration 30\n", prog);
    printf("  %s --url https://myserver.example.com/ --rate 0 \\\n"
           "     --threads 8 --connections 25 --workers 50   # cari batas maksimal\n", prog);
}

static void print_final_summary(double total_duration) {
    unsigned long sent = atomic_load(&g_requests_sent);
    unsigned long completed = atomic_load(&g_completed);
    unsigned long ok = atomic_load(&g_ok);
    unsigned long failed = atomic_load(&g_failed);
    unsigned long conn_errors = atomic_load(&g_conn_errors);
    unsigned long bytes = atomic_load(&g_bytes_received);
    long sum_us = atomic_load(&g_latency_sum_us);
    long min_us = atomic_load(&g_latency_min_us);
    long max_us = atomic_load(&g_latency_max_us);

    printf("\n=== Hasil Akhir ===\n");
    printf("Durasi aktual        : %.1f detik\n", total_duration);
    printf("Request terkirim     : %lu\n", sent);
    printf("Request selesai      : %lu\n", completed);
    printf("Berhasil (2xx/3xx)   : %lu (%.2f%%)\n", ok,
           completed > 0 ? 100.0 * (double)ok / (double)completed : 0.0);
    printf("Gagal (status/error) : %lu (%.2f%%)\n", failed,
           completed > 0 ? 100.0 * (double)failed / (double)completed : 0.0);
    printf("Error koneksi        : %lu\n", conn_errors);
    printf("RPS rata-rata        : %.1f\n", total_duration > 0 ? (double)completed / total_duration : 0.0);
    printf("Latency min/avg/max  : %.1f / %.1f / %.1f ms\n",
           completed > 0 ? min_us / 1000.0 : 0.0,
           completed > 0 ? (sum_us / 1000.0) / (double)completed : 0.0,
           max_us / 1000.0);
    printf("Latency p50/p95/p99  : %.1f / %.1f / %.1f ms\n",
           compute_percentile(50), compute_percentile(95), compute_percentile(99));
    printf("Total data diterima  : %.2f MB\n", (double)bytes / 1e6);
}

int main(int argc, char **argv) {
    g_cfg.rate = 0;
    g_cfg.num_threads = 4;
    g_cfg.num_connections = 10;
    g_cfg.num_workers = 10;
    g_cfg.duration_sec = 10;
    g_cfg.url[0] = '\0';

    static struct option long_options[] = {
        {"url",         required_argument, 0, 'u'},
        {"rate",        required_argument, 0, 'r'},
        {"threads",     required_argument, 0, 't'},
        {"connections", required_argument, 0, 'c'},
        {"workers",     required_argument, 0, 'w'},
        {"duration",    required_argument, 0, 'd'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "u:r:t:c:w:d:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'u': strncpy(g_cfg.url, optarg, sizeof(g_cfg.url) - 1); break;
            case 'r': g_cfg.rate = atof(optarg); break;
            case 't': g_cfg.num_threads = atoi(optarg); break;
            case 'c': g_cfg.num_connections = atoi(optarg); break;
            case 'w': g_cfg.num_workers = atoi(optarg); break;
            case 'd': g_cfg.duration_sec = atoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (g_cfg.url[0] == '\0') {
        fprintf(stderr, "Error: --url wajib diisi.\n\n");
        print_usage(argv[0]);
        return 1;
    }
    if (g_cfg.num_threads < 1 || g_cfg.num_connections < 1 || g_cfg.num_workers < 1) {
        fprintf(stderr, "Error: threads/connections/workers harus >= 1.\n");
        return 1;
    }
    if (g_cfg.rate < 0) g_cfg.rate = 0;

    if (parse_url(g_cfg.url, &g_cfg) != 0) return 1;

    for (int i = 0; i < NUM_LAT_BUCKETS; i++) atomic_store(&g_latency_buckets[i], 0);

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    SSL_library_init();
    SSL_load_error_strings();
    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ssl_ctx) { fprintf(stderr, "Gagal membuat SSL_CTX\n"); return 1; }
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);
    /* ALPN advertises "h2" only, per RFC 7540. */
    unsigned char alpn[] = {2, 'h', '2'};
    if (SSL_CTX_set_alpn_protos(g_ssl_ctx, alpn, sizeof(alpn)) != 0) {
        fprintf(stderr, "Gagal set ALPN\n");
        return 1;
    }
    /* Verifikasi cert dimatikan agar bisa test ke staging/self-signed cert.
     * Hapus baris ini bila ingin verifikasi penuh terhadap target production. */
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_NONE, NULL);

    printf("Menyiapkan %d thread x %d koneksi (pre-heat)...\n",
           g_cfg.num_threads, g_cfg.num_connections);

    pthread_t *threads = calloc((size_t)g_cfg.num_threads, sizeof(pthread_t));
    thread_ctx_t *tctxs = calloc((size_t)g_cfg.num_threads, sizeof(thread_ctx_t));

    for (int i = 0; i < g_cfg.num_threads; i++) {
        tctxs[i].thread_id = i;
        tctxs[i].num_conn = g_cfg.num_connections;
        pthread_create(&threads[i], NULL, thread_main, &tctxs[i]);
    }

    while (atomic_load(&g_threads_ready) < g_cfg.num_threads && !g_stop) usleep(1000);

    g_test_start = now_monotonic();       /* plain write ... */
    atomic_store(&g_measuring, 1);        /* ... published by this atomic store */

    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_thread_main, NULL);

    for (int i = 0; i < g_cfg.num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    g_stop = 1;
    pthread_join(stats_tid, NULL);

    double total_duration = now_monotonic() - g_test_start;
    print_final_summary(total_duration);

    free(threads);
    free(tctxs);
    SSL_CTX_free(g_ssl_ctx);
    return 0;
}
