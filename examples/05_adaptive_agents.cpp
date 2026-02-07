// 05_adaptive_agents.cpp
//
// Demonstrates AgentGuard's three novel safety features:
//   1. Progress Monitoring  — detects stuck agents and auto-releases resources
//   2. Delegation Tracking  — detects authority deadlock cycles (A delegates to B
//                             delegates to C delegates back to A)
//   3. Adaptive Demands     — agents don't declare max needs upfront; the system
//                             learns their resource patterns and runs a probabilistic
//                             Banker's Algorithm
//
// Scenario:
//   Three AI agents share two resources (API tokens and tool slots).
//   - Agent A delegates to B, B delegates to C, C tries to delegate back to A
//     → cycle detected.
//   - Agent B stops reporting progress → stall detected → resources auto-released.
//   - All agents use adaptive demand mode: no upfront max_need declaration required.

#include <agentguard/agentguard.hpp>

#include <iostream>
#include <thread>

using namespace agentguard;
using namespace std::chrono_literals;

int main() {
    std::cout << "=== AgentGuard: Adaptive Agents Demo ===\n\n";

    // ----------------------------------------------------------------
    // 1. Configure with all three features enabled.
    // ----------------------------------------------------------------
    Config config;
    config.default_request_timeout = 5s;

    // Progress monitoring: detect stalls after 200ms, check every 50ms
    config.progress.enabled = true;
    config.progress.default_stall_threshold = 200ms;
    config.progress.check_interval = 50ms;
    config.progress.auto_release_on_stall = true;

    // Delegation cycle detection: reject cyclic delegations
    config.delegation.enabled = true;
    config.delegation.cycle_action = DelegationCycleAction::RejectDelegation;

    // Adaptive demand estimation
    config.adaptive.enabled = true;
    config.adaptive.default_confidence_level = 0.90;
    config.adaptive.cold_start_default_demand = 3;
    config.adaptive.cold_start_headroom_factor = 1.5;
    config.adaptive.adaptive_headroom_factor = 1.2;

    ResourceManager manager(config);

    // Attach a verbose console monitor to observe events
    manager.set_monitor(
        std::make_shared<ConsoleMonitor>(ConsoleMonitor::Verbosity::Verbose));

    // ----------------------------------------------------------------
    // 2. Register resources.
    // ----------------------------------------------------------------
    constexpr ResourceTypeId API_TOKENS = 1;
    constexpr ResourceTypeId TOOL_SLOTS = 2;

    manager.register_resource(Resource(API_TOKENS, "API-Tokens", ResourceCategory::TokenBudget, 20));
    manager.register_resource(Resource(TOOL_SLOTS, "Tool-Slots", ResourceCategory::ToolSlot, 5));

    // ----------------------------------------------------------------
    // 3. Register agents in adaptive mode (no max_need declarations!).
    // ----------------------------------------------------------------
    AgentId a_id = manager.register_agent(Agent(0, "Agent-A"));
    AgentId b_id = manager.register_agent(Agent(0, "Agent-B"));
    AgentId c_id = manager.register_agent(Agent(0, "Agent-C"));

    manager.set_agent_demand_mode(a_id, DemandMode::Adaptive);
    manager.set_agent_demand_mode(b_id, DemandMode::Adaptive);
    manager.set_agent_demand_mode(c_id, DemandMode::Adaptive);

    std::cout << "Registered 3 agents in Adaptive demand mode (no max_need declared).\n\n";

    manager.start();

    // ----------------------------------------------------------------
    // 4. Feature 1: Adaptive resource requests.
    //    The DemandEstimator learns from each request.
    // ----------------------------------------------------------------
    std::cout << "--- Adaptive Resource Requests ---\n";

    auto s1 = manager.request_resources_adaptive(a_id, API_TOKENS, 3);
    std::cout << "Agent-A requests 3 API tokens: " << to_string(s1) << "\n";
    manager.report_progress(a_id, "steps", 1);

    auto s2 = manager.request_resources_adaptive(b_id, API_TOKENS, 4);
    std::cout << "Agent-B requests 4 API tokens: " << to_string(s2) << "\n";
    manager.report_progress(b_id, "steps", 1);

    auto s3 = manager.request_resources_adaptive(c_id, TOOL_SLOTS, 2);
    std::cout << "Agent-C requests 2 tool slots: " << to_string(s3) << "\n";
    manager.report_progress(c_id, "steps", 1);

    // Probabilistic safety check
    auto prob_result = manager.check_safety_probabilistic();
    std::cout << "\nProbabilistic safety check at "
              << (prob_result.confidence_level * 100) << "% confidence: "
              << (prob_result.is_safe ? "SAFE" : "UNSAFE") << "\n\n";

    // ----------------------------------------------------------------
    // 5. Feature 2: Delegation cycle detection.
    // ----------------------------------------------------------------
    std::cout << "--- Delegation Tracking ---\n";

    auto d1 = manager.report_delegation(a_id, b_id, "Summarize document");
    std::cout << "A delegates to B: accepted=" << d1.accepted
              << " cycle=" << d1.cycle_detected << "\n";

    auto d2 = manager.report_delegation(b_id, c_id, "Fact-check claims");
    std::cout << "B delegates to C: accepted=" << d2.accepted
              << " cycle=" << d2.cycle_detected << "\n";

    auto d3 = manager.report_delegation(c_id, a_id, "Get original source");
    std::cout << "C delegates to A: accepted=" << d3.accepted
              << " cycle=" << d3.cycle_detected << "\n";
    if (d3.cycle_detected) {
        std::cout << "  >> Cycle detected! Path: ";
        for (auto id : d3.cycle_path) std::cout << id << " -> ";
        std::cout << "\n";
    }

    std::cout << "\nActive delegations: " << manager.get_all_delegations().size() << "\n";
    manager.complete_delegation(a_id, b_id);
    manager.complete_delegation(b_id, c_id);
    std::cout << "After completing A->B and B->C: "
              << manager.get_all_delegations().size() << " active\n\n";

    // ----------------------------------------------------------------
    // 6. Feature 3: Progress monitoring & stall detection.
    // ----------------------------------------------------------------
    std::cout << "--- Progress Monitoring ---\n";

    // Agent A and C keep reporting progress
    manager.report_progress(a_id, "steps", 2);
    manager.report_progress(c_id, "steps", 2);

    // Agent B stops reporting... simulate a stall
    std::cout << "Agent-B stops reporting progress. Waiting for stall detection...\n";
    std::this_thread::sleep_for(350ms);

    auto stalled = manager.get_stalled_agents();
    std::cout << "Stalled agents: " << stalled.size() << "\n";
    for (auto id : stalled) {
        std::cout << "  Agent " << id << " is stalled"
                  << (manager.is_agent_stalled(id) ? " (confirmed)" : "") << "\n";
    }
    std::cout << "\n";

    // ----------------------------------------------------------------
    // 7. Clean up.
    // ----------------------------------------------------------------
    manager.release_all_resources(a_id);
    manager.release_all_resources(c_id);
    // Agent B's resources were auto-released by the stall handler

    manager.stop();

    std::cout << "=== Done ===\n";
    return 0;
}
