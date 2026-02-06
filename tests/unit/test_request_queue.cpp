#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <thread>
#include <unordered_set>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Helper: create a ResourceRequest with specified fields
// ===========================================================================

static ResourceRequest make_request(AgentId agent, ResourceTypeId rt,
                                     ResourceQuantity qty, Priority prio,
                                     std::optional<Duration> timeout = std::nullopt) {
    ResourceRequest req;
    req.agent_id = agent;
    req.resource_type = rt;
    req.quantity = qty;
    req.priority = prio;
    req.timeout = timeout;
    req.submitted_at = Clock::now();
    return req;
}

// ===========================================================================
// Enqueue/dequeue ordering (priority + FIFO)
// ===========================================================================

TEST(RequestQueueTest, EnqueueAndDequeue_FIFO_SamePriority) {
    RequestQueue q;

    auto r1 = make_request(1, 1, 1, PRIORITY_NORMAL);
    auto r2 = make_request(2, 1, 1, PRIORITY_NORMAL);
    auto r3 = make_request(3, 1, 1, PRIORITY_NORMAL);

    // Small delay between submissions to ensure ordering
    RequestId id1 = q.enqueue(r1);
    RequestId id2 = q.enqueue(r2);
    RequestId id3 = q.enqueue(r3);

    EXPECT_EQ(q.size(), 3);

    // Same priority: FIFO order
    auto d1 = q.dequeue();
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(d1->id, id1);

    auto d2 = q.dequeue();
    ASSERT_TRUE(d2.has_value());
    EXPECT_EQ(d2->id, id2);

    auto d3 = q.dequeue();
    ASSERT_TRUE(d3.has_value());
    EXPECT_EQ(d3->id, id3);
}

TEST(RequestQueueTest, EnqueueAndDequeue_PriorityOrdering) {
    RequestQueue q;

    // Enqueue low, normal, high, critical - should dequeue critical first
    auto r_low = make_request(1, 1, 1, PRIORITY_LOW);
    auto r_normal = make_request(2, 1, 1, PRIORITY_NORMAL);
    auto r_high = make_request(3, 1, 1, PRIORITY_HIGH);
    auto r_critical = make_request(4, 1, 1, PRIORITY_CRITICAL);

    q.enqueue(r_low);
    q.enqueue(r_normal);
    q.enqueue(r_high);
    RequestId crit_id = q.enqueue(r_critical);

    // Critical should come out first
    auto first = q.dequeue();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->priority, PRIORITY_CRITICAL);
    EXPECT_EQ(first->id, crit_id);

    // Then high
    auto second = q.dequeue();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->priority, PRIORITY_HIGH);

    // Then normal
    auto third = q.dequeue();
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->priority, PRIORITY_NORMAL);

    // Then low
    auto fourth = q.dequeue();
    ASSERT_TRUE(fourth.has_value());
    EXPECT_EQ(fourth->priority, PRIORITY_LOW);
}

// ===========================================================================
// Peek does not remove
// ===========================================================================

TEST(RequestQueueTest, PeekDoesNotRemove) {
    RequestQueue q;
    q.enqueue(make_request(1, 1, 1, PRIORITY_HIGH));

    auto peeked = q.peek();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(q.size(), 1);  // Still there

    auto dequeued = q.dequeue();
    ASSERT_TRUE(dequeued.has_value());
    EXPECT_EQ(dequeued->id, peeked->id);
}

// ===========================================================================
// Cancel by ID
// ===========================================================================

TEST(RequestQueueTest, CancelById) {
    RequestQueue q;
    RequestId id1 = q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    RequestId id2 = q.enqueue(make_request(2, 1, 1, PRIORITY_NORMAL));
    RequestId id3 = q.enqueue(make_request(3, 1, 1, PRIORITY_NORMAL));

    EXPECT_TRUE(q.cancel(id2));
    EXPECT_EQ(q.size(), 2);

    // Verify id2 is gone
    auto d1 = q.dequeue();
    auto d2 = q.dequeue();
    ASSERT_TRUE(d1.has_value());
    ASSERT_TRUE(d2.has_value());
    EXPECT_NE(d1->id, id2);
    EXPECT_NE(d2->id, id2);

    // Ids 1 and 3 should be present
    std::unordered_set<RequestId> remaining{d1->id, d2->id};
    EXPECT_TRUE(remaining.count(id1));
    EXPECT_TRUE(remaining.count(id3));
}

TEST(RequestQueueTest, CancelNonexistentReturnsFalse) {
    RequestQueue q;
    EXPECT_FALSE(q.cancel(12345));
}

// ===========================================================================
// Cancel all for agent
// ===========================================================================

