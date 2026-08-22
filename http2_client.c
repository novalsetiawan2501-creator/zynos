#include "http2_client.h"
#include "metrics.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* -------------------------------------------------------------------- */
/* Socket / TLS setup                                                    */
/* -------------------------------------------------------------------- */

static int connect_nonblocking(const char *host, int port, int timeout_ms) {
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) {
            break; /* connected synchronously */
        }
        if (errno == EINPROGRESS) {
            struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
            int pr = poll(&pfd, 1, timeout_ms);
            if (pr > 0 && (pfd.revents & POLLOUT)) {
                int soerr = 0;
                socklen_t slen = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) == 0 && soerr == 0) {
                    break; /* connect succeeded */
                }
            }
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int tls_handshake(SSL *ssl, int fd, int timeout_ms) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        int rc = SSL_do_handshake(ssl);
        if (rc == 1) return 0;

        int err = SSL_get_error(ssl, rc);
        short ev;
        if (err == SSL_ERROR_WANT_READ) ev = POLLIN;
        else if (err == SSL_ERROR_WANT_WRITE) ev = POLLOUT;
        else return -1;

        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                           (now.tv_nsec - start.tv_nsec) / 1000000;
        int remain = timeout_ms - (int)elapsed_ms;
        if (remain <= 0) return -1;

        struct pollfd pfd = {.fd = fd, .events = ev, .revents = 0};
        int pr = poll(&pfd, 1, remain);
        if (pr <= 0) return -1;
    }
}

SSL_CTX *h2c_create_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

    if (SSL_CTX_set_ciphersuites(
            ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:"
                 "TLS_AES_128_GCM_SHA256") != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    static const unsigned char alpn_protos[] = {2, 'h', '2'};
    SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos));

    /* Internal benchmark target: skip cert verification so self-signed /
     * internal-CA certs on the target don't abort the handshake. Remove
     * this if your internal server uses a trusted public CA and you want
     * strict verification. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    return ctx;
}

/* -------------------------------------------------------------------- */
/* Stream slot pool helpers                                              */
/* -------------------------------------------------------------------- */

static void free_stream_slot(h2_connection_t *conn, h2_stream_data_t *sd) {
    int idx = (int)(sd - conn->streams);
    sd->in_use = 0;
    sd->stream_id = -1;
    conn->free_stack[++conn->free_top] = idx;
}

static void schedule_retry(h2_connection_t *conn, h2_stream_data_t *sd) {
    int idx = (int)(sd - conn->streams);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long ns = now.tv_nsec + (long)H2C_RETRY_DELAY_MS * 1000000L;
    h2_pending_retry_t *r = &conn->retries[idx];
    r->in_use = 1;
    r->retry_count = sd->retry_count + 1;
    r->path = sd->path;
    r->due.tv_sec = now.tv_sec + ns / 1000000000L;
    r->due.tv_nsec = ns % 1000000000L;

    /* IMPORTANT: the slot is intentionally NOT returned to free_stack here.
     * It stays reserved for this pending retry until process_retries()
     * resubmits it (reusing the same slot) or gives up permanently. This
     * prevents a fresh request from colliding with the pending retry. */
    sd->stream_id = -1;
}

