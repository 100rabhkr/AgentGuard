#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <algorithm>
#include <unordered_set>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Fixture: helpers for building SafetyCheckInput scenarios
// ===========================================================================

class ProbabilisticSafetyTest : public ::testing::Test {
protected:
    SafetyChecker checker;

    // Convenience: single-resource scenario builder
    // agents: vector of {agent_id, {allocation, max_need}}
    SafetyCheckInput make_single_resource_input(
        ResourceTypeId rt,
        ResourceQuantity total,
        ResourceQuantity available,
        const std::vector<std::pair<AgentId, std::pair<ResourceQuantity, ResourceQuantity>>>& agents)
    {
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
// 1. Safe state returns safe probabilistic result
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, SafeStateReturnsSafeProbabilisticResult) {
    // Total=10, Available=3
    // A1: Alloc=2, Max=5 -> Need=3
    // A2: Alloc=5, Max=7 -> Need=2
    // Available(3) >= Need_A2(2) -> A2 finishes, reclaim -> avail=8
    // Then A1(3<=8) -> safe
    auto input = make_single_resource_input(1, 10, 3,
        {{1, {2, 5}}, {2, {5, 7}}});

    auto result = checker.check_safety_probabilistic(input, 0.95);
    EXPECT_TRUE(result.is_safe);
    EXPECT_FALSE(result.safe_sequence.empty());
}

// ===========================================================================
// 2. Unsafe state returns unsafe probabilistic result
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, UnsafeStateReturnsUnsafeProbabilisticResult) {
    // Total=10, Available=1
    // A1: Alloc=4, Max=8 -> Need=4
    // A2: Alloc=5, Max=9 -> Need=4
    // Available(1) < min(4,4) -> unsafe
    auto input = make_single_resource_input(1, 10, 1,
        {{1, {4, 8}}, {2, {5, 9}}});

    auto result = checker.check_safety_probabilistic(input, 0.90);
    EXPECT_FALSE(result.is_safe);
    EXPECT_TRUE(result.safe_sequence.empty());
    EXPECT_FALSE(result.reason.empty());
}

// ===========================================================================
// 3. Confidence level is recorded in result
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, ConfidenceLevelRecordedInResult) {
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {2, 4}}, {2, {3, 6}}});

    const double confidence = 0.975;
    auto result = checker.check_safety_probabilistic(input, confidence);
    EXPECT_DOUBLE_EQ(result.confidence_level, confidence);
}

// ===========================================================================
// 4. Safe sequence matches binary check
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, SafeSequenceMatchesBinaryCheck) {
    // Total=10, Available=3
    // A0: Alloc=3, Max=9 -> Need=6
    // A1: Alloc=2, Max=4 -> Need=2
    // A2: Alloc=2, Max=7 -> Need=5
    // Classic textbook: safe sequence exists
    auto input = make_single_resource_input(1, 10, 3,
        {{0, {3, 9}}, {1, {2, 4}}, {2, {2, 7}}});

    auto binary_result = checker.check_safety(input);
    auto prob_result = checker.check_safety_probabilistic(input, 0.95);

    ASSERT_TRUE(binary_result.is_safe);
    ASSERT_TRUE(prob_result.is_safe);

    // Both should produce the same safe sequence
    ASSERT_EQ(binary_result.safe_sequence.size(), prob_result.safe_sequence.size());
    for (std::size_t i = 0; i < binary_result.safe_sequence.size(); ++i) {
        EXPECT_EQ(binary_result.safe_sequence[i], prob_result.safe_sequence[i]);
    }
}

// ===========================================================================
// 5. estimated_max_needs populated correctly
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, EstimatedMaxNeedsPopulatedCorrectly) {
    // Build input with known max_need values and verify they appear in result
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {2, 6}}, {2, {3, 8}}});

    auto result = checker.check_safety_probabilistic(input, 0.90);

    // estimated_max_needs should mirror the input's max_need map
    ASSERT_EQ(result.estimated_max_needs.size(), 2);

    ASSERT_TRUE(result.estimated_max_needs.count(1));
    EXPECT_EQ(result.estimated_max_needs.at(1).at(1), 6);

    ASSERT_TRUE(result.estimated_max_needs.count(2));
    EXPECT_EQ(result.estimated_max_needs.at(2).at(1), 8);
}

// ===========================================================================
// 6. max_safe_confidence equals confidence when safe
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, MaxSafeConfidenceEqualsConfidenceWhenSafe) {
    // A clearly safe scenario: single agent with need=0
    auto input = make_single_resource_input(1, 10, 5,
        {{1, {5, 5}}});

    const double confidence = 0.99;
    auto result = checker.check_safety_probabilistic(input, confidence);

    ASSERT_TRUE(result.is_safe);
    EXPECT_DOUBLE_EQ(result.max_safe_confidence, confidence);
}

