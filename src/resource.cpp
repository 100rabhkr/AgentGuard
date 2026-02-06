#include "agentguard/resource.hpp"

#include <stdexcept>

namespace agentguard {

Resource::Resource(ResourceTypeId id,
                   std::string name,
                   ResourceCategory category,
                   ResourceQuantity total_capacity)
    : id_(id)
    , name_(std::move(name))
    , category_(category)
    , total_capacity_(total_capacity)
{
    if (total_capacity_ < 0) {
        throw std::invalid_argument("Resource total_capacity must be non-negative");
    }
}

ResourceTypeId Resource::id() const noexcept { return id_; }
const std::string& Resource::name() const noexcept { return name_; }
ResourceCategory Resource::category() const noexcept { return category_; }
ResourceQuantity Resource::total_capacity() const noexcept { return total_capacity_; }
ResourceQuantity Resource::allocated() const noexcept { return allocated_; }
ResourceQuantity Resource::available() const noexcept { return total_capacity_ - allocated_; }

bool Resource::set_total_capacity(ResourceQuantity new_capacity) {
    if (new_capacity < allocated_) {
        return false;
    }
    total_capacity_ = new_capacity;
    return true;
}

void Resource::set_replenish_interval(Duration interval) {
    replenish_interval_ = interval;
}

std::optional<Duration> Resource::replenish_interval() const noexcept {
    return replenish_interval_;
}

void Resource::set_cost_per_unit(double cost) {
    cost_per_unit_ = cost;
}

std::optional<double> Resource::cost_per_unit() const noexcept {
    return cost_per_unit_;
}

void Resource::allocate(ResourceQuantity qty) {
    allocated_ += qty;
}

void Resource::deallocate(ResourceQuantity qty) {
    allocated_ -= qty;
    if (allocated_ < 0) {
        allocated_ = 0;
    }
}

} // namespace agentguard
