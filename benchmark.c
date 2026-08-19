/*
 * ============================================================================
 * HTTP/2 Performance Benchmarking Tool (nghttp2 based)
 * ============================================================================
 * Untuk internal load-testing server milik sendiri.
 *
 * CATATAN TEKNIS PENTING (baca sebelum pakai):
 *
 * 1. Header "Connection: keep-alive" TIDAK dikirim. HTTP/2 (RFC 9113 ss8.2.2)
 *    melarang connection-specific header field seperti Connection, Keep-Alive,
 *    Upgrade, dsb. Mengirimnya bisa membuat server/proxy yang strict menolak
 *    request dengan PROTOCOL_ERROR. Persistent connection di HTTP/2 memang
 *    sudah default (tidak perlu header ini).
 *
 * 2. sendfile()/splice() TIDAK diimplementasikan. Keduanya adalah teknik
 *    zero-copy sisi SERVER untuk mengirim body (file/socket ke socket).
 *    Tool ini adalah CLIENT yang hanya mengirim GET tanpa body, jadi tidak
 *    ada payload untuk di-zero-copy-kan. Kalau butuh varian dengan body
 *    (POST/PUT), beri tahu saya supaya ditambahkan.
 *
 * 3. "Warmup" di sini berarti: semua koneksi + TLS handshake selesai lebih
 *    dulu (pre-connect), lalu idle sebentar sebelum fase pengukuran dimulai.
 *    Tidak ada traffic yang dikirim selama warmup. Progressive scaling mulai
 *    dari base_frame begitu fase pengukuran (duration) dimulai.
 *
 * 4. Kode ini DITULIS TANGAN dengan hati-hati mengikuti API resmi nghttp2 &
 *    OpenSSL, TAPI belum bisa saya compile-test di sandbox ini karena tidak
 *    ada akses jaringan untuk install libnghttp2-dev/libssl-dev. Compile dan
 *    uji di environment Anda sendiri dulu (lihat instruksi compile di bawah).
 *
 * Compile:
 *   gcc -o benchmark benchmark.c -lnghttp2 -lpthread -lssl -lcrypto -Wall -O3 \
 *       -march=native -mtune=native -flto -funroll-loops -fomit-frame-pointer \
 *       -pipe -D_GNU_SOURCE -DNDEBUG
 *
 * Dependencies (Ubuntu/Debian):
 *   sudo apt-get install libnghttp2-dev libssl-dev
 *
 * Contoh pakai:
 *   ./benchmark --url https://internal-server.local:8443/ --threads 16 \
 *               --connections 200 --duration 60 --warmup 5 \
 *               --streams-per-conn 100 --scale-mode 3
 * ============================================================================
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
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <getopt.h>
#include <poll.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/* ============================== Konstanta ============================== */

#define MAX_THREADS            64
#define MAX_URL_LEN            2048
#define MAX_HOST_LEN           256
#define BUFFER_POOL_CHUNK       (64 * 1024)   /* 64KB per buffer per koneksi */
#define DEFAULT_CONNECTIONS     100
#define DEFAULT_DURATION        60
#define DEFAULT_WARMUP          5
#define DEFAULT_STREAMS_PER_CONN 100
#define DEFAULT_SCALE_MODE      2
#define MAX_EVENTS              256
#define HIST_BUCKETS             48            /* log2 histogram latency (us) */
#define STATS_INTERVAL_MS        100
#define MAX_CSV_SAMPLES           20000

/* ====================== Progressive Scaling Modes ====================== */

typedef struct {
    int base_frame;
    int interval_sec;
    int max_frame;
    const char *label;
} scale_cfg_t;

static const scale_cfg_t SCALE_MODES[4] = {
    {0, 0, 0, "invalid"},
    {2, 5, 20,  "Conservative"},
    {5, 3, 50,  "Balanced"},
    {10, 1, 100, "Aggressive"},
};

/* ============================== Konfigurasi ============================= */

typedef struct {
    char url[MAX_URL_LEN];
    char scheme[8];
    char host[MAX_HOST_LEN];
    char path[MAX_URL_LEN];
    int  port;
    bool use_tls;

    int threads;
    int connections;
    int duration_sec;
    int warmup_sec;
    int streams_per_conn;
    int scale_mode;
} app_config_t;

static app_config_t g_cfg;

/* ============================ Statistik Global =========================== */

typedef struct {
    uint64_t total_requests;
    uint64_t total_responses;
    uint64_t total_success;
    uint64_t total_errors;
    uint64_t total_conn_errors;
    uint64_t total_bytes_recv;
    uint64_t active_connections;
    uint64_t active_streams;
} global_stats_t;

static global_stats_t g_stats;   /* diupdate via __sync_fetch_and_add (lock-free) */

/* ============================ Struktur Koneksi ============================ */

typedef struct {
    int64_t start_ts_us;
    int     last_status;
    bool    in_use;
} stream_track_t;