TEST(RequestQueueTest, CancelAllForAgent) {
    RequestQueue q;
    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(1, 2, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(2, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(1, 3, 1, PRIORITY_HIGH));

    // Cancel all for agent 1 (3 requests)
    std::size_t removed = q.cancel_all_for_agent(1);
    EXPECT_EQ(removed, 3);
    EXPECT_EQ(q.size(), 1);

    // Remaining should be agent 2's request
    auto remaining = q.dequeue();
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(remaining->agent_id, 2);
}

TEST(RequestQueueTest, CancelAllForNonexistentAgentReturnsZero) {
    RequestQueue q;
    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    EXPECT_EQ(q.cancel_all_for_agent(999), 0);
}

// ===========================================================================
// Timeout expiration
// ===========================================================================

TEST(RequestQueueTest, ExpireTimedOut) {
    RequestQueue q;

    // Request with very short timeout
    auto r1 = make_request(1, 1, 1, PRIORITY_NORMAL, 1ms);
    r1.submitted_at = Clock::now() - 100ms;  // Submitted 100ms ago
    q.enqueue(r1);

    // Request with no timeout
    auto r2 = make_request(2, 1, 1, PRIORITY_NORMAL);
    q.enqueue(r2);

    // Request with long timeout
    auto r3 = make_request(3, 1, 1, PRIORITY_NORMAL, 10s);
    r3.submitted_at = Clock::now();
    q.enqueue(r3);

    // Wait briefly to ensure the short timeout has passed
    std::this_thread::sleep_for(5ms);

    auto expired = q.expire_timed_out();
    EXPECT_EQ(expired.size(), 1);
    EXPECT_EQ(q.size(), 2);
}

TEST(RequestQueueTest, NoExpiredWhenNoTimeouts) {
    RequestQueue q;
    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(2, 1, 1, PRIORITY_NORMAL));

    auto expired = q.expire_timed_out();
    EXPECT_TRUE(expired.empty());
}

// ===========================================================================
// Empty queue returns nullopt
// ===========================================================================

TEST(RequestQueueTest, DequeueEmptyReturnsNullopt) {
    RequestQueue q;
    EXPECT_FALSE(q.dequeue().has_value());
}

TEST(RequestQueueTest, PeekEmptyReturnsNullopt) {
    RequestQueue q;
    EXPECT_FALSE(q.peek().has_value());
}

// ===========================================================================
// Size, empty, full
// ===========================================================================

TEST(RequestQueueTest, EmptyAndSize) {
    RequestQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);

    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1);
}

TEST(RequestQueueTest, Full) {
    RequestQueue q(3);  // max size = 3
    EXPECT_EQ(q.max_size(), 3);

    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(2, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(3, 1, 1, PRIORITY_NORMAL));

    EXPECT_TRUE(q.full());

    // Next enqueue should throw QueueFullException
    EXPECT_THROW(q.enqueue(make_request(4, 1, 1, PRIORITY_NORMAL)),
                 QueueFullException);
}

// ===========================================================================
// get_all_pending and get_pending_for_resource
// ===========================================================================

TEST(RequestQueueTest, GetAllPending) {
    RequestQueue q;
    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(2, 2, 1, PRIORITY_HIGH));
    q.enqueue(make_request(3, 1, 1, PRIORITY_LOW));

    auto all = q.get_all_pending();
    EXPECT_EQ(all.size(), 3);
}

TEST(RequestQueueTest, GetPendingForResource) {
    RequestQueue q;
    q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
    q.enqueue(make_request(2, 2, 1, PRIORITY_HIGH));
    q.enqueue(make_request(3, 1, 1, PRIORITY_LOW));
    q.enqueue(make_request(4, 3, 1, PRIORITY_NORMAL));

    auto forR1 = q.get_pending_for_resource(1);
    EXPECT_EQ(forR1.size(), 2);

    auto forR2 = q.get_pending_for_resource(2);
    EXPECT_EQ(forR2.size(), 1);

    auto forR99 = q.get_pending_for_resource(99);
    EXPECT_TRUE(forR99.empty());
}

// ===========================================================================
// wait_and_dequeue with timeout
// ===========================================================================

TEST(RequestQueueTest, WaitAndDequeueTimesOut) {
    RequestQueue q;

    auto start = Clock::now();
    auto result = q.wait_and_dequeue(50ms);
    auto elapsed = Clock::now() - start;

    EXPECT_FALSE(result.has_value());
    // Should have waited approximately 50ms
    EXPECT_GE(elapsed, 40ms);
}

TEST(RequestQueueTest, WaitAndDequeueSucceeds) {
    RequestQueue q;

    // Enqueue from another thread after a small delay
    std::thread producer([&q]() {
        std::this_thread::sleep_for(20ms);
        q.enqueue(make_request(1, 1, 1, PRIORITY_NORMAL));
        q.notify();
    });

    auto result = q.wait_and_dequeue(500ms);
    EXPECT_TRUE(result.has_value());

    producer.join();
}
