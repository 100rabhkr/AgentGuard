#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <cmath>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Fixture
// ===========================================================================

class DemandEstimatorTest : public ::testing::Test {
protected:
    AdaptiveConfig config;
    std::unique_ptr<DemandEstimator> estimator;

    void SetUp() override {
        config.history_window_size = 10;
        config.cold_start_headroom_factor = 2.0;
        config.cold_start_default_demand = 5;
        config.adaptive_headroom_factor = 1.5;
        config.default_confidence_level = 0.95;
        config.default_demand_mode = DemandMode::Static;

        estimator = std::make_unique<DemandEstimator>(config);
    }

    // Convenience identifiers
    static constexpr AgentId kAgent1 = 1;
    static constexpr AgentId kAgent2 = 2;
    static constexpr AgentId kAgent3 = 3;
    static constexpr ResourceTypeId kRes1 = 100;
    static constexpr ResourceTypeId kRes2 = 200;
};

// ===========================================================================
// UsageStats math tests
// ===========================================================================

TEST_F(DemandEstimatorTest, MeanOfEmptyStatsIsZero) {
    UsageStats stats;
    EXPECT_NEAR(stats.mean(), 0.0, 0.01);
}

TEST_F(DemandEstimatorTest, MeanOfSingleValue) {
    UsageStats stats;
    stats.count = 1;
    stats.sum = 7.0;
    stats.sum_sq = 49.0;
    EXPECT_NEAR(stats.mean(), 7.0, 0.01);
}

TEST_F(DemandEstimatorTest, MeanOfMultipleValues) {
    // Values: 2, 4, 4, 4, 5, 5, 7, 9  -> sum = 40, count = 8, mean = 5
    UsageStats stats;
    stats.count = 8;
    stats.sum = 40.0;
    stats.sum_sq = 2.0*2 + 4.0*4 + 4.0*4 + 4.0*4 + 5.0*5 + 5.0*5 + 7.0*7 + 9.0*9;
    // sum_sq = 4 + 16 + 16 + 16 + 25 + 25 + 49 + 81 = 232
    EXPECT_NEAR(stats.mean(), 5.0, 0.01);
}

TEST_F(DemandEstimatorTest, VarianceOfSingleValueIsZero) {
    UsageStats stats;
    stats.count = 1;
    stats.sum = 10.0;
    stats.sum_sq = 100.0;
    EXPECT_NEAR(stats.variance(), 0.0, 0.01);
}

TEST_F(DemandEstimatorTest, VarianceOfMultipleValues) {
    // Values: 2, 4, 4, 4, 5, 5, 7, 9
    // mean = 5, sample variance = sum((xi - mean)^2) / (n-1)
    //   = (9 + 1 + 1 + 1 + 0 + 0 + 4 + 16) / 7 = 32 / 7 ~= 4.571
    UsageStats stats;
    stats.count = 8;
    stats.sum = 40.0;
    stats.sum_sq = 232.0;
    EXPECT_NEAR(stats.variance(), 32.0 / 7.0, 0.01);
}

TEST_F(DemandEstimatorTest, StddevIsSqrtOfVariance) {
    UsageStats stats;
    stats.count = 8;
    stats.sum = 40.0;
    stats.sum_sq = 232.0;
    double expected_var = 32.0 / 7.0;
    EXPECT_NEAR(stats.stddev(), std::sqrt(expected_var), 0.01);
}

// ===========================================================================
// Cold start tests
// ===========================================================================

TEST_F(DemandEstimatorTest, ZeroObservationsReturnsColdStartDefault) {
    // No recordings for agent -- should return cold_start_default_demand
    auto result = estimator->estimate_max_need(kAgent1, kRes1, 0.95);
    EXPECT_EQ(result, config.cold_start_default_demand);
}

TEST_F(DemandEstimatorTest, SingleObservationReturnsValueTimesHeadroomFactor) {
    estimator->record_request(kAgent1, kRes1, 10);
    auto result = estimator->estimate_max_need(kAgent1, kRes1, 0.95);
    // single observation: max_single_request(10) * cold_start_headroom_factor(2.0) = 20
    EXPECT_EQ(result, 20);
}

