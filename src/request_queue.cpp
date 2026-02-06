#include "agentguard/request_queue.hpp"
#include "agentguard/exceptions.hpp"

#include <algorithm>

namespace agentguard {

RequestQueue::RequestQueue(std::size_t max_queue_size)
    : max_queue_size_(max_queue_size)
{}

RequestId RequestQueue::enqueue(ResourceRequest request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (requests_.size() >= max_queue_size_) {
        throw QueueFullException();
    }
    request.id = next_request_id_++;
    request.submitted_at = Clock::now();
    requests_.push_back(std::move(request));
    sort_requests();
    cv_.notify_one();
    return next_request_id_ - 1;
}

std::optional<ResourceRequest> RequestQueue::dequeue() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (requests_.empty()) {
        return std::nullopt;
    }
    auto req = std::move(requests_.front());
    requests_.erase(requests_.begin());
    return req;
}

std::optional<ResourceRequest> RequestQueue::peek() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (requests_.empty()) {
        return std::nullopt;
    }
    return requests_.front();
}

bool RequestQueue::cancel(RequestId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(requests_.begin(), requests_.end(),
        [id](const ResourceRequest& r) { return r.id == id; });
    if (it != requests_.end()) {
        if (it->callback) {
            it->callback(it->id, RequestStatus::Cancelled);
        }
        requests_.erase(it);
        return true;
    }
    return false;
}

std::size_t RequestQueue::cancel_all_for_agent(AgentId agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    auto it = requests_.begin();
    while (it != requests_.end()) {
        if (it->agent_id == agent_id) {
            if (it->callback) {
                it->callback(it->id, RequestStatus::Cancelled);
            }
            it = requests_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

std::vector<ResourceRequest> RequestQueue::get_all_pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

std::vector<ResourceRequest> RequestQueue::get_pending_for_resource(ResourceTypeId rt) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ResourceRequest> result;
    for (auto& req : requests_) {
        if (req.resource_type == rt) {
            result.push_back(req);
        }
    }
    return result;
}

std::vector<RequestId> RequestQueue::expire_timed_out() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = Clock::now();
    std::vector<RequestId> expired;

    auto it = requests_.begin();
    while (it != requests_.end()) {
        if (it->timeout.has_value()) {
            auto deadline = it->submitted_at + it->timeout.value();
            if (now >= deadline) {
                expired.push_back(it->id);
                if (it->callback) {
                    it->callback(it->id, RequestStatus::TimedOut);
                }
                it = requests_.erase(it);
                continue;
            }
        }
        ++it;
    }

    return expired;
}

std::size_t RequestQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
}

bool RequestQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.empty();
}

bool RequestQueue::full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size() >= max_queue_size_;
}

std::size_t RequestQueue::max_size() const noexcept {
    return max_queue_size_;
}

std::optional<ResourceRequest> RequestQueue::wait_and_dequeue(Duration timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !requests_.empty(); })) {
        return std::nullopt;
    }
    auto req = std::move(requests_.front());
    requests_.erase(requests_.begin());
    return req;
}

void RequestQueue::notify() {
    cv_.notify_all();
}

bool RequestQueue::compare(const ResourceRequest& a, const ResourceRequest& b) {
    // Higher priority first
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    // Earlier submission first (FIFO within same priority)
    return a.submitted_at < b.submitted_at;
}

void RequestQueue::sort_requests() {
    std::stable_sort(requests_.begin(), requests_.end(), compare);
}

} // namespace agentguard
