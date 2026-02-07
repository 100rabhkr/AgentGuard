#include "bind_forward.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>

#include <agentguard/agentguard.hpp>

using namespace agentguard;

// ---------------------------------------------------------------------------
// Module entry point
// ---------------------------------------------------------------------------
PYBIND11_MODULE(_agentguard, m) {
    m.doc() = "AgentGuard: Deadlock prevention for multi-AI-agent systems";

    bind_enums_and_structs(m);
    bind_exceptions(m);
    bind_core(m);
    bind_monitors(m);
    bind_policies(m);
    bind_subsystems(m);
    bind_ai(m);
}

// ---------------------------------------------------------------------------
// Enums & structs
// ---------------------------------------------------------------------------
void bind_enums_and_structs(py::module_& m) {

    // ---- Enums ------------------------------------------------------------

    py::enum_<RequestStatus>(m, "RequestStatus")
        .value("Pending",   RequestStatus::Pending)
        .value("Granted",   RequestStatus::Granted)
        .value("Denied",    RequestStatus::Denied)
        .value("TimedOut",  RequestStatus::TimedOut)
        .value("Cancelled", RequestStatus::Cancelled)
        .export_values();

    py::enum_<AgentState>(m, "AgentState")
        .value("Registered",   AgentState::Registered)
        .value("Active",       AgentState::Active)
        .value("Waiting",      AgentState::Waiting)
        .value("Releasing",    AgentState::Releasing)
        .value("Deregistered", AgentState::Deregistered)
        .export_values();

    py::enum_<ResourceCategory>(m, "ResourceCategory")
        .value("ApiRateLimit",  ResourceCategory::ApiRateLimit)
        .value("TokenBudget",   ResourceCategory::TokenBudget)
        .value("ToolSlot",      ResourceCategory::ToolSlot)
        .value("MemoryPool",    ResourceCategory::MemoryPool)
        .value("DatabaseConn",  ResourceCategory::DatabaseConn)
        .value("GpuCompute",    ResourceCategory::GpuCompute)
        .value("FileHandle",    ResourceCategory::FileHandle)
        .value("NetworkSocket", ResourceCategory::NetworkSocket)
        .value("Custom",        ResourceCategory::Custom)
        .export_values();

    py::enum_<DemandMode>(m, "DemandMode")
        .value("Static",   DemandMode::Static)
        .value("Adaptive", DemandMode::Adaptive)
        .value("Hybrid",   DemandMode::Hybrid)
        .export_values();

    py::enum_<DelegationCycleAction>(m, "DelegationCycleAction")
        .value("NotifyOnly",       DelegationCycleAction::NotifyOnly)
        .value("RejectDelegation", DelegationCycleAction::RejectDelegation)
        .value("CancelLatest",     DelegationCycleAction::CancelLatest)
        .export_values();

    py::enum_<EventType>(m, "EventType")
        .value("AgentRegistered",           EventType::AgentRegistered)
        .value("AgentDeregistered",         EventType::AgentDeregistered)
        .value("ResourceRegistered",        EventType::ResourceRegistered)
        .value("ResourceCapacityChanged",   EventType::ResourceCapacityChanged)
        .value("RequestSubmitted",          EventType::RequestSubmitted)
        .value("RequestGranted",            EventType::RequestGranted)
        .value("RequestDenied",             EventType::RequestDenied)
        .value("RequestTimedOut",           EventType::RequestTimedOut)
        .value("RequestCancelled",          EventType::RequestCancelled)
        .value("ResourcesReleased",         EventType::ResourcesReleased)
        .value("SafetyCheckPerformed",      EventType::SafetyCheckPerformed)
        .value("UnsafeStateDetected",       EventType::UnsafeStateDetected)
        .value("QueueSizeChanged",          EventType::QueueSizeChanged)
        .value("AgentProgressReported",     EventType::AgentProgressReported)
        .value("AgentStalled",              EventType::AgentStalled)
        .value("AgentStallResolved",        EventType::AgentStallResolved)
        .value("AgentResourcesAutoReleased", EventType::AgentResourcesAutoReleased)
        .value("DelegationReported",        EventType::DelegationReported)
        .value("DelegationCompleted",       EventType::DelegationCompleted)
        .value("DelegationCancelled",       EventType::DelegationCancelled)
        .value("DelegationCycleDetected",   EventType::DelegationCycleDetected)
        .value("DemandEstimateUpdated",     EventType::DemandEstimateUpdated)
        .value("ProbabilisticSafetyCheck",  EventType::ProbabilisticSafetyCheck)
        .value("AdaptiveDemandModeChanged", EventType::AdaptiveDemandModeChanged)
        .export_values();

    py::enum_<ConsoleMonitor::Verbosity>(m, "Verbosity")
        .value("Quiet",   ConsoleMonitor::Verbosity::Quiet)
        .value("Normal",  ConsoleMonitor::Verbosity::Normal)
        .value("Verbose", ConsoleMonitor::Verbosity::Verbose)
        .value("Debug",   ConsoleMonitor::Verbosity::Debug)
        .export_values();

    // ---- Structs ----------------------------------------------------------

    // ProgressConfig
    py::class_<ProgressConfig>(m, "ProgressConfig")
        .def(py::init<>())
        .def_readwrite("enabled",                  &ProgressConfig::enabled)
        .def_readwrite("default_stall_threshold",  &ProgressConfig::default_stall_threshold)
        .def_readwrite("check_interval",           &ProgressConfig::check_interval)
        .def_readwrite("auto_release_on_stall",    &ProgressConfig::auto_release_on_stall);

    // DelegationConfig
    py::class_<DelegationConfig>(m, "DelegationConfig")
        .def(py::init<>())
        .def_readwrite("enabled",      &DelegationConfig::enabled)
        .def_readwrite("cycle_action", &DelegationConfig::cycle_action);

    // AdaptiveConfig
    py::class_<AdaptiveConfig>(m, "AdaptiveConfig")
        .def(py::init<>())
        .def_readwrite("enabled",                    &AdaptiveConfig::enabled)
        .def_readwrite("default_confidence_level",   &AdaptiveConfig::default_confidence_level)
        .def_readwrite("history_window_size",        &AdaptiveConfig::history_window_size)
        .def_readwrite("cold_start_headroom_factor", &AdaptiveConfig::cold_start_headroom_factor)
        .def_readwrite("cold_start_default_demand",  &AdaptiveConfig::cold_start_default_demand)
        .def_readwrite("adaptive_headroom_factor",   &AdaptiveConfig::adaptive_headroom_factor)
        .def_readwrite("default_demand_mode",        &AdaptiveConfig::default_demand_mode);

    // Config (top-level, embeds the three sub-configs)
    py::class_<Config>(m, "Config")
        .def(py::init<>())
        .def_readwrite("max_agents",                &Config::max_agents)
        .def_readwrite("max_resource_types",        &Config::max_resource_types)
        .def_readwrite("max_queue_size",            &Config::max_queue_size)
        .def_readwrite("default_request_timeout",   &Config::default_request_timeout)
        .def_readwrite("processor_poll_interval",   &Config::processor_poll_interval)
        .def_readwrite("snapshot_interval",         &Config::snapshot_interval)
        .def_readwrite("enable_timeout_expiration", &Config::enable_timeout_expiration)
        .def_readwrite("starvation_threshold",      &Config::starvation_threshold)
        .def_readwrite("thread_safe",               &Config::thread_safe)
        .def_readwrite("progress",                  &Config::progress)
        .def_readwrite("delegation",                &Config::delegation)
        .def_readwrite("adaptive",                  &Config::adaptive);

    // SafetyCheckInput
    py::class_<SafetyCheckInput>(m, "SafetyCheckInput")
        .def(py::init<>())
        .def_readwrite("total",      &SafetyCheckInput::total)
        .def_readwrite("available",  &SafetyCheckInput::available)
        .def_readwrite("allocation", &SafetyCheckInput::allocation)
        .def_readwrite("max_need",   &SafetyCheckInput::max_need);

    // SafetyCheckResult
    py::class_<SafetyCheckResult>(m, "SafetyCheckResult")
        .def(py::init<>())
        .def_readwrite("is_safe",       &SafetyCheckResult::is_safe)
        .def_readwrite("safe_sequence", &SafetyCheckResult::safe_sequence)
        .def_readwrite("reason",        &SafetyCheckResult::reason);

    // MonitorEvent
    py::class_<MonitorEvent>(m, "MonitorEvent")
        .def(py::init<>())
        .def_readwrite("type",            &MonitorEvent::type)
        .def_readwrite("timestamp",       &MonitorEvent::timestamp)
        .def_readwrite("message",         &MonitorEvent::message)
        .def_readwrite("agent_id",        &MonitorEvent::agent_id)
        .def_readwrite("resource_type",   &MonitorEvent::resource_type)
        .def_readwrite("request_id",      &MonitorEvent::request_id)
        .def_readwrite("quantity",        &MonitorEvent::quantity)
        .def_readwrite("safety_result",   &MonitorEvent::safety_result)
        .def_readwrite("target_agent_id", &MonitorEvent::target_agent_id)
        .def_readwrite("cycle_path",      &MonitorEvent::cycle_path);

    // SystemSnapshot
    py::class_<SystemSnapshot>(m, "SystemSnapshot")
        .def(py::init<>())
        .def_readwrite("timestamp",           &SystemSnapshot::timestamp)
        .def_readwrite("total_resources",     &SystemSnapshot::total_resources)
        .def_readwrite("available_resources", &SystemSnapshot::available_resources)
        .def_readwrite("agents",              &SystemSnapshot::agents)
        .def_readwrite("pending_requests",    &SystemSnapshot::pending_requests)
        .def_readwrite("is_safe",             &SystemSnapshot::is_safe);

    // AgentAllocationSnapshot
    py::class_<AgentAllocationSnapshot>(m, "AgentAllocationSnapshot")
        .def(py::init<>())
        .def_readwrite("agent_id",   &AgentAllocationSnapshot::agent_id)
        .def_readwrite("name",       &AgentAllocationSnapshot::name)
        .def_readwrite("priority",   &AgentAllocationSnapshot::priority)
        .def_readwrite("state",      &AgentAllocationSnapshot::state)
        .def_readwrite("allocation", &AgentAllocationSnapshot::allocation)
        .def_readwrite("max_claim",  &AgentAllocationSnapshot::max_claim);

    // DelegationInfo
    py::class_<DelegationInfo>(m, "DelegationInfo")
        .def(py::init<>())
        .def_readwrite("from_agent",       &DelegationInfo::from)
        .def_readwrite("to_agent",         &DelegationInfo::to)
        .def_readwrite("task_description", &DelegationInfo::task_description)
        .def_readwrite("timestamp",        &DelegationInfo::timestamp);

    // DelegationResult
    py::class_<DelegationResult>(m, "DelegationResult")
        .def(py::init<>())
        .def_readwrite("accepted",       &DelegationResult::accepted)
        .def_readwrite("cycle_detected", &DelegationResult::cycle_detected)
        .def_readwrite("cycle_path",     &DelegationResult::cycle_path);

    // ProbabilisticSafetyResult
    py::class_<ProbabilisticSafetyResult>(m, "ProbabilisticSafetyResult")
        .def(py::init<>())
        .def_readwrite("is_safe",              &ProbabilisticSafetyResult::is_safe)
        .def_readwrite("confidence_level",     &ProbabilisticSafetyResult::confidence_level)
        .def_readwrite("max_safe_confidence",  &ProbabilisticSafetyResult::max_safe_confidence)
        .def_readwrite("safe_sequence",        &ProbabilisticSafetyResult::safe_sequence)
        .def_readwrite("reason",               &ProbabilisticSafetyResult::reason)
        .def_readwrite("estimated_max_needs",  &ProbabilisticSafetyResult::estimated_max_needs);

    // UsageStats
    py::class_<UsageStats>(m, "UsageStats")
        .def(py::init<>())
        .def_readwrite("count",              &UsageStats::count)
        .def_readwrite("sum",                &UsageStats::sum)
        .def_readwrite("sum_sq",             &UsageStats::sum_sq)
        .def_readwrite("max_single_request", &UsageStats::max_single_request)
        .def_readwrite("max_cumulative",     &UsageStats::max_cumulative)
        .def_readwrite("window",             &UsageStats::window)
        .def_readwrite("window_head",        &UsageStats::window_head)
        .def_readwrite("window_count",       &UsageStats::window_count)
        .def("mean",     &UsageStats::mean)
        .def("variance", &UsageStats::variance)
        .def("stddev",   &UsageStats::stddev);

    // ProgressRecord
    py::class_<ProgressRecord>(m, "ProgressRecord")
        .def(py::init<>())
        .def_readwrite("metrics",         &ProgressRecord::metrics)
        .def_readwrite("last_update",     &ProgressRecord::last_update)
        .def_readwrite("stall_threshold", &ProgressRecord::stall_threshold)
        .def_readwrite("is_stalled",      &ProgressRecord::is_stalled);

    // ResourceRequest
    py::class_<ResourceRequest>(m, "ResourceRequest")
        .def(py::init<>())
        .def_readwrite("id",            &ResourceRequest::id)
        .def_readwrite("agent_id",      &ResourceRequest::agent_id)
        .def_readwrite("resource_type", &ResourceRequest::resource_type)
        .def_readwrite("quantity",      &ResourceRequest::quantity)
        .def_readwrite("priority",      &ResourceRequest::priority)
        .def_readwrite("timeout",       &ResourceRequest::timeout)
        .def_readwrite("callback",      &ResourceRequest::callback)
        .def_readwrite("submitted_at",  &ResourceRequest::submitted_at);

    // MetricsMonitor::Metrics (bound as module-level "Metrics")
    py::class_<MetricsMonitor::Metrics>(m, "Metrics")
        .def(py::init<>())
        .def_readwrite("total_requests",              &MetricsMonitor::Metrics::total_requests)
        .def_readwrite("granted_requests",            &MetricsMonitor::Metrics::granted_requests)
        .def_readwrite("denied_requests",             &MetricsMonitor::Metrics::denied_requests)
        .def_readwrite("timed_out_requests",          &MetricsMonitor::Metrics::timed_out_requests)
        .def_readwrite("average_wait_time_ms",        &MetricsMonitor::Metrics::average_wait_time_ms)
        .def_readwrite("safety_check_avg_duration_us", &MetricsMonitor::Metrics::safety_check_avg_duration_us)
        .def_readwrite("unsafe_state_detections",     &MetricsMonitor::Metrics::unsafe_state_detections)
        .def_readwrite("resource_utilization_percent", &MetricsMonitor::Metrics::resource_utilization_percent);

    // ---- Priority constants -----------------------------------------------

    m.attr("PRIORITY_LOW")      = PRIORITY_LOW;
    m.attr("PRIORITY_NORMAL")   = PRIORITY_NORMAL;
    m.attr("PRIORITY_HIGH")     = PRIORITY_HIGH;
    m.attr("PRIORITY_CRITICAL") = PRIORITY_CRITICAL;
}

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------
void bind_exceptions(py::module_& m) {
    // Base exception -> RuntimeError
    static auto py_AgentGuardError =
        py::register_exception<AgentGuardException>(m, "AgentGuardError", PyExc_RuntimeError);

    // Derived from AgentGuardError
    static auto py_AgentNotFoundError =
        py::register_exception<AgentNotFoundException>(m, "AgentNotFoundError", py_AgentGuardError.ptr());
    static auto py_ResourceNotFoundError =
        py::register_exception<ResourceNotFoundException>(m, "ResourceNotFoundError", py_AgentGuardError.ptr());

    static auto py_InvalidRequestError =
        py::register_exception<InvalidRequestException>(m, "InvalidRequestError", py_AgentGuardError.ptr());

    // Derived from InvalidRequestError
    static auto py_MaxClaimExceededError =
        py::register_exception<MaxClaimExceededException>(m, "MaxClaimExceededError", py_InvalidRequestError.ptr());
    static auto py_ResourceCapacityExceededError =
        py::register_exception<ResourceCapacityExceededException>(m, "ResourceCapacityExceededError", py_InvalidRequestError.ptr());

    // Derived from AgentGuardError
    static auto py_QueueFullError =
        py::register_exception<QueueFullException>(m, "QueueFullError", py_AgentGuardError.ptr());
    static auto py_AgentAlreadyRegisteredError =
        py::register_exception<AgentAlreadyRegisteredException>(m, "AgentAlreadyRegisteredError", py_AgentGuardError.ptr());
}
