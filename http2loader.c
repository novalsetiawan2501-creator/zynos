/*
 * http2load.c - Multi-threaded HTTP/2 load testing tool
 *
 * Arsitektur:
 *   - N thread tetap (fixed pool), masing-masing di-pin ke 1 CPU core.
 *   - Tiap thread punya epoll instance sendiri (edge-triggered) dan
 *     mengelola sejumlah koneksi HTTP/2 persisten miliknya sendiri
 *     (tidak ada sharing state antar-thread -> tidak perlu lock di hot path).
 *   - Tiap koneksi = 1 nghttp2_session dengan hingga --streams-per-conn
 *     stream konkuren.
 *   - Statistik global memakai atomic counter (lock-free).
 *
 * Catatan teknis penting (dibaca dulu sebelum protes ke penulis kode):
 *   1. Header "Connection: keep-alive" TIDAK dikirim sebagai HTTP/2 header.
 *      RFC 7540 �8.1.2.2 melarang connection-specific header field di HTTP/2
 *      (server yang patuh spek akan mengirim balik stream error
 *      PROTOCOL_ERROR bila header ini dikirim). Persistent connection di
 *      HTTP/2 memang defaultnya keep-alive selama sesi belum di-GOAWAY,
 *      jadi header ini secara semantik tidak diperlukan.
 *   2. sendfile()/splice() adalah teknik zero-copy sisi SERVER (mengirim isi
 *      file/socket tanpa copy ke user-space). Tool ini adalah HTTP/2 CLIENT
 *      (load generator) - tidak ada file yang disajikan dan tidak ada
 *      proxying antar-socket, sehingga sendfile/splice tidak applicable di
 *      sisi client. Sebagai gantinya, zero-copy diusahakan lewat:
 *        - buffer pool pre-allocated (tanpa malloc di hot path)
 *        - nghttp2_session_mem_recv/nghttp2_session_send yang bekerja
 *          langsung di atas buffer yang sama (tanpa copy tambahan)
 *      Ini adalah pendekatan yang benar untuk client load generator.
 *
 * Compile:
 *   gcc -o http2load http2load.c -lnghttp2 -lpthread -lssl -lcrypto \
 *       -Wall -O3 -march=native -mtune=native -flto -funroll-loops \
 *       -fomit-frame-pointer -pipe -D_GNU_SOURCE -DNDEBUG
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <nghttp2/nghttp2.h>

/* ==================== Konstanta & default ==================== */

#define MAX_URL_LEN         2048
#define MAX_HOST_LEN        256
#define MAX_PATH_LEN        1024
#define POOL_BUF_SIZE        (64 * 1024)   /* 64KB per buffer sesuai spek */
#define MAX_EVENTS           256
#define LAT_RING_CAP         200000        /* sample ring per thread utk percentile */
#define STATS_INTERVAL_MS    100
#define MAX_HISTORY_POINTS   200000

#define DEFAULT_CONNECTIONS      100
#define DEFAULT_DURATION         60
#define DEFAULT_WARMUP           5
#define DEFAULT_STREAMS_PER_CONN 100
#define DEFAULT_BATCH_SIZE       20

#define MAKE_NV(NAME, VALUE)                                                 \
    { (uint8_t *)(NAME), (uint8_t *)(VALUE), sizeof(NAME) - 1,               \
      strlen(VALUE), NGHTTP2_NV_FLAG_NONE }

/* ==================== Konfigurasi global ==================== */

typedef struct {
    char url[MAX_URL_LEN];
    char scheme[8];
    char host[MAX_HOST_LEN];
    char path[MAX_PATH_LEN];
    char authority[MAX_HOST_LEN + 8];
    int  port;
    int  is_tls;

    int  threads;
    int  connections;
    int  duration;
    int  warmup;
    int  streams_per_conn;
    int  batch_size;
} config_t;

static config_t g_cfg;
static struct sockaddr_storage g_target_addr;
static socklen_t g_target_addrlen;

/* ==================== Statistik global (lock-free) ==================== */

static atomic_ulong g_total_requests   = 0;
static atomic_ulong g_total_success    = 0;
static atomic_ulong g_total_errors     = 0;
static atomic_ulong g_err_connect      = 0;
static atomic_ulong g_err_timeout      = 0;
static atomic_ulong g_err_http         = 0;
static atomic_ulong g_err_stream       = 0;
static atomic_ulong g_bytes_received   = 0;
static atomic_int   g_active_conns     = 0;
static atomic_int   g_connected_conns  = 0;

static volatile sig_atomic_t g_running       = 1;
static volatile sig_atomic_t g_warmup_done   = 0;
static volatile sig_atomic_t g_test_started  = 0;
static struct timespec g_test_start_ts;
static struct timespec g_test_end_ts;

/* Riwayat untuk CSV */
typedef struct {
    double   t_sec;
    uint64_t rps;
    uint64_t success;
    uint64_t errors;
    int      active_conns;
    double   p50, p95, p99;
} stats_point_t;

static stats_point_t *g_history;
static int g_history_count = 0;
static pthread_mutex_t g_history_lock = PTHREAD_MUTEX_INITIALIZER;

