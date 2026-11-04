#include "AnalysisService.h"

namespace jaudios
{

AnalysisService::AnalysisService() = default;

AnalysisService::~AnalysisService()
{
    cancelPendingUpdate();
    cancelAll();

    Completion* c = nullptr;
    while (completions.pop (c))
        delete c;
}

void AnalysisService::submitErased (std::function<Completion()> job)
{
    pool.addJob ([this, job = std::move (job)]
    {
        auto* completion = new Completion (job());

        // Completion queue is full only if the message thread has stalled for 255 jobs; back off.
        while (! completions.push (completion))
            juce::Thread::sleep (1);

        triggerAsyncUpdate();
    });
}

void AnalysisService::cancelAll (int timeoutMs)
{
    pool.removeAllJobs (true, timeoutMs);
}

void AnalysisService::waitUntilIdle()
{
    while (pool.getNumJobs() > 0)
        juce::Thread::sleep (1);

    drainCompletions();
}

void AnalysisService::handleAsyncUpdate()
{
    drainCompletions();
}

void AnalysisService::drainCompletions()
{
    Completion* c = nullptr;
    while (completions.pop (c))
    {
        std::unique_ptr<Completion> owned (c);
        (*owned)();
    }
}

} // namespace jaudios
