/*
 * ============================================================================
 *  h2loadtest.c — HTTP/2 Load Testing Tool (libcurl multi + pthread)
 * ============================================================================
 *
 *  Simulasi traffic tinggi ke satu endpoint HTTP/2 dengan method GET.
 *  Menggunakan libcurl (dibangun dengan dukungan nghttp2) untuk HTTP/2,
 *  pthread untuk concurrency, dan curl "multi interface" untuk non-blocking
 *  I/O + connection pooling per thread.
 *
 *  ---------------------------------------------------------------------
 *  PEMETAAN PARAMETER (penting dibaca dulu, supaya hasilnya sesuai harapan)
 *  ---------------------------------------------------------------------
 *   -u / --url         target endpoint (wajib)
 *   -t / --time        durasi pengujian, dalam detik
 *   -r / --rate        target total request/detik (dibagi rata ke semua thread)
 *   -T / --thread      jumlah pthread worker yang berjalan paralel
 *   -c / --connection  jumlah koneksi (easy handle) TOTAL yang di-pool.
 *                       Akan dibagi rata ke tiap thread (min. 1 per thread).
 *   -w / --worker      ukuran "worker pool" per thread di dalam curl_multi
 *                       -> berapa banyak transfer yang boleh in-flight
 *                          bersamaan per thread (kalau lebih besar dari
 *                          connection-per-thread, curl akan reuse socket
 *                          sesuai connection pooling libcurl).
 *
 *  Total transfer concurrent secara teori ~= thread * worker (dibatasi lagi
 *  oleh jumlah connection yang di-set sebagai batas CURLMOPT_MAX_TOTAL_CONNECTIONS
 *  per multi handle).
 *
 *  ---------------------------------------------------------------------
 *  COMPILE
 *  ---------------------------------------------------------------------
 *  Butuh libcurl yang di-build dengan HTTP/2 (nghttp2). Cek dengan:
 *      curl --version | grep -i http2
 *
 *  Compile:
 *      gcc -O2 -Wall -o h2loadtest h2loadtest.c -lcurl -lpthread -lm
 *
 *  ---------------------------------------------------------------------
 *  CONTOH RUN
 *  ---------------------------------------------------------------------
 *      ./h2loadtest -u https://example.com/api/ping \
 *                   -t 30 -r 500 -T 4 -c 100 -w 25
 *
 *  Artinya: tes 30 detik, target 500 req/detik total, 4 thread,
 *  100 koneksi paralel (di-bagi ~25/thread), worker pool 25 in-flight/thread.
 *
 *  Tekan Ctrl+C kapan saja untuk graceful shutdown (statistik akhir tetap
 *  dicetak, semua handle curl dibersihkan dengan benar -> no memory leak).
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <curl/curl.h>

/* ------------------------------------------------------------------------ */
/* Struktur konfigurasi global (diisi dari argumen CLI)                      */
/* ------------------------------------------------------------------------ */
typedef struct {
    char     url[2048];
    int      duration_sec;
    int      target_rate;      /* total request/detik yang diinginkan       */
    int      thread_count;
    int      connection_count; /* total koneksi paralel                     */
    int      worker_pool;      /* in-flight transfer per thread              */
} config_t;

/* ------------------------------------------------------------------------ */
/* Statistik global, semua counter atomic supaya aman diakses banyak thread  */
/* ------------------------------------------------------------------------ */
typedef struct {
    atomic_ullong total_requests;
    atomic_ullong success_count;
    atomic_ullong error_count;
    atomic_ullong bytes_received;
    atomic_ullong latency_sum_us;   /* akumulasi latency (mikrodetik)        */
} stats_t;

static stats_t g_stats;
static config_t g_config;

/* Flag graceful shutdown, diset oleh signal handler.                       */
static volatile sig_atomic_t g_running = 1;

/* Waktu mulai tes, dipakai untuk hitung elapsed & rate limiting global.     */
static struct timespec g_start_time;

/* ------------------------------------------------------------------------ */
/* Handler SIGINT/SIGTERM -> graceful shutdown                              */
/* ------------------------------------------------------------------------ */
static void signal_handler(int signo) {
    (void)signo;
    g_running = 0;
}

/* ------------------------------------------------------------------------ */
/* Util: selisih waktu dalam detik (double)                                  */
/* ------------------------------------------------------------------------ */
static double time_diff_sec(struct timespec *a, struct timespec *b) {
    return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) / 1e9;
}