TEST_F(DemandEstimatorTest, TwoObservationsReturnsStatisticalEstimate) {
    estimator->record_request(kAgent1, kRes1, 6);
    estimator->record_request(kAgent1, kRes1, 10);
    // count=2, mean=8, sample_var = ((6-8)^2+(10-8)^2)/(2-1) = 8, stddev=2.828
    // This should use the general estimate_impl path (mean + k * stddev)
    // rather than the cold_start path.
    auto result = estimator->estimate_max_need(kAgent1, kRes1, 0.95);
    // Floor is max_single_request = 10
    EXPECT_GE(result, 10);
    // Should be a reasonable finite value
    EXPECT_LT(result, 100);
}

// ===========================================================================
// Estimation tests
// ===========================================================================

TEST_F(DemandEstimatorTest, EstimateIncreasesWithConfidenceLevel) {
    for (int i = 0; i < 5; ++i) {
        estimator->record_request(kAgent1, kRes1, 3 + i);
    }
    auto low = estimator->estimate_max_need(kAgent1, kRes1, 0.5);
    auto high = estimator->estimate_max_need(kAgent1, kRes1, 0.99);
    EXPECT_GE(high, low);
}

TEST_F(DemandEstimatorTest, EstimateFloorIsMaxSingleRequest) {
    // Record a few small values then one large value
    estimator->record_request(kAgent1, kRes1, 1);
    estimator->record_request(kAgent1, kRes1, 1);
    estimator->record_request(kAgent1, kRes1, 1);
    estimator->record_request(kAgent1, kRes1, 20);

    // Even at low confidence, the estimate should not drop below
    // max_single_request = 20
    auto result = estimator->estimate_max_need(kAgent1, kRes1, 0.5);
    EXPECT_GE(result, 20);
}

TEST_F(DemandEstimatorTest, EstimateCapIsMaxCumulativeTimesHeadroom) {
    // Record several observations so we get a statistical estimate
    for (int i = 0; i < 5; ++i) {
        estimator->record_request(kAgent1, kRes1, 10);
    }
    // Set a low cumulative cap
    estimator->record_allocation_level(kAgent1, kRes1, 12);

    // Cap = max_cumulative(12) * adaptive_headroom_factor(1.5) = 18
    auto result = estimator->estimate_max_need(kAgent1, kRes1, 0.99);
    EXPECT_LE(result, 18);
}

TEST_F(DemandEstimatorTest, HighConfidenceLargerThanLowConfidence) {
    for (int i = 0; i < 10; ++i) {
        estimator->record_request(kAgent1, kRes1, 5 + (i % 4));
    }
    auto low_conf = estimator->estimate_max_need(kAgent1, kRes1, 0.5);
    auto high_conf = estimator->estimate_max_need(kAgent1, kRes1, 0.99);
    EXPECT_GE(high_conf, low_conf);
}

// ===========================================================================
// Rolling window tests
// ===========================================================================

TEST_F(DemandEstimatorTest, WindowRespectsHistoryWindowSize) {
    // Record exactly history_window_size entries
    for (std::size_t i = 0; i < config.history_window_size; ++i) {
        estimator->record_request(kAgent1, kRes1, static_cast<ResourceQuantity>(i + 1));
    }
    auto stats = estimator->get_stats(kAgent1, kRes1);
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->window.size(), config.history_window_size);
    EXPECT_EQ(stats->window_count, config.history_window_size);
}

TEST_F(DemandEstimatorTest, OldValuesAreOverwrittenInCircularBuffer) {
    // Fill the buffer
    for (std::size_t i = 0; i < config.history_window_size; ++i) {
        estimator->record_request(kAgent1, kRes1, 1);
    }
    // Overwrite with a new value
    estimator->record_request(kAgent1, kRes1, 999);

    auto stats = estimator->get_stats(kAgent1, kRes1);
    ASSERT_TRUE(stats.has_value());
    // window_count should be capped at window size
    EXPECT_EQ(stats->window_count, config.history_window_size);
    // The 999 should appear somewhere in the window
    bool found_999 = false;
    for (auto val : stats->window) {
        if (val == 999) {
            found_999 = true;
            break;
        }
    }
    EXPECT_TRUE(found_999);
}

