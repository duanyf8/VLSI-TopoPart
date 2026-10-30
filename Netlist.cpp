#include "Netlist.hpp"

#include <fstream>
#include <sstream>

namespace {

bool edges_look_one_based(const std::vector<Edge>& edges, int n) {
    if (edges.empty()) {
        return false;
    }
    bool saw_zero = false;
    int min_id = std::numeric_limits<int>::max();
    int max_id = std::numeric_limits<int>::min();
    for (const auto& [u, v] : edges) {
        saw_zero = saw_zero || u == 0 || v == 0;
        min_id = std::min({min_id, u, v});
        max_id = std::max({max_id, u, v});
    }
    return !saw_zero && min_id >= 1 && max_id <= n;
}

bool fixed_look_one_based(const std::vector<std::vector<NodeId>>& fixed_nodes, int n) {
    bool any = false;
    bool saw_zero = false;
    int min_id = std::numeric_limits<int>::max();
    int max_id = std::numeric_limits<int>::min();
    for (const auto& line : fixed_nodes) {
        for (int u : line) {
            any = true;
            saw_zero = saw_zero || u == 0;
            min_id = std::min(min_id, u);
            max_id = std::max(max_id, u);
        }
    }
    return any && !saw_zero && min_id >= 1 && max_id <= n;
}

} // namespace

void Netlist::load_from_file(const std::string& path, int num_topology_nodes) {
    std::ifstream in(path);
    require_or_throw(static_cast<bool>(in), "Cannot open netlist file: " + path);

    int raw_edge_count = 0;
    require_or_throw(static_cast<bool>(in >> num_nodes_), "Invalid netlist node count");
    require_or_throw(static_cast<bool>(in >> raw_edge_count), "Invalid netlist edge count");
    require_or_throw(num_nodes_ >= 0, "Netlist node count cannot be negative");
    require_or_throw(raw_edge_count >= 0, "Netlist edge count cannot be negative");

    std::vector<Edge> raw_edges;
    raw_edges.reserve(static_cast<std::size_t>(raw_edge_count));
    for (int i = 0; i < raw_edge_count; ++i) {
        int u = -1;
        int v = -1;
        require_or_throw(static_cast<bool>(in >> u >> v),
                         "Unexpected end of netlist edge list");
        raw_edges.emplace_back(u, v);
    }

    if (edges_look_one_based(raw_edges, num_nodes_)) {
        for (auto& [u, v] : raw_edges) {
            --u;
            --v;
        }
    }

    edges_.clear();
    edges_.reserve(raw_edges.size());
    adjacency_.assign(static_cast<std::size_t>(num_nodes_), {});
    for (auto [u, v] : raw_edges) {
        require_or_throw(0 <= u && u < num_nodes_ && 0 <= v && v < num_nodes_,
                         "Netlist edge endpoint out of range");
        if (u == v) {
            continue;
        }
        edges_.emplace_back(u, v);
        adjacency_[static_cast<std::size_t>(u)].push_back(v);
        adjacency_[static_cast<std::size_t>(v)].push_back(u);
    }

    for (auto& nbrs : adjacency_) {
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
    }

    std::string rest;
    std::getline(in, rest);
    // Fixed-constraint section: one line per topology node (FPGA); line i lists
    // the nodes fixed to FPGA i.  Read EXACTLY k = num_topology_nodes lines and
    // ignore any trailing ones.  This is the homework's "migrating to a smaller
    // FPGA graph" rule: case1 was generated for MFS2 (43 FPGAs) and carries 43
    // fixed lines, so running it on MFS1 (8 FPGAs) reads only lines 0..7 and
    // discards the trailing 35.  `fixed_by_part` is sized to k and indexed by
    // p < k, so no trailing line can cause an out-of-range write.
    std::vector<std::vector<NodeId>> fixed_by_part(static_cast<std::size_t>(num_topology_nodes));
    for (int p = 0; p < num_topology_nodes; ++p) {
        if (!std::getline(in, rest)) {
            break;
        }
        std::istringstream line(rest);
        int u = -1;
        while (line >> u) {
            fixed_by_part[static_cast<std::size_t>(p)].push_back(u);
        }
    }

    if (fixed_look_one_based(fixed_by_part, num_nodes_)) {
        for (auto& line : fixed_by_part) {
            for (int& u : line) {
                --u;
            }
        }
    }

    fixed_part_.assign(static_cast<std::size_t>(num_nodes_), -1);
    for (int p = 0; p < num_topology_nodes; ++p) {
        for (int u : fixed_by_part[static_cast<std::size_t>(p)]) {
            require_or_throw(0 <= u && u < num_nodes_, "Fixed node ID out of range");
            PartId& dst = fixed_part_[static_cast<std::size_t>(u)];
            require_or_throw(dst == -1 || dst == p, "Node fixed to multiple topology nodes");
            dst = p;
        }
    }
}