// ===========================================================================
// 7. max_safe_confidence is 0 when unsafe
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, MaxSafeConfidenceIsZeroWhenUnsafe) {
    // Unsafe: both agents need more than available
    auto input = make_single_resource_input(1, 10, 1,
        {{1, {4, 8}}, {2, {5, 9}}});

    auto result = checker.check_safety_probabilistic(input, 0.95);

    ASSERT_FALSE(result.is_safe);
    EXPECT_DOUBLE_EQ(result.max_safe_confidence, 0.0);
}

// ===========================================================================
// 8. Hypothetical probabilistic - safe grant
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, HypotheticalProbabilistic_SafeGrant) {
    // Total=10, Avail=4
    // A1: Alloc=3, Max=7 -> Need=4
    // A2: Alloc=3, Max=5 -> Need=2
    //
    // Grant 1 to A1: A1 alloc=4, avail=3, need_A1=3, need_A2=2
    // A2(2<=3)->reclaim->avail=6, A1(3<=6) -> safe
    auto input = make_single_resource_input(1, 10, 4,
        {{1, {3, 7}}, {2, {3, 5}}});

    const double confidence = 0.95;
    auto result = checker.check_hypothetical_probabilistic(input, 1, 1, 1, confidence);

    EXPECT_TRUE(result.is_safe);
    EXPECT_DOUBLE_EQ(result.confidence_level, confidence);
    EXPECT_DOUBLE_EQ(result.max_safe_confidence, confidence);
    EXPECT_FALSE(result.safe_sequence.empty());
}

// ===========================================================================
// 9. Hypothetical probabilistic - unsafe grant
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, HypotheticalProbabilistic_UnsafeGrant) {
    // Total=10, Avail=2
    // A1: Alloc=4, Max=8 -> Need=4
    // A2: Alloc=4, Max=8 -> Need=4
    //
    // Grant 2 to A1: A1 alloc=6, avail=0, need_A1=2, need_A2=4
    // Neither can proceed with 0 available -> unsafe
    auto input = make_single_resource_input(1, 10, 2,
        {{1, {4, 8}}, {2, {4, 8}}});

    const double confidence = 0.90;
    auto result = checker.check_hypothetical_probabilistic(input, 1, 1, 2, confidence);

    EXPECT_FALSE(result.is_safe);
    EXPECT_DOUBLE_EQ(result.confidence_level, confidence);
    EXPECT_DOUBLE_EQ(result.max_safe_confidence, 0.0);
    EXPECT_TRUE(result.safe_sequence.empty());

    // estimated_max_needs should reflect the hypothetical state's max_need
    // (the grant modifies allocation and available, not max_need)
    ASSERT_TRUE(result.estimated_max_needs.count(1));
    EXPECT_EQ(result.estimated_max_needs.at(1).at(1), 8);
    ASSERT_TRUE(result.estimated_max_needs.count(2));
    EXPECT_EQ(result.estimated_max_needs.at(2).at(1), 8);
}

// ===========================================================================
// 10. Multi-resource probabilistic check
// ===========================================================================

TEST_F(ProbabilisticSafetyTest, MultiResourceProbabilisticCheck) {
    // 2 resources, 2 agents
    // R1: total=6, avail=2;  R2: total=4, avail=1
    // A1: alloc={R1:2, R2:1}, max={R1:4, R2:2}  -> need={R1:2, R2:1}
    // A2: alloc={R1:2, R2:2}, max={R1:3, R2:3}  -> need={R1:1, R2:1}
    //
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

    const double confidence = 0.85;
    auto result = checker.check_safety_probabilistic(input, confidence);

    EXPECT_TRUE(result.is_safe);
    EXPECT_DOUBLE_EQ(result.confidence_level, confidence);
    EXPECT_DOUBLE_EQ(result.max_safe_confidence, confidence);
    EXPECT_EQ(result.safe_sequence.size(), 2);

    // Verify estimated_max_needs has both agents with both resources
    ASSERT_EQ(result.estimated_max_needs.size(), 2);

    EXPECT_EQ(result.estimated_max_needs.at(1).at(1), 4);
    EXPECT_EQ(result.estimated_max_needs.at(1).at(2), 2);
    EXPECT_EQ(result.estimated_max_needs.at(2).at(1), 3);
    EXPECT_EQ(result.estimated_max_needs.at(2).at(2), 3);
}
