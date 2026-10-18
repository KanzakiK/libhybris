/*
 * Copyright (c) 2022 Jolla Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-config.h>
#include "window_owner.h"
#include "wayland_window.h"
#include "ordered_presenter.h"
#include <algorithm>
#include <errno.h>
#include <new>
#include <string.h>
#include <unistd.h>
#include <poll.h>

struct present_job { ANativeWindowBuffer *buffer; int fence; };
struct hybris_vk_wayland_window {
    wl_event_queue *queue;
    wl_registry *registry;
    android_wlegl *wlegl;
    WaylandNativeWindow *native;
    wl_display *wrapper;
    OrderedPresenter<present_job, 8> *presenter;
};

static int consume_fence(int fd)
{
    if (fd < 0) return 0;
    pollfd wait = {fd, POLLIN, 0};
    int status;
    do { status = poll(&wait, 1, -1); } while (status < 0 && errno == EINTR);
    int error = status < 0 || !(wait.revents & POLLIN) ? -EIO : 0;
    close(fd);
    return error;
}

static int present(void *data, present_job job, bool discard)
{
    auto *owner = static_cast<hybris_vk_wayland_window *>(data);
    // android_wlegl has no acquire-fence protocol. Wait before commit, but
    // outside BOTH the game's submitting thread and the native pool mutex.
    int error = consume_fence(job.fence);
    ANativeWindow *window = owner->native;
    if (!error) error = discard ? window->cancelBuffer(window, job.buffer, -1)
                               : window->queueBuffer(window, job.buffer, -1);
    job.buffer->common.decRef(&job.buffer->common);
    return error;
}

static void registry_global(void *data, wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version)
{
    auto *owner = static_cast<hybris_vk_wayland_window *>(data);
    if (!owner->wlegl && !strcmp(interface, "android_wlegl"))
        owner->wlegl = static_cast<android_wlegl *>(wl_registry_bind(
            registry, name, &android_wlegl_interface, std::min(2u, version)));
}

static void registry_remove(void *, wl_registry *, uint32_t) {}
static const wl_registry_listener listener = {registry_global, registry_remove};

void hybris_vk_wayland_window_destroy(hybris_vk_wayland_window *owner)
{
    if (!owner) return;
    delete owner->presenter; // drain/join before destroying any native objects
    if (owner->native) {
        owner->native->destroyWlEGLWindow();
        owner->native->common.decRef(&owner->native->common);
    }
    if (owner->wlegl) android_wlegl_destroy(owner->wlegl);
    if (owner->registry) wl_registry_destroy(owner->registry);
    if (owner->wrapper) wl_proxy_wrapper_destroy(owner->wrapper);
    if (owner->queue) wl_event_queue_destroy(owner->queue);
    delete owner;
}

int hybris_vk_wayland_window_create(wl_display *display, wl_surface *surface,
    hybris_vk_wayland_window **out)
{
    *out = nullptr;
    auto *owner = new (std::nothrow) hybris_vk_wayland_window{};
    if (!owner) return -ENOMEM;
    int error = -ENOMEM;
    wl_egl_window *egl_window = nullptr;
    owner->queue = wl_display_create_queue(display);
    if (!owner->queue) goto fail;
    owner->wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    if (!owner->wrapper) goto fail;
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(owner->wrapper), owner->queue);
    owner->registry = wl_display_get_registry(owner->wrapper);
    if (!owner->registry) goto fail;
    if (wl_registry_add_listener(owner->registry, &listener, owner) < 0) goto fail;
    // Finish the sync callback before discovery state can be destroyed.
    if (wl_display_roundtrip_queue(display, owner->queue) < 0) {
        error = -EPIPE;
        goto fail;
    }
    if (!owner->wlegl) {
        error = -ENOTSUP;
        goto fail;
    }
    egl_window = wl_egl_window_create(surface, 1, 1);
    if (!egl_window) goto fail;
    owner->native = new (std::nothrow) WaylandNativeWindow(egl_window, display, owner->wlegl);
    if (!owner->native) {
        wl_egl_window_destroy(egl_window);
        goto fail;
    }
    owner->native->common.incRef(&owner->native->common);
    owner->presenter = new (std::nothrow) OrderedPresenter<present_job, 8>;
    if (!owner->presenter) goto fail;
    error = owner->presenter->start(owner, present);
    if (error) goto fail;
    *out = owner;
    return 0;
fail:
    hybris_vk_wayland_window_destroy(owner);
    return error;
}

ANativeWindow *hybris_vk_wayland_window_native(hybris_vk_wayland_window *owner)
{
    return owner->native;
}

void hybris_vk_wayland_window_resize(hybris_vk_wayland_window *owner,
    unsigned width, unsigned height)
{
    owner->native->resize(width, height);
}

int hybris_vk_wayland_window_dequeue(hybris_vk_wayland_window *owner,
    int64_t timeout_ns, ANativeWindowBuffer **buffer, int *fence_fd)
{
    if (!owner || !owner->native || !buffer || !fence_fd) return -EINVAL;
    *buffer = nullptr;
    *fence_fd = -1;
    BaseNativeWindowBuffer *result = nullptr;
    timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    int error;
    for (;;) {
        error = owner->presenter->status();
        if (error) return error;
        // A blocking dequeue holds the pool mutex while polling Wayland and
        // would prevent our worker from committing the buffer that frees it.
        error = owner->native->dequeueBufferTimeout(&result, fence_fd, 0);
        if (error != -EAGAIN) break;
        if (!timeout_ns) return -EAGAIN;
        if (timeout_ns > 0) {
            timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t elapsed = (int64_t)(now.tv_sec - started.tv_sec) * 1000000000LL
                + now.tv_nsec - started.tv_nsec;
            if (elapsed >= timeout_ns) return -ETIMEDOUT;
        }
        owner->presenter->waitProgress();
    }
    // BaseNativeWindowBuffer has a vtable before its ANativeWindowBuffer base.
    // A pointer-to-pointer reinterpret_cast bypasses this base adjustment.
    *buffer = result;
    return error;
}

int hybris_vk_wayland_window_queue(hybris_vk_wayland_window *owner,
    ANativeWindowBuffer *buffer, int fence_fd)
{
    if (!owner || !owner->native || !buffer) {
        if (fence_fd >= 0) close(fence_fd);
        return -EINVAL;
    }
    buffer->common.incRef(&buffer->common);
    int error = owner->presenter->submit({buffer, fence_fd});
    if (error) {
        // The caller will cancel this rejected image. Finish its GPU writes
        // before returning, never close a live fence and recycle the buffer.
        consume_fence(fence_fd);
        buffer->common.decRef(&buffer->common);
    }
    return error;
}

int hybris_vk_wayland_window_cancel(hybris_vk_wayland_window *owner,
    ANativeWindowBuffer *buffer, int fence_fd)
{
    // The underlying legacy cancel does not consume fences. Complete that
    // ownership contract here before making the buffer reusable.
    int error = 0;
    if (fence_fd >= 0) {
        struct pollfd wait = {fence_fd, POLLIN, 0};
        int status;
        do { status = poll(&wait, 1, -1); } while (status < 0 && errno == EINTR);
        if (status < 0 || !(wait.revents & POLLIN)) error = -EIO;
        close(fence_fd);
    }
    if (error) return error;
    if (!owner || !owner->native || !buffer) return -EINVAL;
    ANativeWindow *window = owner->native;
    return window->cancelBuffer(window, buffer, -1);
}

void hybris_vk_wayland_window_disconnect(hybris_vk_wayland_window *owner)
{
    if (!owner || !owner->native) return;
    owner->presenter->drain();
    owner->native->apiDisconnect(0);
}
