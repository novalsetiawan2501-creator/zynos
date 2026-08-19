/*
 * http2load.c — HTTP/2 load tester berbasis nghttp2 + OpenSSL
 *
 * Kompilasi:
 *   gcc -o http2load http2load.c -lnghttp2 -lpthread -lssl -lcrypto -Wall -O3 \
 *       -march=native -mtune=native -flto -funroll-loops -fomit-frame-pointer \
 *       -pipe -D_GNU_SOURCE -DNDEBUG
 *
 * CATATAN PENYESUAIAN DARI SPEK (dan alasannya) — dijelaskan juga di chat:
 *  1. "Connection: keep-alive" TIDAK dikirim. HTTP/2 (RFC 9113 §8.2.2) melarang
 *     connection-specific header field ini; server yang patuh akan RST_STREAM
 *     dengan PROTOCOL_ERROR jika header ini disertakan. PersistOEnsi koneksi di
 *     HTTP/2 memang implisit, jadi header ini tidak diperlukan.
 *  2. sendfile()/splice() tidak dipakai: keduanya adalah teknik zero-copy sisi
 *     SERVER untuk mengirim file besar, sedangkan tool ini adalah CLIENT yang
 *     hanya mengirim GET kecil dan membaca respons — tidak ada operand file
 *     descriptor->socket yang relevan. Sebagai gantinya, header request dikirim
 *     dengan NGHTTP2_NV_FLAG_NO_COPY_NAME/VALUE (nghttp2 langsung mereferensi
 *     buffer kita, tanpa malloc+memcpy per-request) dan I/O socket pakai buffer
 *     pool 64KB yang sudah dialokasikan di awal — ini padanan praktis "minim-copy"
 *     untuk beban kerja client HTTP/2.
 *  3. "Work stealing queue" disederhanakan jadi static partitioning: setiap
 *     thread memiliki set koneksi tetap sejak awal (dibagi rata di startup).
 *     Work-stealing deque yang benar (mis. algoritma Chase-Lev) tidak banyak
 *     berguna di sini karena unit kerjanya adalah *koneksi persisten* yang
 *     memang sudah paralel per-thread, bukan task pendek yang perlu di-rebalance.
 *  4. Verifikasi sertifikat TLS di-nonaktifkan secara default (SSL_VERIFY_NONE)
 *     karena tool ini untuk load-test server sendiri yang sering pakai
 *     sertifikat self-signed. Edit create_ssl_ctx() bila butuh verifikasi ketat.
 *  5. Statistik per-thread (histogram latency, counter) HANYA ditulis oleh
 *     thread pemiliknya (single-writer) dan dibaca "santai" (relaxed atomic /
 *     tanpa lock) oleh thread pelapor statistik — trade-off sengaja diambil demi
 *     performa (hindari lock/contention di hot path). Angka real-time karena itu
 *     approksimasi; angka final (setelah semua thread join, tanpa race) akurat.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdatomic.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <nghttp2/nghttp2.h>

/* ===================== KONSTANTA ===================== */

#define RBUF_SIZE            65536u   /* 64KB per buffer, sesuai spek memory pool */
#define MAX_EVENTS            256
#define LAT_FINE_BUCKETS     10000    /* 0..9999 us, resolusi 1us  */
#define LAT_COARSE_BUCKETS     990    /* 10ms..999ms, resolusi 1ms */
#define REQUEST_TIMEOUT_US 5000000ULL /* 5 detik dianggap macet -> RST_STREAM  */

typedef enum { PHASE_WARMUP = 1, PHASE_MEASURE = 2, PHASE_DRAIN = 3, PHASE_STOPPED = 4 } phase_t;
typedef enum { CONN_IDLE = 0, CONN_CONNECTING, CONN_TLS_HANDSHAKE, CONN_READY, CONN_ERROR, CONN_CLOSED } conn_state_t;
enum { ERR_CONNECT = 1, ERR_HTTP2, ERR_OTHER };

/* ===================== STRUKTUR DATA ===================== */

typedef struct {
    int is_https;
    char host[256];
    int port;
    char path[2048];
} url_t;

typedef struct {
    char url_str[2048];
    url_t url;
    int threads;
    int connections;
    int duration;
    int warmup;
    int streams_per_conn;
    int batch_size;
    char csv_path[512];
    struct sockaddr_storage addr;
    socklen_t addrlen;
} config_t;

typedef struct {
    _Atomic uint64_t sent;
    _Atomic uint64_t success;
    _Atomic uint64_t errors;
    _Atomic uint64_t bytes_received;
    _Atomic uint64_t connections_established;
    _Atomic uint64_t connections_active;
    _Atomic uint64_t reconnects;
    _Atomic uint64_t err_connect;
    _Atomic uint64_t err_timeout;
    _Atomic uint64_t err_http2;
    _Atomic uint64_t err_reset;
    _Atomic uint64_t err_other;
    _Atomic uint64_t cpu_time_us;      /* sampel CLOCK_THREAD_CPUTIME_ID, ditulis thread pemilik saja */
    uint64_t lat_fine[LAT_FINE_BUCKETS];   /* single-writer (thread pemilik) -> non-atomic, cepat  */
    uint64_t lat_coarse[LAT_COARSE_BUCKETS];
    uint64_t lat_overflow;
} thread_stats_t;

typedef struct stream_data {
    struct timespec start_time;
    int status_code;
    int32_t stream_id;
    int in_use;
    int timed_out;
    struct stream_data *next_free;
} stream_data_t;

struct worker_thread; /* fwd decl */

typedef struct connection {
    int fd;
    SSL *ssl;
    nghttp2_session *session;
    conn_state_t state;
    int active_streams;
    int counted_active;
    unsigned char *rbuf;                /* slice dari memory pool 64KB milik worker */
    const uint8_t *wpending;
    size_t wpending_len, wpending_off;
    stream_data_t *stream_pool;
    stream_data_t *stream_free_list;
    struct worker_thread *owner;
} connection_t;

typedef struct worker_thread {
    int id;
    int epoll_fd;
    pthread_t tid;
    int num_connections;
    connection_t *connections;
    unsigned char *mem_pool;            /* num_connections * RBUF_SIZE, dialokasikan sekali di awal */
    nghttp2_session_callbacks *callbacks;
    SSL_CTX *ssl_ctx;
    thread_stats_t stats;
    int stats_reset_done;
    /* bookkeeping dipakai HANYA oleh main thread untuk hitung %CPU, bukan oleh worker */
    uint64_t prev_cpu_us;
    struct timespec prev_cpu_wall;
    double last_cpu_pct;
} worker_thread_t;

