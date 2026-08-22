#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <stdatomic.h>

/*
 * All counters are lock-free atomics so worker/consumer threads can update
 * them from the hot path with minimal contention/overhead.
 */
typedef struct {
    _Atomic uint64_t total_requests;     /* requests submitted (incl. retries) */
    _Atomic uint64_t success_requests;   /* completed with 2xx/3xx status */
    _Atomic uint64_t failed_requests;    /* exhausted retries or hard error */
    _Atomic uint64_t retry_count;        /* number of retry attempts issued */
    _Atomic uint64_t total_latency_us;   /* sum of latencies (us) for successes */
    _Atomic uint64_t connections_active; /* currently established H2 connections */
    _Atomic uint64_t connection_errors;  /* TCP/TLS/handshake failures */
} metrics_t;

extern metrics_t g_metrics;

/* Argument passed to the reporter thread */
typedef struct {
    volatile int *stop_flag;
    int interval_sec;
} metrics_reporter_args_t;

void metrics_init(void);
void metrics_record_submit(void);
void metrics_record_success(uint64_t latency_us);
void metrics_record_failure(void);
void metrics_record_retry(void);
void metrics_record_conn_open(void);
void metrics_record_conn_close(void);
void metrics_record_conn_error(void);

/* pthread entry point: prints one line of stats every `interval_sec` seconds
 * until *stop_flag becomes non-zero. */
void *metrics_reporter_thread(void *arg);

/* Prints a final summary block. elapsed_sec should be the wall-clock
 * duration of the test in seconds (can be fractional-rounded). */
void metrics_print_final(double elapsed_sec);

#endif /* METRICS_H */