static int submit_on_slot(h2_connection_t *conn, int slot_idx, const char *path,
                           int retry_count) {
    h2_stream_data_t *sd = &conn->streams[slot_idx];
    sd->in_use = 1;
    sd->retry_count = retry_count;
    sd->status_code = 0;
    sd->path = path;
    sd->conn = conn;
    clock_gettime(CLOCK_MONOTONIC, &sd->t_start);

    nghttp2_nv hdrs[8];
    int i = 0;

#define NV_STATIC(N, V)                                                      \
    do {                                                                     \
        hdrs[i].name = (uint8_t *)(N);                                      \
        hdrs[i].namelen = strlen(N);                                        \
        hdrs[i].value = (uint8_t *)(V);                                     \
        hdrs[i].valuelen = strlen(V);                                       \
        hdrs[i].flags = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE; \
        i++;                                                                 \
    } while (0)

    NV_STATIC(":method", "GET");
    NV_STATIC(":scheme", "https");

    hdrs[i].name = (uint8_t *)":authority";
    hdrs[i].namelen = 10;
    hdrs[i].value = (uint8_t *)conn->host;
    hdrs[i].valuelen = strlen(conn->host);
    hdrs[i].flags = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    i++;

    hdrs[i].name = (uint8_t *)":path";
    hdrs[i].namelen = 5;
    hdrs[i].value = (uint8_t *)path;
    hdrs[i].valuelen = strlen(path);
    hdrs[i].flags = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    i++;

    NV_STATIC("user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    NV_STATIC("accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    NV_STATIC("accept-language", "en-US,en;q=0.5");
    NV_STATIC("accept-encoding", "gzip, deflate, br");
    /* NOTE: "Connection: keep-alive" and "Upgrade-Insecure-Requests" from
     * the original spec are intentionally omitted. HTTP/2 forbids
     * connection-specific header fields (RFC 7540 8.1.2.2) -- sending
     * "Connection" can make strict servers/intermediaries reject the whole
     * request with a stream error. Keep-alive is implicit in HTTP/2's
     * multiplexed, persistent connections, so it isn't needed. */
#undef NV_STATIC

    int32_t stream_id =
        nghttp2_submit_request(conn->session, NULL, hdrs, (size_t)i, NULL, sd);
    if (stream_id < 0) {
        free_stream_slot(conn, sd);
        return -1;
    }
    sd->stream_id = stream_id;
    conn->inflight_streams++;
    metrics_record_submit();
    return 0;
}

/* -------------------------------------------------------------------- */
/* nghttp2 callbacks                                                     */
/* -------------------------------------------------------------------- */

static int on_begin_headers_callback(nghttp2_session *session,
                                      const nghttp2_frame *frame, void *user_data) {
    (void)session;
    (void)frame;
    (void)user_data;
    return 0;
}

static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen, uint8_t flags,
                               void *user_data) {
    (void)flags;
    (void)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS ||
        frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        h2_stream_data_t *sd =
            nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (sd) {
            char buf[16];
            size_t n = valuelen < sizeof(buf) - 1 ? valuelen : sizeof(buf) - 1;
            memcpy(buf, value, n);
            buf[n] = '\0';
            sd->status_code = atoi(buf);
        }
    }
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                        int32_t stream_id, const uint8_t *data,
                                        size_t len, void *user_data) {
    (void)session;
    (void)flags;
    (void)stream_id;
    (void)data;
    (void)user_data;
    /* Body intentionally discarded -- this is a throughput/latency
     * benchmark, not a content validator. nghttp2 still auto-manages flow
     * control window updates for us since we never disabled that option. */
    (void)len;
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                     uint32_t error_code, void *user_data) {
    h2_connection_t *conn = (h2_connection_t *)user_data;
    h2_stream_data_t *sd = nghttp2_session_get_stream_user_data(session, stream_id);
    if (!sd) return 0;

    if (conn->inflight_streams > 0) conn->inflight_streams--;

    int ok = (error_code == NGHTTP2_NO_ERROR) && sd->status_code >= 200 &&
             sd->status_code < 400;

    if (ok) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t us = (uint64_t)(now.tv_sec - sd->t_start.tv_sec) * 1000000ULL +
                      (uint64_t)(now.tv_nsec - sd->t_start.tv_nsec) / 1000ULL;
        metrics_record_success(us);
        free_stream_slot(conn, sd);
    } else if (sd->retry_count < H2C_MAX_RETRIES) {
        metrics_record_retry();
        schedule_retry(conn, sd);
    } else {
        metrics_record_failure();
        free_stream_slot(conn, sd);
    }
    return 0;
}

/* -------------------------------------------------------------------- */
/* Public API                                                            */
/* -------------------------------------------------------------------- */