typedef struct {
    int fd;
    SSL *ssl;
    nghttp2_session *session;

    uint8_t *recv_buf;         /* slice dari pre-allocated pool, 64KB */
    size_t   recv_buf_size;

    int open_streams;
    int streams_per_conn;
    stream_track_t *stream_table;  /* carve dari recv_buf, tanpa malloc terpisah */

    int  thread_id;
    bool closed;
} conn_state_t;

typedef struct {
    int thread_id;
    int core_id;
    int epfd;

    conn_state_t *conns;
    int num_conns;

    uint8_t *buf_pool_base;    /* memory pool: num_conns * 64KB, pre-allocated */

    pthread_t tid;
} thread_ctx_t;

static thread_ctx_t g_threads[MAX_THREADS];

/* ============================ Kontrol Global ============================ */

static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_phase = 0;   /* 0=connecting 1=warmup 2=running 3=drain 4=done */
static uint64_t g_threads_ready = 0;
static struct timespec g_test_start_ts;

static SSL_CTX *g_ssl_ctx = NULL;

/* ============================ Latency Histogram ========================== */
/* Bucketed log2 histogram (mikrodetik) - O(1) update, tanpa lock, cukup
 * akurat untuk estimasi P50/P95/P99 pada skala 100K+ RPS. */

static uint64_t g_hist[HIST_BUCKETS];

static inline int hist_bucket_index(uint64_t micros) {
    if (micros < 1) micros = 1;
    int idx = 63 - __builtin_clzll((unsigned long long)micros);
    if (idx >= HIST_BUCKETS) idx = HIST_BUCKETS - 1;
    if (idx < 0) idx = 0;
    return idx;
}

static inline void hist_record(uint64_t micros) {
    int idx = hist_bucket_index(micros);
    __sync_fetch_and_add(&g_hist[idx], 1);
}

static uint64_t hist_percentile(double p) {
    uint64_t total = 0;
    for (int i = 0; i < HIST_BUCKETS; i++) total += g_hist[i];
    if (total == 0) return 0;
    uint64_t target = (uint64_t)(total * p);
    uint64_t cum = 0;
    for (int i = 0; i < HIST_BUCKETS; i++) {
        cum += g_hist[i];
        if (cum >= target) {
            uint64_t lo = (uint64_t)1 << i;
            uint64_t hi = (uint64_t)1 << (i + 1);
            return (lo + hi) / 2;
        }
    }
    return (uint64_t)1 << (HIST_BUCKETS - 1);
}

/* ================================ Signal ================================= */

static void handle_signal(int sig) {
    (void)sig;
    g_shutdown = 1;
}

/* ================================ Util ==================================== */

static inline uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static bool parse_url(const char *url, app_config_t *cfg) {
    const char *p = url;

    if (strncmp(p, "https://", 8) == 0) {
        cfg->use_tls = true;
        strcpy(cfg->scheme, "https");
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        cfg->use_tls = false;
        strcpy(cfg->scheme, "http");
        p += 7;
    } else {
        fprintf(stderr, "Error: URL harus diawali http:// atau https://\n");
        return false;
    }

    const char *slash = strchr(p, '/');
    const char *hostport_end = slash ? slash : p + strlen(p);

    char hostport[MAX_HOST_LEN];
    size_t hplen = (size_t)(hostport_end - p);
    if (hplen >= sizeof(hostport)) hplen = sizeof(hostport) - 1;
    memcpy(hostport, p, hplen);
    hostport[hplen] = '\0';

    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = '\0';
        cfg->port = atoi(colon + 1);
    } else {
        cfg->port = cfg->use_tls ? 443 : 80;
    }
    strncpy(cfg->host, hostport, sizeof(cfg->host) - 1);

    if (slash && *slash != '\0') {
        strncpy(cfg->path, slash, sizeof(cfg->path) - 1);
    } else {
        strcpy(cfg->path, "/");
    }

    if (cfg->host[0] == '\0') {
        fprintf(stderr, "Error: host tidak valid pada URL\n");
        return false;
    }
    return true;
}

/* ============================ Socket & TCP Opts =========================== */

static int create_nonblocking_socket(void) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

    int rcvbuf = 4 * 1024 * 1024;
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

#ifdef TCP_QUICKACK
    setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif

#ifdef SO_BUSY_POLL
    {
        int busy_poll_usec = 50;
        /* boleh gagal (butuh privilege/kernel support tertentu), tidak fatal */
        setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_usec, sizeof(busy_poll_usec));
    }
#endif

    return fd;
}

static int connect_socket(int fd, const char *host, int port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int rv = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rv < 0 && errno != EINPROGRESS) return -1;
    return 0;
}

/* ================================= TLS ==================================== */