typedef struct {
    uint64_t fine[LAT_FINE_BUCKETS];
    uint64_t coarse[LAT_COARSE_BUCKETS];
    uint64_t overflow;
    uint64_t total;
} merged_hist_t;

typedef struct {
    double t;
    uint64_t sent, success, errors;
    double p50, p95, p99;
    uint64_t active_conns;
} sample_t;

/* ===================== GLOBAL ===================== */

static config_t g_config;
static worker_thread_t *g_workers;
static volatile sig_atomic_t g_shutdown = 0;
static _Atomic int g_phase = PHASE_WARMUP;

static char g_authority[300];
static size_t g_authority_len;
static size_t g_path_len;

static uint64_t g_last_sent = 0;
static struct timespec g_last_tick;

/* ===================== PROTOTIPE ===================== */

static int parse_url(const char *url, url_t *out);
static void setup_static_headers(void);
static void print_usage(const char *prog);
static int parse_args(int argc, char **argv);
static void on_signal(int sig);
static void set_cpu_affinity(int thread_id);
static int create_nonblocking_socket(int family);
static void mod_epoll(connection_t *conn, uint32_t events);
static void init_stream_pool(connection_t *conn, int size);
static stream_data_t *stream_pool_alloc(connection_t *conn);
static void stream_pool_free(connection_t *conn, stream_data_t *sd);
static uint64_t timespec_diff_us(const struct timespec *a, const struct timespec *b);
static long elapsed_ms(const struct timespec *a, const struct timespec *b);
static int parse_status(const uint8_t *value, size_t len);
static nghttp2_nv make_nv(const char *name, size_t namelen, const char *value, size_t valuelen);
static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data);
static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags, int32_t stream_id,
                                        const uint8_t *data, size_t len, void *user_data);
static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data);
static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data);
static void teardown_conn_resources(connection_t *conn);
static void mark_conn_error(connection_t *conn, int err_type);
static void mark_conn_closed(connection_t *conn);
static int init_http2_session(connection_t *conn);
static void start_connect(connection_t *conn);
static void handle_connecting(connection_t *conn);
static void do_tls_handshake(connection_t *conn);
static void handle_read(connection_t *conn);
static void flush_send(connection_t *conn);
static void submit_new_request(worker_thread_t *wt, connection_t *conn);
static void sweep_timeouts(worker_thread_t *wt);
static void init_worker(worker_thread_t *wt);
static void *worker_main(void *arg);
static void reset_thread_stats(worker_thread_t *wt);
static void record_latency(thread_stats_t *st, uint64_t us);
static void merge_histograms(merged_hist_t *out);
static uint64_t percentile_us_bp(merged_hist_t *h, int bp);
static void sum_stats(uint64_t *sent, uint64_t *success, uint64_t *errors, uint64_t *bytes,
                       uint64_t *active_conns, uint64_t *reconnects,
                       uint64_t *ec, uint64_t *et, uint64_t *eh, uint64_t *er, uint64_t *eo);
static long get_mem_rss_kb(void);
static void update_cpu_pct(void);
static void print_progress_line(const char *phase_name, double elapsed, double total,
                                 uint64_t rps, double success_rate, uint64_t p50, uint64_t p95, uint64_t p99,
                                 uint64_t active_conns);
static void print_detail_block(uint64_t sent, uint64_t success, uint64_t errors, uint64_t bytes,
                                uint64_t reconnects, uint64_t ec, uint64_t et, uint64_t eh, uint64_t er, uint64_t eo);
static void tick(const char *phase_name, double elapsed, double total,
                  sample_t *samples, int *nsamples, int cap, int record_sample);
static void write_csv(const char *path, sample_t *samples, int nsamples, merged_hist_t *hist,
                       uint64_t total_sent, uint64_t total_success, uint64_t total_errors, double duration,
                       uint64_t ec, uint64_t et, uint64_t eh, uint64_t er, uint64_t eo);
static SSL_CTX *create_ssl_ctx(void);

#define LIT(s) (s), (sizeof(s) - 1)

/* ===================== UTIL WAKTU ===================== */

static uint64_t timespec_diff_us(const struct timespec *a, const struct timespec *b) {
    return (uint64_t)(b->tv_sec - a->tv_sec) * 1000000ULL +
           (uint64_t)((b->tv_nsec - a->tv_nsec) / 1000);
}

static long elapsed_ms(const struct timespec *a, const struct timespec *b) {
    return (long)(b->tv_sec - a->tv_sec) * 1000L + (b->tv_nsec - a->tv_nsec) / 1000000L;
}

/* ===================== URL & CLI ===================== */

static int parse_url(const char *url, url_t *out) {
    const char *p = url;
    memset(out, 0, sizeof(*out));
    if (strncmp(p, "https://", 8) == 0) { out->is_https = 1; p += 8; out->port = 443; }
    else if (strncmp(p, "http://", 7) == 0) { out->is_https = 0; p += 7; out->port = 80; }
    else return -1;

    const char *slash = strchr(p, '/');
    const char *hp_end = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(hp_end - p));

    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen == 0 || hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen); out->host[hlen] = 0;
        out->port = atoi(colon + 1);
        if (out->port <= 0) return -1;
    } else {
        size_t hlen = (size_t)(hp_end - p);
        if (hlen == 0 || hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen); out->host[hlen] = 0;
    }

    if (slash) snprintf(out->path, sizeof(out->path), "%s", slash);
    else snprintf(out->path, sizeof(out->path), "/");
    return 0;
}

static void setup_static_headers(void) {
    if ((g_config.url.is_https && g_config.url.port == 443) ||
        (!g_config.url.is_https && g_config.url.port == 80)) {
        snprintf(g_authority, sizeof(g_authority), "%s", g_config.url.host);
    } else {
        snprintf(g_authority, sizeof(g_authority), "%s:%d", g_config.url.host, g_config.url.port);
    }
    g_authority_len = strlen(g_authority);
    g_path_len = strlen(g_config.url.path);
}

static void print_usage(const char *prog) {
    printf("Penggunaan: %s --url <target> [opsi]\n\n", prog);
    printf("Opsi:\n");
    printf("  --url <url>              Target (wajib), contoh: https://host/path\n");
    printf("  --threads <n>            Jumlah thread (default: auto = jumlah core)\n");
    printf("  --connections <n>        Jumlah koneksi total (default: 100)\n");
    printf("  --duration <detik>       Lama pengukuran (default: 60)\n");
    printf("  --warmup <detik>         Lama warmup sebelum diukur (default: 5)\n");
    printf("  --streams-per-conn <n>   Concurrent streams HTTP/2 per koneksi (default: 100)\n");
    printf("  --batch-size <n>         Request disubmit per siklus event loop (default: 20)\n");
    printf("  --csv <path>             Path file CSV output (default: auto)\n");
    printf("  --help                   Tampilkan bantuan ini\n");
}

