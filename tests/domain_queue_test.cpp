#include <cassert>
#include <random>
#include <set>

#include "discover.h"
#include "priority_queue_functions.h"

namespace {
constexpr unsigned int kNumVariables = 64;

pqData queue_data[2][_FPGA_MAX_LITERALS];
pqPosition positions[_FPGA_MAX_LITERALS];
bool in_domain[_FPGA_MAX_LITERALS];

void assert_queue(
    const std::set<unsigned int>& domain,
    const std::set<unsigned int>& expected,
    unsigned int remaining) {
    assert(remaining == expected.size());
    std::set<unsigned int> active;
    std::set<unsigned int> domain_prefix;
    std::set<unsigned int> all;
    for (unsigned int i = 0; i < kNumVariables; ++i) {
        const unsigned int variable = queue_data[0][i].literal;
        assert(variable >= 1 && variable <= kNumVariables);
        assert(queue_data[1][i].literal == variable);
        assert(positions[variable - 1].pos == i);
        all.insert(variable);
        if (i < remaining) {
            active.insert(variable);
        }
        if (i < domain.size()) {
            domain_prefix.insert(variable);
        }
    }
    assert(all.size() == kNumVariables);
    assert(active == expected);
    assert(domain_prefix == domain);
}

void hide(unsigned int variable, unsigned int& remaining) {
    hls::stream<lit> input;
    input.write(variable);
    input.write(pq::EXIT);
    hideElement(input, queue_data, positions, remaining);
}

void unhide(unsigned int variable, unsigned int& remaining) {
    hls::stream<lit> input;
    input.write(variable);
    input.write(pq::EXIT);
    unhideElement(input, queue_data, positions, kNumVariables / 2, remaining);
}
}  // namespace

int main() {
    unsigned int domain[kNumVariables / 2];
    std::set<unsigned int> domain_variables;
    for (unsigned int i = 0; i < kNumVariables / 2; ++i) {
        domain[i] = 2 * (i + 1);
        domain_variables.insert(domain[i]);
        in_domain[domain[i] - 1] = true;
    }
    std::set<unsigned int> active = domain_variables;

    loadPositioning(
        positions, queue_data, domain, kNumVariables, active.size());
    unsigned int remaining = active.size();
    assert_queue(domain_variables, active, remaining);
    assert(domainAllowsPropagation(1, 0, in_domain));
    assert(!domainAllowsPropagation(1, 1, in_domain));
    assert(domainAllowsPropagation(2, 1, in_domain));

    std::mt19937 random(0x1d0a1u);
    for (unsigned int step = 0; step < 4000; ++step) {
        const unsigned int variable = 1 + random() % kNumVariables;
        if ((random() & 1u) == 0) {
            hide(variable, remaining);
            if (in_domain[variable - 1]) {
                active.erase(variable);
            }
        } else {
            unhide(variable, remaining);
            if (in_domain[variable - 1]) {
                active.insert(variable);
            }
        }
        assert_queue(domain_variables, active, remaining);
    }
}
