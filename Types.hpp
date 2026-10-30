#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using NodeId = int;
using PartId = int;
using Edge = std::pair<NodeId, NodeId>;
using CandidateMask = std::vector<std::vector<bool>>;

constexpr int INF_DISTANCE = 1'000'000'000;

struct PartitionConfig {
    double balance_ratio = 0.0;
    int num_threads = 1;
    std::uint64_t seed = 1;
    int max_passes = 8;
};

struct BalanceLimits {
    int lower = 0;
    int upper = 0;
    bool perfect_balance = false;
};

struct PartitionResult {
    std::vector<PartId> assignment;
    std::vector<int> part_sizes;
    long long cut_cost = std::numeric_limits<long long>::max();
    long long topology_violations = std::numeric_limits<long long>::max();
    long long fixed_violations = std::numeric_limits<long long>::max();
    bool feasible = false;
    std::string message;
};

inline long long now_microseconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(
               clock::now().time_since_epoch())
        .count();
}

inline void require_or_throw(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

