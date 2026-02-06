#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <algorithm>
#include <unordered_set>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Helper: build a SafetyCheckInput from a simple description
// ===========================================================================

class SafetyCheckerTest : public ::testing::Test {
protected:
    SafetyChecker checker;

    // Convenience: single-resource scenario builder
    SafetyCheckInput make_single_resource_input(
        ResourceTypeId rt,
        ResourceQuantity total,
        ResourceQuantity available,
        const std::vector<std::pair<AgentId, std::pair<ResourceQuantity, ResourceQuantity>>>& agents)
    {
        // agents: vector of {agent_id, {allocation, max_need}}
        SafetyCheckInput input;
        input.total[rt] = total;
        input.available[rt] = available;
        for (const auto& [aid, alloc_max] : agents) {
            input.allocation[aid][rt] = alloc_max.first;
            input.max_need[aid][rt] = alloc_max.second;
        }
        return input;
    }
};

// ===========================================================================
// Safe state with simple 2-agent 1-resource scenario
// ===========================================================================

TEST_F(SafetyCheckerTest, SimpleSafeState_TwoAgentsOneResource) {
    // Total = 10, Available = 3
    // Agent 1: Alloc=2, Max=5  -> Need=3
    // Agent 2: Alloc=5, Max=7  -> Need=2
    // Available(3) >= Need_A2(2), so grant A2 first, reclaim 5+2=7, total avail=10
    // Then grant A1: Need_A1(3) <= 10-2=8, done.
    auto input = make_single_resource_input(1, 10, 3,
        {{1, {2, 5}}, {2, {5, 7}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    EXPECT_EQ(result.safe_sequence.size(), 2);
}

// ===========================================================================
// Unsafe state detection
// ===========================================================================

TEST_F(SafetyCheckerTest, UnsafeState_TwoAgentsOneResource) {
    // Total = 10, Available = 1
    // Agent 1: Alloc=4, Max=8  -> Need=4
    // Agent 2: Alloc=5, Max=9  -> Need=4
    // Available(1) < min(Need_A1, Need_A2) = 4 -> unsafe
    auto input = make_single_resource_input(1, 10, 1,
        {{1, {4, 8}}, {2, {5, 9}}});

    auto result = checker.check_safety(input);
    EXPECT_FALSE(result.is_safe);
    EXPECT_TRUE(result.safe_sequence.empty());
    EXPECT_FALSE(result.reason.empty());
}

// ===========================================================================
// Empty system (no agents) is safe
// ===========================================================================

TEST_F(SafetyCheckerTest, EmptySystemIsSafe) {
    SafetyCheckInput input;
    input.total[1] = 10;
    input.available[1] = 10;
    // No agents

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    EXPECT_TRUE(result.safe_sequence.empty());
}

// ===========================================================================
// Single agent is always safe when resources suffice
// ===========================================================================

TEST_F(SafetyCheckerTest, SingleAgentSafeWhenResourcesSuffice) {
    // Agent needs 5 more, available is 5 -> safe
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {5, 10}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    EXPECT_EQ(result.safe_sequence.size(), 1);
    EXPECT_EQ(result.safe_sequence[0], 1);
}

TEST_F(SafetyCheckerTest, SingleAgentSafeAvailableExceedsNeed) {
    auto input = make_single_resource_input(1, 100, 90,
        {{1, {10, 50}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
}

// ===========================================================================
// Classic Banker's Algorithm textbook example (3 agents, 1 resource, total=10)
// ===========================================================================

TEST_F(SafetyCheckerTest, ClassicTextbookExample_ThreeAgents) {
    // Classic example:
    // Total = 10
    // Agent 0: Alloc=3, Max=9  -> Need=6
    // Agent 1: Alloc=2, Max=4  -> Need=2
    // Agent 2: Alloc=2, Max=7  -> Need=5
    // Available = 10 - 3 - 2 - 2 = 3
    //
    // Safe sequence: A1 (need=2 <= 3), reclaim -> avail=5
    //                A2 (need=5 <= 5), reclaim -> avail=7
    //                A0 (need=6 <= 7), reclaim -> avail=10
    auto input = make_single_resource_input(1, 10, 3,
        {{0, {3, 9}}, {1, {2, 4}}, {2, {2, 7}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    ASSERT_EQ(result.safe_sequence.size(), 3);

    // Verify it is a valid safe sequence: all agents present
    std::unordered_set<AgentId> ids(result.safe_sequence.begin(),
                                     result.safe_sequence.end());
    EXPECT_EQ(ids.size(), 3);
    EXPECT_TRUE(ids.count(0));
    EXPECT_TRUE(ids.count(1));
    EXPECT_TRUE(ids.count(2));
}

TEST_F(SafetyCheckerTest, ClassicTextbookExample_UnsafeVariant) {
    // Same as above but Agent 0 has allocated 1 more (total avail = 2)
    // Agent 0: Alloc=4, Max=9  -> Need=5
    // Agent 1: Alloc=2, Max=4  -> Need=2
    // Agent 2: Alloc=2, Max=7  -> Need=5
    // Available = 10 - 4 - 2 - 2 = 2
    //
    // A1 (need=2 <= 2) -> reclaim -> avail=4
    // A0 (need=5 > 4), A2 (need=5 > 4) -> stuck -> UNSAFE
    auto input = make_single_resource_input(1, 10, 2,
        {{0, {4, 9}}, {1, {2, 4}}, {2, {2, 7}}});

    auto result = checker.check_safety(input);
    EXPECT_FALSE(result.is_safe);
}

// ===========================================================================
// Multi-resource scenario
// ===========================================================================

TEST_F(SafetyCheckerTest, MultiResource_Safe) {
    // 2 resources, 2 agents
    // R1: total=6, avail=2;  R2: total=4, avail=1
    // A1: alloc={R1:2, R2:1}, max={R1:4, R2:2}  -> need={R1:2, R2:1}
    // A2: alloc={R1:2, R2:2}, max={R1:3, R2:3}  -> need={R1:1, R2:1}
    // Avail: {R1:2, R2:1}
    // A2 can run: need R1=1<=2, R2=1<=1 -> reclaim -> avail={R1:4, R2:3}
    // A1 can run: need R1=2<=4, R2=1<=3 -> safe
    SafetyCheckInput input;
    input.total[1] = 6;
    input.total[2] = 4;
    input.available[1] = 2;
    input.available[2] = 1;

    input.allocation[1][1] = 2; input.allocation[1][2] = 1;
    input.allocation[2][1] = 2; input.allocation[2][2] = 2;

    input.max_need[1][1] = 4; input.max_need[1][2] = 2;
    input.max_need[2][1] = 3; input.max_need[2][2] = 3;

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    EXPECT_EQ(result.safe_sequence.size(), 2);
}

TEST_F(SafetyCheckerTest, MultiResource_Unsafe) {
    // 2 resources, 2 agents
    // R1: total=4, avail=0;  R2: total=4, avail=0
    // A1: alloc={R1:2, R2:2}, max={R1:3, R2:3}  -> need={R1:1, R2:1}
    // A2: alloc={R1:2, R2:2}, max={R1:3, R2:3}  -> need={R1:1, R2:1}
    // Avail: {R1:0, R2:0} -> neither can proceed -> unsafe
    SafetyCheckInput input;
    input.total[1] = 4;
    input.total[2] = 4;
    input.available[1] = 0;
    input.available[2] = 0;

    input.allocation[1][1] = 2; input.allocation[1][2] = 2;
    input.allocation[2][1] = 2; input.allocation[2][2] = 2;

    input.max_need[1][1] = 3; input.max_need[1][2] = 3;
    input.max_need[2][1] = 3; input.max_need[2][2] = 3;

    auto result = checker.check_safety(input);
    EXPECT_FALSE(result.is_safe);
}

// ===========================================================================
// check_hypothetical: granting a request keeps state safe
// ===========================================================================

TEST_F(SafetyCheckerTest, HypotheticalSafe) {
    // Current state: safe, and granting 1 more unit to Agent 1 stays safe
    // Total=10, Avail=4
    // A1: Alloc=3, Max=7 -> Need=4
    // A2: Alloc=3, Max=5 -> Need=2
    //
    // If grant 1 to A1: A1 alloc=4, avail=3, need_A1=3, need_A2=2
    // A2(2<=3)-> reclaim-> avail=6, A1(3<=6) -> safe
    auto input = make_single_resource_input(1, 10, 4,
        {{1, {3, 7}}, {2, {3, 5}}});

    auto result = checker.check_hypothetical(input, 1, 1, 1);
    EXPECT_TRUE(result.is_safe);
}

// ===========================================================================
// check_hypothetical: granting a request creates unsafe state
// ===========================================================================

TEST_F(SafetyCheckerTest, HypotheticalUnsafe) {
    // Total=10, Avail=2
    // A1: Alloc=4, Max=8 -> Need=4
    // A2: Alloc=4, Max=8 -> Need=4
    //
    // If grant 2 to A1: A1 alloc=6, avail=0, need_A1=2, need_A2=4
    // Neither can proceed with 0 available -> unsafe
    auto input = make_single_resource_input(1, 10, 2,
        {{1, {4, 8}}, {2, {4, 8}}});

    auto result = checker.check_hypothetical(input, 1, 1, 2);
    EXPECT_FALSE(result.is_safe);
}

TEST_F(SafetyCheckerTest, HypotheticalGrantOneUnitStillSafe) {
    // Same scenario as above but grant only 1
    // A1 alloc=5, avail=1, need_A1=3, need_A2=4
    // Neither need <= 1 -> unsafe
    auto input = make_single_resource_input(1, 10, 2,
        {{1, {4, 8}}, {2, {4, 8}}});

    auto result = checker.check_hypothetical(input, 1, 1, 1);
    EXPECT_FALSE(result.is_safe);
}

// ===========================================================================
// find_grantable_requests returns correct subset
// ===========================================================================

TEST_F(SafetyCheckerTest, FindGrantableRequests) {
    // Total=10, Avail=3
    // A1: Alloc=3, Max=6 -> Need=3
    // A2: Alloc=4, Max=7 -> Need=3
    //
    // Candidate requests:
    // R1: A1 wants 1 unit -> if granted, avail=2, need_A1=2, need_A2=3
    //     A1(2<=2)->reclaim->avail=6, A2(3<=6)->safe => GRANTABLE
    // R2: A2 wants 3 units -> if granted, avail=0, need_A1=3, need_A2=0
    //     A2 done immediately (need=0)->reclaim->avail=4, A1(3<=4)->safe => GRANTABLE
    // R3: A1 wants 3 units -> if granted, avail=0, need_A1=0, need_A2=3
    //     A1 done (need=0)->reclaim->avail=6, A2(3<=6)->safe => GRANTABLE
    auto input = make_single_resource_input(1, 10, 3,
        {{1, {3, 6}}, {2, {4, 7}}});

    std::vector<ResourceRequest> candidates;
    {
        ResourceRequest req;
        req.id = 101;
        req.agent_id = 1;
        req.resource_type = 1;
        req.quantity = 1;
        candidates.push_back(req);
    }
    {
        ResourceRequest req;
        req.id = 102;
        req.agent_id = 2;
        req.resource_type = 1;
        req.quantity = 3;
        candidates.push_back(req);
    }
    {
        ResourceRequest req;
        req.id = 103;
        req.agent_id = 1;
        req.resource_type = 1;
        req.quantity = 3;
        candidates.push_back(req);
    }

    auto grantable = checker.find_grantable_requests(input, candidates);

    // All three should be grantable (individually)
    EXPECT_EQ(grantable.size(), 3);
    std::unordered_set<RequestId> granted_ids(grantable.begin(), grantable.end());
    EXPECT_TRUE(granted_ids.count(101));
    EXPECT_TRUE(granted_ids.count(102));
    EXPECT_TRUE(granted_ids.count(103));
}

TEST_F(SafetyCheckerTest, FindGrantableRequests_SomeUnsafe) {
    // Total=10, Avail=2
    // A1: Alloc=4, Max=8 -> Need=4
    // A2: Alloc=4, Max=8 -> Need=4
    //
    // Request R1: A1 wants 1 => avail=1, need_A1=3, need_A2=4 -> neither fits -> UNSAFE
    // Request R2: A2 wants 1 => avail=1, need_A1=4, need_A2=3 -> neither fits -> UNSAFE
    auto input = make_single_resource_input(1, 10, 2,
        {{1, {4, 8}}, {2, {4, 8}}});

    std::vector<ResourceRequest> candidates;
    {
        ResourceRequest req;
        req.id = 201;
        req.agent_id = 1;
        req.resource_type = 1;
        req.quantity = 1;
        candidates.push_back(req);
    }
    {
        ResourceRequest req;
        req.id = 202;
        req.agent_id = 2;
        req.resource_type = 1;
        req.quantity = 1;
        candidates.push_back(req);
    }

    auto grantable = checker.find_grantable_requests(input, candidates);
    EXPECT_TRUE(grantable.empty());
}

// ===========================================================================
// identify_bottleneck_agents ordering
// ===========================================================================

TEST_F(SafetyCheckerTest, IdentifyBottleneckAgents) {
    // Total=10, Avail=3
    // A1: Alloc=3, Max=5 -> Need=2 (least bottleneck)
    // A2: Alloc=2, Max=9 -> Need=7 (biggest bottleneck: needs most relative to avail)
    // A3: Alloc=2, Max=6 -> Need=4 (medium)
    auto input = make_single_resource_input(1, 10, 3,
        {{1, {3, 5}}, {2, {2, 9}}, {3, {2, 6}}});

    auto bottlenecks = checker.identify_bottleneck_agents(input);
    ASSERT_FALSE(bottlenecks.empty());

    // Agent 2 should be the most constrained (need=7 vs avail=3),
    // so it should appear first or at least be present
    EXPECT_EQ(bottlenecks[0], 2);

    // All agents should be listed
    EXPECT_EQ(bottlenecks.size(), 3);
}

TEST_F(SafetyCheckerTest, IdentifyBottleneckAgents_EmptySystem) {
    SafetyCheckInput input;
    input.total[1] = 10;
    input.available[1] = 10;

    auto bottlenecks = checker.identify_bottleneck_agents(input);
    EXPECT_TRUE(bottlenecks.empty());
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_F(SafetyCheckerTest, AgentWithZeroNeedIsSafe) {
    // Agent that already has everything it needs
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {5, 5}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
}

TEST_F(SafetyCheckerTest, AllResourcesAllocatedButAllAgentsSatisfied) {
    // Total=6, Avail=0
    // A1: Alloc=3, Max=3 -> Need=0
    // A2: Alloc=3, Max=3 -> Need=0
    // Both are done, so state is safe
    auto input = make_single_resource_input(1, 6, 0,
        {{1, {3, 3}}, {2, {3, 3}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    EXPECT_EQ(result.safe_sequence.size(), 2);
}

TEST_F(SafetyCheckerTest, ManyAgentsSafe) {
    // 5 agents, each needing 1 out of total 5 available
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {1, 2}}, {2, {1, 2}}, {3, {1, 2}}, {4, {1, 2}}, {5, {1, 2}}});

    auto result = checker.check_safety(input);
    EXPECT_TRUE(result.is_safe);
    EXPECT_EQ(result.safe_sequence.size(), 5);
}

// ===========================================================================
// check_hypothetical_batch
// ===========================================================================

TEST_F(SafetyCheckerTest, HypotheticalBatchSafe) {
    // Total=10, Avail=5
    // A1: Alloc=2, Max=4 -> Need=2
    // A2: Alloc=3, Max=6 -> Need=3
    //
    // Batch: A1 gets 1, A2 gets 1 -> avail=3, need_A1=1, need_A2=2
    // A1(1<=3)->reclaim->avail=6, A2(2<=6)->safe
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {2, 4}}, {2, {3, 6}}});

    std::vector<ResourceRequest> batch;
    {
        ResourceRequest r;
        r.id = 1;
        r.agent_id = 1;
        r.resource_type = 1;
        r.quantity = 1;
        batch.push_back(r);
    }
    {
        ResourceRequest r;
        r.id = 2;
        r.agent_id = 2;
        r.resource_type = 1;
        r.quantity = 1;
        batch.push_back(r);
    }

    auto result = checker.check_hypothetical_batch(input, batch);
    EXPECT_TRUE(result.is_safe);
}

TEST_F(SafetyCheckerTest, HypotheticalBatchUnsafe) {
    // Total=10, Avail=2
    // A1: Alloc=4, Max=8 -> Need=4
    // A2: Alloc=4, Max=8 -> Need=4
    //
    // Batch: A1 gets 1, A2 gets 1 -> avail=0, need_A1=3, need_A2=3
    // Neither can proceed -> unsafe
    auto input = make_single_resource_input(1, 10, 2,
        {{1, {4, 8}}, {2, {4, 8}}});

    std::vector<ResourceRequest> batch;
    {
        ResourceRequest r;
        r.id = 1;
        r.agent_id = 1;
        r.resource_type = 1;
        r.quantity = 1;
        batch.push_back(r);
    }
    {
        ResourceRequest r;
        r.id = 2;
        r.agent_id = 2;
        r.resource_type = 1;
        r.quantity = 1;
        batch.push_back(r);
    }

    auto result = checker.check_hypothetical_batch(input, batch);
    EXPECT_FALSE(result.is_safe);
}
