// 04_priority_agents.cpp
//
// Demonstrates priority-based scheduling with MetricsMonitor.
//
// Four agents at different priority levels compete for a shared resource
// (a token budget).  The PriorityPolicy ensures that CRITICAL and HIGH
// priority agents are served before NORMAL and LOW ones.
//
// At the end, MetricsMonitor reports aggregate statistics showing how
// the system performed under contention.

#include <agentguard/agentguard.hpp>

#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace agentguard;
using namespace agentguard::ai;
using namespace std::chrono_literals;

// Shared mutex for orderly console output.
static std::mutex cout_mutex;

void log(const std::string& msg) {
    std::lock_guard<std::mutex> lk(cout_mutex);
    std::cout << msg << "\n";
}

// Each agent repeatedly requests tokens, does work, and releases them.
void agent_loop(ResourceManager& manager,
                AgentId id,
                const std::string& name,
                ResourceTypeId token_id,
                int num_rounds,
                ResourceQuantity tokens_per_round) {
    int granted_count = 0;
    int denied_count  = 0;

    for (int i = 0; i < num_rounds; ++i) {
        log("[" + name + "] round " + std::to_string(i + 1) +
            ": requesting " + std::to_string(tokens_per_round) + " tokens...");

        auto status = manager.request_resources(id, token_id,
                                                tokens_per_round, 3s);

        if (status == RequestStatus::Granted) {
            ++granted_count;
            log("[" + name + "] GRANTED -- doing work...");

            // Simulate work proportional to tokens consumed.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10 * tokens_per_round));

            manager.release_resources(id, token_id, tokens_per_round);
            log("[" + name + "] released " + std::to_string(tokens_per_round) +
                " tokens.");
        } else {
            ++denied_count;
            log("[" + name + "] " + std::string(to_string(status)) +
                " -- backing off.");
            std::this_thread::sleep_for(50ms);
        }
    }

    log("[" + name + "] finished: " + std::to_string(granted_count) +
        " granted, " + std::to_string(denied_count) + " denied.");
}

