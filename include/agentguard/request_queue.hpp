#pragma once

#include "agentguard/types.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace agentguard {

class RequestQueue {
public:
    explicit RequestQueue(std::size_t max_queue_size = 10000);

    // Enqueue a new resource request. Assigns and returns the RequestId.
    RequestId enqueue(ResourceRequest request);

    // Dequeue the highest-priority request.
    std::optional<ResourceRequest> dequeue();

    // Peek at the highest-priority request without removing it.
    std::optional<ResourceRequest> peek() const;

    // Cancel a specific request.
    bool cancel(RequestId id);

    // Cancel all requests from a specific agent. Returns count removed.
    std::size_t cancel_all_for_agent(AgentId agent_id);

    // Get all pending requests (snapshot).
    std::vector<ResourceRequest> get_all_pending() const;

    // Get pending requests for a specific resource type.
    std::vector<ResourceRequest> get_pending_for_resource(ResourceTypeId rt) const;

    // Expire timed-out requests. Returns the expired request IDs.
    std::vector<RequestId> expire_timed_out();

    // Size and capacity.
    std::size_t size() const;
    bool empty() const;
    bool full() const;
    std::size_t max_size() const noexcept;

    // Block until a request is available or timeout elapses.
    std::optional<ResourceRequest> wait_and_dequeue(Duration timeout);

    // Notify waiting threads (called when new requests are enqueued).
    void notify();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t max_queue_size_;

    // Sorted vector (priority desc, then submission time asc)
    std::vector<ResourceRequest> requests_;

    RequestId next_request_id_{1};

    // Compare for ordering: higher priority first, then earlier submission
    static bool compare(const ResourceRequest& a, const ResourceRequest& b);

    // Resort after modification
    void sort_requests();
};

} // namespace agentguard
