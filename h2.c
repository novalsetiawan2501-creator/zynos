/*
 * ============================================================================
 *  loadtest.c — HTTP/2 Load Testing Tool (GET only)
 * ============================================================================
 *
 *  Tool load testing sederhana untuk menyimulasikan traffic tinggi ke sebuah
 *  endpoint HTTP/HTTPS dengan dukungan HTTP/2 multiplexing via libcurl.
 *
 *  Arsitektur singkat:
 *    - N thread jaringan ("--thread"), masing-masing punya 1 CURLM multi
 *      handle non-blocking (event loop sendiri).
 *    - Tiap thread memelihara sejumlah "koneksi" (--connection) berupa easy
 *      handle yang di-reuse terus menerus sepanjang durasi test (connection
 *      pooling asli, bukan bikin handle baru tiap request).
 *    - Rate limiting berbasis token bucket per-thread. Target rate efektif
 *      = rate * multiplier, dibagi rata ke semua thread.
 *    - Worker pool ("--worker") terpisah dari thread jaringan: hasil tiap
 *      request (sukses/gagal, latency, bytes) dikirim lewat antrian
 *      producer-consumer ke worker, supaya agregasi statistik & logging
 *      tidak membebani/blocking loop I/O jaringan.
 *    - Monitor thread mencetak statistik real-time tiap 1 detik, dan progress
 *      bar tiap 5 detik.
 *    - Graceful shutdown lewat SIGINT/SIGTERM (Ctrl+C) — semua thread berhenti
 *      rapi, semua resource dibersihkan (tidak ada memory leak).
 *
 *  Compile:
 *      gcc -O2 -Wall -o loadtest loadtest.c -lcurl -lpthread -lm
 *
 *  Contoh run:
 *      ./loadtest --url https://example.com/api/ping \
 *                 --time 30 --rate 100 --thread 4 \
 *                 --connection 20 --multiplier 2 --worker 4
 *
 *  Catatan:
 *      - Butuh libcurl yang dikompilasi dengan dukungan HTTP/2 (nghttp2).
 *        Cek dengan: curl-config --features   (harus muncul "HTTP2")
 *      - Program ini hanya untuk menguji server/infrastruktur milik sendiri.
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
#include <stdbool.h>
#include <curl/curl.h>

/* ------------------------------------------------------------------------ */
/*  Konfigurasi & konstanta                                                  */
/* ------------------------------------------------------------------------ */

#define DEFAULT_TIME        10
#define DEFAULT_RATE        50
#define DEFAULT_THREADS     4
#define DEFAULT_CONNECTIONS 10
#define DEFAULT_MULTIPLIER  1.0
#define DEFAULT_WORKERS     2
#define RESULT_QUEUE_CAP    65536   /* kapasitas antrian hasil request */

typedef struct {
    char   url[2048];
    int    duration;     /* --time         : detik */
    long   rate;         /* --rate         : request/detik (baseline) */
    int    threads;      /* --thread       : jumlah thread jaringan */
    int    connections;  /* --connection   : koneksi paralel per-thread */
    double multiplier;   /* --multipler    : pengkali rate efektif */
    int    workers;      /* --worker       : ukuran worker pool statistik */
} Config;

/* Hasil satu request, dikirim dari thread jaringan ke worker pool */
typedef struct {
    bool   success;
    long   http_code;
    double latency_ms;
    double bytes;
} RequestResult;

/* Antrian bundar (ring buffer) producer-consumer yang thread-safe */
typedef struct {
    RequestResult items[RESULT_QUEUE_CAP];
    size_t head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    atomic_bool     closed;
} ResultQueue;

/* Statistik global yang diagregasi oleh worker pool (atomic = aman diakses
 * lintas thread tanpa lock terpisah untuk pembacaan oleh monitor thread). */
typedef struct {
    atomic_long   total_requests;
    atomic_long   total_success;
    atomic_long   total_failed;
    atomic_long   dropped_results;   /* jika antrian penuh */
    atomic_ullong total_bytes;
    /* latency disimpan dalam microdetik dikali 1000 (fixed point) agar bisa
     * pakai atomic integer, dihindari race pada floating point */
    atomic_ullong latency_sum_us;
    atomic_long   latency_count;
} Stats;

static Config      g_cfg;
static Stats       g_stats;
static ResultQueue  g_queue;
static volatile sig_atomic_t g_running = 1;   /* flag graceful shutdown */
static struct timespec g_start_time;
static struct curl_slist *g_headers = NULL;   /* header dipakai bersama, read-only */

