#ifndef HTTP2_CLIENT_H
#define HTTP2_CLIENT_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <openssl/ssl.h>
#include <nghttp2/nghttp2.h>

#define MAX_HOST_LEN     256
#define MAX_PATH_LEN     1024
#define MAX_CONNECTIONS_PER_WORKER 8
#define IO_BUF_SIZE      65536

/* ---------------------------------------------------------------- */
/* Global configuration parsed from CLI arguments                    */
/* ---------------------------------------------------------------- */
typedef struct {
    char     host[MAX_HOST_LEN];
    char     path[MAX_PATH_LEN];
    int      port;
    int      use_tls;          /* derived from scheme (https -> 1)   */

    int      concurrency;      /* -c  streams per connection          */
    int      workers;          /* -w  worker threads                  */
    int      duration_sec;     /* -d  test duration                   */
    double   rate;             /* -r  target requests/sec (0 = off)   */
    int      debug;            /* --debug verbose logging              */
    char     output_csv[512];  /* -o  output file                      */
} h2c_config_t;

/* ---------------------------------------------------------------- */
/* Per-request measurement record                                    */
/* ---------------------------------------------------------------- */
typedef struct {
    struct timespec t_start;
    struct timespec t_first_byte;
    struct timespec t_end;
    int32_t  stream_id;
    int      status_code;
    size_t   resp_header_bytes;   /* wire-size estimate (post-HPACK)  */
    size_t   resp_header_bytes_raw; /* uncompressed header size est.  */
    size_t   resp_body_bytes;
    int      error;                /* 0 = ok, non-zero = error code    */
    const char *error_str;
} h2c_request_stat_t;

/* ---------------------------------------------------------------- */
/* Aggregated statistics, updated atomically across worker threads   */
/* ---------------------------------------------------------------- */
typedef struct {
    atomic_long total_requests;
    atomic_long total_success;
    atomic_long total_errors;
    atomic_long total_conn_established;
    atomic_long total_conn_reused_streams;
    atomic_long total_header_bytes_wire;
    atomic_long total_header_bytes_raw;
    atomic_long total_body_bytes;

    pthread_mutex_t latency_mutex;
    double  *latencies_ms;     /* dynamic array, protected by mutex   */
    size_t   latencies_len;
    size_t   latencies_cap;

    pthread_mutex_t csv_mutex;
    FILE    *csv_fp;
} h2c_stats_t;

/* ---------------------------------------------------------------- */
/* One physical TCP+TLS+nghttp2 connection                           */
/* ---------------------------------------------------------------- */
typedef struct h2c_connection {
    int              fd;
    int              epfd;          /* epoll fd owned by the worker   */
    SSL_CTX         *ssl_ctx;
    SSL             *ssl;
    nghttp2_session *session;
    int              want_read;
    int              want_write;
    int              alive;
    long             streams_opened;   /* lifetime stream count        */
    long             in_flight;
    h2c_config_t    *cfg;
    h2c_stats_t     *stats;
    int              worker_id;
    int              debug;

    /* pending request stat slots, indexed by stream_id % capacity    */
    h2c_request_stat_t *pending;
    size_t               pending_cap;
} h2c_connection_t;

/* Connection pool: an array of connections owned by one worker      */
typedef struct {
    h2c_connection_t *conns;
    int               count;
} h2c_pool_t;

typedef struct {
    int              worker_id;
    h2c_config_t    *cfg;
    h2c_stats_t     *stats;
    volatile int    *stop_flag;
} h2c_worker_arg_t;

/* --- connection_pool.c ------------------------------------------- */
int  h2c_pool_init(h2c_pool_t *pool, int worker_id, h2c_config_t *cfg,
                    h2c_stats_t *stats);
void h2c_pool_destroy(h2c_pool_t *pool);
h2c_connection_t *h2c_pool_next_available(h2c_pool_t *pool);

/* --- http2_client.c ----------------------------------------------- */
int  h2c_connection_open(h2c_connection_t *conn, h2c_config_t *cfg,
                          h2c_stats_t *stats, int worker_id, int epfd);
void h2c_connection_close(h2c_connection_t *conn);
int  h2c_submit_request(h2c_connection_t *conn);
int  h2c_connection_service(h2c_connection_t *conn, uint32_t events);
int  h2c_connection_flush(h2c_connection_t *conn);

/* --- worker.c ------------------------------------------------------ */
void *h2c_worker_main(void *arg);

/* --- stats helpers (main.c) ---------------------------------------- */
void h2c_stats_init(h2c_stats_t *stats, const char *csv_path);
void h2c_stats_destroy(h2c_stats_t *stats);
void h2c_stats_record_latency(h2c_stats_t *stats, double ms);
void h2c_stats_write_row(h2c_stats_t *stats, int worker_id,
                          h2c_request_stat_t *rs);

double h2c_timespec_diff_ms(struct timespec *a, struct timespec *b);

#endif /* HTTP2_CLIENT_H */
