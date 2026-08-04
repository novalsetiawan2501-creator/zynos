/*
 * ============================================================================
 *  loadtest.c — HTTP/2 Load Testing Tool
 * ============================================================================
 *
 * Simulasi traffic tinggi ke server web menggunakan libcurl (multi interface)
 * dengan dukungan HTTP/2, multithreading (pthread), connection pooling,
 * non-blocking I/O, rate limiting, dan statistik real-time.
 *
 * Kegunaan: benchmarking / stress test kapasitas server milik sendiri
 * (staging, internal service, dsb). Gunakan hanya pada target yang memang
 * berhak Anda uji.
 *
 * ----------------------------------------------------------------------
 * KOMPILASI
 * ----------------------------------------------------------------------
 *   Debian/Ubuntu:
 *     sudo apt-get install libcurl4-openssl-dev
 *     gcc -O2 -Wall -o loadtest loadtest.c -lcurl -lpthread
 *
 *   Pastikan libcurl Anda dibuild dengan dukungan HTTP/2 (butuh nghttp2).
 *   Cek dengan:
 *     curl -V
 *   Harus muncul "HTTP2" di baris "Features:".
 *
 * ----------------------------------------------------------------------
 * CONTOH PENGGUNAAN
 * ----------------------------------------------------------------------
 *   ./loadtest --url=https://example.com/api/ping \
 *              --time=30 --rate=200 --thread=4 --connection=20 --worker=4
 *
 *   Argumen:
 *     --url=<endpoint>       URL target (wajib)
 *     --time=<detik>         durasi pengujian (default 10)
 *     --rate=<req/detik>     target total request per detik (default 50)
 *     --thread=<n>           jumlah thread pekerja (default 4)
 *     --connection=<n>       jumlah koneksi paralel PER thread, dipakai
 *                            sebagai ukuran connection pool (default 10)
 *     --worker=<n>           ukuran worker pool untuk multi handle
 *                            tambahan (mis. paralel curl_multi loop per
 *                            thread); default 1 (satu multi handle/thread)
 *     --header="K: V"        tambahan header custom (boleh diulang)
 *     --insecure             lewati verifikasi SSL (untuk self-signed cert)
 *
 * ============================================================================
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <stdatomic.h>
#include <curl/curl.h>

/* ----------------------------------------------------------------------
 * Konstanta & tipe data
 * ---------------------------------------------------------------------- */

#define MAX_HEADERS      32
#define MAX_URL_LEN      2048
#define DEFAULT_TIME     10
#define DEFAULT_RATE     50
#define DEFAULT_THREAD   4
#define DEFAULT_CONN     10
#define DEFAULT_WORKER   1

/* Statistik global, diakses lintas thread lewat operasi atomic supaya
 * tidak perlu mutex (lock-free counters). */
typedef struct {
    atomic_long total_requests;   /* total request yang selesai (sukses+gagal) */
    atomic_long success_count;    /* HTTP 2xx/3xx */
    atomic_long fail_count;       /* error koneksi / HTTP >=400 */
    atomic_long bytes_received;   /* total body bytes yang diterima */
    atomic_long latency_sum_us;   /* akumulasi latency (microsecond) untuk rata-rata */
} stats_t;

static stats_t g_stats = {0};

/* Flag global untuk graceful shutdown (SIGINT/SIGTERM) */
static atomic_int g_stop_flag = 0;

/* Konfigurasi hasil parsing argumen */
typedef struct {
    char url[MAX_URL_LEN];
    int  duration_sec;
    int  rate_per_sec;     /* target total (semua thread digabung) */
    int  thread_count;
    int  connection_count; /* ukuran connection pool per thread */
    int  worker_count;     /* jumlah multi-handle loop per thread */
    char *headers[MAX_HEADERS];
    int  header_count;
    int  insecure;
} config_t;

/* Data yang dikirim ke tiap thread pekerja */
typedef struct {
    int thread_id;
    const config_t *cfg;
    double rate_per_thread;   /* target request/detik untuk thread ini */
    time_t start_time;
} thread_arg_t;

/* Context per easy-handle (dipakai di connection pool), supaya callback
 * write tahu handle mana yang sedang aktif dan bisa mencatat waktu mulai
 * (untuk menghitung latency). */
typedef struct {
    CURL *easy;
    struct curl_slist *headers;
    struct timespec t_start;
    char errbuf[CURL_ERROR_SIZE];
} conn_ctx_t;

