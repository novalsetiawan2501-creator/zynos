/*
 * ============================================================================
 *  loadtest.c - HTTP/2 Load Testing Tool
 * ============================================================================
 *  Tool load testing sederhana yang mensimulasikan traffic tinggi ke server
 *  web menggunakan protokol HTTP/2 (method GET).
 *
 *  Fitur:
 *    - Connection pooling (reuse CURL easy handle -> reuse koneksi TCP/TLS)
 *    - Multithreading dengan pthread
 *    - Non-blocking I/O via curl_multi interface
 *    - Rate limiting (token bucket) dengan rate * multiplier
 *    - Worker pool (jumlah handle yang di-reuse tiap thread)
 *    - Statistik real-time + progress bar tiap 5 detik
 *    - Graceful shutdown (SIGINT / SIGTERM)
 *    - Error handling dasar, tanpa memory leak (semua handle dibersihkan)
 *
 *  Compile:
 *      gcc -O2 -Wall -o loadtest loadtest.c -lcurl -lpthread
 *
 *  Run contoh:
 *      ./loadtest --url https://example.com --time 30 --rate 50 \
 *                 --thread 4 --connection 20 --multiplier 2 --worker 40
 *
 *      Artinya: target rate efektif = rate * multiplier = 100 req/detik,
 *      dijalankan selama 30 detik, menggunakan 4 thread, tiap thread
 *      menjaga 20 koneksi paralel yang diambil dari pool 40 handle (worker).
 *
 *  Catatan penting (harap dibaca):
 *      - Alat ini HANYA untuk menguji server/endpoint milik sendiri atau
 *        yang memang sudah diberi izin untuk diuji. Jangan gunakan untuk
 *        menyerang server pihak lain tanpa izin (itu ilegal / DoS).
 *      - Perlu libcurl yang dibangun dengan dukungan HTTP/2 (nghttp2).
 *        Cek dengan: curl --version | grep -i http2
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <curl/curl.h>

/* ------------------------------------------------------------------------ */
/* Konfigurasi global (diisi dari argumen CLI)                              */
/* ------------------------------------------------------------------------ */
typedef struct {
    char url[1024];
    int  duration_sec;   /* --time        */
    int  rate;            /* --rate        */
    int  multiplier;      /* --multipler   */
    int  n_threads;       /* --thread      */
    int  n_connections;   /* --connection  */
    int  n_worker;        /* --worker      */
} config_t;

static config_t g_cfg;

/* ------------------------------------------------------------------------ */
/* Statistik global (atomic supaya aman diakses banyak thread)              */
/* ------------------------------------------------------------------------ */
static atomic_long g_total_requests = 0;
static atomic_long g_total_success  = 0;
static atomic_long g_total_error    = 0;
static atomic_long g_total_bytes    = 0;

/* Flag untuk graceful shutdown. volatile karena diubah dari signal handler. */
static volatile sig_atomic_t g_running = 1;

/* Token bucket sederhana untuk rate limiting.
 * g_tokens diisi ulang oleh thread khusus setiap 100ms berdasarkan
 * effective_rate = rate * multiplier.                                      */
static atomic_long g_tokens = 0;

static struct timespec g_start_time;

/* ------------------------------------------------------------------------ */
/* Signal handler - graceful shutdown                                       */
/* ------------------------------------------------------------------------ */
static void handle_signal(int sig) {
    (void)sig;
    /* Cukup set flag; semua thread akan berhenti secara bertahap dan
     * membersihkan resource masing-masing (tidak ada exit() paksa di sini
     * supaya tidak ada memory leak / koneksi menggantung). */
    g_running = 0;
}

/* ------------------------------------------------------------------------ */
/* Callback penerima body response.
 * Kita tidak butuh isi body, hanya hitung ukurannya lalu buang.            */
/* ------------------------------------------------------------------------ */
static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    size_t total = size * nmemb;
    atomic_fetch_add(&g_total_bytes, (long)total);
    return total; /* wajib return total, kalau tidak curl anggap error */
}