static int parse_args(int argc, char **argv) {
    static struct option long_opts[] = {
        {"url", required_argument, 0, 'u'},
        {"threads", required_argument, 0, 't'},
        {"connections", required_argument, 0, 'c'},
        {"duration", required_argument, 0, 'd'},
        {"warmup", required_argument, 0, 'w'},
        {"streams-per-conn", required_argument, 0, 's'},
        {"batch-size", required_argument, 0, 'b'},
        {"csv", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "u:t:c:d:w:s:b:o:h", long_opts, NULL)) != -1) {
        switch (c) {
            case 'u': snprintf(g_config.url_str, sizeof(g_config.url_str), "%s", optarg); break;
            case 't': g_config.threads = atoi(optarg); break;
            case 'c': g_config.connections = atoi(optarg); break;
            case 'd': g_config.duration = atoi(optarg); break;
            case 'w': g_config.warmup = atoi(optarg); break;
            case 's': g_config.streams_per_conn = atoi(optarg); break;
            case 'b': g_config.batch_size = atoi(optarg); break;
            case 'o': snprintf(g_config.csv_path, sizeof(g_config.csv_path), "%s", optarg); break;
            case 'h': default: return -1;
        }
    }
    return 0;
}

static void on_signal(int sig) { (void)sig; g_shutdown = 1; }

/* ===================== CPU AFFINITY & SOCKET ===================== */

static void set_cpu_affinity(int thread_id) {
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 0) return;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET((size_t)(thread_id % (int)ncpu), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static int create_nonblocking_socket(int family) {
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

    int bufsz = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));

#ifdef TCP_QUICKACK
    setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif
#ifdef SO_BUSY_POLL
    { int bp_us = 50; setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &bp_us, sizeof(bp_us)); /* best-effort */ }
#endif
    return fd;
}

static void mod_epoll(connection_t *conn, uint32_t events) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = conn;
    epoll_ctl(conn->owner->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

/* ===================== STREAM POOL (pre-allocated, single-writer per koneksi) ===================== */

static void init_stream_pool(connection_t *conn, int size) {
    conn->stream_pool = calloc((size_t)size, sizeof(stream_data_t));
    conn->stream_free_list = NULL;
    for (int i = 0; i < size; i++) {
        conn->stream_pool[i].next_free = conn->stream_free_list;
        conn->stream_free_list = &conn->stream_pool[i];
    }
}

static stream_data_t *stream_pool_alloc(connection_t *conn) {
    stream_data_t *sd = conn->stream_free_list;
    if (sd) {
        conn->stream_free_list = sd->next_free;
        sd->in_use = 1;
        sd->timed_out = 0;
        sd->stream_id = -1;
    }
    return sd;
}

static void stream_pool_free(connection_t *conn, stream_data_t *sd) {
    sd->in_use = 0;
    sd->next_free = conn->stream_free_list;
    conn->stream_free_list = sd;
}

/* ===================== HISTOGRAM LATENCY ===================== */

static void record_latency(thread_stats_t *st, uint64_t us) {
    if (us < LAT_FINE_BUCKETS) {
        st->lat_fine[us]++;
    } else if (us < 1000000ULL) {
        uint64_t idx = (us - LAT_FINE_BUCKETS) / 1000;
        if (idx >= LAT_COARSE_BUCKETS) idx = LAT_COARSE_BUCKETS - 1;
        st->lat_coarse[idx]++;
    } else {
        st->lat_overflow++;
    }
}

static void merge_histograms(merged_hist_t *out) {
    memset(out, 0, sizeof(*out));
    for (int t = 0; t < g_config.threads; t++) {
        thread_stats_t *st = &g_workers[t].stats;
        for (int i = 0; i < LAT_FINE_BUCKETS; i++) out->fine[i] += st->lat_fine[i];
        for (int i = 0; i < LAT_COARSE_BUCKETS; i++) out->coarse[i] += st->lat_coarse[i];
        out->overflow += st->lat_overflow;
    }
    for (int i = 0; i < LAT_FINE_BUCKETS; i++) out->total += out->fine[i];
    for (int i = 0; i < LAT_COARSE_BUCKETS; i++) out->total += out->coarse[i];
    out->total += out->overflow;
}

/* bp = basis points dari 10000 (5000=P50, 9500=P95, 9900=P99). Integer murni -> tidak butuh -lm. */
static uint64_t percentile_us_bp(merged_hist_t *h, int bp) {
    if (h->total == 0) return 0;
    uint64_t num = h->total * (uint64_t)bp;
    uint64_t rank = num / 10000;
    if (num % 10000 != 0) rank++;
    if (rank < 1) rank = 1;

    uint64_t cum = 0;
    for (int i = 0; i < LAT_FINE_BUCKETS; i++) {
        cum += h->fine[i];
        if (cum >= rank) return (uint64_t)i;
    }
    for (int i = 0; i < LAT_COARSE_BUCKETS; i++) {
        cum += h->coarse[i];
        if (cum >= rank) return (uint64_t)(LAT_FINE_BUCKETS + i * 1000);
    }
    return 1000000ULL;
}

/* ===================== NGHTTP2 NV HELPER ===================== */

static nghttp2_nv make_nv(const char *name, size_t namelen, const char *value, size_t valuelen) {
    nghttp2_nv nv;
    nv.name = (uint8_t *)(uintptr_t)name;
    nv.value = (uint8_t *)(uintptr_t)value;
    nv.namelen = namelen;
    nv.valuelen = valuelen;
    /* NO_COPY aman krn semua sumber (literal, g_authority, g_config.url.path) hidup
       sepanjang umur program -> ini yg menggantikan peran zero-copy sendfile/splice
       yg tak relevan utk client GET (lihat catatan di kepala file). */
    nv.flags = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    return nv;
}

static int parse_status(const uint8_t *value, size_t len) {
    int v = 0;
    for (size_t i = 0; i < len && i < 3; i++) {
        if (value[i] < '0' || value[i] > '9') break;
        v = v * 10 + (int)(value[i] - '0');
    }
    return v;
}

/* ===================== CALLBACK NGHTTP2 ===================== */

static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
    (void)user_data; (void)flags;
    if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
        if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
            stream_data_t *sd = (stream_data_t *)nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
            if (sd) sd->status_code = parse_status(value, valuelen);
        }
    }
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags, int32_t stream_id,
                                        const uint8_t *data, size_t len, void *user_data) {
    (void)session; (void)flags; (void)stream_id; (void)data;
    connection_t *conn = (connection_t *)user_data;
    atomic_fetch_add_explicit(&conn->owner->stats.bytes_received, len, memory_order_relaxed);
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data) {
    connection_t *conn = (connection_t *)user_data;
    worker_thread_t *wt = conn->owner;
    stream_data_t *sd = (stream_data_t *)nghttp2_session_get_stream_user_data(session, stream_id);

    if (conn->active_streams > 0) conn->active_streams--;

    if (sd) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t lat_us = timespec_diff_us(&sd->start_time, &now);
        record_latency(&wt->stats, lat_us);

        if (sd->timed_out) {
            atomic_fetch_add_explicit(&wt->stats.errors, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&wt->stats.err_timeout, 1, memory_order_relaxed);
        } else if (error_code == NGHTTP2_NO_ERROR && sd->status_code >= 200 && sd->status_code < 400) {
            atomic_fetch_add_explicit(&wt->stats.success, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&wt->stats.errors, 1, memory_order_relaxed);
            if (error_code != NGHTTP2_NO_ERROR)
                atomic_fetch_add_explicit(&wt->stats.err_reset, 1, memory_order_relaxed);
            else
                atomic_fetch_add_explicit(&wt->stats.err_other, 1, memory_order_relaxed);
        }
        stream_pool_free(conn, sd);
    }
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    (void)session;
    if (frame->hd.type == NGHTTP2_GOAWAY) {
        connection_t *conn = (connection_t *)user_data;
        atomic_fetch_add_explicit(&conn->owner->stats.err_reset, 1, memory_order_relaxed);
    }
    return 0;
}

