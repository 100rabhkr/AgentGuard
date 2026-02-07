#include "agentguard/delegation_tracker.hpp"

#include <algorithm>
#include <queue>
#include <stack>

namespace agentguard {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

DelegationTracker::DelegationTracker(DelegationConfig config)
    : config_(std::move(config)) {}

// ---------------------------------------------------------------------------
// Agent lifecycle
// ---------------------------------------------------------------------------

void DelegationTracker::register_agent(AgentId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    known_agents_.insert(id);
}

void DelegationTracker::deregister_agent(AgentId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    known_agents_.erase(id);

    // Remove all outgoing edges from this agent
    if (auto it = adj_.find(id); it != adj_.end()) {
        for (const auto& target : it->second) {
            edges_.erase({id, target});
        }
        adj_.erase(it);
    }

    // Remove all incoming edges to this agent
    for (auto& [src, targets] : adj_) {
        if (targets.erase(id)) {
            edges_.erase({src, id});
        }
    }
}

// ---------------------------------------------------------------------------
// Monitor
// ---------------------------------------------------------------------------

void DelegationTracker::set_monitor(std::shared_ptr<Monitor> monitor) {
    std::lock_guard<std::mutex> lock(mutex_);
    monitor_ = std::move(monitor);
}

// ---------------------------------------------------------------------------
// Delegation API
// ---------------------------------------------------------------------------

DelegationResult DelegationTracker::report_delegation(
    AgentId from, AgentId to, const std::string& task_description) {

    DelegationResult result;
    bool cycle_found = false;
    bool cancel_latest = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Validate both agents are known
        if (known_agents_.find(from) == known_agents_.end() ||
            known_agents_.find(to) == known_agents_.end()) {
            return {false, false, {}};
        }

        // Add edge
        adj_[from].insert(to);
        edges_[{from, to}] = DelegationInfo{from, to, task_description, Clock::now()};

        // Check for cycle
        auto cycle_path = detect_cycle_from(from, to);
        if (!cycle_path.empty()) {
            cycle_found = true;
            result.cycle_detected = true;
            result.cycle_path = cycle_path;

            switch (config_.cycle_action) {
                case DelegationCycleAction::NotifyOnly:
                    // Keep edge, delegation accepted
                    result.accepted = true;
                    break;
                case DelegationCycleAction::RejectDelegation:
                    // Remove edge, delegation rejected
                    adj_[from].erase(to);
                    if (adj_[from].empty()) {
                        adj_.erase(from);
                    }
                    edges_.erase({from, to});
                    result.accepted = false;
                    break;
                case DelegationCycleAction::CancelLatest:
                    // Remove edge, delegation cancelled
                    adj_[from].erase(to);
                    if (adj_[from].empty()) {
                        adj_.erase(from);
                    }
                    edges_.erase({from, to});
                    result.accepted = false;
                    cancel_latest = true;
                    break;
            }
        } else {
            result.accepted = true;
            result.cycle_detected = false;
        }
    }

    // Emit events outside lock
    if (result.accepted) {
        emit_event(EventType::DelegationReported,
                   "Delegation reported: agent " + std::to_string(from) +
                   " -> agent " + std::to_string(to),
                   from, to);
    }

    if (cycle_found) {
        emit_event(EventType::DelegationCycleDetected,
                   "Delegation cycle detected involving agent " +
                   std::to_string(from) + " -> agent " + std::to_string(to),
                   from, to, result.cycle_path);
    }

    if (cancel_latest) {
        emit_event(EventType::DelegationCancelled,
                   "Delegation cancelled (cycle prevention): agent " +
                   std::to_string(from) + " -> agent " + std::to_string(to),
                   from, to);
    }

    return result;
}

void DelegationTracker::complete_delegation(AgentId from, AgentId to) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = adj_.find(from); it != adj_.end()) {
            it->second.erase(to);
            if (it->second.empty()) {
                adj_.erase(it);
            }
        }
        edges_.erase({from, to});
    }

    emit_event(EventType::DelegationCompleted,
               "Delegation completed: agent " + std::to_string(from) +
               " -> agent " + std::to_string(to),
               from, to);
}

