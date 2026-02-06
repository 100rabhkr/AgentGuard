// 01_basic_usage.cpp
//
// Minimal AgentGuard example: Two resources, three agents.
// Demonstrates the Banker's Algorithm preventing unsafe states when
// agents compete for limited resources.
//
// Scenario:
//   - Resource A has 10 units, Resource B has 5 units.
//   - Three agents declare their maximum needs.
//   - The ResourceManager grants requests only when doing so leaves
//     the system in a safe state (every agent can still finish).
//   - A request that would create a deadlock risk is denied.

#include <agentguard/agentguard.hpp>

#include <iostream>
#include <string>

using namespace agentguard;
using namespace std::chrono_literals;

int main() {
    std::cout << "=== AgentGuard: Basic Usage Example ===\n\n";

    // ----------------------------------------------------------------
    // 1. Create the ResourceManager with default configuration.
    // ----------------------------------------------------------------
    Config config;
    config.default_request_timeout = 5s;
    ResourceManager manager(config);

    // Attach a console monitor so we can see what happens internally.
    manager.set_monitor(
        std::make_shared<ConsoleMonitor>(ConsoleMonitor::Verbosity::Verbose));

    // ----------------------------------------------------------------
    // 2. Register two resources.
    // ----------------------------------------------------------------
    constexpr ResourceTypeId RES_A = 1;
    constexpr ResourceTypeId RES_B = 2;

    Resource resource_a(RES_A, "Resource-A", ResourceCategory::Custom, 10);
    Resource resource_b(RES_B, "Resource-B", ResourceCategory::Custom, 5);

    manager.register_resource(resource_a);
    manager.register_resource(resource_b);

    std::cout << "Registered Resource-A (capacity 10) and Resource-B (capacity 5).\n\n";

    // ----------------------------------------------------------------
    // 3. Register three agents and declare their maximum needs.
    //    The Banker's Algorithm uses these declarations to decide
    //    whether granting a request keeps the system safe.
    // ----------------------------------------------------------------

    // Agent Alpha: needs at most 7 of A and 3 of B.
    Agent alpha(0, "Alpha");
    alpha.declare_max_need(RES_A, 7);
    alpha.declare_max_need(RES_B, 3);
    AgentId alpha_id = manager.register_agent(alpha);

    // Agent Beta: needs at most 4 of A and 2 of B.
    Agent beta(0, "Beta");
    beta.declare_max_need(RES_A, 4);
    beta.declare_max_need(RES_B, 2);
    AgentId beta_id = manager.register_agent(beta);

    // Agent Gamma: needs at most 3 of A and 3 of B.
    Agent gamma(0, "Gamma");
    gamma.declare_max_need(RES_A, 3);
    gamma.declare_max_need(RES_B, 3);
    AgentId gamma_id = manager.register_agent(gamma);

    std::cout << "Registered 3 agents with declared max needs.\n\n";

    // ----------------------------------------------------------------
    // 4. Start the manager (launches the background queue processor).
    // ----------------------------------------------------------------
    manager.start();

    // ----------------------------------------------------------------
    // 5. Make some resource requests and observe the Banker's Algorithm.
    // ----------------------------------------------------------------

    // Request 1: Alpha asks for 3 of A.  Safe -- plenty of resources left.
    std::cout << "--- Alpha requests 3 of A ---\n";
    auto s1 = manager.request_resources(alpha_id, RES_A, 3, 5s);
    std::cout << "Result: " << to_string(s1) << "\n\n";

    // Request 2: Beta asks for 2 of A and 2 of B.
    std::cout << "--- Beta requests 2 of A ---\n";
    auto s2 = manager.request_resources(beta_id, RES_A, 2, 5s);
    std::cout << "Result: " << to_string(s2) << "\n";

    std::cout << "--- Beta requests 2 of B ---\n";
    auto s3 = manager.request_resources(beta_id, RES_B, 2, 5s);
    std::cout << "Result: " << to_string(s3) << "\n\n";

    // Request 3: Gamma asks for 2 of A.
    std::cout << "--- Gamma requests 2 of A ---\n";
    auto s4 = manager.request_resources(gamma_id, RES_A, 2, 5s);
    std::cout << "Result: " << to_string(s4) << "\n\n";

    // At this point: A has 10 - 3 - 2 - 2 = 3 available,
    //                B has 5 - 0 - 2 - 0 = 3 available.
    // Alpha still needs 4 of A and 3 of B to finish.
    // Beta  still needs 2 of A and 0 of B.
    // Gamma still needs 1 of A and 3 of B.

    // Request 4: Alpha asks for 3 more of B.
    // If granted, B would have 0 available. Beta (still needing 2A) could finish
    // (3A available after gamma gets 1A is tight).  The Banker's Algorithm will
    // determine whether this is safe.
    std::cout << "--- Alpha requests 3 of B (may push system to edge) ---\n";
    auto s5 = manager.request_resources(alpha_id, RES_B, 3, 5s);
    std::cout << "Result: " << to_string(s5) << "\n\n";

    // ----------------------------------------------------------------
    // 6. Show the current system snapshot.
    // ----------------------------------------------------------------
    auto snapshot = manager.get_snapshot();
    std::cout << "=== System Snapshot ===\n";
    std::cout << "System is " << (snapshot.is_safe ? "SAFE" : "UNSAFE") << "\n";
    std::cout << "Pending requests: " << snapshot.pending_requests << "\n";
    for (const auto& [rt, total] : snapshot.total_resources) {
        auto avail = snapshot.available_resources.at(rt);
        std::cout << "  Resource " << rt << ": " << avail << " / " << total << " available\n";
    }
    for (const auto& agent_snap : snapshot.agents) {
        std::cout << "  Agent \"" << agent_snap.name << "\" [" << to_string(agent_snap.state) << "]:\n";
        for (const auto& [rt, alloc] : agent_snap.allocation) {
            auto max_c = agent_snap.max_claim.count(rt) ? agent_snap.max_claim.at(rt) : 0;
            std::cout << "    Resource " << rt << ": holding " << alloc << " / max " << max_c << "\n";
        }
    }
    std::cout << "\n";

    // ----------------------------------------------------------------
    // 7. Release resources and clean up.
    // ----------------------------------------------------------------
    std::cout << "--- Releasing all resources ---\n";
    manager.release_all_resources(alpha_id);
    manager.release_all_resources(beta_id);
    manager.release_all_resources(gamma_id);

    // Verify everything is released.
    auto final_snap = manager.get_snapshot();
    std::cout << "After release, system is " << (final_snap.is_safe ? "SAFE" : "UNSAFE") << "\n";
    for (const auto& [rt, total] : final_snap.total_resources) {
        auto avail = final_snap.available_resources.at(rt);
        std::cout << "  Resource " << rt << ": " << avail << " / " << total << " available\n";
    }

    // ----------------------------------------------------------------
    // 8. Stop the manager.
    // ----------------------------------------------------------------
    manager.stop();

    std::cout << "\n=== Done ===\n";
    return 0;
}
