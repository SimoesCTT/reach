#define _GNU_SOURCE
#include "reach.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <drm/drm.h>

#define REACH_MAX 256
struct entry { chipmem_buffer_t *buf; int device_fd; reach_t answer; };
static struct entry g_cache[REACH_MAX];
static size_t g_n;

static struct entry *find(chipmem_buffer_t *buf, int fd)
{
    for (size_t i = 0; i < g_n; i++)
        if (g_cache[i].buf == buf && g_cache[i].device_fd == fd)
            return &g_cache[i];
    return NULL;
}

static reach_t cost_class_from_backend(chipmem_buffer_t *buf)
{
    switch (chipmem_backend(buf)) {
    case CHIPMEM_BACKEND_DMABUF_COHERENT:
    case CHIPMEM_BACKEND_DMABUF_SYNC_IOCTL: return REACH_YES_ZERO_COPY;
    case CHIPMEM_BACKEND_COPY_FALLBACK:     return REACH_YES_COPY;
    default:                                return REACH_UNKNOWN;
    }
}

int reach_query(chipmem_buffer_t *buf, int device_fd, reach_t *out)
{
    if (!buf || device_fd < 0 || !out) return -EINVAL;
    struct entry *e = find(buf, device_fd);
    if (e) { *out = e->answer; return 0; }
    if (g_n >= REACH_MAX) return -ENOSPC;
    int dmabuf = chipmem_dmabuf_fd(buf);
    if (dmabuf < 0) return dmabuf;
    struct drm_prime_handle ph = { .fd = dmabuf, .handle = 0, .flags = 0 };
    if (ioctl(device_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph) != 0) return -errno;
    struct drm_gem_close c = { .handle = ph.handle };
    ioctl(device_fd, DRM_IOCTL_GEM_CLOSE, &c);
    reach_t answer = cost_class_from_backend(buf);
    g_cache[g_n++] = (struct entry){ buf, device_fd, answer };
    *out = answer;
    return 0;
}

void reach_invalidate(chipmem_buffer_t *buf, int device_fd)
{
    for (size_t i = 0; i < g_n; i++)
        if (g_cache[i].buf == buf && g_cache[i].device_fd == device_fd) {
            g_cache[i] = g_cache[--g_n]; return;
        }
}
void reach_reset(void) { g_n = 0; }
size_t reach_cached(void) { return g_n; }
