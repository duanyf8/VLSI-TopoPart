#include "Partitioner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Engine: a per-run-single incremental local-search state.
//
// All hot loops are O(degree(u)) per move instead of O(V) or O(E).  The state
// is created fresh for every run_single() call so it is safe to use across the
// threads spawned by run_parallel() (each thread owns its own Engine; the
// Partitioner itself stays const and shared).
//
// Invariants maintained incrementally by move_node():
//   * sizes[p]          = number of nodes currently on partition p
//   * node_bad[u]       = number of u's (deduplicated) incident edges that are
//                         currently topology violations
//   * total_bad         = number of violating undirected edges
//   * active            = movable (non-fixed) nodes with node_bad[u] > 0
//   * active_pos[u]     = index of u inside `active`, or -1 if absent
// ---------------------------------------------------------------------------
struct Engine {
    const TopologyGraph& topo;
    const Netlist& net;
    const CandidateMask& cand;
    std::mt19937_64& rng;

    const int V;
    const int K;
    const int lower;   // balance lower bound we must reach to be feasible
    const int upper;   // capacity upper bound (never exceeded)
    long long deadline_us;   // phase-by-phase budget; lowered between phases

    std::vector<PartId> assign;
    std::vector<int> sizes;
    std::vector<int> node_bad;
    long long total_bad = 0;
    std::vector<NodeId> active;
    std::vector<int> active_pos;
    std::vector<int> tabu_until;

    Engine(const TopologyGraph& t, const Netlist& n, const CandidateMask& c,
           std::mt19937_64& r, int low, int up, long long deadline)
        : topo(t), net(n), cand(c), rng(r),
          V(n.num_nodes()), K(t.num_nodes()),
          lower(low), upper(up), deadline_us(deadline),
          assign(static_cast<std::size_t>(V), -1),
          sizes(static_cast<std::size_t>(K), 0),
          node_bad(static_cast<std::size_t>(V), 0),
          active_pos(static_cast<std::size_t>(V), -1),
          tabu_until(static_cast<std::size_t>(V), 0) {}

    bool cand_ok(NodeId u, PartId p) const {
        return cand[static_cast<std::size_t>(u)][static_cast<std::size_t>(p)];
    }
    bool conn(PartId a, PartId b) const { return topo.connected_or_same(a, b); }
    const std::vector<NodeId>& nbrs(NodeId u) const {
        return net.adjacency()[static_cast<std::size_t>(u)];
    }
    bool timed_out() const { return now_microseconds() > deadline_us; }
    void set_deadline(long long d) { deadline_us = d; }

    // Diagnostics (used by the optional TOPO_DEBUG trace in run_single).
    int under_floor() const {
        int c = 0;
        for (PartId p = 0; p < K; ++p) {
            if (sizes[static_cast<std::size_t>(p)] < lower) {
                ++c;
            }
        }
        return c;
    }

    // Number of u's incident edges that would violate if u were placed on `p`.
    int incident_bad(NodeId u, PartId p) const {
        int bad = 0;
        for (NodeId w : nbrs(u)) {
            const PartId q = assign[static_cast<std::size_t>(w)];
            if (q >= 0 && !conn(p, q)) {
                ++bad;
            }
        }
        return bad;
    }

    // Reduction in cut edges (positive == fewer cuts) if u moves to `target`.
    int cut_gain(NodeId u, PartId target) const {
        const PartId cur = assign[static_cast<std::size_t>(u)];
        int gain = 0;
        for (NodeId w : nbrs(u)) {
            const PartId q = assign[static_cast<std::size_t>(w)];
            if (q < 0) {
                continue;
            }
            gain += static_cast<int>(cur != q) - static_cast<int>(target != q);
        }
        return gain;
    }

    void active_add(NodeId u) {
        if (active_pos[static_cast<std::size_t>(u)] >= 0) {
            return;
        }
        active_pos[static_cast<std::size_t>(u)] = static_cast<int>(active.size());
        active.push_back(u);
    }

    void active_remove(NodeId u) {
        const int pos = active_pos[static_cast<std::size_t>(u)];
        if (pos < 0) {
            return;
        }
        const NodeId last = active.back();
        active[static_cast<std::size_t>(pos)] = last;
        active_pos[static_cast<std::size_t>(last)] = pos;
        active.pop_back();
        active_pos[static_cast<std::size_t>(u)] = -1;
    }

    // Rebuild node_bad / total_bad / active from the current (fully assigned)
    // state.  Uses the deduplicated adjacency so it stays consistent with the
    // incremental updates in move_node(); each undirected edge is visited once.
    void build_conflicts() {
        std::fill(node_bad.begin(), node_bad.end(), 0);
        std::fill(active_pos.begin(), active_pos.end(), -1);
        active.clear();
        total_bad = 0;

        for (NodeId u = 0; u < V; ++u) {
            const PartId pu = assign[static_cast<std::size_t>(u)];
            for (NodeId w : nbrs(u)) {
                if (w <= u) {
                    continue;  // visit each undirected pair once
                }
                const PartId pw = assign[static_cast<std::size_t>(w)];
                if (pu >= 0 && pw >= 0 && !conn(pu, pw)) {
                    ++node_bad[static_cast<std::size_t>(u)];
                    ++node_bad[static_cast<std::size_t>(w)];
                    ++total_bad;
                }
            }
        }
        for (NodeId u = 0; u < V; ++u) {
            if (node_bad[static_cast<std::size_t>(u)] > 0 && !net.is_fixed(u)) {
                active_add(u);
            }
        }
    }

