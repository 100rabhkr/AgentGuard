#include "agentguard/ai/memory_pool.hpp"

namespace agentguard::ai {

MemoryPool::MemoryPool(ResourceTypeId id,
                       std::string name,
                       ResourceQuantity total_capacity,
                       MemoryUnit unit)
    : id_(id)
    , name_(std::move(name))
    , total_capacity_(total_capacity)
    , unit_(unit)
{}

Resource MemoryPool::as_resource() const {
    return Resource(id_, name_, ResourceCategory::MemoryPool, total_capacity_);
}

MemoryPool::MemoryUnit MemoryPool::unit() const noexcept { return unit_; }

const std::string& MemoryPool::unit_name() const {
    static const std::string names[] = {"Bytes", "KB", "MB", "Tokens", "Entries"};
    return names[static_cast<int>(unit_)];
}

void MemoryPool::set_eviction_policy(std::string policy_name) {
    eviction_policy_ = std::move(policy_name);
}

const std::string& MemoryPool::eviction_policy() const noexcept {
    return eviction_policy_;
}

void MemoryPool::set_fragmentation_threshold(double threshold) {
    fragmentation_threshold_ = threshold;
}

double MemoryPool::fragmentation_threshold() const noexcept {
    return fragmentation_threshold_;
}

ResourceTypeId MemoryPool::id() const noexcept { return id_; }
const std::string& MemoryPool::name() const noexcept { return name_; }

} // namespace agentguard::ai
