#include "agentguard/agent.hpp"

namespace agentguard {

Agent::Agent(AgentId id, std::string name, Priority priority)
    : id_(id)
    , name_(std::move(name))
    , priority_(priority)
{}

AgentId Agent::id() const noexcept { return id_; }
const std::string& Agent::name() const noexcept { return name_; }
Priority Agent::priority() const noexcept { return priority_; }
void Agent::set_priority(Priority p) noexcept { priority_ = p; }
AgentState Agent::state() const noexcept { return state_; }

void Agent::declare_max_need(ResourceTypeId resource_type, ResourceQuantity max_qty) {
    max_needs_[resource_type] = max_qty;
}

const std::unordered_map<ResourceTypeId, ResourceQuantity>& Agent::max_needs() const noexcept {
    return max_needs_;
}

const std::unordered_map<ResourceTypeId, ResourceQuantity>& Agent::current_allocation() const noexcept {
    return allocation_;
}

ResourceQuantity Agent::remaining_need(ResourceTypeId resource_type) const {
    auto max_it = max_needs_.find(resource_type);
    ResourceQuantity max_val = (max_it != max_needs_.end()) ? max_it->second : 0;

    auto alloc_it = allocation_.find(resource_type);
    ResourceQuantity alloc_val = (alloc_it != allocation_.end()) ? alloc_it->second : 0;

    return max_val - alloc_val;
}

void Agent::set_model_identifier(std::string model_id) {
    model_identifier_ = std::move(model_id);
}

const std::string& Agent::model_identifier() const noexcept {
    return model_identifier_;
}

void Agent::set_task_description(std::string desc) {
    task_description_ = std::move(desc);
}

const std::string& Agent::task_description() const noexcept {
    return task_description_;
}

void Agent::set_state(AgentState s) {
    state_ = s;
}

void Agent::allocate(ResourceTypeId rt, ResourceQuantity qty) {
    allocation_[rt] += qty;
    if (state_ == AgentState::Registered) {
        state_ = AgentState::Active;
    }
}

void Agent::deallocate(ResourceTypeId rt, ResourceQuantity qty) {
    auto it = allocation_.find(rt);
    if (it != allocation_.end()) {
        it->second -= qty;
        if (it->second <= 0) {
            allocation_.erase(it);
        }
    }
}

} // namespace agentguard
