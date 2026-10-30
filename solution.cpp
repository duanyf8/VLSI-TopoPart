#include "solution.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

using namespace std;

namespace {

struct HeapEntry {
    int key;
    int degree;
    int vertex;
    int version;

    HeapEntry() : key(0), degree(0), vertex(-1), version(0) {}

    HeapEntry(int k, int d, int v, int ver)
        : key(k), degree(d), vertex(v), version(ver) {}
};

struct HeapEntryCmp {
    bool operator()(const HeapEntry &a, const HeapEntry &b) const {
        if (a.key != b.key) {
            return a.key < b.key;
        }

        if (a.degree != b.degree) {
            return a.degree < b.degree;
        }

        return a.vertex > b.vertex;
    }
};

struct MoveRecord {
    int vertex;
    int old_part;
    int gain;

    MoveRecord(int v, int p, int g)
        : vertex(v), old_part(p), gain(g) {}
};

void ensure_results_dir() {
#ifdef _WIN32
    _mkdir("results");
#else
    mkdir("results", 0755);
#endif
}

string result_path(const string &filename) {
    return string("results/") + filename;
}

string trim_copy(const string &s) {
    size_t l = 0;
    size_t r = s.size();

    while (l < r && isspace(static_cast<unsigned char>(s[l]))) {
        ++l;
    }

    while (r > l && isspace(static_cast<unsigned char>(s[r - 1]))) {
        --r;
    }

    return s.substr(l, r - l);
}

string remove_comment_and_trim(const string &line) {
    size_t p1 = line.find('%');
    size_t p2 = line.find('#');

    size_t p = string::npos;

    if (p1 != string::npos) {
        p = p1;
    }

    if (p2 != string::npos) {
        if (p == string::npos || p2 < p) {
            p = p2;
        }
    }

    string s = line;

    if (p != string::npos) {
        s = line.substr(0, p);
    }

    return trim_copy(s);
}

bool read_valid_line(ifstream &fin, string &line) {
    string raw;

    while (getline(fin, raw)) {
        line = remove_comment_and_trim(raw);

        if (!line.empty()) {
            return true;
        }
    }

    return false;
}

vector<int> parse_ints(const string &line) {
    vector<int> values;
    stringstream ss(line);
    int x;

    while (ss >> x) {
        values.push_back(x);
    }

    return values;
}

string basename_no_ext(const string &path) {
    size_t slash = path.find_last_of("/\\");

    string name;

    if (slash == string::npos) {
        name = path;
    } else {
        name = path.substr(slash + 1);
    }

    size_t dot = name.find_last_of('.');

    if (dot != string::npos) {
        name = name.substr(0, dot);
    }

    return name;
}

string to_upper_copy(string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<char>(
            toupper(static_cast<unsigned char>(s[i]))
        );
    }

    return s;
}

int optimal_cut_2_percent(const string &benchmark_name) {
    static map<string, int> opt;

    if (opt.empty()) {
        opt["IBM01"] = 200;
        opt["IBM02"] = 307;
        opt["IBM03"] = 951;
        opt["IBM04"] = 573;
        opt["IBM05"] = 1706;
        opt["IBM06"] = 962;
        opt["IBM07"] = 878;
        opt["IBM08"] = 1140;
        opt["IBM09"] = 620;
        opt["IBM10"] = 1253;
        opt["IBM11"] = 1051;
        opt["IBM12"] = 1919;
        opt["IBM13"] = 831;
        opt["IBM14"] = 1842;
        opt["IBM15"] = 2730;
        opt["IBM16"] = 1827;
        opt["IBM17"] = 2270;
        opt["IBM18"] = 1521;
    }

    string key = to_upper_copy(benchmark_name);

    if (opt.find(key) == opt.end()) {
        return -1;
    }

    return opt[key];
}

bool better_heap_entry(const HeapEntry &a, const HeapEntry &b) {
    if (a.key != b.key) {
        return a.key > b.key;
    }

    if (a.degree != b.degree) {
        return a.degree > b.degree;
    }

    return a.vertex < b.vertex;
}

}  // namespace


Solution::HGraph::HGraph()
    : n(0), m(0), total_weight(0) {}


Solution::FMResult::FMResult()
    : cut(0), passes(0), seconds(0.0) {}


Solution::MLResult::MLResult()
    : cut(0), levels(0), seconds(0.0) {}


Solution::Solution()
    : n_(0),
      m_(0),
      lower_bound_(0),
      upper_bound_(0),
      best_cut_(numeric_limits<int>::max()) {}


