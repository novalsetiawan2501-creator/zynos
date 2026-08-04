#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <time.h>

#include "http2_client.h"

#define MAX_EVENTS 32

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void *h2c_worker_main(void *arg) {
    h2c_worker_arg_t *wa = (h2c_worker_arg_t *)arg;
    h2c_config_t *cfg = wa->cfg;
    h2c_stats_t *stats = wa->stats;

    h2c_pool_t pool;
    if (h2c_pool_init(&pool, wa->worker_id, cfg, stats) != 0) {
        fprintf(stderr, "[worker %d] could not establish any connection, aborting\n",
                wa->worker_id);
        return NULL;
    }

    int epfd = pool.conns[0].epfd;
    struct epoll_event events[MAX_EVENTS];

    double t_start = now_sec();
    double t_deadline = t_start + cfg->duration_sec;

    /* Rate limiting: if -r given, this worker gets an equal share of the
       global target rate and paces new-request submission accordingly. */
    double per_req_interval = 0.0;
    double next_send_time = t_start;
    if (cfg->rate > 0) {
        double worker_rate = cfg->rate / (double)cfg->workers;
        per_req_interval = worker_rate > 0 ? 1.0 / worker_rate : 0.0;
    }

    /* Prime the pipe: fill available stream capacity up front. */
    for (int i = 0; i < pool.count; i++) {
        h2c_connection_t *c = &pool.conns[i];
        if (!c->alive) continue;
        while (c->in_flight < cfg->concurrency) {
            if (h2c_submit_request(c) < 0) break;
        }
        h2c_connection_flush(c);
    }

    while (now_sec() < t_deadline && !(*wa->stop_flag)) {
        int timeout_ms = 200;
        int n = epoll_wait(epfd, events, MAX_EVENTS, timeout_ms);

        for (int i = 0; i < n; i++) {
            h2c_connection_t *conn = (h2c_connection_t *)events[i].data.ptr;
            if (!conn->alive) continue;

            int rv = h2c_connection_service(conn, events[i].events);
            if (rv < 0) {
                if (cfg->debug) {
                    fprintf(stderr, "[worker %d] connection lost, reopening\n", wa->worker_id);
                }
                h2c_connection_close(conn);
                h2c_connection_open(conn, cfg, stats, wa->worker_id, epfd);
            }
        }

        double t = now_sec();
        if (t >= t_deadline) break;

        /* Refill: keep each connection saturated up to -c, honoring -r
           as a soft pacing cap when provided. */
        if (cfg->rate <= 0 || t >= next_send_time) {
            for (int i = 0; i < pool.count; i++) {
                h2c_connection_t *c = &pool.conns[i];
                if (!c->alive) continue;
                while (c->in_flight < cfg->concurrency) {
                    if (h2c_submit_request(c) < 0) break;
                    if (cfg->rate > 0) {
                        next_send_time += per_req_interval;
                        if (next_send_time < t) next_send_time = t;
                        break; /* one request per pacing tick under -r */
                    }
                }
                h2c_connection_flush(c);
            }
        }
    }

    /* Drain: give in-flight streams a brief window to finish so we do
       not count them as artificial errors just because the timer hit 0. */
    double drain_deadline = now_sec() + 2.0;
    while (now_sec() < drain_deadline) {
        int in_flight = 0;
        for (int i = 0; i < pool.count; i++) {
            if (pool.conns[i].alive) in_flight += (int)pool.conns[i].in_flight;
        }
        if (in_flight == 0) break;

        int n = epoll_wait(epfd, events, MAX_EVENTS, 200);
        for (int i = 0; i < n; i++) {
            h2c_connection_t *conn = (h2c_connection_t *)events[i].data.ptr;
            if (!conn->alive) continue;
            h2c_connection_service(conn, events[i].events);
        }
    }

    h2c_pool_destroy(&pool);
    return NULL;
}
