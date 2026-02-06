#pragma once

#include "agentguard/resource.hpp"

#include <string>

namespace agentguard::ai {

// Represents shared memory resources (context windows, vector DB capacity, etc.)
class MemoryPool {
public:
    enum class MemoryUnit { Bytes, Kilobytes, Megabytes, Tokens, Entries };

    MemoryPool(ResourceTypeId id,
               std::string name,
               ResourceQuantity total_capacity,
               MemoryUnit unit);

    Resource as_resource() const;

    MemoryUnit unit() const noexcept;
    const std::string& unit_name() const;

    void set_eviction_policy(std::string policy_name);
    const std::string& eviction_policy() const noexcept;

    void set_fragmentation_threshold(double threshold);
    double fragmentation_threshold() const noexcept;

    ResourceTypeId id() const noexcept;
    const std::string& name() const noexcept;

private:
    ResourceTypeId id_;
    std::string name_;
    ResourceQuantity total_capacity_;
    MemoryUnit unit_;
    std::string eviction_policy_{"LRU"};
    double fragmentation_threshold_{0.3};
};

} // namespace agentguard::ai