/* ------------------------------------------------------------------------ */
/*  Util waktu                                                               */
/* ------------------------------------------------------------------------ */

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double elapsed_since_start(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double a = (double)g_start_time.tv_sec + (double)g_start_time.tv_nsec / 1e9;
    double b = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return b - a;
}

/* ------------------------------------------------------------------------ */
/*  Signal handler — graceful shutdown                                       */
/* ------------------------------------------------------------------------ */

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0; /* semua thread akan mengecek flag ini dan berhenti rapi */
}

/* ------------------------------------------------------------------------ */
/*  Result queue: init, push (producer), pop (consumer), destroy             */
/* ------------------------------------------------------------------------ */

static void queue_init(ResultQueue *q) {
    q->head = q->tail = q->count = 0;
    atomic_init(&q->closed, false);
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void queue_destroy(ResultQueue *q) {
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

/* Producer (dipanggil dari thread jaringan). Non-blocking: kalau penuh,
 * result di-drop (dihitung di dropped_results) supaya thread jaringan
 * tidak pernah macet menunggu worker. */
static void queue_push(ResultQueue *q, RequestResult r) {
    pthread_mutex_lock(&q->lock);
    if (q->count >= RESULT_QUEUE_CAP) {
        pthread_mutex_unlock(&q->lock);
        atomic_fetch_add(&g_stats.dropped_results, 1);
        return;
    }
    q->items[q->tail] = r;
    q->tail = (q->tail + 1) % RESULT_QUEUE_CAP;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

/* Consumer (dipanggil dari worker thread). Return false kalau queue sudah
 * ditutup dan kosong (artinya waktunya worker berhenti). */
static bool queue_pop(ResultQueue *q, RequestResult *out) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !atomic_load(&q->closed)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; /* wake tiap 1 detik untuk re-check kondisi berhenti */
        pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
    }
    if (q->count == 0 && atomic_load(&q->closed)) {
        pthread_mutex_unlock(&q->lock);
        return false;
    }
    *out = q->items[q->head];
    q->head = (q->head + 1) % RESULT_QUEUE_CAP;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return true;
}

static void queue_close(ResultQueue *q) {
    pthread_mutex_lock(&q->lock);
    atomic_store(&q->closed, true);
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

/* ------------------------------------------------------------------------ */
/*  Worker pool — mengonsumsi hasil request & mengagregasi statistik          */
/* ------------------------------------------------------------------------ */

typedef struct {
    int id;
} WorkerArg;

static void *worker_thread_fn(void *arg) {
    WorkerArg *wa = (WorkerArg *)arg;
    RequestResult r;
    while (queue_pop(&g_queue, &r)) {
        atomic_fetch_add(&g_stats.total_requests, 1);
        if (r.success) {
            atomic_fetch_add(&g_stats.total_success, 1);
        } else {
            atomic_fetch_add(&g_stats.total_failed, 1);
        }
        atomic_fetch_add(&g_stats.total_bytes, (unsigned long long)r.bytes);
        atomic_fetch_add(&g_stats.latency_sum_us,
                          (unsigned long long)(r.latency_ms * 1000.0));
        atomic_fetch_add(&g_stats.latency_count, 1);
    }
    free(wa);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/*  libcurl write callback — buang body, hanya hitung ukurannya               */
/*  (menghindari penumpukan memori untuk response besar / long-running test) */
/* ------------------------------------------------------------------------ */

static size_t discard_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb; /* return jumlah byte yang "diterima" tanpa disimpan */
}

/* ------------------------------------------------------------------------ */
/*  Per-koneksi (easy handle) yang dipakai berulang selama test berjalan     */
/* ------------------------------------------------------------------------ */

typedef struct {
    CURL  *easy;
    double start_time;   /* waktu request ini dimulai (untuk latency) */
    bool   in_flight;
} Connection;

typedef struct {
    int         thread_id;
    Config     *cfg;
    long        per_thread_rate; /* target request/detik untuk thread ini */
} ThreadArg;

/* Konfigurasi ulang 1 easy handle supaya siap dipakai untuk request
 * berikutnya (connection tetap di-reuse oleh libcurl connection cache milik
 * multi handle — bukan re-create dari nol). */
static void arm_connection(CURLM *multi, Connection *conn, const char *url) {
    curl_easy_setopt(conn->easy, CURLOPT_URL, url);
    conn->start_time = now_seconds();
    conn->in_flight = true;
    curl_multi_add_handle(multi, conn->easy);
}

static void *network_thread_fn(void *arg) {
    ThreadArg *targ = (ThreadArg *)arg;
    Config *cfg = targ->cfg;

    CURLM *multi = curl_multi_init();
    if (!multi) {
        fprintf(stderr, "[thread %d] gagal membuat CURLM multi handle\n", targ->thread_id);
        return NULL;
    }

    /* Aktifkan HTTP/2 multiplexing di level multi handle */
    curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, (long)cfg->connections);
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)cfg->connections);

    /* Siapkan pool koneksi (easy handle) yang akan dipakai berulang */
    Connection *pool = calloc((size_t)cfg->connections, sizeof(Connection));
    if (!pool) {
        curl_multi_cleanup(multi);
        fprintf(stderr, "[thread %d] gagal alokasi connection pool\n", targ->thread_id);
        return NULL;
    }

    for (int i = 0; i < cfg->connections; i++) {
        CURL *e = curl_easy_init();
        pool[i].easy = e;
        pool[i].in_flight = false;

        curl_easy_setopt(e, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(e, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, discard_write_cb);
        curl_easy_setopt(e, CURLOPT_HTTPHEADER, g_headers);
        curl_easy_setopt(e, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, ""); /* aktifkan semua encoding didukung */
        curl_easy_setopt(e, CURLOPT_PRIVATE, &pool[i]);
    }

    /* Token bucket rate limiter per-thread */
    double interval = (targ->per_thread_rate > 0)
                           ? (1.0 / (double)targ->per_thread_rate)
                           : 0.0;
    double next_allowed = now_seconds();

    /* Nyalakan semua koneksi di awal supaya langsung ada traffic in-flight */
    for (int i = 0; i < cfg->connections; i++) {
        arm_connection(multi, &pool[i], cfg->url);
        next_allowed += interval;
    }

    int still_running = 0;
    curl_multi_perform(multi, &still_running);

    while (g_running && elapsed_since_start() < (double)cfg->duration) {
        int numfds = 0;
        /* curl_multi_poll = non-blocking I/O dengan timeout pendek, supaya
         * loop tetap responsif terhadap sinyal shutdown & rate limiter */
        curl_multi_poll(multi, NULL, 0, 100, &numfds);
        curl_multi_perform(multi, &still_running);

        /* Proses handle yang sudah selesai */
        int msgs_left = 0;
        CURLMsg *msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
            if (msg->msg != CURLMSG_DONE) continue;

            CURL *e = msg->easy_handle;
            Connection *conn = NULL;
            curl_easy_getinfo(e, CURLINFO_PRIVATE, &conn);

            RequestResult res = {0};
            long http_code = 0;
            curl_off_t dl_bytes = 0;
            curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_getinfo(e, CURLINFO_SIZE_DOWNLOAD_T, &dl_bytes);

            res.success = (msg->data.result == CURLE_OK && http_code >= 200 && http_code < 400);
            res.http_code = http_code;
            res.bytes = (double)dl_bytes;
            res.latency_ms = (now_seconds() - conn->start_time) * 1000.0;

            queue_push(&g_queue, res);

            curl_multi_remove_handle(multi, e);
            conn->in_flight = false;

            /* Rate limiting: tunggu giliran sesuai token bucket sebelum
             * menembak request berikutnya lewat koneksi yang sama */
            double t = now_seconds();
            if (interval > 0.0 && t < next_allowed) {
                struct timespec req = {
                    .tv_sec = (time_t)(next_allowed - t),
                    .tv_nsec = (long)(((next_allowed - t) - (time_t)(next_allowed - t)) * 1e9)
                };
                if (req.tv_sec > 0 || req.tv_nsec > 0) nanosleep(&req, NULL);
            }
            if (!g_running || elapsed_since_start() >= (double)cfg->duration) break;

            arm_connection(multi, conn, cfg->url);
            next_allowed += interval;
        }
    }

    /* --- Graceful shutdown untuk thread ini --- */
    for (int i = 0; i < cfg->connections; i++) {
        if (pool[i].in_flight) {
            curl_multi_remove_handle(multi, pool[i].easy);
        }
        curl_easy_cleanup(pool[i].easy);
    }
    free(pool);
    curl_multi_cleanup(multi);
    free(targ);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/*  Monitor thread — statistik real-time & progress bar tiap 5 detik         */
