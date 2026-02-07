#include "bind_forward.hpp"
#include <agentguard/agentguard.hpp>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>

using namespace agentguard;

void bind_subsystems(py::module_& m) {
    // SafetyChecker - stateless, all methods are const
    py::class_<SafetyChecker>(m, "SafetyChecker")
        .def(py::init<>())
        .def("check_safety", &SafetyChecker::check_safety,
             py::arg("input"))
        .def("check_hypothetical", &SafetyChecker::check_hypothetical,
             py::arg("current_state"),
             py::arg("requesting_agent"),
             py::arg("resource_type"),
             py::arg("quantity"))
        .def("check_hypothetical_batch", &SafetyChecker::check_hypothetical_batch,
             py::arg("current_state"),
             py::arg("requests"))
        .def("find_grantable_requests", &SafetyChecker::find_grantable_requests,
             py::arg("current_state"),
             py::arg("candidates"))
        .def("identify_bottleneck_agents", &SafetyChecker::identify_bottleneck_agents,
             py::arg("input"))
        .def("check_safety_probabilistic", &SafetyChecker::check_safety_probabilistic,
             py::arg("input"),
             py::arg("confidence_level"))
        .def("check_hypothetical_probabilistic", &SafetyChecker::check_hypothetical_probabilistic,
             py::arg("current_state"),
             py::arg("agent"),
             py::arg("resource"),
             py::arg("quantity"),
             py::arg("confidence_level"));

    // DemandEstimator
    py::class_<DemandEstimator>(m, "DemandEstimator")
        .def(py::init<AdaptiveConfig>(),
             py::arg("config") = AdaptiveConfig{})
        .def("record_request", &DemandEstimator::record_request,
             py::arg("agent"),
             py::arg("resource"),
             py::arg("quantity"))
        .def("record_allocation_level", &DemandEstimator::record_allocation_level,
             py::arg("agent"),
             py::arg("resource"),
             py::arg("current_total_allocation"))
        .def("clear_agent", &DemandEstimator::clear_agent,
             py::arg("agent"))
        .def("estimate_max_need", &DemandEstimator::estimate_max_need,
             py::arg("agent"),
             py::arg("resource"),
             py::arg("confidence_level"))
        .def("estimate_all_max_needs", &DemandEstimator::estimate_all_max_needs,
             py::arg("confidence_level"))
        .def("set_agent_demand_mode", &DemandEstimator::set_agent_demand_mode,
             py::arg("agent"),
             py::arg("mode"))
        .def("get_agent_demand_mode", &DemandEstimator::get_agent_demand_mode,
             py::arg("agent"))
        .def("get_stats", &DemandEstimator::get_stats,
             py::arg("agent"),
             py::arg("resource"))
        .def("config", &DemandEstimator::config,
             py::return_value_policy::reference_internal);
}
