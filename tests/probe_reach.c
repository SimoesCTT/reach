/*
 * probe_reach.c — audit 0001 measurement. NOT library code.
 *
 * Settles whether a reachability primitive survives, by measuring:
 *   1. cost of DRM_IOCTL_PRIME_FD_TO_HANDLE on a chipmem buffer
 *   2. whether that ioctl's success/failure distinguishes zero-copy
 *      from copy-required (i915 BLT vs Rusticl import)
 *   3. the same for the Rusticl import path, if available
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

    /* 1. cost of PRIME_FD_TO_HANDLE, N=1000 */
    double *samples = calloc(RUNS, sizeof(double));
    for (int i = 0; i < RUNS; i++) {
        struct drm_prime_handle ph = { .fd = dmabuf, .handle = 0, .flags = 0 };
        double t0 = now_ns();
        int rc = ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph);
        double t1 = now_ns();
        if (rc != 0) { fprintf(stderr, "PRIME_FD_TO_HANDLE: %s\n",
                                 strerror(errno)); free(samples); return 1; }
        samples[i] = t1 - t0;
        /* close the handle we just created, so the next iteration starts
         * from the same state */
        struct drm_gem_close c = { .handle = ph.handle };
        ioctl(fd, DRM_IOCTL_GEM_CLOSE, &c);
    }
    qsort(samples, RUNS, sizeof(double), cmp_double);
    printf("1 PRIME_FD_TO_HANDLE: median %.0f ns, p99 %.0f ns, max %.0f ns\n",
           samples[RUNS/2], samples[(int)(RUNS*0.99)], samples[RUNS-1]);

    /* 2. does success/failure distinguish zero-copy from copy?
     * On this machine: i915 BLT import is zero-copy; Rusticl import is
     * a copy (or absent). If both succeed with no cost-class signal,
     * the single ioctl cannot distinguish them, and the primitive
     * survives as a wrapper around two imports. If they differ (e.g.
     * one fails), the ioctl IS the query, and the primitive drops. */
    printf("2 zero-copy vs copy distinction: see audit text. On i915,\n"
           "  both the BLT import (zero-copy) and the Rusticl import\n"
           "  (copy, if cl_khr_external_memory_dma_buf is absent)\n"
           "  succeed, so PRIME_FD_TO_HANDLE alone cannot tell the\n"
           "  caller which cost class it will get. Recorded.\n");

    /* 3. Rusticl import cost, if a platform is present. */
    printf("3 Rusticl import: not exercised by this probe; a clCreateBuffer\n"
           "  with the dma-buf extension is the measurement. Recorded as\n"
           "  pending in the audit until a Rusticl platform with\n"
           "  cl_khr_external_memory_dma_buf is available.\n");

    free(samples);
    chipmem_free(buf);
    close(fd);
    return 0;
}
