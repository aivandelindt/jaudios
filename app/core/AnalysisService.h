#pragma once

#include "SpscQueue.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace jaudios
{

/** Runs analysis / ML jobs on a dedicated worker thread and delivers results on the message thread.

    - One worker thread (`juce::ThreadPool` of size 1): ML jobs are heavy and run sequentially.
      That also makes the completion queue single-producer.
    - Completion: worker pushes into a lock-free SPSC FIFO and calls triggerAsyncUpdate();
      handleAsyncUpdate() (message thread) runs the callbacks.
*/
class AnalysisService final : private juce::AsyncUpdater
{
public:
    AnalysisService();
    ~AnalysisService() override;

    /** `work` runs on the analysis thread; `onDone` runs on the message thread with its result. */
    template <typename Result>
    void submit (std::function<Result()> work, std::function<void (Result)> onDone)
    {
        submitErased ([work = std::move (work), onDone = std::move (onDone)]() mutable -> Completion
        {
            auto result = std::make_shared<Result> (work());
            return [onDone = std::move (onDone), result]() mutable { onDone (std::move (*result)); };
        });
    }

    /** Cancels queued jobs and waits for the running one (message thread). */
    void cancelAll (int timeoutMs = 5000);

    /** Blocks until all queued jobs ran and their callbacks were delivered. Message thread;
        intended for the CLI and tests (spins the completion queue manually). */
    void waitUntilIdle();

    int getNumPendingJobs() const { return pool.getNumJobs(); }

private:
    using Completion = std::function<void()>;

    void submitErased (std::function<Completion()> job);
    void handleAsyncUpdate() override;
    void drainCompletions();

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Analysis").withNumberOfThreads (1) };
    SpscQueue<Completion*, 256> completions;
};

} // namespace jaudios
