// 03_tool_sharing.cpp
//
// Four AI agents share three tools:
//   - Code interpreter: exclusive access (1 slot)
//   - Web browser:      concurrent access (2 slots)
//   - Filesystem:       concurrent access (3 slots)
//
// Uses the ToolSlot helper from ai/tool_slot.hpp.  The Banker's Algorithm
// prevents deadlocks when agents hold some tools and request others.
//
// Scenario that would deadlock without AgentGuard:
//   Agent 1 holds the code interpreter, wants the browser.
//   Agent 2 holds both browser slots, wants the code interpreter.
//   -> Classic circular wait.  The Banker's Algorithm detects that granting
//      one of these requests would leave no safe execution sequence, so it
//      denies (or queues) the request instead of letting the deadlock form.

#include <agentguard/agentguard.hpp>

#include <iostream>
#include <thread>
#include <vector>

using namespace agentguard;
using namespace agentguard::ai;
using namespace std::chrono_literals;

// Each agent performs a multi-tool workflow.
void agent_task(ResourceManager& manager,
                AgentId id,
                const std::string& name,
                ResourceTypeId interpreter_id,
                ResourceTypeId browser_id,
                ResourceTypeId fs_id) {
    // Step 1: Grab some filesystem access.
    std::cout << "[" << name << "] requesting 1 filesystem slot...\n";
    auto s1 = manager.request_resources(id, fs_id, 1, 8s);
    std::cout << "[" << name << "] filesystem: " << to_string(s1) << "\n";

    // Simulate work with the filesystem.
    std::this_thread::sleep_for(30ms);

    // Step 2: While holding filesystem, try to get the web browser.
    std::cout << "[" << name << "] requesting 1 browser slot (while holding FS)...\n";
    auto s2 = manager.request_resources(id, browser_id, 1, 8s);
    std::cout << "[" << name << "] browser: " << to_string(s2) << "\n";

    // Simulate browsing.
    std::this_thread::sleep_for(40ms);

    // Step 3: Now try the exclusive code interpreter.
    // With 4 agents all trying this sequence, the Banker's Algorithm
    // will ensure that granting interpreter access is always safe.
    std::cout << "[" << name << "] requesting code interpreter (exclusive)...\n";
    auto s3 = manager.request_resources(id, interpreter_id, 1, 8s);
    std::cout << "[" << name << "] code interpreter: " << to_string(s3) << "\n";

    if (s3 == RequestStatus::Granted) {
        // Simulate executing code.
        std::this_thread::sleep_for(60ms);
        manager.release_resources(id, interpreter_id, 1);
        std::cout << "[" << name << "] released code interpreter.\n";
    }

    // Release browser.
    if (s2 == RequestStatus::Granted) {
        manager.release_resources(id, browser_id, 1);
        std::cout << "[" << name << "] released browser.\n";
    }

    // Release filesystem.
    if (s1 == RequestStatus::Granted) {
        manager.release_resources(id, fs_id, 1);
        std::cout << "[" << name << "] released filesystem.\n";
    }

    std::cout << "[" << name << "] finished.\n";
}

int main() {
    std::cout << "=== AgentGuard: Tool Sharing Example ===\n\n";

    // ----------------------------------------------------------------
    // 1. Create and configure the ResourceManager.
    // ----------------------------------------------------------------
    Config config;
    config.default_request_timeout = 10s;
    config.processor_poll_interval = 5ms;
    ResourceManager manager(config);

    manager.set_monitor(
        std::make_shared<ConsoleMonitor>(ConsoleMonitor::Verbosity::Verbose));

    // Use shortest-need-first scheduling to maximize throughput.
    manager.set_scheduling_policy(std::make_unique<ShortestNeedPolicy>());

    // ----------------------------------------------------------------
    // 2. Register tool resources using the ToolSlot helper.
    // ----------------------------------------------------------------
    constexpr ResourceTypeId INTERP_ID  = 10;
    constexpr ResourceTypeId BROWSER_ID = 20;
    constexpr ResourceTypeId FS_ID      = 30;

    // Code interpreter: exclusive access, 1 slot.
    ToolSlot interpreter(INTERP_ID, "CodeInterpreter",
                         ToolSlot::AccessMode::Exclusive, 1);
    interpreter.set_estimated_usage_duration(100ms);
    manager.register_resource(interpreter.as_resource());

    // Web browser: concurrent access, 2 slots.
    ToolSlot browser(BROWSER_ID, "WebBrowser",
                     ToolSlot::AccessMode::Concurrent, 2);
    browser.set_estimated_usage_duration(200ms);
    manager.register_resource(browser.as_resource());

    // Filesystem: concurrent access, 3 slots.
    ToolSlot filesystem(FS_ID, "Filesystem",
                        ToolSlot::AccessMode::Concurrent, 3);
    filesystem.set_estimated_usage_duration(50ms);
    manager.register_resource(filesystem.as_resource());

    std::cout << "Registered 3 tools: CodeInterpreter(1), WebBrowser(2), Filesystem(3).\n\n";

    // ----------------------------------------------------------------
    // 3. Register four agents.  Each might need all three tools.
    // ----------------------------------------------------------------
    struct AgentDef {
        std::string name;
        Priority priority;
    };
    AgentDef defs[] = {
        {"Coder",      PRIORITY_HIGH},
        {"Researcher", PRIORITY_NORMAL},
        {"Analyst",    PRIORITY_NORMAL},
        {"Archivist",  PRIORITY_LOW},
    };

    std::vector<AgentId> agent_ids;
    for (const auto& def : defs) {
        Agent a(0, def.name, def.priority);
        a.declare_max_need(INTERP_ID, 1);
        a.declare_max_need(BROWSER_ID, 1);
        a.declare_max_need(FS_ID, 1);
        AgentId aid = manager.register_agent(a);
        agent_ids.push_back(aid);
        std::cout << "Registered agent \"" << def.name
                  << "\" (priority " << def.priority << ") -> id " << aid << "\n";
    }
    std::cout << "\n";

    // ----------------------------------------------------------------
    // 4. Start and run all agents concurrently.
    // ----------------------------------------------------------------
    manager.start();

    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < agent_ids.size(); ++i) {
        threads.emplace_back(agent_task, std::ref(manager),
                             agent_ids[i], defs[i].name,
                             INTERP_ID, BROWSER_ID, FS_ID);
    }

    for (auto& t : threads) {
        t.join();
    }

    // ----------------------------------------------------------------
    // 5. Verify that the system is clean.
    // ----------------------------------------------------------------
    auto snap = manager.get_snapshot();
    std::cout << "\n=== Final System State ===\n";
    std::cout << "System is " << (snap.is_safe ? "SAFE" : "UNSAFE") << "\n";
    for (const auto& [rt, total] : snap.total_resources) {
        auto avail = snap.available_resources.at(rt);
        std::cout << "  Resource " << rt << ": " << avail << " / " << total << " available\n";
    }
    std::cout << "Pending requests: " << snap.pending_requests << "\n";

    // ----------------------------------------------------------------
    // 6. Clean up.
    // ----------------------------------------------------------------
    manager.stop();

    std::cout << "\n=== Done (no deadlocks!) ===\n";
    return 0;
}
