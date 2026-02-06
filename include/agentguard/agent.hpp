#pragma once

#include "agentguard/types.hpp"
#include <string>
#include <unordered_map>

namespace agentguard {

class ResourceManager;

class Agent {
public:
    Agent(AgentId id, std::string name, Priority priority = PRIORITY_NORMAL);

    AgentId id() const noexcept;
    const std::string& name() const noexcept;

    Priority priority() const noexcept;
    void set_priority(Priority p) noexcept;

    AgentState state() const noexcept;

    // Declare maximum resource needs (must call before requesting)
    void declare_max_need(ResourceTypeId resource_type, ResourceQuantity max_qty);

    const std::unordered_map<ResourceTypeId, ResourceQuantity>& max_needs() const noexcept;
    const std::unordered_map<ResourceTypeId, ResourceQuantity>& current_allocation() const noexcept;

    // How much more this agent might still need for a resource type
    ResourceQuantity remaining_need(ResourceTypeId resource_type) const;

    // AI-specific metadata
    void set_model_identifier(std::string model_id);
    const std::string& model_identifier() const noexcept;

    void set_task_description(std::string desc);
    const std::string& task_description() const noexcept;

private:
    AgentId      id_;
    std::string  name_;
    Priority     priority_;
    AgentState   state_{AgentState::Registered};
    std::string  model_identifier_;
    std::string  task_description_;

    std::unordered_map<ResourceTypeId, ResourceQuantity> max_needs_;
    std::unordered_map<ResourceTypeId, ResourceQuantity> allocation_;

    void set_state(AgentState s);
    void allocate(ResourceTypeId rt, ResourceQuantity qty);
    void deallocate(ResourceTypeId rt, ResourceQuantity qty);

    friend class ResourceManager;
};

} // namespace agentguard