static bool init_ssl_ctx(void) {
    SSL_library_init();
    SSL_load_error_strings();

    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ssl_ctx) return false;

    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);

    static const unsigned char alpn[] = { 2, 'h', '2' };
    SSL_CTX_set_alpn_protos(g_ssl_ctx, alpn, sizeof(alpn));

    /* Internal benchmarking: umumnya pakai self-signed cert -> skip verify.
     * Kalau target pakai cert valid dan Anda mau verifikasi, ganti ke
     * SSL_VERIFY_PEER + SSL_CTX_load_verify_locations(). */
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_NONE, NULL);

    return true;
}

/* ============================ nghttp2 Callbacks =========================== */

static ssize_t nghttp2_send_cb(nghttp2_session *session, const uint8_t *data,
                                size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    conn_state_t *conn = (conn_state_t *)user_data;
    ssize_t n;

    if (conn->ssl) {
        n = SSL_write(conn->ssl, data, (int)length);
        if (n <= 0) {
            int err = SSL_get_error(conn->ssl, (int)n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                return NGHTTP2_ERR_WOULDBLOCK;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    } else {
        n = write(conn->fd, data, length);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return NGHTTP2_ERR_WOULDBLOCK;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }
    return n;
}

static ssize_t nghttp2_recv_cb(nghttp2_session *session, uint8_t *buf,
                                size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    conn_state_t *conn = (conn_state_t *)user_data;
    ssize_t n;

    if (conn->ssl) {
        n = SSL_read(conn->ssl, buf, (int)length);
        if (n <= 0) {
            int err = SSL_get_error(conn->ssl, (int)n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                return NGHTTP2_ERR_WOULDBLOCK;
            if (err == SSL_ERROR_ZERO_RETURN) return NGHTTP2_ERR_EOF;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    } else {
        n = read(conn->fd, buf, length);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return NGHTTP2_ERR_WOULDBLOCK;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        if (n == 0) return NGHTTP2_ERR_EOF;
    }
    return n;
}

static int on_header_cb(nghttp2_session *session, const nghttp2_frame *frame,
                         const uint8_t *name, size_t namelen,
                         const uint8_t *value, size_t valuelen,
                         uint8_t flags, void *user_data) {
    (void)session; (void)flags;
    conn_state_t *conn = (conn_state_t *)user_data;

    if (frame->hd.type != NGHTTP2_HEADERS) return 0;
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        int status = atoi((const char *)value);
        int idx = (frame->hd.stream_id / 2) % conn->streams_per_conn;
        if (idx >= 0) conn->stream_table[idx].last_status = status;
    }
    (void)valuelen;
    return 0;
}

static int on_data_chunk_recv_cb(nghttp2_session *session, uint8_t flags,
                                  int32_t stream_id, const uint8_t *data,
                                  size_t len, void *user_data) {
    (void)session; (void)flags; (void)stream_id; (void)data; (void)user_data;
    __sync_fetch_and_add(&g_stats.total_bytes_recv, len);
    return 0;
}

static int on_stream_close_cb(nghttp2_session *session, int32_t stream_id,
                               uint32_t error_code, void *user_data) {
    (void)session;
    conn_state_t *conn = (conn_state_t *)user_data;
    int idx = (stream_id / 2) % conn->streams_per_conn;
    stream_track_t *st = &conn->stream_table[idx];

    if (st->in_use) {
        uint64_t elapsed = now_us() - (uint64_t)st->start_ts_us;
        hist_record(elapsed);
        __sync_fetch_and_add(&g_stats.total_responses, 1);

        if (error_code == 0 && st->last_status > 0 && st->last_status < 400) {
            __sync_fetch_and_add(&g_stats.total_success, 1);
        } else {
            __sync_fetch_and_add(&g_stats.total_errors, 1);
        }
        st->in_use = false;
    }

    if (conn->open_streams > 0) conn->open_streams--;
    __sync_fetch_and_sub(&g_stats.active_streams, 1);
    return 0;
}

/* ============================ Submit Request =============================== */

static bool submit_request(conn_state_t *conn) {
    if (conn->open_streams >= conn->streams_per_conn) return false;

    char authority[300];
    if ((g_cfg.use_tls && g_cfg.port != 443) || (!g_cfg.use_tls && g_cfg.port != 80)) {
        snprintf(authority, sizeof(authority), "%s:%d", g_cfg.host, g_cfg.port);
    } else {
        snprintf(authority, sizeof(authority), "%s", g_cfg.host);
    }

    nghttp2_nv hdrs[8];
    int n = 0;

#define ADD_NV(N, V) do {                                   \
        hdrs[n].name = (uint8_t *)(N);                      \
        hdrs[n].namelen = strlen(N);                         \
        hdrs[n].value = (uint8_t *)(V);                      \
        hdrs[n].valuelen = strlen(V);                        \
        hdrs[n].flags = NGHTTP2_NV_FLAG_NONE;                \
        n++;                                                  \
    } while (0)

    ADD_NV(":method", "GET");
    ADD_NV(":scheme", g_cfg.scheme);
    ADD_NV(":authority", authority);
    ADD_NV(":path", g_cfg.path);
    ADD_NV("user-agent", "Mozilla/5.0 (compatible; Benchmark/1.0)");
    ADD_NV("accept", "text/html,application/xhtml+xml");
    ADD_NV("accept-encoding", "gzip, deflate, br");
    /* Header "Connection" sengaja TIDAK dikirim - lihat catatan di atas file. */
#undef ADD_NV

    int32_t stream_id = nghttp2_submit_request(conn->session, NULL, hdrs, (size_t)n, NULL, conn);
    if (stream_id < 0) return false;

    int idx = (stream_id / 2) % conn->streams_per_conn;
    conn->stream_table[idx].in_use = true;
    conn->stream_table[idx].start_ts_us = (int64_t)now_us();
    conn->stream_table[idx].last_status = 0;

    conn->open_streams++;
    __sync_fetch_and_add(&g_stats.total_requests, 1);
    __sync_fetch_and_add(&g_stats.active_streams, 1);
    return true;
}

/* =============================== I/O Driver ================================= */

static bool conn_step_io(conn_state_t *conn) {
    int rv;

    if (nghttp2_session_want_write(conn->session)) {
        rv = nghttp2_session_send(conn->session);
        if (rv != 0) return false;
    }
    if (nghttp2_session_want_read(conn->session)) {
        rv = nghttp2_session_recv(conn->session);
        if (rv != 0) return false;
    }
    return nghttp2_session_want_read(conn->session) || nghttp2_session_want_write(conn->session);
}

/* ============================ Setup / Reconnect ============================= */

static bool setup_connection(conn_state_t *conn, thread_ctx_t *tctx, int idx) {
    memset(conn, 0, sizeof(*conn));

    conn->fd = create_nonblocking_socket();
    if (conn->fd < 0) return false;

    if (connect_socket(conn->fd, g_cfg.host, g_cfg.port) < 0) {
        close(conn->fd);
        return false;
    }

    /* Tunggu connect() non-blocking selesai (hanya dipakai saat setup awal). */
    struct pollfd pfd = { .fd = conn->fd, .events = POLLOUT, .revents = 0 };
    int prv = poll(&pfd, 1, 5000);
    if (prv <= 0) { close(conn->fd); return false; }

    int soerr = 0;
    socklen_t slen = sizeof(soerr);
    getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
    if (soerr != 0) { close(conn->fd); return false; }

    conn->recv_buf = tctx->buf_pool_base + (size_t)idx * BUFFER_POOL_CHUNK;
    conn->recv_buf_size = BUFFER_POOL_CHUNK;
    conn->streams_per_conn = g_cfg.streams_per_conn;
    /* stream_table di-carve dari pre-allocated pool (bukan malloc per koneksi) */
    conn->stream_table = (stream_track_t *)conn->recv_buf;
    memset(conn->stream_table, 0, sizeof(stream_track_t) * (size_t)conn->streams_per_conn);
    conn->thread_id = tctx->thread_id;

    if (g_cfg.use_tls) {
        conn->ssl = SSL_new(g_ssl_ctx);
        if (!conn->ssl) { close(conn->fd); return false; }

        SSL_set_fd(conn->ssl, conn->fd);
        SSL_set_connect_state(conn->ssl);
        SSL_set_tlsext_host_name(conn->ssl, g_cfg.host);

        int rv;
        while ((rv = SSL_do_handshake(conn->ssl)) != 1) {
            int err = SSL_get_error(conn->ssl, rv);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                struct pollfd hp = {
                    .fd = conn->fd,
                    .events = (short)((err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT),
                    .revents = 0
                };
                if (poll(&hp, 1, 5000) <= 0) { SSL_free(conn->ssl); close(conn->fd); return false; }
                continue;
            }
            SSL_free(conn->ssl);
            close(conn->fd);
            return false;
        }

        const unsigned char *alpn = NULL;
        unsigned int alpnlen = 0;
        SSL_get0_alpn_selected(conn->ssl, &alpn, &alpnlen);
        if (!alpn || alpnlen != 2 || memcmp(alpn, "h2", 2) != 0) {
            fprintf(stderr, "Error: server tidak menyetujui HTTP/2 (ALPN h2)\n");
            SSL_free(conn->ssl);
            close(conn->fd);
            return false;
        }
    }

    nghttp2_session_callbacks *cbs;
    nghttp2_session_callbacks_new(&cbs);
    nghttp2_session_callbacks_set_send_callback(cbs, nghttp2_send_cb);
    nghttp2_session_callbacks_set_recv_callback(cbs, nghttp2_recv_cb);
    nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_cb);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_recv_cb);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_stream_close_cb);

    if (nghttp2_session_client_new(&conn->session, cbs, conn) != 0) {
        nghttp2_session_callbacks_del(cbs);
        if (conn->ssl) SSL_free(conn->ssl);
        close(conn->fd);
        return false;
    }
    nghttp2_session_callbacks_del(cbs);

    nghttp2_settings_entry iv[] = {
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, (uint32_t)g_cfg.streams_per_conn },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65536 },
        { NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384 },
        { NGHTTP2_SETTINGS_ENABLE_PUSH, 0 },
        { NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 4096 },
    };
    if (nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, iv, 5) != 0) {
        fprintf(stderr, "Warning: gagal submit SETTINGS frame\n");
    }
    nghttp2_session_send(conn->session);

    conn->closed = false;
    __sync_fetch_and_add(&g_stats.active_connections, 1);
    return true;
}

