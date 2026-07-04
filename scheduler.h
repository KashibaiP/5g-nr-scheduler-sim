#pragma once
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace nr5g {

// 3GPP TS 38.321 - MAC Scheduler types
enum class SchedulerType {
    ROUND_ROBIN,
    PROPORTIONAL_FAIR,
    MAX_THROUGHPUT
};

enum class SlotFormat {
    DL_ONLY,
    UL_ONLY,
    MIXED_SLOT
};

struct UE {
    uint16_t rnti;               // Radio Network Temporary Identifier
    uint8_t  cqi;                // Channel Quality Indicator (0-15)
    uint32_t buffer_status;      // BSR in bytes
    double   avg_throughput;     // Exponential moving average throughput
    double   instantaneous_rate;
    uint8_t  numerology;         // mu: 0=15kHz, 1=30kHz, 2=60kHz, 3=120kHz
    bool     is_active;

    UE(uint16_t rnti, uint8_t cqi, uint8_t numerology = 1);
    double getPFMetric() const;
    std::string toString() const;
};

struct ResourceBlock {
    uint16_t rb_index;
    uint8_t  mcs;                // Modulation and Coding Scheme (0-28)
    uint16_t assigned_rnti;      // 0xFFFF = unassigned
    uint32_t transport_block_size;
    SlotFormat slot_format;

    ResourceBlock(uint16_t index);
    bool isAssigned() const { return assigned_rnti != 0xFFFF; }
};

struct SlotAllocation {
    uint32_t slot_number;
    uint8_t  numerology;
    std::vector<ResourceBlock> dl_rbs;
    std::vector<ResourceBlock> ul_rbs;
    uint32_t timestamp_us;
};

struct SchedulerStats {
    uint64_t total_slots;
    uint64_t total_bytes_scheduled;
    double   avg_ue_throughput;
    double   cell_throughput_mbps;
    double   resource_utilization;
    uint32_t active_ues;
};

class IScheduler {
public:
    virtual ~IScheduler() = default;
    virtual SlotAllocation schedule(
        uint32_t slot_number,
        std::vector<std::shared_ptr<UE>>& ues,
        uint16_t num_rbs) = 0;
    virtual SchedulerStats getStats() const = 0;
    virtual std::string getName() const = 0;
};

// 3GPP TS 38.214 - CQI to MCS mapping
uint8_t  cqiToMcs(uint8_t cqi);
uint32_t mcsToTBS(uint8_t mcs, uint16_t num_rbs, uint8_t numerology);

} // namespace nr5g
