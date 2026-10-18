/* SPDX-License-Identifier: Apache-2.0
 * Native window scheduling regression, real production queue/worker/dequeue.
 * XCB and gralloc are deterministic fakes; this is not rendered-frame evidence.
 */
#include <atomic>
#include <deque>
#include <thread>
#include <vector>
#include <fcntl.h>
#include "../../hybris/vulkan/icd/native_window_x11.cpp"

static int notifications[2];
static pthread_mutex_t mock_lock = PTHREAD_MUTEX_INITIALIZER;
struct Release { xcb_generic_event_t *event; bool notified; };
static std::deque<Release> releases;
static std::vector<uint32_t> presented;
static uint32_t pending_serial;
static bool reject_request;
static std::atomic<bool> block_check, check_entered;
static std::atomic<bool> silent_notifications;
static std::atomic<unsigned> allocations, destructions;

extern "C" int hybris_gralloc_allocate(int w, int, int, int, buffer_handle_t *out, uint32_t *stride) {
    auto *handle = static_cast<native_handle_t *>(calloc(1, sizeof(native_handle_t)));
    assert(handle); handle->version = sizeof(native_handle_t); *out = handle; *stride = w;
    ++allocations; return 0;
}
extern "C" int hybris_gralloc_release(buffer_handle_t handle, int) {
    free(const_cast<native_handle_t *>(handle)); ++destructions; return 0;
}
extern "C" int xcb_connection_has_error(xcb_connection_t *) { return 0; }
extern "C" int xcb_get_file_descriptor(xcb_connection_t *) { return notifications[0]; }
extern "C" xcb_get_geometry_cookie_t xcb_get_geometry(xcb_connection_t *, xcb_drawable_t) { return {0}; }
extern "C" xcb_get_geometry_reply_t *xcb_get_geometry_reply(xcb_connection_t *, xcb_get_geometry_cookie_t, xcb_generic_error_t **) { return nullptr; }
extern "C" xcb_get_window_attributes_cookie_t xcb_get_window_attributes(xcb_connection_t *, xcb_window_t) { return {0}; }
extern "C" xcb_get_window_attributes_reply_t *xcb_get_window_attributes_reply(xcb_connection_t *, xcb_get_window_attributes_cookie_t, xcb_generic_error_t **) { return nullptr; }
extern "C" unsigned xcb_send_request(xcb_connection_t *, int, struct iovec *, const xcb_protocol_request_t *) { return 1; }
extern "C" unsigned xcb_send_request_with_fds(xcb_connection_t *, int, struct iovec *parts,
                                             const xcb_protocol_request_t *, unsigned n, int *fds) {
    pthread_mutex_lock(&mock_lock);
    pending_serial = static_cast<tawc_dri_present_buffer_req *>(parts[0].iov_base)->serial;
    presented.push_back(pending_serial);
    pthread_mutex_unlock(&mock_lock);
    for (unsigned i = 0; i < n; ++i) close(fds[i]);
    return 1;
}
extern "C" xcb_generic_error_t *xcb_request_check(xcb_connection_t *, xcb_void_cookie_t) {
    pthread_mutex_lock(&mock_lock);
    xcb_generic_error_t *error = nullptr;
    bool presenting = pending_serial != 0;
    if (pending_serial) {
        if (reject_request) {
            error = static_cast<xcb_generic_error_t *>(calloc(1, sizeof(*error)));
            error->error_code = 8;
        } else {
            /* libxcb appends full_sequence to a 32-byte wire event. */
            auto *generic = static_cast<xcb_generic_event_t *>(calloc(1, sizeof(xcb_ge_generic_event_t)));
            auto *event = reinterpret_cast<tawc_dri_buffer_release_event *>(generic);
            reinterpret_cast<xcb_ge_generic_event_t *>(event)->event_type = TAWC_DRI_EVENT_BUFFER_RELEASE;
            event->serial = pending_serial;
            bool notify = !silent_notifications;
            releases.push_back({generic, notify});
            if (notify) assert(write(notifications[1], "x", 1) == 1);
        }
        pending_serial = 0;
    }
    pthread_mutex_unlock(&mock_lock);
    if (presenting) {
        check_entered = true;
        while (block_check) usleep(1000);
    }
    return error;
}
extern "C" xcb_generic_event_t *xcb_poll_for_special_event(xcb_connection_t *, xcb_special_event_t *) {
    pthread_mutex_lock(&mock_lock);
    xcb_generic_event_t *event = nullptr;
    if (!releases.empty()) {
        Release release = releases.front(); releases.pop_front();
        event = release.event;
        if (release.notified) { char byte; assert(read(notifications[0], &byte, 1) == 1); }
    }
    pthread_mutex_unlock(&mock_lock);
    return event;
}
extern "C" void xcb_unregister_for_special_event(xcb_connection_t *, xcb_special_event_t *) {
    pthread_mutex_lock(&mock_lock);
    while (!releases.empty()) {
        Release release = releases.front(); releases.pop_front(); free(release.event);
        if (release.notified) { char byte; assert(read(notifications[0], &byte, 1) == 1); }
    }
    pthread_mutex_unlock(&mock_lock);
}