void Solution::build_incidence(HGraph &h) const {
    h.m = static_cast<int>(h.nets.size());

    if (h.net_weight.empty()) {
        h.net_weight.assign(h.m, 1);
    }

    if (static_cast<int>(h.net_weight.size()) != h.m) {
        h.net_weight.assign(h.m, 1);
    }

    if (h.vertex_weight.empty()) {
        h.vertex_weight.assign(h.n, 1);
    }

    h.total_weight = 0;

    for (int i = 0; i < h.n; ++i) {
        h.total_weight += h.vertex_weight[i];
    }

    h.incident.assign(h.n, vector<int>());

    for (int e = 0; e < h.m; ++e) {
        vector<int> &pins = h.nets[e];

        sort(pins.begin(), pins.end());
        pins.erase(unique(pins.begin(), pins.end()), pins.end());

        for (size_t i = 0; i < pins.size(); ++i) {
            int v = pins[i];

            if (v >= 0 && v < h.n) {
                h.incident[v].push_back(e);
            }
        }
    }
}


void Solution::read_benchmark(const string &benchmark_path) {
    input_path_ = benchmark_path;
    benchmark_name_ = basename_no_ext(benchmark_path);

    ifstream fin(benchmark_path.c_str());

    if (!fin) {
        throw runtime_error("Cannot open benchmark file: " + benchmark_path);
    }

    string line;

    if (!read_valid_line(fin, line)) {
        throw runtime_error("Empty benchmark file: " + benchmark_path);
    }

    stringstream header(line);

    string fmt_str;

    if (!(header >> m_ >> n_)) {
        throw runtime_error("Invalid header. Expected: edge_num node_num [fmt]");
    }

    bool edge_weighted = false;

    if (header >> fmt_str) {
        int fmt = atoi(fmt_str.c_str());
        edge_weighted = (fmt % 10 == 1);
    }

    if (n_ <= 0 || m_ < 0) {
        throw runtime_error("Invalid benchmark size.");
    }

    graph_.clear();

    original_ = HGraph();
    original_.n = n_;
    original_.nets.reserve(m_);
    original_.net_weight.reserve(m_);
    original_.vertex_weight.assign(n_, 1);

    for (int i = 0; i < n_; ++i) {
        graph_.add_node(i + 1);
    }

    for (int e = 0; e < m_; ++e) {
        if (!read_valid_line(fin, line)) {
            throw runtime_error("Not enough net lines in benchmark file.");
        }

        vector<int> values = parse_ints(line);

        int start = 0;
        int weight = 1;

        if (edge_weighted) {
            if (values.empty()) {
                throw runtime_error("Invalid weighted net line.");
            }

            weight = values[0];
            start = 1;
        }

        vector<int> pins;

        for (size_t k = static_cast<size_t>(start); k < values.size(); ++k) {
            int node_id = values[k];

            if (node_id < 1 || node_id > n_) {
                stringstream msg;
                msg << "Invalid node id " << node_id
                    << " in net " << e + 1
                    << ". Valid range is 1.." << n_;
                throw runtime_error(msg.str());
            }

            pins.push_back(node_id - 1);
        }

        sort(pins.begin(), pins.end());
        pins.erase(unique(pins.begin(), pins.end()), pins.end());

        Net *net_ptr = graph_.add_net(e + 1);

        for (size_t i = 0; i < pins.size(); ++i) {
            int v = pins[i];

            net_ptr->add_node(graph_.node_at(v));
            graph_.node_at(v)->add_net(net_ptr);
        }

        original_.nets.push_back(pins);
        original_.net_weight.push_back(weight);
    }

    build_incidence(original_);

    /*
     * epsilon = 2%
     * 48% <= |V0| / |V| <= 52%
     */
    lower_bound_ = (48LL * n_ + 99LL) / 100LL;
    upper_bound_ = (52LL * n_) / 100LL;

    best_partition_.clear();
    best_cut_ = numeric_limits<int>::max();

    cout << "[READ] benchmark: " << benchmark_name_ << endl;
    cout << "[READ] vertices : " << n_ << endl;
    cout << "[READ] nets     : " << m_ << endl;
    cout << "[READ] balance  : "
         << lower_bound_ << " <= |V0| <= "
         << upper_bound_ << endl;
}


int Solution::compute_cut(
    const HGraph &h,
    const vector<int> &part
) const {
    int cut = 0;

    for (int e = 0; e < h.m; ++e) {
        const vector<int> &pins = h.nets[e];

        if (pins.size() <= 1) {
            continue;
        }

        bool has0 = false;
        bool has1 = false;

        for (size_t i = 0; i < pins.size(); ++i) {
            int v = pins[i];

            if (part[v] == 0) {
                has0 = true;
            } else {
                has1 = true;
            }

            if (has0 && has1) {
                cut += h.net_weight[e];
                break;
            }
        }
    }

    return cut;
}


