#include <algorithm>
#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

#include "priority_queue_functions.h"

namespace {
constexpr unsigned int kNumVariables = 64;

pqData queue_data[2][_FPGA_MAX_LITERALS];
pqPosition positions[_FPGA_MAX_LITERALS];
ap_uint<3> bucket_state[_FPGA_MAX_LITERALS];
unsigned int bucket_next[_FPGA_MAX_LITERALS];
unsigned int bucket_heads[GIPSAT_NUM_BUCKETS];

unsigned int bit_length(unsigned int value) {
    unsigned int result = 0;
    while (value != 0) {
        ++result;
        value >>= 1;
    }
    return result;
}

class RustVsidsModel {
  public:
    explicit RustVsidsModel(const std::vector<unsigned int>& domain)
        : domain_(domain), activity_(kNumVariables + 1, 0.0),
          rank_position_(kNumVariables + 1, -1),
          in_bucket_(kNumVariables + 1, false),
          buckets_(GIPSAT_NUM_BUCKETS), head_(0), increment_(1.0) {
        for (unsigned int variable : domain_) {
            push_bucket(variable);
        }
    }

    void bump_batch(const std::vector<unsigned int>& variables) {
        for (unsigned int variable : variables) {
            activity_[variable] += increment_;
            if (rank_position_[variable] < 0) {
                rank_position_[variable] = rank_heap_.size();
                rank_heap_.push_back(variable);
            }
            heap_up(rank_heap_, rank_position_, variable);
            if (activity_[variable] > 1e100) {
                for (double& value : activity_) {
                    value *= 1e-100;
                }
                increment_ *= 1e-100;
            }
        }
        increment_ *= 1.0 / 0.95;
    }

    void push_bucket(unsigned int variable) {
        if (std::find(domain_.begin(), domain_.end(), variable) == domain_.end() ||
            in_bucket_[variable]) {
            return;
        }
        const int position = rank_position_[variable];
        const unsigned int bucket = bit_length(
            position < 0 ? rank_heap_.size() : static_cast<unsigned int>(position));
        head_ = std::min(head_, bucket);
        buckets_[bucket].push_back(variable);
        in_bucket_[variable] = true;
    }

    unsigned int pop_bucket() {
        while (head_ < buckets_.size() && buckets_[head_].empty()) {
            ++head_;
        }
        if (head_ == buckets_.size()) {
            return 0;
        }
        const unsigned int variable = buckets_[head_].back();
        buckets_[head_].pop_back();
        in_bucket_[variable] = false;
        return variable;
    }

    void switch_to_heap() {
        exact_heap_.clear();
        std::fill(exact_position_.begin(), exact_position_.end(), -1);
        for (unsigned int variable : domain_) {
            if (!in_bucket_[variable]) {
                continue;
            }
            exact_position_[variable] = exact_heap_.size();
            exact_heap_.push_back(variable);
            heap_up(exact_heap_, exact_position_, variable);
        }
    }

    unsigned int pop_heap() {
        if (exact_heap_.empty()) {
            return 0;
        }
        const unsigned int result = exact_heap_.front();
        const unsigned int tail = exact_heap_.back();
        exact_heap_.pop_back();
        exact_position_[result] = -1;
        if (!exact_heap_.empty()) {
            exact_heap_[0] = tail;
            exact_position_[tail] = 0;
            heap_down(exact_heap_, exact_position_, 0);
        }
        return result;
    }

    std::size_t heap_size() const { return exact_heap_.size(); }

  private:
    void heap_up(std::vector<unsigned int>& heap, std::vector<int>& position,
                 unsigned int variable) {
        unsigned int index = position[variable];
        while (index != 0) {
            const unsigned int parent = (index - 1) >> 1;
            if (activity_[heap[parent]] >= activity_[variable]) {
                break;
            }
            heap[index] = heap[parent];
            position[heap[index]] = index;
            index = parent;
        }
        heap[index] = variable;
        position[variable] = index;
    }

    void heap_down(std::vector<unsigned int>& heap, std::vector<int>& position,
                   unsigned int index) {
        const unsigned int variable = heap[index];
        while (true) {
            const unsigned int left = (index << 1) + 1;
            if (left >= heap.size()) {
                break;
            }
            const unsigned int right = left + 1;
            const unsigned int child = right < heap.size() &&
                    activity_[heap[right]] > activity_[heap[left]]
                ? right
                : left;
            if (activity_[variable] >= activity_[heap[child]]) {
                break;
            }
            heap[index] = heap[child];
            position[heap[index]] = index;
            index = child;
        }
        heap[index] = variable;
        position[variable] = index;
    }