/* ===================== LIFECYCLE KONEKSI ===================== */

static void teardown_conn_resources(connection_t *conn) {
    if (conn->fd >= 0) {
        epoll_ctl(conn->owner->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
        close(conn->fd);
        conn->fd = -1;
    }
    if (conn->ssl) { SSL_free(conn->ssl); conn->ssl = NULL; }
    if (conn->session) { nghttp2_session_del(conn->session); conn->session = NULL; }
    if (conn->counted_active) {
        atomic_fetch_sub_explicit(&conn->owner->stats.connections_active, 1, memory_order_relaxed);
        conn->counted_active = 0;
    }
    conn->active_streams = 0;
    conn->wpending_len = 0;
    conn->wpending_off = 0;

    conn->stream_free_list = NULL;
    for (int i = 0; i < g_config.streams_per_conn; i++) {
        conn->stream_pool[i].in_use = 0;
        conn->stream_pool[i].next_free = conn->stream_free_list;
        conn->stream_free_list = &conn->stream_pool[i];
    }
}

static void mark_conn_error(connection_t *conn, int err_type) {
    if (conn->state != CONN_ERROR) {
        switch (err_type) {
            case ERR_CONNECT: atomic_fetch_add_explicit(&conn->owner->stats.err_connect, 1, memory_order_relaxed); break;
            case ERR_HTTP2:   atomic_fetch_add_explicit(&conn->owner->stats.err_http2, 1, memory_order_relaxed); break;
            default:          atomic_fetch_add_explicit(&conn->owner->stats.err_other, 1, memory_order_relaxed); break;
        }
    }
    conn->state = CONN_ERROR;
    teardown_conn_resources(conn);
}

static void mark_conn_closed(connection_t *conn) {
    conn->state = CONN_CLOSED;
    teardown_conn_resources(conn);
}

static int init_http2_session(connection_t *conn) {
    if (nghttp2_session_client_new(&conn->session, conn->owner->callbacks, conn) != 0) return -1;

    nghttp2_settings_entry iv[] = {
        {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 4096},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, (uint32_t)g_config.streams_per_conn},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65536},
        {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384},
    };
    nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, iv, 5);

    atomic_fetch_add_explicit(&conn->owner->stats.connections_established, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&conn->owner->stats.connections_active, 1, memory_order_relaxed);
    conn->counted_active = 1;
    return 0;
}

static void start_connect(connection_t *conn) {
    int fd = create_nonblocking_socket(g_config.addr.ss_family);
    if (fd < 0) {
        atomic_fetch_add_explicit(&conn->owner->stats.err_connect, 1, memory_order_relaxed);
        conn->state = CONN_ERROR;
        return;
    }
    conn->fd = fd;
    int rc = connect(fd, (struct sockaddr *)&g_config.addr, g_config.addrlen);
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        conn->fd = -1;
        atomic_fetch_add_explicit(&conn->owner->stats.err_connect, 1, memory_order_relaxed);
        conn->state = CONN_ERROR;
        return;
    }
    conn->state = CONN_CONNECTING;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = conn;
    epoll_ctl(conn->owner->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static void handle_connecting(connection_t *conn) {
    int err = 0; socklen_t elen = sizeof(err);
    if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
        mark_conn_error(conn, ERR_CONNECT);
        return;
    }
    if (g_config.url.is_https) {
        conn->ssl = SSL_new(conn->owner->ssl_ctx);
        if (!conn->ssl) { mark_conn_error(conn, ERR_CONNECT); return; }
        SSL_set_fd(conn->ssl, conn->fd);
        SSL_set_connect_state(conn->ssl);
        SSL_set_tlsext_host_name(conn->ssl, g_config.url.host);
        conn->state = CONN_TLS_HANDSHAKE;
        do_tls_handshake(conn);
    } else {
        if (init_http2_session(conn) != 0) { mark_conn_error(conn, ERR_HTTP2); return; }
        conn->state = CONN_READY;
        mod_epoll(conn, EPOLLIN | EPOLLOUT | EPOLLET);
        flush_send(conn);
    }
}

static void do_tls_handshake(connection_t *conn) {
    int rc = SSL_do_handshake(conn->ssl);
    if (rc == 1) {
        const unsigned char *alpn = NULL; unsigned int alpnlen = 0;
        SSL_get0_alpn_selected(conn->ssl, &alpn, &alpnlen);
        if (!alpn || alpnlen != 2 || alpn[0] != 'h' || alpn[1] != '2') {
            mark_conn_error(conn, ERR_HTTP2);
            return;
        }
        if (init_http2_session(conn) != 0) { mark_conn_error(conn, ERR_HTTP2); return; }
        conn->state = CONN_READY;
        mod_epoll(conn, EPOLLIN | EPOLLOUT | EPOLLET);
        flush_send(conn);
        return;
    }
    int err = SSL_get_error(conn->ssl, rc);
    if (err == SSL_ERROR_WANT_READ) mod_epoll(conn, EPOLLIN | EPOLLET);
    else if (err == SSL_ERROR_WANT_WRITE) mod_epoll(conn, EPOLLOUT | EPOLLET);
    else mark_conn_error(conn, ERR_CONNECT);
}