long long Solution::part0_weight(
    const HGraph &h,
    const vector<int> &part
) const {
    long long w = 0;

    for (int i = 0; i < h.n; ++i) {
        if (part[i] == 0) {
            w += h.vertex_weight[i];
        }
    }

    return w;
}


bool Solution::is_balanced(
    const HGraph &h,
    const vector<int> &part
) const {
    long long w0 = part0_weight(h, part);
    return lower_bound_ <= w0 && w0 <= upper_bound_;
}


void Solution::compute_counts(
    const HGraph &h,
    const vector<int> &part,
    vector<int> &cnt0,
    vector<int> &cnt1
) const {
    cnt0.assign(h.m, 0);
    cnt1.assign(h.m, 0);

    for (int e = 0; e < h.m; ++e) {
        const vector<int> &pins = h.nets[e];

        int c0 = 0;

        for (size_t i = 0; i < pins.size(); ++i) {
            int v = pins[i];

            if (part[v] == 0) {
                ++c0;
            }
        }

        cnt0[e] = c0;
        cnt1[e] = static_cast<int>(pins.size()) - c0;
    }
}


int Solution::gain_of_vertex(
    const HGraph &h,
    int v,
    const vector<int> &part,
    const vector<int> &cnt0,
    const vector<int> &cnt1
) const {
    int from = part[v];
    int to = 1 - from;

    int gain = 0;

    const vector<int> &inc = h.incident[v];

    for (size_t i = 0; i < inc.size(); ++i) {
        int e = inc[i];

        int c_from = (from == 0) ? cnt0[e] : cnt1[e];
        int c_to = (to == 0) ? cnt0[e] : cnt1[e];

        bool before_cut = (cnt0[e] > 0 && cnt1[e] > 0);

        int after_from = c_from - 1;
        int after_to = c_to + 1;

        bool after_cut = (after_from > 0 && after_to > 0);

        if (before_cut && !after_cut) {
            gain += h.net_weight[e];
        } else if (!before_cut && after_cut) {
            gain -= h.net_weight[e];
        }
    }

    return gain;
}


void Solution::repair_balance(
    const HGraph &h,
    vector<int> &part,
    mt19937 &rng
) const {
    long long w0 = part0_weight(h, part);

    vector<int> order(h.n);
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int safety = 0;

    while (w0 < lower_bound_ && safety < h.n * 4) {
        ++safety;

        int best = -1;
        int best_w = -1;

        for (int i = 0; i < h.n; ++i) {
            int v = order[i];

            if (part[v] == 1 &&
                w0 + h.vertex_weight[v] <= upper_bound_) {
                if (h.vertex_weight[v] > best_w) {
                    best_w = h.vertex_weight[v];
                    best = v;
                }
            }
        }

        if (best == -1) {
            break;
        }

        part[best] = 0;
        w0 += h.vertex_weight[best];
    }

    safety = 0;

    while (w0 > upper_bound_ && safety < h.n * 4) {
        ++safety;

        int best = -1;
        int best_w = -1;

        for (int i = 0; i < h.n; ++i) {
            int v = order[i];

            if (part[v] == 0 &&
                w0 - h.vertex_weight[v] >= lower_bound_) {
                if (h.vertex_weight[v] > best_w) {
                    best_w = h.vertex_weight[v];
                    best = v;
                }
            }
        }

        if (best == -1) {
            break;
        }

        part[best] = 1;
        w0 -= h.vertex_weight[best];
    }
}


vector<int> Solution::make_random_initial(
    const HGraph &h,
    mt19937 &rng
) const {
    vector<int> part(h.n, 1);

    vector<int> order(h.n);
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    long long range = upper_bound_ - lower_bound_ + 1;
    long long target = lower_bound_;

    if (range > 0) {
        target += static_cast<long long>(rng() % static_cast<unsigned int>(range));
    }

    long long w0 = 0;

    for (int i = 0; i < h.n; ++i) {
        int v = order[i];

        if (w0 < target &&
            w0 + h.vertex_weight[v] <= upper_bound_) {
            part[v] = 0;
            w0 += h.vertex_weight[v];
        }
    }

    repair_balance(h, part, rng);

    return part;
}


