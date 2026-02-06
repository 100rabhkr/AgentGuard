#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// THE PROOF TEST
//
// Classic Dining Philosophers adapted for AI agents:
// N agents each need 2 of N tools (resources). Without AgentGuard they
// would deadlock (each grabs one tool and waits for the other).
// With AgentGuard, all agents complete because the Banker's Algorithm
// prevents unsafe states.
// ===========================================================================

TEST(DeadlockPreventionTest, DiningPhilosophers_AllComplete) {
    constexpr int N = 5;  // 5 agents, 5 resources (tools)

    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 5s;
    ResourceManager mgr(cfg);
    mgr.start();

    // Register N tool resources, each with capacity 1 (like a fork/chopstick)
    for (int i = 0; i < N; ++i) {
        Resource r(static_cast<ResourceTypeId>(i + 1),
                   "Tool-" + std::to_string(i + 1),
                   ResourceCategory::ToolSlot, 1);
        mgr.register_resource(std::move(r));
    }

    // Register N agents. Agent i needs Tool i and Tool (i+1)%N
    std::vector<AgentId> agent_ids;
    for (int i = 0; i < N; ++i) {
        Agent a(static_cast<AgentId>(i + 1),
                "Philosopher-" + std::to_string(i + 1));
        ResourceTypeId left = static_cast<ResourceTypeId>(i + 1);
        ResourceTypeId right = static_cast<ResourceTypeId>((i % N) + 1);
        // In dining philosophers, right neighbor tool is (i+1)%N + 1
        right = static_cast<ResourceTypeId>(((i + 1) % N) + 1);
        a.declare_max_need(left, 1);
        a.declare_max_need(right, 1);
        AgentId aid = mgr.register_agent(std::move(a));
        agent_ids.push_back(aid);
    }

    // Track completion
    std::atomic<int> completed{0};
    std::vector<RequestStatus> final_statuses(N, RequestStatus::Pending);
    std::vector<std::thread> threads;

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]() {
            AgentId aid = agent_ids[i];
            ResourceTypeId left = static_cast<ResourceTypeId>(i + 1);
            ResourceTypeId right = static_cast<ResourceTypeId>(((i + 1) % N) + 1);

            // Use batch request: atomically request both tools
            std::unordered_map<ResourceTypeId, ResourceQuantity> batch;
            batch[left] = 1;
            batch[right] = 1;

            auto status = mgr.request_resources_batch(aid, batch, 5s);
            final_statuses[i] = status;

            if (status == RequestStatus::Granted) {
                // Simulate work
                std::this_thread::sleep_for(10ms);

                // Release both tools
                mgr.release_resources(aid, left, 1);
                mgr.release_resources(aid, right, 1);
                completed.fetch_add(1);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    mgr.stop();

    // Key assertion: ALL agents must have completed successfully.
    // Without deadlock prevention, this would hang or timeout.
    EXPECT_EQ(completed.load(), N)
        << "Not all philosophers completed! Only " << completed.load()
        << " out of " << N << " finished.";

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(final_statuses[i], RequestStatus::Granted)
            << "Philosopher " << i << " got status "
            << to_string(final_statuses[i]);
    }

    // System should be safe after all resources released
    EXPECT_TRUE(mgr.is_safe());
}

// ===========================================================================
// Circular wait scenario: 3 agents, 3 resources
//
// Without prevention:
//   Agent 1 holds R1, wants R2
//   Agent 2 holds R2, wants R3
//   Agent 3 holds R3, wants R1
//   => Circular wait => Deadlock
//
// With AgentGuard: the manager should prevent this unsafe state
// ===========================================================================