    // Move u to partition np, incrementally maintaining every invariant.
    // Cost is O(degree(u)).
    void move_node(NodeId u, PartId np) {
        const PartId op = assign[static_cast<std::size_t>(u)];
        if (op == np) {
            return;
        }
        for (NodeId w : nbrs(u)) {
            const PartId q = assign[static_cast<std::size_t>(w)];
            if (q < 0) {
                continue;
            }
            const bool before = !conn(op, q);
            const bool after = !conn(np, q);
            if (before == after) {
                continue;
            }
            if (before) {  // edge (u,w) was violating, now satisfied
                --node_bad[static_cast<std::size_t>(u)];
                if (--node_bad[static_cast<std::size_t>(w)] == 0 && !net.is_fixed(w)) {
                    active_remove(w);
                }
                --total_bad;
            } else {       // edge (u,w) becomes a new violation
                ++node_bad[static_cast<std::size_t>(u)];
                if (++node_bad[static_cast<std::size_t>(w)] == 1 && !net.is_fixed(w)) {
                    active_add(w);
                }
                ++total_bad;
            }
        }
        --sizes[static_cast<std::size_t>(op)];
        ++sizes[static_cast<std::size_t>(np)];
        assign[static_cast<std::size_t>(u)] = np;

        if (!net.is_fixed(u)) {
            if (node_bad[static_cast<std::size_t>(u)] > 0) {
                active_add(u);
            } else {
                active_remove(u);
            }
        }
    }

    // -- Phase 1a: seat fixed nodes ----------------------------------------
    bool setup_fixed(std::string& message) {
        for (NodeId u = 0; u < V; ++u) {
            const PartId fixed = net.fixed_part()[static_cast<std::size_t>(u)];
            if (fixed < 0) {
                continue;
            }
            if (!cand_ok(u, fixed)) {
                message = "Fixed node is outside its propagated candidate set";
                return false;
            }
            assign[static_cast<std::size_t>(u)] = fixed;
            if (++sizes[static_cast<std::size_t>(fixed)] > upper) {
                message = "Fixed constraints exceed partition upper capacity";
                return false;
            }
        }
        return true;
    }

    // -- Phase 1b: greedy seed (min local violations, only upper bound) -----
    bool greedy_init(std::string& message) {
        std::vector<NodeId> order;
        order.reserve(static_cast<std::size_t>(V));
        for (NodeId u = 0; u < V; ++u) {
            if (assign[static_cast<std::size_t>(u)] < 0) {
                order.push_back(u);
            }
        }
        std::shuffle(order.begin(), order.end(), rng);

        for (NodeId u : order) {
            PartId best = -1;
            int best_bad = std::numeric_limits<int>::max();
            int best_size = -1;
            for (PartId p = 0; p < K; ++p) {
                if (!cand_ok(u, p) || sizes[static_cast<std::size_t>(p)] >= upper) {
                    continue;
                }
                const int bad = incident_bad(u, p);
                const int size = sizes[static_cast<std::size_t>(p)];
                // Prefer fewest induced violations, then the *largest* existing
                // partition.  Clustering onto a few mutually-adjacent FPGAs is
                // what lets a topology-constrained instance reach 0 violations;
                // spreading would force cross-FPGA edges to violate.
                if (bad < best_bad || (bad == best_bad && size > best_size)) {
                    best = p;
                    best_bad = bad;
                    best_size = size;
                }
            }
            if (best < 0) {
                message = "No capacity-feasible partition for node " + std::to_string(u);
                return false;
            }
            assign[static_cast<std::size_t>(u)] = best;
            ++sizes[static_cast<std::size_t>(best)];
        }
        return true;
    }