/* ==================== Struktur per-thread ==================== */

typedef enum {
    CONN_STATE_CONNECTING = 0,
    CONN_STATE_TLS_HANDSHAKE,
    CONN_STATE_H2_READY,
    CONN_STATE_CLOSED
} conn_state_t;

typedef struct thread_ctx thread_ctx_t;

typedef struct conn_s {
    int             fd;
    SSL            *ssl;
    nghttp2_session *session;
    conn_state_t    state;
    thread_ctx_t   *tctx;
    int             active_streams;
    uint8_t        *rbuf;      /* dari pool, 64KB */
} conn_t;

typedef struct stream_data_s {
    struct timespec start;
    conn_t         *conn;
    int             status_code;
    int             in_use;
} stream_data_t;

struct thread_ctx {
    int              id;
    int              epoll_fd;
    conn_t          *conns;
    int              nconns;
    pthread_t        thread;
    SSL_CTX         *ssl_ctx;

    /* pre-allocated read-buffer pool: 1 buffer 64KB per koneksi */
    uint8_t         *pool_mem;

    /* pre-allocated stream_data pool (request pooling) */
    stream_data_t   *stream_pool;
    int              stream_pool_cap;
    int             *stream_free_stack;
    int              stream_free_top;

    /* ring buffer latency (microsecond) untuk percentile */
    uint32_t        *lat_ring;
    unsigned         lat_idx;
    int              lat_cap;

    unsigned long    local_requests;
    unsigned long    local_success;
    unsigned long    local_errors;
};

static thread_ctx_t *g_threads;

/* ==================== Util waktu ==================== */

static inline double ts_diff_sec(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

static inline uint64_t ts_diff_usec(struct timespec a, struct timespec b) {
    return (uint64_t)(b.tv_sec - a.tv_sec) * 1000000ULL +
           (b.tv_nsec - a.tv_nsec) / 1000;
}

/* ==================== Signal handling (graceful shutdown) ==================== */

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

/* ==================== Parsing URL & argumen ==================== */

static void die(const char *msg) {
    fprintf(stderr, "[FATAL] %s\n", msg);
    exit(1);
}

static int parse_url(const char *url, config_t *cfg) {
    const char *p = url;

    if (strncasecmp(p, "https://", 8) == 0) {
        strcpy(cfg->scheme, "https");
        cfg->is_tls = 1;
        cfg->port = 443;
        p += 8;
    } else if (strncasecmp(p, "http://", 7) == 0) {
        strcpy(cfg->scheme, "http");
        cfg->is_tls = 0;
        cfg->port = 80;
        p += 7;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *hostend = slash ? slash : p + strlen(p);

    const char *colon = memchr(p, ':', hostend - p);
    if (colon) {
        size_t hlen = colon - p;
        if (hlen >= MAX_HOST_LEN) return -1;
        memcpy(cfg->host, p, hlen);
        cfg->host[hlen] = '\0';
        cfg->port = atoi(colon + 1);
    } else {
        size_t hlen = hostend - p;
        if (hlen >= MAX_HOST_LEN) return -1;
        memcpy(cfg->host, p, hlen);
        cfg->host[hlen] = '\0';
    }

    if (slash) {
        snprintf(cfg->path, sizeof(cfg->path), "%s", slash);
    } else {
        snprintf(cfg->path, sizeof(cfg->path), "/");
    }

    if (cfg->port == 80 || cfg->port == 443) {
        snprintf(cfg->authority, sizeof(cfg->authority), "%s", cfg->host);
    } else {
        snprintf(cfg->authority, sizeof(cfg->authority), "%s:%d", cfg->host, cfg->port);
    }

    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Penggunaan: %s --url <target> [opsi]\n"
        "\n"
        "Opsi:\n"
        "  --url <target>              URL target, wajib. Contoh: https://host/path\n"
        "  --threads <num>              Jumlah thread (default: auto-detect core)\n"
        "  --connections <num>          Jumlah koneksi HTTP/2 (default: %d)\n"
        "  --duration <detik>           Durasi test (default: %d)\n"
        "  --warmup <detik>             Warmup sebelum test (default: %d)\n"
        "  --streams-per-conn <num>     Max concurrent streams/koneksi (default: %d)\n"
        "  --batch-size <num>           Request per event-loop cycle (default: %d)\n"
        "  -h, --help                   Tampilkan bantuan ini\n",
        prog, DEFAULT_CONNECTIONS, DEFAULT_DURATION, DEFAULT_WARMUP,
        DEFAULT_STREAMS_PER_CONN, DEFAULT_BATCH_SIZE);
}

static void parse_args(int argc, char **argv) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.threads          = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (g_cfg.threads < 1) g_cfg.threads = 4;
    if (g_cfg.threads > 16) g_cfg.threads = 16; /* sesuai spek: 8-16 thread */
    if (g_cfg.threads < 8) {
        /* tetap hormati core count kecil, tapi jaga minimal wajar */
    }
    g_cfg.connections      = DEFAULT_CONNECTIONS;
    g_cfg.duration         = DEFAULT_DURATION;
    g_cfg.warmup           = DEFAULT_WARMUP;
    g_cfg.streams_per_conn = DEFAULT_STREAMS_PER_CONN;
    g_cfg.batch_size       = DEFAULT_BATCH_SIZE;

    int have_url = 0;

    static struct option_map { const char *name; } dummy; (void)dummy;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            snprintf(g_cfg.url, sizeof(g_cfg.url), "%s", argv[++i]);
            have_url = 1;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            g_cfg.threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--connections") == 0 && i + 1 < argc) {
            g_cfg.connections = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            g_cfg.duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            g_cfg.warmup = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--streams-per-conn") == 0 && i + 1 < argc) {
            g_cfg.streams_per_conn = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--batch-size") == 0 && i + 1 < argc) {
            g_cfg.batch_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Argumen tidak dikenal: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }

    if (!have_url) {
        fprintf(stderr, "--url wajib diisi.\n");
        print_usage(argv[0]);
        exit(1);
    }

    if (parse_url(g_cfg.url, &g_cfg) != 0) {
        die("URL tidak valid, gunakan format http(s)://host[:port]/path");
    }

    if (g_cfg.threads < 1)          g_cfg.threads = 1;
    if (g_cfg.connections < g_cfg.threads) g_cfg.connections = g_cfg.threads;
    if (g_cfg.streams_per_conn < 1) g_cfg.streams_per_conn = 1;
    if (g_cfg.batch_size < 1)       g_cfg.batch_size = 1;
}