static void handle_read(connection_t *conn) {
    for (;;) {
        ssize_t n;
        if (g_config.url.is_https) {
            n = SSL_read(conn->ssl, conn->rbuf, RBUF_SIZE);
            if (n <= 0) {
                int err = SSL_get_error(conn->ssl, (int)n);
                if (err == SSL_ERROR_WANT_READ) return;
                if (err == SSL_ERROR_WANT_WRITE) { mod_epoll(conn, EPOLLIN | EPOLLOUT | EPOLLET); return; }
                if (err == SSL_ERROR_ZERO_RETURN) { mark_conn_closed(conn); return; }
                mark_conn_error(conn, ERR_OTHER);
                return;
            }
        } else {
            n = recv(conn->fd, conn->rbuf, RBUF_SIZE, 0);
            if (n == 0) { mark_conn_closed(conn); return; }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR) continue;
                mark_conn_error(conn, ERR_OTHER);
                return;
            }
        }
        ssize_t rv = nghttp2_session_mem_recv(conn->session, conn->rbuf, (size_t)n);
        if (rv < 0) { mark_conn_error(conn, ERR_HTTP2); return; }
        if (conn->state != CONN_READY) return;
    }
}

static void flush_send(connection_t *conn) {
    for (;;) {
        if (conn->wpending_len == 0) {
            ssize_t n = nghttp2_session_mem_send(conn->session, &conn->wpending);
            if (n < 0) { mark_conn_error(conn, ERR_HTTP2); return; }
            if (n == 0) { mod_epoll(conn, EPOLLIN | EPOLLET); return; }
            conn->wpending_len = (size_t)n;
            conn->wpending_off = 0;
        }

        ssize_t sent;
        if (g_config.url.is_https) {
            sent = SSL_write(conn->ssl, conn->wpending + conn->wpending_off,
                              (int)(conn->wpending_len - conn->wpending_off));
            if (sent <= 0) {
                int err = SSL_get_error(conn->ssl, (int)sent);
                if (err == SSL_ERROR_WANT_WRITE) { mod_epoll(conn, EPOLLIN | EPOLLOUT | EPOLLET); return; }
                if (err == SSL_ERROR_WANT_READ) { mod_epoll(conn, EPOLLIN | EPOLLET); return; }
                mark_conn_error(conn, ERR_OTHER);
                return;
            }
        } else {
            sent = send(conn->fd, conn->wpending + conn->wpending_off,
                        conn->wpending_len - conn->wpending_off, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) { mod_epoll(conn, EPOLLIN | EPOLLOUT | EPOLLET); return; }
                if (errno == EINTR) continue;
                mark_conn_error(conn, ERR_OTHER);
                return;
            }
        }
        conn->wpending_off += (size_t)sent;
        if (conn->wpending_off >= conn->wpending_len) {
            conn->wpending_len = 0;
            conn->wpending_off = 0;
        } else {
            mod_epoll(conn, EPOLLIN | EPOLLOUT | EPOLLET);
            return;
        }
    }
}

static void submit_new_request(worker_thread_t *wt, connection_t *conn) {
    stream_data_t *sd = stream_pool_alloc(conn);
    if (!sd) return; /* pool penuh utk siklus ini, coba lagi siklus berikutnya */
    clock_gettime(CLOCK_MONOTONIC, &sd->start_time);
    sd->status_code = 0;

    nghttp2_nv hdrs[7];
    hdrs[0] = make_nv(LIT(":method"), LIT("GET"));
    hdrs[1] = make_nv(LIT(":scheme"), g_config.url.is_https ? "https" : "http",
                       g_config.url.is_https ? (size_t)5 : (size_t)4);
    hdrs[2] = make_nv(LIT(":authority"), g_authority, g_authority_len);
    hdrs[3] = make_nv(LIT(":path"), g_config.url.path, g_path_len);
    hdrs[4] = make_nv(LIT("user-agent"), LIT("Mozilla/5.0 (compatible; ZANGXX/1.0)"));
    hdrs[5] = make_nv(LIT("accept"), LIT("text/html,application/xhtml+xml"));
    hdrs[6] = make_nv(LIT("accept-encoding"), LIT("gzip, deflate, br"));
    /* "Connection: keep-alive" sengaja tidak dikirim - lihat catatan di kepala file. */

    int32_t sid = nghttp2_submit_request(conn->session, NULL, hdrs, 7, NULL, sd);
    if (sid < 0) {
        stream_pool_free(conn, sd);
        atomic_fetch_add_explicit(&wt->stats.err_other, 1, memory_order_relaxed);
        return;
    }
    sd->stream_id = sid;
    conn->active_streams++;
    atomic_fetch_add_explicit(&wt->stats.sent, 1, memory_order_relaxed);
}

static void sweep_timeouts(worker_thread_t *wt) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < wt->num_connections; i++) {
        connection_t *conn = &wt->connections[i];
        if (conn->state != CONN_READY) continue;
        for (int j = 0; j < g_config.streams_per_conn; j++) {
            stream_data_t *sd = &conn->stream_pool[j];
            if (sd->in_use && !sd->timed_out) {
                uint64_t age = timespec_diff_us(&sd->start_time, &now);
                if (age > REQUEST_TIMEOUT_US) {
                    sd->timed_out = 1;
                    nghttp2_submit_rst_stream(conn->session, NGHTTP2_FLAG_NONE, sd->stream_id, NGHTTP2_CANCEL);
                }
            }
        }
    }
}

/* ===================== WORKER THREAD ===================== */

static void reset_thread_stats(worker_thread_t *wt) {
    /* Hanya dipanggil oleh thread pemilik saat transisi WARMUP->MEASURE. */
    memset(&wt->stats, 0, sizeof(wt->stats));
}

