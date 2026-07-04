#include "scheduler.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace nr5g {

// ─── UE ──────────────────────────────────────────────────────────────────────

UE::UE(uint16_t rnti, uint8_t cqi, uint8_t numerology)
    : rnti(rnti)
    , cqi(std::min(cqi, static_cast<uint8_t>(15)))
    , buffer_status(0)
    , avg_throughput(1.0)   // Initialise to 1 to avoid div-by-zero in PF metric
    , instantaneous_rate(0.0)
    , numerology(numerology)
    , is_active(true)
{
    if (numerology > 3)
        throw std::invalid_argument("Numerology mu must be 0-3 per 3GPP TS 38.211");
}

double UE::getPFMetric() const {
    // PF metric = instantaneous_rate / avg_throughput  (3GPP TR 36.814 Sec 6.2)
    if (avg_throughput < 1e-9) return 0.0;
    return instantaneous_rate / avg_throughput;
}

std::string UE::toString() const {
    std::ostringstream ss;
    ss << "UE[RNTI=0x" << std::hex << rnti << std::dec
       << " CQI=" << static_cast<int>(cqi)
       << " mu=" << static_cast<int>(numerology)
       << " BSR=" << buffer_status << "B"
       << " avgTP=" << std::fixed << avg_throughput / 1e6 << "Mbps]";
    return ss.str();
}

// ─── ResourceBlock ────────────────────────────────────────────────────────────

ResourceBlock::ResourceBlock(uint16_t index)
    : rb_index(index)
    , mcs(0)
    , assigned_rnti(0xFFFF)
    , transport_block_size(0)
    , slot_format(SlotFormat::DL_ONLY)
{}

// ─── CQI → MCS mapping (3GPP TS 38.214 Table 5.2.2.1-2) ─────────────────────
//     Maps CQI index 0-15 to MCS index 0-28

static const uint8_t CQI_TO_MCS_TABLE[16] = {
    0,   // CQI 0  → out of range / no data (QPSK, lowest MCS)
    0,   // CQI 1  → QPSK, code rate ~0.08
    1,   // CQI 2
    2,   // CQI 3
    4,   // CQI 4
    6,   // CQI 5
    8,   // CQI 6
    11,  // CQI 7
    13,  // CQI 8  → 16-QAM
    16,  // CQI 9
    18,  // CQI 10
    20,  // CQI 11 → 64-QAM
    22,  // CQI 12
    24,  // CQI 13
    26,  // CQI 14
    28   // CQI 15 → 256-QAM, highest code rate
};

uint8_t cqiToMcs(uint8_t cqi) {
    if (cqi > 15) cqi = 15;
    return CQI_TO_MCS_TABLE[cqi];
}

// ─── MCS → TBS (simplified model, proportional to 3GPP TS 38.214 Table 5.1.3.1-1)
//     Full TBS tables are large; this approximation is within ~5% for simulation.

uint32_t mcsToTBS(uint8_t mcs, uint16_t num_rbs, uint8_t /*numerology*/) {
    if (mcs > 28) mcs = 28;

    // Bits per resource element per subcarrier (spectral efficiency proxy)
    static const double SE_TABLE[29] = {
        0.234, 0.311, 0.379, 0.490, 0.602, 0.703, 0.877, 1.033, 1.201, 1.378,
        1.570, 1.766, 1.973, 2.277, 2.570, 2.766, 3.068, 3.379, 3.621, 3.877,
        4.213, 4.523, 4.816, 5.115, 5.391, 5.696, 5.992, 6.272, 6.592
    };

    // RB = 12 subcarriers × 14 OFDM symbols per slot
    const double symbols_per_slot = 14.0;
    const double sc_per_rb        = 12.0;
    const double overhead         = 0.86; // ~14% overhead (DMRS, PDCCH, etc.)

    double bits = SE_TABLE[mcs] * sc_per_rb * symbols_per_slot * num_rbs * overhead;
    return static_cast<uint32_t>(bits / 8); // bytes
}

} // namespace nr5g
