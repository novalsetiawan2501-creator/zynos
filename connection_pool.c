#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "http2_client.h"

/* Number of physical connections each worker keeps open. Concurrency
   (-c) is streams-per-connection, so total in-flight capacity per
   worker is roughly MAX_CONNECTIONS_PER_WORKER * cfg->concurrency.
   Kept small and fixed so "connection reuse efficiency" stays easy to
   reason about: most streams should land on an already-open connection. */
#define CONNS_PER_WORKER 2

int h2c_pool_init(h2c_pool_t *pool, int worker_id, h2c_config_t *cfg,
                   h2c_stats_t *stats) {
    pool->count = CONNS_PER_WORKER;
    pool->conns = calloc(pool->count, sizeof(h2c_connection_t));
    if (!pool->conns) return -1;

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        free(pool->conns);
        return -1;
    }

    int opened = 0;
    for (int i = 0; i < pool->count; i++) {
        if (h2c_connection_open(&pool->conns[i], cfg, stats, worker_id, epfd) == 0) {
            opened++;
        } else if (cfg->debug) {
            fprintf(stderr, "[worker %d] failed to open connection %d\n", worker_id, i);
        }
    }

    if (opened == 0) {
        close(epfd);
        free(pool->conns);
        pool->conns = NULL;
        pool->count = 0;
        return -1;
    }
    return 0;
}

void h2c_pool_destroy(h2c_pool_t *pool) {
    if (!pool->conns) return;
    int epfd = pool->conns[0].epfd;
    for (int i = 0; i < pool->count; i++) {
        h2c_connection_close(&pool->conns[i]);
    }
    if (epfd >= 0) close(epfd);
    free(pool->conns);
    pool->conns = NULL;
    pool->count = 0;
}

/* Round-robin over live connections, preferring the one with the
   fewest in-flight streams (basic load spreading across the pool). */
h2c_connection_t *h2c_pool_next_available(h2c_pool_t *pool) {
    h2c_connection_t *best = NULL;
    for (int i = 0; i < pool->count; i++) {
        h2c_connection_t *c = &pool->conns[i];
        if (!c->alive) continue;
        if (c->in_flight >= pool->conns[i].cfg->concurrency) continue;
        if (!best || c->in_flight < best->in_flight) best = c;
    }
    return best;
}
