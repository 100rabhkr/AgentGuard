#include "bind_forward.hpp"
#include <agentguard/agentguard.hpp>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>

using namespace agentguard;

// Trampoline class to allow Python subclassing of Monitor
class PyMonitor : public Monitor {
public:
    using Monitor::Monitor;

    void on_event(const MonitorEvent& event) override {
        py::gil_scoped_acquire acquire;
        PYBIND11_OVERRIDE_PURE(void, Monitor, on_event, event);
    }

    void on_snapshot(const SystemSnapshot& snapshot) override {
        py::gil_scoped_acquire acquire;
        PYBIND11_OVERRIDE_PURE(void, Monitor, on_snapshot, snapshot);
    }
};

void bind_monitors(py::module_& m) {
    // --- Abstract Monitor with trampoline ---
    py::class_<Monitor, PyMonitor, std::shared_ptr<Monitor>>(m, "Monitor")
        .def(py::init<>())
        .def("on_event", &Monitor::on_event)
        .def("on_snapshot", &Monitor::on_snapshot);

    // --- ConsoleMonitor ---
    py::class_<ConsoleMonitor, Monitor, std::shared_ptr<ConsoleMonitor>>(m, "ConsoleMonitor")
        .def(py::init<ConsoleMonitor::Verbosity>(),
             py::arg("verbosity") = ConsoleMonitor::Verbosity::Normal);

    // ConsoleMonitor::Verbosity is bound in bindings.cpp as "Verbosity"

    // --- MetricsMonitor ---
    py::class_<MetricsMonitor, Monitor, std::shared_ptr<MetricsMonitor>>(m, "MetricsMonitor")
        .def(py::init<>())
        .def("get_metrics", &MetricsMonitor::get_metrics)
        .def("reset_metrics", &MetricsMonitor::reset_metrics)
        .def("set_utilization_alert_threshold",
            [](MetricsMonitor& self, double threshold, py::function cb) {
                MetricsMonitor::AlertCallback cpp_cb =
                    [cb = py::object(cb)](const std::string& msg) {
                        py::gil_scoped_acquire acquire;
                        cb(msg);
                    };
                self.set_utilization_alert_threshold(threshold, std::move(cpp_cb));
            },
            py::arg("threshold"), py::arg("callback"))
        .def("set_queue_size_alert_threshold",
            [](MetricsMonitor& self, std::size_t threshold, py::function cb) {
                MetricsMonitor::AlertCallback cpp_cb =
                    [cb = py::object(cb)](const std::string& msg) {
                        py::gil_scoped_acquire acquire;
                        cb(msg);
                    };
                self.set_queue_size_alert_threshold(threshold, std::move(cpp_cb));
            },
            py::arg("threshold"), py::arg("callback"));

    // MetricsMonitor::Metrics is bound in bindings.cpp as "Metrics"

    // --- CompositeMonitor ---
    py::class_<CompositeMonitor, Monitor, std::shared_ptr<CompositeMonitor>>(m, "CompositeMonitor")
        .def(py::init<>())
        .def("add_monitor", &CompositeMonitor::add_monitor);
}
