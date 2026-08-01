// Build the edge layout the PL graph engine consumes, from nb_dump.py's graph.
//
// The model applies, per relation r and per destination node n:
//
//     acc[n] = sum over in-edges (s -> n) of type r  of  f_r(x[s])
//
// where f_r is a dense MLP (RGINConv) or an attention-weighted projection
// (GATv2Conv).  The dense part goes to the AIE array; what the PL has to do is
// feed it source embeddings and reduce its output back onto destinations.
//
// Doing that against the raw edge list means a random read for the gather and a
// random read-modify-write for the accumulate.  Sorting the edges once by
// (type, destination) removes the second one entirely: every destination's
// in-edges become one contiguous run, so the accumulator lives in a register
// until the destination changes, and GATv2's per-destination softmax becomes a
// sequential scan over that run instead of a scatter-max plus scatter-sum.
// Only the gather stays random.
//
// The layout is therefore CSR over in-edges, held separately per relation:
//
//     seg_ptr[r][n] .. seg_ptr[r][n+1]   the slice of edges landing on n
//     edge_src[r][i]                     the source node of edge i
//
// Sorting is counting sort on the destination, which is O(E + N) and stable, so
// no comparison sort is needed on device if this ever moves off the host.
//
// Build:  g++ -O2 -std=c++17 -o gnn_layout gnn_layout.cpp
// Run:    ./gnn_layout ref/graph ref/layout

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int NUM_RELATIONS = 3;   // edge_type = edge_attr + 1, so 0, 1, 2

bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n < 0 ? 0 : static_cast<size_t>(n));
    size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

template <typename T>
bool read_array(const std::string &path, std::vector<T> &out) {
    std::vector<uint8_t> raw;
    if (!read_file(path, raw)) return false;
    if (raw.size() % sizeof(T) != 0) return false;
    out.resize(raw.size() / sizeof(T));
    if (!out.empty()) std::memcpy(out.data(), raw.data(), raw.size());
    return true;
}

template <typename T>
bool write_array(const std::string &path, const std::vector<T> &v) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t put = v.empty() ? 0 : std::fwrite(v.data(), sizeof(T), v.size(), f);
    std::fclose(f);
    return put == v.size();
}

