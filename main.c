#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>

#include "http2_client.h"

static volatile int g_stop_flag = 0;

static void on_sigint(int signo) {
    (void)signo;
    g_stop_flag = 1;
}

/* --------------------------------------------------------------- */
/* Minimal URL parser: http(s)://host[:port][/path]                  */
/* --------------------------------------------------------------- */
static int parse_url(const char *url, h2c_config_t *cfg) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        cfg->use_tls = 1;
        p += 8;
        cfg->port = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        cfg->use_tls = 0;
        p += 7;
        cfg->port = 80;
    } else {
        fprintf(stderr, "URL harus diawali http:// atau https://\n");
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *hostend = slash ? slash : p + strlen(p);

    const char *colon = memchr(p, ':', (size_t)(hostend - p));
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= MAX_HOST_LEN) return -1;
        memcpy(cfg->host, p, hlen);
        cfg->host[hlen] = '\0';
        cfg->port = atoi(colon + 1);
    } else {
        size_t hlen = (size_t)(hostend - p);
        if (hlen >= MAX_HOST_LEN) return -1;
        memcpy(cfg->host, p, hlen);
        cfg->host[hlen] = '\0';
    }

    if (slash) {
        strncpy(cfg->path, slash, MAX_PATH_LEN - 1);
        cfg->path[MAX_PATH_LEN - 1] = '\0';
    } else {
        strcpy(cfg->path, "/");
    }
    return 0;
}

/* --------------------------------------------------------------- */
/* Stats: latency array + CSV writer                                 */
/* --------------------------------------------------------------- */
void h2c_stats_init(h2c_stats_t *stats, const char *csv_path) {
    memset(stats, 0, sizeof(*stats));
    pthread_mutex_init(&stats->latency_mutex, NULL);
    pthread_mutex_init(&stats->csv_mutex, NULL);

    stats->latencies_cap = 65536;
    stats->latencies = malloc(stats->latencies_cap * sizeof(double));
    stats->latencies_len = 0;

    stats->csv_fp = fopen(csv_path, "w");
    if (!stats->csv_fp) {
        fprintf(stderr, "gagal membuka file output '%s': ", csv_path);
        perror("");
        exit(1);
    }
    fprintf(stats->csv_fp,
            "timestamp,worker_id,stream_id,status_code,latency_ms,"
            "ttfb_ms,header_bytes_wire,header_bytes_raw,body_bytes,error\n");
}

void h2c_stats_destroy(h2c_stats_t *stats) {
    if (stats->csv_fp) fclose(stats->csv_fp);
    free(stats->latencies);
    pthread_mutex_destroy(&stats->latency_mutex);
    pthread_mutex_destroy(&stats->csv_mutex);
}

void h2c_stats_record_latency(h2c_stats_t *stats, double ms) {
    pthread_mutex_lock(&stats->latency_mutex);
    if (stats->latencies_len >= stats->latencies_cap) {
        stats->latencies_cap *= 2;
        stats->latencies = realloc(stats->latencies,
                                    stats->latencies_cap * sizeof(double));
    }
    stats->latencies[stats->latencies_len++] = ms;
    pthread_mutex_unlock(&stats->latency_mutex);
}

void h2c_stats_write_row(h2c_stats_t *stats, int worker_id, h2c_request_stat_t *rs) {
    double latency_ms = h2c_timespec_diff_ms(&rs->t_start, &rs->t_end);
    double ttfb_ms = (rs->t_first_byte.tv_sec != 0)
                         ? h2c_timespec_diff_ms(&rs->t_start, &rs->t_first_byte)
                         : -1.0;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    pthread_mutex_lock(&stats->csv_mutex);
    fprintf(stats->csv_fp, "%ld.%09ld,%d,%d,%d,%.3f,%.3f,%zu,%zu,%zu,%d\n",
            (long)now.tv_sec, now.tv_nsec, worker_id, rs->stream_id,
            rs->status_code, latency_ms, ttfb_ms, rs->resp_header_bytes,
            rs->resp_header_bytes_raw, rs->resp_body_bytes, rs->error);
    fflush(stats->csv_fp);
    pthread_mutex_unlock(&stats->csv_mutex);
}

/* --------------------------------------------------------------- */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static double percentile(double *sorted, size_t n, double p) {
    if (n == 0) return 0.0;
    double idx = p * (double)(n - 1);
    size_t lo = (size_t)floor(idx);
    size_t hi = (size_t)ceil(idx);
    if (hi >= n) hi = n - 1;
    double frac = idx - (double)lo;
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Pemakaian: %s -u <url> [opsi]\n"
        "  -u, --url URL          target URL, mis. https://localhost:8443/\n"
        "  -c, --concurrency N    concurrent streams per connection (default 10)\n"
        "  -w, --workers N        worker threads (default 1)\n"
        "  -d, --duration SEC     durasi pengujian dalam detik (default 10)\n"
        "  -r, --rate N           target request rate global, req/s (opsional)\n"
        "  -o, --output FILE      file CSV output (default results.csv)\n"
        "      --debug            aktifkan log detail\n"
        "  -h, --help             tampilkan bantuan ini\n",
        prog);
}