/* ------------------------------------------------------------------------ */
/* Callback penerima body response. Kita tidak butuh isinya, cukup hitung    */
/* jumlah byte agar tidak ada memory build-up (dibuang, bukan disimpan).     */
/* ------------------------------------------------------------------------ */
static size_t discard_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    size_t real_size = size * nmemb;
    atomic_fetch_add(&g_stats.bytes_received, real_size);
    return real_size; /* wajib return jumlah byte yang "diproses" */
}

/* ------------------------------------------------------------------------ */
/* Info per-easy-handle yang kita simpan via CURLOPT_PRIVATE, supaya saat    */
/* transfer selesai kita bisa hitung latency & langsung reuse handle-nya     */
/* (ini bagian dari "connection pooling": easy handle + koneksi di-reuse,    */
/* tidak dibuat/dihancurkan berulang kali).                                  */
/* ------------------------------------------------------------------------ */
typedef struct {
    struct timespec start_ts;
} req_ctx_t;

/* ------------------------------------------------------------------------ */
/* Header HTTP sederhana dibuat SEKALI secara global dan dipakai ulang oleh  */
/* semua request (isinya statis) -> menghindari alokasi/leak berulang.      */
/* ------------------------------------------------------------------------ */
static struct curl_slist *g_shared_headers = NULL;

static void build_shared_headers(void) {
    g_shared_headers = curl_slist_append(g_shared_headers, "User-Agent: h2loadtest/1.0");
    g_shared_headers = curl_slist_append(g_shared_headers, "Accept: */*");
}

/* ------------------------------------------------------------------------ */
/* Reset & mulai ulang satu easy handle untuk request baru (reuse koneksi). */
/* Dipanggil sekali saat easy handle pertama kali dibuat, dan setiap kali    */
/* handle selesai lalu di-requeue untuk request berikutnya (connection      */
/* pooling: socket/koneksi TCP+TLS yang sama dipakai ulang oleh libcurl).    */
/* ------------------------------------------------------------------------ */
static void requeue_handle(CURL *easy, req_ctx_t *ctx) {
    clock_gettime(CLOCK_MONOTONIC, &ctx->start_ts);
    curl_easy_setopt(easy, CURLOPT_URL, g_config.url);
    curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, discard_write_cb);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, ""); /* terima semua encoding */
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, g_shared_headers); /* reuse, no realloc */
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);
}

/* ------------------------------------------------------------------------ */
/* Argumen per-thread                                                        */
/* ------------------------------------------------------------------------ */
typedef struct {
    int thread_id;
    int connections_for_this_thread;
    double per_thread_rate; /* target req/detik untuk thread ini */
} thread_arg_t;

