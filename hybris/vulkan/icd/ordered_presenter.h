/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <time.h>

/* One consumer preserves present order. The native buffer pool, not this
 * queue, decides when an image can be acquired again. No per-frame allocation.
 * consume owns each accepted job, including its fence and buffer reference. */
template<class Job, size_t Capacity> class OrderedPresenter {
public:
    using Consumer = int (*)(void *, Job, bool);
    OrderedPresenter() {
        pthread_mutex_init(&mutex, nullptr);
        pthread_cond_init(&changed, nullptr);
    }
    ~OrderedPresenter() {
        if (started) {
            pthread_mutex_lock(&mutex);
            stopping = true;
            pthread_cond_broadcast(&changed);
            pthread_mutex_unlock(&mutex);
            pthread_join(thread, nullptr);
        }
        pthread_cond_destroy(&changed);
        pthread_mutex_destroy(&mutex);
    }
    OrderedPresenter(const OrderedPresenter &) = delete;
    OrderedPresenter &operator=(const OrderedPresenter &) = delete;
    int start(void *context, Consumer consumer) {
        data = context;
        consume = consumer;
        int status = pthread_create(&thread, nullptr, run, this);
        started = !status;
        return -status;
    }
    // A rejected job remains owned by the caller.
    int submit(Job job) {
        pthread_mutex_lock(&mutex);
        while (count == Capacity && !error && !stopping)
            pthread_cond_wait(&changed, &mutex);
        int status = error ? error : stopping ? -EPIPE : 0;
        if (!status) {
            jobs[(head + count) % Capacity] = job;
            ++count;
            pthread_cond_broadcast(&changed);
        }
        pthread_mutex_unlock(&mutex);
        return status;
    }
    int status() {
        pthread_mutex_lock(&mutex);
        int result = error;
        pthread_mutex_unlock(&mutex);
        return result;
    }
    void drain() {
        pthread_mutex_lock(&mutex);
        while (count || busy) pthread_cond_wait(&changed, &mutex);
        pthread_mutex_unlock(&mutex);
    }
    // Release events still belong to Wayland. Recheck them even if the worker
    // has no new completion; never wait holding the native pool mutex.
    void waitProgress() {
        timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 1000000;
        if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
        pthread_mutex_lock(&mutex);
        if (!error) pthread_cond_timedwait(&changed, &mutex, &until);
        pthread_mutex_unlock(&mutex);
    }
private:
    static void *run(void *argument) {
        auto *self = static_cast<OrderedPresenter *>(argument);
        pthread_mutex_lock(&self->mutex);
        for (;;) {
            while (!self->count && !self->stopping)
                pthread_cond_wait(&self->changed, &self->mutex);
            if (!self->count && self->stopping) break;
            Job job = self->jobs[self->head];
            self->head = (self->head + 1) % Capacity;
            --self->count;
            self->busy = true;
            bool discard = self->error != 0;
            pthread_cond_broadcast(&self->changed);
            pthread_mutex_unlock(&self->mutex);
            int result = self->consume(self->data, job, discard);
            pthread_mutex_lock(&self->mutex);
            if (result && !self->error) self->error = result;
            self->busy = false;
            pthread_cond_broadcast(&self->changed);
        }
        pthread_mutex_unlock(&self->mutex);
        return nullptr;
    }
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread{};
    Job jobs[Capacity]{};
    size_t head = 0, count = 0;
    bool busy = false, stopping = false, started = false;
    int error = 0;
    void *data = nullptr;
    Consumer consume = nullptr;
};