static Owner *make_owner() {
    auto *o = static_cast<Owner *>(calloc(1, sizeof(Owner)));
    assert(o); o->base.ops = &ops;
    o->connection = reinterpret_cast<xcb_connection_t *>(1);
    o->events = reinterpret_cast<xcb_special_event_t *>(1);
    pthread_mutex_init(&o->mutex, nullptr);
    pthread_cond_init(&o->work_ready, nullptr); pthread_cond_init(&o->idle, nullptr);
    assert(!pthread_create(&o->worker, nullptr, present_worker, o)); o->worker_started = true;
    assert(!configure(&o->base, 64, 64, 1, 0, 2));
    return o;
}
static ANativeWindowBuffer *take(Owner *o) {
    ANativeWindowBuffer *b = nullptr; int fd = -2;
    assert(!dequeue(&o->base, 1000000000, &b, &fd)); assert(b && fd == -1); return b;
}
static void wait_idle(Owner *o) {
    pthread_mutex_lock(&o->mutex);
    while (o->busy || o->work_head) pthread_cond_wait(&o->idle, &o->mutex);
    pthread_mutex_unlock(&o->mutex);
}
int main() {
    assert(!pipe(notifications));
    Owner *o = make_owner();
    auto *a = take(o), *b = take(o);
    int fence[2]; assert(!pipe(fence));
    int64_t started = now_ns();
    assert(!queue(&o->base, a, fence[0]));
    assert(now_ns() - started < 50000000); /* unsignaled fence must not block caller */
    assert(!queue(&o->base, b, -1));
    usleep(10000);
    pthread_mutex_lock(&mock_lock); assert(presented.empty()); pthread_mutex_unlock(&mock_lock);
    std::thread signal([&] { usleep(10000); assert(write(fence[1], "x", 1) == 1); close(fence[1]); });
    auto *available = take(o); signal.join();
    assert(!cancel(&o->base, available, -1));
    wait_idle(o);
    assert(fcntl(fence[0], F_GETFD) == -1 && errno == EBADF);
    assert(presented.size() == 2 && presented[0] < presented[1]);
    disconnect(&o->base);
    assert(allocations == destructions);
    assert(!configure(&o->base, 128, 96, 1, 0, 2));
    a = take(o); assert(!pipe(fence)); assert(!queue(&o->base, a, fence[0]));
    std::thread flush_signal([&] { usleep(10000); assert(write(fence[1], "x", 1) == 1); close(fence[1]); });
    disconnect(&o->base); flush_signal.join();
    assert(allocations == destructions);
    assert(!configure(&o->base, 32, 32, 1, 0, 2));
    a = take(o); reject_request = true; assert(!queue(&o->base, a, -1)); wait_idle(o);
    int fd; ANativeWindowBuffer *none;
    assert(dequeue(&o->base, 0, &none, &fd) == -EPIPE);
    destroy(&o->base);
    assert(allocations == destructions && releases.empty());
    reject_request = false;
    /* An early release must not recycle a buffer before request checking,
     * but the server round trip must not block acquisition of another one. */
    o = make_owner(); a = take(o);
    check_entered = false; block_check = true;
    assert(!queue(&o->base, a, -1));
    started = now_ns();
    while (!check_entered && now_ns() - started < 1000000000) usleep(1000);
    assert(check_entered);
    std::thread unblock([] { usleep(150000); block_check = false; });
    started = now_ns(); b = take(o);
    assert(now_ns() - started < 50000000);
    assert(b != a); assert(!queue(&o->base, b, -1));
    assert(dequeue(&o->base, 0, &none, &fd) == -EAGAIN);
    unblock.join(); wait_idle(o);
    a = take(o); assert(!cancel(&o->base, a, -1));
    disconnect(&o->base); destroy(&o->base);
    assert(allocations == destructions && releases.empty());
    /* Simulate another XCB reader consuming socket readiness after routing
     * releases into the special-event queue. Pool availability must not wait
     * for the old 50-ms socket poll to expire. */
    o = make_owner(); a = take(o); b = take(o);
    silent_notifications = true;
    assert(!pipe(fence)); assert(!queue(&o->base, a, fence[0]));
    assert(!queue(&o->base, b, -1));
    std::thread private_release([&] { usleep(5000); assert(write(fence[1], "x", 1) == 1); close(fence[1]); });
    started = now_ns(); available = take(o);
    int64_t private_wait = now_ns() - started;
    private_release.join();
    assert(private_wait < 30000000);
    assert(!cancel(&o->base, available, -1)); wait_idle(o);
    disconnect(&o->base); destroy(&o->base);
    silent_notifications = false;
    assert(allocations == destructions && releases.empty());
    o = make_owner(); a = take(o);
    assert(!pipe(fence)); assert(!queue(&o->base, a, fence[0]));
    std::thread destroy_signal([&] { usleep(10000); assert(write(fence[1], "x", 1) == 1); close(fence[1]); });
    destroy(&o->base); destroy_signal.join();
    assert(allocations == destructions && releases.empty());
    close(notifications[0]); close(notifications[1]);
    puts("PASS async fence/order/reuse/private-release/early-release/round-trip/disconnect/resize/error/destroy/lifetime");
}