/* ------------------------------------------------------------------------ */
/* Fungsi utama tiap thread worker:                                          */
/*  - Membuat CURLM (multi handle) + N easy handle (connection pool)         */
/*  - Loop non-blocking pakai curl_multi_perform + curl_multi_wait           */
/*  - Rate limiting sederhana: hitung berapa request "seharusnya" sudah      */
/*    dikirim sejak start, kalau kurang -> requeue handle idle secepatnya,   */
/*    kalau sudah cukup -> tunggu sebentar (non-busy-wait, pakai wait di     */
/*    curl_multi_wait dengan timeout kecil).                                 */
/* ------------------------------------------------------------------------ */
static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    int n_conn = targ->connections_for_this_thread;
    if (n_conn < 1) n_conn = 1;

    CURLM *multi = curl_multi_init();
    if (!multi) {
        fprintf(stderr, "[thread %d] gagal membuat curl_multi handle\n", targ->thread_id);
        return NULL;
    }

    /* Batasi jumlah koneksi real yang dipakai multi handle ini -> ini yang
       memastikan "connection pooling" sesuai parameter -c (connection).   */
    curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)n_conn);
    curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, (long)n_conn);
    /* Aktifkan HTTP/2 multiplexing di level multi handle juga. */
    curl_multi_setopt(multi, CURLMOPT_PIPELINING, (long)CURLPIPE_MULTIPLEX);

    /* worker_pool menentukan berapa banyak easy handle in-flight bersamaan
       yang kita kelola per thread. Idealnya <= n_conn * (multiplex factor),
       tapi kita cap sederhana: minimal n_conn, maksimal worker_pool.        */
    int n_handles = targ->connections_for_this_thread;
    if (g_config.worker_pool > n_handles) n_handles = g_config.worker_pool;
    if (n_handles < 1) n_handles = 1;

    CURL **easy_handles = calloc((size_t)n_handles, sizeof(CURL *));
    req_ctx_t *ctxs = calloc((size_t)n_handles, sizeof(req_ctx_t));
    if (!easy_handles || !ctxs) {
        fprintf(stderr, "[thread %d] gagal alokasi memori\n", targ->thread_id);
        free(easy_handles);
        free(ctxs);
        curl_multi_cleanup(multi);
        return NULL;
    }

    for (int i = 0; i < n_handles; i++) {
        easy_handles[i] = curl_easy_init();
        if (!easy_handles[i]) continue;
        requeue_handle(easy_handles[i], &ctxs[i]);
        curl_multi_add_handle(multi, easy_handles[i]);
    }

    unsigned long long sent_by_this_thread = 0;
    struct timespec now;

    while (g_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = time_diff_sec(&g_start_time, &now);
        if (g_config.duration_sec > 0 && elapsed >= g_config.duration_sec) break;

        /* --- Non-blocking I/O: proses socket yang sudah siap --- */
        int still_running = 0;
        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) {
            fprintf(stderr, "[thread %d] curl_multi_perform error: %s\n",
                    targ->thread_id, curl_multi_strerror(mc));
        }

        /* --- Ambil hasil transfer yang sudah selesai --- */
        int msgs_left = 0;
        CURLMsg *msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
            if (msg->msg != CURLMSG_DONE) continue;

            CURL *easy = msg->easy_handle;
            req_ctx_t *ctx = NULL;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ctx);

            atomic_fetch_add(&g_stats.total_requests, 1);

            if (msg->data.result == CURLE_OK) {
                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
                if (http_code >= 200 && http_code < 400) {
                    atomic_fetch_add(&g_stats.success_count, 1);
                } else {
                    atomic_fetch_add(&g_stats.error_count, 1);
                }
            } else {
                /* error_handling basic: koneksi gagal / timeout / dll */
                atomic_fetch_add(&g_stats.error_count, 1);
            }

            if (ctx) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                double lat_sec = time_diff_sec(&ctx->start_ts, &now);
                atomic_fetch_add(&g_stats.latency_sum_us,
                                  (unsigned long long)(lat_sec * 1e6));
            }

            curl_multi_remove_handle(multi, easy);

            /* --- Rate limiting per thread: cek apakah kita "boleh" kirim
                   request baru berdasarkan target_rate. Kalau belum waktunya,
                   simpan handle dan tunda requeue sedikit lewat sleep kecil
                   di akhir loop (bukan busy loop). --- */
            clock_gettime(CLOCK_MONOTONIC, &now);
            double now_elapsed = time_diff_sec(&g_start_time, &now);
            unsigned long long expected_sent =
                (unsigned long long)(targ->per_thread_rate * now_elapsed);

            if (targ->per_thread_rate <= 0 || sent_by_this_thread < expected_sent
                || !g_running) {
                /* Boleh langsung kirim ulang (reuse connection & handle) */
                if (g_running) {
                    requeue_handle(easy, ctx);
                    curl_multi_add_handle(multi, easy);
                    sent_by_this_thread++;
                } else {
                    curl_easy_cleanup(easy);
                }
            } else {
                /* Belum waktunya, tunda sebentar lalu requeue supaya rate
                   tetap sesuai target (menghindari burst berlebihan). */
                struct timespec ts = {0, 2 * 1000 * 1000}; /* 2ms */
                nanosleep(&ts, NULL);
                if (g_running) {
                    requeue_handle(easy, ctx);
                    curl_multi_add_handle(multi, easy);
                    sent_by_this_thread++;
                } else {
                    curl_easy_cleanup(easy);
                }
            }
        }

        /* Tunggu I/O event berikutnya (non-blocking, dengan timeout kecil
           supaya kita tetap responsif terhadap g_running / durasi tes). */
        int numfds = 0;
        curl_multi_wait(multi, NULL, 0, 100 /* ms */, &numfds);
        if (numfds == 0) {
            /* Tidak ada socket siap -> hindari busy-spin murni. */
            struct timespec ts = {0, 1 * 1000 * 1000}; /* 1ms */
            nanosleep(&ts, NULL);
        }
    }

    /* ---------------- Graceful shutdown & cleanup handle ---------------- */
    for (int i = 0; i < n_handles; i++) {
        if (!easy_handles[i]) continue;
        curl_multi_remove_handle(multi, easy_handles[i]);
        curl_easy_cleanup(easy_handles[i]);
    }
    free(easy_handles);
    free(ctxs);
    curl_multi_cleanup(multi);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Thread terpisah untuk mencetak statistik real-time + progress bar        */
