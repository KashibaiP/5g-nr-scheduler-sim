#include "schedulers.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <random>
#include <chrono>

using namespace nr5g;

// ─── Simulation config ───────────────────────────────────────────────────────

struct SimConfig {
    uint32_t num_slots     = 10000;  // 10000 slots = 10 seconds @ mu=1
    uint16_t num_rbs       = 52;     // 10 MHz NR bandwidth
    uint32_t num_ues       = 8;
    uint8_t  numerology    = 1;      // 30 kHz SCS
    bool     verbose       = false;
};

// ─── UE factory ──────────────────────────────────────────────────────────────

std::vector<std::shared_ptr<UE>> createUEs(uint32_t count, std::mt19937& rng) {
    std::vector<std::shared_ptr<UE>> ues;
    std::uniform_int_distribution<uint8_t> cqi_dist(1, 15);
    std::uniform_int_distribution<uint32_t> bsr_dist(5000, 500000); // 5KB – 500KB

    for (uint32_t i = 0; i < count; ++i) {
        auto ue = std::make_shared<UE>(
            static_cast<uint16_t>(0x1001 + i),
            cqi_dist(rng),
            1 // mu=1 (30 kHz)
        );
        ue->buffer_status = bsr_dist(rng);
        ues.push_back(ue);
    }
    return ues;
}

// ─── Simulation runner ───────────────────────────────────────────────────────

void runSimulation(IScheduler& scheduler, const SimConfig& cfg, std::mt19937& rng) {
    auto ues = createUEs(cfg.num_ues, rng);

    std::uniform_int_distribution<uint32_t> arrival(0, 50000);
    std::bernoulli_distribution  cqi_change(0.05); // 5% chance CQI changes each slot
    std::uniform_int_distribution<uint8_t> new_cqi(1, 15);

    auto t_start = std::chrono::high_resolution_clock::now();

    for (uint32_t slot = 0; slot < cfg.num_slots; ++slot) {
        // Simulate channel variations and buffer arrivals
        for (auto& ue : ues) {
            if (cqi_change(rng)) ue->cqi = new_cqi(rng);
            ue->buffer_status += arrival(rng); // new data arrivals
        }

        auto alloc = scheduler.schedule(slot, ues, cfg.num_rbs);

        if (cfg.verbose && slot % 1000 == 0) {
            auto stats = scheduler.getStats();
            std::cout << "  Slot " << std::setw(5) << slot
                      << "  CellTP=" << std::fixed << std::setprecision(1)
                      << stats.cell_throughput_mbps << " Mbps"
                      << "  ActiveUEs=" << stats.active_ues << "\n";
        }
    }

    auto t_end  = std::chrono::high_resolution_clock::now();
    auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

    auto stats = scheduler.getStats();

    std::cout << "\n┌─────────────────────────────────────────────┐\n";
    std::cout << "│  Scheduler : " << std::left << std::setw(31) << scheduler.getName() << "│\n";
    std::cout << "├─────────────────────────────────────────────┤\n";
    std::cout << "│  Total slots       : " << std::setw(22) << stats.total_slots << "│\n";
    std::cout << "│  Cell throughput   : " << std::setw(18) << std::fixed << std::setprecision(2)
              << stats.cell_throughput_mbps << " Mbps    │\n";
    std::cout << "│  Total scheduled   : " << std::setw(16) << stats.total_bytes_scheduled / 1024
              << " KB        │\n";
    std::cout << "│  Active UEs        : " << std::setw(22) << stats.active_ues << "│\n";
    std::cout << "│  Sim wall time     : " << std::setw(16) << dur_us / 1000
              << " ms        │\n";
    std::cout << "└─────────────────────────────────────────────┘\n\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    SimConfig cfg;
    if (argc > 1) cfg.num_ues   = std::atoi(argv[1]);
    if (argc > 2) cfg.num_slots = std::atoi(argv[2]);
    if (argc > 3) cfg.verbose   = (std::string(argv[3]) == "--verbose");

    std::mt19937 rng(42); // reproducible seed

    std::cout << "\n╔══════════════════════════════════════════════╗\n";
    std::cout << "║   5G NR MAC Scheduler Simulation             ║\n";
    std::cout << "║   Numerology μ=1 (30 kHz SCS)  BW=10 MHz   ║\n";
    std::cout << "║   UEs=" << std::setw(3) << cfg.num_ues
              << "  Slots=" << std::setw(6) << cfg.num_slots
              << "  RBs=" << cfg.num_rbs << "             ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    {
        RoundRobinScheduler rr(cfg.num_rbs);
        runSimulation(rr, cfg, rng);
    }
    rng.seed(42);
    {
        ProportionalFairScheduler pf(cfg.num_rbs);
        runSimulation(pf, cfg, rng);
    }
    rng.seed(42);
    {
        MaxThroughputScheduler mt(cfg.num_rbs);
        runSimulation(mt, cfg, rng);
    }

    return 0;
}