int main(int argc, char **argv) {
    h2c_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.concurrency = 10;
    cfg.workers = 1;
    cfg.duration_sec = 10;
    cfg.rate = 0;
    cfg.debug = 0;
    strcpy(cfg.output_csv, "results.csv");

    int url_given = 0;

    static struct option long_opts[] = {
        {"url",         required_argument, 0, 'u'},
        {"concurrency", required_argument, 0, 'c'},
        {"workers",     required_argument, 0, 'w'},
        {"duration",    required_argument, 0, 'd'},
        {"rate",        required_argument, 0, 'r'},
        {"output",      required_argument, 0, 'o'},
        {"debug",       no_argument,       0,  1000},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "u:c:w:d:r:o:h", long_opts, &idx)) != -1) {
        switch (opt) {
            case 'u':
                if (parse_url(optarg, &cfg) != 0) return 1;
                url_given = 1;
                break;
            case 'c': cfg.concurrency = atoi(optarg); break;
            case 'w': cfg.workers = atoi(optarg); break;
            case 'd': cfg.duration_sec = atoi(optarg); break;
            case 'r': cfg.rate = atof(optarg); break;
            case 'o': strncpy(cfg.output_csv, optarg, sizeof(cfg.output_csv) - 1); break;
            case 1000: cfg.debug = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (!url_given) {
        fprintf(stderr, "Error: -u/--url wajib diisi.\n\n");
        print_usage(argv[0]);
        return 1;
    }
    if (cfg.concurrency < 1 || cfg.workers < 1 || cfg.duration_sec < 1) {
        fprintf(stderr, "Error: -c, -w, -d harus bernilai positif.\n");
        return 1;
    }

    signal(SIGINT, on_sigint);
    signal(SIGPIPE, SIG_IGN);

    printf("=== HTTP/2 Research Client ===\n");
    printf("Target       : %s://%s:%d%s\n", cfg.use_tls ? "https" : "http",
           cfg.host, cfg.port, cfg.path);
    printf("Concurrency  : %d streams/connection\n", cfg.concurrency);
    printf("Workers      : %d\n", cfg.workers);
    printf("Duration     : %d s\n", cfg.duration_sec);
    if (cfg.rate > 0) printf("Target rate  : %.2f req/s\n", cfg.rate);
    printf("Output CSV   : %s\n", cfg.output_csv);
    printf("===============================\n\n");

    if (!cfg.use_tls) {
        fprintf(stderr,
            "Peringatan: skema http:// diberikan; klien ini dirancang untuk "
            "HTTP/2 over TLS 1.3 (h2). Prior knowledge/h2c belum didukung "
            "pada versi ini.\n");
    }

    h2c_stats_t stats;
    h2c_stats_init(&stats, cfg.output_csv);

    pthread_t *threads = calloc(cfg.workers, sizeof(pthread_t));
    h2c_worker_arg_t *wargs = calloc(cfg.workers, sizeof(h2c_worker_arg_t));

    struct timespec wall_start, wall_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);

    for (int i = 0; i < cfg.workers; i++) {
        wargs[i].worker_id = i;
        wargs[i].cfg = &cfg;
        wargs[i].stats = &stats;
        wargs[i].stop_flag = &g_stop_flag;
        pthread_create(&threads[i], NULL, h2c_worker_main, &wargs[i]);
    }

    for (int i = 0; i < cfg.workers; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    double wall_sec = h2c_timespec_diff_ms(&wall_start, &wall_end) / 1000.0;

    /* ---- Summary ---- */
    long total = atomic_load(&stats.total_requests);
    long success = atomic_load(&stats.total_success);
    long errors = atomic_load(&stats.total_errors);
    long conns = atomic_load(&stats.total_conn_established);
    long reused = atomic_load(&stats.total_conn_reused_streams);
    long hdr_wire = atomic_load(&stats.total_header_bytes_wire);
    long hdr_raw = atomic_load(&stats.total_header_bytes_raw);
    long body_bytes = atomic_load(&stats.total_body_bytes);

    qsort(stats.latencies, stats.latencies_len, sizeof(double), cmp_double);

    printf("=== Ringkasan Hasil ===\n");
    printf("Total requests      : %ld\n", total);
    printf("Sukses / Error      : %ld / %ld (%.2f%% error rate)\n",
           success, errors, total > 0 ? 100.0 * (double)errors / (double)total : 0.0);
    printf("Throughput          : %.2f req/s\n", wall_sec > 0 ? (double)total / wall_sec : 0.0);
    printf("Koneksi dibuat      : %ld\n", conns);
    printf("Stream reused-conn  : %ld (%.2f%% dari total)\n",
           reused, total > 0 ? 100.0 * (double)reused / (double)total : 0.0);

    if (stats.latencies_len > 0) {
        printf("\nLatency (ms):\n");
        printf("  min    : %.3f\n", stats.latencies[0]);
        printf("  p50    : %.3f\n", percentile(stats.latencies, stats.latencies_len, 0.50));
        printf("  p90    : %.3f\n", percentile(stats.latencies, stats.latencies_len, 0.90));
        printf("  p99    : %.3f\n", percentile(stats.latencies, stats.latencies_len, 0.99));
        printf("  max    : %.3f\n", stats.latencies[stats.latencies_len - 1]);
    }

    if (hdr_raw > 0) {
        double ratio = 1.0 - ((double)hdr_wire / (double)hdr_raw);
        printf("\nHPACK compression:\n");
        printf("  header bytes (wire, post-HPACK) : %ld\n", hdr_wire);
        printf("  header bytes (raw, uncompressed): %ld\n", hdr_raw);
        printf("  compression ratio saved         : %.2f%%\n", ratio * 100.0);
    }
    printf("\nBody bytes total    : %ld\n", body_bytes);
    printf("\nData mentah per-request tersimpan di: %s\n", cfg.output_csv);

    h2c_stats_destroy(&stats);
    free(threads);
    free(wargs);
    return 0;
}
