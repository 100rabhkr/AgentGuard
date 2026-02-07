#include "bind_forward.hpp"
#include <agentguard/agentguard.hpp>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>

using namespace agentguard;

void bind_ai(py::module_& m) {
    auto ai_mod = m.def_submodule("ai", "AI-specific resource types");

    // ---- TokenBudget --------------------------------------------------------
    py::class_<ai::TokenBudget>(ai_mod, "TokenBudget")
        .def(py::init<ResourceTypeId, std::string, ResourceQuantity, Duration>(),
             py::arg("id"),
             py::arg("name"),
             py::arg("total_tokens_per_window"),
             py::arg("window_duration"))
        .def("as_resource", &ai::TokenBudget::as_resource)
        .def("total_tokens_per_window", &ai::TokenBudget::total_tokens_per_window)
        .def("window_duration", &ai::TokenBudget::window_duration)
        .def("tokens_per_second_rate", &ai::TokenBudget::tokens_per_second_rate)
        .def("set_input_output_ratio", &ai::TokenBudget::set_input_output_ratio,
             py::arg("input_fraction"))
        .def("input_output_ratio", &ai::TokenBudget::input_output_ratio)
        .def("id", &ai::TokenBudget::id)
        .def("name", &ai::TokenBudget::name);

    // ---- RateLimiter --------------------------------------------------------
    py::class_<ai::RateLimiter> rl(ai_mod, "RateLimiter");

    py::enum_<ai::RateLimiter::WindowType>(rl, "WindowType")
        .value("PerSecond", ai::RateLimiter::WindowType::PerSecond)
        .value("PerMinute", ai::RateLimiter::WindowType::PerMinute)
        .value("PerHour",   ai::RateLimiter::WindowType::PerHour)
        .value("PerDay",    ai::RateLimiter::WindowType::PerDay)
        .export_values();

    rl.def(py::init<ResourceTypeId, std::string, ResourceQuantity,
                     ai::RateLimiter::WindowType>(),
           py::arg("id"),
           py::arg("api_name"),
           py::arg("requests_per_window"),
           py::arg("window"))
      .def("as_resource", &ai::RateLimiter::as_resource)
      .def("window_type", &ai::RateLimiter::window_type)
      .def("requests_per_window", &ai::RateLimiter::requests_per_window)
      .def("set_burst_allowance", &ai::RateLimiter::set_burst_allowance,
           py::arg("burst_extra"))
      .def("burst_allowance", &ai::RateLimiter::burst_allowance)
      .def("add_endpoint_sublimit", &ai::RateLimiter::add_endpoint_sublimit,
           py::arg("endpoint"),
           py::arg("limit"))
      .def("endpoint_sublimits", &ai::RateLimiter::endpoint_sublimits)
      .def("id", &ai::RateLimiter::id)
      .def("api_name", &ai::RateLimiter::api_name);

    // ---- ToolSlot -----------------------------------------------------------
    py::class_<ai::ToolSlot> ts(ai_mod, "ToolSlot");

    py::enum_<ai::ToolSlot::AccessMode>(ts, "AccessMode")
        .value("Exclusive",  ai::ToolSlot::AccessMode::Exclusive)
        .value("SharedRead", ai::ToolSlot::AccessMode::SharedRead)
        .value("Concurrent", ai::ToolSlot::AccessMode::Concurrent)
        .export_values();

    ts.def(py::init<ResourceTypeId, std::string, ai::ToolSlot::AccessMode,
                     ResourceQuantity>(),
           py::arg("id"),
           py::arg("tool_name"),
           py::arg("mode"),
           py::arg("max_concurrent_users") = 1)
      .def("as_resource", &ai::ToolSlot::as_resource)
      .def("access_mode", &ai::ToolSlot::access_mode)
      .def("tool_name", &ai::ToolSlot::tool_name)
      .def("max_concurrent_users", &ai::ToolSlot::max_concurrent_users)
      .def("set_estimated_usage_duration", &ai::ToolSlot::set_estimated_usage_duration,
           py::arg("d"))
      .def("estimated_usage_duration", &ai::ToolSlot::estimated_usage_duration)
      .def("set_fallback_tool", &ai::ToolSlot::set_fallback_tool,
           py::arg("fallback"))
      .def("fallback_tool", &ai::ToolSlot::fallback_tool)
      .def("id", &ai::ToolSlot::id);

    // ---- MemoryPool ---------------------------------------------------------
    py::class_<ai::MemoryPool> mp(ai_mod, "MemoryPool");

    py::enum_<ai::MemoryPool::MemoryUnit>(mp, "MemoryUnit")
        .value("Bytes",     ai::MemoryPool::MemoryUnit::Bytes)
        .value("Kilobytes", ai::MemoryPool::MemoryUnit::Kilobytes)
        .value("Megabytes", ai::MemoryPool::MemoryUnit::Megabytes)
        .value("Tokens",    ai::MemoryPool::MemoryUnit::Tokens)
        .value("Entries",   ai::MemoryPool::MemoryUnit::Entries)
        .export_values();

    mp.def(py::init<ResourceTypeId, std::string, ResourceQuantity,
                     ai::MemoryPool::MemoryUnit>(),
           py::arg("id"),
           py::arg("name"),
           py::arg("total_capacity"),
           py::arg("unit"))
      .def("as_resource", &ai::MemoryPool::as_resource)
      .def("unit", &ai::MemoryPool::unit)
      .def("unit_name", &ai::MemoryPool::unit_name)
      .def("set_eviction_policy", &ai::MemoryPool::set_eviction_policy,
           py::arg("policy_name"))
      .def("eviction_policy", &ai::MemoryPool::eviction_policy)
      .def("set_fragmentation_threshold", &ai::MemoryPool::set_fragmentation_threshold,
           py::arg("threshold"))
      .def("fragmentation_threshold", &ai::MemoryPool::fragmentation_threshold)
      .def("id", &ai::MemoryPool::id)
      .def("name", &ai::MemoryPool::name);
}