void DelegationTracker::cancel_delegation(AgentId from, AgentId to) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = adj_.find(from); it != adj_.end()) {
            it->second.erase(to);
            if (it->second.empty()) {
                adj_.erase(it);
            }
        }
        edges_.erase({from, to});
    }

    emit_event(EventType::DelegationCancelled,
               "Delegation cancelled: agent " + std::to_string(from) +
               " -> agent " + std::to_string(to),
               from, to);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<DelegationInfo> DelegationTracker::get_all_delegations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DelegationInfo> result;
    result.reserve(edges_.size());
    for (const auto& [key, info] : edges_) {
        result.push_back(info);
    }
    return result;
}

std::vector<DelegationInfo> DelegationTracker::get_delegations_from(AgentId from) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DelegationInfo> result;
    auto it = adj_.find(from);
    if (it == adj_.end()) {
        return result;
    }
    for (const auto& target : it->second) {
        auto edge_it = edges_.find({from, target});
        if (edge_it != edges_.end()) {
            result.push_back(edge_it->second);
        }
    }
    return result;
}

std::vector<DelegationInfo> DelegationTracker::get_delegations_to(AgentId to) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DelegationInfo> result;
    for (const auto& [src, targets] : adj_) {
        if (targets.count(to)) {
            auto edge_it = edges_.find({src, to});
            if (edge_it != edges_.end()) {
                result.push_back(edge_it->second);
            }
        }
    }
    return result;
}

std::optional<std::vector<AgentId>> DelegationTracker::find_cycle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return detect_any_cycle();
}

// ---------------------------------------------------------------------------
// Cycle detection helpers
// ---------------------------------------------------------------------------

std::vector<AgentId> DelegationTracker::detect_cycle_from(
    AgentId from, AgentId to) const {
    // After adding edge from->to, check if there is a path from 'to' back to 'from'.
    // BFS from 'to'.
    if (from == to) {
        // Self-loop
        return {from, from};
    }

    std::queue<AgentId> queue;
    std::unordered_set<AgentId> visited;
    std::unordered_map<AgentId, AgentId> parent;

    queue.push(to);
    visited.insert(to);

    while (!queue.empty()) {
        AgentId current = queue.front();
        queue.pop();

        auto adj_it = adj_.find(current);
        if (adj_it == adj_.end()) {
            continue;
        }

        for (const auto& neighbor : adj_it->second) {
            if (neighbor == from) {
                // Found cycle! Reconstruct path: from -> ... -> to -> ... -> current -> from
                // First build the path from 'to' to 'current' via parent map
                std::vector<AgentId> path;
                path.push_back(from);

                // Reconstruct from 'to' to 'current'
                std::vector<AgentId> segment;
                AgentId node = current;
                while (node != to) {
                    segment.push_back(node);
                    node = parent.at(node);
                }
                segment.push_back(to);

                // segment is [current, ..., to], reverse to get [to, ..., current]
                std::reverse(segment.begin(), segment.end());

                for (const auto& s : segment) {
                    path.push_back(s);
                }
                path.push_back(from);

                return path;
            }

            if (visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                parent[neighbor] = current;
                queue.push(neighbor);
            }
        }
    }

    return {};  // No cycle
}