struct Relation {
    std::vector<int32_t> seg_ptr;    // length N + 1
    std::vector<int32_t> edge_src;   // length E_r, grouped by destination
    int64_t max_degree = 0;
    int64_t nonempty_nodes = 0;
};

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <graph dir> <out dir>\n", argv[0]);
        return 2;
    }
    const std::string in = argv[1], out = argv[2];

    std::vector<int8_t> x, edge_attr;
    std::vector<uint8_t> edge_type;
    std::vector<int32_t> edge_src, edge_dst;

    if (!read_array(in + "/x.bin", x) ||
        !read_array(in + "/edge_src.bin", edge_src) ||
        !read_array(in + "/edge_dst.bin", edge_dst) ||
        !read_array(in + "/edge_type.bin", edge_type)) {
        std::fprintf(stderr, "error: cannot read graph from %s\n", in.c_str());
        std::fprintf(stderr, "       expected x.bin, edge_src.bin, edge_dst.bin, "
                             "edge_type.bin from nb_dump.py graph\n");
        return 1;
    }

    const int64_t N = static_cast<int64_t>(x.size());
    const int64_t E = static_cast<int64_t>(edge_src.size());
    if (static_cast<int64_t>(edge_dst.size()) != E ||
        static_cast<int64_t>(edge_type.size()) != E) {
        std::fprintf(stderr, "error: edge arrays disagree: src=%zu dst=%zu type=%zu\n",
                     edge_src.size(), edge_dst.size(), edge_type.size());
        return 1;
    }
    std::printf("nodes %lld  edges %lld\n", (long long)N, (long long)E);

    for (int64_t i = 0; i < E; ++i) {
        if (edge_src[i] < 0 || edge_src[i] >= N || edge_dst[i] < 0 || edge_dst[i] >= N) {
            std::fprintf(stderr, "error: edge %lld out of range (%d -> %d, N=%lld)\n",
                         (long long)i, edge_src[i], edge_dst[i], (long long)N);
            return 1;
        }
        if (edge_type[i] >= NUM_RELATIONS) {
            std::fprintf(stderr, "error: edge %lld has type %u\n",
                         (long long)i, edge_type[i]);
            return 1;
        }
    }

    Relation rel[NUM_RELATIONS];
    int64_t count[NUM_RELATIONS] = {0, 0, 0};
    for (int64_t i = 0; i < E; ++i) count[edge_type[i]]++;

    for (int r = 0; r < NUM_RELATIONS; ++r) {
        Relation &R = rel[r];
        R.seg_ptr.assign(N + 1, 0);

        // Counting sort on the destination: histogram, prefix sum, place.
        for (int64_t i = 0; i < E; ++i)
            if (edge_type[i] == r) R.seg_ptr[edge_dst[i] + 1]++;
        for (int64_t n = 0; n < N; ++n) {
            int64_t deg = R.seg_ptr[n + 1];
            if (deg > R.max_degree) R.max_degree = deg;
            if (deg > 0) R.nonempty_nodes++;
            R.seg_ptr[n + 1] += R.seg_ptr[n];
        }

        R.edge_src.resize(count[r]);
        std::vector<int32_t> cursor(R.seg_ptr.begin(), R.seg_ptr.end() - 1);
        for (int64_t i = 0; i < E; ++i)
            if (edge_type[i] == r) R.edge_src[cursor[edge_dst[i]]++] = edge_src[i];

        // The cursor must have advanced to exactly the next segment start.
        for (int64_t n = 0; n < N; ++n) {
            if (cursor[n] != R.seg_ptr[n + 1]) {
                std::fprintf(stderr, "error: relation %d node %lld cursor %d != %d\n",
                             r, (long long)n, cursor[n], R.seg_ptr[n + 1]);
                return 1;
            }
        }
        if (R.seg_ptr[N] != static_cast<int32_t>(count[r])) {
            std::fprintf(stderr, "error: relation %d seg_ptr end %d != count %lld\n",
                         r, R.seg_ptr[N], (long long)count[r]);
            return 1;
        }
    }

    // The sorted layout must contain exactly the same multiset of (src, dst,
    // type) triples as the input.  Compare by scanning the CSR back out.
    {
        std::vector<int64_t> before(NUM_RELATIONS, 0), after(NUM_RELATIONS, 0);
        int64_t sum_before = 0, sum_after = 0;
        for (int64_t i = 0; i < E; ++i) {
            before[edge_type[i]]++;
            sum_before += static_cast<int64_t>(edge_src[i]) * 1000003 + edge_dst[i];
        }
        for (int r = 0; r < NUM_RELATIONS; ++r) {
            const Relation &R = rel[r];
            for (int64_t n = 0; n < N; ++n)
                for (int32_t i = R.seg_ptr[n]; i < R.seg_ptr[n + 1]; ++i) {
                    after[r]++;
                    sum_after += static_cast<int64_t>(R.edge_src[i]) * 1000003 + n;
                }
        }
        for (int r = 0; r < NUM_RELATIONS; ++r)
            if (before[r] != after[r]) {
                std::fprintf(stderr, "error: relation %d count %lld -> %lld\n",
                             r, (long long)before[r], (long long)after[r]);
                return 1;
            }
        if (sum_before != sum_after) {
            std::fprintf(stderr, "error: edge multiset checksum %lld != %lld\n",
                         (long long)sum_before, (long long)sum_after);
            return 1;
        }
    }

    std::printf("\n%-10s %10s %12s %12s %10s\n",
                "relation", "edges", "nonempty", "max degree", "mean deg");
    static const char *names[NUM_RELATIONS] = {"0 negative", "1 root", "2 positive"};
    for (int r = 0; r < NUM_RELATIONS; ++r) {
        const Relation &R = rel[r];
        double mean = R.nonempty_nodes ? double(count[r]) / double(R.nonempty_nodes) : 0.0;
        std::printf("%-10s %10lld %12lld %12lld %10.2f\n",
                    names[r], (long long)count[r],
                    (long long)R.nonempty_nodes, (long long)R.max_degree, mean);
    }

    // The longest run bounds the accumulator's live range and, for GATv2, the
    // depth of the online softmax; it is the number the HLS pipeline is sized
    // against.
    int64_t global_max = 0;
    for (int r = 0; r < NUM_RELATIONS; ++r)
        if (rel[r].max_degree > global_max) global_max = rel[r].max_degree;
    std::printf("\nlongest in-edge run across relations: %lld\n", (long long)global_max);

    for (int r = 0; r < NUM_RELATIONS; ++r) {
        char p1[512], p2[512];
        std::snprintf(p1, sizeof p1, "%s/seg_ptr_r%d.bin", out.c_str(), r);
        std::snprintf(p2, sizeof p2, "%s/edge_src_r%d.bin", out.c_str(), r);
        if (!write_array(p1, rel[r].seg_ptr) || !write_array(p2, rel[r].edge_src)) {
            std::fprintf(stderr, "error: cannot write to %s (does it exist?)\n",
                         out.c_str());
            return 1;
        }
    }

    char meta[512];
    std::snprintf(meta, sizeof meta, "%s/layout.txt", out.c_str());
    if (FILE *f = std::fopen(meta, "w")) {
        std::fprintf(f, "nodes %lld\nedges %lld\nrelations %d\n",
                     (long long)N, (long long)E, NUM_RELATIONS);
        for (int r = 0; r < NUM_RELATIONS; ++r)
            std::fprintf(f, "relation %d edges %lld max_degree %lld\n",
                         r, (long long)count[r], (long long)rel[r].max_degree);
        std::fprintf(f, "longest_run %lld\n", (long long)global_max);
        std::fclose(f);
    }
    std::printf("\nwrote seg_ptr_r{0,1,2}.bin, edge_src_r{0,1,2}.bin, layout.txt to %s\n",
                out.c_str());
    return 0;
}
