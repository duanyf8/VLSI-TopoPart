#include "CandidatePropagator.hpp"
#include "Netlist.hpp"
#include "Partitioner.hpp"
#include "TopologyGraph.hpp"
#include "solution.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

/*
README
======

Compile (CMake):
  cmake -B build
  cmake --build build --config Release -j

Compile (Make, Linux/WSL):
  make all

Run - 拓扑约束多路划分 (requirement 3):
  ./TopoPartitioner topo <topo_file> <netlist_file> <r> <threads> <seed>

Run - 基线二路FM划分 (lab1 baseline):
  ./TopoPartitioner baseline <netlist.hgr> <runs> <max_passes> <seed>

Run - 域探测 (diagnostic):
  ./TopoPartitioner probe <topo_file> <netlist_file>

Examples:
  拓扑约束: TopoPartitioner topo benchmark/MFS1 benchmark/case1 0.05 8 2026
  基线划分: TopoPartitioner baseline benchmark/ibm01.hgr 8 8 2026
  域探测:   TopoPartitioner probe benchmark/MFS1 benchmark/case1
*/

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage:\n"
              << "  " << argv0 << " topo <topo_file> <netlist_file> <r> <threads> <seed>\n"
              << "  " << argv0 << " baseline <netlist.hgr> <runs> <max_passes> <seed>\n"
              << "  " << argv0 << " probe <topo_file> <netlist_file>\n";
}

double parse_double(const char* value, const std::string& name) {
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    require_or_throw(end != value && *end == '\0', "Invalid " + name + ": " + value);
    return parsed;
}

int parse_int(const char* value, const std::string& name) {
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    require_or_throw(end != value && *end == '\0', "Invalid " + name + ": " + value);
    require_or_throw(parsed > 0 && parsed <= std::numeric_limits<int>::max(),
                     name + " must be a positive integer");
    return static_cast<int>(parsed);
}

std::uint64_t parse_u64(const char* value, const std::string& name) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    require_or_throw(end != value && *end == '\0', "Invalid " + name + ": " + value);
    return static_cast<std::uint64_t>(parsed);
}

void print_sizes(const std::vector<int>& sizes) {
    std::cout << "Partition Size Distribution:";
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        std::cout << (i == 0 ? " " : ", ") << i << ":" << sizes[i];
    }
    std::cout << '\n';
}

// ---- 拓扑约束多路划分 ----
int run_topo(int argc, char** argv) {
    if (argc != 7) {
        std::cerr << "Usage: topo <topo_file> <netlist_file> <r> <threads> <seed>\n";
        return 1;
    }

    const std::string topo_path = argv[2];
    const std::string netlist_path = argv[3];
    const double r = parse_double(argv[4], "r_parameter");
    const int threads = parse_int(argv[5], "num_threads");
    const std::uint64_t seed = parse_u64(argv[6], "seed");

    const long long load_start = now_microseconds();
    TopologyGraph topology;
    topology.load_from_file(topo_path);

    Netlist netlist;
    netlist.load_from_file(netlist_path, topology.num_nodes());

    CandidateMask candidates = CandidatePropagator::initialize_and_propagate(netlist, topology);
    const double load_seconds = static_cast<double>(now_microseconds() - load_start) / 1'000'000.0;

    PartitionConfig config;
    config.balance_ratio = r;
    config.num_threads = threads;
    config.seed = seed;
    config.max_passes = 20;

    Partitioner partitioner(topology, netlist, candidates);
    const BalanceLimits limits = partitioner.compute_balance_limits(r);

    double single_seconds = 0.0;
    double multi_seconds = 0.0;
    double speedup = 0.0;
    PartitionResult result = partitioner.run_parallel(config, single_seconds, multi_seconds, speedup);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Topology Nodes: " << topology.num_nodes() << '\n';
    std::cout << "Netlist Nodes: " << netlist.num_nodes() << '\n';
    std::cout << "Netlist Edges: " << netlist.num_edges() << '\n';
    std::cout << "Balance Lower Bound: " << limits.lower << '\n';
    std::cout << "Balance Upper Bound: " << limits.upper << '\n';
    std::cout << "Total Cut Cost: " << result.cut_cost << '\n';
    print_sizes(result.part_sizes);
    std::cout << "Number of Topology Violations: " << result.topology_violations << '\n';
    std::cout << "Number of Fixed Constraint Violations: " << result.fixed_violations << '\n';
    std::cout << "Input and CPP Time Seconds: " << load_seconds << '\n';
    std::cout << "Attempts Per Batch: " << threads << '\n';
    std::cout << "Single-thread Time Seconds: " << single_seconds << '\n';
    std::cout << "Multi-thread Time Seconds: " << multi_seconds << '\n';
    std::cout << "Parallel Speedup Ratio: " << speedup << '\n';
    std::cout << "Feasible: " << (result.feasible ? "yes" : "no") << '\n';

    if (!result.feasible) {
        if (!result.message.empty()) {
            std::cerr << "[ERROR] " << result.message << '\n';
        }
        return 2;
    }
    return 0;
}

// ---- 基线二路FM划分 ----
int run_baseline(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: baseline <netlist.hgr> <runs> <max_passes> <seed>\n";
        return 1;
    }

    const std::string netlist_path = argv[2];
    const int runs = parse_int(argv[3], "runs");
    const int max_passes = parse_int(argv[4], "max_passes");
    const unsigned int seed = static_cast<unsigned int>(parse_u64(argv[5], "seed"));

    Solution solution;
    solution.read_benchmark(netlist_path);
    solution.run(runs, max_passes, seed);

    return 0;
}

// ---- 域探测 ----
int run_probe(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: probe <topo_file> <netlist_file>\n";
        return 1;
    }

    TopologyGraph topo;
    topo.load_from_file(argv[2]);
    Netlist net;
    net.load_from_file(argv[3], topo.num_nodes());
    const int k = topo.num_nodes();

    CandidateMask m = CandidatePropagator::initialize_and_propagate(net, topo);

    std::map<int, long long> hist;
    long long total = 0;
    long long fixed_cnt = 0;
    for (int u = 0; u < net.num_nodes(); ++u) {
        int c = 0;
        for (int p = 0; p < k; ++p) if (m[u][p]) ++c;
        ++hist[c];
        total += c;
        if (net.is_fixed(u)) ++fixed_cnt;
    }
    std::cout << "topology FPGAs k=" << k << "  nodes=" << net.num_nodes()
              << "  fixed=" << fixed_cnt << "\n";
    std::cout << "avg domain size = " << static_cast<double>(total) / net.num_nodes() << "\n";
    std::cout << "domain-size histogram (size : #nodes):\n";
    for (auto [sz, cnt] : hist) std::cout << "  " << sz << " : " << cnt << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string mode = argv[1];
        if (mode == "topo") {
            return run_topo(argc, argv);
        } else if (mode == "baseline") {
            return run_baseline(argc, argv);
        } else if (mode == "probe") {
            return run_probe(argc, argv);
        } else {
            std::cerr << "Unknown mode: " << mode << "\n";
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << '\n';
        return 1;
    }

    return 0;
}
