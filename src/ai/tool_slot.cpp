#include "agentguard/ai/tool_slot.hpp"

namespace agentguard::ai {

ToolSlot::ToolSlot(ResourceTypeId id,
                   std::string tool_name,
                   AccessMode mode,
                   ResourceQuantity max_concurrent_users)
    : id_(id)
    , tool_name_(std::move(tool_name))
    , access_mode_(mode)
    , max_concurrent_(max_concurrent_users)
{
    if (mode == AccessMode::Exclusive) {
        max_concurrent_ = 1;
    }
}

Resource ToolSlot::as_resource() const {
    return Resource(id_, tool_name_, ResourceCategory::ToolSlot, max_concurrent_);
}

ToolSlot::AccessMode ToolSlot::access_mode() const noexcept { return access_mode_; }
const std::string& ToolSlot::tool_name() const noexcept { return tool_name_; }
ResourceQuantity ToolSlot::max_concurrent_users() const noexcept { return max_concurrent_; }

void ToolSlot::set_estimated_usage_duration(Duration d) {
    estimated_usage_duration_ = d;
}

std::optional<Duration> ToolSlot::estimated_usage_duration() const noexcept {
    return estimated_usage_duration_;
}

void ToolSlot::set_fallback_tool(ResourceTypeId fallback) {
    fallback_tool_ = fallback;
}

std::optional<ResourceTypeId> ToolSlot::fallback_tool() const noexcept {
    return fallback_tool_;
}

ResourceTypeId ToolSlot::id() const noexcept { return id_; }

} // namespace agentguard::ai