int main() {
    std::cout << "=== AgentGuard: Priority Agents Example ===\n\n";

    // ----------------------------------------------------------------
    // 1. Create the ResourceManager.
    // ----------------------------------------------------------------
    Config config;
    config.default_request_timeout = 5s;
    config.processor_poll_interval = 5ms;
    config.snapshot_interval = 2s;
    ResourceManager manager(config);

    // ----------------------------------------------------------------
    // 2. Set up monitoring: MetricsMonitor + ConsoleMonitor.
    // ----------------------------------------------------------------
    auto metrics_monitor = std::make_shared<MetricsMonitor>();
    auto console_monitor = std::make_shared<ConsoleMonitor>(
        ConsoleMonitor::Verbosity::Normal);

    // Set up alerts on the metrics monitor.
    metrics_monitor->set_utilization_alert_threshold(0.9, [](const std::string& msg) {
        std::lock_guard<std::mutex> lk(cout_mutex);
        std::cout << "[ALERT] " << msg << "\n";
    });
    metrics_monitor->set_queue_size_alert_threshold(5, [](const std::string& msg) {
        std::lock_guard<std::mutex> lk(cout_mutex);
        std::cout << "[ALERT] " << msg << "\n";
    });

    auto composite = std::make_shared<CompositeMonitor>();
    composite->add_monitor(console_monitor);
    composite->add_monitor(metrics_monitor);
    manager.set_monitor(composite);

    // Priority-based scheduling -- the key feature of this example.
    manager.set_scheduling_policy(std::make_unique<PriorityPolicy>());

    // ----------------------------------------------------------------
    // 3. Register a shared token budget resource.
    //    Only 100 tokens available -- creates contention.
    // ----------------------------------------------------------------
    constexpr ResourceTypeId TOKEN_ID = 1;

    TokenBudget token_budget(TOKEN_ID, "SharedTokenPool", 100, 60s);
    token_budget.set_input_output_ratio(0.7);
    manager.register_resource(token_budget.as_resource());

    std::cout << "Registered SharedTokenPool with 100 tokens.\n\n";

    // ----------------------------------------------------------------
    // 4. Register agents at four different priority levels.
    // ----------------------------------------------------------------
    struct AgentSpec {
        std::string name;
        Priority priority;
        ResourceQuantity max_need;
        ResourceQuantity per_round;
        int rounds;
    };

    AgentSpec specs[] = {
        {"EmergencyBot",  PRIORITY_CRITICAL, 40, 20, 5},
        {"PrimaryAgent",  PRIORITY_HIGH,     30, 15, 6},
        {"WorkerAgent",   PRIORITY_NORMAL,   25, 10, 7},
        {"BackgroundBot", PRIORITY_LOW,      20,  8, 7},
    };

    std::vector<AgentId> ids;
    for (const auto& spec : specs) {
        Agent a(0, spec.name, spec.priority);
        a.declare_max_need(TOKEN_ID, spec.max_need);

        // Set AI-specific metadata.
        a.set_model_identifier("model-v1");
        a.set_task_description(spec.name + " task");

        AgentId aid = manager.register_agent(a);
        ids.push_back(aid);

        std::cout << "  Registered \"" << spec.name
                  << "\" priority=" << spec.priority
                  << " max_need=" << spec.max_need
                  << " per_round=" << spec.per_round
                  << " rounds=" << spec.rounds
                  << " -> id " << aid << "\n";
    }
    std::cout << "\n";

    // ----------------------------------------------------------------
    // 5. Start the manager and launch agent threads.
    // ----------------------------------------------------------------
    manager.start();

    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        threads.emplace_back(agent_loop, std::ref(manager),
                             ids[i], specs[i].name,
                             TOKEN_ID, specs[i].rounds,
                             specs[i].per_round);
    }

    for (auto& t : threads) {
        t.join();
    }

    // ----------------------------------------------------------------
    // 6. Print the final metrics report.
    // ----------------------------------------------------------------
    auto m = metrics_monitor->get_metrics();

    std::cout << "\n";
    std::cout << "==========================================================\n";
    std::cout << "              METRICS REPORT                              \n";
    std::cout << "==========================================================\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Total requests submitted:    " << m.total_requests       << "\n";
    std::cout << "  Granted:                     " << m.granted_requests     << "\n";
    std::cout << "  Denied:                      " << m.denied_requests      << "\n";
    std::cout << "  Timed out:                   " << m.timed_out_requests   << "\n";
    std::cout << "  Average wait time (ms):      " << m.average_wait_time_ms << "\n";
    std::cout << "  Safety check avg (us):       " << m.safety_check_avg_duration_us << "\n";
    std::cout << "  Unsafe state detections:     " << m.unsafe_state_detections << "\n";
    std::cout << "  Resource utilization (%):    " << m.resource_utilization_percent << "\n";
    std::cout << "==========================================================\n";

    // Show that higher-priority agents should have more granted requests.
    std::cout << "\nExpected behavior: EmergencyBot (CRITICAL) and PrimaryAgent (HIGH)\n"
              << "should see more grants relative to their request count than\n"
              << "WorkerAgent (NORMAL) and BackgroundBot (LOW) under contention.\n";

    // ----------------------------------------------------------------
    // 7. Final snapshot and cleanup.
    // ----------------------------------------------------------------
    auto snap = manager.get_snapshot();
    std::cout << "\n=== Final Snapshot ===\n";
    std::cout << "System safe: " << (snap.is_safe ? "yes" : "no") << "\n";
    std::cout << "Pending requests: " << snap.pending_requests << "\n";
    for (const auto& [rt, total] : snap.total_resources) {
        auto avail = snap.available_resources.at(rt);
        std::cout << "  Token pool: " << avail << " / " << total << " available\n";
    }

    // Release any straggling resources and stop.
    for (auto aid : ids) {
        manager.release_all_resources(aid);
    }
    manager.stop();

    std::cout << "\n=== Done ===\n";
    return 0;
}
