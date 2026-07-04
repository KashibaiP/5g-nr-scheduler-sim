#pragma once
#include "scheduler.h"

namespace nr5g {

class RoundRobinScheduler : public IScheduler {
public:
    explicit RoundRobinScheduler(uint16_t num_rbs = 52); // 10 MHz BW = 52 RBs
    SlotAllocation schedule(uint32_t slot, std::vector<std::shared_ptr<UE>>& ues, uint16_t num_rbs) override;
    SchedulerStats getStats() const override;
    std::string getName() const override { return "Round Robin"; }

private:
    uint16_t       m_num_rbs;
    uint32_t       m_last_ue_index;
    SchedulerStats m_stats;
};

// 3GPP-compliant Proportional Fair - maximises sum(log(throughput))
class ProportionalFairScheduler : public IScheduler {
public:
    explicit ProportionalFairScheduler(uint16_t num_rbs = 52, double alpha = 0.8);
    SlotAllocation schedule(uint32_t slot, std::vector<std::shared_ptr<UE>>& ues, uint16_t num_rbs) override;
    SchedulerStats getStats() const override;
    std::string getName() const override { return "Proportional Fair"; }

private:
    uint16_t       m_num_rbs;
    double         m_alpha;     // EMA smoothing factor (3GPP TR 36.814)
    SchedulerStats m_stats;

    void updateThroughputEstimate(UE& ue, uint32_t allocated_bytes);
};

class MaxThroughputScheduler : public IScheduler {
public:
    explicit MaxThroughputScheduler(uint16_t num_rbs = 52);
    SlotAllocation schedule(uint32_t slot, std::vector<std::shared_ptr<UE>>& ues, uint16_t num_rbs) override;
    SchedulerStats getStats() const override;
    std::string getName() const override { return "Max Throughput"; }

private:
    uint16_t       m_num_rbs;
    SchedulerStats m_stats;
};

} // namespace nr5g
