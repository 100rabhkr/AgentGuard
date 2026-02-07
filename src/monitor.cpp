#include "agentguard/monitor.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>

namespace agentguard {

namespace {

const char* to_string(EventType t) {
    switch (t) {
        case EventType::AgentRegistered:         return "AgentRegistered";
        case EventType::AgentDeregistered:        return "AgentDeregistered";
        case EventType::ResourceRegistered:       return "ResourceRegistered";
        case EventType::ResourceCapacityChanged:  return "ResourceCapacityChanged";
        case EventType::RequestSubmitted:         return "RequestSubmitted";
        case EventType::RequestGranted:           return "RequestGranted";
        case EventType::RequestDenied:            return "RequestDenied";
        case EventType::RequestTimedOut:          return "RequestTimedOut";
        case EventType::RequestCancelled:         return "RequestCancelled";
        case EventType::ResourcesReleased:        return "ResourcesReleased";
        case EventType::SafetyCheckPerformed:     return "SafetyCheckPerformed";
        case EventType::UnsafeStateDetected:      return "UnsafeStateDetected";
        case EventType::QueueSizeChanged:         return "QueueSizeChanged";
        case EventType::AgentProgressReported:    return "AgentProgressReported";
        case EventType::AgentStalled:             return "AgentStalled";
        case EventType::AgentStallResolved:       return "AgentStallResolved";
        case EventType::AgentResourcesAutoReleased: return "AgentResourcesAutoReleased";
        case EventType::DelegationReported:       return "DelegationReported";
        case EventType::DelegationCompleted:      return "DelegationCompleted";
        case EventType::DelegationCancelled:      return "DelegationCancelled";
        case EventType::DelegationCycleDetected:  return "DelegationCycleDetected";
        case EventType::DemandEstimateUpdated:    return "DemandEstimateUpdated";
        case EventType::ProbabilisticSafetyCheck: return "ProbabilisticSafetyCheck";
        case EventType::AdaptiveDemandModeChanged:return "AdaptiveDemandModeChanged";
    }
    return "Unknown";
}

bool is_important_event(EventType t) {
    switch (t) {
        case EventType::RequestGranted:
        case EventType::RequestDenied:
        case EventType::RequestTimedOut:
        case EventType::UnsafeStateDetected:
        case EventType::AgentRegistered:
        case EventType::AgentDeregistered:
        case EventType::AgentStalled:
        case EventType::AgentStallResolved:
        case EventType::AgentResourcesAutoReleased:
        case EventType::DelegationCycleDetected:
            return true;
        default:
            return false;
    }
}

} // anonymous namespace

// ========== ConsoleMonitor ==========

ConsoleMonitor::ConsoleMonitor(Verbosity v) : verbosity_(v) {}

void ConsoleMonitor::on_event(const MonitorEvent& event) {
    if (verbosity_ == Verbosity::Quiet) return;
    if (verbosity_ == Verbosity::Normal && !is_important_event(event.type)) return;

    std::lock_guard<std::mutex> lock(output_mutex_);

    std::cout << "[AgentGuard] " << to_string(event.type);

    if (event.agent_id.has_value()) {
        std::cout << " agent=" << event.agent_id.value();
    }
    if (event.resource_type.has_value()) {
        std::cout << " resource=" << event.resource_type.value();
    }
    if (event.request_id.has_value()) {
        std::cout << " request=" << event.request_id.value();
    }
    if (event.quantity.has_value()) {
        std::cout << " qty=" << event.quantity.value();
    }
    if (event.safety_result.has_value()) {
        std::cout << " safe=" << (event.safety_result.value() ? "true" : "false");
    }
    if (event.target_agent_id.has_value()) {
        std::cout << " target_agent=" << event.target_agent_id.value();
    }

    if (!event.message.empty()) {
        std::cout << " | " << event.message;
    }

    std::cout << "\n";
}

void ConsoleMonitor::on_snapshot(const SystemSnapshot& snapshot) {
    if (verbosity_ < Verbosity::Verbose) return;

    std::lock_guard<std::mutex> lock(output_mutex_);

    std::cout << "\n[AgentGuard] === System Snapshot ===\n";
    std::cout << "  Agents: " << snapshot.agents.size() << "\n";
    std::cout << "  Pending requests: " << snapshot.pending_requests << "\n";
    std::cout << "  Safe state: " << (snapshot.is_safe ? "YES" : "NO") << "\n";
    std::cout << "  Resources:\n";

    for (auto& [rt, total] : snapshot.total_resources) {
        auto avail_it = snapshot.available_resources.find(rt);
        ResourceQuantity avail = (avail_it != snapshot.available_resources.end())
                                 ? avail_it->second : 0;
        double util = (total > 0) ? 100.0 * (1.0 - static_cast<double>(avail) / total) : 0.0;
        std::cout << "    [" << rt << "] total=" << total
                  << " avail=" << avail
                  << " util=" << std::fixed << std::setprecision(1) << util << "%\n";
    }
    std::cout << "  ========================\n\n";
}

// ========== MetricsMonitor ==========

MetricsMonitor::MetricsMonitor() = default;

void MetricsMonitor::on_event(const MonitorEvent& event) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    switch (event.type) {
        case EventType::RequestSubmitted:
            metrics_.total_requests++;
            break;
        case EventType::RequestGranted:
            metrics_.granted_requests++;
            break;
        case EventType::RequestDenied:
            metrics_.denied_requests++;
            break;
        case EventType::RequestTimedOut:
            metrics_.timed_out_requests++;
            break;
        case EventType::UnsafeStateDetected:
            metrics_.unsafe_state_detections++;
            break;
        default:
            break;
    }
}