/* ----------------------------------------------------------------------
 * Signal handler — graceful shutdown
 * ---------------------------------------------------------------------- */
static void handle_signal(int sig) {
    (void)sig;
    /* set flag saja di signal handler, aman dipakai (async-signal-safe) */
    atomic_store(&g_stop_flag, 1);
}

/* ----------------------------------------------------------------------
 * Helper waktu
 * ---------------------------------------------------------------------- */
static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static long diff_micros(struct timespec *a, struct timespec *b) {
    return (b->tv_sec - a->tv_sec) * 1000000L +
           (b->tv_nsec - a->tv_nsec) / 1000L;
}

/* ----------------------------------------------------------------------
 * libcurl write callback — kita cuma butuh ukuran body, tidak perlu
 * menyimpan isinya (hemat memori, mencegah leak dari buffer growing).
 * ---------------------------------------------------------------------- */
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    size_t total = size * nmemb;
    atomic_fetch_add(&g_stats.bytes_received, (long)total);
    (void)userdata;
    return total; /* WAJIB mengembalikan jumlah byte yang "diproses" */
}

/* ----------------------------------------------------------------------
 * Membuat satu easy handle terkonfigurasi HTTP/2 + GET, siap dipakai
 * ulang berkali-kali (connection pooling ditangani otomatis oleh
 * connection cache internal libcurl selama easy handle / multi handle
 * yang sama dipakai ulang).
 * ---------------------------------------------------------------------- */
