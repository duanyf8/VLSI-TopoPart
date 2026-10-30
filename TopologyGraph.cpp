#include "TopologyGraph.hpp"

#include <fstream>
#include <queue>
#include <sstream>

namespace {

bool should_convert_one_based(const std::vector<Edge>& edges, int n) {
    bool saw_zero = false;
    int min_id = std::numeric_limits<int>::max();
    int max_id = std::numeric_limits<int>::min();
    for (const auto& [u, v] : edges) {
        saw_zero = saw_zero || u == 0 || v == 0;
        min_id = std::min({min_id, u, v});
        max_id = std::max({max_id, u, v});
    }
    return !edges.empty() && !saw_zero && min_id >= 1 && max_id <= n;
}

} // namespace

void TopologyGraph::load_from_file(const std::string& path) {
    std::ifstream in(path);
    require_or_throw(static_cast<bool>(in), "Cannot open topology file: " + path);

    require_or_throw(static_cast<bool>(in >> num_nodes_ >> num_edges_),
                     "Invalid topology header in: " + path);
    require_or_throw(num_nodes_ > 0, "Topology graph must contain at least one node");
    require_or_throw(num_edges_ >= 0, "Topology edge count cannot be negative");

    std::vector<Edge> edges;
    edges.reserve(static_cast<std::size_t>(num_edges_));
    for (int i = 0; i < num_edges_; ++i) {
        int u = -1;
        int v = -1;
        require_or_throw(static_cast<bool>(in >> u >> v),
                         "Unexpected end of topology edge list");
        edges.emplace_back(u, v);
    }

    if (should_convert_one_based(edges, num_nodes_)) {
        for (auto& [u, v] : edges) {
            --u;
            --v;
        }
    }

    adjacency_.assign(static_cast<std::size_t>(num_nodes_), {});
    for (const auto& [u, v] : edges) {
        require_or_throw(0 <= u && u < num_nodes_ && 0 <= v && v < num_nodes_,
                         "Topology edge endpoint out of range");
        if (u == v) {
            continue;
        }
        adjacency_[static_cast<std::size_t>(u)].push_back(v);
        adjacency_[static_cast<std::size_t>(v)].push_back(u);
    }

    for (auto& nbrs : adjacency_) {
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
    }

    compute_all_pairs_shortest_paths();
}

void TopologyGraph::compute_all_pairs_shortest_paths() {
    distances_.assign(static_cast<std::size_t>(num_nodes_),
                      std::vector<int>(static_cast<std::size_t>(num_nodes_), INF_DISTANCE));

    for (int source = 0; source < num_nodes_; ++source) {
        auto& dist = distances_[static_cast<std::size_t>(source)];
        std::queue<int> q;
        dist[static_cast<std::size_t>(source)] = 0;
        q.push(source);

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (int v : adjacency_[static_cast<std::size_t>(u)]) {
                if (dist[static_cast<std::size_t>(v)] == INF_DISTANCE) {
                    dist[static_cast<std::size_t>(v)] = dist[static_cast<std::size_t>(u)] + 1;
                    q.push(v);
                }
            }
        }
    }
}