vector<int> Solution::make_region_initial(
    const HGraph &h,
    int run_id,
    mt19937 &rng
) const {
    vector<int> part(h.n, 1);
    vector<char> selected(h.n, 0);

    vector<int> score(h.n, 0);
    vector<int> version(h.n, 0);

    priority_queue<HeapEntry, vector<HeapEntry>, HeapEntryCmp> pq;

    long long range = upper_bound_ - lower_bound_ + 1;
    long long target = lower_bound_;

    if (range > 0) {
        target += static_cast<long long>(rng() % static_cast<unsigned int>(range));
    }

    auto push_candidate = [&](int v) {
        ++version[v];
        pq.push(
            HeapEntry(
                score[v],
                static_cast<int>(h.incident[v].size()),
                v,
                version[v]
            )
        );
    };

    auto add_vertex = [&](int v, long long &w0) -> bool {
        if (v < 0 || v >= h.n) {
            return false;
        }

        if (selected[v]) {
            return false;
        }

        if (w0 + h.vertex_weight[v] > upper_bound_) {
            return false;
        }

        selected[v] = 1;
        part[v] = 0;
        w0 += h.vertex_weight[v];

        const vector<int> &inc = h.incident[v];

        for (size_t i = 0; i < inc.size(); ++i) {
            int e = inc[i];

            const vector<int> &pins = h.nets[e];

            if (pins.size() > 120) {
                continue;
            }

            int add = max(
                1,
                (h.net_weight[e] * 1000) /
                max(1, static_cast<int>(pins.size()) - 1)
            );

            for (size_t j = 0; j < pins.size(); ++j) {
                int u = pins[j];

                if (!selected[u]) {
                    score[u] += add;
                    push_candidate(u);
                }
            }
        }

        return true;
    };

    auto random_feasible_unselected = [&](long long w0) -> int {
        uniform_int_distribution<int> dist(0, h.n - 1);

        for (int t = 0; t < 2000; ++t) {
            int v = dist(rng);

            if (!selected[v] &&
                w0 + h.vertex_weight[v] <= upper_bound_) {
                return v;
            }
        }

        for (int v = 0; v < h.n; ++v) {
            if (!selected[v] &&
                w0 + h.vertex_weight[v] <= upper_bound_) {
                return v;
            }
        }

        return -1;
    };

    int seed_vertex = -1;

    if (run_id % 3 == 0) {
        uniform_int_distribution<int> dist(0, h.n - 1);
        seed_vertex = dist(rng);
    } else {
        int sample_cnt = min(h.n, 64);
        uniform_int_distribution<int> dist(0, h.n - 1);

        int best = -1;
        int best_deg = -1;

        for (int i = 0; i < sample_cnt; ++i) {
            int v = dist(rng);
            int deg = static_cast<int>(h.incident[v].size());

            if (deg > best_deg) {
                best_deg = deg;
                best = v;
            }
        }

        seed_vertex = best;
    }

    long long w0 = 0;

    add_vertex(seed_vertex, w0);

    while (w0 < target) {
        int v = -1;

        while (!pq.empty()) {
            HeapEntry top = pq.top();
            pq.pop();

            if (selected[top.vertex]) {
                continue;
            }

            if (top.version != version[top.vertex]) {
                continue;
            }

            if (w0 + h.vertex_weight[top.vertex] > upper_bound_) {
                continue;
            }

            v = top.vertex;
            break;
        }

        if (v == -1) {
            v = random_feasible_unselected(w0);
        }

        if (v == -1) {
            break;
        }

        add_vertex(v, w0);
    }

    repair_balance(h, part, rng);

    return part;
}


vector<int> Solution::make_initial_partition(
    const HGraph &h,
    int run_id,
    mt19937 &rng
) const {
    if (run_id % 4 == 0) {
        return make_random_initial(h, rng);
    }

    return make_region_initial(h, run_id, rng);
}