/* ------------------------------------------------------------------------ */

static void print_progress_bar(double elapsed, double total) {
    int width = 30;
    double frac = elapsed / total;
    if (frac > 1.0) frac = 1.0;
    int filled = (int)(frac * width);

    printf("[");
    for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
    printf("] %5.1f%% (%.0f/%.0fs)\n", frac * 100.0, elapsed, total);
}

static void *monitor_thread_fn(void *arg) {
    (void)arg;
    long last_total = 0;
    int tick = 0;

    while (g_running && elapsed_since_start() < (double)g_cfg.duration) {
        sleep(1);
        tick++;

        long total   = atomic_load(&g_stats.total_requests);
        long success = atomic_load(&g_stats.total_success);
        long failed  = atomic_load(&g_stats.total_failed);
        long dropped = atomic_load(&g_stats.dropped_results);
        unsigned long long bytes = atomic_load(&g_stats.total_bytes);
        long lat_count = atomic_load(&g_stats.latency_count);
        unsigned long long lat_sum = atomic_load(&g_stats.latency_sum_us);
        double avg_latency_ms = (lat_count > 0) ? ((double)lat_sum / 1000.0) / (double)lat_count : 0.0;

        long current_rps = total - last_total;
        last_total = total;

        printf("\r\033[K[t=%3ds] req=%-7ld ok=%-7ld fail=%-6ld drop=%-4ld rps=%-5ld avg_latency=%.1fms bytes=%.1fMB",
               (int)elapsed_since_start(), total, success, failed, dropped,
               current_rps, avg_latency_ms, bytes / (1024.0 * 1024.0));
        fflush(stdout);

        if (tick % 5 == 0) {
            printf("\n");
            print_progress_bar(elapsed_since_start(), (double)g_cfg.duration);
        }
    }
    printf("\n");
    return NULL;
}