    // -- Phase 1b' (balance active): topology-aware balanced region growth --
    // Used instead of greedy_init when a real lower bound exists.  Grows one
    // connected region per partition outward from a seed, pulling in netlist
    // neighbours ONLY where the placement creates no topology violation, and
    // round-robins so every partition climbs toward `lower` together (a
    // balanced wavefront).  The point is to hand balance_fill / fm_refine a
    // balanced, violation-free start instead of the corner-packed cluster the
    // greedy produces — that corner-packing is exactly what left ~20 MFS2
    // partitions below the floor and made the instance look infeasible.
    //
    // grow_target_pct scales how far past the floor stage B keeps growing the
    // violation-free wavefront (see below): 0 == stop at the floor (the old
    // behaviour), 100 == grow toward the balance midpoint V/K.
    void seeded_growth(long long grow_target_pct) {
        // Placing w on p is legal iff every already-assigned neighbour of w
        // sits on p or a topology-adjacent partition (so no edge violates).
        auto legal = [&](NodeId w, PartId p) {
            for (NodeId x : nbrs(w)) {
                const PartId q = assign[static_cast<std::size_t>(x)];
                if (q >= 0 && !conn(p, q)) {
                    return false;
                }
            }
            return true;
        };

        // Seed empty partitions in a TOPOLOGY-COHERENT order so that
        // netlist-adjacent regions land on topology-adjacent FPGAs (otherwise
        // every place where two regions meet violates).  Two parts:
        //
        //   (a) Visit order = multi-source BFS over the topology, sourced from
        //       the FPGAs that already hold fixed nodes (the anchors).  Each
        //       FPGA is thus reached via a topology-adjacent predecessor.  With
        //       no fixed anchors anywhere, start from the highest-degree FPGA.
        //   (b) Seed each empty FPGA p from the netlist *boundary* of the
        //       already-placed regions sitting on FPGAs equal/adjacent to p, so
        //       p's region grows out of a topology-adjacent neighbour's region.
        //       Fall back to a scattered candidate-legal node when no such
        //       boundary node exists (first source, disconnected netlist).
        std::vector<PartId> visit;
        visit.reserve(static_cast<std::size_t>(K));
        {
            std::vector<char> queued(static_cast<std::size_t>(K), 0);
            std::deque<PartId> bfsq;
            for (PartId p = 0; p < K; ++p) {
                if (sizes[static_cast<std::size_t>(p)] > 0) {
                    bfsq.push_back(p);
                    queued[static_cast<std::size_t>(p)] = 1;
                }
            }
            if (bfsq.empty()) {
                PartId best = 0;
                for (PartId p = 1; p < K; ++p) {
                    if (topo.adjacency()[static_cast<std::size_t>(p)].size() >
                        topo.adjacency()[static_cast<std::size_t>(best)].size()) {
                        best = p;
                    }
                }
                bfsq.push_back(best);
                queued[static_cast<std::size_t>(best)] = 1;
            }
            while (!bfsq.empty()) {
                const PartId p = bfsq.front();
                bfsq.pop_front();
                visit.push_back(p);
                for (PartId q : topo.adjacency()[static_cast<std::size_t>(p)]) {
                    if (!queued[static_cast<std::size_t>(q)]) {
                        queued[static_cast<std::size_t>(q)] = 1;
                        bfsq.push_back(q);
                    }
                }
            }
            for (PartId p = 0; p < K; ++p) {  // disconnected topology pieces
                if (!queued[static_cast<std::size_t>(p)]) {
                    visit.push_back(p);
                }
            }
        }

        // `placed` tracks currently-assigned nodes.  At seeding time this is
        // tiny (fixed nodes + one seed per FPGA so far), so boundary search is
        // cheap even though it rescans the list for every new seed.
        std::vector<NodeId> placed;
        placed.reserve(static_cast<std::size_t>(V));
        for (NodeId u = 0; u < V; ++u) {
            if (assign[static_cast<std::size_t>(u)] >= 0) {
                placed.push_back(u);
            }
        }

        // Scattered fallback order (also used to seed the very first region).
        std::vector<NodeId> order(static_cast<std::size_t>(V));
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), rng);

        auto place_seed = [&](NodeId w, PartId p) {
            assign[static_cast<std::size_t>(w)] = p;
            ++sizes[static_cast<std::size_t>(p)];
            placed.push_back(w);
        };

        for (PartId p : visit) {
            if (sizes[static_cast<std::size_t>(p)] > 0) {
                continue;
            }
            bool seeded = false;
            // (b) Boundary seed: an unassigned neighbour of a placed node that
            // sits on an FPGA equal/adjacent to p.
            for (NodeId u : placed) {
                if (!conn(p, assign[static_cast<std::size_t>(u)])) {
                    continue;
                }
                for (NodeId w : nbrs(u)) {
                    if (assign[static_cast<std::size_t>(w)] < 0 && cand_ok(w, p) && legal(w, p)) {
                        place_seed(w, p);
                        seeded = true;
                        break;
                    }
                }
                if (seeded) {
                    break;
                }
            }
            if (seeded) {
                continue;
            }
            // Fallback: first scattered candidate-legal node.
            for (NodeId s : order) {
                if (assign[static_cast<std::size_t>(s)] < 0 && cand_ok(s, p) && legal(s, p)) {
                    place_seed(s, p);
                    break;
                }
            }
        }

        // Frontier per partition: unassigned neighbours of its current members.
        std::vector<std::deque<NodeId>> frontier(static_cast<std::size_t>(K));
        for (NodeId u = 0; u < V; ++u) {
            const PartId p = assign[static_cast<std::size_t>(u)];
            if (p < 0) {
                continue;
            }
            for (NodeId w : nbrs(u)) {
                if (assign[static_cast<std::size_t>(w)] < 0) {
                    frontier[static_cast<std::size_t>(p)].push_back(w);
                }
            }
        }

        // Round-robin violation-free growth up to `target`.  Each round, every
        // partition still below `target` claims a small batch from its frontier;
        // legality is re-checked at pop time because neighbours may have been
        // claimed since.  Growth is legal()-gated, so it can NEVER create a
        // violation, and it only ever adds nodes, so it can never push a
        // partition below its current size — `target` is always safe to raise.
        constexpr int kBatch = 64;
        auto grow_to = [&](int target) {
            bool progress = true;
            int tick = 0;
            while (progress) {
                if (((++tick) & 63) == 0 && timed_out()) {
                    break;
                }
                progress = false;
                for (PartId p = 0; p < K; ++p) {
                    if (sizes[static_cast<std::size_t>(p)] >= target) {
                        continue;
                    }
                    auto& fr = frontier[static_cast<std::size_t>(p)];
                    int budget = kBatch;
                    while (budget-- > 0 && !fr.empty() &&
                           sizes[static_cast<std::size_t>(p)] < target) {
                        const NodeId w = fr.front();
                        fr.pop_front();
                        if (assign[static_cast<std::size_t>(w)] >= 0 || !cand_ok(w, p) ||
                            !legal(w, p)) {
                            continue;
                        }
                        assign[static_cast<std::size_t>(w)] = p;
                        ++sizes[static_cast<std::size_t>(p)];
                        progress = true;
                        for (NodeId x : nbrs(w)) {
                            if (assign[static_cast<std::size_t>(x)] < 0) {
                                fr.push_back(x);
                            }
                        }
                    }
                }
            }
        };

        // Stage A: balanced wavefront to the floor — the multi-way partition
        // baseline that makes the lower bound reachable.
        grow_to(lower);

        // Stage B: keep the SAME violation-free wavefront flowing past the floor
        // toward the balance midpoint V/K, so far fewer nodes fall through to the
        // violation-creating fill_remaining.  This is the dominant lever: it is
        // what collapses the ~104k construction start on MFS2.  The target is
        // balance-neutral (<= V/K <= upper), so balance_fill can still seat every
        // partition inside [lower, upper] afterward.  grow_target_pct scales the
        // (midpoint - floor) gap: 0 reproduces the old stop-at-floor behaviour.
        {
            long long gap = static_cast<long long>(V) -
                            static_cast<long long>(K) * static_cast<long long>(lower);
            if (gap < 0) {
                gap = 0;
            }
            int target = lower + static_cast<int>(gap * grow_target_pct / 100 / K);
            if (target > upper) {
                target = upper;
            }
            if (target > lower) {
                grow_to(target);
            }
        }
    }

    // -- Phase 1c (balance active): assign whatever growth could not reach ---
    // Growth has already laid a connected, violation-free floor region on every
    // partition.  The free majority (everything above the floors) should now
    // CLUSTER for low cut, not scatter — otherwise balancing needlessly inflates
    // the cut.  Preference: fewest induced violations, then cover any floor
    // growth missed, then most neighbours already there, then the LARGEST
    // partition (so the free mass coalesces instead of spreading thin).
    bool fill_remaining(std::string& message) {
        std::vector<NodeId> order;
        order.reserve(static_cast<std::size_t>(V));
        for (NodeId u = 0; u < V; ++u) {
            if (assign[static_cast<std::size_t>(u)] < 0) {
                order.push_back(u);
            }
        }
        std::shuffle(order.begin(), order.end(), rng);

        for (NodeId u : order) {
            PartId best = -1;
            int best_bad = std::numeric_limits<int>::max();
            int best_under = -1;   // 1 if target is below the floor (prefer)
            int best_same = -1;    // neighbours already on target (cut proxy)
            int best_size = -1;    // prefer the LARGER partition (cluster)
            for (PartId p = 0; p < K; ++p) {
                if (!cand_ok(u, p) || sizes[static_cast<std::size_t>(p)] >= upper) {
                    continue;
                }
                const int bad = incident_bad(u, p);
                const int under = sizes[static_cast<std::size_t>(p)] < lower ? 1 : 0;
                int same = 0;
                for (NodeId w : nbrs(u)) {
                    if (assign[static_cast<std::size_t>(w)] == p) {
                        ++same;
                    }
                }
                const int size = sizes[static_cast<std::size_t>(p)];
                // Lexicographic preference: fewest induced violations, then fill
                // floors, then minimise cut, then coalesce on the larger part.
                const bool better =
                    best < 0 ||
                    bad < best_bad ||
                    (bad == best_bad && under > best_under) ||
                    (bad == best_bad && under == best_under && same > best_same) ||
                    (bad == best_bad && under == best_under && same == best_same &&
                     size > best_size);
                if (better) {
                    best = p;
                    best_bad = bad;
                    best_under = under;
                    best_same = same;
                    best_size = size;
                }
            }
            if (best < 0) {
                message = "No capacity-feasible partition for node " + std::to_string(u);
                return false;
            }
            assign[static_cast<std::size_t>(u)] = best;
            ++sizes[static_cast<std::size_t>(best)];
        }
        return true;
    }

    // -- Phase 2: active-queue min-conflicts (relaxed balance) -------------
    // Drives topology violations toward 0.  O(degree) per step.  Greedy
    // min-conflict moves (fewest induced violations, ties by cut gain) plus a
    // small random-walk noise so it can climb out of the local minima that a
    // spread-out (balanced) start tends to create.  A short tabu lock stops
    // plateau ping-ponging; the deadline and move cap keep it bounded.
    void min_conflicts() {
        // Move cap is a safety backstop only; the phase deadline is the real
        // limiter (at the default budget min_conflicts is deadline-bound, and a
        // larger TOPO_BUDGET_MS should buy proportionally more search rather
        // than hit an artificial wall).
        const long long max_moves = 2000LL * static_cast<long long>(V);
        long long step = 0;
        int tick = 0;

        while (!active.empty() && step < max_moves) {
            ++step;
            if (((++tick) & 4095) == 0 && timed_out()) {
                break;
            }

            const std::size_t idx =
                static_cast<std::size_t>(rng() % static_cast<std::uint64_t>(active.size()));
            const NodeId u = active[idx];
            const PartId cur = assign[static_cast<std::size_t>(u)];
            const int cur_bad = node_bad[static_cast<std::size_t>(u)];

            // Soft floor guard: a partition at/below `lower` may be drained ONLY
            // by a strict violation-reducing move (below), never by a lateral,
            // cut-greedy, or noise move — those are what collapse the floor
            // regions growth built.  Strict repairs are rare and any small
            // deficit is refilled by balance_fill, so violations still reach 0
            // while the floors hold.
            const bool floor_locked =
                lower > 0 && sizes[static_cast<std::size_t>(cur)] <= lower;

            // Random-walk noise (~4%): jump u to a random capacity-legal
            // candidate, reservoir-sampled, to escape local minima.
            if (!floor_locked && (rng() % 100) < 4) {
                PartId pick = -1;
                int seen = 0;
                for (PartId p = 0; p < K; ++p) {
                    if (p == cur || !cand_ok(u, p) ||
                        sizes[static_cast<std::size_t>(p)] >= upper) {
                        continue;
                    }
                    if (rng() % static_cast<std::uint64_t>(++seen) == 0) {
                        pick = p;
                    }
                }
                if (pick >= 0) {
                    move_node(u, pick);
                }
                continue;
            }

            PartId best_improve = cur;
            int best_improve_bad = cur_bad;
            int best_improve_gain = 0;
            PartId best_lateral = -1;
            int best_lateral_gain = std::numeric_limits<int>::min();

            for (PartId p = 0; p < K; ++p) {
                if (p == cur || !cand_ok(u, p) || sizes[static_cast<std::size_t>(p)] >= upper) {
                    continue;
                }
                const int bad = incident_bad(u, p);
                const int gain = cut_gain(u, p);
                if (bad < best_improve_bad ||
                    (bad == best_improve_bad && gain > best_improve_gain)) {
                    best_improve = p;
                    best_improve_bad = bad;
                    best_improve_gain = gain;
                }
                if (bad == cur_bad && gain > best_lateral_gain) {
                    best_lateral = p;
                    best_lateral_gain = gain;
                }
            }

            if (best_improve_bad < cur_bad) {
                // Strictly fewer violations: always take it (floors may dip; a
                // small deficit is cheaper to refill than a violation to clear).
                move_node(u, best_improve);
                tabu_until[static_cast<std::size_t>(u)] = static_cast<int>(step) + 7;
            } else if (best_lateral >= 0 && !floor_locked &&
                       tabu_until[static_cast<std::size_t>(u)] <= step) {
                // Plateau move (equal violations) — allowed only when not tabu,
                // which is what breaks the ping-pong cycle.
                move_node(u, best_lateral);
                tabu_until[static_cast<std::size_t>(u)] = static_cast<int>(step) + 7;
            } else {
                // Nothing useful right now; lock u briefly so we stop re-picking it.
                tabu_until[static_cast<std::size_t>(u)] = static_cast<int>(step) + 7;
            }
        }
    }

    // -- Phase 3 (stage 1): balance fill, soft penalty lambda = 1e6 --------
    // Fills under-full partitions (and drains any over-full ones) using only
    // moves that do not create topology violations.  Incremental and bounded.
    void balance_fill() {
        if (lower <= 0) {
            return;
        }
        constexpr double kLambda = 1.0e6;
        const int max_pass = std::max(2 * K, 16);

        for (int pass = 0; pass < max_pass; ++pass) {
            bool under = false;
            bool over = false;
            for (PartId p = 0; p < K; ++p) {
                if (sizes[static_cast<std::size_t>(p)] < lower) under = true;
                if (sizes[static_cast<std::size_t>(p)] > upper) over = true;
            }
            if ((!under && !over) || timed_out()) {
                return;
            }

            std::vector<NodeId> order(static_cast<std::size_t>(V));
            std::iota(order.begin(), order.end(), 0);
            std::shuffle(order.begin(), order.end(), rng);

            bool moved = false;
            for (NodeId u : order) {
                if (net.is_fixed(u)) {
                    continue;
                }
                const PartId cur = assign[static_cast<std::size_t>(u)];
                const bool cur_over = sizes[static_cast<std::size_t>(cur)] > upper;
                const bool donor_ok = sizes[static_cast<std::size_t>(cur)] - 1 >= lower;
                if (!cur_over && !donor_ok) {
                    continue;  // moving u would push its current partition below `lower`
                }

                PartId best = cur;
                double best_cost = 0.0;  // baseline: staying put
                for (PartId p = 0; p < K; ++p) {
                    if (p == cur || !cand_ok(u, p)) {
                        continue;
                    }
                    if (sizes[static_cast<std::size_t>(p)] + 1 > upper) {
                        continue;
                    }
                    const bool target_under = sizes[static_cast<std::size_t>(p)] < lower;
                    if (!target_under && !cur_over) {
                        continue;  // only move to help balance
                    }
                    const int dviol = incident_bad(u, p) - node_bad[static_cast<std::size_t>(u)];
                    const int gain = cut_gain(u, p);
                    double cost = -static_cast<double>(gain) + kLambda * static_cast<double>(dviol);
                    if (target_under) cost -= 1000.0;  // reward filling an under-full part
                    if (cur_over) cost -= 1000.0;      // reward draining an over-full part
                    if (cost < best_cost) {
                        best_cost = cost;
                        best = p;
                    }
                }

                if (best != cur) {
                    move_node(u, best);
                    moved = true;
                }
            }

            if (!moved) {
                return;  // no zero-violation balancing move available -> stop
            }
        }
    }

    // -- Phase 3 (stage 2): soft-penalty FM refinement ---------------------
    // Cost = Cut + lambda * Violations, lambda = 1e6.  Minimizes cut while the
    // heavy penalty keeps violations pinned at 0 and balance inside its window.
    void fm_refine(int max_passes) {
        constexpr double kLambda = 1.0e6;
        std::vector<NodeId> order(static_cast<std::size_t>(V));
        std::iota(order.begin(), order.end(), 0);

        for (int pass = 0; pass < max_passes; ++pass) {
            if (timed_out()) {
                break;
            }
            std::shuffle(order.begin(), order.end(), rng);
            bool improved = false;

            for (NodeId u : order) {
                if (net.is_fixed(u)) {
                    continue;
                }
                const PartId cur = assign[static_cast<std::size_t>(u)];
                if (sizes[static_cast<std::size_t>(cur)] - 1 < lower) {
                    continue;  // keep donor inside the balance window
                }

                PartId best = cur;
                double best_cost = 0.0;
                for (PartId p = 0; p < K; ++p) {
                    if (p == cur || !cand_ok(u, p)) {
                        continue;
                    }
                    if (sizes[static_cast<std::size_t>(p)] + 1 > upper) {
                        continue;
                    }
                    const int dviol = incident_bad(u, p) - node_bad[static_cast<std::size_t>(u)];
                    const int gain = cut_gain(u, p);
                    const double cost =
                        -static_cast<double>(gain) + kLambda * static_cast<double>(dviol);
                    if (cost < best_cost) {
                        best_cost = cost;
                        best = p;
                    }
                }

                if (best != cur) {
                    move_node(u, best);
                    improved = true;
                }
            }

            if (!improved) {
                break;
            }
        }
    }

    // -- ILS perturbation: random legal kick-moves on the violating set ------
    // Bounces up to `kick_size` currently-violating nodes to a random
    // candidate-legal, capacity-OK partition so the next min_conflicts descends
    // from a different basin (escaping the plateau a pure descent gets stuck on).
    // Guards mirror min_conflicts: never drains a partition at/below `lower`
    // (so the floor holds) and never overfills past `upper`.  move_node keeps
    // every incremental invariant, so min_conflicts can run immediately after.
    void kick_perturb(int kick_size) {
        if (active.empty() || kick_size <= 0) {
            return;
        }
        // Snapshot the violating nodes first; `active` mutates as we move them.
        std::vector<NodeId> pool(active.begin(), active.end());
        std::shuffle(pool.begin(), pool.end(), rng);
        const int n = std::min(kick_size, static_cast<int>(pool.size()));
        for (int i = 0; i < n; ++i) {
            const NodeId u = pool[static_cast<std::size_t>(i)];
            if (net.is_fixed(u)) {
                continue;  // fixed nodes never enter `active`, but be explicit
            }
            const PartId cur = assign[static_cast<std::size_t>(u)];
            if (lower > 0 && sizes[static_cast<std::size_t>(cur)] <= lower) {
                continue;  // donor guard: do not break the floor
            }
            PartId pick = -1;
            int seen = 0;
            for (PartId p = 0; p < K; ++p) {
                if (p == cur || !cand_ok(u, p) ||
                    sizes[static_cast<std::size_t>(p)] >= upper) {
                    continue;
                }
                if (rng() % static_cast<std::uint64_t>(++seen) == 0) {
                    pick = p;  // reservoir sample a random legal target
                }
            }
            if (pick >= 0) {
                move_node(u, pick);
            }
        }
    }
};

}  // namespace