int h2c_connection_open(h2_connection_t *conn, SSL_CTX *ssl_ctx, const char *host,
                         int port, int connect_timeout_ms) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = -1;
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port;
    conn->free_top = H2C_MAX_STREAMS_PER_CONN - 1;
    for (int i = 0; i < H2C_MAX_STREAMS_PER_CONN; i++) {
        conn->streams[i].stream_id = -1;
        conn->free_stack[i] = i;
    }

    conn->fd = connect_nonblocking(host, port, connect_timeout_ms);
    if (conn->fd < 0) {
        metrics_record_conn_error();
        return -1;
    }

    conn->ssl = SSL_new(ssl_ctx);
    if (!conn->ssl) {
        close(conn->fd);
        conn->fd = -1;
        metrics_record_conn_error();
        return -1;
    }
    SSL_set_fd(conn->ssl, conn->fd);
    SSL_set_tlsext_host_name(conn->ssl, host); /* SNI */
    SSL_set_connect_state(conn->ssl);

    if (tls_handshake(conn->ssl, conn->fd, connect_timeout_ms) != 0) {
        SSL_free(conn->ssl);
        conn->ssl = NULL;
        close(conn->fd);
        conn->fd = -1;
        metrics_record_conn_error();
        return -1;
    }

    nghttp2_session_callbacks *callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
        callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
                                                            on_stream_close_callback);
    nghttp2_session_callbacks_set_on_begin_headers_callback(
        callbacks, on_begin_headers_callback);

    int rv = nghttp2_session_client_new2(&conn->session, callbacks, conn, NULL);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0) {
        SSL_free(conn->ssl);
        conn->ssl = NULL;
        close(conn->fd);
        conn->fd = -1;
        metrics_record_conn_error();
        return -1;
    }

    nghttp2_settings_entry entries[] = {
        {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 65536},
        {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 32768},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 15564991},
        {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
    };
    nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, entries,
                             sizeof(entries) / sizeof(entries[0]));

    /* NOTE: the spec named `nghttp2_settings_max_initial_window_size()`,
     * which is not an actual function in the public nghttp2 API. The real
     * equivalent -- raising OUR receive window past the default 64KiB to
     * match the INITIAL_WINDOW_SIZE we just advertised -- is
     * nghttp2_session_set_local_window_size() (stream_id 0 = connection
     * level), used below. */
    nghttp2_session_set_local_window_size(conn->session, NGHTTP2_FLAG_NONE, 0,
                                           15564991);

    if (h2c_on_writable(conn) != 0) {
        h2c_connection_close(conn);
        return -1;
    }

    conn->connected = 1;
    metrics_record_conn_open();
    return 0;
}

void h2c_connection_close(h2_connection_t *conn) {
    if (conn->session) {
        nghttp2_session_del(conn->session);
        conn->session = NULL;
    }
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    if (conn->connected) {
        metrics_record_conn_close();
    }
    conn->connected = 0;
}

int h2c_submit_get(h2_connection_t *conn, const char *path) {
    if (conn->free_top < 0) return -1; /* local slot pool exhausted */

    uint32_t remote_max = nghttp2_session_get_remote_settings(
        conn->session, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    if (conn->inflight_streams >= remote_max) return -1; /* server-imposed cap */

    int slot_idx = conn->free_stack[conn->free_top--];
    if (submit_on_slot(conn, slot_idx, path, 0) != 0) return -1;
    return 0;
}

void h2c_process_retries(h2_connection_t *conn) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int idx = 0; idx < H2C_MAX_STREAMS_PER_CONN; idx++) {
        h2_pending_retry_t *r = &conn->retries[idx];
        if (!r->in_use) continue;
        if (now.tv_sec < r->due.tv_sec ||
            (now.tv_sec == r->due.tv_sec && now.tv_nsec < r->due.tv_nsec)) {
            continue; /* not due yet */
        }

        uint32_t remote_max = nghttp2_session_get_remote_settings(
            conn->session, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
        if (conn->inflight_streams >= remote_max) {
            /* server still saturated: re-arm a short backoff instead of
             * dropping the retry */
            long ns = r->due.tv_nsec + 20L * 1000000L;
            r->due.tv_sec = now.tv_sec + ns / 1000000000L;
            r->due.tv_nsec = ns % 1000000000L;
            continue;
        }

        r->in_use = 0;
        if (submit_on_slot(conn, idx, r->path, r->retry_count) != 0) {
            metrics_record_failure();
            conn->streams[idx].in_use = 0;
            conn->streams[idx].stream_id = -1;
            conn->free_stack[++conn->free_top] = idx;
        }
    }
}

