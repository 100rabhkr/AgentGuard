#pragma once

#include "agentguard/types.hpp"
#include "agentguard/config.hpp"
#include "agentguard/monitor.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace agentguard {

struct DelegationResult {
    bool accepted{true};              // false if rejected due to cycle
    bool cycle_detected{false};
    std::vector<AgentId> cycle_path;  // e.g., [A, B, C, A]
};

class DelegationTracker {
public:
    explicit DelegationTracker(DelegationConfig config);
    ~DelegationTracker() = default;

    // Non-copyable
    DelegationTracker(const DelegationTracker&) = delete;
    DelegationTracker& operator=(const DelegationTracker&) = delete;

    // Agent lifecycle
    void register_agent(AgentId id);
    void deregister_agent(AgentId id);  // removes ALL edges involving this agent

    void set_monitor(std::shared_ptr<Monitor> monitor);

    // Delegation API
    DelegationResult report_delegation(AgentId from, AgentId to,
                                       const std::string& task_description = "");
    void complete_delegation(AgentId from, AgentId to);
    void cancel_delegation(AgentId from, AgentId to);

    // Queries
    std::vector<DelegationInfo> get_all_delegations() const;
    std::vector<DelegationInfo> get_delegations_from(AgentId from) const;
    std::vector<DelegationInfo> get_delegations_to(AgentId to) const;
    std::optional<std::vector<AgentId>> find_cycle() const;

private:
    DelegationConfig config_;
    mutable std::mutex mutex_;

    // Adjacency list: from -> set of to
    std::unordered_map<AgentId, std::unordered_set<AgentId>> adj_;

    // Edge metadata
    struct PairHash {
        std::size_t operator()(const std::pair<AgentId, AgentId>& p) const {
            auto h1 = std::hash<AgentId>{}(p.first);
            auto h2 = std::hash<AgentId>{}(p.second);
            return h1 ^ (h2 * 2654435761ULL);
        }
    };
    std::unordered_map<std::pair<AgentId, AgentId>, DelegationInfo, PairHash> edges_;

    std::unordered_set<AgentId> known_agents_;
    std::shared_ptr<Monitor> monitor_;

    // Cycle detection: after adding edge (from, to), check if there's a path from 'to' back to 'from'
    std::vector<AgentId> detect_cycle_from(AgentId from, AgentId to) const;

    // Full graph cycle detection (DFS with coloring)
    std::optional<std::vector<AgentId>> detect_any_cycle() const;

    void emit_event(EventType type, const std::string& message,
                    std::optional<AgentId> from_agent = std::nullopt,
                    std::optional<AgentId> to_agent = std::nullopt,
                    std::vector<AgentId> cycle = {});
};

} // namespace agentguard
