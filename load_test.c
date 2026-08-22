/*
 * load_test.c — HTTP Load Testing Tool (libcurl + pthread + epoll)
 * =============================================================================
 * Untuk benchmark server INTERNAL (mis. 16 core / 32 thread) dengan target
 * throughput tinggi (100K+ RPS tergantung kapasitas target & jaringan).
 *
 * ARSITEKTUR
 * ----------
 *   [dispatcher/worker threads]  --push token-->  [job queue (semaphore)]
 *                                                        |
 *                                                        v
 *   [I/O threads: N = --thread] --pop token--> [curl_multi + epoll loop]
 *          (masing-masing thread punya CURLM sendiri, menjaga hingga
 *           --connection koneksi konkuren via keep-alive/connection reuse)
 *
 *   - --thread     : jumlah OS thread I/O (masing-masing punya event loop
 *                     epoll + CURLM sendiri). Ini yang men-scale ke jumlah
 *                     core/thread CPU.
 *   - --connection : batas koneksi konkuren PER thread I/O (di-set ke
 *                     CURLMOPT_MAX_TOTAL_CONNECTIONS / MAX_HOST_CONNECTIONS).
 *   - --worker     : jumlah thread dispatcher/"worker pool" yang mengisi
 *                     job queue (mengatur pacing kalau --rate dipakai; kalau
 *                     --rate=0 dispatcher hanya menjaga antrean tetap penuh
 *                     supaya throughput maksimal).
 *
 * CATATAN PENTING — MOHON DIBACA
 * -------------------------------
 * 1. Parameter frame HTTP/2 yang Anda sebutkan (headerTableSize,
 *    maxHeaderListSize, initialWindowSize, maxFrameSize, enablePush) adalah
 *    nama opsi milik modul `http2` Node.js, BUKAN opsi yang diekspos oleh
 *    libcurl. libcurl (via nghttp2) mengelola SETTINGS frame HTTP/2 secara
 *    internal dan TIDAK menyediakan CURLOPT_* untuk men-tuning nilai-nilai
 *    tersebut secara manual. Karena itu kode ini TIDAK men-set opsi-opsi
 *    tersebut (tidak ada makro CURLOPT yang sesuai — mengarang nama makro
 *    hanya akan membuat kode gagal kompilasi). Yang benar-benar bisa
 *    dikontrol lewat libcurl sudah diterapkan di bawah: HTTP_VERSION,
 *    ALPN, TCP_FASTOPEN, PIPEWAIT, dan TLS 1.3 cipher suite.
 * 2. CURLOPT_SSL_ENABLE_ALPN sudah deprecated di libcurl modern (ALPN selalu
 *    dicoba secara default), tapi tetap di-set di sini (dibungkus #ifdef)
 *    untuk kompatibilitas versi lama.
 * 3. CURLOPT_SSL_VERIFYPEER/VERIFYHOST = 0 HANYA untuk testing server
 *    internal. Jangan pernah dipakai untuk trafik ke luar/produksi.
 * 4. libcurl >= 7.61 dibutuhkan untuk CURLOPT_TLS13_CIPHERS, dan backend TLS
 *    harus OpenSSL/BoringSSL/LibreSSL (bukan GnuTLS/NSS/Schannel) agar opsi
 *    ini berpengaruh. Cek dengan `curl -V`.
 * 5. Header "Connection: keep-alive" tidak valid di HTTP/2 (hop-by-hop
 *    header dilarang oleh spec). libcurl/nghttp2 akan mengabaikan/strip
 *    header ini secara otomatis kalau koneksi ternyata naik ke h2 lewat
 *    ALPN; header ini baru benar-benar relevan kalau server jatuh ke
 *    HTTP/1.1.
 *
 * KOMPILASI
 * ---------
 *   gcc -O2 -pthread -D_GNU_SOURCE -o load_test load_test.c -lcurl
 *
 * Pastikan libcurl versi runtime & compile-time mendukung fitur di atas:
 *   curl -V   (harus muncul: HTTP2, TLS-1.3/ssl backend openssl)
 *
 * CONTOH PAKAI
 * ------------
 *   ./load_test --host https://internal.svc.local/health \
 *       --time 30 --thread 32 --connection 100 --worker 8
 *
 *   ./load_test --host https://internal.svc.local/health \
 *       --time 30 --thread 32 --connection 100 --worker 8 --rate 50000
 *
 * Sebelum lari ke 100K+ RPS, naikkan file descriptor limit:
 *   ulimit -n 200000
 * (program ini juga mencoba menaikkannya sendiri secara best-effort)
 * =============================================================================
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>
#include <sys/resource.h>

#include <curl/curl.h>

#ifdef __linux__
#include <sys/epoll.h>
#define USE_EPOLL 1
#endif

/* ============================== Konfigurasi ============================== */