static void reconnect_connection(conn_state_t *conn, thread_ctx_t *tctx) {
    epoll_ctl(tctx->epfd, EPOLL_CTL_DEL, conn->fd, NULL);

    if (conn->ssl) { SSL_shutdown(conn->ssl); SSL_free(conn->ssl); conn->ssl = NULL; }
    if (conn->session) { nghttp2_session_del(conn->session); conn->session = NULL; }
    if (conn->fd >= 0) { close(conn->fd); conn->fd = -1; }

    __sync_fetch_and_sub(&g_stats.active_connections, 1);
    __sync_fetch_and_add(&g_stats.total_conn_errors, 1);

    if (g_shutdown || g_phase >= 3) { conn->closed = true; return; }

    int idx = (int)(conn - tctx->conns);
    if (setup_connection(conn, tctx, idx)) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(tctx->epfd, EPOLL_CTL_ADD, conn->fd, &ev);
    } else {
        conn->closed = true;
    }
}

/* ============================ Progressive Scaling ============================ */

static int current_frame_size(const scale_cfg_t *sc) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed_sec = (double)(now.tv_sec - g_test_start_ts.tv_sec) +
                          (double)(now.tv_nsec - g_test_start_ts.tv_nsec) / 1e9;
    if (elapsed_sec < 0) elapsed_sec = 0;

    long steps = (long)(elapsed_sec / (double)sc->interval_sec);
    long frame = sc->base_frame;
    for (long s = 0; s < steps; s++) {
        frame *= 2;
        if (frame >= sc->max_frame) { frame = sc->max_frame; break; }
    }
    if (frame > sc->max_frame) frame = sc->max_frame;
    if (frame < 1) frame = 1;
    return (int)frame;
}

