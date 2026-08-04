#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>

#include <openssl/err.h>

#include "http2_client.h"

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

double h2c_timespec_diff_ms(struct timespec *a, struct timespec *b) {
    double sec  = (double)(b->tv_sec - a->tv_sec);
    double nsec = (double)(b->tv_nsec - a->tv_nsec);
    return sec * 1000.0 + nsec / 1e6;
}

static h2c_request_stat_t *pending_slot(h2c_connection_t *conn, int32_t sid) {
    if (conn->pending_cap == 0) return NULL;
    return &conn->pending[(size_t)sid % conn->pending_cap];
}

static void ensure_pending_capacity(h2c_connection_t *conn) {
    if (conn->pending) return;
    conn->pending_cap = 4096; /* enough for many concurrent/lifetime streams,
                                  indexed modulo capacity since we only need
                                  the record alive between submit and close */
    conn->pending = calloc(conn->pending_cap, sizeof(h2c_request_stat_t));
}

/* ------------------------------------------------------------------ */
/* Non-blocking TCP connect                                            */
/* ------------------------------------------------------------------ */

static int connect_tcp(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        if (errno == EINPROGRESS) break; /* handled by caller if nonblock set before */

        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ------------------------------------------------------------------ */
/* ALPN callback: advertise/select "h2"                                */
/* ------------------------------------------------------------------ */

static int alpn_select_cb(SSL *ssl, const unsigned char **out,
                           unsigned char *outlen, const unsigned char *in,
                           unsigned int inlen, void *arg) {
    (void)ssl; (void)arg;
    static const unsigned char h2_proto[] = { 2, 'h', '2' };
    if (SSL_select_next_proto((unsigned char **)out, outlen, in, inlen,
                               h2_proto, sizeof(h2_proto)) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    return SSL_TLSEXT_ERR_OK;
}

static SSL_CTX *make_ssl_ctx(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    /* Force TLS 1.3 as required by the research protocol under test. */
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_alpn_protos(ctx, (const unsigned char *)"\x02h2", 3);
    SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb, NULL);

    /* Research harness against local/test endpoints: verification can be
       tightened with SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL) plus
       SSL_CTX_load_verify_locations() when testing against a real CA. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    return ctx;
}

/* ------------------------------------------------------------------ */
/* nghttp2 <-> OpenSSL glue                                             */
/* ------------------------------------------------------------------ */

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    h2c_connection_t *conn = (h2c_connection_t *)user_data;
    int rv = SSL_write(conn->ssl, data, (int)length);
    if (rv <= 0) {
        int err = SSL_get_error(conn->ssl, rv);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            conn->want_write = (err == SSL_ERROR_WANT_WRITE);
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return rv;
}

static ssize_t recv_callback(nghttp2_session *session, uint8_t *buf,
                              size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    h2c_connection_t *conn = (h2c_connection_t *)user_data;
    int rv = SSL_read(conn->ssl, buf, (int)length);
    if (rv <= 0) {
        int err = SSL_get_error(conn->ssl, rv);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            return NGHTTP2_ERR_EOF;
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return rv;
}

/* Fired for each header field of a response: lets us measure HPACK
   efficiency (raw name+value length vs. what actually crossed the wire,
   which nghttp2 tracks in the frame's hd.length for HEADERS frames). */
static int on_header_callback(nghttp2_session *session,
                               const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
    (void)session; (void)flags;
    h2c_connection_t *conn = (h2c_connection_t *)user_data;
    if (frame->hd.type != NGHTTP2_HEADERS) return 0;

    h2c_request_stat_t *rs = pending_slot(conn, frame->hd.stream_id);
    if (!rs) return 0;

    rs->resp_header_bytes_raw += namelen + valuelen;

    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        char tmp[16] = {0};
        size_t n = valuelen < sizeof(tmp) - 1 ? valuelen : sizeof(tmp) - 1;
        memcpy(tmp, value, n);
        rs->status_code = atoi(tmp);
    }
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session,
                                   const nghttp2_frame *frame, void *user_data) {
    h2c_connection_t *conn = (h2c_connection_t *)user_data;

    if (frame->hd.type == NGHTTP2_HEADERS) {
        h2c_request_stat_t *rs = pending_slot(conn, frame->hd.stream_id);
        if (rs) {
            /* frame->hd.length is the on-wire size of the HEADERS frame
               payload (i.e. the HPACK-compressed block for this frame). */
            rs->resp_header_bytes += frame->hd.length;
            if (rs->t_first_byte.tv_sec == 0) {
                clock_gettime(CLOCK_MONOTONIC, &rs->t_first_byte);
            }
        }
    } else if (frame->hd.type == NGHTTP2_SETTINGS &&
               (frame->hd.flags & NGHTTP2_FLAG_ACK) == 0) {
        if (conn->debug) {
            fprintf(stderr, "[conn worker=%d] received SETTINGS frame\n",
                    conn->worker_id);
        }
    }
    (void)session;
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session,
                                        uint8_t flags, int32_t stream_id,
                                        const uint8_t *data, size_t len,
                                        void *user_data) {
    (void)session; (void)flags; (void)data;
    h2c_connection_t *conn = (h2c_connection_t *)user_data;
    h2c_request_stat_t *rs = pending_slot(conn, stream_id);
    if (rs) rs->resp_body_bytes += len;
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session,
                                     int32_t stream_id, uint32_t error_code,
                                     void *user_data) {
    (void)session;
    h2c_connection_t *conn = (h2c_connection_t *)user_data;
    h2c_request_stat_t *rs = pending_slot(conn, stream_id);
    if (!rs) return 0;

    clock_gettime(CLOCK_MONOTONIC, &rs->t_end);
    rs->stream_id = stream_id;
    if (error_code != 0 && rs->status_code == 0) {
        rs->error = (int)error_code;
        rs->error_str = "stream_error";
    }

    conn->in_flight--;

    double latency_ms = h2c_timespec_diff_ms(&rs->t_start, &rs->t_end);
    h2c_stats_record_latency(conn->stats, latency_ms);
    h2c_stats_write_row(conn->stats, conn->worker_id, rs);

    atomic_fetch_add(&conn->stats->total_requests, 1);
    if (rs->error == 0 && rs->status_code > 0 && rs->status_code < 400) {
        atomic_fetch_add(&conn->stats->total_success, 1);
    } else {
        atomic_fetch_add(&conn->stats->total_errors, 1);
    }
    atomic_fetch_add(&conn->stats->total_header_bytes_wire, (long)rs->resp_header_bytes);
    atomic_fetch_add(&conn->stats->total_header_bytes_raw, (long)rs->resp_header_bytes_raw);
    atomic_fetch_add(&conn->stats->total_body_bytes, (long)rs->resp_body_bytes);

    if (conn->streams_opened > 1) {
        /* every stream after the first on this connection reused the
           TCP+TLS handshake, which is exactly the metric we want for
           "connection reuse efficiency". */
        atomic_fetch_add(&conn->stats->total_conn_reused_streams, 1);
    }

    memset(rs, 0, sizeof(*rs));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Connection lifecycle                                                 */
/* ------------------------------------------------------------------ */

int h2c_connection_open(h2c_connection_t *conn, h2c_config_t *cfg,
                         h2c_stats_t *stats, int worker_id, int epfd) {
    memset(conn, 0, sizeof(*conn));
    conn->cfg = cfg;
    conn->stats = stats;
    conn->worker_id = worker_id;
    conn->debug = cfg->debug;
    conn->epfd = epfd;

    conn->fd = connect_tcp(cfg->host, cfg->port);
    if (conn->fd < 0) {
        if (cfg->debug) perror("connect_tcp");
        return -1;
    }
    set_nonblocking(conn->fd);

    conn->ssl_ctx = make_ssl_ctx();
    if (!conn->ssl_ctx) return -1;

    conn->ssl = SSL_new(conn->ssl_ctx);
    SSL_set_fd(conn->ssl, conn->fd);
    SSL_set_tlsext_host_name(conn->ssl, cfg->host);
    SSL_set_connect_state(conn->ssl);

    /* Blocking-style handshake loop driven manually since the socket is
       non-blocking; acceptable here because handshake happens once per
       connection at startup, not on the steady-state request path. */
    int rv;
    for (;;) {
        rv = SSL_connect(conn->ssl);
        if (rv == 1) break;
        int err = SSL_get_error(conn->ssl, rv);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            struct pollfd pfd = { .fd = conn->fd,
                                   .events = (err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT };
            if (poll(&pfd, 1, 5000) <= 0) {
                if (cfg->debug) fprintf(stderr, "TLS handshake timeout\n");
                return -1;
            }
            continue;
        }
        if (cfg->debug) {
            fprintf(stderr, "TLS handshake failed: %s\n",
                    ERR_error_string(ERR_get_error(), NULL));
        }
        return -1;
    }

    if (cfg->debug) {
        const unsigned char *alpn = NULL;
        unsigned int alpnlen = 0;
        SSL_get0_alpn_selected(conn->ssl, &alpn, &alpnlen);
        fprintf(stderr, "[worker %d] TLS established, ALPN=%.*s, cipher=%s\n",
                worker_id, alpnlen, alpn ? (const char *)alpn : "(none)",
                SSL_get_cipher(conn->ssl));
    }

    nghttp2_session_callbacks *cbs;
    nghttp2_session_callbacks_new(&cbs);
    nghttp2_session_callbacks_set_send_callback(cbs, send_callback);
    nghttp2_session_callbacks_set_recv_callback(cbs, recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_stream_close_callback);

    nghttp2_session_client_new(&conn->session, cbs, conn);
    nghttp2_session_callbacks_del(cbs);

    nghttp2_settings_entry iv[] = {
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, (uint32_t)cfg->concurrency },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 1 << 20 },
    };
    nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, iv, 2);

    ensure_pending_capacity(conn);

    struct epoll_event ev = {0};
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.ptr = conn;
    epoll_ctl(epfd, EPOLL_CTL_ADD, conn->fd, &ev);

    conn->alive = 1;
    atomic_fetch_add(&stats->total_conn_established, 1);
    return 0;
}