// ===========================================================================
// Demand mode tests
// ===========================================================================

TEST_F(DemandEstimatorTest, DefaultModeIsStatic) {
    auto mode = estimator->get_agent_demand_mode(kAgent1);
    EXPECT_EQ(mode, DemandMode::Static);
}

TEST_F(DemandEstimatorTest, SetAndGetAgentDemandMode) {
    estimator->set_agent_demand_mode(kAgent1, DemandMode::Adaptive);
    EXPECT_EQ(estimator->get_agent_demand_mode(kAgent1), DemandMode::Adaptive);

    estimator->set_agent_demand_mode(kAgent1, DemandMode::Hybrid);
    EXPECT_EQ(estimator->get_agent_demand_mode(kAgent1), DemandMode::Hybrid);
}

TEST_F(DemandEstimatorTest, ModePersistsAcrossCalls) {
    estimator->set_agent_demand_mode(kAgent1, DemandMode::Adaptive);
    // Call other methods in between
    estimator->record_request(kAgent1, kRes1, 5);
    estimator->estimate_max_need(kAgent1, kRes1, 0.95);
    // Mode should still be Adaptive
    EXPECT_EQ(estimator->get_agent_demand_mode(kAgent1), DemandMode::Adaptive);
}

// ===========================================================================
// Other tests
// ===========================================================================

TEST_F(DemandEstimatorTest, ClearAgentRemovesAllStats) {
    estimator->record_request(kAgent1, kRes1, 10);
    estimator->record_request(kAgent1, kRes2, 20);
    estimator->set_agent_demand_mode(kAgent1, DemandMode::Adaptive);

    estimator->clear_agent(kAgent1);

    // Stats should be gone
    EXPECT_FALSE(estimator->get_stats(kAgent1, kRes1).has_value());
    EXPECT_FALSE(estimator->get_stats(kAgent1, kRes2).has_value());
    // Mode should revert to default
    EXPECT_EQ(estimator->get_agent_demand_mode(kAgent1), config.default_demand_mode);
    // estimate_max_need should return cold start default
    EXPECT_EQ(estimator->estimate_max_need(kAgent1, kRes1, 0.95),
              config.cold_start_default_demand);
}

TEST_F(DemandEstimatorTest, EstimateAllMaxNeedsReturnsAllAgents) {
    estimator->record_request(kAgent1, kRes1, 5);
    estimator->record_request(kAgent2, kRes1, 8);
    estimator->record_request(kAgent3, kRes2, 3);

    auto all = estimator->estimate_all_max_needs(0.95);

    EXPECT_EQ(all.size(), 3u);
    EXPECT_TRUE(all.count(kAgent1) > 0);
    EXPECT_TRUE(all.count(kAgent2) > 0);
    EXPECT_TRUE(all.count(kAgent3) > 0);
    EXPECT_TRUE(all[kAgent1].count(kRes1) > 0);
    EXPECT_TRUE(all[kAgent3].count(kRes2) > 0);
}

TEST_F(DemandEstimatorTest, GetStatsForUnknownAgentReturnsNullopt) {
    EXPECT_FALSE(estimator->get_stats(999, kRes1).has_value());
}

TEST_F(DemandEstimatorTest, RecordAllocationLevelUpdatesMaxCumulative) {
    estimator->record_request(kAgent1, kRes1, 3);
    estimator->record_allocation_level(kAgent1, kRes1, 10);
    estimator->record_allocation_level(kAgent1, kRes1, 7);  // lower -- should not overwrite

    auto stats = estimator->get_stats(kAgent1, kRes1);
    ASSERT_TRUE(stats.has_value());
    // High-water mark should be 10, not 7
    EXPECT_EQ(stats->max_cumulative, 10);
}