void MetricsMonitor::on_snapshot(const SystemSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    // Calculate average utilization across all resource types
    double total_util = 0.0;
    int count = 0;
    for (auto& [rt, total] : snapshot.total_resources) {
        if (total > 0) {
            auto avail_it = snapshot.available_resources.find(rt);
            ResourceQuantity avail = (avail_it != snapshot.available_resources.end())
                                     ? avail_it->second : 0;
            total_util += 100.0 * (1.0 - static_cast<double>(avail) / total);
            count++;
        }
    }
    metrics_.resource_utilization_percent = (count > 0) ? total_util / count : 0.0;

    // Check alert thresholds
    if (utilization_cb_ && metrics_.resource_utilization_percent > utilization_threshold_ * 100.0) {
        utilization_cb_("Resource utilization " +
                       std::to_string(metrics_.resource_utilization_percent) +
                       "% exceeds threshold");
    }

    if (queue_size_cb_ && snapshot.pending_requests > queue_size_threshold_) {
        queue_size_cb_("Queue size " + std::to_string(snapshot.pending_requests) +
                      " exceeds threshold " + std::to_string(queue_size_threshold_));
    }
}

MetricsMonitor::Metrics MetricsMonitor::get_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

void MetricsMonitor::reset_metrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_ = Metrics{};
}

void MetricsMonitor::set_utilization_alert_threshold(double threshold, AlertCallback cb) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    utilization_threshold_ = threshold;
    utilization_cb_ = std::move(cb);
}

void MetricsMonitor::set_queue_size_alert_threshold(std::size_t threshold, AlertCallback cb) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    queue_size_threshold_ = threshold;
    queue_size_cb_ = std::move(cb);
}

// ========== CompositeMonitor ==========

void CompositeMonitor::add_monitor(std::shared_ptr<Monitor> monitor) {
    monitors_.push_back(std::move(monitor));
}

void CompositeMonitor::on_event(const MonitorEvent& event) {
    for (auto& m : monitors_) {
        m->on_event(event);
    }
}

void CompositeMonitor::on_snapshot(const SystemSnapshot& snapshot) {
    for (auto& m : monitors_) {
        m->on_snapshot(snapshot);
    }
}

} // namespace agentguard
