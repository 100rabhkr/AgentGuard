#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <atomic>
#include <random>
#include <thread>
#include <vector>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Concurrent stress test: 10 agents, 3 resources, parallel requests/releases
// ===========================================================================

TEST(ConcurrentAgentsTest, StressTest_10Agents_3Resources) {
    constexpr int NUM_AGENTS = 10;
    constexpr int NUM_RESOURCES = 3;
    constexpr int OPS_PER_AGENT = 20;

    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 5s;
    ResourceManager mgr(cfg);
    mgr.start();

    // Register 3 resources with moderate capacity
    mgr.register_resource(
        Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 10));
    mgr.register_resource(
        Resource(2, "Token-Budget", ResourceCategory::TokenBudget, 15));
    mgr.register_resource(
        Resource(3, "Tool-Slots", ResourceCategory::ToolSlot, 8));

    // Register 10 agents, each declaring max need for all 3 resources
    std::vector<AgentId> agent_ids;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        Agent a(static_cast<AgentId>(i + 1),
                "Agent-" + std::to_string(i + 1));
        a.declare_max_need(1, 3);  // Max 3 API slots
        a.declare_max_need(2, 4);  // Max 4 token budget units
        a.declare_max_need(3, 2);  // Max 2 tool slots
        AgentId aid = mgr.register_agent(std::move(a));
        agent_ids.push_back(aid);
    }

    // Track stats
    std::atomic<int> total_grants{0};
    std::atomic<int> total_denials{0};
    std::atomic<int> total_releases{0};
    std::atomic<int> errors{0};

    // Each agent runs in its own thread doing request/release cycles
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        threads.emplace_back([&, i]() {
            AgentId aid = agent_ids[i];
            std::mt19937 rng(static_cast<unsigned>(i * 42 + 7));
            std::uniform_int_distribution<int> resource_dist(1, NUM_RESOURCES);
            std::uniform_int_distribution<int> qty_dist(1, 2);

            for (int op = 0; op < OPS_PER_AGENT; ++op) {
                try {
                    ResourceTypeId rt = static_cast<ResourceTypeId>(
                        resource_dist(rng));
                    ResourceQuantity qty = qty_dist(rng);

                    // Try to request
                    auto status = mgr.request_resources(aid, rt, qty, 500ms);

                    if (status == RequestStatus::Granted) {
                        total_grants.fetch_add(1);

                        // Brief simulated work
                        std::this_thread::sleep_for(1ms);

                        // Release what we acquired
                        mgr.release_resources(aid, rt, qty);
                        total_releases.fetch_add(1);
                    } else {
                        total_denials.fetch_add(1);
                    }
                } catch (const MaxClaimExceededException&) {
                    // Expected if random qty exceeds declared max
                    total_denials.fetch_add(1);
                } catch (const std::exception& e) {
                    errors.fetch_add(1);
                }
            }
        });
    }

    // Wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }

    mgr.stop();

    // ==== Verification ====

    // No unexpected errors (crashes, data races, etc.)
    EXPECT_EQ(errors.load(), 0)
        << "Unexpected errors occurred during concurrent operations";

    // We should have processed some requests
    EXPECT_GT(total_grants.load() + total_denials.load(), 0)
        << "No operations were processed";

    // Every grant should have been followed by a release
    EXPECT_EQ(total_grants.load(), total_releases.load())
        << "Grant/release mismatch: grants=" << total_grants.load()
        << " releases=" << total_releases.load();

    // System should be in a safe state after everything completes
    EXPECT_TRUE(mgr.is_safe());

    // All resources should be fully available (nothing leaked)
    for (int r = 1; r <= NUM_RESOURCES; ++r) {
        auto res = mgr.get_resource(static_cast<ResourceTypeId>(r));
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(res->allocated(), 0)
            << "Resource " << r << " has leaked allocations: "
            << res->allocated();
        EXPECT_EQ(res->available(), res->total_capacity())
            << "Resource " << r << " available != capacity";
    }
}

// ===========================================================================
// Many agents registering and deregistering concurrently
// ===========================================================================

