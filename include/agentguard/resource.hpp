#pragma once

#include "agentguard/types.hpp"
#include <optional>
#include <string>

namespace agentguard {

class ResourceManager;

class Resource {
public:
    Resource(ResourceTypeId id,
             std::string name,
             ResourceCategory category,
             ResourceQuantity total_capacity);

    ResourceTypeId id() const noexcept;
    const std::string& name() const noexcept;
    ResourceCategory category() const noexcept;
    ResourceQuantity total_capacity() const noexcept;
    ResourceQuantity allocated() const noexcept;
    ResourceQuantity available() const noexcept;

    // Dynamic capacity adjustment. Returns false if new_capacity < allocated.
    bool set_total_capacity(ResourceQuantity new_capacity);

    // AI-specific metadata
    void set_replenish_interval(Duration interval);
    std::optional<Duration> replenish_interval() const noexcept;

    void set_cost_per_unit(double cost);
    std::optional<double> cost_per_unit() const noexcept;

private:
    ResourceTypeId   id_;
    std::string      name_;
    ResourceCategory category_;
    ResourceQuantity total_capacity_;
    ResourceQuantity allocated_{0};

    std::optional<Duration> replenish_interval_;
    std::optional<double>   cost_per_unit_;

    // ResourceManager modifies allocated_
    void allocate(ResourceQuantity qty);
    void deallocate(ResourceQuantity qty);

    friend class ResourceManager;
};

} // namespace agentguard
