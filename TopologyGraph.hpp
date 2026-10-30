#pragma once

#include "Types.hpp"

#include <string>

class TopologyGraph {
public:
    void load_from_file(const std::string& path);

    int num_nodes() const { return num_nodes_; }
    int num_edges() const { return num_edges_; }
    const std::vector<std::vector<PartId>>& adjacency() const { return adjacency_; }
    const std::vector<std::vector<int>>& distances() const { return distances_; }

    bool connected_or_same(PartId a, PartId b) const {
        return a >= 0 && b >= 0 && a < num_nodes_ && b < num_nodes_ &&
               distances_[a][b] <= 1;
    }

private:
    int num_nodes_ = 0;
    int num_edges_ = 0;
    std::vector<std::vector<PartId>> adjacency_;
    std::vector<std::vector<int>> distances_;

    void compute_all_pairs_shortest_paths();
};