/* ==================== Resolve target sekali di awal ==================== */

static void resolve_target(void) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", g_cfg.port);

    int rc = getaddrinfo(g_cfg.host, portstr, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "DNS resolve gagal untuk %s: %s\n", g_cfg.host, gai_strerror(rc));
        exit(1);
    }

    memcpy(&g_target_addr, res->ai_addr, res->ai_addrlen);
    g_target_addrlen = res->ai_addrlen;
    freeaddrinfo(res);
}

/* ==================== Socket tuning sesuai spek ==================== */

static int create_tuned_socket(void) {
    int fd = socket(g_target_addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

    int bufsz = 4 * 1024 * 1024; /* 4MB */
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));

#ifdef TCP_QUICKACK
    setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif

#ifdef SO_BUSY_POLL
    {
        int busy_usec = 50;
        setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy_usec, sizeof(busy_usec));
    }
#endif

    return fd;
}

/* ==================== Memory pool & stream pool ==================== */

static void thread_pool_init(thread_ctx_t *t, int nconns) {
    t->pool_mem = malloc((size_t)nconns * POOL_BUF_SIZE);
    if (!t->pool_mem) die("gagal alokasi buffer pool");

    t->stream_pool_cap = nconns * g_cfg.streams_per_conn;
    t->stream_pool = calloc((size_t)t->stream_pool_cap, sizeof(stream_data_t));
    t->stream_free_stack = malloc((size_t)t->stream_pool_cap * sizeof(int));
    if (!t->stream_pool || !t->stream_free_stack) die("gagal alokasi stream pool");
    for (int i = 0; i < t->stream_pool_cap; i++) {
        t->stream_free_stack[i] = i;
    }
    t->stream_free_top = t->stream_pool_cap;

    t->lat_cap = LAT_RING_CAP;
    t->lat_ring = calloc((size_t)t->lat_cap, sizeof(uint32_t));
    t->lat_idx = 0;
}

static stream_data_t *stream_alloc(thread_ctx_t *t, conn_t *c) {
    if (t->stream_free_top == 0) return NULL; /* pool habis, drop request */
    int idx = t->stream_free_stack[--t->stream_free_top];
    stream_data_t *sd = &t->stream_pool[idx];
    sd->conn = c;
    sd->status_code = 0;
    sd->in_use = 1;
    clock_gettime(CLOCK_MONOTONIC, &sd->start);
    /* simpan index di slot pertama sebelum pointer via trik offset */
    return sd;
}

static void stream_free(thread_ctx_t *t, stream_data_t *sd) {
    int idx = (int)(sd - t->stream_pool);
    sd->in_use = 0;
    t->stream_free_stack[t->stream_free_top++] = idx;
}

static void lat_ring_push(thread_ctx_t *t, uint64_t usec) {
    uint32_t v = (usec > 0xFFFFFFFFULL) ? 0xFFFFFFFF : (uint32_t)usec;
    t->lat_ring[t->lat_idx % (unsigned)t->lat_cap] = v;
    t->lat_idx++;
}

/* ==================== nghttp2 callbacks ==================== */

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    conn_t *c = (conn_t *)user_data;
    ssize_t n;

    if (c->ssl) {
        n = SSL_write(c->ssl, data, (int)length);
        if (n <= 0) {
            int err = SSL_get_error(c->ssl, (int)n);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
                return NGHTTP2_ERR_WOULDBLOCK;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    } else {
        n = write(c->fd, data, length);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return NGHTTP2_ERR_WOULDBLOCK;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }
    return n;
}