Solution::FMResult Solution::fm_refine(
    const HGraph &h,
    const vector<int> &initial_part,
    int max_passes
) const {
    using chrono::duration;
    using chrono::steady_clock;

    steady_clock::time_point t0 = steady_clock::now();

    vector<int> part = initial_part;

    repair_balance(h, part, const_cast<mt19937 &>(
        *reinterpret_cast<const mt19937 *>(new mt19937(2026))
    ));

    int current_cut = compute_cut(h, part);

    vector<int> pass_cuts;
    pass_cuts.push_back(current_cut);

    int passes_done = 0;

    for (int pass = 1; pass <= max_passes; ++pass) {
        vector<int> cnt0;
        vector<int> cnt1;

        compute_counts(h, part, cnt0, cnt1);

        vector<char> locked(h.n, 0);
        vector<int> version(h.n, 0);

        priority_queue<HeapEntry, vector<HeapEntry>, HeapEntryCmp> heaps[2];

        auto push_vertex = [&](int v) {
            if (locked[v]) {
                return;
            }

            int side = part[v];
            int g = gain_of_vertex(h, v, part, cnt0, cnt1);

            ++version[v];

            heaps[side].push(
                HeapEntry(
                    g,
                    static_cast<int>(h.incident[v].size()),
                    v,
                    version[v]
                )
            );
        };

        for (int v = 0; v < h.n; ++v) {
            push_vertex(v);
        }

        auto get_valid_top = [&](int side, HeapEntry &out) -> bool {
            while (!heaps[side].empty()) {
                HeapEntry top = heaps[side].top();

                if (locked[top.vertex]) {
                    heaps[side].pop();
                    continue;
                }

                if (part[top.vertex] != side) {
                    heaps[side].pop();
                    continue;
                }

                if (top.version != version[top.vertex]) {
                    heaps[side].pop();
                    continue;
                }

                int real_gain = gain_of_vertex(
                    h,
                    top.vertex,
                    part,
                    cnt0,
                    cnt1
                );

                if (real_gain != top.key) {
                    heaps[side].pop();
                    push_vertex(top.vertex);
                    continue;
                }

                out = top;
                return true;
            }

            return false;
        };

        long long w0 = part0_weight(h, part);

        vector<MoveRecord> moves;
        moves.reserve(h.n);

        int cumulative_gain = 0;
        int best_cumulative_gain = numeric_limits<int>::min();
        int best_index = -1;

        vector<int> touched_mark(h.n, 0);
        int stamp = 1;

        int previous_cut = current_cut;

        for (int step = 0; step < h.n; ++step) {
            bool allow_move_from_0 = false;
            bool allow_move_from_1 = false;

            if (w0 > lower_bound_) {
                allow_move_from_0 = true;
            }

            if (w0 < upper_bound_) {
                allow_move_from_1 = true;
            }

            HeapEntry cand0;
            HeapEntry cand1;

            bool has0 = false;
            bool has1 = false;

            if (allow_move_from_0) {
                has0 = get_valid_top(0, cand0);

                while (has0 &&
                       w0 - h.vertex_weight[cand0.vertex] < lower_bound_) {
                    heaps[0].pop();
                    has0 = get_valid_top(0, cand0);
                }
            }

            if (allow_move_from_1) {
                has1 = get_valid_top(1, cand1);

                while (has1 &&
                       w0 + h.vertex_weight[cand1.vertex] > upper_bound_) {
                    heaps[1].pop();
                    has1 = get_valid_top(1, cand1);
                }
            }

            if (!has0 && !has1) {
                break;
            }

            int chosen_side;

            if (has0 && has1) {
                chosen_side = better_heap_entry(cand0, cand1) ? 0 : 1;
            } else if (has0) {
                chosen_side = 0;
            } else {
                chosen_side = 1;
            }

            HeapEntry chosen = (chosen_side == 0) ? cand0 : cand1;

            heaps[chosen_side].pop();

            int v = chosen.vertex;

            if (locked[v] || part[v] != chosen_side) {
                continue;
            }

            int real_gain = gain_of_vertex(h, v, part, cnt0, cnt1);

            if (real_gain != chosen.key) {
                push_vertex(v);
                continue;
            }

            int old_part = part[v];
            int new_part = 1 - old_part;

            part[v] = new_part;

            if (old_part == 0) {
                w0 -= h.vertex_weight[v];
            } else {
                w0 += h.vertex_weight[v];
            }

            const vector<int> &inc = h.incident[v];

            for (size_t i = 0; i < inc.size(); ++i) {
                int e = inc[i];

                if (old_part == 0) {
                    --cnt0[e];
                    ++cnt1[e];
                } else {
                    --cnt1[e];
                    ++cnt0[e];
                }
            }

            locked[v] = 1;

            moves.push_back(MoveRecord(v, old_part, real_gain));

            cumulative_gain += real_gain;

            if (cumulative_gain > best_cumulative_gain) {
                best_cumulative_gain = cumulative_gain;
                best_index = static_cast<int>(moves.size()) - 1;
            }

            vector<int> touched;

            ++stamp;

            if (stamp == numeric_limits<int>::max()) {
                fill(touched_mark.begin(), touched_mark.end(), 0);
                stamp = 1;
            }

            for (size_t i = 0; i < inc.size(); ++i) {
                int e = inc[i];

                const vector<int> &pins = h.nets[e];

                for (size_t j = 0; j < pins.size(); ++j) {
                    int u = pins[j];

                    if (!locked[u] && touched_mark[u] != stamp) {
                        touched_mark[u] = stamp;
                        touched.push_back(u);
                    }
                }
            }

            for (size_t i = 0; i < touched.size(); ++i) {
                push_vertex(touched[i]);
            }
        }

        if (best_index < 0 || best_cumulative_gain <= 0) {
            for (int i = static_cast<int>(moves.size()) - 1; i >= 0; --i) {
                part[moves[i].vertex] = moves[i].old_part;
            }

            break;
        }

        for (int i = static_cast<int>(moves.size()) - 1; i > best_index; --i) {
            part[moves[i].vertex] = moves[i].old_part;
        }

        int new_cut = compute_cut(h, part);

        if (new_cut >= previous_cut) {
            for (int i = best_index; i >= 0; --i) {
                part[moves[i].vertex] = moves[i].old_part;
            }

            current_cut = previous_cut;
            break;
        }

        current_cut = new_cut;
        pass_cuts.push_back(current_cut);
        ++passes_done;

        if (current_cut == 0) {
            break;
        }
    }

    steady_clock::time_point t1 = steady_clock::now();

    FMResult result;
    result.part = part;
    result.cut = current_cut;
    result.passes = passes_done;
    result.seconds = duration<double>(t1 - t0).count();
    result.pass_cuts = pass_cuts;

    return result;
}


