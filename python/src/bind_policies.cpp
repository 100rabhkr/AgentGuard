#include "bind_forward.hpp"
#include <agentguard/agentguard.hpp>
#include <pybind11/stl.h>

using namespace agentguard;

// Trampoline class to allow Python subclassing of SchedulingPolicy
class PySchedulingPolicy : public SchedulingPolicy {
public:
    using SchedulingPolicy::SchedulingPolicy;

    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending,
        const SystemSnapshot& state) const override {
        py::gil_scoped_acquire acquire;
        PYBIND11_OVERRIDE_PURE(
            std::vector<ResourceRequest>, SchedulingPolicy, prioritize, pending, state);
    }

    std::string name() const override {
        py::gil_scoped_acquire acquire;
        PYBIND11_OVERRIDE_PURE(std::string, SchedulingPolicy, name);
    }
};

void bind_policies(py::module_& m) {
    // --- Abstract SchedulingPolicy with trampoline ---
    py::class_<SchedulingPolicy, PySchedulingPolicy, std::shared_ptr<SchedulingPolicy>>(
            m, "SchedulingPolicy")
        .def(py::init<>())
        .def("prioritize", &SchedulingPolicy::prioritize)
        .def("name", &SchedulingPolicy::name);

    // --- Concrete policies ---

    py::class_<FifoPolicy, SchedulingPolicy, std::shared_ptr<FifoPolicy>>(m, "FifoPolicy")
        .def(py::init<>())
        .def("prioritize", &FifoPolicy::prioritize)
        .def("name", &FifoPolicy::name);

    py::class_<PriorityPolicy, SchedulingPolicy, std::shared_ptr<PriorityPolicy>>(
            m, "PriorityPolicy")
        .def(py::init<>())
        .def("prioritize", &PriorityPolicy::prioritize)
        .def("name", &PriorityPolicy::name);

    py::class_<ShortestNeedPolicy, SchedulingPolicy, std::shared_ptr<ShortestNeedPolicy>>(
            m, "ShortestNeedPolicy")
        .def(py::init<>())
        .def("prioritize", &ShortestNeedPolicy::prioritize)
        .def("name", &ShortestNeedPolicy::name);

    py::class_<DeadlinePolicy, SchedulingPolicy, std::shared_ptr<DeadlinePolicy>>(
            m, "DeadlinePolicy")
        .def(py::init<>())
        .def("prioritize", &DeadlinePolicy::prioritize)
        .def("name", &DeadlinePolicy::name);

    py::class_<FairnessPolicy, SchedulingPolicy, std::shared_ptr<FairnessPolicy>>(
            m, "FairnessPolicy")
        .def(py::init<>())
        .def("prioritize", &FairnessPolicy::prioritize)
        .def("name", &FairnessPolicy::name);
}