TEST(ConcurrentAgentsTest, ConcurrentRegistrationDeregistration) {
    Config cfg;
    cfg.thread_safe = true;
    ResourceManager mgr(cfg);
    mgr.start();

    mgr.register_resource(
        Resource(1, "SharedResource", ResourceCategory::ToolSlot, 100));

    constexpr int NUM_THREADS = 8;
    constexpr int AGENTS_PER_THREAD = 10;
    std::atomic<int> registered{0};
    std::atomic<int> deregistered{0};
    std::atomic<int> reg_errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < AGENTS_PER_THREAD; ++i) {
                try {
                    AgentId base_id = static_cast<AgentId>(
                        t * AGENTS_PER_THREAD + i + 1);
                    Agent a(base_id, "Agent-" + std::to_string(base_id));
                    a.declare_max_need(1, 2);
                    AgentId aid = mgr.register_agent(std::move(a));
                    registered.fetch_add(1);

                    // Immediately deregister
                    mgr.deregister_agent(aid);
                    deregistered.fetch_add(1);
                } catch (...) {
                    reg_errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    mgr.stop();

    EXPECT_EQ(reg_errors.load(), 0);
    EXPECT_EQ(registered.load(), NUM_THREADS * AGENTS_PER_THREAD);
    EXPECT_EQ(deregistered.load(), NUM_THREADS * AGENTS_PER_THREAD);
    EXPECT_EQ(mgr.agent_count(), 0);
}

// ===========================================================================
// Concurrent batch requests
// ===========================================================================

TEST(ConcurrentAgentsTest, ConcurrentBatchRequests) {
    constexpr int NUM_AGENTS = 5;

    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 5s;
    ResourceManager mgr(cfg);
    mgr.start();

    mgr.register_resource(
        Resource(1, "R1", ResourceCategory::ApiRateLimit, 6));
    mgr.register_resource(
        Resource(2, "R2", ResourceCategory::TokenBudget, 6));

    std::vector<AgentId> aids;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        Agent a(static_cast<AgentId>(i + 1),
                "BatchAgent-" + std::to_string(i + 1));
        a.declare_max_need(1, 2);
        a.declare_max_need(2, 2);
        AgentId aid = mgr.register_agent(std::move(a));
        aids.push_back(aid);
    }

    std::atomic<int> completed{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_AGENTS; ++i) {
        threads.emplace_back([&, i]() {
            std::unordered_map<ResourceTypeId, ResourceQuantity> batch;
            batch[1] = 1;
            batch[2] = 1;

            auto s = mgr.request_resources_batch(aids[i], batch, 5s);
            if (s == RequestStatus::Granted) {
                std::this_thread::sleep_for(5ms);
                mgr.release_all_resources(aids[i]);
                completed.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    mgr.stop();

    // All should eventually complete (resources are sufficient for serialized access)
    EXPECT_EQ(completed.load(), NUM_AGENTS)
        << "Not all batch requests completed: " << completed.load()
        << " of " << NUM_AGENTS;

    EXPECT_TRUE(mgr.is_safe());

    // Verify no resource leaks
    auto r1 = mgr.get_resource(1);
    auto r2 = mgr.get_resource(2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->allocated(), 0);
    EXPECT_EQ(r2->allocated(), 0);
}

// ===========================================================================
// Async requests with futures
// ===========================================================================

TEST(ConcurrentAgentsTest, AsyncRequests) {
    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 5s;
    ResourceManager mgr(cfg);
    mgr.start();

    mgr.register_resource(
        Resource(1, "AsyncResource", ResourceCategory::ToolSlot, 3));

    Agent a(1, "AsyncAgent");
    a.declare_max_need(1, 2);
    AgentId aid = mgr.register_agent(std::move(a));

    // Issue async request
    auto future = mgr.request_resources_async(aid, 1, 2, 3s);

    // Wait for result
    auto status = future.get();
    EXPECT_EQ(status, RequestStatus::Granted);

    // Release
    mgr.release_all_resources(aid, 1);

    mgr.stop();
    EXPECT_TRUE(mgr.is_safe());
}

// ===========================================================================
// High contention: many agents competing for scarce resources
// ===========================================================================

TEST(ConcurrentAgentsTest, HighContention_ScarceResources) {
    constexpr int NUM_AGENTS = 8;

    Config cfg;
    cfg.thread_safe = true;
    cfg.default_request_timeout = 5s;
    ResourceManager mgr(cfg);
    mgr.start();

    // Very scarce: only 2 units
    mgr.register_resource(
        Resource(1, "ScarceResource", ResourceCategory::GpuCompute, 2));

    std::vector<AgentId> aids;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        Agent a(static_cast<AgentId>(i + 1),
                "Competitor-" + std::to_string(i + 1));
        a.declare_max_need(1, 1);  // Each needs just 1
        AgentId aid = mgr.register_agent(std::move(a));
        aids.push_back(aid);
    }

    std::atomic<int> completed{0};
    std::atomic<int> failed{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_AGENTS; ++i) {
        threads.emplace_back([&, i]() {
            auto s = mgr.request_resources(aids[i], 1, 1, 5s);
            if (s == RequestStatus::Granted) {
                std::this_thread::sleep_for(10ms);
                mgr.release_resources(aids[i], 1, 1);
                completed.fetch_add(1);
            } else {
                failed.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    mgr.stop();

    // With capacity 2 and each needing 1, all 8 should eventually complete
    // (at most 2 at a time, but all should get through)
    EXPECT_EQ(completed.load(), NUM_AGENTS)
        << "Only " << completed.load() << " of " << NUM_AGENTS
        << " agents completed under high contention";

    EXPECT_TRUE(mgr.is_safe());

    auto res = mgr.get_resource(1);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->allocated(), 0);
}
