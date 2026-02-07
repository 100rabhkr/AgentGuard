#include "agentguard/progress_tracker.hpp"

namespace agentguard {

ProgressTracker::ProgressTracker(ProgressConfig config)
    : config_(std::move(config)) {}

ProgressTracker::~ProgressTracker() {
    if (running_.load()) {
        stop();
    }
}

void ProgressTracker::register_agent(AgentId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    ProgressRecord record;
    record.last_update = Clock::now();
    records_[id] = std::move(record);
}

void ProgressTracker::deregister_agent(AgentId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.erase(id);
}

void ProgressTracker::report_progress(AgentId id, const std::string& metric_name, double value) {
    bool was_stalled = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(id);
        if (it == records_.end()) {
            return;
        }

        auto& record = it->second;
        record.metrics[metric_name] = value;
        record.last_update = Clock::now();

        if (record.is_stalled) {
            was_stalled = true;
            record.is_stalled = false;
        }
    }

    // Outside the lock: emit events
    emit_event(EventType::AgentProgressReported,
               "Agent " + std::to_string(id) + " reported progress: " + metric_name + " = " + std::to_string(value),
               id);

    if (was_stalled) {
        emit_event(EventType::AgentStallResolved,
                   "Agent " + std::to_string(id) + " stall resolved after progress report",
                   id);
    }
}

void ProgressTracker::set_agent_stall_threshold(AgentId id, Duration threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find(id);
    if (it != records_.end()) {
        it->second.stall_threshold = threshold;
    }
}

bool ProgressTracker::is_stalled(AgentId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find(id);
    if (it == records_.end()) {
        return false;
    }
    return it->second.is_stalled;
}

std::vector<AgentId> ProgressTracker::get_stalled_agents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AgentId> stalled;
    for (const auto& [id, record] : records_) {
        if (record.is_stalled) {
            stalled.push_back(id);
        }
    }
    return stalled;
}

std::optional<ProgressRecord> ProgressTracker::get_progress(AgentId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find(id);
    if (it == records_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void ProgressTracker::start(std::shared_ptr<Monitor> monitor, StallActionCallback stall_action) {
    monitor_ = std::move(monitor);
    stall_action_ = std::move(stall_action);
    running_.store(true);
    checker_thread_ = std::thread(&ProgressTracker::check_loop, this);
}

void ProgressTracker::stop() {
    running_.store(false);
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        cv_.notify_all();
    }
    if (checker_thread_.joinable()) {
        checker_thread_.join();
    }
}

void ProgressTracker::check_loop() {
    while (running_.load()) {
        check_for_stalls();

        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, config_.check_interval, [this] {
            return !running_.load();
        });
    }
}

void ProgressTracker::check_for_stalls() {
    std::vector<AgentId> newly_stalled;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Clock::now();

        for (auto& [id, record] : records_) {
            Duration threshold = record.stall_threshold.value_or(config_.default_stall_threshold);

            if (!record.is_stalled &&
                record.last_update != Timestamp{} &&
                (now - record.last_update) > threshold) {
                record.is_stalled = true;
                newly_stalled.push_back(id);
            }
        }
    }

    // Outside the lock: emit events and invoke stall actions
    for (AgentId id : newly_stalled) {
        emit_event(EventType::AgentStalled,
                   "Agent " + std::to_string(id) + " has stalled (no progress reported)",
                   id);

        if (config_.auto_release_on_stall && stall_action_) {
            stall_action_(id);
        }
    }
}

void ProgressTracker::emit_event(EventType type, const std::string& message, AgentId agent_id) {
    MonitorEvent event;
    event.type = type;
    event.timestamp = Clock::now();
    event.message = message;
    event.agent_id = agent_id;

    if (monitor_) {
        monitor_->on_event(event);
    }
}

} // namespace agentguard
