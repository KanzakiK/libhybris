/* SPDX-License-Identifier: Apache-2.0 */
#include "../../hybris/vulkan/icd/ordered_presenter.h"
#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <future>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <unistd.h>
#include <vector>

struct Job { int sequence, fence; };
struct Context {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    std::vector<int> presented, discarded;
    int failure = -1;
};
static int consume(void *data, Job job, bool discard)
{
    auto &state = *static_cast<Context *>(data);
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        state.entered = true;
        state.changed.notify_all();
    }
    if (job.fence >= 0) {
        char signal;
        assert(read(job.fence, &signal, 1) == 1);
        assert(close(job.fence) == 0);
    }
    (discard ? state.discarded : state.presented).push_back(job.sequence);
    return job.sequence == state.failure ? -EIO : 0;
}
static void waitEntered(Context &state)
{
    std::unique_lock<std::mutex> guard(state.mutex);
    assert(state.changed.wait_for(guard, std::chrono::seconds(2), [&] { return state.entered; }));
}
int main()
{
    alarm(10); // Fail deadlocks instead of hanging the component runner.
    using Presenter = OrderedPresenter<Job, 8>;
    {
        Context state;
        Presenter presenter;
        assert(presenter.start(&state, consume) == 0);
        int fence[2];
        assert(pipe(fence) == 0);
        assert(presenter.submit({0, fence[0]}) == 0);
        waitEntered(state);
        // GPU fence is unsignaled, yet every following submit can return.
        for (int i = 1; i < 8; ++i) assert(presenter.submit({i, -1}) == 0);
        auto draining = std::async(std::launch::async, [&] { presenter.drain(); });
        assert(draining.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        assert(write(fence[1], "x", 1) == 1);
        close(fence[1]);
        assert(draining.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        draining.get();
        assert(state.presented.size() == 8 && state.discarded.empty());
        for (int i = 0; i < 8; ++i) assert(state.presented[i] == i);
        assert(fcntl(fence[0], F_GETFD) == -1 && errno == EBADF);
        assert(presenter.status() == 0);
        puts("PASS nonblocking submit, FIFO order, fence ownership, draining");
    }
    {
        Context state;
        state.failure = 0;
        Presenter presenter;
        assert(presenter.start(&state, consume) == 0);
        int fence[2];
        assert(pipe(fence) == 0);
        assert(presenter.submit({0, fence[0]}) == 0);
        waitEntered(state);
        assert(presenter.submit({1, -1}) == 0);
        assert(write(fence[1], "x", 1) == 1);
        close(fence[1]);
        presenter.drain();
        assert(presenter.status() == -EIO);
        assert(state.presented == std::vector<int>{0});
        assert(state.discarded == std::vector<int>{1});
        assert(presenter.submit({2, -1}) == -EIO);
        puts("PASS deferred failure, pending-job discard, rejected ownership");
    }
    {
        Context state;
        auto presenter = std::make_unique<Presenter>();
        assert(presenter->start(&state, consume) == 0);
        int fence[2];
        assert(pipe(fence) == 0);
        assert(presenter->submit({0, fence[0]}) == 0);
        waitEntered(state);
        assert(presenter->submit({1, -1}) == 0);
        auto destroying = std::async(std::launch::async, [&] { presenter.reset(); });
        assert(destroying.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        assert(write(fence[1], "x", 1) == 1);
        close(fence[1]);
        assert(destroying.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        destroying.get();
        assert((state.presented == std::vector<int>{0, 1}));
        puts("PASS destroy joins after draining accepted jobs");
    }
    return 0;
}