Solution::HGraph Solution::coarsen_once(
    const HGraph &fine,
    mt19937 &rng,
    vector<int> &fine_to_coarse
) const {
    HGraph coarse;

    fine_to_coarse.assign(fine.n, -1);

    vector<int> order(fine.n);
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int max_cluster_weight = static_cast<int>(
        max(2LL, fine.total_weight / 500LL)
    );

    vector<int> mark(fine.n, 0);
    vector<int> score(fine.n, 0);
    int stamp = 1;

    vector<int> coarse_weight;

    for (int idx = 0; idx < fine.n; ++idx) {
        int v = order[idx];

        if (fine_to_coarse[v] != -1) {
            continue;
        }

        ++stamp;

        if (stamp == numeric_limits<int>::max()) {
            fill(mark.begin(), mark.end(), 0);
            stamp = 1;
        }

        vector<int> candidates;

        const vector<int> &inc = fine.incident[v];

        for (size_t i = 0; i < inc.size(); ++i) {
            int e = inc[i];

            const vector<int> &pins = fine.nets[e];

            if (pins.size() <= 1 || pins.size() > 120) {
                continue;
            }

            int add = max(
                1,
                (fine.net_weight[e] * 1000) /
                max(1, static_cast<int>(pins.size()) - 1)
            );

            for (size_t j = 0; j < pins.size(); ++j) {
                int u = pins[j];

                if (u == v) {
                    continue;
                }

                if (fine_to_coarse[u] != -1) {
                    continue;
                }

                if (fine.vertex_weight[v] + fine.vertex_weight[u] >
                    max_cluster_weight) {
                    continue;
                }

                if (mark[u] != stamp) {
                    mark[u] = stamp;
                    score[u] = 0;
                    candidates.push_back(u);
                }

                score[u] += add;
            }
        }

        int best_u = -1;
        int best_score = -1;
        int best_degree = -1;

        for (size_t i = 0; i < candidates.size(); ++i) {
            int u = candidates[i];
            int deg = static_cast<int>(fine.incident[u].size());

            if (score[u] > best_score ||
                (score[u] == best_score && deg > best_degree)) {
                best_score = score[u];
                best_degree = deg;
                best_u = u;
            }
        }

        int cid = static_cast<int>(coarse_weight.size());

        fine_to_coarse[v] = cid;

        int cw = fine.vertex_weight[v];

        if (best_u != -1) {
            fine_to_coarse[best_u] = cid;
            cw += fine.vertex_weight[best_u];
        }

        coarse_weight.push_back(cw);
    }

    coarse.n = static_cast<int>(coarse_weight.size());
    coarse.vertex_weight = coarse_weight;

    vector<int> mark_cluster(coarse.n, -1);

    for (int e = 0; e < fine.m; ++e) {
        const vector<int> &pins = fine.nets[e];

        vector<int> cpins;

        for (size_t i = 0; i < pins.size(); ++i) {
            int v = pins[i];
            int c = fine_to_coarse[v];

            if (c < 0) {
                continue;
            }

            if (mark_cluster[c] != e) {
                mark_cluster[c] = e;
                cpins.push_back(c);
            }
        }

        if (cpins.size() >= 2) {
            sort(cpins.begin(), cpins.end());
            coarse.nets.push_back(cpins);
            coarse.net_weight.push_back(fine.net_weight[e]);
        }
    }

    build_incidence(coarse);

    return coarse;
}