/* ================================ Worker Thread =============================== */

static void *worker_thread_fn(void *arg) {
    thread_ctx_t *tctx = (thread_ctx_t *)arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(tctx->core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    tctx->epfd = epoll_create1(0);
    if (tctx->epfd < 0) {
        fprintf(stderr, "[thread %d] epoll_create1 gagal\n", tctx->thread_id);
        return NULL;
    }

    /* Pre-connect semua koneksi milik thread ini (warm-up phase). */
    for (int i = 0; i < tctx->num_conns; i++) {
        if (!setup_connection(&tctx->conns[i], tctx, i)) {
            fprintf(stderr, "[thread %d] gagal setup koneksi #%d\n", tctx->thread_id, i);
            tctx->conns[i].closed = true;
            tctx->conns[i].fd = -1;
            continue;
        }
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.ptr = &tctx->conns[i];
        epoll_ctl(tctx->epfd, EPOLL_CTL_ADD, tctx->conns[i].fd, &ev);
    }

    __sync_fetch_and_add(&g_threads_ready, 1);

    /* Tunggu fase "running" (g_phase == 2) dimulai oleh main thread. */
    while (g_phase < 2 && !g_shutdown) {
        struct epoll_event events[MAX_EVENTS];
        /* tetap layani I/O (mis. balasan SETTINGS) selama menunggu, jangan idle total */
        int n = epoll_wait(tctx->epfd, events, MAX_EVENTS, 50);
        for (int i = 0; i < n; i++) {
            conn_state_t *conn = (conn_state_t *)events[i].data.ptr;
            if (conn->fd >= 0 && !conn_step_io(conn)) {
                reconnect_connection(conn, tctx);
            }
        }
    }

    const scale_cfg_t *sc = &SCALE_MODES[g_cfg.scale_mode];
    struct epoll_event events[MAX_EVENTS];

    while (!g_shutdown && g_phase == 2) {
        int n = epoll_wait(tctx->epfd, events, MAX_EVENTS, 10);

        for (int i = 0; i < n; i++) {
            conn_state_t *conn = (conn_state_t *)events[i].data.ptr;
            if (conn->fd >= 0 && !conn_step_io(conn)) {
                reconnect_connection(conn, tctx);
            }
        }

        int frame = current_frame_size(sc);
        int submitted = 0;

        for (int c = 0; c < tctx->num_conns && submitted < frame; c++) {
            conn_state_t *conn = &tctx->conns[c];
            if (conn->closed || conn->fd < 0) continue;
            if (submit_request(conn)) {
                submitted++;
            }
        }
        if (submitted > 0) {
            /* flush semua koneksi yang barusan disubmit di cycle ini */
            for (int c = 0; c < tctx->num_conns; c++) {
                conn_state_t *conn = &tctx->conns[c];
                if (!conn->closed && conn->fd >= 0 && nghttp2_session_want_write(conn->session)) {
                    if (!conn_step_io(conn)) reconnect_connection(conn, tctx);
                }
            }
        }
    }

    /* fase drain: berhenti submit request baru, tetap layani I/O in-flight sebentar */
    uint64_t drain_start = now_us();
    while (!g_shutdown && (now_us() - drain_start) < 300000ULL) {
        int n = epoll_wait(tctx->epfd, events, MAX_EVENTS, 10);
        for (int i = 0; i < n; i++) {
            conn_state_t *conn = (conn_state_t *)events[i].data.ptr;
            if (conn->fd >= 0) conn_step_io(conn);
        }
    }

    /* cleanup semua koneksi milik thread ini */
    for (int i = 0; i < tctx->num_conns; i++) {
        conn_state_t *conn = &tctx->conns[i];
        if (conn->fd >= 0) {
            epoll_ctl(tctx->epfd, EPOLL_CTL_DEL, conn->fd, NULL);
            if (conn->ssl) { SSL_shutdown(conn->ssl); SSL_free(conn->ssl); }
            if (conn->session) nghttp2_session_del(conn->session);
            close(conn->fd);
            conn->fd = -1;
        }
    }
    close(tctx->epfd);

    return NULL;
}

/* =============================== Statistik / CSV =============================== */

typedef struct {
    double t_sec;
    double rps;
    uint64_t success;
    uint64_t errors;
    uint64_t active_conns;
    int frame;
} csv_sample_t;

static csv_sample_t g_samples[MAX_CSV_SAMPLES];
static int g_sample_count = 0;

static void *stats_thread_fn(void *arg) {
    (void)arg;
    uint64_t last_responses = 0;
    struct timespec last_ts, now_ts;
    clock_gettime(CLOCK_MONOTONIC, &last_ts);

    while (g_phase < 4) {
        usleep(STATS_INTERVAL_MS * 1000);
        if (g_phase < 2) continue;

        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        double dt = (double)(now_ts.tv_sec - last_ts.tv_sec) +
                    (double)(now_ts.tv_nsec - last_ts.tv_nsec) / 1e9;

        uint64_t responses = g_stats.total_responses;
        double rps = (dt > 0) ? (double)(responses - last_responses) / dt : 0.0;
        last_responses = responses;
        last_ts = now_ts;

        double elapsed = (double)(now_ts.tv_sec - g_test_start_ts.tv_sec) +
                          (double)(now_ts.tv_nsec - g_test_start_ts.tv_nsec) / 1e9;
        double total = (double)g_cfg.duration_sec;
        double pct = (total > 0) ? elapsed / total : 0;
        if (pct > 1.0) pct = 1.0;
        if (pct < 0.0) pct = 0.0;

        const scale_cfg_t *sc = &SCALE_MODES[g_cfg.scale_mode];
        int frame = current_frame_size(sc);

        uint64_t succ = g_stats.total_success;
        uint64_t err = g_stats.total_errors;
        uint64_t total_resp = succ + err;
        double success_rate = (total_resp > 0) ? (100.0 * (double)succ / (double)total_resp) : 0.0;

        uint64_t p50 = hist_percentile(0.50);
        uint64_t p95 = hist_percentile(0.95);
        uint64_t p99 = hist_percentile(0.99);

        const int barw = 30;
        int filled = (int)(pct * barw);
        char bar[64];
        int bp = 0;
        bar[bp++] = '[';
        for (int i = 0; i < barw; i++) bar[bp++] = (i < filled) ? '#' : '-';
        bar[bp++] = ']';
        bar[bp] = '\0';

        printf("\r%s %5.1f%% | RPS:%9.0f | OK:%6.2f%% | P50:%5llu P95:%5llu P99:%5llu us | "
               "conn:%3llu | frame:%3d | connerr:%llu   ",
               bar, pct * 100.0, rps, success_rate,
               (unsigned long long)p50, (unsigned long long)p95, (unsigned long long)p99,
               (unsigned long long)g_stats.active_connections, frame,
               (unsigned long long)g_stats.total_conn_errors);
        fflush(stdout);

        if (g_sample_count < MAX_CSV_SAMPLES) {
            g_samples[g_sample_count].t_sec = elapsed;
            g_samples[g_sample_count].rps = rps;
            g_samples[g_sample_count].success = succ;
            g_samples[g_sample_count].errors = err;
            g_samples[g_sample_count].active_conns = g_stats.active_connections;
            g_samples[g_sample_count].frame = frame;
            g_sample_count++;
        }
    }
    printf("\n");
    return NULL;
}

static void export_csv(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) { perror("fopen csv"); return; }

    fprintf(f, "time_sec,rps,success,errors,active_connections,frame_size\n");
    for (int i = 0; i < g_sample_count; i++) {
        csv_sample_t *s = &g_samples[i];
        fprintf(f, "%.1f,%.1f,%llu,%llu,%llu,%d\n",
                s->t_sec, s->rps,
                (unsigned long long)s->success, (unsigned long long)s->errors,
                (unsigned long long)s->active_conns, s->frame);
    }
    fclose(f);
    printf("Hasil per-interval disimpan ke: %s\n", filename);
}