static void init_worker(worker_thread_t *wt) {
    wt->epoll_fd = epoll_create1(0);
    wt->connections = calloc((size_t)wt->num_connections, sizeof(connection_t));
    wt->mem_pool = aligned_alloc(4096, (size_t)wt->num_connections * RBUF_SIZE);
    if (!wt->mem_pool) wt->mem_pool = malloc((size_t)wt->num_connections * RBUF_SIZE);

    nghttp2_session_callbacks_new(&wt->callbacks);
    nghttp2_session_callbacks_set_on_header_callback(wt->callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(wt->callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(wt->callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(wt->callbacks, on_frame_recv_callback);

    for (int i = 0; i < wt->num_connections; i++) {
        connection_t *conn = &wt->connections[i];
        conn->fd = -1;
        conn->owner = wt;
        conn->rbuf = wt->mem_pool + (size_t)i * RBUF_SIZE;
        conn->state = CONN_IDLE;
        init_stream_pool(conn, g_config.streams_per_conn);
    }
    for (int i = 0; i < wt->num_connections; i++) start_connect(&wt->connections[i]);
}

static void *worker_main(void *arg) {
    worker_thread_t *wt = (worker_thread_t *)arg;
    set_cpu_affinity(wt->id);
    init_worker(wt);

    struct epoll_event events[MAX_EVENTS];
    struct timespec last_cpu_sample, last_sweep, now;
    clock_gettime(CLOCK_MONOTONIC, &last_cpu_sample);
    last_sweep = last_cpu_sample;

    while (!g_shutdown) {
        int phase = atomic_load_explicit(&g_phase, memory_order_relaxed);
        if (phase == PHASE_STOPPED) break;

        int n = epoll_wait(wt->epoll_fd, events, MAX_EVENTS, 10);
        for (int i = 0; i < n; i++) {
            connection_t *conn = (connection_t *)events[i].data.ptr;
            uint32_t evs = events[i].events;
            if (conn->state == CONN_CONNECTING) {
                handle_connecting(conn);
            } else if (conn->state == CONN_TLS_HANDSHAKE) {
                do_tls_handshake(conn);
            } else if (conn->state == CONN_READY) {
                if (evs & (EPOLLHUP | EPOLLERR)) { mark_conn_error(conn, ERR_OTHER); continue; }
                if (evs & EPOLLIN) handle_read(conn);
                if (conn->state == CONN_READY && (evs & EPOLLOUT)) flush_send(conn);
            }
        }

        if (phase == PHASE_MEASURE && !wt->stats_reset_done) {
            reset_thread_stats(wt);
            wt->stats_reset_done = 1;
        }

        /* submit batch request + reconnect koneksi yg mati, hanya saat WARMUP/MEASURE */
        if (phase == PHASE_WARMUP || phase == PHASE_MEASURE) {
            for (int i = 0; i < wt->num_connections; i++) {
                connection_t *conn = &wt->connections[i];
                if (conn->state == CONN_ERROR || conn->state == CONN_CLOSED) {
                    atomic_fetch_add_explicit(&wt->stats.reconnects, 1, memory_order_relaxed);
                    start_connect(conn);
                    continue;
                }
                if (conn->state != CONN_READY) continue;
                int batch = 0;
                while (conn->active_streams < g_config.streams_per_conn && batch < g_config.batch_size) {
                    submit_new_request(wt, conn);
                    batch++;
                }
            }
        }

        for (int i = 0; i < wt->num_connections; i++) {
            connection_t *conn = &wt->connections[i];
            if (conn->state == CONN_READY) flush_send(conn);
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_ms(&last_cpu_sample, &now) >= 20) {
            struct timespec cpu;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu);
            uint64_t us = (uint64_t)cpu.tv_sec * 1000000ULL + (uint64_t)cpu.tv_nsec / 1000ULL;
            atomic_store_explicit(&wt->stats.cpu_time_us, us, memory_order_relaxed);
            last_cpu_sample = now;
        }
        if (elapsed_ms(&last_sweep, &now) >= 500) {
            sweep_timeouts(wt);
            last_sweep = now;
        }
    }

    for (int i = 0; i < wt->num_connections; i++) {
        teardown_conn_resources(&wt->connections[i]);
        free(wt->connections[i].stream_pool);
    }
    if (wt->callbacks) nghttp2_session_callbacks_del(wt->callbacks);
    free(wt->connections);
    free(wt->mem_pool);
    return NULL;
}

/* ===================== STATISTIK / TAMPILAN ===================== */

static void sum_stats(uint64_t *sent, uint64_t *success, uint64_t *errors, uint64_t *bytes,
                       uint64_t *active_conns, uint64_t *reconnects,
                       uint64_t *ec, uint64_t *et, uint64_t *eh, uint64_t *er, uint64_t *eo) {
    *sent = *success = *errors = *bytes = *active_conns = *reconnects = 0;
    *ec = *et = *eh = *er = *eo = 0;
    for (int i = 0; i < g_config.threads; i++) {
        thread_stats_t *st = &g_workers[i].stats;
        *sent += atomic_load_explicit(&st->sent, memory_order_relaxed);
        *success += atomic_load_explicit(&st->success, memory_order_relaxed);
        *errors += atomic_load_explicit(&st->errors, memory_order_relaxed);
        *bytes += atomic_load_explicit(&st->bytes_received, memory_order_relaxed);
        *active_conns += atomic_load_explicit(&st->connections_active, memory_order_relaxed);
        *reconnects += atomic_load_explicit(&st->reconnects, memory_order_relaxed);
        *ec += atomic_load_explicit(&st->err_connect, memory_order_relaxed);
        *et += atomic_load_explicit(&st->err_timeout, memory_order_relaxed);
        *eh += atomic_load_explicit(&st->err_http2, memory_order_relaxed);
        *er += atomic_load_explicit(&st->err_reset, memory_order_relaxed);
        *eo += atomic_load_explicit(&st->err_other, memory_order_relaxed);
    }
}

static long get_mem_rss_kb(void) {
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

static void update_cpu_pct(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < g_config.threads; i++) {
        worker_thread_t *wt = &g_workers[i];
        uint64_t cur = atomic_load_explicit(&wt->stats.cpu_time_us, memory_order_relaxed);
        if (wt->prev_cpu_us > 0) {
            long wall_us = (long)timespec_diff_us(&wt->prev_cpu_wall, &now);
            if (wall_us > 0) wt->last_cpu_pct = 100.0 * (double)(cur - wt->prev_cpu_us) / (double)wall_us;
        }
        wt->prev_cpu_us = cur;
        wt->prev_cpu_wall = now;
    }
}

static void print_progress_line(const char *phase_name, double elapsed, double total,
                                 uint64_t rps, double success_rate, uint64_t p50, uint64_t p95, uint64_t p99,
                                 uint64_t active_conns) {
    const int width = 30;
    double frac = total > 0 ? elapsed / total : 0;
    if (frac > 1) frac = 1; if (frac < 0) frac = 0;
    int filled = (int)(frac * width);
    char bar[width + 1];
    int i = 0;
    for (; i < filled && i < width; i++) bar[i] = '#';
    for (; i < width; i++) bar[i] = '-';
    bar[width] = 0;
    printf("\r[%s] %-7s %5.1f%% | RPS:%9llu | OK:%5.1f%% | P50:%6llu P95:%6llu P99:%6llu us | Conn:%4llu   ",
           bar, phase_name, frac * 100.0,
           (unsigned long long)rps, success_rate,
           (unsigned long long)p50, (unsigned long long)p95, (unsigned long long)p99,
           (unsigned long long)active_conns);
    fflush(stdout);
}

static void print_detail_block(uint64_t sent, uint64_t success, uint64_t errors, uint64_t bytes,
                                uint64_t reconnects, uint64_t ec, uint64_t et, uint64_t eh, uint64_t er, uint64_t eo) {
    long rss = get_mem_rss_kb();
    (void)success;
    printf("\n  Total: sent=%llu err=%llu bytes=%.2fMB reconnects=%llu | RSS=%ldMB\n",
           (unsigned long long)sent, (unsigned long long)errors,
           bytes / (1024.0 * 1024.0), (unsigned long long)reconnects,
           rss > 0 ? rss / 1024 : -1);
    printf("  Errors: connect=%llu timeout=%llu http2=%llu reset/goaway=%llu lainnya=%llu\n",
           (unsigned long long)ec, (unsigned long long)et, (unsigned long long)eh,
           (unsigned long long)er, (unsigned long long)eo);
    printf("  CPU/thread: ");
    for (int i = 0; i < g_config.threads; i++) printf("T%d=%.0f%% ", i, g_workers[i].last_cpu_pct);
    printf("\n");
}

static void tick(const char *phase_name, double elapsed, double total,
                  sample_t *samples, int *nsamples, int cap, int record_sample) {
    uint64_t sent, success, errors, bytes, active_conns, reconnects, ec, et, eh, er, eo;
    sum_stats(&sent, &success, &errors, &bytes, &active_conns, &reconnects, &ec, &et, &eh, &er, &eo);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t rps = 0;
    if (g_last_tick.tv_sec != 0 || g_last_tick.tv_nsec != 0) {
        uint64_t dt_us = timespec_diff_us(&g_last_tick, &now);
        if (dt_us > 0) rps = (uint64_t)(((double)(sent - g_last_sent)) * 1000000.0 / (double)dt_us);
    }
    g_last_sent = sent;
    g_last_tick = now;

    merged_hist_t hist;
    merge_histograms(&hist);
    uint64_t p50 = percentile_us_bp(&hist, 5000);
    uint64_t p95 = percentile_us_bp(&hist, 9500);
    uint64_t p99 = percentile_us_bp(&hist, 9900);
    double success_rate = sent > 0 ? 100.0 * (double)success / (double)sent : 0.0;

    print_progress_line(phase_name, elapsed, total, rps, success_rate, p50, p95, p99, active_conns);
    update_cpu_pct();

    static int tick_count = 0;
    tick_count++;
    if (tick_count % 10 == 0) {
        print_detail_block(sent, success, errors, bytes, reconnects, ec, et, eh, er, eo);
    }

    if (record_sample && samples && *nsamples < cap) {
        sample_t *s = &samples[*nsamples];
        s->t = elapsed; s->sent = sent; s->success = success; s->errors = errors;
        s->p50 = (double)p50; s->p95 = (double)p95; s->p99 = (double)p99;
        s->active_conns = active_conns;
        (*nsamples)++;
    }
}

static void write_csv(const char *path, sample_t *samples, int nsamples, merged_hist_t *hist,
                       uint64_t total_sent, uint64_t total_success, uint64_t total_errors, double duration,
                       uint64_t ec, uint64_t et, uint64_t eh, uint64_t er, uint64_t eo) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "Warning: gagal menulis CSV ke %s: %s\n", path, strerror(errno)); return; }

    fprintf(f, "elapsed_s,total_sent,total_success,total_errors,p50_us,p95_us,p99_us,active_connections\n");
    for (int i = 0; i < nsamples; i++) {
        sample_t *s = &samples[i];
        fprintf(f, "%.1f,%llu,%llu,%llu,%.0f,%.0f,%.0f,%llu\n",
                s->t, (unsigned long long)s->sent, (unsigned long long)s->success, (unsigned long long)s->errors,
                s->p50, s->p95, s->p99, (unsigned long long)s->active_conns);
    }

    fprintf(f, "\n# SUMMARY\nmetric,value\n");
    fprintf(f, "duration_seconds,%.2f\n", duration);
    fprintf(f, "total_requests,%llu\n", (unsigned long long)total_sent);
    fprintf(f, "successful_requests,%llu\n", (unsigned long long)total_success);
    fprintf(f, "failed_requests,%llu\n", (unsigned long long)total_errors);
    fprintf(f, "success_rate_pct,%.2f\n", total_sent ? 100.0 * (double)total_success / (double)total_sent : 0.0);
    fprintf(f, "avg_rps,%.1f\n", duration > 0 ? (double)total_sent / duration : 0.0);
    fprintf(f, "p50_latency_us,%llu\n", (unsigned long long)percentile_us_bp(hist, 5000));
    fprintf(f, "p95_latency_us,%llu\n", (unsigned long long)percentile_us_bp(hist, 9500));
    fprintf(f, "p99_latency_us,%llu\n", (unsigned long long)percentile_us_bp(hist, 9900));
    fprintf(f, "err_connect,%llu\n", (unsigned long long)ec);
    fprintf(f, "err_timeout,%llu\n", (unsigned long long)et);
    fprintf(f, "err_http2,%llu\n", (unsigned long long)eh);
    fprintf(f, "err_reset_goaway,%llu\n", (unsigned long long)er);
    fprintf(f, "err_other,%llu\n", (unsigned long long)eo);
    fclose(f);
}

