#include <gtest/gtest.h>
#include "schedulers.h"
#include <memory>

using namespace nr5g;

// ── Helper ────────────────────────────────────────────────────────────────────

static std::vector<std::shared_ptr<UE>> makeUEs(
    std::vector<std::pair<uint16_t,uint8_t>> rnti_cqi)
{
    std::vector<std::shared_ptr<UE>> ues;
    for (auto [rnti, cqi] : rnti_cqi) {
        auto ue = std::make_shared<UE>(rnti, cqi);
        ue->buffer_status = 100000; // 100 KB — always has data
        ues.push_back(ue);
    }
    return ues;
}

// ── CQI/MCS/TBS ──────────────────────────────────────────────────────────────

TEST(CqiMcs, BoundaryValues) {
    EXPECT_EQ(cqiToMcs(0),  0);
    EXPECT_EQ(cqiToMcs(15), 28);
    EXPECT_EQ(cqiToMcs(16), 28); // clamp
}

TEST(McsTbs, IncreaseWithMcs) {
    for (uint8_t mcs = 0; mcs < 28; ++mcs)
        EXPECT_LT(mcsToTBS(mcs, 52, 1), mcsToTBS(mcs + 1, 52, 1));
}

TEST(McsTbs, IncreaseWithRBs) {
    EXPECT_LT(mcsToTBS(14, 26, 1), mcsToTBS(14, 52, 1));
}

// ── UE ───────────────────────────────────────────────────────────────────────

TEST(UE, InvalidNumerologyThrows) {
    EXPECT_THROW(UE(0x1001, 10, 4), std::invalid_argument);
}

TEST(UE, PFMetricZeroWhenNoRate) {
    UE ue(0x1001, 8);
    ue.instantaneous_rate = 0;
    ue.avg_throughput     = 1e6;
    EXPECT_DOUBLE_EQ(ue.getPFMetric(), 0.0);
}

// ── Round Robin ───────────────────────────────────────────────────────────────

TEST(RoundRobin, AllRBsAssigned) {
    RoundRobinScheduler sched(52);
    auto ues = makeUEs({{0x1001, 8}, {0x1002, 12}});
    auto alloc = sched.schedule(0, ues, 52);
    for (auto& rb : alloc.dl_rbs)
        EXPECT_TRUE(rb.isAssigned());
}

TEST(RoundRobin, EmptyWhenNoActiveUEs) {
    RoundRobinScheduler sched(52);
    std::vector<std::shared_ptr<UE>> ues;
    auto alloc = sched.schedule(0, ues, 52);
    for (auto& rb : alloc.dl_rbs)
        EXPECT_FALSE(rb.isAssigned());
}

// ── Proportional Fair ────────────────────────────────────────────────────────

TEST(ProportionalFair, AllRBsAssigned) {
    ProportionalFairScheduler sched(52);
    auto ues = makeUEs({{0x1001, 5}, {0x1002, 10}, {0x1003, 15}});
    auto alloc = sched.schedule(0, ues, 52);
    for (auto& rb : alloc.dl_rbs)
        EXPECT_TRUE(rb.isAssigned());
}

TEST(ProportionalFair, FairnessOverSlots) {
    // All UEs have same CQI — PF should distribute evenly
    ProportionalFairScheduler sched(52);
    auto ues = makeUEs({{0x1001, 8}, {0x1002, 8}, {0x1003, 8}, {0x1004, 8}});

    std::map<uint16_t, uint32_t> rb_counts;
    for (uint32_t s = 0; s < 100; ++s) {
        auto alloc = sched.schedule(s, ues, 52);
        for (auto& rb : alloc.dl_rbs)
            if (rb.isAssigned()) rb_counts[rb.assigned_rnti]++;
    }

    // Each UE should get roughly 25% of RBs — allow 10% tolerance
    for (auto& [rnti, count] : rb_counts)
        EXPECT_NEAR(count, 1300, 200); // 5200 total RBs / 4 UEs = 1300
}

// ── Max Throughput ────────────────────────────────────────────────────────────

TEST(MaxThroughput, AssignsToHighestCQI) {
    MaxThroughputScheduler sched(52);
    auto ues = makeUEs({{0x1001, 3}, {0x1002, 15}, {0x1003, 7}});
    auto alloc = sched.schedule(0, ues, 52);
    for (auto& rb : alloc.dl_rbs)
        EXPECT_EQ(rb.assigned_rnti, 0x1002);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
