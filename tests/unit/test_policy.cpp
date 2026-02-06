#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <algorithm>
#include <thread>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Helper: create a ResourceRequest with specific timing
// ===========================================================================

static ResourceRequest make_request(RequestId id, AgentId agent,
                                     ResourceTypeId rt, ResourceQuantity qty,
                                     Priority prio, Timestamp submitted) {
    ResourceRequest req;
    req.id = id;
    req.agent_id = agent;
    req.resource_type = rt;
    req.quantity = qty;
    req.priority = prio;
    req.submitted_at = submitted;
    return req;
}

// Helper: create a minimal SystemSnapshot for policy tests
static SystemSnapshot make_snapshot() {
    SystemSnapshot snap;
    snap.is_safe = true;
    return snap;
}

// ===========================================================================
// FifoPolicy orders by submission time
// ===========================================================================

TEST(PolicyTest, FifoPolicyOrdersBySubmissionTime) {
    FifoPolicy policy;
    EXPECT_EQ(policy.name(), "FIFO");

    auto now = Clock::now();
    std::vector<ResourceRequest> requests = {
        make_request(1, 1, 1, 1, PRIORITY_CRITICAL, now + 30ms),  // Submitted last
        make_request(2, 2, 1, 1, PRIORITY_LOW,      now),          // Submitted first
        make_request(3, 3, 1, 1, PRIORITY_NORMAL,   now + 10ms),  // Submitted second
    };

    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);

    ASSERT_EQ(ordered.size(), 3);
    // Should be ordered by submission time: earliest first
    EXPECT_EQ(ordered[0].id, 2);  // now
    EXPECT_EQ(ordered[1].id, 3);  // now + 10ms
    EXPECT_EQ(ordered[2].id, 1);  // now + 30ms
}

TEST(PolicyTest, FifoPolicyEmptyInput) {
    FifoPolicy policy;
    std::vector<ResourceRequest> empty;
    auto snap = make_snapshot();
    auto ordered = policy.prioritize(empty, snap);
    EXPECT_TRUE(ordered.empty());
}

TEST(PolicyTest, FifoPolicySingleRequest) {
    FifoPolicy policy;
    auto now = Clock::now();
    std::vector<ResourceRequest> requests = {
        make_request(1, 1, 1, 1, PRIORITY_NORMAL, now),
    };
    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);
    ASSERT_EQ(ordered.size(), 1);
    EXPECT_EQ(ordered[0].id, 1);
}

// ===========================================================================
// PriorityPolicy orders by priority then FIFO
// ===========================================================================

TEST(PolicyTest, PriorityPolicyOrdersByPriorityDescending) {
    PriorityPolicy policy;
    EXPECT_EQ(policy.name(), "Priority");

    auto now = Clock::now();
    std::vector<ResourceRequest> requests = {
        make_request(1, 1, 1, 1, PRIORITY_LOW,      now),
        make_request(2, 2, 1, 1, PRIORITY_HIGH,     now + 10ms),
        make_request(3, 3, 1, 1, PRIORITY_CRITICAL, now + 20ms),
        make_request(4, 4, 1, 1, PRIORITY_NORMAL,   now + 5ms),
    };

    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);

    ASSERT_EQ(ordered.size(), 4);
    // Highest priority first
    EXPECT_EQ(ordered[0].id, 3);  // CRITICAL
    EXPECT_EQ(ordered[1].id, 2);  // HIGH
    EXPECT_EQ(ordered[2].id, 4);  // NORMAL
    EXPECT_EQ(ordered[3].id, 1);  // LOW
}

TEST(PolicyTest, PriorityPolicyFIFOWithinSamePriority) {
    PriorityPolicy policy;

    auto now = Clock::now();
    std::vector<ResourceRequest> requests = {
        make_request(1, 1, 1, 1, PRIORITY_HIGH, now + 20ms),  // Later
        make_request(2, 2, 1, 1, PRIORITY_HIGH, now),          // First
        make_request(3, 3, 1, 1, PRIORITY_HIGH, now + 10ms),  // Middle
    };

    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);

    ASSERT_EQ(ordered.size(), 3);
    // Same priority: FIFO order (earlier submission first)
    EXPECT_EQ(ordered[0].id, 2);
    EXPECT_EQ(ordered[1].id, 3);
    EXPECT_EQ(ordered[2].id, 1);
}

