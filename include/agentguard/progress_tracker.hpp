#pragma once

#include "agentguard/types.hpp"
#include "agentguard/config.hpp"
#include "agentguard/monitor.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace agentguard {

struct ProgressRecord {
    std::unordered_map<std::string, double> metrics;  // metric_name -> latest value
    Timestamp last_update{};                           // last progress report time
    std::optional<Duration> stall_threshold;           // per-agent override
    bool is_stalled{false};
};

class ProgressTracker {
public:
    using StallActionCallback = std::function<void(AgentId)>;

    explicit ProgressTracker(ProgressConfig config);
    ~ProgressTracker();

    // Non-copyable
    ProgressTracker(const ProgressTracker&) = delete;
    ProgressTracker& operator=(const ProgressTracker&) = delete;

    // Agent lifecycle
    void register_agent(AgentId id);
    void deregister_agent(AgentId id);

    // Progress reporting
    void report_progress(AgentId id, const std::string& metric_name, double value);

    // Per-agent config
    void set_agent_stall_threshold(AgentId id, Duration threshold);

    // Queries
    bool is_stalled(AgentId id) const;
    std::vector<AgentId> get_stalled_agents() const;
    std::optional<ProgressRecord> get_progress(AgentId id) const;

    // Lifecycle
    void start(std::shared_ptr<Monitor> monitor, StallActionCallback stall_action = nullptr);
    void stop();

private:
    ProgressConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<AgentId, ProgressRecord> records_;
    std::thread checker_thread_;
    std::atomic<bool> running_{false};
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::shared_ptr<Monitor> monitor_;
    StallActionCallback stall_action_;

    void check_loop();
    void check_for_stalls();
    void emit_event(EventType type, const std::string& message, AgentId agent_id);
};

} // namespace agentguard