static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
    (void)flags; (void)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS) return 0;
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        stream_data_t *sd = (stream_data_t *)nghttp2_session_get_stream_user_data(
            session, frame->hd.stream_id);
        if (sd) {
            char buf[8];
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
    atomic_fetch_add_explicit(&g_bytes_received, len, memory_order_relaxed);
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                     uint32_t error_code, void *user_data) {
    conn_t *c = (conn_t *)user_data;
    thread_ctx_t *t = c->tctx;

    stream_data_t *sd = (stream_data_t *)nghttp2_session_get_stream_user_data(
        session, stream_id);

    c->active_streams--;

    if (sd) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t usec = ts_diff_usec(sd->start, now);

        atomic_fetch_add_explicit(&g_total_requests, 1, memory_order_relaxed);
        t->local_requests++;

        if (error_code == NGHTTP2_NO_ERROR && sd->status_code >= 200 && sd->status_code < 400) {
            atomic_fetch_add_explicit(&g_total_success, 1, memory_order_relaxed);
            t->local_success++;
            lat_ring_push(t, usec);
        } else {
            atomic_fetch_add_explicit(&g_total_errors, 1, memory_order_relaxed);
            t->local_errors++;
            if (error_code != NGHTTP2_NO_ERROR) {
                atomic_fetch_add_explicit(&g_err_stream, 1, memory_order_relaxed);
            } else {
                atomic_fetch_add_explicit(&g_err_http, 1, memory_order_relaxed);
            }
        }
        stream_free(t, sd);
    }

    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                                   void *user_data) {
    (void)session;
    conn_t *c = (conn_t *)user_data;
    if (frame->hd.type == NGHTTP2_GOAWAY) {
        c->state = CONN_STATE_CLOSED;
    }
    return 0;
}

static nghttp2_session_callbacks *g_callbacks;

static void init_nghttp2_callbacks(void) {
    nghttp2_session_callbacks_new(&g_callbacks);
    nghttp2_session_callbacks_set_send_callback(g_callbacks, send_callback);
    nghttp2_session_callbacks_set_on_header_callback(g_callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(g_callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(g_callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(g_callbacks, on_frame_recv_callback);
}

/* ==================== Submit request (batch) ==================== */

static void submit_one_request(conn_t *c) {
    thread_ctx_t *t = c->tctx;
    stream_data_t *sd = stream_alloc(t, c);
    if (!sd) return; /* pool penuh, skip cycle ini */

    nghttp2_nv nva[] = {
        MAKE_NV(":method", "GET"),
        MAKE_NV(":scheme", g_cfg.scheme),
        MAKE_NV(":authority", g_cfg.authority),
        MAKE_NV(":path", g_cfg.path),
        MAKE_NV("user-agent", "Mozilla/5.0 (compatible; ZANGXX/1.0)"),
        MAKE_NV("accept", "text/html,application/xhtml+xml"),
        MAKE_NV("accept-encoding", "gzip, deflate, br"),
        /* "Connection: keep-alive" sengaja TIDAK dikirim - lihat catatan
         * teknis di header file ini (dilarang oleh RFC 7540 8.1.2.2). */
    };

    int32_t stream_id = nghttp2_submit_request(c->session, NULL, nva,
                                                sizeof(nva) / sizeof(nva[0]),
                                                NULL, sd);
    if (stream_id < 0) {
        stream_free(t, sd);
        return;
    }
    c->active_streams++;
}

static void fill_request_batch(conn_t *c) {
    if (c->state != CONN_STATE_H2_READY) return;
    int room = g_cfg.streams_per_conn - c->active_streams;
    if (room <= 0) return;
    int n = g_cfg.batch_size < room ? g_cfg.batch_size : room;
    for (int i = 0; i < n; i++) {
        submit_one_request(c);
    }
}

/* ==================== Koneksi: connect, TLS, HTTP/2 setup ==================== */

static void epoll_mod(thread_ctx_t *t, conn_t *c, uint32_t events) {
    struct epoll_event ev;
    ev.events = events | EPOLLET;
    ev.data.ptr = c;
    epoll_ctl(t->epoll_fd, EPOLL_CTL_MOD, c->fd, &ev);
}

static void conn_close(conn_t *c) {
    if (c->state == CONN_STATE_CLOSED && c->fd < 0) return;
    thread_ctx_t *t = c->tctx;
    if (c->fd >= 0) {
        epoll_ctl(t->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        atomic_fetch_sub_explicit(&g_active_conns, 1, memory_order_relaxed);
    }
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->session) {
        nghttp2_session_del(c->session);
        c->session = NULL;
    }
    c->fd = -1;
    c->state = CONN_STATE_CLOSED;
}

static int conn_start(conn_t *c) {
    c->fd = create_tuned_socket();
    if (c->fd < 0) {
        atomic_fetch_add_explicit(&g_err_connect, 1, memory_order_relaxed);
        return -1;
    }

    int rc = connect(c->fd, (struct sockaddr *)&g_target_addr, g_target_addrlen);
    if (rc < 0 && errno != EINPROGRESS) {
        atomic_fetch_add_explicit(&g_err_connect, 1, memory_order_relaxed);
        close(c->fd);
        c->fd = -1;
        return -1;
    }

    if (g_cfg.is_tls) {
        c->ssl = SSL_new(c->tctx->ssl_ctx);
        SSL_set_fd(c->ssl, c->fd);
        SSL_set_connect_state(c->ssl);
        SSL_set_tlsext_host_name(c->ssl, g_cfg.host);
        c->state = CONN_STATE_CONNECTING; /* lanjut TLS handshake setelah connect */
    } else {
        c->state = CONN_STATE_CONNECTING;
    }

    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLIN | EPOLLET;
    ev.data.ptr = c;
    if (epoll_ctl(c->tctx->epoll_fd, EPOLL_CTL_ADD, c->fd, &ev) < 0) {
        close(c->fd);
        c->fd = -1;
        return -1;
    }

    atomic_fetch_add_explicit(&g_active_conns, 1, memory_order_relaxed);
    return 0;
}

static int setup_h2_session(conn_t *c) {
    if (nghttp2_session_client_new(&c->session, g_callbacks, c) != 0) {
        return -1;
    }

    nghttp2_settings_entry iv[] = {
        { NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 4096 },
        { NGHTTP2_SETTINGS_ENABLE_PUSH, 0 },
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100 },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65536 },
        { NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384 },
    };
    nghttp2_submit_settings(c->session, NGHTTP2_FLAG_NONE, iv,
                             sizeof(iv) / sizeof(iv[0]));
    nghttp2_session_send(c->session);

    c->state = CONN_STATE_H2_READY;
    atomic_fetch_add_explicit(&g_connected_conns, 1, memory_order_relaxed);
    return 0;
}

/* Coba lanjutkan TLS handshake yang sedang berjalan (non-blocking) */
static void progress_tls_handshake(conn_t *c) {
    int rc = SSL_do_handshake(c->ssl);
    if (rc == 1) {
        setup_h2_session(c);
        epoll_mod(c->tctx, c, EPOLLIN | EPOLLOUT);
        return;
    }
    int err = SSL_get_error(c->ssl, rc);
    if (err == SSL_ERROR_WANT_READ) {
        epoll_mod(c->tctx, c, EPOLLIN);
    } else if (err == SSL_ERROR_WANT_WRITE) {
        epoll_mod(c->tctx, c, EPOLLOUT);
    } else {
        atomic_fetch_add_explicit(&g_err_connect, 1, memory_order_relaxed);
        conn_close(c);
    }
}

/* Dipanggil saat socket connect() selesai (EPOLLOUT pertama kali) */
static void progress_connect(conn_t *c) {
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        atomic_fetch_add_explicit(&g_err_connect, 1, memory_order_relaxed);
        conn_close(c);
        return;
    }

    if (g_cfg.is_tls) {
        c->state = CONN_STATE_TLS_HANDSHAKE;
        progress_tls_handshake(c);
    } else {
        setup_h2_session(c);
        epoll_mod(c->tctx, c, EPOLLIN | EPOLLOUT);
    }
}