/* setiap 5 detik.                                                           */
/* ------------------------------------------------------------------------ */
static void print_progress_bar(double elapsed, double total) {
    int width = 30;
    double frac = total > 0 ? elapsed / total : 0;
    if (frac > 1.0) frac = 1.0;
    int filled = (int)(frac * width);

    printf("\r[");
    for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
    printf("] %5.1f%%", frac * 100.0);
}

static void *stats_thread(void *arg) {
    (void)arg;
    struct timespec now;

    while (g_running) {
        sleep(5);
        if (!g_running) break;

        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = time_diff_sec(&g_start_time, &now);

        unsigned long long total = atomic_load(&g_stats.total_requests);
        unsigned long long ok    = atomic_load(&g_stats.success_count);
        unsigned long long err   = atomic_load(&g_stats.error_count);
        unsigned long long bytes = atomic_load(&g_stats.bytes_received);
        unsigned long long lat_sum = atomic_load(&g_stats.latency_sum_us);

        double rps = elapsed > 0 ? (double)total / elapsed : 0;
        double avg_lat_ms = total > 0 ? (lat_sum / (double)total) / 1000.0 : 0;

        printf("\n--- Statistik @ %.0fs ---\n", elapsed);
        printf("Total   : %llu req | Sukses: %llu | Error: %llu\n",
               total, ok, err);
        printf("Rate    : %.1f req/s (target: %d req/s)\n", rps, g_config.target_rate);
        printf("Latency : rata-rata %.2f ms\n", avg_lat_ms);
        printf("Data    : %.2f MB diterima\n", bytes / (1024.0 * 1024.0));

        print_progress_bar(elapsed, g_config.duration_sec);
        printf("\n");
        fflush(stdout);
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Cetak laporan akhir setelah tes selesai / dihentikan                     */
/* ------------------------------------------------------------------------ */
static void print_final_report(double elapsed) {
    unsigned long long total = atomic_load(&g_stats.total_requests);
    unsigned long long ok    = atomic_load(&g_stats.success_count);
    unsigned long long err   = atomic_load(&g_stats.error_count);
    unsigned long long bytes = atomic_load(&g_stats.bytes_received);
    unsigned long long lat_sum = atomic_load(&g_stats.latency_sum_us);

    double rps = elapsed > 0 ? (double)total / elapsed : 0;
    double avg_lat_ms = total > 0 ? (lat_sum / (double)total) / 1000.0 : 0;
    double success_rate = total > 0 ? (100.0 * ok / total) : 0;

    printf("\n\n========== LAPORAN AKHIR ==========\n");
    printf("URL              : %s\n", g_config.url);
    printf("Durasi aktual    : %.2f detik\n", elapsed);
    printf("Total request    : %llu\n", total);
    printf("Sukses           : %llu (%.2f%%)\n", ok, success_rate);
    printf("Error            : %llu\n", err);
    printf("Rata-rata RPS    : %.2f req/s\n", rps);
    printf("Latency rata-rata: %.2f ms\n", avg_lat_ms);
    printf("Total data       : %.2f MB\n", bytes / (1024.0 * 1024.0));
    printf("====================================\n");
}

/* ------------------------------------------------------------------------ */
/* Parsing argumen CLI sederhana                                             */
/* ------------------------------------------------------------------------ */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Pemakaian: %s -u <url> -t <detik> -r <rate> -T <thread> -c <connection> -w <worker>\n"
        "Contoh   : %s -u https://example.com -t 30 -r 500 -T 4 -c 100 -w 25\n",
        prog, prog);
}

