#ifndef SOLUTION_H
#define SOLUTION_H

#include <string>
#include <vector>
#include <random>
#include "Graph.h"

class Solution {
private:
    struct TopoGraph {
        int k;
        std::vector<std::vector<int>> adj;
    };

    struct HGraph {
        int n;
        int m;
        std::vector<std::vector<int> > nets;
        std::vector<int> net_weight;
        std::vector<std::vector<int> > incident;
        std::vector<int> vertex_weight;
        long long total_weight;
        std::vector<int> fixed_part;

        HGraph();
    };

    struct FMResult {
        std::vector<int> part;
        int cut;
        int passes;
        double seconds;
        std::vector<int> pass_cuts;

        FMResult();
    };

    struct MLResult {
        std::vector<int> part;
        int cut;
        int levels;
        double seconds;
        std::vector<int> level_cuts;

        MLResult();
    };

private:
    Graph graph_;

    HGraph original_;
    TopoGraph topo_;

    int n_;
    int m_;
    double r_;

    long long lower_bound_;
    long long upper_bound_;

    std::string input_path_;
    std::string benchmark_name_;

    std::vector<int> best_partition_;
    int best_cut_;

private:
    void build_incidence(HGraph &h) const;

    int compute_cut(
        const HGraph &h,
        const std::vector<int> &part
    ) const;

    void compute_counts(
        const HGraph &h,
        const std::vector<int> &part,
        std::vector<int> &cnt0,
        std::vector<int> &cnt1
    ) const;

    long long part0_weight(
        const HGraph &h,
        const std::vector<int> &part
    ) const;

    bool is_balanced(
        const HGraph &h,
        const std::vector<int> &part
    ) const;

    int gain_of_vertex(
        const HGraph &h,
        int v,
        const std::vector<int> &part,
        const std::vector<int> &cnt0,
        const std::vector<int> &cnt1
    ) const;

    void repair_balance(
        const HGraph &h,
        std::vector<int> &part,
        std::mt19937 &rng
    ) const;

    std::vector<int> make_random_initial(
        const HGraph &h,
        std::mt19937 &rng
    ) const;

    std::vector<int> make_region_initial(
        const HGraph &h,
        int run_id,
        std::mt19937 &rng
    ) const;

    std::vector<int> make_initial_partition(
        const HGraph &h,
        int run_id,
        std::mt19937 &rng
    ) const;

    FMResult fm_refine(
        const HGraph &h,
        const std::vector<int> &initial_part,
        int max_passes
    ) const;

    HGraph coarsen_once(
        const HGraph &fine,
        std::mt19937 &rng,
        std::vector<int> &fine_to_coarse
    ) const;

    MLResult multilevel_partition_one_run(
        int run_id,
        int max_passes,
        std::mt19937 &rng
    ) const;

    void write_partition(const std::string &benchmark_path) const;

    void write_run_csv_header(const std::string &csv_path) const;

    void append_run_csv(
        const std::string &csv_path,
        int run_id,
        const MLResult &result
    ) const;

    void write_summary_csv(
        const std::string &summary_path,
        int runs,
        int max_passes,
        double total_seconds
    ) const;

public:
    Solution();

    void read_benchmark(const std::string &benchmark_path);

    void run(
        int runs = 6,
        int max_passes = 8,
        unsigned int seed = 2026
    );

    const std::vector<int> &get_partition() const;

    int get_best_cut() const;
};

#endif