#pragma once

#include "agentguard/resource.hpp"

#include <optional>
#include <string>

namespace agentguard::ai {

// Represents exclusive or shared access to a tool (code interpreter, browser, etc.)
class ToolSlot {
public:
    enum class AccessMode {
        Exclusive,   // Only one agent at a time
        SharedRead,  // Multiple concurrent readers, exclusive writer
        Concurrent   // Multiple agents up to slot limit
    };

    ToolSlot(ResourceTypeId id,
             std::string tool_name,
             AccessMode mode,
             ResourceQuantity max_concurrent_users = 1);

    Resource as_resource() const;

    AccessMode access_mode() const noexcept;
    const std::string& tool_name() const noexcept;
    ResourceQuantity max_concurrent_users() const noexcept;

    void set_estimated_usage_duration(Duration d);
    std::optional<Duration> estimated_usage_duration() const noexcept;

    void set_fallback_tool(ResourceTypeId fallback);
    std::optional<ResourceTypeId> fallback_tool() const noexcept;

    ResourceTypeId id() const noexcept;

private:
    ResourceTypeId id_;
    std::string tool_name_;
    AccessMode access_mode_;
    ResourceQuantity max_concurrent_;
    std::optional<Duration> estimated_usage_duration_;
    std::optional<ResourceTypeId> fallback_tool_;
};

} // namespace agentguard::ai