/* ================================ Argumen CLI ================================ */

static void print_usage(const char *prog) {
    printf("Pemakaian: %s --url <target> [opsi]\n\n", prog);
    printf("Opsi:\n");
    printf("  --url <target>            URL target (wajib), contoh: https://host:port/path\n");
    printf("  --threads <num>           Jumlah thread (default: auto = jumlah core, max %d)\n", MAX_THREADS);
    printf("  --connections <num>       Jumlah koneksi total (default: %d)\n", DEFAULT_CONNECTIONS);
    printf("  --duration <detik>        Durasi fase pengukuran (default: %d)\n", DEFAULT_DURATION);
    printf("  --warmup <detik>          Durasi warmup/settle setelah connect (default: %d)\n", DEFAULT_WARMUP);
    printf("  --streams-per-conn <num>  Max concurrent stream per koneksi, max 100 (default: %d)\n", DEFAULT_STREAMS_PER_CONN);
    printf("  --scale-mode <1|2|3>      1=Conservative 2=Balanced 3=Aggressive (default: %d)\n", DEFAULT_SCALE_MODE);
    printf("  --help                    Tampilkan bantuan ini\n");
}

static void set_defaults(app_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    cfg->threads = (ncpu > 0) ? (int)ncpu : 4;
    if (cfg->threads > MAX_THREADS) cfg->threads = MAX_THREADS;
    cfg->connections = DEFAULT_CONNECTIONS;
    cfg->duration_sec = DEFAULT_DURATION;
    cfg->warmup_sec = DEFAULT_WARMUP;
    cfg->streams_per_conn = DEFAULT_STREAMS_PER_CONN;
    cfg->scale_mode = DEFAULT_SCALE_MODE;
}

