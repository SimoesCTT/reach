#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "chipmem.h"
#include "reach.h"

#define RUNS 1000
static double now_ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}
int main(void)
{
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("renderD128"); return 1; }
    chipmem_set_fd(fd);
    chipmem_backend_t be;
    chipmem_buffer_t *buf = chipmem_alloc(4096, &be);
    if (!buf) { fprintf(stderr, "alloc\n"); return 1; }
    printf("chipmem backend: %d\n", (int)be);
    reach_t ans;
    double t0 = now_ns();
    int rc = reach_query(buf, fd, &ans);
    double t1 = now_ns();
    if (rc != 0) { fprintf(stderr, "first: %s\n", strerror(-rc)); return 1; }
    printf("first query: %s in %.0f ns (cache %zu)\n",
           ans == REACH_YES_ZERO_COPY ? "ZERO_COPY" :
           ans == REACH_YES_COPY ? "COPY" :
           ans == REACH_NO ? "NO" : "UNKNOWN",
           t1 - t0, reach_cached());
    double sum = 0, mx = 0;
    for (int i = 0; i < RUNS; i++) {
        double a = now_ns();
        if (reach_query(buf, fd, &ans) != 0) return 1;
        double b = now_ns();
        double d = b - a; sum += d; if (d > mx) mx = d;
    }
    double mean = sum / RUNS;
    printf("cached: %d runs, mean %.0f ns, max %.0f ns\n", RUNS, mean, mx);
    printf("cache size: %zu\n", reach_cached());
    if (reach_cached() != 1) { fprintf(stderr, "FAIL: cache grew\n"); return 1; }
    if (mean > 1000.0) { fprintf(stderr, "FAIL: not sub-us (%.0f ns)\n", mean); return 1; }
    printf("PASS\n");
    chipmem_free(buf);
    close(fd);
    return 0;
}