/* ------------------------------------------------------------------------ */
/* Helper: dapatkan waktu dalam detik (double) sejak start                  */
/* ------------------------------------------------------------------------ */
static double elapsed_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - g_start_time.tv_sec) +
           (now.tv_nsec - g_start_time.tv_nsec) / 1e9;
}

/* ------------------------------------------------------------------------ */
/* Thread pengisi token bucket.
 * Mengisi token setiap 100ms sebesar effective_rate/10, sehingga rata-rata
 * jumlah request per detik across semua thread mendekati target.           */
/* ------------------------------------------------------------------------ */
static void *rate_limiter_thread(void *arg) {
    (void)arg;
    long effective_rate = (long)g_cfg.rate * (long)g_cfg.multiplier;
    if (effective_rate < 1) effective_rate = 1;
    long tokens_per_tick = effective_rate / 10; /* tick = 100ms */
    if (tokens_per_tick < 1) tokens_per_tick = 1;
    long max_burst = effective_rate; /* cap supaya token tidak menumpuk
                                         tak terbatas saat semua thread
                                         sedang sibuk (mencegah lonjakan
                                         request mendadak / burst).      */

    while (g_running && elapsed_seconds() < g_cfg.duration_sec) {
        long cur = atomic_load(&g_tokens);
        if (cur < max_burst) {
            atomic_fetch_add(&g_tokens, tokens_per_tick);
        }
        usleep(100 * 1000); /* 100 ms */
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Ambil satu token dari bucket (non-blocking, atomic).
 * Return 1 kalau berhasil ambil token, 0 kalau bucket kosong.              */
/* ------------------------------------------------------------------------ */
static int try_consume_token(void) {
    long cur = atomic_load(&g_tokens);
    while (cur > 0) {
        if (atomic_compare_exchange_weak(&g_tokens, &cur, cur - 1)) {
            return 1;
        }
        /* cur otomatis diperbarui oleh compare_exchange kalau gagal */
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Struct kecil untuk menyimpan header list per easy handle supaya bisa
 * dibersihkan (curl_slist_free_all) tanpa leak.                            */
/* ------------------------------------------------------------------------ */
typedef struct {
    CURL *easy;
    struct curl_slist *headers;
} handle_slot_t;

/* ------------------------------------------------------------------------ */
/* Setup satu easy handle: URL, HTTP/2, header sederhana, callback, dsb.
 * Handle ini akan DI-REUSE berkali-kali (connection pooling) selama
 * multi handle induknya tidak di-cleanup, karena libcurl otomatis menjaga
 * koneksi tetap terbuka (keep-alive) selama easy handle tetap hidup dan
 * dipakai dalam multi handle yang sama.                                    */
static void setup_handle(handle_slot_t *slot, CURLM *multi, const char *url) {
    slot->easy = curl_easy_init();
    slot->headers = NULL;

    if (!slot->easy) {
        fprintf(stderr, "[ERROR] curl_easy_init gagal (out of memory?)\n");
        return;
    }

    /* Header HTTP sederhana */
    slot->headers = curl_slist_append(slot->headers, "User-Agent: c-loadtest/1.0");
    slot->headers = curl_slist_append(slot->headers, "Accept: */*");
    slot->headers = curl_slist_append(slot->headers, "Connection: keep-alive");

    curl_easy_setopt(slot->easy, CURLOPT_URL, url);
    curl_easy_setopt(slot->easy, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(slot->easy, CURLOPT_HTTPHEADER, slot->headers);
    curl_easy_setopt(slot->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(slot->easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(slot->easy, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(slot->easy, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(slot->easy, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(slot->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(slot->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(slot->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(slot->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    /* Simpan pointer slot di private data supaya gampang diakses balik
     * saat handle selesai (untuk tahu slot mana yang harus di-restart). */
    curl_easy_setopt(slot->easy, CURLOPT_PRIVATE, slot);

    curl_multi_add_handle(multi, slot->easy);
}

/* ------------------------------------------------------------------------ */
/* Argumen untuk tiap worker thread                                        */
/* ------------------------------------------------------------------------ */
typedef struct {
    int thread_id;
} thread_arg_t;

/* ------------------------------------------------------------------------ */
/* Fungsi utama tiap worker thread:
 *   - Punya 1 curl_multi handle (non-blocking I/O)
 *   - Mengelola pool sejumlah g_cfg.n_worker easy handle (connection pool)
 *   - Menjaga maksimal g_cfg.n_connections transfer berjalan paralel
 *   - Saat sebuah transfer selesai, ambil token dari rate limiter;
 *     kalau ada token -> langsung fire ulang request di handle yang sama
 *     (reuse koneksi), kalau tidak ada token -> tunggu sebentar.           */
static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    int tid = targ->thread_id;

    CURLM *multi = curl_multi_init();
    if (!multi) {
        fprintf(stderr, "[Thread %d] ERROR: curl_multi_init gagal\n", tid);
        return NULL;
    }

    /* Batasi jumlah koneksi paralel sesuai parameter --connection */
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)g_cfg.n_connections);
    curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)g_cfg.n_connections);

    /* Alokasikan pool handle (worker pool). Minimal sebanyak n_connections
     * supaya semua slot koneksi paralel bisa terisi.                      */
    int pool_size = g_cfg.n_worker;
    if (pool_size < g_cfg.n_connections) pool_size = g_cfg.n_connections;

    handle_slot_t *pool = calloc((size_t)pool_size, sizeof(handle_slot_t));
    if (!pool) {
        fprintf(stderr, "[Thread %d] ERROR: alokasi memori gagal\n", tid);
        curl_multi_cleanup(multi);
        return NULL;
    }

    /* Nyalakan handle sejumlah n_connections dulu (sisanya di pool
     * dipakai sebagai pengganti saat rotasi/reuse).                       */
    int active_slots = g_cfg.n_connections;
    if (active_slots > pool_size) active_slots = pool_size;

    for (int i = 0; i < active_slots; i++) {
        setup_handle(&pool[i], multi, g_cfg.url);
    }

    int still_running = 0;
    curl_multi_perform(multi, &still_running);

    /* Loop utama non-blocking I/O */
    while (g_running && elapsed_seconds() < g_cfg.duration_sec) {

        /* curl_multi_poll: non-blocking I/O, tunggu event maksimal 100ms */
        int numfds = 0;
        CURLMcode mc = curl_multi_poll(multi, NULL, 0, 100, &numfds);
        if (mc != CURLM_OK) {
            fprintf(stderr, "[Thread %d] curl_multi_poll error: %d\n", tid, mc);
            break;
        }

        curl_multi_perform(multi, &still_running);

        /* Cek handle mana saja yang sudah selesai transfer-nya */
        int msgs_left = 0;
        CURLMsg *msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
            if (msg->msg != CURLMSG_DONE) continue;

            CURL *easy = msg->easy_handle;
            handle_slot_t *slot = NULL;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &slot);

            atomic_fetch_add(&g_total_requests, 1);

            if (msg->data.result == CURLE_OK) {
                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
                if (http_code >= 200 && http_code < 400) {
                    atomic_fetch_add(&g_total_success, 1);
                } else {
                    /* Error handling basic: HTTP status di luar 2xx/3xx
                     * tetap dihitung sebagai error request.               */
                    atomic_fetch_add(&g_total_error, 1);
                }
            } else {
                atomic_fetch_add(&g_total_error, 1);
                /* Basic error logging, tidak menghentikan program */
                /* fprintf(stderr, "[Thread %d] transfer error: %s\n",
                 *          tid, curl_easy_strerror(msg->data.result)); */
            }

            /* Lepas handle dari multi dulu sebelum diputuskan reuse/stop */
            curl_multi_remove_handle(multi, easy);

            /* Connection pooling: kalau waktu masih ada dan masih running,
             * dan ada token rate limit -> pakai LAGI handle yang sama
             * (koneksi TCP/TLS yang sudah terbuka akan dipakai ulang oleh
             * libcurl secara internal selama easy handle tidak di-cleanup
             * dan ditambahkan lagi ke multi handle yang sama).            */
            if (g_running && elapsed_seconds() < g_cfg.duration_sec) {
                /* Coba ambil token beberapa kali dengan sedikit delay
                 * kalau bucket kosong, supaya tidak busy-loop 100% CPU.   */
                int got_token = try_consume_token();
                if (!got_token) {
                    usleep(2000); /* 2ms, lalu handle akan dicoba lagi di
                                     iterasi luar berikutnya via re-add     */
                }
                curl_multi_add_handle(multi, easy);
            } else {
                /* Waktu habis / shutdown: bersihkan handle ini sekarang.
                 * Catatan: handle ini sudah di-remove dari multi di atas,
                 * jadi di sini langsung cleanup saja (tidak remove lagi). */
                curl_easy_cleanup(easy);
                if (slot) {
                    curl_slist_free_all(slot->headers);
                    slot->headers = NULL;
                    slot->easy = NULL;
                }
            }
        }
    }

    /* -------------------- Graceful shutdown per-thread ------------------ */
    /* Hentikan semua handle yang masih aktif di multi handle ini dulu */
    for (int i = 0; i < active_slots; i++) {
        if (pool[i].easy) {
            curl_multi_remove_handle(multi, pool[i].easy);
            curl_easy_cleanup(pool[i].easy);
            curl_slist_free_all(pool[i].headers);
            pool[i].easy = NULL;
            pool[i].headers = NULL;
        }
    }

    free(pool);
    curl_multi_cleanup(multi);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Progress bar + statistik real-time, dicetak tiap 5 detik oleh main thread*/
/* ------------------------------------------------------------------------ */
static void print_progress(void) {
    double elapsed = elapsed_seconds();
    double pct = (elapsed / g_cfg.duration_sec) * 100.0;
    if (pct > 100.0) pct = 100.0;

    long total   = atomic_load(&g_total_requests);
    long success = atomic_load(&g_total_success);
    long errors  = atomic_load(&g_total_error);
    long bytes   = atomic_load(&g_total_bytes);
    double cur_rps = (elapsed > 0) ? (double)total / elapsed : 0.0;

    int bar_width = 30;
    int filled = (int)(bar_width * pct / 100.0);

    printf("\r[");
    for (int i = 0; i < bar_width; i++) putchar(i < filled ? '#' : '-');
    printf("] %5.1f%% | %.0fs/%ds | req=%ld ok=%ld err=%ld | %.1f req/s | %.2f MB\n",
           pct, elapsed, g_cfg.duration_sec, total, success, errors,
           cur_rps, bytes / (1024.0 * 1024.0));
    fflush(stdout);
}

/* ------------------------------------------------------------------------ */
/* Parsing argumen CLI sederhana --key value                                */
/* ------------------------------------------------------------------------ */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Pemakaian: %s --url <url> --time <detik> --rate <rps> "
        "--thread <n> --connection <n> --multipler <n> --worker <n>\n"
        "\nContoh:\n"
        "  %s --url https://example.com --time 30 --rate 50 --thread 4 "
        "--connection 20 --multipler 2 --worker 40\n",
        prog, prog);
}

static int parse_args(int argc, char **argv) {
    /* Nilai default */
    strncpy(g_cfg.url, "http://127.0.0.1/", sizeof(g_cfg.url) - 1);
    g_cfg.duration_sec  = 10;
    g_cfg.rate          = 10;
    g_cfg.multiplier    = 1;
    g_cfg.n_threads     = 2;
    g_cfg.n_connections = 10;
    g_cfg.n_worker      = 10;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            strncpy(g_cfg.url, argv[++i], sizeof(g_cfg.url) - 1);
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            g_cfg.duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            g_cfg.rate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--thread") == 0 && i + 1 < argc) {
            g_cfg.n_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--connection") == 0 && i + 1 < argc) {
            g_cfg.n_connections = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--multipler") == 0 && i + 1 < argc) {
            g_cfg.multiplier = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--worker") == 0 && i + 1 < argc) {
            g_cfg.n_worker = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else {
            fprintf(stderr, "[WARN] argumen tidak dikenal: %s\n", argv[i]);
        }
    }

    if (g_cfg.duration_sec <= 0 || g_cfg.rate <= 0 || g_cfg.n_threads <= 0 ||
        g_cfg.n_connections <= 0 || g_cfg.multiplier <= 0 || g_cfg.n_worker <= 0) {
        fprintf(stderr, "[ERROR] semua parameter numerik harus > 0\n");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* main                                                                      */
/* ------------------------------------------------------------------------ */
int main(int argc, char **argv) {
    if (parse_args(argc, argv) != 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Wajib sekali di awal program sebelum thread apa pun dibuat */
    CURLcode gres = curl_global_init(CURL_GLOBAL_ALL);
    if (gres != CURLE_OK) {
        fprintf(stderr, "[ERROR] curl_global_init gagal: %s\n",
                curl_easy_strerror(gres));
        return EXIT_FAILURE;
    }

    printf("=== C HTTP/2 Load Testing Tool ===\n");
    printf("URL          : %s\n", g_cfg.url);
    printf("Durasi       : %d detik\n", g_cfg.duration_sec);
    printf("Rate dasar   : %d req/s\n", g_cfg.rate);
    printf("Multiplier   : %d (rate efektif = %d req/s)\n",
           g_cfg.multiplier, g_cfg.rate * g_cfg.multiplier);
    printf("Threads      : %d\n", g_cfg.n_threads);
    printf("Connections  : %d per thread\n", g_cfg.n_connections);
    printf("Worker pool  : %d handle per thread\n", g_cfg.n_worker);
    printf("===================================\n\n");

    clock_gettime(CLOCK_MONOTONIC, &g_start_time);

    /* Thread rate limiter (token bucket) */
    pthread_t rl_thread;
    pthread_create(&rl_thread, NULL, rate_limiter_thread, NULL);

    /* Buat worker threads */
    pthread_t *threads = calloc((size_t)g_cfg.n_threads, sizeof(pthread_t));
    thread_arg_t *targs = calloc((size_t)g_cfg.n_threads, sizeof(thread_arg_t));
    if (!threads || !targs) {
        fprintf(stderr, "[ERROR] alokasi memori untuk thread gagal\n");
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    for (int i = 0; i < g_cfg.n_threads; i++) {
        targs[i].thread_id = i;
        if (pthread_create(&threads[i], NULL, worker_thread, &targs[i]) != 0) {
            fprintf(stderr, "[ERROR] gagal membuat thread %d\n", i);
        }
    }

    /* Main thread: cetak progress bar tiap 5 detik sampai selesai/berhenti */
    while (g_running && elapsed_seconds() < g_cfg.duration_sec) {
        sleep(5);
        print_progress();
    }

    if (!g_running) {
        printf("\n[INFO] Sinyal berhenti diterima, melakukan graceful shutdown...\n");
    } else {
        printf("\n[INFO] Waktu pengujian selesai, menunggu semua thread berhenti...\n");
    }

    /* Pastikan semua thread tahu waktu sudah habis / harus berhenti */
    g_running = 0;

    /* Tunggu semua worker thread selesai membersihkan resource-nya */
    for (int i = 0; i < g_cfg.n_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(rl_thread, NULL);

    free(threads);
    free(targs);

    /* ------------------------- Ringkasan akhir --------------------------- */
    double total_elapsed = elapsed_seconds();
    long total   = atomic_load(&g_total_requests);
    long success = atomic_load(&g_total_success);
    long errors  = atomic_load(&g_total_error);
    long bytes   = atomic_load(&g_total_bytes);

    printf("\n=========== RINGKASAN HASIL ===========\n");
    printf("Total durasi     : %.2f detik\n", total_elapsed);
    printf("Total request    : %ld\n", total);
    printf("Sukses (2xx/3xx) : %ld\n", success);
    printf("Error            : %ld\n", errors);
    printf("Rata-rata req/s  : %.2f\n",
           total_elapsed > 0 ? total / total_elapsed : 0.0);
    printf("Total data       : %.2f MB\n", bytes / (1024.0 * 1024.0));
    printf("========================================\n");

    curl_global_cleanup();
    return EXIT_SUCCESS;
}