int h2c_on_readable(h2_connection_t *conn) {
    uint8_t buf[H2C_INBUF_SIZE];

    for (;;) {
        int n = SSL_read(conn->ssl, buf, sizeof(buf));
        if (n > 0) {
            ssize_t rv = nghttp2_session_mem_recv(conn->session, buf, (size_t)n);
            if (rv < 0) return -1;
            if (SSL_pending(conn->ssl) > 0) continue;
            break;
        }
        int err = SSL_get_error(conn->ssl, n);
        if (err == SSL_ERROR_WANT_READ) break;
        if (err == SSL_ERROR_WANT_WRITE) {
            if (h2c_on_writable(conn) != 0) return -1;
            break;
        }
        if (err == SSL_ERROR_ZERO_RETURN) return -1; /* peer closed */
        return -1;                                    /* fatal I/O error */
    }

    /* Reading may have unblocked new frames to send (ACKs, window updates,
     * queued requests freed up by a stream close). */
    return h2c_on_writable(conn);
}

int h2c_on_writable(h2_connection_t *conn) {
    for (;;) {
        if (conn->outbuf_len > conn->outbuf_sent) {
            size_t remain = conn->outbuf_len - conn->outbuf_sent;
            int n = SSL_write(conn->ssl, conn->outbuf + conn->outbuf_sent, (int)remain);
            if (n > 0) {
                conn->outbuf_sent += (size_t)n;
                if (conn->outbuf_sent == conn->outbuf_len) {
                    conn->outbuf_len = conn->outbuf_sent = 0;
                    continue;
                }
                return 0; /* partial; wait for next writable event */
            }
            int err = SSL_get_error(conn->ssl, n);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) return 0;
            return -1;
        }

        const uint8_t *data = NULL;
        ssize_t len = nghttp2_session_mem_send(conn->session, &data);
        if (len < 0) return -1;
        if (len == 0) break; /* nothing pending */

        int n = SSL_write(conn->ssl, data, (int)len);
        if (n > 0 && (size_t)n == (size_t)len) {
            continue; /* fully flushed, pull more from nghttp2 */
        } else if (n > 0) {
            size_t remain = (size_t)len - (size_t)n;
            if (remain > H2C_OUTBUF_SIZE) remain = H2C_OUTBUF_SIZE;
            memcpy(conn->outbuf, data + n, remain);
            conn->outbuf_len = remain;
            conn->outbuf_sent = 0;
            return 0;
        } else {
            int err = SSL_get_error(conn->ssl, n);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                size_t remain = (size_t)len;
                if (remain > H2C_OUTBUF_SIZE) remain = H2C_OUTBUF_SIZE;
                memcpy(conn->outbuf, data, remain);
                conn->outbuf_len = remain;
                conn->outbuf_sent = 0;
                return 0;
            }
            return -1;
        }
    }
    return 0;
}

int h2c_want_read(const h2_connection_t *conn) {
    return nghttp2_session_want_read(conn->session) ? 1 : 0;
}

int h2c_want_write(const h2_connection_t *conn) {
    if (conn->outbuf_len > conn->outbuf_sent) return 1;
    return nghttp2_session_want_write(conn->session) ? 1 : 0;
}

int h2c_warmup(h2_connection_t *conn, const char *path, int count, int timeout_ms) {
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int sent = 0;
    while (sent < count) {
        if (h2c_submit_get(conn, path) != 0) break;
        sent++;
    }
    if (h2c_on_writable(conn) != 0) return 0;

    int completed = 0;
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms =
            (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (elapsed_ms > timeout_ms) break;
        if (conn->inflight_streams == 0) {
            completed = sent;
            break;
        }

        struct pollfd pfd;
        pfd.fd = conn->fd;
        pfd.events = 0;
        if (h2c_want_read(conn)) pfd.events |= POLLIN;
        if (h2c_want_write(conn)) pfd.events |= POLLOUT;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, 200);
        if (pr <= 0) continue;
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            if (h2c_on_readable(conn) != 0) break;
        }
        if (pfd.revents & POLLOUT) {
            if (h2c_on_writable(conn) != 0) break;
        }
    }
    h2c_process_retries(conn);
    return completed;
}