/* Flush semua frame nghttp2 yang pending ke socket */
static void h2_flush(conn_t *c) {
    if (!c->session) return;
    int rv = nghttp2_session_send(c->session);
    if (rv != 0) {
        conn_close(c);
    }
}

/* Baca data dari socket dan umpankan ke nghttp2 (edge-triggered: baca sampai EAGAIN) */
static void h2_read(conn_t *c) {
    if (!c->session) return;
    for (;;) {
        ssize_t n;
        if (c->ssl) {
            n = SSL_read(c->ssl, c->rbuf, POOL_BUF_SIZE);
            if (n <= 0) {
                int err = SSL_get_error(c->ssl, (int)n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) break;
                conn_close(c);
                return;
            }
        } else {
            n = read(c->fd, c->rbuf, POOL_BUF_SIZE);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                conn_close(c);
                return;
            }
            if (n == 0) { /* server menutup koneksi */
                conn_close(c);
                return;
            }
        }

        ssize_t proc = nghttp2_session_mem_recv(c->session, c->rbuf, (size_t)n);
        if (proc < 0) {
            conn_close(c);
            return;
        }
        if (n < POOL_BUF_SIZE) break;
    }
    h2_flush(c);
}

/* ==================== Thread worker utama ==================== */

static void set_cpu_affinity(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core % (int)sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static void *thread_main(void *arg) {
    thread_ctx_t *t = (thread_ctx_t *)arg;
    set_cpu_affinity(t->id);

    t->epoll_fd = epoll_create1(0);
    if (t->epoll_fd < 0) die("epoll_create1 gagal");

    thread_pool_init(t, t->nconns);

    for (int i = 0; i < t->nconns; i++) {
        conn_t *c = &t->conns[i];
        memset(c, 0, sizeof(*c));
        c->tctx = t;
        c->fd = -1;
        c->rbuf = t->pool_mem + (size_t)i * POOL_BUF_SIZE;
        conn_start(c);
    }

    struct epoll_event events[MAX_EVENTS];

    /* ---- Fase warmup: tunggu koneksi & handshake selesai ---- */
    struct timespec warmup_deadline;
    clock_gettime(CLOCK_MONOTONIC, &warmup_deadline);
    warmup_deadline.tv_sec += g_cfg.warmup;

    while (!g_warmup_done && g_running) {
        int nfds = epoll_wait(t->epoll_fd, events, MAX_EVENTS, 50);
        for (int i = 0; i < nfds; i++) {
            conn_t *c = (conn_t *)events[i].data.ptr;
            if (c->fd < 0) continue;
            if (c->state == CONN_STATE_CONNECTING) {
                progress_connect(c);
            } else if (c->state == CONN_STATE_TLS_HANDSHAKE) {
                progress_tls_handshake(c);
            } else if (c->state == CONN_STATE_H2_READY) {
                if (events[i].events & EPOLLIN) h2_read(c);
                if (events[i].events & EPOLLOUT) h2_flush(c);
            }
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (ts_diff_sec(warmup_deadline, now) >= 0) break;
    }

    /* ---- Fase test utama ---- */
    struct timespec end_deadline;
    clock_gettime(CLOCK_MONOTONIC, &end_deadline);
    end_deadline.tv_sec += g_cfg.duration;

    while (g_running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (ts_diff_sec(now, end_deadline) <= 0) break;

        int nfds = epoll_wait(t->epoll_fd, events, MAX_EVENTS, 10);
        for (int i = 0; i < nfds; i++) {
            conn_t *c = (conn_t *)events[i].data.ptr;
            if (c->fd < 0) continue;

            if (c->state == CONN_STATE_CONNECTING) {
                progress_connect(c);
                continue;
            }
            if (c->state == CONN_STATE_TLS_HANDSHAKE) {
                progress_tls_handshake(c);
                continue;
            }
            if (c->state == CONN_STATE_H2_READY) {
                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    conn_close(c);
                    continue;
                }
                if (events[i].events & EPOLLIN) h2_read(c);
                if (c->state == CONN_STATE_H2_READY && (events[i].events & EPOLLOUT)) {
                    h2_flush(c);
                }
            }
        }

        /* Batch submit: isi ulang stream yang kosong di semua koneksi ready */
        for (int i = 0; i < t->nconns; i++) {
            conn_t *c = &t->conns[i];
            if (c->state == CONN_STATE_H2_READY) {
                fill_request_batch(c);
                h2_flush(c);
            } else if (c->state == CONN_STATE_CLOSED) {
                /* reconnect supaya throughput tetap terjaga */
                conn_start(c);
            }
        }
    }

    /* ---- Graceful shutdown: GOAWAY lalu tutup ---- */
    for (int i = 0; i < t->nconns; i++) {
        conn_t *c = &t->conns[i];
        if (c->state == CONN_STATE_H2_READY && c->session) {
            nghttp2_session_terminate_session(c->session, NGHTTP2_NO_ERROR);
            nghttp2_session_send(c->session);
        }
        conn_close(c);
    }

    close(t->epoll_fd);
    return NULL;
}

/* ==================== Percentile & reporting ==================== */

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* Ambil snapshot gabungan ring buffer semua thread lalu hitung P50/P95/P99 (ms) */
static void compute_percentiles(double *p50, double *p95, double *p99) {
    int total_cap = 0;
    for (int i = 0; i < g_cfg.threads; i++) total_cap += g_threads[i].lat_cap;

    uint32_t *tmp = malloc((size_t)total_cap * sizeof(uint32_t));
    int n = 0;
    if (tmp) {
        for (int i = 0; i < g_cfg.threads; i++) {
            thread_ctx_t *t = &g_threads[i];
            int cnt = (int)t->lat_idx < t->lat_cap ? (int)t->lat_idx : t->lat_cap;
            for (int j = 0; j < cnt; j++) {
                tmp[n++] = t->lat_ring[j];
            }
        }
    }

    if (n == 0) {
        *p50 = *p95 = *p99 = 0.0;
        free(tmp);
        return;
    }

    qsort(tmp, (size_t)n, sizeof(uint32_t), cmp_u32);
    *p50 = tmp[(size_t)(n * 0.50)] / 1000.0;
    *p95 = tmp[(size_t)(n * 0.95 < n ? n * 0.95 : n - 1)] / 1000.0;
    *p99 = tmp[(size_t)(n * 0.99 < n ? n * 0.99 : n - 1)] / 1000.0;
    free(tmp);
}

static double get_cpu_usage_self(void) {
    /* Perkiraan kasar total CPU time proses (bukan per-thread akurat tanpa
     * membaca /proc/self/task/<tid>/stat per thread; cukup untuk indikator). */
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return 0.0;
    long utime = 0, stime = 0;
    char line[1024];
    if (fgets(line, sizeof(line), f)) {
        char *p = strrchr(line, ')');
        if (p) {
            p += 2;
            sscanf(p, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld",
                   &utime, &stime);
        }
    }
    fclose(f);
    long hz = sysconf(_SC_CLK_TCK);
    return (double)(utime + stime) / (hz > 0 ? hz : 100);
}

static long get_mem_usage_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    long kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
}