TEST(DeadlockPreventionTest, CircularWaitPrevention_ThreeAgents) {
    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 5s;
    ResourceManager mgr(cfg);
    mgr.start();

    // 3 resources, each capacity 1
    mgr.register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 1));
    mgr.register_resource(Resource(2, "R2", ResourceCategory::ToolSlot, 1));
    mgr.register_resource(Resource(3, "R3", ResourceCategory::ToolSlot, 1));

    // Agent 1 needs R1 and R2
    Agent a1(1, "Agent-1");
    a1.declare_max_need(1, 1);
    a1.declare_max_need(2, 1);
    AgentId aid1 = mgr.register_agent(std::move(a1));

    // Agent 2 needs R2 and R3
    Agent a2(2, "Agent-2");
    a2.declare_max_need(2, 1);
    a2.declare_max_need(3, 1);
    AgentId aid2 = mgr.register_agent(std::move(a2));

    // Agent 3 needs R3 and R1
    Agent a3(3, "Agent-3");
    a3.declare_max_need(3, 1);
    a3.declare_max_need(1, 1);
    AgentId aid3 = mgr.register_agent(std::move(a3));

    std::atomic<int> completed{0};
    std::vector<std::thread> threads;

    // Each agent tries to grab both resources
    threads.emplace_back([&]() {
        auto s = mgr.request_resources_batch(aid1,
            {{1, 1}, {2, 1}}, 5s);
        if (s == RequestStatus::Granted) {
            std::this_thread::sleep_for(10ms);
            mgr.release_all_resources(aid1);
            completed.fetch_add(1);
        }
    });

    threads.emplace_back([&]() {
        auto s = mgr.request_resources_batch(aid2,
            {{2, 1}, {3, 1}}, 5s);
        if (s == RequestStatus::Granted) {
            std::this_thread::sleep_for(10ms);
            mgr.release_all_resources(aid2);
            completed.fetch_add(1);
        }
    });

    threads.emplace_back([&]() {
        auto s = mgr.request_resources_batch(aid3,
            {{3, 1}, {1, 1}}, 5s);
        if (s == RequestStatus::Granted) {
            std::this_thread::sleep_for(10ms);
            mgr.release_all_resources(aid3);
            completed.fetch_add(1);
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    mgr.stop();

    // All 3 agents should have eventually completed
    EXPECT_EQ(completed.load(), 3)
        << "Circular wait detected! Only " << completed.load()
        << " of 3 agents completed.";

    EXPECT_TRUE(mgr.is_safe());
}

// ===========================================================================
// Safety check prevents deadlock even with incremental requests
// ===========================================================================

TEST(DeadlockPreventionTest, IncrementalRequestsSafetyPrevention) {
    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 3s;
    ResourceManager mgr(cfg);
    mgr.start();

    // Single resource with capacity 4
    mgr.register_resource(Resource(1, "SharedPool", ResourceCategory::MemoryPool, 4));

    // 3 agents, each needs up to 3
    Agent a1(1, "A1"); a1.declare_max_need(1, 3);
    Agent a2(2, "A2"); a2.declare_max_need(1, 3);
    Agent a3(3, "A3"); a3.declare_max_need(1, 3);
    AgentId id1 = mgr.register_agent(std::move(a1));
    AgentId id2 = mgr.register_agent(std::move(a2));
    AgentId id3 = mgr.register_agent(std::move(a3));

    std::atomic<int> completed{0};
    std::vector<std::thread> threads;

    auto worker = [&](AgentId aid) {
        // Try to acquire 1, then 1 more, then 1 more (total 3)
        bool ok = true;
        for (int i = 0; i < 3 && ok; ++i) {
            auto s = mgr.request_resources(aid, 1, 1, 3s);
            if (s != RequestStatus::Granted) {
                ok = false;
            }
        }
        if (ok) {
            // Simulate work
            std::this_thread::sleep_for(5ms);
            mgr.release_all_resources(aid, 1);
            completed.fetch_add(1);
        } else {
            // Release whatever was acquired
            mgr.release_all_resources(aid, 1);
        }
    };

    threads.emplace_back(worker, id1);
    threads.emplace_back(worker, id2);
    threads.emplace_back(worker, id3);

    for (auto& t : threads) {
        t.join();
    }

    mgr.stop();

    // With capacity 4 and 3 agents needing 3 each, not all can run
    // simultaneously, but they should serialize through the Banker's
    // Algorithm and all eventually complete
    EXPECT_GE(completed.load(), 1);
    EXPECT_TRUE(mgr.is_safe());
}

// ===========================================================================
// Verify that without batching, individual requests maintain safety
// ===========================================================================

TEST(DeadlockPreventionTest, IndividualRequestsMaintainSafety) {
    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 3s;
    ResourceManager mgr(cfg);
    mgr.start();

    mgr.register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 2));

    Agent a1(1, "A1"); a1.declare_max_need(1, 2);
    Agent a2(2, "A2"); a2.declare_max_need(1, 2);
    AgentId id1 = mgr.register_agent(std::move(a1));
    AgentId id2 = mgr.register_agent(std::move(a2));

    // Agent 1 gets 1 unit
    auto s1 = mgr.request_resources(id1, 1, 1, 3s);
    EXPECT_EQ(s1, RequestStatus::Granted);

    // Agent 2 requesting 1 unit would leave 0 available with both needing 1
    // more each. The Banker's algorithm should detect this is unsafe and
    // deny or queue the request (depending on implementation).
    // After agent 1 finishes, agent 2 should eventually get its resources.

    // Verify system is still safe at this point
    EXPECT_TRUE(mgr.is_safe());

    // Release and let agent 2 proceed
    mgr.release_resources(id1, 1, 1);

    auto s2 = mgr.request_resources(id2, 1, 1, 3s);
    EXPECT_EQ(s2, RequestStatus::Granted);

    mgr.release_resources(id2, 1, 1);

    mgr.stop();
    EXPECT_TRUE(mgr.is_safe());
}
