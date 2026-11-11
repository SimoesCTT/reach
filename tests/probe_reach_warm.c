/*
 * probe_reach_warm.c — audit 0001, second measurement.
 *
 * The cold probe (probe_reach.c) closed the GEM handle each iteration,
 * so it measured 1000 fresh imports. The kernel's per-file PRIME cache
 * means repeated imports of the same dma-buf into the same DRM file
 * return the cached GEM handle without re-attaching. This probe
 * measures the warm path: one cold import, then N cached lookups, no
 * close between iterations.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/i915_drm.h>

#include "chipmem.h"

#define BYTES 4096
#define RUNS 1000

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("renderD128"); return 1; }
    chipmem_set_fd(fd);

    chipmem_backend_t be;
    chipmem_buffer_t *buf = chipmem_alloc(BYTES, &be);
    if (!buf) { fprintf(stderr, "alloc\n"); return 1; }
    printf("chipmem backend: %d\n", (int)be);

    int dmabuf = chipmem_dmabuf_fd(buf);
    if (dmabuf < 0) { fprintf(stderr, "dmabuf_fd: %d\n", dmabuf); return 1; }

    /* cold: one import, kept open for the whole warm loop */
    struct drm_prime_handle ph0 = { .fd = dmabuf, .handle = 0, .flags = 0 };
    double tc0 = now_ns();
    if (ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph0) != 0) {
        fprintf(stderr, "cold import: %s\n", strerror(errno)); return 1;
    }
    double tc1 = now_ns();
    printf("0 cold import: %.0f ns (handle=%u)\n", tc1 - tc0, ph0.handle);

    /* warm: N cached lookups, no close */
    double *samples = calloc(RUNS, sizeof(double));
    for (int i = 0; i < RUNS; i++) {
        struct drm_prime_handle ph = { .fd = dmabuf, .handle = 0, .flags = 0 };
        double t0 = now_ns();
        if (ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph) != 0) {
            fprintf(stderr, "warm import: %s\n", strerror(errno));
            free(samples); return 1;
        }
        double t1 = now_ns();
        samples[i] = t1 - t0;
        /* deliberately do NOT close ph.handle: the kernel returns the
         * cached handle, and closing it would evict the cache entry */
    }
    qsort(samples, RUNS, sizeof(double), cmp_double);
    printf("1 warm PRIME_FD_TO_HANDLE: median %.0f ns, p99 %.0f ns, "
           "max %.0f ns\n",
           samples[RUNS/2], samples[(int)(RUNS*0.99)], samples[RUNS-1]);

    printf("2 cost-class distinction: the ioctl returns a handle whether\n"
           "  the subsequent access is zero-copy or copied. Warm cost is\n"
           "  measured above; cost class is not observable through it.\n");

    free(samples);
    chipmem_free(buf);
    close(fd);
    return 0;
}
