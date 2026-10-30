#pragma once

#include "Netlist.hpp"
#include "TopologyGraph.hpp"
#include "Types.hpp"

#include <deque>

class CandidatePropagator {
public:
    static bool pair_compatible(const TopologyGraph& topology, PartId a, PartId b) {
        return topology.connected_or_same(a, b);
    }

    static CandidateMask initialize_relaxed(const Netlist& netlist, const TopologyGraph& topology) {
        return initialize(netlist, topology, false);
    }

    static CandidateMask initialize_and_propagate(const Netlist& netlist,
                                                  const TopologyGraph& topology) {
        return initialize(netlist, topology, true);
    }

private:
    static CandidateMask initialize(const Netlist& netlist,
                                    const TopologyGraph& topology,
                                    bool propagate) {
        const int v_count = netlist.num_nodes();
        const int k = topology.num_nodes();
        CandidateMask mask(static_cast<std::size_t>(v_count),
                           std::vector<bool>(static_cast<std::size_t>(k), true));

        for (int u = 0; u < v_count; ++u) {
            const PartId fixed = netlist.fixed_part()[static_cast<std::size_t>(u)];
            if (fixed >= 0) {
                std::fill(mask[static_cast<std::size_t>(u)].begin(),
                          mask[static_cast<std::size_t>(u)].end(), false);
                mask[static_cast<std::size_t>(u)][static_cast<std::size_t>(fixed)] = true;
            }
        }

        if (!propagate) {
            return mask;
        }

        // Save a relaxed copy before propagation. If propagation empties a
        // node's domain (happens when fixed-node pairs are topology-
        // disconnected on a smaller FPGA graph, e.g. MFS2 data on MFS1),
        // fall back to the relaxed mask instead of crashing.
        CandidateMask relaxed = mask;

        std::deque<Edge> queue;
        for (const auto& e : netlist.edges()) {
            queue.push_back(e);
            queue.emplace_back(e.second, e.first);
        }

        bool ok = true;
        while (!queue.empty() && ok) {
            const auto [u, v] = queue.front();
            queue.pop_front();
            if (!revise(mask, topology, u, v)) {
                continue;
            }
            if (!any_candidate(mask, u)) {
                ok = false;
                break;
            }
            for (NodeId w : netlist.adjacency()[static_cast<std::size_t>(u)]) {
                if (w != v) {
                    queue.emplace_back(w, u);
                }
            }
        }

        if (!ok) {
            return relaxed;
        }

        return mask;
    }

    static bool any_candidate(const CandidateMask& mask, NodeId u) {
        const auto& row = mask[static_cast<std::size_t>(u)];
        return std::any_of(row.begin(), row.end(), [](bool x) { return x; });
    }

    static bool revise(CandidateMask& mask, const TopologyGraph& topology, NodeId u, NodeId v) {
        bool changed = false;
        auto& cu = mask[static_cast<std::size_t>(u)];
        const auto& cv = mask[static_cast<std::size_t>(v)];

        for (PartId p = 0; p < topology.num_nodes(); ++p) {
            if (!cu[static_cast<std::size_t>(p)]) {
                continue;
            }
            bool supported = false;
            for (PartId q = 0; q < topology.num_nodes(); ++q) {
                if (cv[static_cast<std::size_t>(q)] && topology.connected_or_same(p, q)) {
                    supported = true;
                    break;
                }
            }
            if (!supported) {
                cu[static_cast<std::size_t>(p)] = false;
                changed = true;
            }
        }

        return changed;
    }
};
