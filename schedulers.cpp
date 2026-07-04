#include "schedulers.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace nr5g {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static SlotAllocation makeEmptySlot(uint32_t slot, uint8_t numerology, uint16_t num_rbs) {
    SlotAllocation alloc;
    alloc.slot_number  = slot;
    alloc.numerology   = numerology;
    alloc.timestamp_us = slot * (1000u >> numerology); // slot duration = 1ms / 2^mu
    alloc.dl_rbs.reserve(num_rbs);
    for (uint16_t i = 0; i < num_rbs; ++i)
        alloc.dl_rbs.emplace_back(i);
    return alloc;
}

static void assignRB(ResourceBlock& rb, const UE& ue) {
    rb.assigned_rnti        = ue.rnti;
    rb.mcs                  = cqiToMcs(ue.cqi);
    rb.transport_block_size = mcsToTBS(rb.mcs, 1, ue.numerology);
}

// ─── Round Robin ─────────────────────────────────────────────────────────────

RoundRobinScheduler::RoundRobinScheduler(uint16_t num_rbs)
    : m_num_rbs(num_rbs), m_last_ue_index(0), m_stats{}
{}

SlotAllocation RoundRobinScheduler::schedule(
    uint32_t slot,
    std::vector<std::shared_ptr<UE>>& ues,
    uint16_t num_rbs)
{
    auto alloc = makeEmptySlot(slot, 1, num_rbs);

    std::vector<std::shared_ptr<UE>> active;
    for (auto& ue : ues)
        if (ue->is_active && ue->buffer_status > 0)
            active.push_back(ue);

    if (active.empty()) return alloc;

    // Distribute RBs in round-robin order starting from last served UE
    uint32_t ue_count = static_cast<uint32_t>(active.size());
    for (uint16_t rb = 0; rb < num_rbs; ++rb) {
        uint32_t ue_idx = (m_last_ue_index + rb) % ue_count;
        assignRB(alloc.dl_rbs[rb], *active[ue_idx]);
    }
    m_last_ue_index = (m_last_ue_index + 1) % ue_count;

    // Update stats
    m_stats.total_slots++;
    m_stats.active_ues = static_cast<uint32_t>(active.size());
    uint64_t slot_bytes = 0;
    for (auto& rb : alloc.dl_rbs)
        slot_bytes += rb.transport_block_size;
    m_stats.total_bytes_scheduled += slot_bytes;

    double slot_duration_s = 1e-3 / (1u << alloc.numerology);
    double slot_mbps = (slot_bytes * 8.0) / slot_duration_s / 1e6;
    m_stats.cell_throughput_mbps =
        0.9 * m_stats.cell_throughput_mbps + 0.1 * slot_mbps;

    return alloc;
}

SchedulerStats RoundRobinScheduler::getStats() const { return m_stats; }

// ─── Proportional Fair ───────────────────────────────────────────────────────

ProportionalFairScheduler::ProportionalFairScheduler(uint16_t num_rbs, double alpha)
    : m_num_rbs(num_rbs), m_alpha(alpha), m_stats{}
{}

void ProportionalFairScheduler::updateThroughputEstimate(UE& ue, uint32_t allocated_bytes) {
    // 3GPP TR 36.814 exponential moving average:
    // T(t+1) = (1-alpha)*T(t) + alpha*r(t)
    ue.avg_throughput = (1.0 - m_alpha) * ue.avg_throughput
                      + m_alpha * (allocated_bytes > 0 ? ue.instantaneous_rate : 0.0);
}

SlotAllocation ProportionalFairScheduler::schedule(
    uint32_t slot,
    std::vector<std::shared_ptr<UE>>& ues,
    uint16_t num_rbs)
{
    auto alloc = makeEmptySlot(slot, 1, num_rbs);

    std::vector<std::shared_ptr<UE>> active;
    for (auto& ue : ues)
        if (ue->is_active && ue->buffer_status > 0)
            active.push_back(ue);

    if (active.empty()) return alloc;

    // Pre-compute instantaneous rate for each UE (1 RB worth)
    for (auto& ue : active) {
        uint8_t  mcs = cqiToMcs(ue->cqi);
        uint32_t tbs = mcsToTBS(mcs, 1, ue->numerology);
        double   slot_dur_s = 1e-3 / (1u << ue->numerology);
        ue->instantaneous_rate = (tbs * 8.0) / slot_dur_s; // bps for 1 RB
    }

    // Greedy per-RB PF allocation: assign each RB to UE with highest metric
    std::vector<uint32_t> rb_alloc_count(active.size(), 0);

    for (uint16_t rb = 0; rb < num_rbs; ++rb) {
        double   best_metric = -1.0;
        uint32_t best_idx    = 0;

        for (uint32_t i = 0; i < active.size(); ++i) {
            double metric = active[i]->getPFMetric();
            if (metric > best_metric) {
                best_metric = metric;
                best_idx    = i;
            }
        }

        assignRB(alloc.dl_rbs[rb], *active[best_idx]);
        rb_alloc_count[best_idx]++;

        // Temporarily reduce metric to distribute RBs more fairly within slot
        active[best_idx]->avg_throughput *= 1.05;
    }

    // Restore and properly update EMA throughput
    for (uint32_t i = 0; i < active.size(); ++i) {
        uint8_t  mcs       = cqiToMcs(active[i]->cqi);
        uint32_t tbs_total = mcsToTBS(mcs, rb_alloc_count[i], active[i]->numerology);
        updateThroughputEstimate(*active[i], tbs_total);
        active[i]->buffer_status -= std::min(active[i]->buffer_status, tbs_total);
    }

    // Stats
    m_stats.total_slots++;
    m_stats.active_ues = static_cast<uint32_t>(active.size());
    uint64_t slot_bytes = 0;
    for (auto& rb : alloc.dl_rbs)
        slot_bytes += rb.transport_block_size;
    m_stats.total_bytes_scheduled += slot_bytes;

    double slot_dur_s  = 1e-3 / (1u << alloc.numerology);
    double slot_mbps   = (slot_bytes * 8.0) / slot_dur_s / 1e6;
    m_stats.cell_throughput_mbps =
        0.9 * m_stats.cell_throughput_mbps + 0.1 * slot_mbps;

    return alloc;
}

SchedulerStats ProportionalFairScheduler::getStats() const { return m_stats; }

// ─── Max Throughput ───────────────────────────────────────────────────────────

MaxThroughputScheduler::MaxThroughputScheduler(uint16_t num_rbs)
    : m_num_rbs(num_rbs), m_stats{}
{}

SlotAllocation MaxThroughputScheduler::schedule(
    uint32_t slot,
    std::vector<std::shared_ptr<UE>>& ues,
    uint16_t num_rbs)
{
    auto alloc = makeEmptySlot(slot, 1, num_rbs);

    // Find UE with highest CQI — assign all RBs to it
    std::shared_ptr<UE> best_ue = nullptr;
    for (auto& ue : ues) {
        if (!ue->is_active || ue->buffer_status == 0) continue;
        if (!best_ue || ue->cqi > best_ue->cqi)
            best_ue = ue;
    }

    if (!best_ue) return alloc;

    for (auto& rb : alloc.dl_rbs)
        assignRB(rb, *best_ue);

    m_stats.total_slots++;
    m_stats.active_ues = 1;
    uint64_t slot_bytes = 0;
    for (auto& rb : alloc.dl_rbs) slot_bytes += rb.transport_block_size;
    m_stats.total_bytes_scheduled += slot_bytes;

    return alloc;
}

SchedulerStats MaxThroughputScheduler::getStats() const { return m_stats; }

} // namespace nr5g
