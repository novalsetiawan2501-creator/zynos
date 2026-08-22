#ifndef HTTP2_CLIENT_H
#define HTTP2_CLIENT_H

#include <stdint.h>
#include <time.h>
#include <openssl/ssl.h>
#include <nghttp2/nghttp2.h>

#define H2C_MAX_STREAMS_PER_CONN 256   /* pre-allocated stream-slot pool size */
#define H2C_OUTBUF_SIZE          (64 * 1024)
#define H2C_INBUF_SIZE           (64 * 1024)
#define H2C_MAX_RETRIES          3
#define H2C_RETRY_DELAY_MS       100

struct h2_connection;

/* Per-stream bookkeeping. Pre-allocated in a fixed-size array per
 * connection (H2C_MAX_STREAMS_PER_CONN) and recycled via a freelist stack,
 * so no malloc/free happens on the request hot path. */
typedef struct h2_stream_data {
    int32_t  stream_id;      /* -1 when the slot is free */
    int      in_use;
    int      retry_count;
    int      status_code;
    struct timespec t_start;
    struct h2_connection *conn;
    /* the request path this slot is (re)submitting; points into a
     * caller-owned static/long-lived buffer (zero-copy, NO_COPY nv flags) */
    const char *path;
} h2_stream_data_t;

/* A pending retry: a stream that failed and must be resubmitted after
 * H2C_RETRY_DELAY_MS. Stored inline per-connection, no malloc. */
typedef struct {
    int in_use;
    int retry_count;
    const char *path;
    struct timespec due;
} h2_pending_retry_t;

typedef struct h2_connection {
    int fd;
    SSL *ssl;
    nghttp2_session *session;

    char host[256];
    int  port;

    int connected;      /* TCP + TLS + HTTP/2 handshake complete */
    int want_close;      /* set when connection should be torn down */

    /* outbound spill buffer: holds bytes returned by
     * nghttp2_session_mem_send() that could not be fully written to the
     * socket in one go (EWOULDBLOCK). Avoids re-invoking mem_send() before
     * the previous chunk is fully flushed, per nghttp2 API contract. */
    uint8_t outbuf[H2C_OUTBUF_SIZE];
    size_t  outbuf_len;
    size_t  outbuf_sent;

    h2_stream_data_t streams[H2C_MAX_STREAMS_PER_CONN];
    int free_stack[H2C_MAX_STREAMS_PER_CONN];
    int free_top; /* index of next free slot in free_stack, -1 if full */

    h2_pending_retry_t retries[H2C_MAX_STREAMS_PER_CONN];

    uint32_t remote_max_concurrent_streams; /* learned from server SETTINGS */
    uint32_t inflight_streams;

    void *user_data; /* back-pointer to owning worker context */
} h2_connection_t;

/* Creates an SSL_CTX configured for TLS 1.3 only, with the requested
 * ciphersuite list and ALPN set to "h2". Call once per process (or once per
 * thread if you prefer thread-local contexts); SSL_CTX is thread-safe for
 * creating new SSL* objects concurrently. */
SSL_CTX *h2c_create_ssl_ctx(void);

/* Resolves + connects a non-blocking TCP socket, performs the TLS 1.3
 * handshake (blocking-style helper loop bounded by connect_timeout_ms) and
 * the HTTP/2 client preface + initial SETTINGS. On success `conn` is fully
 * usable (connected == 1). Returns 0 on success, -1 on failure. */
int h2c_connection_open(h2_connection_t *conn, SSL_CTX *ssl_ctx,
                         const char *host, int port, int connect_timeout_ms);

void h2c_connection_close(h2_connection_t *conn);

/* Submits a GET request for `path` on `conn`. `path` must remain valid for
 * the lifetime of the request (pass a static string or one owned by the
 * caller for the connection's lifetime -- enables NO_COPY zero-copy nv).
 * Returns 0 on success, -1 if no free stream slot / session error. */
int h2c_submit_get(h2_connection_t *conn, const char *path);

/* Drains any due retries for this connection and resubmits them. Call once
 * per event-loop iteration. */
void h2c_process_retries(h2_connection_t *conn);

/* Should be called when epoll reports EPOLLIN readiness. Reads from the
 * socket via SSL_read and feeds bytes to nghttp2_session_mem_recv(),
 * triggering the registered callbacks synchronously. Returns 0 on success,
 * -1 on fatal connection error (caller should close+optionally reconnect). */
int h2c_on_readable(h2_connection_t *conn);

/* Should be called when epoll reports EPOLLOUT readiness (or whenever new
 * frames may need flushing after a submit). Drains nghttp2's internal send
 * buffer via nghttp2_session_mem_send() directly into SSL_write() -- no
 * intermediate copy on the fast path; only spills to outbuf on partial
 * writes. Returns 0 on success, -1 on fatal connection error. */
int h2c_on_writable(h2_connection_t *conn);

/* Returns 1 if nghttp2 wants to read/write, i.e. what epoll events to
 * register for this connection's fd right now. */
int h2c_want_read(const h2_connection_t *conn);
int h2c_want_write(const h2_connection_t *conn);

/* Sends `count` sequential dummy GET requests on `conn` and blocks (using a
 * small local poll loop, bounded by timeout_ms) until all responses close
 * or the timeout elapses. Used for connection warm-up before the timed
 * test window starts. Returns number of requests that completed. */
int h2c_warmup(h2_connection_t *conn, const char *path, int count, int timeout_ms);

#endif /* HTTP2_CLIENT_H */
