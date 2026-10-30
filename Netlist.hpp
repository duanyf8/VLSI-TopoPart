#pragma once

#include "TopologyGraph.hpp"
#include "Types.hpp"

#include <string>

class Netlist {
public:
    void load_from_file(const std::string& path, int num_topology_nodes);

    int num_nodes() const { return num_nodes_; }
    int num_edges() const { return static_cast<int>(edges_.size()); }
    const std::vector<Edge>& edges() const { return edges_; }
    const std::vector<std::vector<NodeId>>& adjacency() const { return adjacency_; }
    const std::vector<PartId>& fixed_part() const { return fixed_part_; }
    bool is_fixed(NodeId u) const { return fixed_part_[static_cast<std::size_t>(u)] >= 0; }

private:
    int num_nodes_ = 0;
    std::vector<Edge> edges_;
    std::vector<std::vector<NodeId>> adjacency_;
    std::vector<PartId> fixed_part_;
};