// ===========================================================================
// FairnessPolicy orders by wait time (longest waiting first)
// ===========================================================================

TEST(PolicyTest, FairnessPolicyOrdersByWaitTimeDescending) {
    FairnessPolicy policy;
    EXPECT_EQ(policy.name(), "Fairness");

    auto now = Clock::now();
    std::vector<ResourceRequest> requests = {
        make_request(1, 1, 1, 1, PRIORITY_CRITICAL, now),           // Waited 0ms (least)
        make_request(2, 2, 1, 1, PRIORITY_LOW,      now - 100ms),   // Waited 100ms (most)
        make_request(3, 3, 1, 1, PRIORITY_NORMAL,   now - 50ms),    // Waited 50ms (middle)
    };

    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);

    ASSERT_EQ(ordered.size(), 3);
    // Longest waiting first (earliest submission time first, same as FIFO
    // but specifically optimized for starvation prevention)
    EXPECT_EQ(ordered[0].id, 2);  // Submitted earliest, waited longest
    EXPECT_EQ(ordered[1].id, 3);  // Middle wait
    EXPECT_EQ(ordered[2].id, 1);  // Just submitted, least wait
}

TEST(PolicyTest, FairnessPolicyIgnoresPriority) {
    FairnessPolicy policy;

    auto now = Clock::now();
    std::vector<ResourceRequest> requests = {
        make_request(1, 1, 1, 1, PRIORITY_CRITICAL, now),          // High prio but just submitted
        make_request(2, 2, 1, 1, PRIORITY_LOW,      now - 200ms),  // Low prio but waited long
    };

    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);

    ASSERT_EQ(ordered.size(), 2);
    // The one that waited longest should come first, regardless of priority
    EXPECT_EQ(ordered[0].id, 2);
    EXPECT_EQ(ordered[1].id, 1);
}

// ===========================================================================
// ShortestNeedPolicy
// ===========================================================================

TEST(PolicyTest, ShortestNeedPolicyName) {
    ShortestNeedPolicy policy;
    EXPECT_EQ(policy.name(), "ShortestNeedFirst");
}

// ===========================================================================
// DeadlinePolicy
// ===========================================================================

TEST(PolicyTest, DeadlinePolicyName) {
    DeadlinePolicy policy;
    EXPECT_EQ(policy.name(), "DeadlineAware");
}

TEST(PolicyTest, DeadlinePolicyOrdersByTimeout) {
    DeadlinePolicy policy;

    auto now = Clock::now();
    // Request 1: submitted now, timeout 100ms -> deadline = now+100ms
    auto r1 = make_request(1, 1, 1, 1, PRIORITY_NORMAL, now);
    r1.timeout = 100ms;

    // Request 2: submitted now, timeout 50ms -> deadline = now+50ms (earlier!)
    auto r2 = make_request(2, 2, 1, 1, PRIORITY_NORMAL, now);
    r2.timeout = 50ms;

    // Request 3: submitted now, no timeout -> should go last
    auto r3 = make_request(3, 3, 1, 1, PRIORITY_NORMAL, now);

    std::vector<ResourceRequest> requests = {r1, r2, r3};
    auto snap = make_snapshot();
    auto ordered = policy.prioritize(requests, snap);

    ASSERT_EQ(ordered.size(), 3);
    // Earliest deadline first
    EXPECT_EQ(ordered[0].id, 2);  // 50ms deadline
    EXPECT_EQ(ordered[1].id, 1);  // 100ms deadline
    EXPECT_EQ(ordered[2].id, 3);  // No deadline (infinite)
}