/* ------------------------------------------------------------------------ */
/*  Argument parsing                                                         */
/* ------------------------------------------------------------------------ */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Pemakaian: %s --url <URL> [opsi]\n\n"
        "Opsi:\n"
        "  --url <url>          Target endpoint (wajib)\n"
        "  --time <detik>        Durasi pengujian (default %d)\n"
        "  --rate <n>            Request per detik baseline (default %d)\n"
        "  --thread <n>          Jumlah thread jaringan (default %d)\n"
        "  --connection <n>      Koneksi paralel per-thread (default %d)\n"
        "  --multipler <x>       Pengkali rate efektif (default %.1f)\n"
        "  --worker <n>          Ukuran worker pool statistik (default %d)\n"
        "  --help                Tampilkan bantuan ini\n",
        prog, DEFAULT_TIME, DEFAULT_RATE, DEFAULT_THREADS, DEFAULT_CONNECTIONS,
        DEFAULT_MULTIPLIER, DEFAULT_WORKERS);
}

static void parse_args(int argc, char **argv, Config *cfg) {
    cfg->url[0] = '\0';
    cfg->duration    = DEFAULT_TIME;
    cfg->rate        = DEFAULT_RATE;
    cfg->threads     = DEFAULT_THREADS;
    cfg->connections = DEFAULT_CONNECTIONS;
    cfg->multiplier  = DEFAULT_MULTIPLIER;
    cfg->workers     = DEFAULT_WORKERS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            strncpy(cfg->url, argv[++i], sizeof(cfg->url) - 1);
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            cfg->duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            cfg->rate = atol(argv[++i]);
        } else if (strcmp(argv[i], "--thread") == 0 && i + 1 < argc) {
            cfg->threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--connection") == 0 && i + 1 < argc) {
            cfg->connections = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--multipler") == 0 || strcmp(argv[i], "--multiplier") == 0) && i + 1 < argc) {
            cfg->multiplier = atof(argv[++i]);
        } else if (strcmp(argv[i], "--worker") == 0 && i + 1 < argc) {
            cfg->workers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Argumen tidak dikenal: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }

    if (cfg->url[0] == '\0') {
        fprintf(stderr, "Error: --url wajib diisi.\n\n");
        print_usage(argv[0]);
        exit(1);
    }
    if (cfg->duration <= 0 || cfg->rate <= 0 || cfg->threads <= 0 ||
        cfg->connections <= 0 || cfg->workers <= 0 || cfg->multiplier <= 0) {
        fprintf(stderr, "Error: semua parameter numerik harus lebih besar dari 0.\n");
        exit(1);
    }
}

/* ------------------------------------------------------------------------ */
/*  main                                                                      */
/* ------------------------------------------------------------------------ */