static bool parse_args(int argc, char **argv, app_config_t *cfg) {
    set_defaults(cfg);
    bool has_url = false;

    static struct option long_opts[] = {
        {"url",              required_argument, 0, 'u'},
        {"threads",          required_argument, 0, 't'},
        {"connections",      required_argument, 0, 'c'},
        {"duration",         required_argument, 0, 'd'},
        {"warmup",           required_argument, 0, 'w'},
        {"streams-per-conn", required_argument, 0, 's'},
        {"scale-mode",       required_argument, 0, 'm'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "", long_opts, &idx)) != -1) {
        switch (opt) {
            case 'u': strncpy(cfg->url, optarg, sizeof(cfg->url) - 1); has_url = true; break;
            case 't': cfg->threads = atoi(optarg); break;
            case 'c': cfg->connections = atoi(optarg); break;
            case 'd': cfg->duration_sec = atoi(optarg); break;
            case 'w': cfg->warmup_sec = atoi(optarg); break;
            case 's': cfg->streams_per_conn = atoi(optarg); break;
            case 'm': cfg->scale_mode = atoi(optarg); break;
            case 'h': print_usage(argv[0]); exit(0);
            default:  print_usage(argv[0]); return false;
        }
    }

    if (!has_url) {
        fprintf(stderr, "Error: --url wajib diisi\n");
        print_usage(argv[0]);
        return false;
    }
    if (!parse_url(cfg->url, cfg)) return false;

    if (cfg->threads < 1 || cfg->threads > MAX_THREADS) {
        fprintf(stderr, "Error: --threads harus 1..%d\n", MAX_THREADS);
        return false;
    }
    if (cfg->connections < cfg->threads) cfg->connections = cfg->threads;
    if (cfg->scale_mode < 1 || cfg->scale_mode > 3) {
        fprintf(stderr, "Error: --scale-mode harus 1, 2, atau 3\n");
        return false;
    }
    if (cfg->streams_per_conn < 1 || cfg->streams_per_conn > 100) {
        fprintf(stderr, "Error: --streams-per-conn harus 1..100 (batas SETTINGS_MAX_CONCURRENT_STREAMS)\n");
        return false;
    }
    if (cfg->duration_sec < 1) {
        fprintf(stderr, "Error: --duration harus >= 1\n");
        return false;
    }

    return true;
}

/* ================================== main() ==================================== */