Partitioner::Partitioner(const TopologyGraph& topology,
                         const Netlist& netlist,
                         const CandidateMask& candidates)
    : topology_(topology), netlist_(netlist), candidates_(candidates) {}

BalanceLimits Partitioner::compute_balance_limits(double r) const {
    const int k = topology_.num_nodes();
    const int v = netlist_.num_nodes();
    require_or_throw(r >= 0.0, "Balance ratio r must be non-negative");
    require_or_throw(r <= 1.0 / static_cast<double>(k) + 1e-12,
                     "Balance ratio r must be in [0, 1/N]");

    BalanceLimits limits;
    limits.perfect_balance = std::abs(r * static_cast<double>(k) - 1.0) <= 1e-12;
    if (limits.perfect_balance) {
        limits.lower = v / k;
        limits.upper = (v + k - 1) / k;
    } else {
        limits.lower = static_cast<int>(std::floor(r * static_cast<double>(v) + 1e-12));
        limits.upper = v - limits.lower * (k - 1);
    }
    require_or_throw(limits.lower <= limits.upper, "Infeasible balance limits");
    return limits;
}

PartitionResult Partitioner::run_parallel(const PartitionConfig& config,
                                          double& single_thread_seconds,
                                          double& multi_thread_seconds,
                                          double& speedup_ratio) const {
    const int threads = std::max(1, config.num_threads);

    // The search is best-of-N independent attempts (no shared state, no locks).
    // To report an honest speedup we must time the SAME workload both ways:
    //   * sequential baseline: `threads` attempts run one after another;
    //   * parallel: `threads` attempts, one per worker thread.
    // speedup = T_seq(threads) / T_par(threads).  (The old code timed ONE run
    // against `threads` runs, so its ratio could not exceed ~1 by construction.)
    // The two batches use disjoint seeds, so the 2*threads attempts all count
    // toward final quality — the baseline is not wasted work.

    // -- Sequential baseline -------------------------------------------------
    const long long seq_start = now_microseconds();
    PartitionResult best =
        run_single(config.seed, config.balance_ratio, config.max_passes);
    for (int t = 1; t < threads; ++t) {
        PartitionResult r =
            run_single(config.seed + static_cast<std::uint64_t>(t),
                       config.balance_ratio, config.max_passes);
        if (result_better(r, best)) {
            best = std::move(r);
        }
    }
    single_thread_seconds =
        static_cast<double>(now_microseconds() - seq_start) / 1'000'000.0;

    // -- Parallel batch (disjoint seeds) -------------------------------------
    const std::uint64_t par_base = config.seed + static_cast<std::uint64_t>(threads);
    const long long par_start = now_microseconds();
    std::vector<std::thread> workers;
    std::vector<PartitionResult> results(static_cast<std::size_t>(threads));
    workers.reserve(static_cast<std::size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
            results[static_cast<std::size_t>(t)] =
                run_single(par_base + static_cast<std::uint64_t>(t),
                           config.balance_ratio, config.max_passes);
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    multi_thread_seconds =
        static_cast<double>(now_microseconds() - par_start) / 1'000'000.0;

    for (const auto& result : results) {
        if (result_better(result, best)) {
            best = result;
        }
    }

    speedup_ratio =
        multi_thread_seconds > 0.0 ? single_thread_seconds / multi_thread_seconds : 0.0;
    return best;
}

PartitionResult Partitioner::run_single(std::uint64_t seed, double r, int max_passes) const {
    PartitionResult result;
    const BalanceLimits limits = compute_balance_limits(r);
    std::mt19937_64 rng(seed);

    // Per-run safety budget, split across phases so the violation-killer
    // (min_conflicts) cannot starve the floor-filler (balance_fill) or the cut
    // optimiser (fm_refine).  Each phase gets a deadline; the phase loops poll
    // timed_out() against it.
    const long long t0 = now_microseconds();
    auto env_ll = [](const char* name, long long fallback) -> long long {
        const char* v = std::getenv(name);
        if (v == nullptr) {
            return fallback;
        }
        const long long parsed = std::strtoll(v, nullptr, 10);
        return parsed > 0 ? parsed : fallback;
    };
    // Like env_ll but accepts 0 (and any non-negative value).  Needed for knobs
    // where 0 is a meaningful setting rather than "unset" — e.g. a 0% growth
    // target reproduces the old stop-at-floor construction exactly.
    auto env_ll_nonneg = [](const char* name, long long fallback) -> long long {
        const char* v = std::getenv(name);
        if (v == nullptr) {
            return fallback;
        }
        char* end = nullptr;
        const long long parsed = std::strtoll(v, &end, 10);
        return (end != v && parsed >= 0) ? parsed : fallback;
    };
    // Tunable per-run budget and phase splits (defaults baked for the 3s target).
    // Phase deadlines are cumulative fractions of the budget and must be ordered
    // cons_pct <= mc_pct <= ils_pct <= bf_pct <= 100.
    const long long budget = env_ll("TOPO_BUDGET_MS", 3000) * 1000;
    const long long cons_pct = env_ll("TOPO_CONS_PCT", 35);  // construction deadline
    const long long mc_pct = env_ll("TOPO_MC_PCT", 60);   // first min_conflicts deadline
    const long long ils_pct = env_ll("TOPO_ILS_PCT", 85);  // ILS loop deadline
    const long long bf_pct = env_ll("TOPO_BF_PCT", 92);   // balance_fill deadline
    // Construction: how far past the floor stage-B growth fills (percent of the
    // floor->midpoint gap).  100 == grow to V/K; 0 == old stop-at-floor.
    const long long grow_target_pct = env_ll_nonneg("TOPO_GROW_TARGET_PCT", 100);
    Engine engine(topology_, netlist_, candidates_, rng, limits.lower, limits.upper,
                  t0 + budget);

    const bool debug = std::getenv("TOPO_DEBUG") != nullptr;
    auto trace = [&](const char* phase) {
        if (debug) {
            std::fprintf(stderr,
                         "  [%-12s] viol=%-7lld under_floor=%-3d t=%.3fs\n", phase,
                         engine.total_bad, engine.under_floor(),
                         static_cast<double>(now_microseconds() - t0) / 1e6);
        }
    };

    std::string message;
    bool init_ok = engine.setup_fixed(message);
    if (init_ok) {
        if (limits.lower > 0) {
            // Balance is active: build a balanced, topology-legal start so the
            // floor is reachable.  (Clustering greedy would corner-pack here.)
            engine.set_deadline(t0 + budget * cons_pct / 100);
            engine.seeded_growth(grow_target_pct);
            init_ok = engine.fill_remaining(message);
        } else {
            // No floor (r == 0): clustering is optimal — keep it.
            init_ok = engine.greedy_init(message);
        }
    }
    if (init_ok) {
        engine.build_conflicts();
        trace("construct");
        engine.set_deadline(t0 + budget * mc_pct / 100);
        engine.min_conflicts();          // relaxed balance: violations -> 0
        trace("min_conflict");

        // ILS: if a residual violation plateau remains, escape it by perturbing
        // the violating set and re-descending, always keeping the best state.
        // The authoritative state is assign+sizes; everything else (node_bad,
        // total_bad, active, active_pos) is rebuilt by build_conflicts(), and
        // tabu_until is reset (it is keyed to min_conflicts' per-call step).
        if (engine.total_bad > 0) {
            const long long ils_deadline = t0 + budget * ils_pct / 100;
            std::vector<PartId> best_assign = engine.assign;
            std::vector<int> best_sizes = engine.sizes;
            long long best_bad = engine.total_bad;
            const int kick_base = std::max(1, static_cast<int>(engine.active.size()) / 20);
            constexpr int kKickCap = 400;
            int kick = kick_base;
            int rounds = 0;
            while (engine.total_bad > 0 && now_microseconds() < ils_deadline &&
                   !engine.active.empty()) {
                engine.set_deadline(ils_deadline);
                engine.kick_perturb(kick);
                engine.min_conflicts();
                ++rounds;
                if (engine.total_bad < best_bad) {
                    best_bad = engine.total_bad;
                    best_assign = engine.assign;
                    best_sizes = engine.sizes;
                    kick = kick_base;                     // progress -> reset strength
                } else {
                    engine.assign = best_assign;          // restore best...
                    engine.sizes = best_sizes;
                    engine.build_conflicts();             // ...rebuild derived state
                    std::fill(engine.tabu_until.begin(), engine.tabu_until.end(), 0);
                    kick = std::min(kick * 2, kKickCap);  // escalate restart strength
                }
            }
            // Leave the engine on the best state found for the finishers below.
            engine.assign = best_assign;
            engine.sizes = best_sizes;
            engine.build_conflicts();
            std::fill(engine.tabu_until.begin(), engine.tabu_until.end(), 0);
            if (debug) {
                std::fprintf(stderr,
                             "  [%-12s] viol=%-7lld under_floor=%-3d t=%.3fs rounds=%d\n",
                             "ils", engine.total_bad, engine.under_floor(),
                             static_cast<double>(now_microseconds() - t0) / 1e6, rounds);
            }
        }

        engine.set_deadline(t0 + budget * bf_pct / 100);
        engine.balance_fill();           // satisfy lower bound, no new violations
        trace("balance_fill");
        engine.set_deadline(t0 + budget);
        engine.fm_refine(std::max(1, max_passes));  // minimize cut, keep violations 0
        trace("fm_refine");
    }

    result.assignment = std::move(engine.assign);
    result.part_sizes = compute_part_sizes(result.assignment);
    result.cut_cost = compute_cut_cost(result.assignment);
    result.topology_violations = count_topology_violations(result.assignment);
    result.fixed_violations = count_fixed_violations(result.assignment);
    result.feasible = init_ok &&
                      validate_balance(result.part_sizes, limits) &&
                      result.topology_violations == 0 &&
                      result.fixed_violations == 0;

    if (!result.feasible) {
        if (!message.empty()) {
            result.message = message;
        } else if (result.topology_violations != 0) {
            result.message = "Local search left " +
                             std::to_string(result.topology_violations) + " topology violations";
        } else if (!validate_balance(result.part_sizes, limits)) {
            result.message = "Could not satisfy balance constraints without topology violations";
        } else if (result.fixed_violations != 0) {
            result.message = "Fixed-constraint violations remain";
        }
    }
    return result;
}

long long Partitioner::compute_cut_cost(const std::vector<PartId>& assignment) const {
    long long cut = 0;
    for (const auto& [u, v] : netlist_.edges()) {
        if (assignment[static_cast<std::size_t>(u)] != assignment[static_cast<std::size_t>(v)]) {
            ++cut;
        }
    }
    return cut;
}

long long Partitioner::count_topology_violations(const std::vector<PartId>& assignment) const {
    long long violations = 0;
    for (const auto& [u, v] : netlist_.edges()) {
        const PartId pu = assignment[static_cast<std::size_t>(u)];
        const PartId pv = assignment[static_cast<std::size_t>(v)];
        if (pu < 0 || pv < 0 || !topology_.connected_or_same(pu, pv)) {
            ++violations;
        }
    }
    return violations;
}

long long Partitioner::count_fixed_violations(const std::vector<PartId>& assignment) const {
    long long violations = 0;
    for (NodeId u = 0; u < netlist_.num_nodes(); ++u) {
        const PartId fixed = netlist_.fixed_part()[static_cast<std::size_t>(u)];
        if (fixed >= 0 && assignment[static_cast<std::size_t>(u)] != fixed) {
            ++violations;
        }
    }
    return violations;
}

std::vector<int> Partitioner::compute_part_sizes(const std::vector<PartId>& assignment) const {
    std::vector<int> sizes(static_cast<std::size_t>(topology_.num_nodes()), 0);
    for (PartId p : assignment) {
        if (p >= 0 && p < topology_.num_nodes()) {
            ++sizes[static_cast<std::size_t>(p)];
        }
    }
    return sizes;
}

bool Partitioner::validate_balance(const std::vector<int>& sizes, const BalanceLimits& limits) const {
    if (static_cast<int>(sizes.size()) != topology_.num_nodes()) {
        return false;
    }
    int total = 0;
    for (int size : sizes) {
        if (size < limits.lower || size > limits.upper) {
            return false;
        }
        total += size;
    }
    if (total != netlist_.num_nodes()) {
        return false;
    }
    if (limits.perfect_balance) {
        const auto [min_it, max_it] = std::minmax_element(sizes.begin(), sizes.end());
        return *max_it - *min_it <= 1;
    }
    return true;
}

bool Partitioner::result_better(const PartitionResult& a, const PartitionResult& b) const {
    if (a.feasible != b.feasible) {
        return a.feasible;
    }
    if (a.topology_violations != b.topology_violations) {
        return a.topology_violations < b.topology_violations;
    }
    if (a.fixed_violations != b.fixed_violations) {
        return a.fixed_violations < b.fixed_violations;
    }
    if (a.cut_cost != b.cut_cost) {
        return a.cut_cost < b.cut_cost;
    }
    return a.assignment < b.assignment;
}
