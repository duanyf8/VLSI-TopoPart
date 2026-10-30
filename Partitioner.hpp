#pragma once

#include "CandidatePropagator.hpp"
#include "Netlist.hpp"
#include "TopologyGraph.hpp"
#include "Types.hpp"

// Parallelism is best-of-N independent run_single() attempts (see
// Partitioner.cpp): each owns its RNG and state, so there is no shared mutable
// state and no locking — the threading lives entirely in the .cpp.

class Partitioner {
public:
    Partitioner(const TopologyGraph& topology, const Netlist& netlist, const CandidateMask& candidates);

    BalanceLimits compute_balance_limits(double r) const;
    PartitionResult run_single(std::uint64_t seed, double r, int max_passes) const;
    PartitionResult run_parallel(const PartitionConfig& config,
                                 double& single_thread_seconds,
                                 double& multi_thread_seconds,
                                 double& speedup_ratio) const;

    long long compute_cut_cost(const std::vector<PartId>& assignment) const;
    long long count_topology_violations(const std::vector<PartId>& assignment) const;
    long long count_fixed_violations(const std::vector<PartId>& assignment) const;
    std::vector<int> compute_part_sizes(const std::vector<PartId>& assignment) const;
    bool validate_balance(const std::vector<int>& sizes, const BalanceLimits& limits) const;

private:
    const TopologyGraph& topology_;
    const Netlist& netlist_;
    const CandidateMask& candidates_;

    bool result_better(const PartitionResult& a, const PartitionResult& b) const;
};