Solution::MLResult Solution::multilevel_partition_one_run(
    int run_id,
    int max_passes,
    mt19937 &rng
) const {
    using chrono::duration;
    using chrono::steady_clock;

    steady_clock::time_point t0 = steady_clock::now();

    vector<HGraph> levels;
    vector<vector<int> > maps;

    levels.push_back(original_);

    const int stop_vertices = 700;
    const int max_levels = 30;

    for (int level = 0; level < max_levels; ++level) {
        const HGraph &cur = levels.back();

        if (cur.n <= stop_vertices) {
            break;
        }

        vector<int> fine_to_coarse;

        HGraph coarse = coarsen_once(cur, rng, fine_to_coarse);

        if (coarse.n >= cur.n) {
            break;
        }

        if (coarse.n > static_cast<int>(cur.n * 0.92)) {
            break;
        }

        maps.push_back(fine_to_coarse);
        levels.push_back(coarse);
    }

    int L = static_cast<int>(levels.size());

    const HGraph &coarse_graph = levels.back();

    int coarse_trials = 8;

    vector<int> best_coarse_part;
    int best_coarse_cut = numeric_limits<int>::max();

    for (int t = 0; t < coarse_trials; ++t) {
        vector<int> init = make_initial_partition(
            coarse_graph,
            run_id * 100 + t,
            rng
        );

        FMResult coarse_result = fm_refine(
            coarse_graph,
            init,
            max(4, max_passes)
        );

        if (coarse_result.cut < best_coarse_cut &&
            is_balanced(coarse_graph, coarse_result.part)) {
            best_coarse_cut = coarse_result.cut;
            best_coarse_part = coarse_result.part;
        }
    }

    if (best_coarse_part.empty()) {
        best_coarse_part = make_random_initial(coarse_graph, rng);
    }

    vector<int> part = best_coarse_part;

    vector<int> level_cuts;

    for (int lev = L - 2; lev >= 0; --lev) {
        const HGraph &fine = levels[lev];

        vector<int> fine_part(fine.n, 1);

        const vector<int> &mp = maps[lev];

        for (int v = 0; v < fine.n; ++v) {
            fine_part[v] = part[mp[v]];
        }

        repair_balance(fine, fine_part, rng);

        FMResult refined = fm_refine(
            fine,
            fine_part,
            max_passes
        );

        part = refined.part;
        level_cuts.push_back(refined.cut);
    }

    int final_cut = compute_cut(original_, part);

    steady_clock::time_point t1 = steady_clock::now();

    MLResult result;

    result.part = part;
    result.cut = final_cut;
    result.levels = L;
    result.seconds = duration<double>(t1 - t0).count();
    result.level_cuts = level_cuts;

    return result;
}


void Solution::write_run_csv_header(const string &csv_path) const {
    ofstream fout(csv_path.c_str());

    fout << "run_id,final_cut,gap_percent,levels,time_sec,size0,size1,balanced\n";
}


void Solution::append_run_csv(
    const string &csv_path,
    int run_id,
    const MLResult &result
) const {
    ofstream fout(csv_path.c_str(), ios::app);

    long long size0 = part0_weight(original_, result.part);
    long long size1 = original_.total_weight - size0;

    int opt = optimal_cut_2_percent(benchmark_name_);

    double gap = 0.0;

    if (opt > 0) {
        gap = 100.0 * static_cast<double>(result.cut - opt)
            / static_cast<double>(opt);
    }

    fout << run_id << ","
         << result.cut << ","
         << fixed << setprecision(4) << gap << ","
         << result.levels << ","
         << fixed << setprecision(6) << result.seconds << ","
         << size0 << ","
         << size1 << ","
         << (is_balanced(original_, result.part) ? "YES" : "NO")
         << "\n";
}


void Solution::write_summary_csv(
    const string &summary_path,
    int runs,
    int max_passes,
    double total_seconds
) const {
    ofstream fout(summary_path.c_str());

    long long size0 = part0_weight(original_, best_partition_);
    long long size1 = original_.total_weight - size0;

    int opt = optimal_cut_2_percent(benchmark_name_);

    fout << "benchmark,vertices,hyperedges,epsilon,size0,size1,best_cut,opt_cut_2pct,gap_percent,runs,max_passes,total_time_sec\n";

    fout << benchmark_name_ << ","
         << n_ << ","
         << m_ << ","
         << "2%" << ","
         << size0 << ","
         << size1 << ","
         << best_cut_ << ",";

    if (opt > 0) {
        double gap = 100.0 * static_cast<double>(best_cut_ - opt)
            / static_cast<double>(opt);

        fout << opt << ","
             << fixed << setprecision(4) << gap << ",";
    } else {
        fout << ",,";
    }

    fout << runs << ","
         << max_passes << ","
         << fixed << setprecision(6) << total_seconds << "\n";
}