std::optional<std::vector<AgentId>> DelegationTracker::detect_any_cycle() const {
    // DFS with 3-color marking: White=0, Gray=1, Black=2
    enum Color { White = 0, Gray = 1, Black = 2 };

    std::unordered_map<AgentId, Color> color;
    std::unordered_map<AgentId, AgentId> parent;

    // Initialize all known agents as White
    for (const auto& agent : known_agents_) {
        color[agent] = White;
    }

    // Also consider agents that appear in adj_ but may not be in known_agents_
    for (const auto& [src, targets] : adj_) {
        if (color.find(src) == color.end()) {
            color[src] = White;
        }
        for (const auto& t : targets) {
            if (color.find(t) == color.end()) {
                color[t] = White;
            }
        }
    }

    std::optional<std::vector<AgentId>> result;

    // DFS using explicit stack to avoid recursion limits
    for (const auto& [start_agent, start_color] : color) {
        if (start_color != White) {
            continue;
        }

        // Iterative DFS
        // Stack stores (node, iterator_index) pairs
        // We use a separate structure to track DFS state
        struct DfsFrame {
            AgentId node;
            std::vector<AgentId> neighbors;
            std::size_t next_idx;
        };

        std::stack<DfsFrame> stk;

        // Start DFS from start_agent
        color[start_agent] = Gray;

        DfsFrame initial_frame;
        initial_frame.node = start_agent;
        initial_frame.next_idx = 0;
        auto adj_it = adj_.find(start_agent);
        if (adj_it != adj_.end()) {
            initial_frame.neighbors.assign(adj_it->second.begin(), adj_it->second.end());
        }
        stk.push(std::move(initial_frame));

        while (!stk.empty()) {
            auto& frame = stk.top();

            if (frame.next_idx < frame.neighbors.size()) {
                AgentId neighbor = frame.neighbors[frame.next_idx];
                frame.next_idx++;

                if (color[neighbor] == Gray) {
                    // Back edge found -- cycle detected
                    // Reconstruct cycle: neighbor -> ... -> frame.node -> neighbor
                    std::vector<AgentId> cycle_path;
                    cycle_path.push_back(neighbor);

                    // Walk the stack from top to find 'neighbor'
                    // The stack contains the current DFS path
                    std::vector<AgentId> stack_nodes;
                    // Collect stack nodes (current path)
                    std::stack<DfsFrame> temp;
                    // We need to extract the path from the stack
                    // The path is: bottom ... frame.node (top)
                    // We want from 'neighbor' to 'frame.node'

                    // Build path from stack
                    std::vector<AgentId> path_from_stack;
                    {
                        // Copy stack to vector
                        std::stack<DfsFrame> copy = stk;
                        while (!copy.empty()) {
                            path_from_stack.push_back(copy.top().node);
                            copy.pop();
                        }
                        // path_from_stack is [top, ..., bottom], reverse to get [bottom, ..., top]
                        std::reverse(path_from_stack.begin(), path_from_stack.end());
                    }

                    // Find 'neighbor' in the path
                    bool found = false;
                    for (std::size_t i = 0; i < path_from_stack.size(); ++i) {
                        if (path_from_stack[i] == neighbor) {
                            cycle_path.clear();
                            for (std::size_t j = i; j < path_from_stack.size(); ++j) {
                                cycle_path.push_back(path_from_stack[j]);
                            }
                            cycle_path.push_back(neighbor);  // Close the cycle
                            found = true;
                            break;
                        }
                    }

                    if (found) {
                        return cycle_path;
                    }
                } else if (color[neighbor] == White) {
                    color[neighbor] = Gray;
                    parent[neighbor] = frame.node;

                    DfsFrame new_frame;
                    new_frame.node = neighbor;
                    new_frame.next_idx = 0;
                    auto it = adj_.find(neighbor);
                    if (it != adj_.end()) {
                        new_frame.neighbors.assign(it->second.begin(), it->second.end());
                    }
                    stk.push(std::move(new_frame));
                }
                // Black nodes are already fully explored, skip
            } else {
                // Done with this node
                color[frame.node] = Black;
                stk.pop();
            }
        }
    }

    return std::nullopt;  // No cycle found
}

// ---------------------------------------------------------------------------
// Event emission
// ---------------------------------------------------------------------------

void DelegationTracker::emit_event(EventType type, const std::string& message,
                                    std::optional<AgentId> from_agent,
                                    std::optional<AgentId> to_agent,
                                    std::vector<AgentId> cycle) {
    std::shared_ptr<Monitor> mon;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mon = monitor_;
    }

    if (!mon) {
        return;
    }

    MonitorEvent event;
    event.type = type;
    event.timestamp = Clock::now();
    event.message = message;
    event.agent_id = from_agent;
    event.target_agent_id = to_agent;

    if (!cycle.empty()) {
        event.cycle_path = std::move(cycle);
    }

    mon->on_event(event);
}

} // namespace agentguard