typedef struct {
    char host[2048];
    int  duration_sec;
    long rate;                  /* 0 = unlimited / maximum throughput */
    int  num_threads;           /* jumlah I/O thread (event loop) */
    int  connections_per_thread;/* concurrent connections per I/O thread */
    int  num_workers;           /* jumlah dispatcher thread (job queue producer) */
    int  timeout_sec;           /* per-request timeout */
    int  max_retries;           /* max retry per request */
} config_t;

static config_t g_cfg;

/* ============================== Statistik global (atomic) ============================== */

static atomic_ullong g_total_requests        = 0;
static atomic_ullong g_success_count         = 0;
static atomic_ullong g_failed_count          = 0;
static atomic_ullong g_retry_count           = 0;
static atomic_ullong g_total_latency_us      = 0;
static atomic_ullong g_completed_this_window = 0; /* di-reset reporter tiap 1 detik */
static atomic_ullong g_min_latency_us        = ATOMIC_VAR_INIT((unsigned long long)-1);
static atomic_ullong g_max_latency_us        = 0;

static atomic_int g_stop = 0;

static sem_t g_job_sem; /* "job queue": token = izin mengirim 1 request */

static struct timespec g_start_time;

/* ============================== Helper waktu ============================== */

static double elapsed_sec(struct timespec *from) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - from->tv_sec) + (now.tv_nsec - from->tv_nsec) / 1e9;
}

static long long timespec_diff_us(const struct timespec *a, const struct timespec *b) {
    return (long long)(b->tv_sec - a->tv_sec) * 1000000LL +
           (b->tv_nsec - a->tv_nsec) / 1000LL;
}

static void update_min_max_latency(unsigned long long lat_us) {
    unsigned long long cur_min = atomic_load(&g_min_latency_us);
    while (lat_us < cur_min &&
           !atomic_compare_exchange_weak(&g_min_latency_us, &cur_min, lat_us)) { }
    unsigned long long cur_max = atomic_load(&g_max_latency_us);
    while (lat_us > cur_max &&
           !atomic_compare_exchange_weak(&g_max_latency_us, &cur_max, lat_us)) { }
}

/* ============================== Request context ============================== */

typedef struct {
    struct timespec t_start;
    int             retries_left;
} req_ctx_t;

static size_t discard_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr; (void)userdata;
    return size * nmemb; /* buang body, kita cuma butuh status & timing */
}