void Solution::run(
    int runs,
    int max_passes,
    unsigned int seed
) {
    using chrono::duration;
    using chrono::steady_clock;

    if (n_ <= 0) {
        throw runtime_error("No benchmark loaded. Call read_benchmark first.");
    }

    if (runs <= 0) {
        runs = 1;
    }

    if (max_passes <= 0) {
        max_passes = 1;
    }

    ensure_results_dir();

    string partition_txt = result_path(benchmark_name_ + "_partition.txt");
    string runs_csv = result_path(benchmark_name_ + "_runs.csv");
    string summary_csv = result_path(benchmark_name_ + "_summary.csv");

    cout << endl;
    cout << "============================================================\n";
    cout << " Multilevel Hypergraph Bipartition\n";
    cout << " Coarsening + Coarse Partition + Uncoarsening + FM\n";
    cout << "============================================================\n";
    cout << "Benchmark       : " << benchmark_name_ << "\n";
    cout << "Vertices        : " << n_ << "\n";
    cout << "Hyperedges      : " << m_ << "\n";
    cout << "Balance epsilon : 2%\n";
    cout << "Balance range   : [" << lower_bound_
         << ", " << upper_bound_ << "]\n";
    cout << "Runs            : " << runs << "\n";
    cout << "Max FM passes   : " << max_passes << "\n";
    cout << "Seed            : " << seed << "\n";
    cout << "============================================================\n";

    write_run_csv_header(runs_csv);

    steady_clock::time_point total_t0 = steady_clock::now();

    best_cut_ = numeric_limits<int>::max();
    best_partition_.clear();

    int opt = optimal_cut_2_percent(benchmark_name_);

    cout << left
         << setw(8) << "Run"
         << setw(14) << "Cut"
         << setw(14) << "Gap%"
         << setw(10) << "Levels"
         << setw(12) << "Time(s)"
         << setw(10) << "|V0|"
         << setw(10) << "|V1|"
         << endl;

    for (int r = 0; r < runs; ++r) {
        unsigned int run_seed =
            seed + static_cast<unsigned int>(1000003u * r);

        mt19937 rng(run_seed);

        MLResult result = multilevel_partition_one_run(
            r,
            max_passes,
            rng
        );

        long long size0 = part0_weight(original_, result.part);
        long long size1 = original_.total_weight - size0;

        double gap = 0.0;

        if (opt > 0) {
            gap = 100.0 * static_cast<double>(result.cut - opt)
                / static_cast<double>(opt);
        }

        cout << left
             << setw(8) << r
             << setw(14) << result.cut
             << setw(14) << fixed << setprecision(2) << gap
             << setw(10) << result.levels
             << setw(12) << fixed << setprecision(4) << result.seconds
             << setw(10) << size0
             << setw(10) << size1
             << endl;

        append_run_csv(runs_csv, r, result);

        if (result.cut < best_cut_ &&
            is_balanced(original_, result.part)) {
            best_cut_ = result.cut;
            best_partition_ = result.part;
        }
    }

    steady_clock::time_point total_t1 = steady_clock::now();

    double total_seconds = duration<double>(total_t1 - total_t0).count();

    write_partition(input_path_);
    write_summary_csv(summary_csv, runs, max_passes, total_seconds);

    long long size0 = part0_weight(original_, best_partition_);
    long long size1 = original_.total_weight - size0;

    cout << "============================================================\n";
    cout << "[BEST] cutsize     : " << best_cut_ << "\n";
    cout << "[BEST] |V0|        : " << size0 << "\n";
    cout << "[BEST] |V1|        : " << size1 << "\n";
    cout << "[BEST] balanced    : "
         << (is_balanced(original_, best_partition_) ? "YES" : "NO")
         << "\n";

    if (opt > 0) {
        double gap = 100.0 * static_cast<double>(best_cut_ - opt)
            / static_cast<double>(opt);

        cout << "[BEST] opt 2%      : " << opt << "\n";
        cout << "[BEST] gap         : "
             << fixed << setprecision(2) << gap << "%\n";
    } else {
        cout << "[BEST] opt 2%      : unknown\n";
    }

    cout << "[TIME] total       : "
         << fixed << setprecision(4) << total_seconds << " sec\n";

    cout << "[SAVE] partition   : " << partition_txt << "\n";
    cout << "[SAVE] runs csv    : " << runs_csv << "\n";
    cout << "[SAVE] summary csv : " << summary_csv << "\n";
    cout << "============================================================\n";
}


void Solution::write_partition(const string &benchmark_path) const {
    if (best_partition_.empty()) {
        throw runtime_error("No partition result to write.");
    }

    ensure_results_dir();

    string out_name = result_path(
        basename_no_ext(benchmark_path) + "_partition.txt"
    );

    ofstream fout(out_name.c_str());

    if (!fout) {
        throw runtime_error("Cannot write partition file: " + out_name);
    }

    for (size_t i = 0; i < best_partition_.size(); ++i) {
        fout << best_partition_[i] << "\n";
    }
}


const vector<int> &Solution::get_partition() const {
    return best_partition_;
}


int Solution::get_best_cut() const {
    return best_cut_;
}