/* SPDX-License-Identifier: Apache-2.0 */
#include "native_window.h"
#include "../../platforms/common/nativewindowbase.h"
#include <hybris/gralloc/gralloc.h>
#include <errno.h>
#include <limits.h>
#include <new>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

namespace {
struct Owner;
struct Buffer : BaseNativeWindowBuffer {
    Owner *owner;
    bool held = false;
    int fence = -1;
    explicit Buffer(Owner *o) : owner(o) { handle = nullptr; }
    ~Buffer() {
        if (fence >= 0) close(fence);
        if (handle) hybris_gralloc_release(handle, 1);
    }
};
struct Owner {
    hybris_icd_window base;
    Buffer *pool[8] = {};
    unsigned count = 0;
    pthread_mutex_t mutex;
    pthread_cond_t available;
};
Owner *owner(hybris_icd_window *window) { return reinterpret_cast<Owner *>(window); }
void drop_pool(Owner *o) {
    for (unsigned i = 0; i < o->count; ++i)
        o->pool[i]->common.decRef(&o->pool[i]->common);
    o->count = 0;
}
void disconnect(hybris_icd_window *window) {
    Owner *o = owner(window);
    pthread_mutex_lock(&o->mutex);
    drop_pool(o);
    pthread_cond_broadcast(&o->available);
    pthread_mutex_unlock(&o->mutex);
}
int configure(hybris_icd_window *window, unsigned width, unsigned height,
              int format, unsigned usage, unsigned count) {
    if (!width || !height || width > INT_MAX || height > INT_MAX || count < 2 || count > 8)
        return -EINVAL;
    Owner *o = owner(window);
    pthread_mutex_lock(&o->mutex);
    if (o->count) { pthread_mutex_unlock(&o->mutex); return -EBUSY; }
    int error = 0;
    for (unsigned i = 0; i < count; ++i) {
        Buffer *b = new (std::nothrow) Buffer(o);
        if (!b) { error = -ENOMEM; break; }
        b->width = width; b->height = height; b->format = format; b->usage = usage;
        uint32_t stride = 0;
        if (hybris_gralloc_allocate(width, height, format, usage, &b->handle, &stride)) {
            delete b; error = -ENOMEM; break;
        }
        b->stride = stride;
        b->common.incRef(&b->common);
        o->pool[o->count++] = b;
    }
    if (error) drop_pool(o);
    pthread_mutex_unlock(&o->mutex);
    return error;
}
int dequeue(hybris_icd_window *window, int64_t timeout, ANativeWindowBuffer **buffer, int *fence) {
    Owner *o = owner(window);
    *buffer = nullptr; *fence = -1;
    timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    if (timeout > 0) {
        deadline.tv_sec += timeout / 1000000000LL;
        deadline.tv_nsec += timeout % 1000000000LL;
        if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
    }
    pthread_mutex_lock(&o->mutex);
    int error = 0;
    for (;;) {
        if (!o->count) { error = -ESTALE; break; }
        for (unsigned i = 0; i < o->count; ++i) {
            Buffer *b = o->pool[i];
            if (b->held) continue;
            b->held = true;
            *buffer = b; *fence = b->fence; b->fence = -1;
            pthread_mutex_unlock(&o->mutex);
            return 0;
        }
        if (!timeout) { error = -EAGAIN; break; }
        int result = timeout < 0 ? pthread_cond_wait(&o->available, &o->mutex)
                                : pthread_cond_timedwait(&o->available, &o->mutex, &deadline);
        if (result) { error = -result; break; }
    }
    pthread_mutex_unlock(&o->mutex);
    return error;
}
int release(hybris_icd_window *window, ANativeWindowBuffer *buffer, int fence) {
    Owner *o = owner(window);
    Buffer *b = static_cast<Buffer *>(buffer);
    pthread_mutex_lock(&o->mutex);
    if (!b || b->owner != o || !b->held) {
        pthread_mutex_unlock(&o->mutex);
        if (fence >= 0) close(fence);
        return -EINVAL;
    }
    /* Presentation has no display-side effects. Transfer, rather than wait
     * for, the GPU release fence: the next vkAcquireImageANDROID imports it.
     * This preserves rendering dependencies without CPU waits or a compositor. */
    if (b->fence >= 0) close(b->fence);
    b->fence = fence;
    b->held = false;
    pthread_cond_signal(&o->available);
    pthread_mutex_unlock(&o->mutex);
    return 0;
}
int extent(hybris_icd_window *, uint32_t *width, uint32_t *height) {
    *width = *height = 0;
    return 0;
}
void destroy(hybris_icd_window *window) {
    Owner *o = owner(window);
    disconnect(window);
    pthread_cond_destroy(&o->available);
    pthread_mutex_destroy(&o->mutex);
    delete o;
}
const hybris_icd_window_ops ops = {configure, dequeue, release, release, disconnect, destroy, extent};
}

extern "C" int hybris_icd_window_headless(hybris_icd_window **output) {
    Owner *o = new (std::nothrow) Owner;
    if (!o) return -ENOMEM;
    int error = pthread_mutex_init(&o->mutex, nullptr);
    if (error) { delete o; return -error; }
    pthread_condattr_t attr;
    error = pthread_condattr_init(&attr);
    if (!error) {
        error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (!error) error = pthread_cond_init(&o->available, &attr);
        pthread_condattr_destroy(&attr);
    }
    if (error) { pthread_mutex_destroy(&o->mutex); delete o; return -error; }
    o->base.ops = &ops;
    *output = &o->base;
    return 0;
}
