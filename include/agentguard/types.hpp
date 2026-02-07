#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentguard {

// Unique identifiers
using AgentId = std::uint64_t;
using ResourceTypeId = std::uint64_t;
using RequestId = std::uint64_t;

// Resource quantity (integer units)
using ResourceQuantity = std::int64_t;

// Time types
using Clock = std::chrono::steady_clock;
using Timestamp = Clock::time_point;
using Duration = Clock::duration;

// Agent priority (higher = more important)
using Priority = std::int32_t;

constexpr Priority PRIORITY_LOW      = 0;
constexpr Priority PRIORITY_NORMAL   = 50;
constexpr Priority PRIORITY_HIGH     = 100;
constexpr Priority PRIORITY_CRITICAL = 200;

// Request status
enum class RequestStatus {
    Pending,
    Granted,
    Denied,
    TimedOut,
    Cancelled
};

// Agent lifecycle state
enum class AgentState {
    Registered,
    Active,
    Waiting,
    Releasing,
    Deregistered
};

// Resource category (AI-specific taxonomy)
enum class ResourceCategory {
    ApiRateLimit,
    TokenBudget,
    ToolSlot,
    MemoryPool,
    DatabaseConn,
    GpuCompute,
    FileHandle,
    NetworkSocket,
    Custom
};

// Callback types
using RequestCallback = std::function<void(RequestId, RequestStatus)>;
using AgentEventCallback = std::function<void(AgentId, AgentState)>;

// Resource request descriptor
struct ResourceRequest {
    RequestId       id{0};
    AgentId         agent_id{0};
    ResourceTypeId  resource_type{0};
    ResourceQuantity quantity{0};
    Priority        priority{PRIORITY_NORMAL};
    std::optional<Duration> timeout;
    RequestCallback callback;
    Timestamp       submitted_at{};
};

// Snapshot of one agent's allocation
struct AgentAllocationSnapshot {
    AgentId agent_id{0};
    std::string name;
    Priority priority{PRIORITY_NORMAL};
    AgentState state{AgentState::Registered};
    std::unordered_map<ResourceTypeId, ResourceQuantity> allocation;
    std::unordered_map<ResourceTypeId, ResourceQuantity> max_claim;
};

// System-wide snapshot for monitoring
struct SystemSnapshot {
    Timestamp timestamp{};
    std::unordered_map<ResourceTypeId, ResourceQuantity> total_resources;
    std::unordered_map<ResourceTypeId, ResourceQuantity> available_resources;
    std::vector<AgentAllocationSnapshot> agents;
    std::size_t pending_requests{0};
    bool is_safe{true};
};

inline const char* to_string(RequestStatus s) {
    switch (s) {
        case RequestStatus::Pending:   return "Pending";
        case RequestStatus::Granted:   return "Granted";
        case RequestStatus::Denied:    return "Denied";
        case RequestStatus::TimedOut:  return "TimedOut";
        case RequestStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

inline const char* to_string(AgentState s) {
    switch (s) {
        case AgentState::Registered:   return "Registered";
        case AgentState::Active:       return "Active";
        case AgentState::Waiting:      return "Waiting";
        case AgentState::Releasing:    return "Releasing";
        case AgentState::Deregistered: return "Deregistered";
    }
    return "Unknown";
}

inline const char* to_string(ResourceCategory c) {
    switch (c) {
        case ResourceCategory::ApiRateLimit:  return "ApiRateLimit";
        case ResourceCategory::TokenBudget:   return "TokenBudget";
        case ResourceCategory::ToolSlot:      return "ToolSlot";
        case ResourceCategory::MemoryPool:    return "MemoryPool";
        case ResourceCategory::DatabaseConn:  return "DatabaseConn";
        case ResourceCategory::GpuCompute:    return "GpuCompute";
        case ResourceCategory::FileHandle:    return "FileHandle";
        case ResourceCategory::NetworkSocket: return "NetworkSocket";
        case ResourceCategory::Custom:        return "Custom";
    }
    return "Unknown";
}

// Delegation edge metadata
struct DelegationInfo {
    AgentId from{0};
    AgentId to{0};
    std::string task_description;
    Timestamp timestamp{};
};

// Demand estimation mode
enum class DemandMode {
    Static,    // Use explicit declare_max_need() only (backward compat)
    Adaptive,  // Compute from usage statistics only
    Hybrid     // Statistical estimate capped by explicit declaration
};

// Probabilistic safety result
struct ProbabilisticSafetyResult {
    bool is_safe{false};
    double confidence_level{0.0};
    double max_safe_confidence{0.0};
    std::vector<AgentId> safe_sequence;
    std::string reason;
    std::unordered_map<AgentId,
        std::unordered_map<ResourceTypeId, ResourceQuantity>> estimated_max_needs;
};

inline const char* to_string(DemandMode m) {
    switch (m) {
        case DemandMode::Static:   return "Static";
        case DemandMode::Adaptive: return "Adaptive";
        case DemandMode::Hybrid:   return "Hybrid";
    }
    return "Unknown";
}

} // namespace agentguard