static void print_progress_bar(double frac, const char *label) {
    int width = 30;
    int filled = (int)(frac * width);
    if (filled > width) filled = width;
    printf("\r%s [", label);
    for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
    printf("] %3d%%", (int)(frac * 100));
    fflush(stdout);
}

/* ==================== Thread reporter (statistik real-time) ==================== */

static void *reporter_main(void *arg) {
    (void)arg;

    /* Tunggu warmup */
    struct timespec warmup_deadline;
    clock_gettime(CLOCK_MONOTONIC, &warmup_deadline);
    warmup_deadline.tv_sec += g_cfg.warmup;

    while (g_running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double left = ts_diff_sec(now, warmup_deadline);
        if (left <= 0) break;
        double frac = 1.0 - (left / (g_cfg.warmup > 0 ? g_cfg.warmup : 1));
        print_progress_bar(frac, "Warmup ");
        usleep(50000);
    }
    printf("\n");
    g_warmup_done = 1;

    clock_gettime(CLOCK_MONOTONIC, &g_test_start_ts);
    g_test_started = 1;

    unsigned long last_total = 0;
    struct timespec last_ts = g_test_start_ts;

    struct timespec deadline = g_test_start_ts;
    deadline.tv_sec += g_cfg.duration;

    printf("=== Test dimulai: %d thread, %d koneksi, target %d detik ===\n",
           g_cfg.threads, g_cfg.connections, g_cfg.duration);
    printf("%-8s %-12s %-10s %-10s %-8s %-8s %-8s %-6s %-8s\n",
           "T(s)", "RPS", "Success%", "Errors", "P50ms", "P95ms", "P99ms", "Conn", "MemMB");

    while (g_running) {
        usleep(STATS_INTERVAL_MS * 1000);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        unsigned long total = atomic_load_explicit(&g_total_requests, memory_order_relaxed);
        unsigned long success = atomic_load_explicit(&g_total_success, memory_order_relaxed);
        unsigned long errors = atomic_load_explicit(&g_total_errors, memory_order_relaxed);

        double dt = ts_diff_sec(last_ts, now);
        double rps = dt > 0 ? (double)(total - last_total) / dt : 0.0;

        double p50, p95, p99;
        compute_percentiles(&p50, &p95, &p99);

        double success_rate = total > 0 ? (100.0 * success / total) : 100.0;
        long mem_kb = get_mem_usage_kb();
        int conns = atomic_load_explicit(&g_active_conns, memory_order_relaxed);

        double elapsed = ts_diff_sec(g_test_start_ts, now);
        printf("\r%-8.1f %-12.0f %-10.2f %-10lu %-8.2f %-8.2f %-8.2f %-6d %-8.1f\n",
               elapsed, rps, success_rate, errors, p50, p95, p99, conns,
               mem_kb / 1024.0);
        fflush(stdout);

        pthread_mutex_lock(&g_history_lock);
        if (g_history_count < MAX_HISTORY_POINTS) {
            stats_point_t *sp = &g_history[g_history_count++];
            sp->t_sec = elapsed;
            sp->rps = (uint64_t)rps;
            sp->success = success;
            sp->errors = errors;
            sp->active_conns = conns;
            sp->p50 = p50; sp->p95 = p95; sp->p99 = p99;
        }
        pthread_mutex_unlock(&g_history_lock);

        last_total = total;
        last_ts = now;

        if (ts_diff_sec(now, deadline) <= 0) {
            g_running = 0;
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &g_test_end_ts);
    return NULL;
}

/* ==================== CSV output ==================== */

static void write_csv(void) {
    char fname[128];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(fname, sizeof(fname), "http2load_results_%Y%m%d_%H%M%S.csv", &tmv);

    FILE *f = fopen(fname, "w");
    if (!f) {
        fprintf(stderr, "Gagal menulis CSV: %s\n", strerror(errno));
        return;
    }

    fprintf(f, "elapsed_sec,rps,success,errors,active_connections,p50_ms,p95_ms,p99_ms\n");
    pthread_mutex_lock(&g_history_lock);
    for (int i = 0; i < g_history_count; i++) {
        stats_point_t *sp = &g_history[i];
        fprintf(f, "%.1f,%lu,%lu,%lu,%d,%.2f,%.2f,%.2f\n",
                sp->t_sec, (unsigned long)sp->rps, (unsigned long)sp->success,
                (unsigned long)sp->errors, sp->active_conns, sp->p50, sp->p95, sp->p99);
    }
    pthread_mutex_unlock(&g_history_lock);

    fprintf(f, "\n# Ringkasan akhir\n");
    unsigned long total = atomic_load_explicit(&g_total_requests, memory_order_relaxed);
    unsigned long success = atomic_load_explicit(&g_total_success, memory_order_relaxed);
    unsigned long errors = atomic_load_explicit(&g_total_errors, memory_order_relaxed);
    double dur = ts_diff_sec(g_test_start_ts, g_test_end_ts);
    fprintf(f, "# total_requests=%lu success=%lu errors=%lu duration_sec=%.2f avg_rps=%.0f\n",
            total, success, errors, dur, dur > 0 ? total / dur : 0.0);
    fprintf(f, "# err_connect=%lu err_stream=%lu err_http=%lu err_timeout=%lu bytes_received=%lu\n",
            (unsigned long)atomic_load_explicit(&g_err_connect, memory_order_relaxed),
            (unsigned long)atomic_load_explicit(&g_err_stream, memory_order_relaxed),
            (unsigned long)atomic_load_explicit(&g_err_http, memory_order_relaxed),
            (unsigned long)atomic_load_explicit(&g_err_timeout, memory_order_relaxed),
            (unsigned long)atomic_load_explicit(&g_bytes_received, memory_order_relaxed));

    fclose(f);
    printf("\nHasil disimpan ke: %s\n", fname);
}

/* ==================== main ==================== */

int main(int argc, char **argv) {
    parse_args(argc, argv);
    install_signal_handlers();
    resolve_target();
    init_nghttp2_callbacks();

    SSL_CTX *ssl_ctx = NULL;
    if (g_cfg.is_tls) {
        SSL_library_init();
        SSL_load_error_strings();
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
        /* Wire-format ALPN: len-prefixed string "h2" */
        static const unsigned char alpn_protos[] = { 2, 'h', '2' };
        SSL_CTX_set_alpn_protos(ssl_ctx, alpn_protos, sizeof(alpn_protos));
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL); /* load test: skip verifikasi cert */
    }

    g_history = calloc(MAX_HISTORY_POINTS, sizeof(stats_point_t));
    if (!g_history) die("gagal alokasi history buffer");

    printf("=== HTTP/2 Load Tester ===\n");
    printf("Target      : %s\n", g_cfg.url);
    printf("Threads     : %d\n", g_cfg.threads);
    printf("Connections : %d (%.1f per thread)\n", g_cfg.connections,
           (double)g_cfg.connections / g_cfg.threads);
    printf("Streams/conn: %d\n", g_cfg.streams_per_conn);
    printf("Batch size  : %d\n", g_cfg.batch_size);
    printf("Warmup      : %ds | Duration: %ds\n\n", g_cfg.warmup, g_cfg.duration);

    g_threads = calloc((size_t)g_cfg.threads, sizeof(thread_ctx_t));
    if (!g_threads) die("gagal alokasi thread context");

    int base_conns = g_cfg.connections / g_cfg.threads;
    int remainder  = g_cfg.connections % g_cfg.threads;

    for (int i = 0; i < g_cfg.threads; i++) {
        thread_ctx_t *t = &g_threads[i];
        t->id = i;
        t->ssl_ctx = ssl_ctx;
        t->nconns = base_conns + (i < remainder ? 1 : 0);
        t->conns = calloc((size_t)t->nconns, sizeof(conn_t));
        if (!t->conns) die("gagal alokasi conn array");
    }

    pthread_t reporter_tid;
    pthread_create(&reporter_tid, NULL, reporter_main, NULL);

    for (int i = 0; i < g_cfg.threads; i++) {
        pthread_create(&g_threads[i].thread, NULL, thread_main, &g_threads[i]);
    }

    for (int i = 0; i < g_cfg.threads; i++) {
        pthread_join(g_threads[i].thread, NULL);
    }
    g_running = 0;
    pthread_join(reporter_tid, NULL);

    /* ---- Ringkasan akhir ---- */
    unsigned long total = atomic_load_explicit(&g_total_requests, memory_order_relaxed);
    unsigned long success = atomic_load_explicit(&g_total_success, memory_order_relaxed);
    unsigned long errors = atomic_load_explicit(&g_total_errors, memory_order_relaxed);
    double dur = ts_diff_sec(g_test_start_ts, g_test_end_ts);
    double p50, p95, p99;
    compute_percentiles(&p50, &p95, &p99);

    printf("\n=== RINGKASAN ===\n");
    printf("Total request   : %lu\n", total);
    printf("Success          : %lu (%.2f%%)\n", success,
           total > 0 ? 100.0 * success / total : 0.0);
    printf("Errors           : %lu\n", errors);
    printf("  - connect      : %lu\n", (unsigned long)atomic_load_explicit(&g_err_connect, memory_order_relaxed));
    printf("  - stream/proto : %lu\n", (unsigned long)atomic_load_explicit(&g_err_stream, memory_order_relaxed));
    printf("  - http status  : %lu\n", (unsigned long)atomic_load_explicit(&g_err_http, memory_order_relaxed));
    printf("Durasi aktual    : %.2f detik\n", dur);
    printf("Rata-rata RPS    : %.0f\n", dur > 0 ? total / dur : 0.0);
    printf("Latency P50/P95/P99 (ms): %.2f / %.2f / %.2f\n", p50, p95, p99);
    printf("Total bytes recv : %lu\n", (unsigned long)atomic_load_explicit(&g_bytes_received, memory_order_relaxed));
    printf("CPU time (total) : %.1f detik\n", get_cpu_usage_self());
    printf("Memory RSS       : %.1f MB\n", get_mem_usage_kb() / 1024.0);

    write_csv();

    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    nghttp2_session_callbacks_del(g_callbacks);

    return 0;
}