static void parse_args(int argc, char **argv, config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    /* Nilai default supaya tool tetap jalan kalau user lupa isi sebagian. */
    cfg->duration_sec = 10;
    cfg->target_rate = 100;
    cfg->thread_count = 2;
    cfg->connection_count = 10;
    cfg->worker_pool = 10;

    int opt;
    int has_url = 0;
    while ((opt = getopt(argc, argv, "u:t:r:T:c:w:h")) != -1) {
        switch (opt) {
            case 'u':
                strncpy(cfg->url, optarg, sizeof(cfg->url) - 1);
                has_url = 1;
                break;
            case 't': cfg->duration_sec = atoi(optarg); break;
            case 'r': cfg->target_rate = atoi(optarg); break;
            case 'T': cfg->thread_count = atoi(optarg); break;
            case 'c': cfg->connection_count = atoi(optarg); break;
            case 'w': cfg->worker_pool = atoi(optarg); break;
            case 'h':
            default:
                print_usage(argv[0]);
                exit(opt == 'h' ? 0 : 1);
        }
    }

    if (!has_url) {
        fprintf(stderr, "Error: parameter -u (url) wajib diisi.\n");
        print_usage(argv[0]);
        exit(1);
    }
    if (cfg->thread_count < 1) cfg->thread_count = 1;
    if (cfg->connection_count < cfg->thread_count) cfg->connection_count = cfg->thread_count;
    if (cfg->worker_pool < 1) cfg->worker_pool = 1;
}

/* ------------------------------------------------------------------------ */
/* main                                                                      */
/* ------------------------------------------------------------------------ */
int main(int argc, char **argv) {
    parse_args(argc, argv, &g_config);

    /* Pasang signal handler untuk graceful shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    curl_global_init(CURL_GLOBAL_ALL);
    build_shared_headers();

    atomic_init(&g_stats.total_requests, 0ULL);
    atomic_init(&g_stats.success_count, 0ULL);
    atomic_init(&g_stats.error_count, 0ULL);
    atomic_init(&g_stats.bytes_received, 0ULL);
    atomic_init(&g_stats.latency_sum_us, 0ULL);

    printf("=== h2loadtest — HTTP/2 Load Testing Tool ===\n");
    printf("Target     : %s\n", g_config.url);
    printf("Durasi     : %d detik\n", g_config.duration_sec);
    printf("Target rate: %d req/detik\n", g_config.target_rate);
    printf("Thread     : %d\n", g_config.thread_count);
    printf("Connection : %d (total)\n", g_config.connection_count);
    printf("Worker pool: %d (per-thread in-flight)\n", g_config.worker_pool);
    printf("Tekan Ctrl+C untuk menghentikan lebih awal (graceful shutdown)\n\n");

    clock_gettime(CLOCK_MONOTONIC, &g_start_time);

    /* Distribusi connection & rate ke tiap thread secara merata. */
    pthread_t *threads = calloc((size_t)g_config.thread_count, sizeof(pthread_t));
    thread_arg_t *targs = calloc((size_t)g_config.thread_count, sizeof(thread_arg_t));
    if (!threads || !targs) {
        fprintf(stderr, "Gagal alokasi memori untuk thread.\n");
        return 1;
    }

    int base_conn = g_config.connection_count / g_config.thread_count;
    int extra_conn = g_config.connection_count % g_config.thread_count;
    double base_rate = (double)g_config.target_rate / g_config.thread_count;

    for (int i = 0; i < g_config.thread_count; i++) {
        targs[i].thread_id = i;
        targs[i].connections_for_this_thread = base_conn + (i < extra_conn ? 1 : 0);
        targs[i].per_thread_rate = base_rate;
        if (pthread_create(&threads[i], NULL, worker_thread, &targs[i]) != 0) {
            fprintf(stderr, "Gagal membuat thread %d\n", i);
        }
    }

    /* Thread khusus statistik real-time */
    pthread_t stat_tid;
    pthread_create(&stat_tid, NULL, stats_thread, NULL);

    /* Thread utama menunggu sampai durasi habis atau Ctrl+C ditekan */
    struct timespec now;
    while (g_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = time_diff_sec(&g_start_time, &now);
        if (g_config.duration_sec > 0 && elapsed >= g_config.duration_sec) {
            g_running = 0;
            break;
        }
        usleep(50 * 1000); /* 50ms, cukup responsif tanpa membebani CPU */
    }

    /* Tunggu semua worker thread selesai membersihkan diri */
    for (int i = 0; i < g_config.thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(stat_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &now);
    double total_elapsed = time_diff_sec(&g_start_time, &now);
    print_final_report(total_elapsed);

    free(threads);
    free(targs);
    curl_slist_free_all(g_shared_headers);
    curl_global_cleanup();
    return 0;
}
