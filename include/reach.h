#ifndef REACH_H
#define REACH_H

#include <stddef.h>
#include <stdint.h>
#include "chipmem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REACH_YES_ZERO_COPY = 0,
    REACH_YES_COPY      = 1,
    REACH_NO            = 2,
    REACH_UNKNOWN       = 3
} reach_t;

int  reach_query(chipmem_buffer_t *buf, int device_fd, reach_t *out);
void reach_invalidate(chipmem_buffer_t *buf, int device_fd);
void reach_reset(void);
size_t reach_cached(void);

#ifdef __cplusplus
}
#endif
#endif