int main(int argc, char **argv) {
    if (!parse_args(argc, argv, &g_cfg)) return 1;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    if (g_cfg.use_tls && !init_ssl_ctx()) {
        fprintf(stderr, "Error: gagal inisialisasi SSL context\n");
        return 1;
    }

    const scale_cfg_t *sc = &SCALE_MODES[g_cfg.scale_mode];

    printf("=== HTTP/2 Benchmark Tool ===\n");
    printf("Target        : %s\n", g_cfg.url);
    printf("Threads       : %d\n", g_cfg.threads);
    printf("Connections   : %d\n", g_cfg.connections);
    printf("Streams/conn  : %d\n", g_cfg.streams_per_conn);
    printf("Duration      : %ds (warmup %ds)\n", g_cfg.duration_sec, g_cfg.warmup_sec);
    printf("Scale mode    : %d (%s) base:%d interval:%ds ceiling:%d\n",
           g_cfg.scale_mode, sc->label, sc->base_frame, sc->interval_sec, sc->max_frame);
    printf("==============================\n\n");

    int base_conns = g_cfg.connections / g_cfg.threads;
    int extra = g_cfg.connections % g_cfg.threads;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;

    for (int i = 0; i < g_cfg.threads; i++) {
        thread_ctx_t *t = &g_threads[i];
        t->thread_id = i;
        t->core_id = (int)(i % ncpu);
        t->num_conns = base_conns + (i < extra ? 1 : 0);
        if (t->num_conns < 1) t->num_conns = 1;
        t->conns = calloc((size_t)t->num_conns, sizeof(conn_state_t));
        t->buf_pool_base = calloc((size_t)t->num_conns, BUFFER_POOL_CHUNK);
        if (!t->conns || !t->buf_pool_base) {
            fprintf(stderr, "Error: alokasi memory pool gagal (thread %d)\n", i);
            return 1;
        }
    }

    g_phase = 0;

    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_thread_fn, NULL);

    for (int i = 0; i < g_cfg.threads; i++) {
        pthread_create(&g_threads[i].tid, NULL, worker_thread_fn, &g_threads[i]);
    }

    /* tunggu semua thread selesai pre-connect (dengan timeout jaga-jaga) */
    printf("Menyambungkan %d koneksi...\n", g_cfg.connections);
    uint64_t wait_start = now_us();
    while ((int)g_threads_ready < g_cfg.threads && !g_shutdown) {
        if (now_us() - wait_start > 30000000ULL) {  /* timeout 30 detik */
            fprintf(stderr, "Warning: timeout menunggu semua thread connect, lanjut dengan yang berhasil\n");
            break;
        }
        usleep(10000);
    }

    printf("Koneksi selesai (aktif: %llu). Warmup %ds...\n",
           (unsigned long long)g_stats.active_connections, g_cfg.warmup_sec);

    g_phase = 1;
    for (int s = 0; s < g_cfg.warmup_sec && !g_shutdown; s++) sleep(1);

    printf("Warmup selesai. Mulai pengukuran %ds (scale-mode %d: %s)...\n\n",
           g_cfg.duration_sec, g_cfg.scale_mode, sc->label);

    clock_gettime(CLOCK_MONOTONIC, &g_test_start_ts);
    g_phase = 2;

    for (int s = 0; s < g_cfg.duration_sec && !g_shutdown; s++) sleep(1);

    printf("\n\nMenghentikan pengiriman request baru, draining in-flight requests...\n");
    g_phase = 3;
    usleep(350000);
    g_phase = 4;
    g_shutdown = 1;

    for (int i = 0; i < g_cfg.threads; i++) pthread_join(g_threads[i].tid, NULL);
    pthread_join(stats_tid, NULL);

    uint64_t succ = g_stats.total_success;
    uint64_t err = g_stats.total_errors;
    uint64_t total_resp = succ + err;

    printf("\n=== HASIL AKHIR ===\n");
    printf("Total request terkirim  : %llu\n", (unsigned long long)g_stats.total_requests);
    printf("Total response diterima : %llu\n", (unsigned long long)g_stats.total_responses);
    printf("Sukses                  : %llu (%.2f%%)\n",
           (unsigned long long)succ, total_resp ? (100.0 * (double)succ / (double)total_resp) : 0.0);
    printf("Gagal (4xx/5xx/protocol): %llu\n", (unsigned long long)err);
    printf("Connection error        : %llu\n", (unsigned long long)g_stats.total_conn_errors);
    printf("Total bytes diterima    : %llu\n", (unsigned long long)g_stats.total_bytes_recv);
    printf("Latency P50 / P95 / P99 : %llu / %llu / %llu us\n",
           (unsigned long long)hist_percentile(0.50),
           (unsigned long long)hist_percentile(0.95),
           (unsigned long long)hist_percentile(0.99));
    printf("Rata-rata RPS           : %.0f\n",
           g_cfg.duration_sec > 0 ? (double)g_stats.total_responses / (double)g_cfg.duration_sec : 0.0);

    export_csv("benchmark_results.csv");

    for (int i = 0; i < g_cfg.threads; i++) {
        free(g_threads[i].conns);
        free(g_threads[i].buf_pool_base);
    }
    if (g_ssl_ctx) SSL_CTX_free(g_ssl_ctx);

    return 0;
}