/* Membuat satu easy handle siap pakai untuk satu request GET. */
static CURL *make_easy_handle(const config_t *cfg, struct curl_slist *shared_headers) {
    CURL *eh = curl_easy_init();
    if (!eh) return NULL;

    req_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) { curl_easy_cleanup(eh); return NULL; }
    ctx->retries_left = cfg->max_retries;
    clock_gettime(CLOCK_MONOTONIC, &ctx->t_start);

    curl_easy_setopt(eh, CURLOPT_URL, cfg->host);
    curl_easy_setopt(eh, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(eh, CURLOPT_HTTPHEADER, shared_headers);
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, discard_cb);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(eh, CURLOPT_TIMEOUT, (long)cfg->timeout_sec);
    curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT, (long)cfg->timeout_sec);
    curl_easy_setopt(eh, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(eh, CURLOPT_ACCEPT_ENCODING, ""); /* terima semua encoding yg didukung */

    /* --- Connection reuse / keep-alive --- */
    curl_easy_setopt(eh, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(eh, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(eh, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(eh, CURLOPT_FORBID_REUSE, 0L);
    curl_easy_setopt(eh, CURLOPT_FRESH_CONNECT, 0L);
    /* Catatan: reuse koneksi terjadi di level CURLM (connection cache milik
       multi handle per-thread), bukan di level easy handle. Selama easy
       handle baru ditambahkan ke CURLM yang sama, libcurl otomatis memakai
       ulang koneksi TCP/TLS yang masih hidup. */

    /* --- HTTP/2 --- */
    curl_easy_setopt(eh, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
#ifdef CURLOPT_SSL_ENABLE_ALPN
    curl_easy_setopt(eh, CURLOPT_SSL_ENABLE_ALPN, 1L); /* deprecated tapi aman di-set */
#endif
#ifdef CURLOPT_TCP_FASTOPEN
    curl_easy_setopt(eh, CURLOPT_TCP_FASTOPEN, 1L);
#endif
#ifdef CURLOPT_PIPEWAIT
    curl_easy_setopt(eh, CURLOPT_PIPEWAIT, 1L); /* tunggu multiplexing drpd buka koneksi baru */
#endif

    /* --- TLS 1.3 cipher suites --- */
#ifdef CURLOPT_TLS13_CIPHERS
    curl_easy_setopt(eh, CURLOPT_TLS13_CIPHERS,
        "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");
#endif

    /* --- Internal testing only: matikan verifikasi TLS --- */
    curl_easy_setopt(eh, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(eh, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(eh, CURLOPT_PRIVATE, (void *)ctx);

    return eh;
}

/* ============================== I/O thread (event loop) ============================== */

typedef struct {
    CURLM              *multi;
    int                 epoll_fd;
    int                 in_flight;
    long                timeout_ms; /* -1 = tidak ada timer aktif dari curl */
    const config_t     *cfg;
    struct curl_slist   *headers;
    int                 idx;
} thread_ctx_t;

#ifdef USE_EPOLL
static int sock_cb(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp) {
    (void)easy;
    thread_ctx_t *tc = (thread_ctx_t *)userp;
    int *fdp = (int *)socketp;

    if (what == CURL_POLL_REMOVE) {
        if (fdp) {
            epoll_ctl(tc->epoll_fd, EPOLL_CTL_DEL, s, NULL);
            curl_multi_assign(tc->multi, s, NULL);
            free(fdp);
        }
        return 0;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    if (what & CURL_POLL_IN)  ev.events |= EPOLLIN;
    if (what & CURL_POLL_OUT) ev.events |= EPOLLOUT;
    ev.data.fd = s;

    if (!fdp) {
        fdp = malloc(sizeof(int));
        *fdp = (int)s;
        curl_multi_assign(tc->multi, s, fdp);
        epoll_ctl(tc->epoll_fd, EPOLL_CTL_ADD, s, &ev);
    } else {
        epoll_ctl(tc->epoll_fd, EPOLL_CTL_MOD, s, &ev);
    }
    return 0;
}

static int timer_cb(CURLM *multi, long timeout_ms, void *userp) {
    (void)multi;
    thread_ctx_t *tc = (thread_ctx_t *)userp;
    tc->timeout_ms = timeout_ms;
    return 0;
}
#endif /* USE_EPOLL */

/* Baca hasil transfer yang sudah selesai, catat statistik, tangani retry. */
static void reap_completed(thread_ctx_t *tc) {
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(tc->multi, &msgs_left)) != NULL) {
        if (msg->msg != CURLMSG_DONE) continue;

        CURL *eh = msg->easy_handle;
        req_ctx_t *ctx = NULL;
        curl_easy_getinfo(eh, CURLINFO_PRIVATE, (char **)&ctx);

        long http_code = 0;
        curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_code);
        CURLcode result = msg->data.result;
        bool ok = (result == CURLE_OK && http_code >= 200 && http_code < 400);

        if (!ok && ctx && ctx->retries_left > 0) {
            /* --- retry sederhana dengan backoff sederhana --- */
            int attempt = tc->cfg->max_retries - ctx->retries_left + 1;
            ctx->retries_left--;
            atomic_fetch_add(&g_retry_count, 1);

            curl_multi_remove_handle(tc->multi, eh);
            usleep(50000 * attempt); /* 50ms, 100ms, ... — simple linear backoff */
            clock_gettime(CLOCK_MONOTONIC, &ctx->t_start);
            curl_multi_add_handle(tc->multi, eh);
            continue; /* tetap in_flight, belum dihitung selesai */
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        unsigned long long lat_us = ctx ? (unsigned long long)timespec_diff_us(&ctx->t_start, &now) : 0;

        atomic_fetch_add(&g_total_requests, 1);
        atomic_fetch_add(&g_completed_this_window, 1);
        atomic_fetch_add(&g_total_latency_us, lat_us);
        update_min_max_latency(lat_us);

        if (ok) atomic_fetch_add(&g_success_count, 1);
        else    atomic_fetch_add(&g_failed_count, 1);

        curl_multi_remove_handle(tc->multi, eh);
        curl_easy_cleanup(eh);
        free(ctx);
        tc->in_flight--;
    }
}

static void *io_thread_main(void *arg) {
    thread_ctx_t *tc = (thread_ctx_t *)arg;

    tc->multi = curl_multi_init();
    tc->in_flight = 0;
    tc->timeout_ms = -1;

    long max_conn = tc->cfg->connections_per_thread;
    curl_multi_setopt(tc->multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, max_conn);
    curl_multi_setopt(tc->multi, CURLMOPT_MAX_HOST_CONNECTIONS, max_conn);
#ifdef CURLPIPE_MULTIPLEX
    curl_multi_setopt(tc->multi, CURLMOPT_PIPELINING, (long)CURLPIPE_MULTIPLEX);
#endif

#ifdef USE_EPOLL
    tc->epoll_fd = epoll_create1(0);
    curl_multi_setopt(tc->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
    curl_multi_setopt(tc->multi, CURLMOPT_SOCKETDATA, tc);
    curl_multi_setopt(tc->multi, CURLMOPT_TIMERFUNCTION, timer_cb);
    curl_multi_setopt(tc->multi, CURLMOPT_TIMERDATA, tc);

    int still_running = 0;
    struct epoll_event events[256];

    while (!atomic_load(&g_stop) || tc->in_flight > 0) {
        /* Isi slot kosong dari job queue (non-blocking) selama belum stop */
        while (!atomic_load(&g_stop) && tc->in_flight < tc->cfg->connections_per_thread) {
            if (sem_trywait(&g_job_sem) != 0) break; /* tidak ada token, jangan blok loop */
            CURL *eh = make_easy_handle(tc->cfg, tc->headers);
            if (!eh) break;
            curl_multi_add_handle(tc->multi, eh);
            tc->in_flight++;
        }

        int wait_ms = 100;
        if (tc->timeout_ms >= 0 && tc->timeout_ms < wait_ms) wait_ms = (int)tc->timeout_ms;
        if (wait_ms <= 0) wait_ms = 1;

        int n = epoll_wait(tc->epoll_fd, events, 256, wait_ms);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) {
            curl_multi_socket_action(tc->multi, CURL_SOCKET_TIMEOUT, 0, &still_running);
        } else {
            for (int i = 0; i < n; i++) {
                int mask = 0;
                if (events[i].events & EPOLLIN)  mask |= CURL_CSELECT_IN;
                if (events[i].events & EPOLLOUT) mask |= CURL_CSELECT_OUT;
                if (events[i].events & (EPOLLERR | EPOLLHUP)) mask |= CURL_CSELECT_ERR;
                curl_multi_socket_action(tc->multi, events[i].data.fd, mask, &still_running);
            }
        }
        reap_completed(tc);
    }
    close(tc->epoll_fd);
#else
    /* Fallback non-Linux: pakai curl_multi_poll bawaan (tetap non-blocking
       terhadap thread lain karena tiap thread punya CURLM sendiri). */
    int still_running = 0;
    while (!atomic_load(&g_stop) || tc->in_flight > 0) {
        while (!atomic_load(&g_stop) && tc->in_flight < tc->cfg->connections_per_thread) {
            if (sem_trywait(&g_job_sem) != 0) break;
            CURL *eh = make_easy_handle(tc->cfg, tc->headers);
            if (!eh) break;
            curl_multi_add_handle(tc->multi, eh);
            tc->in_flight++;
        }
        curl_multi_perform(tc->multi, &still_running);
        curl_multi_poll(tc->multi, NULL, 0, 100, NULL);
        reap_completed(tc);
    }
#endif

    curl_multi_cleanup(tc->multi);
    return NULL;
}

/* ============================== Dispatcher / worker pool ============================== */
/* Thread ini mengisi "job queue" (semaphore token). Kalau --rate=0, tugasnya
 * cuma menjaga antrean tetap penuh (maksimalkan throughput). Kalau --rate>0,
 * tiap dispatcher memacu sebagian dari total rate secara merata. */

typedef struct {
    long rate_per_worker; /* 0 = unlimited */
    int  max_pending;     /* batas atas token menumpuk di semaphore */
} dispatcher_ctx_t;

static void *dispatcher_main(void *arg) {
    dispatcher_ctx_t *dc = (dispatcher_ctx_t *)arg;

    if (dc->rate_per_worker <= 0) {
        while (!atomic_load(&g_stop)) {
            int val = 0;
            sem_getvalue(&g_job_sem, &val);
            if (val < dc->max_pending) {
                sem_post(&g_job_sem);
            } else {
                usleep(200); /* hindari busy-spin murni saat antrean penuh */
            }
        }
    } else {
        long interval_ns = (long)(1e9 / (double)dc->rate_per_worker);
        struct timespec next;
        clock_gettime(CLOCK_MONOTONIC, &next);

        while (!atomic_load(&g_stop)) {
            sem_post(&g_job_sem);

            next.tv_nsec += interval_ns;
            while (next.tv_nsec >= 1000000000L) {
                next.tv_nsec -= 1000000000L;
                next.tv_sec++;
            }
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long long sleep_ns = (long long)(next.tv_sec - now.tv_sec) * 1000000000LL +
                                  (next.tv_nsec - now.tv_nsec);
            if (sleep_ns > 0) {
                struct timespec req;
                req.tv_sec = sleep_ns / 1000000000LL;
                req.tv_nsec = sleep_ns % 1000000000LL;
                nanosleep(&req, NULL);
            }
        }
    }
    return NULL;
}

/* ============================== Reporter (metrik realtime) ============================== */

static void *reporter_main(void *arg) {
    (void)arg;
    while (!atomic_load(&g_stop)) {
        sleep(1);
        if (atomic_load(&g_stop)) break;

        unsigned long long completed = atomic_exchange(&g_completed_this_window, 0);
        unsigned long long total  = atomic_load(&g_total_requests);
        unsigned long long ok     = atomic_load(&g_success_count);
        unsigned long long fail   = atomic_load(&g_failed_count);
        unsigned long long latsum = atomic_load(&g_total_latency_us);

        double avg_lat_ms = total ? (double)latsum / (double)total / 1000.0 : 0.0;
        double err_rate    = total ? (double)fail * 100.0 / (double)total : 0.0;

        printf("[t=%5.0fs] RPS: %7llu | Total: %9llu | Success: %9llu | Failed: %7llu | AvgLatency: %7.2fms | ErrRate: %5.2f%%\n",
               elapsed_sec(&g_start_time), completed, total, ok, fail, avg_lat_ms, err_rate);
        fflush(stdout);
    }
    return NULL;
}

/* ============================== SIGINT handler ============================== */

static void on_sigint(int signo) {
    (void)signo;
    atomic_store(&g_stop, 1);
}

/* ============================== rlimit check ============================== */

static void ensure_fd_limit(const config_t *cfg) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return;

    rlim_t needed = (rlim_t)cfg->num_threads * (rlim_t)cfg->connections_per_thread + 256;
    if (rl.rlim_cur < needed) {
        fprintf(stderr,
            "[WARN] ulimit -n saat ini (%lu) mungkin kurang untuk %lu koneksi target.\n"
            "       Mencoba menaikkan otomatis...\n",
            (unsigned long)rl.rlim_cur, (unsigned long)needed);

        rlim_t target = needed;
        if (rl.rlim_max != RLIM_INFINITY && target > rl.rlim_max) target = rl.rlim_max;
        rl.rlim_cur = target;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            fprintf(stderr,
                "[WARN] Gagal menaikkan otomatis. Jalankan manual sebelum start:\n"
                "       ulimit -n %lu\n", (unsigned long)needed);
        } else {
            fprintf(stderr, "[INFO] ulimit -n dinaikkan ke %lu\n", (unsigned long)rl.rlim_cur);
        }
    }
}

/* ============================== CLI ============================== */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Pemakaian: %s --host <url> --time <detik> [opsi]\n\n"
        "Wajib:\n"
        "  --host <url>         URL target, contoh https://internal.svc/health\n"
        "  --time <detik>       Durasi test dalam detik\n\n"
        "Opsional:\n"
        "  --rate <n>           Target request/detik total (0/tidak diset = maksimal)\n"
        "  --thread <n>         Jumlah I/O thread (default 4, sarankan ~jumlah core)\n"
        "  --connection <n>     Max koneksi konkuren per thread (default 50)\n"
        "  --worker <n>         Jumlah dispatcher/job-queue worker (default = thread)\n"
        "  --timeout <detik>    Timeout per request (default 10)\n"
        "  --retries <n>        Max retry per request (default 2)\n"
        "  -h, --help           Tampilkan bantuan ini\n\n"
        "Contoh:\n"
        "  %s --host https://internal.local/ping --time 30 --thread 32 --connection 100 --worker 8\n",
        prog, prog);
}

static int parse_args(int argc, char **argv, config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->rate = 0;
    cfg->num_threads = 4;
    cfg->connections_per_thread = 50;
    cfg->num_workers = -1; /* -1 = default ke num_threads, dihitung nanti */
    cfg->timeout_sec = 10;
    cfg->max_retries = 2;

    static struct option long_opts[] = {
        {"host",       required_argument, 0, 'H'},
        {"time",       required_argument, 0, 't'},
        {"rate",       required_argument, 0, 'r'},
        {"thread",     required_argument, 0, 'T'},
        {"connection", required_argument, 0, 'c'},
        {"worker",     required_argument, 0, 'w'},
        {"timeout",    required_argument, 0, 'o'},
        {"retries",    required_argument, 0, 'x'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int has_host = 0, has_time = 0;
    int c;
    while ((c = getopt_long(argc, argv, "h", long_opts, NULL)) != -1) {
        switch (c) {
            case 'H': snprintf(cfg->host, sizeof(cfg->host), "%s", optarg); has_host = 1; break;
            case 't': cfg->duration_sec = atoi(optarg); has_time = 1; break;
            case 'r': cfg->rate = atol(optarg); break;
            case 'T': cfg->num_threads = atoi(optarg); break;
            case 'c': cfg->connections_per_thread = atoi(optarg); break;
            case 'w': cfg->num_workers = atoi(optarg); break;
            case 'o': cfg->timeout_sec = atoi(optarg); break;
            case 'x': cfg->max_retries = atoi(optarg); break;
            case 'h': print_usage(argv[0]); exit(0);
            default:  print_usage(argv[0]); return -1;
        }
    }

    if (!has_host || !has_time) {
        fprintf(stderr, "Error: --host dan --time wajib diisi.\n\n");
        print_usage(argv[0]);
        return -1;
    }
    if (strncmp(cfg->host, "http://", 7) != 0 && strncmp(cfg->host, "https://", 8) != 0) {
        fprintf(stderr, "Error: --host harus diawali http:// atau https://\n");
        return -1;
    }
    if (cfg->duration_sec <= 0)            { fprintf(stderr, "Error: --time harus > 0\n"); return -1; }
    if (cfg->num_threads <= 0)             { fprintf(stderr, "Error: --thread harus > 0\n"); return -1; }
    if (cfg->connections_per_thread <= 0)  { fprintf(stderr, "Error: --connection harus > 0\n"); return -1; }
    if (cfg->num_workers <= 0)             cfg->num_workers = cfg->num_threads;
    if (cfg->max_retries < 0)              cfg->max_retries = 0;

    return 0;
}

/* ============================== main ============================== */

int main(int argc, char **argv) {
    if (parse_args(argc, argv, &g_cfg) != 0) return 1;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    ensure_fd_limit(&g_cfg);

    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        fprintf(stderr, "curl_global_init gagal\n");
        return 1;
    }
    /* Catatan: OpenSSL >= 1.1.0 sudah thread-safe secara default (tidak
       perlu locking callback manual). Kalau anda link ke OpenSSL versi
       sangat lama, tambahkan locking callback CRYPTO_set_locking_callback
       sebelum titik ini. */

    sem_init(&g_job_sem, 0, 0);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: LoadTester/1.0");
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Connection: keep-alive");

    clock_gettime(CLOCK_MONOTONIC, &g_start_time);

    printf("=== Load Test Dimulai ===\n");
    printf("Target       : %s\n", g_cfg.host);
    printf("Durasi       : %ds\n", g_cfg.duration_sec);
    printf("Rate         : %s\n", g_cfg.rate > 0 ? "" : "maksimal (uncapped)");
    if (g_cfg.rate > 0) printf("               %ld req/s\n", g_cfg.rate);
    printf("Thread (I/O) : %d\n", g_cfg.num_threads);
    printf("Connection   : %d per thread (total ~%d)\n",
           g_cfg.connections_per_thread, g_cfg.num_threads * g_cfg.connections_per_thread);
    printf("Worker       : %d\n", g_cfg.num_workers);
    printf("Timeout      : %ds | Max retry: %d\n", g_cfg.timeout_sec, g_cfg.max_retries);
    printf("==========================\n\n");

    /* --- spawn I/O threads --- */
    pthread_t *io_threads = calloc(g_cfg.num_threads, sizeof(pthread_t));
    thread_ctx_t *tctxs = calloc(g_cfg.num_threads, sizeof(thread_ctx_t));
    for (int i = 0; i < g_cfg.num_threads; i++) {
        tctxs[i].cfg = &g_cfg;
        tctxs[i].headers = headers;
        tctxs[i].idx = i;
        pthread_create(&io_threads[i], NULL, io_thread_main, &tctxs[i]);
    }

    /* --- spawn dispatcher/worker threads --- */
    pthread_t *worker_threads = calloc(g_cfg.num_workers, sizeof(pthread_t));
    dispatcher_ctx_t *dctxs = calloc(g_cfg.num_workers, sizeof(dispatcher_ctx_t));
    long total_capacity = (long)g_cfg.num_threads * g_cfg.connections_per_thread;
    for (int i = 0; i < g_cfg.num_workers; i++) {
        if (g_cfg.rate > 0) {
            long base = g_cfg.rate / g_cfg.num_workers;
            long rem  = g_cfg.rate % g_cfg.num_workers;
            dctxs[i].rate_per_worker = base + (i < rem ? 1 : 0);
            if (dctxs[i].rate_per_worker <= 0) dctxs[i].rate_per_worker = 1;
        } else {
            dctxs[i].rate_per_worker = 0;
        }
        /* tiap dispatcher boleh menumpuk token hingga porsinya dari total
           kapasitas koneksi, supaya I/O thread selalu punya kerjaan */
        dctxs[i].max_pending = (int)(total_capacity / g_cfg.num_workers) + 1;
        pthread_create(&worker_threads[i], NULL, dispatcher_main, &dctxs[i]);
    }

    /* --- reporter --- */
    pthread_t reporter_thread;
    pthread_create(&reporter_thread, NULL, reporter_main, NULL);

    /* --- jalan sesuai durasi (atau sampai Ctrl+C) --- */
    int slept = 0;
    while (slept < g_cfg.duration_sec && !atomic_load(&g_stop)) {
        sleep(1);
        slept++;
    }
    atomic_store(&g_stop, 1);

    /* bangunkan dispatcher yang mungkin sedang nanosleep/blocking */
    for (int i = 0; i < g_cfg.num_workers; i++) sem_post(&g_job_sem);

    pthread_join(reporter_thread, NULL);
    for (int i = 0; i < g_cfg.num_workers; i++) pthread_join(worker_threads[i], NULL);
    for (int i = 0; i < g_cfg.num_threads; i++) pthread_join(io_threads[i], NULL);

    /* --- ringkasan akhir --- */
    unsigned long long total  = atomic_load(&g_total_requests);
    unsigned long long ok     = atomic_load(&g_success_count);
    unsigned long long fail   = atomic_load(&g_failed_count);
    unsigned long long retry  = atomic_load(&g_retry_count);
    unsigned long long latsum = atomic_load(&g_total_latency_us);
    unsigned long long minlat = atomic_load(&g_min_latency_us);
    unsigned long long maxlat = atomic_load(&g_max_latency_us);
    double total_time = elapsed_sec(&g_start_time);

    printf("\n=== Ringkasan ===\n");
    printf("Durasi aktual     : %.2fs\n", total_time);
    printf("Total requests    : %llu\n", total);
    printf("Success           : %llu\n", ok);
    printf("Failed            : %llu\n", fail);
    printf("Retry dilakukan   : %llu\n", retry);
    printf("Error rate        : %.2f%%\n", total ? (double)fail * 100.0 / (double)total : 0.0);
    printf("Rata-rata RPS     : %.2f\n", total_time > 0 ? (double)total / total_time : 0.0);
    printf("Avg latency       : %.2f ms\n", total ? (double)latsum / (double)total / 1000.0 : 0.0);
    printf("Min / Max latency : %.2f ms / %.2f ms\n",
           (minlat == (unsigned long long)-1 ? 0.0 : minlat / 1000.0),
           maxlat / 1000.0);
    printf("=================\n");

    curl_slist_free_all(headers);
    curl_global_cleanup();
    sem_destroy(&g_job_sem);
    free(io_threads); free(tctxs);
    free(worker_threads); free(dctxs);

    return 0;
}