/* ===================== TLS CONTEXT ===================== */

static SSL_CTX *create_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { fprintf(stderr, "Gagal membuat SSL_CTX\n"); ERR_print_errors_fp(stderr); exit(1); }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    /* Default: skip verifikasi sertifikat (server load-test sering self-signed).
       Ubah ke SSL_VERIFY_PEER + set CA path bila target pakai cert publik/CA sendiri. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    static const unsigned char alpn_protos[] = { 2, 'h', '2' };
    SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos));
    return ctx;
}

/* ===================== MAIN ===================== */

int main(int argc, char **argv) {
    memset(&g_config, 0, sizeof(g_config));
    g_config.threads = 0;
    g_config.connections = 100;
    g_config.duration = 60;
    g_config.warmup = 5;
    g_config.streams_per_conn = 100;
    g_config.batch_size = 20;

    if (parse_args(argc, argv) != 0) { print_usage(argv[0]); return 1; }
    if (g_config.url_str[0] == 0) { fprintf(stderr, "Error: --url wajib diisi\n"); print_usage(argv[0]); return 1; }
    if (parse_url(g_config.url_str, &g_config.url) != 0) {
        fprintf(stderr, "Error: URL tidak valid: %s (gunakan http:// atau https://)\n", g_config.url_str);
        return 1;
    }

    if (g_config.threads <= 0) {
        long nc = sysconf(_SC_NPROCESSORS_ONLN);
        g_config.threads = nc > 0 ? (int)nc : 4;
    }
    if (g_config.threads > 64) g_config.threads = 64;
    if (g_config.connections < g_config.threads) g_config.connections = g_config.threads;
    if (g_config.streams_per_conn < 1) g_config.streams_per_conn = 1;
    if (g_config.batch_size < 1) g_config.batch_size = 1;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", g_config.url.port);
    int gai = getaddrinfo(g_config.url.host, portstr, &hints, &res);
    if (gai != 0 || !res) {
        fprintf(stderr, "Error: gagal resolve DNS untuk %s: %s\n", g_config.url.host, gai_strerror(gai));
        return 1;
    }
    memcpy(&g_config.addr, res->ai_addr, res->ai_addrlen);
    g_config.addrlen = res->ai_addrlen;
    freeaddrinfo(res);

    setup_static_headers();

    if (g_config.csv_path[0] == 0) {
        time_t tnow = time(NULL);
        snprintf(g_config.csv_path, sizeof(g_config.csv_path), "http2load_results_%ld.csv", (long)tnow);
    }

    SSL_CTX *ssl_ctx = NULL;
    if (g_config.url.is_https) ssl_ctx = create_ssl_ctx();

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    g_workers = calloc((size_t)g_config.threads, sizeof(worker_thread_t));
    int base = g_config.connections / g_config.threads;
    int rem = g_config.connections % g_config.threads;
    for (int i = 0; i < g_config.threads; i++) {
        g_workers[i].id = i;
        g_workers[i].num_connections = base + (i < rem ? 1 : 0);
        g_workers[i].ssl_ctx = ssl_ctx;
    }

    printf("=== HTTP/2 Load Tester ===\n");
    printf("Target      : %s\n", g_config.url_str);
    printf("Threads     : %d\n", g_config.threads);
    printf("Connections : %d (streams/conn: %d, batch: %d)\n",
           g_config.connections, g_config.streams_per_conn, g_config.batch_size);
    printf("Warmup      : %ds | Duration: %ds\n\n", g_config.warmup, g_config.duration);

    atomic_store_explicit(&g_phase, PHASE_WARMUP, memory_order_relaxed);

    for (int i = 0; i < g_config.threads; i++) {
        if (pthread_create(&g_workers[i].tid, NULL, worker_main, &g_workers[i]) != 0) {
            fprintf(stderr, "Error: gagal membuat thread %d\n", i);
            return 1;
        }
    }

    int cap = (g_config.warmup + g_config.duration + 5) * 12 + 16;
    sample_t *samples = calloc((size_t)cap, sizeof(sample_t));
    int nsamples = 0;

    struct timespec t_phase_start;
    clock_gettime(CLOCK_MONOTONIC, &t_phase_start);
    memset(&g_last_tick, 0, sizeof(g_last_tick));

    printf("[WARMUP] Menghubungkan & memanaskan koneksi...\n");
    while (!g_shutdown) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double el = (double)timespec_diff_us(&t_phase_start, &now) / 1000000.0;
        if (el >= (double)g_config.warmup) break;
        usleep(100000);
        tick("WARMUP", el, (double)g_config.warmup, NULL, NULL, 0, 0);
    }

    if (!g_shutdown) atomic_store_explicit(&g_phase, PHASE_MEASURE, memory_order_relaxed);
    printf("\n[MEASURE] Test dimulai...\n");
    clock_gettime(CLOCK_MONOTONIC, &t_phase_start);
    memset(&g_last_tick, 0, sizeof(g_last_tick));
    g_last_sent = 0;

    while (!g_shutdown) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double el = (double)timespec_diff_us(&t_phase_start, &now) / 1000000.0;
        if (el >= (double)g_config.duration) break;
        usleep(100000);
        tick("MEASURE", el, (double)g_config.duration, samples, &nsamples, cap, 1);
    }

    atomic_store_explicit(&g_phase, PHASE_DRAIN, memory_order_relaxed);
    printf("\n[DRAIN] Menyelesaikan request yang masih berjalan...\n");
    struct timespec drain_start; clock_gettime(CLOCK_MONOTONIC, &drain_start);
    while (1) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        if (timespec_diff_us(&drain_start, &now) >= 1000000ULL) break;
        usleep(100000);
    }

    atomic_store_explicit(&g_phase, PHASE_STOPPED, memory_order_relaxed);
    for (int i = 0; i < g_config.threads; i++) pthread_join(g_workers[i].tid, NULL);

    uint64_t sent, success, errors, bytes, active_conns, reconnects, ec, et, eh, er, eo;
    sum_stats(&sent, &success, &errors, &bytes, &active_conns, &reconnects, &ec, &et, &eh, &er, &eo);
    merged_hist_t hist;
    merge_histograms(&hist);
    uint64_t p50 = percentile_us_bp(&hist, 5000);
    uint64_t p95 = percentile_us_bp(&hist, 9500);
    uint64_t p99 = percentile_us_bp(&hist, 9900);

    printf("\n\n=== HASIL AKHIR ===\n");
    printf("Total request      : %llu\n", (unsigned long long)sent);
    printf("Berhasil           : %llu (%.2f%%)\n", (unsigned long long)success,
           sent ? 100.0 * (double)success / (double)sent : 0.0);
    printf("Gagal              : %llu\n", (unsigned long long)errors);
    printf("  - connect        : %llu\n", (unsigned long long)ec);
    printf("  - timeout        : %llu\n", (unsigned long long)et);
    printf("  - http2 protocol : %llu\n", (unsigned long long)eh);
    printf("  - reset/goaway   : %llu\n", (unsigned long long)er);
    printf("  - lainnya        : %llu\n", (unsigned long long)eo);
    printf("Rata-rata RPS      : %.1f\n", g_config.duration > 0 ? (double)sent / g_config.duration : 0.0);
    printf("Latency P50/P95/P99: %llu / %llu / %llu us\n",
           (unsigned long long)p50, (unsigned long long)p95, (unsigned long long)p99);
    printf("Total bytes diterima: %.2f MB\n", bytes / (1024.0 * 1024.0));
    printf("Reconnects         : %llu\n", (unsigned long long)reconnects);

    write_csv(g_config.csv_path, samples, nsamples, &hist, sent, success, errors,
              (double)g_config.duration, ec, et, eh, er, eo);
    printf("\nHasil disimpan ke: %s\n", g_config.csv_path);

    free(samples);
    free(g_workers);
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    return 0;
}
