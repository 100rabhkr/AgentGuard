#include "bind_forward.hpp"
#include <agentguard/agentguard.hpp>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>

using namespace agentguard;

// ---------------------------------------------------------------------------
// Wrapper for std::future<RequestStatus>
// ---------------------------------------------------------------------------
struct FutureRequestStatus {
    std::future<RequestStatus> fut;

    RequestStatus result() {
        py::gil_scoped_release release;
        return fut.get();
    }

    bool ready() const {
        return fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
};

// ---------------------------------------------------------------------------
// bind_core  --  Resource, Agent, FutureRequestStatus, ResourceManager
// ---------------------------------------------------------------------------
void bind_core(py::module_& m) {

    // ===================================================================
    // Resource
    // ===================================================================
    py::class_<Resource>(m, "Resource")
        .def(py::init<ResourceTypeId, std::string, ResourceCategory, ResourceQuantity>(),
             py::arg("id"), py::arg("name"),
             py::arg("category"), py::arg("total_capacity"))
        // Getters
        .def("id",             &Resource::id)
        .def("name",           &Resource::name)
        .def("category",       &Resource::category)
        .def("total_capacity", &Resource::total_capacity)
        .def("allocated",      &Resource::allocated)
        .def("available",      &Resource::available)
        // Setters / metadata
        .def("set_total_capacity",    &Resource::set_total_capacity,
             py::arg("new_capacity"))
        .def("set_replenish_interval",&Resource::set_replenish_interval,
             py::arg("interval"))
        .def("replenish_interval",    &Resource::replenish_interval)
        .def("set_cost_per_unit",     &Resource::set_cost_per_unit,
             py::arg("cost"))
        .def("cost_per_unit",         &Resource::cost_per_unit)
        // __repr__
        .def("__repr__", [](const Resource& r) {
            return "<Resource id=" + std::to_string(r.id())
                 + " name='" + r.name()
                 + "' capacity=" + std::to_string(r.total_capacity()) + ">";
        });

    // ===================================================================
    // Agent
    // ===================================================================
    py::class_<Agent>(m, "Agent")
        .def(py::init<AgentId, std::string, Priority>(),
             py::arg("id"), py::arg("name"),
             py::arg("priority") = PRIORITY_NORMAL)
        // Getters
        .def("id",       &Agent::id)
        .def("name",     &Agent::name)
        .def("priority", &Agent::priority)
        .def("state",    &Agent::state)
        // Setters
        .def("set_priority", &Agent::set_priority, py::arg("p"))
        // Max needs / allocation
        .def("declare_max_need", &Agent::declare_max_need,
             py::arg("resource_type"), py::arg("max_qty"))
        .def("max_needs",           &Agent::max_needs)
        .def("current_allocation",  &Agent::current_allocation)
        .def("remaining_need",      &Agent::remaining_need,
             py::arg("resource_type"))
        // AI-specific metadata
        .def("set_model_identifier", &Agent::set_model_identifier,
             py::arg("model_id"))
        .def("model_identifier",     &Agent::model_identifier)
        .def("set_task_description", &Agent::set_task_description,
             py::arg("desc"))
        .def("task_description",     &Agent::task_description)
        // __repr__
        .def("__repr__", [](const Agent& a) {
            return "<Agent id=" + std::to_string(a.id())
                 + " name='" + a.name()
                 + "' state=" + std::string(to_string(a.state())) + ">";
        });

    // ===================================================================
    // FutureRequestStatus
    // ===================================================================
    py::class_<FutureRequestStatus>(m, "FutureRequestStatus")
        .def("result", &FutureRequestStatus::result,
             "Block until the result is available (releases the GIL while waiting).")
        .def("ready",  &FutureRequestStatus::ready,
             "Return True if the result is available without blocking.");

    // ===================================================================
    // ResourceManager
    // ===================================================================
    py::class_<ResourceManager>(m, "ResourceManager")
        .def(py::init<Config>(), py::arg("config") = Config{})

        // ------------- Resource Registration -------------
        .def("register_resource",        &ResourceManager::register_resource,
             py::arg("resource"))
        .def("unregister_resource",      &ResourceManager::unregister_resource,
             py::arg("id"))
        .def("adjust_resource_capacity", &ResourceManager::adjust_resource_capacity,
             py::arg("id"), py::arg("new_capacity"))
        .def("get_resource",             &ResourceManager::get_resource,
             py::arg("id"))
        .def("get_all_resources",        &ResourceManager::get_all_resources)

        // ------------- Agent Lifecycle -------------
        .def("register_agent",           &ResourceManager::register_agent,
             py::arg("agent"))
        .def("deregister_agent",         &ResourceManager::deregister_agent,
             py::arg("id"))
        .def("update_agent_max_claim",   &ResourceManager::update_agent_max_claim,
             py::arg("id"), py::arg("resource_type"), py::arg("new_max"))
        .def("get_agent",               &ResourceManager::get_agent,
             py::arg("id"))
        .def("get_all_agents",          &ResourceManager::get_all_agents)
        .def("agent_count",             &ResourceManager::agent_count)

        // ------------- Synchronous Resource Requests -------------
        .def("request_resources", &ResourceManager::request_resources,
             py::arg("agent_id"), py::arg("resource_type"),
             py::arg("quantity"), py::arg("timeout") = std::nullopt,
             py::call_guard<py::gil_scoped_release>())
        .def("request_resources_batch", &ResourceManager::request_resources_batch,
             py::arg("agent_id"), py::arg("requests"),
             py::arg("timeout") = std::nullopt,
             py::call_guard<py::gil_scoped_release>())

        // ------------- Asynchronous Resource Requests -------------
        .def("request_resources_async",
             [](ResourceManager& self, AgentId aid, ResourceTypeId rt,
                ResourceQuantity qty, std::optional<Duration> timeout) {
                 return FutureRequestStatus{
                     self.request_resources_async(aid, rt, qty, timeout)};
             },
             py::arg("agent_id"), py::arg("resource_type"),
             py::arg("quantity"), py::arg("timeout") = std::nullopt)

        // ------------- Callback Resource Requests -------------
        .def("request_resources_callback",
             [](ResourceManager& self, AgentId agent_id, ResourceTypeId rt,
                ResourceQuantity qty, py::function callback,
                std::optional<Duration> timeout) -> RequestId {
                 RequestCallback cpp_cb = [cb = py::object(callback)](
                     RequestId id, RequestStatus status) {
                     py::gil_scoped_acquire acquire;
                     cb(id, status);
                 };
                 return self.request_resources_callback(
                     agent_id, rt, qty, std::move(cpp_cb), timeout);
             },
             py::arg("agent_id"), py::arg("resource_type"),
             py::arg("quantity"), py::arg("callback"),
             py::arg("timeout") = std::nullopt)

        // ------------- Resource Release -------------
        .def("release_resources", &ResourceManager::release_resources,
             py::arg("agent_id"), py::arg("resource_type"), py::arg("quantity"))
        .def("release_all_resources",
             py::overload_cast<AgentId, ResourceTypeId>(
                 &ResourceManager::release_all_resources),
             py::arg("agent_id"), py::arg("resource_type"))
        .def("release_all_resources",
             py::overload_cast<AgentId>(
                 &ResourceManager::release_all_resources),
             py::arg("agent_id"))

        // ------------- Queries -------------
        .def("is_safe",               &ResourceManager::is_safe)
        .def("get_snapshot",          &ResourceManager::get_snapshot)
        .def("pending_request_count", &ResourceManager::pending_request_count)

        // ------------- Progress Monitoring -------------
        .def("report_progress", &ResourceManager::report_progress,
             py::arg("id"), py::arg("metric"), py::arg("value"))
        .def("set_agent_stall_threshold", &ResourceManager::set_agent_stall_threshold,
             py::arg("id"), py::arg("threshold"))
        .def("is_agent_stalled", &ResourceManager::is_agent_stalled,
             py::arg("id"))
        .def("get_stalled_agents", &ResourceManager::get_stalled_agents)

        // ------------- Delegation Tracking -------------
        .def("report_delegation", &ResourceManager::report_delegation,
             py::arg("from_agent"), py::arg("to_agent"),
             py::arg("task_desc") = "")
        .def("complete_delegation", &ResourceManager::complete_delegation,
             py::arg("from_agent"), py::arg("to_agent"))
        .def("cancel_delegation", &ResourceManager::cancel_delegation,
             py::arg("from_agent"), py::arg("to_agent"))
        .def("get_all_delegations",   &ResourceManager::get_all_delegations)
        .def("find_delegation_cycle",  &ResourceManager::find_delegation_cycle)

        // ------------- Adaptive Demand -------------
        .def("set_agent_demand_mode", &ResourceManager::set_agent_demand_mode,
             py::arg("id"), py::arg("mode"))
        .def("check_safety_probabilistic",
             py::overload_cast<double>(
                 &ResourceManager::check_safety_probabilistic, py::const_),
             py::arg("confidence"))
        .def("check_safety_probabilistic",
             py::overload_cast<>(
                 &ResourceManager::check_safety_probabilistic, py::const_))
        .def("request_resources_adaptive",
             &ResourceManager::request_resources_adaptive,
             py::arg("agent_id"), py::arg("resource_type"),
             py::arg("quantity"), py::arg("timeout") = std::nullopt,
             py::call_guard<py::gil_scoped_release>())

        // ------------- Configuration / Lifecycle -------------
        .def("set_monitor", &ResourceManager::set_monitor,
             py::arg("monitor"))
        .def("set_scheduling_policy",
             [](ResourceManager& self, std::shared_ptr<SchedulingPolicy> policy) {
                 // Bridge shared_ptr (pybind11 holder) to unique_ptr (C++ API)
                 struct PolicyBridge : SchedulingPolicy {
                     std::shared_ptr<SchedulingPolicy> inner;
                     PolicyBridge(std::shared_ptr<SchedulingPolicy> p) : inner(std::move(p)) {}
                     std::vector<ResourceRequest> prioritize(
                         const std::vector<ResourceRequest>& pending,
                         const SystemSnapshot& state) const override {
                         return inner->prioritize(pending, state);
                     }
                     std::string name() const override { return inner->name(); }
                 };
                 self.set_scheduling_policy(
                     std::make_unique<PolicyBridge>(std::move(policy)));
             },
             py::arg("policy"))
        .def("start",      &ResourceManager::start)
        .def("stop",       &ResourceManager::stop)
        .def("is_running", &ResourceManager::is_running);
}
