#include "metrics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

metrics_t g_metrics;

void metrics_init(void) {
    atomic_init(&g_metrics.total_requests, 0);
    atomic_init(&g_metrics.success_requests, 0);
    atomic_init(&g_metrics.failed_requests, 0);
    atomic_init(&g_metrics.retry_count, 0);
    atomic_init(&g_metrics.total_latency_us, 0);
    atomic_init(&g_metrics.connections_active, 0);
    atomic_init(&g_metrics.connection_errors, 0);
}

void metrics_record_submit(void) {
    atomic_fetch_add_explicit(&g_metrics.total_requests, 1, memory_order_relaxed);
}

void metrics_record_success(uint64_t latency_us) {
    atomic_fetch_add_explicit(&g_metrics.success_requests, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_metrics.total_latency_us, latency_us, memory_order_relaxed);
}

void metrics_record_failure(void) {
    atomic_fetch_add_explicit(&g_metrics.failed_requests, 1, memory_order_relaxed);
}

void metrics_record_retry(void) {
    atomic_fetch_add_explicit(&g_metrics.retry_count, 1, memory_order_relaxed);
}

void metrics_record_conn_open(void) {
    atomic_fetch_add_explicit(&g_metrics.connections_active, 1, memory_order_relaxed);
}

void metrics_record_conn_close(void) {
    atomic_fetch_sub_explicit(&g_metrics.connections_active, 1, memory_order_relaxed);
}

void metrics_record_conn_error(void) {
    atomic_fetch_add_explicit(&g_metrics.connection_errors, 1, memory_order_relaxed);
}

static double now_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void *metrics_reporter_thread(void *arg) {
    metrics_reporter_args_t *a = (metrics_reporter_args_t *)arg;
    int interval = a->interval_sec > 0 ? a->interval_sec : 1;

    uint64_t last_total = 0;
    double start = now_monotonic();
    double last_tick = start;

    printf("%-8s %12s %10s %10s %12s %10s %10s\n",
           "time(s)", "total_req", "success", "failed", "rps", "avg_lat(ms)", "err%");
    fflush(stdout);

    while (!*(a->stop_flag)) {
        struct timespec req = {.tv_sec = interval, .tv_nsec = 0};
        nanosleep(&req, NULL);
        if (*(a->stop_flag)) break;

        double t = now_monotonic();
        uint64_t total = atomic_load_explicit(&g_metrics.total_requests, memory_order_relaxed);
        uint64_t success = atomic_load_explicit(&g_metrics.success_requests, memory_order_relaxed);
        uint64_t failed = atomic_load_explicit(&g_metrics.failed_requests, memory_order_relaxed);
        uint64_t lat_us = atomic_load_explicit(&g_metrics.total_latency_us, memory_order_relaxed);

        double dt = t - last_tick;
        if (dt <= 0) dt = (double)interval;
        double rps = (double)(total - last_total) / dt;

        double avg_lat_ms = success > 0 ? ((double)lat_us / (double)success) / 1000.0 : 0.0;
        uint64_t completed = success + failed;
        double err_pct = completed > 0 ? (100.0 * (double)failed / (double)completed) : 0.0;

        printf("%-8.0f %12" PRIu64 " %10" PRIu64 " %10" PRIu64 " %12.1f %10.2f %9.2f%%\n",
               t - start, total, success, failed, rps, avg_lat_ms, err_pct);
        fflush(stdout);

        last_total = total;
        last_tick = t;
    }
    return NULL;
}

void metrics_print_final(double elapsed_sec) {
    uint64_t total = atomic_load_explicit(&g_metrics.total_requests, memory_order_relaxed);
    uint64_t success = atomic_load_explicit(&g_metrics.success_requests, memory_order_relaxed);
    uint64_t failed = atomic_load_explicit(&g_metrics.failed_requests, memory_order_relaxed);
    uint64_t retries = atomic_load_explicit(&g_metrics.retry_count, memory_order_relaxed);
    uint64_t lat_us = atomic_load_explicit(&g_metrics.total_latency_us, memory_order_relaxed);
    uint64_t conn_err = atomic_load_explicit(&g_metrics.connection_errors, memory_order_relaxed);

    double avg_lat_ms = success > 0 ? ((double)lat_us / (double)success) / 1000.0 : 0.0;
    uint64_t completed = success + failed;
    double err_pct = completed > 0 ? (100.0 * (double)failed / (double)completed) : 0.0;
    double overall_rps = elapsed_sec > 0 ? (double)total / elapsed_sec : 0.0;

    printf("\n================ FINAL SUMMARY ================\n");
    printf("Duration            : %.2f s\n", elapsed_sec);
    printf("Total requests       : %" PRIu64 "\n", total);
    printf("Successful           : %" PRIu64 "\n", success);
    printf("Failed               : %" PRIu64 "\n", failed);
    printf("Retries issued       : %" PRIu64 "\n", retries);
    printf("Connection errors    : %" PRIu64 "\n", conn_err);
    printf("Average RPS          : %.1f\n", overall_rps);
    printf("Average latency      : %.2f ms\n", avg_lat_ms);
    printf("Error rate           : %.2f %%\n", err_pct);
    printf("=================================================\n");
}