    std::vector<unsigned int> domain_;
    std::vector<double> activity_;
    std::vector<unsigned int> rank_heap_;
    std::vector<int> rank_position_;
    std::vector<bool> in_bucket_;
    std::vector<std::vector<unsigned int>> buckets_;
    unsigned int head_;
    double increment_;
    std::vector<unsigned int> exact_heap_;
    std::vector<int> exact_position_{kNumVariables + 1, -1};
};

void hardware_bump(const std::vector<unsigned int>& variables,
                   unsigned int& activity_heap_size, double& multiplier) {
    hls::stream<lit> input;
    for (unsigned int variable : variables) {
        input.write(variable);
    }
    input.write(pq::EXIT);
    gipsatBumpActivity(input, queue_data, positions, activity_heap_size,
                       multiplier, 0.95);
}

void hide_bucket(unsigned int variable) {
    hls::stream<lit> input;
    input.write(variable);
    input.write(pq::EXIT);
    gipsatBucketHide(input, bucket_state);
}

void unhide_bucket(unsigned int variable, unsigned int activity_heap_size,
                   unsigned int& bucket_head) {
    hls::stream<lit> input;
    input.write(variable);
    input.write(pq::EXIT);
    gipsatBucketUnhide(input, queue_data, positions, bucket_state, bucket_next,
                       bucket_heads, activity_heap_size, bucket_head);
}

void test_assignment_visibility() {
    unsigned int domain[] = {1, 2, 3, 4};
    unsigned int bucket_head = 0;
    unsigned int activity_heap_size = 0;

    loadGipsatBuckets(positions, bucket_state, bucket_next, bucket_heads,
                      domain, 4, 4, bucket_head, activity_heap_size);
    hide_bucket(4);
    assert(gipsatBucketPop(bucket_state, bucket_next, bucket_heads,
                           bucket_head) == 3);
    unhide_bucket(4, activity_heap_size, bucket_head);
    assert(gipsatBucketPop(bucket_state, bucket_next, bucket_heads,
                           bucket_head) == 4);
    assert(gipsatBucketPop(bucket_state, bucket_next, bucket_heads,
                           bucket_head) == 2);

    loadGipsatBuckets(positions, bucket_state, bucket_next, bucket_heads,
                      domain, 4, 4, bucket_head, activity_heap_size);
    hide_bucket(4);
    unhide_bucket(4, activity_heap_size, bucket_head);
    assert(gipsatBucketPop(bucket_state, bucket_next, bucket_heads,
                           bucket_head) == 4);
    assert(gipsatBucketPop(bucket_state, bucket_next, bucket_heads,
                           bucket_head) == 3);

    loadGipsatBuckets(positions, bucket_state, bucket_next, bucket_heads,
                      domain, 4, 4, bucket_head, activity_heap_size);
    hide_bucket(4);
    unsigned int remaining = 0;
    gipsatSwitchToHeap(domain, queue_data, positions, bucket_state, bucket_next,
                       4, 4, activity_heap_size, remaining);
    assert(remaining == 3);
    assert(positions[3].pos >= remaining);
    hls::stream<lit> heap_input;
    heap_input.write(4);
    heap_input.write(pq::EXIT);
    unhideElement(heap_input, queue_data, positions, 4, remaining);
    assert(remaining == 4);
    assert(positions[3].pos < remaining);
}
}  // namespace

int main() {
    test_assignment_visibility();

    std::vector<unsigned int> domain;
    unsigned int domain_array[kNumVariables / 2];
    for (unsigned int i = 0; i < kNumVariables / 2; ++i) {
        domain_array[i] = 2 * (i + 1);
        domain.push_back(domain_array[i]);
    }

    unsigned int bucket_head = 0;
    unsigned int activity_heap_size = 0;
    double multiplier = 1.0;
    loadGipsatBuckets(positions, bucket_state, bucket_next, bucket_heads,
                      domain_array, kNumVariables, domain.size(), bucket_head,
                      activity_heap_size);
    RustVsidsModel reference(domain);

    std::mt19937 random(0x6a697073u);
    std::vector<unsigned int> held;
    for (unsigned int step = 0; step < 3000; ++step) {
        const unsigned int operation = random() % 3;
        if (operation == 0) {
            const unsigned int expected = reference.pop_bucket();
            const unsigned int actual = gipsatBucketPop(
                bucket_state, bucket_next, bucket_heads, bucket_head);
            assert(actual == expected);
            if (actual != 0) {
                held.push_back(actual);
            }
        } else if (operation == 1) {
            std::vector<unsigned int> bumped;
            const unsigned int count = 1 + random() % 8;
            for (unsigned int i = 0; i < count; ++i) {
                bumped.push_back(1 + random() % kNumVariables);
            }
            reference.bump_batch(bumped);
            hardware_bump(bumped, activity_heap_size, multiplier);
        } else if (!held.empty()) {
            const unsigned int index = random() % held.size();
            const unsigned int variable = held[index];
            held.erase(held.begin() + index);
            reference.push_bucket(variable);
            gipsatBucketPush(variable, queue_data, positions, bucket_state,
                             bucket_next, bucket_heads, activity_heap_size,
                             bucket_head);
        }
    }

    reference.switch_to_heap();
    unsigned int remaining = 0;
    gipsatSwitchToHeap(domain_array, queue_data, positions, bucket_state,
                       bucket_next, kNumVariables, domain.size(),
                       activity_heap_size, remaining);
    assert(remaining == reference.heap_size());

    while (remaining != 0) {
        const unsigned int expected = reference.pop_heap();
        const unsigned int actual = queue_data[0][0].literal;
        assert(actual == expected);
        hls::stream<lit> hide_input;
        hide_input.write(actual);
        hide_input.write(pq::EXIT);
        hideElement(hide_input, queue_data, positions, remaining);
    }
    assert(reference.pop_heap() == 0);
}