int main(int argc, char **argv) {
    parse_args(argc, argv, &g_cfg);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    queue_init(&g_queue);

    atomic_init(&g_stats.total_requests, 0);
    atomic_init(&g_stats.total_success, 0);
    atomic_init(&g_stats.total_failed, 0);
    atomic_init(&g_stats.dropped_results, 0);
    atomic_init(&g_stats.total_bytes, 0);
    atomic_init(&g_stats.latency_sum_us, 0);
    atomic_init(&g_stats.latency_count, 0);

    /* Header HTTP sederhana, dibuat sekali & dipakai bersama (read-only)
     * oleh semua easy handle di semua thread. */
    g_headers = curl_slist_append(g_headers, "User-Agent: C-LoadTester/1.0");
    g_headers = curl_slist_append(g_headers, "Accept: */*");
    g_headers = curl_slist_append(g_headers, "Connection: keep-alive");

    double effective_rate = (double)g_cfg.rate * g_cfg.multiplier;
    long per_thread_rate = (long)(effective_rate / (double)g_cfg.threads);
    if (per_thread_rate < 1) per_thread_rate = 1;

    printf("=== Load Testing Tool (HTTP/2, libcurl) ===\n");
    printf("Target       : %s\n", g_cfg.url);
    printf("Durasi       : %d detik\n", g_cfg.duration);
    printf("Rate         : %ld req/s x multiplier %.2f = %.0f req/s efektif\n",
           g_cfg.rate, g_cfg.multiplier, effective_rate);
    printf("Thread       : %d (~%ld req/s per thread)\n", g_cfg.threads, per_thread_rate);
    printf("Connection   : %d koneksi paralel per thread\n", g_cfg.connections);
    printf("Worker pool  : %d\n", g_cfg.workers);
    printf("Tekan Ctrl+C untuk menghentikan lebih awal (graceful shutdown)\n\n");

    clock_gettime(CLOCK_MONOTONIC, &g_start_time);

    /* --- Start worker pool --- */
    pthread_t *worker_ids = calloc((size_t)g_cfg.workers, sizeof(pthread_t));
    for (int i = 0; i < g_cfg.workers; i++) {
        WorkerArg *wa = malloc(sizeof(WorkerArg));
        wa->id = i;
        pthread_create(&worker_ids[i], NULL, worker_thread_fn, wa);
    }

    /* --- Start network threads --- */
    pthread_t *net_ids = calloc((size_t)g_cfg.threads, sizeof(pthread_t));
    for (int i = 0; i < g_cfg.threads; i++) {
        ThreadArg *ta = malloc(sizeof(ThreadArg));
        ta->thread_id = i;
        ta->cfg = &g_cfg;
        ta->per_thread_rate = per_thread_rate;
        pthread_create(&net_ids[i], NULL, network_thread_fn, ta);
    }

    /* --- Start monitor thread --- */
    pthread_t monitor_id;
    pthread_create(&monitor_id, NULL, monitor_thread_fn, NULL);

    /* Tunggu semua thread jaringan selesai (baik karena durasi habis atau
     * karena sinyal shutdown) */
    for (int i = 0; i < g_cfg.threads; i++) {
        pthread_join(net_ids[i], NULL);
    }
    pthread_join(monitor_id, NULL);

    /* Semua request sudah selesai dikirim -> tutup antrian, worker akan
     * berhenti begitu antrian benar-benar kosong */
    queue_close(&g_queue);
    for (int i = 0; i < g_cfg.workers; i++) {
        pthread_join(worker_ids[i], NULL);
    }

    free(net_ids);
    free(worker_ids);

    /* --- Ringkasan akhir --- */
    long total   = atomic_load(&g_stats.total_requests);
    long success = atomic_load(&g_stats.total_success);
    long failed  = atomic_load(&g_stats.total_failed);
    long dropped = atomic_load(&g_stats.dropped_results);
    unsigned long long bytes = atomic_load(&g_stats.total_bytes);
    long lat_count = atomic_load(&g_stats.latency_count);
    unsigned long long lat_sum = atomic_load(&g_stats.latency_sum_us);
    double avg_latency_ms = (lat_count > 0) ? ((double)lat_sum / 1000.0) / (double)lat_count : 0.0;
    double real_duration = elapsed_since_start();

    printf("\n=== Ringkasan Hasil ===\n");
    printf("Durasi aktual     : %.1f detik\n", real_duration);
    printf("Total request     : %ld\n", total);
    printf("Sukses            : %ld (%.1f%%)\n", success, total ? 100.0 * success / total : 0.0);
    printf("Gagal             : %ld (%.1f%%)\n", failed, total ? 100.0 * failed / total : 0.0);
    printf("Drop (queue penuh): %ld\n", dropped);
    printf("Rata-rata latency : %.1f ms\n", avg_latency_ms);
    printf("Throughput        : %.1f req/s\n", real_duration > 0 ? total / real_duration : 0.0);
    printf("Total data        : %.2f MB\n", bytes / (1024.0 * 1024.0));

    /* --- Cleanup global --- */
    curl_slist_free_all(g_headers);
    queue_destroy(&g_queue);
    curl_global_cleanup();

    return 0;
}