void h2c_connection_close(h2c_connection_t *conn) {
    if (!conn->alive) return;
    conn->alive = 0;
    if (conn->epfd >= 0 && conn->fd >= 0) {
        epoll_ctl(conn->epfd, EPOLL_CTL_DEL, conn->fd, NULL);
    }
    if (conn->session) nghttp2_session_del(conn->session);
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }
    if (conn->ssl_ctx) SSL_CTX_free(conn->ssl_ctx);
    if (conn->fd >= 0) close(conn->fd);
    free(conn->pending);
    conn->pending = NULL;
}

int h2c_submit_request(h2c_connection_t *conn) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", conn->cfg->port);

    char authority[MAX_HOST_LEN + 16];
    snprintf(authority, sizeof(authority), "%s:%d", conn->cfg->host, conn->cfg->port);

    nghttp2_nv nva[] = {
        { (uint8_t *)":method", (uint8_t *)"GET", 7, 3, NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)":scheme", (uint8_t *)(conn->cfg->use_tls ? "https" : "http"),
          7, conn->cfg->use_tls ? 5u : 4u, NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)":authority", (uint8_t *)authority, 10, strlen(authority), NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)":path", (uint8_t *)conn->cfg->path, 5, strlen(conn->cfg->path), NGHTTP2_NV_FLAG_NONE },
        { (uint8_t *)"user-agent", (uint8_t *)"h2client-research/1.0", 10, 21, NGHTTP2_NV_FLAG_NONE },
    };

    int32_t stream_id = nghttp2_submit_request(conn->session, NULL, nva, 5, NULL, conn);
    if (stream_id < 0) {
        if (conn->debug) {
            fprintf(stderr, "submit_request failed: %s\n", nghttp2_strerror(stream_id));
        }
        return -1;
    }

    h2c_request_stat_t *rs = pending_slot(conn, stream_id);
    if (rs) {
        memset(rs, 0, sizeof(*rs));
        clock_gettime(CLOCK_MONOTONIC, &rs->t_start);
        rs->stream_id = stream_id;
    }

    conn->streams_opened++;
    conn->in_flight++;
    return stream_id;
}

int h2c_connection_flush(h2c_connection_t *conn) {
    if (nghttp2_session_want_write(conn->session)) {
        int rv = nghttp2_session_send(conn->session);
        if (rv != 0) return rv;
    }
    return 0;
}

int h2c_connection_service(h2c_connection_t *conn, uint32_t events) {
    (void)events;

    if (nghttp2_session_want_read(conn->session)) {
        int rv = nghttp2_session_recv(conn->session);
        if (rv != 0 && rv != NGHTTP2_ERR_WOULDBLOCK) {
            if (conn->debug) fprintf(stderr, "session_recv error: %s\n", nghttp2_strerror(rv));
            return -1;
        }
    }

    if (nghttp2_session_want_write(conn->session)) {
        int rv = nghttp2_session_send(conn->session);
        if (rv != 0 && rv != NGHTTP2_ERR_WOULDBLOCK) {
            if (conn->debug) fprintf(stderr, "session_send error: %s\n", nghttp2_strerror(rv));
            return -1;
        }
    }

    if (!nghttp2_session_want_read(conn->session) &&
        !nghttp2_session_want_write(conn->session)) {
        return -1; /* connection has nothing left to do -> closed by peer/GOAWAY */
    }
    return 0;
}