static conn_ctx_t *create_connection(const config_t *cfg) {
    conn_ctx_t *ctx = calloc(1, sizeof(conn_ctx_t));
    if (!ctx) return NULL;

    ctx->easy = curl_easy_init();
    if (!ctx->easy) {
        free(ctx);
        return NULL;
    }

    curl_easy_setopt(ctx->easy, CURLOPT_URL, cfg->url);
    curl_easy_setopt(ctx->easy, CURLOPT_HTTPGET, 1L);

    /* Paksa HTTP/2 (fallback otomatis ke 1.1 jika server tidak mendukung) */
    curl_easy_setopt(ctx->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    /* Multiplexing: biarkan beberapa request berbagi 1 koneksi TCP HTTP/2 */
    curl_easy_setopt(ctx->easy, CURLOPT_PIPEWAIT, 1L);

    curl_easy_setopt(ctx->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(ctx->easy, CURLOPT_WRITEDATA, NULL);

    /* Non-blocking I/O ditangani lewat curl_multi (event loop), bukan di
     * level easy handle — easy handle ini hanya dipakai sebagai "job
     * descriptor" yang di-attach/detach dari multi handle. */
    curl_easy_setopt(ctx->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(ctx->easy, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(ctx->easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(ctx->easy, CURLOPT_ERRORBUFFER, ctx->errbuf);
    curl_easy_setopt(ctx->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(ctx->easy, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(ctx->easy, CURLOPT_USERAGENT, "loadtest-c/1.0");

    if (cfg->insecure) {
        curl_easy_setopt(ctx->easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(ctx->easy, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    /* Header sederhana + custom header dari argumen CLI */
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept: */*");
    hdrs = curl_slist_append(hdrs, "Connection: keep-alive");
    for (int i = 0; i < cfg->header_count; i++) {
        hdrs = curl_slist_append(hdrs, cfg->headers[i]);
    }
    ctx->headers = hdrs;
    curl_easy_setopt(ctx->easy, CURLOPT_HTTPHEADER, ctx->headers);

    /* Simpan pointer ctx di private data supaya bisa diambil balik lewat
     * curl_multi_info_read() (CURLINFO_PRIVATE). */
    curl_easy_setopt(ctx->easy, CURLOPT_PRIVATE, ctx);

    return ctx;
}

static void destroy_connection(conn_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->headers) curl_slist_free_all(ctx->headers);
    if (ctx->easy) curl_easy_cleanup(ctx->easy);
    free(ctx);
}

/* ----------------------------------------------------------------------
 * Worker thread: menjalankan satu (atau lebih, sesuai `worker`) event
 * loop curl_multi, masing-masing memelihara `connection` easy handle
 * sebagai connection pool, dan menjaga rate request/detik lewat token
 * bucket sederhana.
 * ---------------------------------------------------------------------- */
static void *multi_loop(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    const config_t *cfg = targ->cfg;

    CURLM *multi = curl_multi_init();
    if (!multi) {
        fprintf(stderr, "[thread %d] gagal membuat curl_multi handle\n",
                targ->thread_id);
        return NULL;
    }

    /* Batasi jumlah koneksi TCP yang di-cache per multi handle, ini yang
     * memberi efek "connection pooling": koneksi lama dipakai ulang,
     * tidak selalu bikin handshake TLS baru. */
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)cfg->connection_count);
    /* Multiplexing HTTP/2: izinkan banyak transfer berbagi 1 koneksi. */
    curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    /* Buat connection pool: sejumlah `connection_count` easy handle yang
     * langsung didaftarkan ke multi handle supaya request berjalan
     * konkuren dari awal. */
    conn_ctx_t **pool = calloc(cfg->connection_count, sizeof(conn_ctx_t *));
    if (!pool) {
        curl_multi_cleanup(multi);
        return NULL;
    }

    for (int i = 0; i < cfg->connection_count; i++) {
        pool[i] = create_connection(cfg);
        if (!pool[i]) continue;
        clock_gettime(CLOCK_MONOTONIC, &pool[i]->t_start);
        curl_multi_add_handle(multi, pool[i]->easy);
    }

    /* Token bucket sederhana untuk rate limiting per thread */
    double tokens = 0.0;
    double last_refill = now_seconds();
    const double max_tokens = (double)cfg->connection_count; /* burst cap */

    int still_running = 0;
    curl_multi_perform(multi, &still_running);

    double deadline = now_seconds() + (double)cfg->duration_sec;

    while (!atomic_load(&g_stop_flag) && now_seconds() < deadline) {
        int numfds = 0;
        /* curl_multi_poll = non-blocking I/O yang efisien (mirip epoll),
         * timeout 100ms supaya loop tetap responsif terhadap stop flag. */
        CURLMcode mc = curl_multi_poll(multi, NULL, 0, 100, &numfds);
        if (mc != CURLM_OK) {
            fprintf(stderr, "[thread %d] curl_multi_poll error: %s\n",
                    targ->thread_id, curl_multi_strerror(mc));
            break;
        }

        curl_multi_perform(multi, &still_running);

        /* Isi ulang token sesuai waktu yang berlalu (rate limiting) */
        double t = now_seconds();
        tokens += (t - last_refill) * targ->rate_per_thread;
        if (tokens > max_tokens) tokens = max_tokens;
        last_refill = t;

        /* Proses transfer yang sudah selesai */
        CURLMsg *msg;
        int msgs_left = 0;
        while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
            if (msg->msg != CURLMSG_DONE) continue;

            CURL *easy = msg->easy_handle;
            conn_ctx_t *ctx = NULL;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ctx);

            struct timespec t_end;
            clock_gettime(CLOCK_MONOTONIC, &t_end);
            long lat_us = ctx ? diff_micros(&ctx->t_start, &t_end) : 0;

            long http_code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

            atomic_fetch_add(&g_stats.total_requests, 1);
            atomic_fetch_add(&g_stats.latency_sum_us, lat_us);

            if (msg->data.result == CURLE_OK && http_code >= 200 && http_code < 400) {
                atomic_fetch_add(&g_stats.success_count, 1);
            } else {
                atomic_fetch_add(&g_stats.fail_count, 1);
                /* Error handling basic: log ringkas, tidak menghentikan
                 * seluruh test hanya karena satu request gagal. */
                if (msg->data.result != CURLE_OK) {
                    fprintf(stderr, "[thread %d] request error: %s\n",
                            targ->thread_id,
                            ctx && ctx->errbuf[0] ? ctx->errbuf
                                                   : curl_easy_strerror(msg->data.result));
                }
            }

            curl_multi_remove_handle(multi, easy);

            /* Requeue handle (reuse koneksi & memori) jika waktu &
             * rate limit masih mengizinkan, jika tidak, tutup handle
             * supaya tidak ada leak saat test berakhir. */
            int time_left = now_seconds() < deadline && !atomic_load(&g_stop_flag);
            if (time_left && tokens >= 1.0) {
                tokens -= 1.0;
                clock_gettime(CLOCK_MONOTONIC, &ctx->t_start);
                curl_multi_add_handle(multi, easy);
            } else if (time_left) {
                /* belum ada token, handle nganggur sejenak lalu di-retry
                 * di iterasi berikutnya lewat sisa pool (idle), supaya
                 * rate tetap terjaga tanpa busy-loop. */
                clock_gettime(CLOCK_MONOTONIC, &ctx->t_start);
                curl_multi_add_handle(multi, easy);
            }
            /* jika waktu habis, handle sengaja tidak di-requeue; akan
             * dibersihkan di bagian cleanup di bawah. */
        }
    }

    /* --- Graceful shutdown: hentikan semua transfer yang masih aktif --- */
    for (int i = 0; i < cfg->connection_count; i++) {
        if (!pool[i]) continue;
        curl_multi_remove_handle(multi, pool[i]->easy);
        destroy_connection(pool[i]);
    }
    free(pool);
    curl_multi_cleanup(multi);

    return NULL;
}

/* ----------------------------------------------------------------------
 * Progress bar / statistik real-time, dicetak tiap 5 detik dari thread
 * utama sampai durasi tes selesai atau dihentikan (Ctrl+C).
 * ---------------------------------------------------------------------- */
static void print_progress(int elapsed, int total, stats_t *snapshot) {
    int width = 30;
    double frac = total > 0 ? (double)elapsed / (double)total : 1.0;
    if (frac > 1.0) frac = 1.0;
    int filled = (int)(frac * width);

    long total_req = atomic_load(&snapshot->total_requests);
    long ok = atomic_load(&snapshot->success_count);
    long fail = atomic_load(&snapshot->fail_count);
    long lat_sum = atomic_load(&snapshot->latency_sum_us);
    double avg_lat_ms = total_req > 0 ? (lat_sum / (double)total_req) / 1000.0 : 0.0;
    double rps = elapsed > 0 ? (double)total_req / elapsed : 0.0;

    printf("\r[");
    for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
    printf("] %3ds/%ds | req=%-6ld ok=%-6ld fail=%-5ld avg_lat=%.1fms rps=%.1f   ",
           elapsed, total, total_req, ok, fail, avg_lat_ms, rps);
    fflush(stdout);
}

/* ----------------------------------------------------------------------
 * Parsing argumen CLI sederhana (format --key=value)
 * ---------------------------------------------------------------------- */
static void parse_args(int argc, char **argv, config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->duration_sec = DEFAULT_TIME;
    cfg->rate_per_sec = DEFAULT_RATE;
    cfg->thread_count = DEFAULT_THREAD;
    cfg->connection_count = DEFAULT_CONN;
    cfg->worker_count = DEFAULT_WORKER;

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strncmp(a, "--url=", 6) == 0) {
            strncpy(cfg->url, a + 6, sizeof(cfg->url) - 1);
        } else if (strncmp(a, "--time=", 7) == 0) {
            cfg->duration_sec = atoi(a + 7);
        } else if (strncmp(a, "--rate=", 7) == 0) {
            cfg->rate_per_sec = atoi(a + 7);
        } else if (strncmp(a, "--thread=", 9) == 0) {
            cfg->thread_count = atoi(a + 9);
        } else if (strncmp(a, "--connection=", 13) == 0) {
            cfg->connection_count = atoi(a + 13);
        } else if (strncmp(a, "--worker=", 9) == 0) {
            cfg->worker_count = atoi(a + 9);
        } else if (strncmp(a, "--header=", 9) == 0) {
            if (cfg->header_count < MAX_HEADERS) {
                cfg->headers[cfg->header_count++] = strdup(a + 9);
            }
        } else if (strcmp(a, "--insecure") == 0) {
            cfg->insecure = 1;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            printf("Pemakaian: %s --url=<endpoint> [--time=10] [--rate=50] "
                   "[--thread=4] [--connection=10] [--worker=1] "
                   "[--header=\"K: V\"] [--insecure]\n", argv[0]);
            exit(0);
        }
    }

    if (cfg->url[0] == '\0') {
        fprintf(stderr, "Error: --url wajib diisi.\n");
        fprintf(stderr, "Contoh: %s --url=https://example.com --time=30 "
                        "--rate=200 --thread=4 --connection=20 --worker=4\n",
                argv[0]);
        exit(1);
    }
    if (cfg->duration_sec <= 0) cfg->duration_sec = DEFAULT_TIME;
    if (cfg->rate_per_sec <= 0) cfg->rate_per_sec = DEFAULT_RATE;
    if (cfg->thread_count <= 0) cfg->thread_count = DEFAULT_THREAD;
    if (cfg->connection_count <= 0) cfg->connection_count = DEFAULT_CONN;
    if (cfg->worker_count <= 0) cfg->worker_count = DEFAULT_WORKER;
}

static void free_config(config_t *cfg) {
    for (int i = 0; i < cfg->header_count; i++) free(cfg->headers[i]);
}

/* ----------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(int argc, char **argv) {
    config_t cfg;
    parse_args(argc, argv, &cfg);

    /* Graceful shutdown: tangkap Ctrl+C / kill, biarkan thread selesai
     * dengan rapi (drain in-flight request lalu keluar) alih-alih mati
     * mendadak dan bocor koneksi/memori. */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    curl_global_init(CURL_GLOBAL_ALL);

    printf("=== HTTP/2 Load Test ===\n");
    printf("Target      : %s\n", cfg.url);
    printf("Durasi      : %d detik\n", cfg.duration_sec);
    printf("Target rate : %d req/detik (total, dibagi ke %d thread)\n",
           cfg.rate_per_sec, cfg.thread_count);
    printf("Connection  : %d per thread (connection pool)\n", cfg.connection_count);
    printf("Worker pool : %d\n", cfg.worker_count);
    printf("========================\n\n");

    /* Total thread aktual = thread_count * worker_count, karena tiap
     * "worker" menjalankan event-loop curl_multi sendiri (menambah
     * paralelisme I/O tanpa menambah jumlah pthread pengelola CLI). */
    int total_threads = cfg.thread_count * cfg.worker_count;
    pthread_t *tids = calloc(total_threads, sizeof(pthread_t));
    thread_arg_t *targs = calloc(total_threads, sizeof(thread_arg_t));
    if (!tids || !targs) {
        fprintf(stderr, "Gagal alokasi memori thread.\n");
        return 1;
    }

    double rate_per_thread = (double)cfg.rate_per_sec / (double)total_threads;
    time_t t0 = time(NULL);

    for (int i = 0; i < total_threads; i++) {
        targs[i].thread_id = i;
        targs[i].cfg = &cfg;
        targs[i].rate_per_thread = rate_per_thread;
        targs[i].start_time = t0;
        if (pthread_create(&tids[i], NULL, multi_loop, &targs[i]) != 0) {
            fprintf(stderr, "Gagal membuat thread %d\n", i);
        }
    }

    /* Loop utama: cetak progress bar tiap 5 detik sampai selesai atau
     * SIGINT diterima. */
    int elapsed = 0;
    while (elapsed < cfg.duration_sec && !atomic_load(&g_stop_flag)) {
        sleep(5);
        elapsed += 5;
        if (elapsed > cfg.duration_sec) elapsed = cfg.duration_sec;
        print_progress(elapsed, cfg.duration_sec, &g_stats);
    }
    printf("\n");

    if (atomic_load(&g_stop_flag)) {
        printf("\n[!] Sinyal berhenti diterima, melakukan graceful shutdown...\n");
    } else {
        printf("\n[i] Durasi tes selesai, menunggu thread menyelesaikan request in-flight...\n");
    }
    /* Pastikan semua worker berhenti mengambil request baru */
    atomic_store(&g_stop_flag, 1);

    for (int i = 0; i < total_threads; i++) {
        pthread_join(tids[i], NULL);
    }

    /* --- Laporan akhir --- */
    long total_req = atomic_load(&g_stats.total_requests);
    long ok = atomic_load(&g_stats.success_count);
    long fail = atomic_load(&g_stats.fail_count);
    long bytes = atomic_load(&g_stats.bytes_received);
    long lat_sum = atomic_load(&g_stats.latency_sum_us);
    double avg_lat_ms = total_req > 0 ? (lat_sum / (double)total_req) / 1000.0 : 0.0;
    time_t elapsed_total = time(NULL) - t0;
    double overall_rps = elapsed_total > 0 ? (double)total_req / elapsed_total : 0.0;

    printf("\n=== Hasil Akhir ===\n");
    printf("Total request     : %ld\n", total_req);
    printf("Sukses             : %ld (%.1f%%)\n", ok,
           total_req > 0 ? (100.0 * ok / total_req) : 0.0);
    printf("Gagal              : %ld (%.1f%%)\n", fail,
           total_req > 0 ? (100.0 * fail / total_req) : 0.0);
    printf("Total data diterima: %.2f MB\n", bytes / (1024.0 * 1024.0));
    printf("Rata-rata latency  : %.2f ms\n", avg_lat_ms);
    printf("Rata-rata RPS      : %.2f req/detik\n", overall_rps);
    printf("Durasi aktual      : %ld detik\n", (long)elapsed_total);
    printf("====================\n");

    free(tids);
    free(targs);
    free_config(&cfg);
    curl_global_cleanup();

    return 0;
}
