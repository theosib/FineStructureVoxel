#include <gtest/gtest.h>
#include "finevox/core/signal_solver.hpp"

#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <set>

using namespace finevox;

namespace {

constexpr SignalLevel MAX = 15;

/// A chain of conductors, each linked both ways to the next with loss 1
std::vector<SignalNodeId> wire(SignalSolver& s, size_t length, SignalLevel loss = 1) {
    std::vector<SignalNodeId> w;
    for (size_t i = 0; i < length; ++i) {
        w.push_back(s.addConductor());
        if (i > 0) {
            s.link(w[i - 1], w[i], loss);
            s.link(w[i], w[i - 1], loss);
        }
    }
    return w;
}

SignalNodeId component(SignalSolver& s, SignalOp op, uint16_t delay = 1, bool full = false,
                       uint16_t minPulse = 0) {
    SignalComponentSpec spec;
    spec.op = op;
    spec.delay = delay;
    spec.fullOutput = full;
    spec.minPulse = minPulse;
    return s.addComponent(spec);
}

}  // namespace

// ============================================================================
// Conductors
// ============================================================================

TEST(SignalSolver, ConductorsLoseLevelAlongTheWire) {
    SignalSolver s(MAX);
    auto src = s.addSource(MAX);
    auto w = wire(s, 20);
    s.link(src, w[0], 0);
    s.step();
    for (size_t i = 0; i < w.size(); ++i) {
        SignalLevel expected = i < MAX ? static_cast<SignalLevel>(MAX - i) : 0;
        EXPECT_EQ(s.level(w[i]), expected) << "at " << i;
    }
}

TEST(SignalSolver, ConductorsSettleInTheSameStep) {
    SignalSolver s(MAX);
    auto src = s.addSource();
    auto w = wire(s, 5);
    s.link(src, w[0], 0);
    s.step();
    EXPECT_EQ(s.level(w[4]), 0);
    s.setSource(src, 10);
    s.step();
    EXPECT_EQ(s.level(w[4]), 6);
}

TEST(SignalSolver, TheStrongestDriverWins) {
    SignalSolver s(MAX);
    auto weak = s.addSource(8);
    auto strong = s.addSource(MAX);
    auto w = wire(s, 10);
    s.link(weak, w[0], 0);
    s.link(strong, w[9], 0);
    s.step();
    EXPECT_EQ(s.level(w[0]), 8 > MAX - 9 ? 8 : MAX - 9);
    EXPECT_EQ(s.level(w[9]), MAX);
    EXPECT_EQ(s.level(w[5]), MAX - 4);
}

TEST(SignalSolver, LevelsFallWhenTheDriverGoes) {
    SignalSolver s(MAX);
    auto src = s.addSource(MAX);
    auto w = wire(s, 8);
    s.link(src, w[0], 0);
    s.step();
    ASSERT_EQ(s.level(w[7]), MAX - 7);
    s.setSource(src, 0);
    auto& changes = s.step();
    for (auto c : w) EXPECT_EQ(s.level(c), 0);
    EXPECT_EQ(changes.size(), w.size() + 1) << "every conductor and the source changed";
}

TEST(SignalSolver, CuttingAWireSplitsIt) {
    SignalSolver s(MAX);
    auto src = s.addSource(MAX);
    auto w = wire(s, 10);
    s.link(src, w[0], 0);
    s.step();
    s.removeNode(w[5]);
    s.step();
    EXPECT_EQ(s.level(w[4]), MAX - 4);
    EXPECT_EQ(s.level(w[6]), 0);
    EXPECT_EQ(s.level(w[9]), 0);
}

TEST(SignalSolver, JoiningWiresMergesThem) {
    SignalSolver s(MAX);
    auto src = s.addSource(MAX);
    auto left = wire(s, 3);
    auto right = wire(s, 3);
    s.link(src, left[0], 0);
    s.step();
    EXPECT_EQ(s.level(right[0]), 0);
    s.link(left[2], right[0], 1);
    s.link(right[0], left[2], 1);
    s.step();
    EXPECT_EQ(s.level(right[2]), MAX - 5);
}

TEST(SignalSolver, OneWayLinksOnlyCarryForward) {
    SignalSolver s(MAX);
    auto a = s.addSource(MAX);
    auto b = s.addSource(MAX);
    auto x = s.addConductor();
    auto y = s.addConductor();
    s.link(x, y, 0);  // x feeds y, not the other way
    s.link(a, x, 0);
    s.step();
    EXPECT_EQ(s.level(y), MAX);
    s.setSource(a, 0);
    s.link(b, y, 3);
    s.step();
    EXPECT_EQ(s.level(y), MAX - 3);
    EXPECT_EQ(s.level(x), 0);
}

TEST(SignalSolver, ZeroLossLoopsSettle) {
    SignalSolver s(MAX);
    auto src = s.addSource(9);
    auto ring = wire(s, 6, 0);
    s.link(ring[5], ring[0], 0);
    s.link(ring[0], ring[5], 0);
    s.link(src, ring[2], 0);
    s.step();
    for (auto c : ring) EXPECT_EQ(s.level(c), 9);
    s.setSource(src, 0);
    s.step();
    for (auto c : ring) EXPECT_EQ(s.level(c), 0) << "a loop does not hold its own level";
}

TEST(SignalSolver, OnlyTouchedRegionsAreSettled) {
    SignalSolver s(MAX);
    auto a = s.addSource();
    auto b = s.addSource();
    auto wa = wire(s, 100);
    auto wb = wire(s, 50);
    s.link(a, wa[0], 0);
    s.link(b, wb[0], 0);
    s.step();
    s.setSource(b, MAX);
    s.step();
    EXPECT_EQ(s.lastStats().regionsSettled, 1u);
    EXPECT_EQ(s.lastStats().conductorsSettled, 50u);
    s.step();
    EXPECT_EQ(s.lastStats().regionsSettled, 0u) << "a quiet circuit costs nothing";
}

// ============================================================================
// Components
// ============================================================================

TEST(SignalSolver, ComponentsOutputAfterTheirDelay) {
    SignalSolver s(MAX);
    auto src = s.addSource();
    auto buf = component(s, SignalOp::Buffer, 3);
    s.connect(src, buf, SignalPort::A);
    s.step();
    s.setSource(src, 7);
    s.step();  // the buffer reads 7 in this step
    for (int i = 0; i < 2; ++i) {
        s.step();
        EXPECT_EQ(s.level(buf), 0) << "step " << i;
    }
    s.step();
    EXPECT_EQ(s.level(buf), 7);
}

TEST(SignalSolver, DelayIsAtLeastOneStep) {
    SignalSolver s(MAX);
    auto src = s.addSource(MAX);
    auto buf = component(s, SignalOp::Buffer, 0);
    s.connect(src, buf, SignalPort::A);
    s.step();
    EXPECT_EQ(s.level(buf), 0);
    s.step();
    EXPECT_EQ(s.level(buf), MAX);
}

TEST(SignalSolver, FullOutputRaisesTheLevel) {
    SignalSolver s(MAX);
    auto src = s.addSource(1);
    auto buf = component(s, SignalOp::Buffer, 1, true);
    s.connect(src, buf, SignalPort::A);
    s.step();
    s.step();
    EXPECT_EQ(s.level(buf), MAX);
}

TEST(SignalSolver, AnInverterLoopIsADeterministicClock) {
    SignalSolver s(MAX);
    auto inv = component(s, SignalOp::Invert, 2);
    auto w = s.addConductor();
    s.link(inv, w, 0);
    s.connect(w, inv, SignalPort::A);
    std::vector<SignalLevel> trace;
    for (int i = 0; i < 12; ++i) {
        s.step();
        trace.push_back(s.level(inv));
    }
    // Reads 0 in step 1, so turns on in step 3; reads on in step 3, off in step 5...
    std::vector<SignalLevel> expected = {0, 0, MAX, MAX, 0, 0, MAX, MAX, 0, 0, MAX, MAX};
    EXPECT_EQ(trace, expected);
}

TEST(SignalSolver, MinPulseStretchesShortPulses) {
    SignalSolver s(MAX);
    auto src = s.addSource();
    auto buf = component(s, SignalOp::Buffer, 2, false, 4);
    s.connect(src, buf, SignalPort::A);
    s.step();
    s.setSource(src, MAX);
    s.step();  // step 2 reads on
    s.setSource(src, 0);
    s.step();  // step 3 reads off
    std::vector<SignalLevel> trace;
    for (int i = 0; i < 8; ++i) {
        s.step();
        trace.push_back(s.level(buf));
    }
    // On at step 4 (2 + 2), held for 4 steps although the line fell at step 5
    std::vector<SignalLevel> expected = {MAX, MAX, MAX, MAX, 0, 0, 0, 0};
    EXPECT_EQ(trace, expected);
}

TEST(SignalSolver, HoldFreezesTheOutput) {
    SignalSolver s(MAX);
    auto in = s.addSource();
    auto hold = s.addSource();
    auto buf = component(s, SignalOp::Buffer, 1);
    s.connect(in, buf, SignalPort::A);
    s.connect(hold, buf, SignalPort::Hold);
    s.setSource(in, 5);
    s.step();
    s.step();
    ASSERT_EQ(s.level(buf), 5);
    s.setSource(hold, MAX);
    s.step();
    s.setSource(in, 0);
    for (int i = 0; i < 4; ++i) {
        s.step();
        EXPECT_EQ(s.level(buf), 5);
    }
    s.setSource(hold, 0);
    s.step();  // released: reads 0
    EXPECT_EQ(s.level(buf), 5);
    s.step();
    EXPECT_EQ(s.level(buf), 0);
}

TEST(SignalSolver, CompareAndSubtract) {
    SignalSolver s(MAX);
    auto a = s.addSource(9);
    auto b = s.addSource(4);
    auto cmp = component(s, SignalOp::Compare);
    auto sub = component(s, SignalOp::Subtract);
    for (auto c : {cmp, sub}) {
        s.connect(a, c, SignalPort::A);
        s.connect(b, c, SignalPort::B);
    }
    s.step();
    s.step();
    EXPECT_EQ(s.level(cmp), 9);
    EXPECT_EQ(s.level(sub), 5);
    s.setSource(b, 12);
    s.step();
    s.step();
    EXPECT_EQ(s.level(cmp), 0);
    EXPECT_EQ(s.level(sub), 0);
}

TEST(SignalSolver, GatesOverManyInputs) {
    SignalSolver s(MAX);
    auto x = s.addSource(3);
    auto y = s.addSource(7);
    auto z = s.addSource(0);
    auto mx = component(s, SignalOp::Max);
    auto mn = component(s, SignalOp::Min);
    auto par = component(s, SignalOp::Parity);
    for (auto g : {mx, mn, par}) {
        for (auto in : {x, y, z}) s.connect(in, g, SignalPort::A);
    }
    s.step();
    s.step();
    EXPECT_EQ(s.level(mx), 7);
    EXPECT_EQ(s.level(mn), 0);
    EXPECT_EQ(s.level(par), 0) << "two inputs on";
    s.setSource(z, 1);
    s.step();
    s.step();
    EXPECT_EQ(s.level(mn), 1);
    EXPECT_EQ(s.level(par), MAX);
}

TEST(SignalSolver, LatchAndToggle) {
    SignalSolver s(MAX);
    auto set = s.addSource();
    auto reset = s.addSource();
    auto latch = component(s, SignalOp::Latch);
    auto toggle = component(s, SignalOp::Toggle);
    s.connect(set, latch, SignalPort::A);
    s.connect(reset, latch, SignalPort::B);
    s.connect(set, toggle, SignalPort::A);

    auto pulse = [&](SignalNodeId src) {
        s.setSource(src, MAX);
        s.step();
        s.setSource(src, 0);
        s.step();
    };
    pulse(set);
    s.step();
    EXPECT_EQ(s.level(latch), MAX);
    EXPECT_EQ(s.level(toggle), MAX);
    pulse(set);
    s.step();
    EXPECT_EQ(s.level(latch), MAX);
    EXPECT_EQ(s.level(toggle), 0);
    pulse(reset);
    s.step();
    EXPECT_EQ(s.level(latch), 0);
}

TEST(SignalSolver, RemovedNodesAreForgotten) {
    SignalSolver s(MAX);
    auto src = s.addSource(MAX);
    auto buf = component(s, SignalOp::Buffer, 5);
    auto w = s.addConductor();
    s.connect(src, buf, SignalPort::A);
    s.link(buf, w, 0);
    s.step();
    s.removeNode(buf);
    auto reused = s.addComponent({SignalOp::Buffer, 1, 0, false});
    EXPECT_EQ(reused, buf) << "ids are reused";
    for (int i = 0; i < 8; ++i) s.step();
    EXPECT_EQ(s.level(reused), 0) << "the old delay line must not fire into the new node";
    EXPECT_EQ(s.level(w), 0);
}

// ============================================================================
// Against a reference that recomputes everything every step
// ============================================================================

namespace {

/// Same rules, computed the slow way: conductors by relaxation to a fixpoint, delay lines
/// as the full history of each component's result.
class Reference {
public:
    struct Node {
        SignalOp op;
        SignalComponentSpec spec;
        SignalLevel level = 0;
        std::map<SignalNodeId, SignalLevel> in;  // conductor feeds
        std::vector<SignalNodeId> ports[3];
        std::map<uint64_t, SignalLevel> results;  // step -> result computed then
        SignalLevel line = 0;
        uint64_t holdUntil = 0;
        SignalLevel prevA = 0;
        bool state = false;
    };
    std::map<SignalNodeId, Node> nodes;
    std::map<SignalNodeId, SignalLevel> sourceSets;
    uint64_t now = 0;

    void remove(SignalNodeId id) {
        nodes.erase(id);
        sourceSets.erase(id);
        for (auto& [_, n] : nodes) {
            n.in.erase(id);
            for (auto& p : n.ports) p.erase(std::remove(p.begin(), p.end(), id), p.end());
        }
    }

    SignalLevel portMax(const Node& n, SignalPort p) const {
        SignalLevel best = 0;
        for (auto src : n.ports[static_cast<size_t>(p)]) best = std::max(best, nodes.at(src).level);
        return best;
    }

    void step() {
        ++now;
        for (auto& [id, n] : nodes) {
            if (!isSignalComponent(n.op)) continue;
            uint64_t d = std::max<uint16_t>(n.spec.delay, 1);
            if (now > d) {
                auto it = n.results.find(now - d);
                if (it != n.results.end()) n.line = it->second;
            }
            SignalLevel out = n.line > 0 ? n.line : (now < n.holdUntil ? n.level : 0);
            if (n.level == 0 && out > 0) n.holdUntil = now + n.spec.minPulse;
            n.level = out;
        }
        for (auto [id, lvl] : sourceSets) nodes.at(id).level = lvl;
        sourceSets.clear();

        for (auto& [id, n] : nodes) {
            if (n.op == SignalOp::Conductor) n.level = 0;
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (auto& [id, n] : nodes) {
                if (n.op != SignalOp::Conductor) continue;
                for (auto [from, loss] : n.in) {
                    SignalLevel f = nodes.at(from).level;
                    SignalLevel v = f > loss ? static_cast<SignalLevel>(f - loss) : 0;
                    if (v > n.level) {
                        n.level = v;
                        changed = true;
                    }
                }
            }
        }

        for (auto& [id, n] : nodes) {
            if (!isSignalComponent(n.op)) continue;
            SignalLevel a = portMax(n, SignalPort::A);
            SignalLevel b = portMax(n, SignalPort::B);
            SignalLevel r = 0;
            auto& pa = n.ports[0];
            switch (n.op) {
                case SignalOp::Buffer: case SignalOp::Max: r = a; break;
                case SignalOp::Invert: r = a ? 0 : MAX; break;
                case SignalOp::Compare: r = a >= b ? a : 0; break;
                case SignalOp::Subtract: r = a > b ? a - b : 0; break;
                case SignalOp::Min:
                    if (!pa.empty()) {
                        r = MAX;
                        for (auto src : pa) r = std::min(r, nodes.at(src).level);
                    }
                    break;
                case SignalOp::Parity: {
                    int on = 0;
                    for (auto src : pa) on += nodes.at(src).level > 0;
                    r = (on % 2) ? MAX : 0;
                    break;
                }
                case SignalOp::Latch:
                    if (b) n.state = false;
                    else if (a) n.state = true;
                    r = n.state ? MAX : 0;
                    break;
                case SignalOp::Toggle:
                    if (a && !n.prevA) n.state = !n.state;
                    r = n.state ? MAX : 0;
                    break;
                default: break;
            }
            n.prevA = a;
            if (n.spec.fullOutput && r > 0) r = MAX;
            n.results[now] = r;
        }
    }
};

}  // namespace

class SignalSolverRandom : public ::testing::TestWithParam<uint32_t> {};

TEST_P(SignalSolverRandom, MatchesTheReference) {
    std::mt19937 rng(GetParam());
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<uint32_t>(n)); };
    SignalSolver s(MAX);
    Reference ref;
    std::vector<SignalNodeId> live;
    const SignalOp ops[] = {SignalOp::Buffer, SignalOp::Invert, SignalOp::Compare, SignalOp::Subtract,
                            SignalOp::Max, SignalOp::Min, SignalOp::Parity, SignalOp::Latch,
                            SignalOp::Toggle};
    auto ofKind = [&](bool conductor) {
        std::vector<SignalNodeId> v;
        for (auto id : live) {
            if ((ref.nodes.at(id).op == SignalOp::Conductor) == conductor) v.push_back(id);
        }
        return v;
    };

    for (int stepNo = 0; stepNo < 400; ++stepNo) {
        int edits = pick(6);
        for (int e = 0; e < edits; ++e) {
            int what = pick(100);
            if (what < 12 || live.size() < 4) {
                SignalNodeId id;
                Reference::Node rn{};
                int kind = pick(10);
                if (kind < 2) {
                    id = s.addSource();
                    rn.op = SignalOp::Source;
                } else if (kind < 6) {
                    id = s.addConductor();
                    rn.op = SignalOp::Conductor;
                } else {
                    SignalComponentSpec spec;
                    spec.op = ops[pick(9)];
                    spec.delay = static_cast<uint16_t>(1 + pick(4));
                    spec.minPulse = pick(3) == 0 ? static_cast<uint16_t>(pick(6)) : 0;
                    spec.fullOutput = pick(2) == 0;
                    id = s.addComponent(spec);
                    rn.op = spec.op;
                    rn.spec = spec;
                }
                ASSERT_EQ(ref.nodes.count(id), 0u);
                ref.nodes[id] = rn;
                live.push_back(id);
            } else if (what < 18) {
                size_t i = static_cast<size_t>(pick(static_cast<int>(live.size())));
                s.removeNode(live[i]);
                ref.remove(live[i]);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
            } else if (what < 50) {
                auto conductors = ofKind(true);
                if (conductors.empty()) continue;
                SignalNodeId to = conductors[static_cast<size_t>(pick(static_cast<int>(conductors.size())))];
                SignalNodeId from = live[static_cast<size_t>(pick(static_cast<int>(live.size())))];
                if (from == to) continue;
                SignalLevel loss = static_cast<SignalLevel>(pick(3));
                s.link(from, to, loss);
                ref.nodes.at(to).in[from] = loss;
            } else if (what < 58) {
                auto conductors = ofKind(true);
                if (conductors.empty()) continue;
                SignalNodeId to = conductors[static_cast<size_t>(pick(static_cast<int>(conductors.size())))];
                auto& in = ref.nodes.at(to).in;
                if (in.empty()) continue;
                auto it = std::next(in.begin(), pick(static_cast<int>(in.size())));
                s.unlink(it->first, to);
                in.erase(it);
            } else if (what < 80) {
                std::vector<SignalNodeId> comps;
                for (auto id : live) {
                    if (isSignalComponent(ref.nodes.at(id).op)) comps.push_back(id);
                }
                if (comps.empty()) continue;
                SignalNodeId c = comps[static_cast<size_t>(pick(static_cast<int>(comps.size())))];
                SignalNodeId from = live[static_cast<size_t>(pick(static_cast<int>(live.size())))];
                SignalPort port = pick(2) == 0 ? SignalPort::A : SignalPort::B;
                s.connect(from, c, port);
                ref.nodes.at(c).ports[static_cast<size_t>(port)].push_back(from);
            } else if (what < 85) {
                std::vector<SignalNodeId> comps;
                for (auto id : live) {
                    auto& rn = ref.nodes.at(id);
                    if (isSignalComponent(rn.op) && !rn.ports[0].empty()) comps.push_back(id);
                }
                if (comps.empty()) continue;
                SignalNodeId c = comps[static_cast<size_t>(pick(static_cast<int>(comps.size())))];
                auto& p = ref.nodes.at(c).ports[0];
                size_t i = static_cast<size_t>(pick(static_cast<int>(p.size())));
                s.disconnect(p[i], c, SignalPort::A);
                // The solver drops the first occurrence of that node
                auto first = std::find(p.begin(), p.end(), p[i]);
                p.erase(first);
            } else {
                auto sources = std::vector<SignalNodeId>{};
                for (auto id : live) {
                    if (ref.nodes.at(id).op == SignalOp::Source) sources.push_back(id);
                }
                if (sources.empty()) continue;
                SignalNodeId src = sources[static_cast<size_t>(pick(static_cast<int>(sources.size())))];
                SignalLevel lvl = pick(3) == 0 ? 0 : static_cast<SignalLevel>(pick(MAX + 1));
                s.setSource(src, lvl);
                ref.sourceSets[src] = lvl;
            }
        }

        std::map<SignalNodeId, SignalLevel> before;
        for (auto id : live) before[id] = s.level(id);
        const auto& changes = s.step();
        ref.step();

        for (auto id : live) {
            ASSERT_EQ(s.level(id), ref.nodes.at(id).level)
                << "seed " << GetParam() << " step " << stepNo << " node " << id;
        }
        std::set<SignalNodeId> reported;
        for (const auto& c : changes) {
            reported.insert(c.node);
            ASSERT_EQ(c.level, s.level(c.node));
        }
        for (auto id : live) {
            bool changed = before[id] != s.level(id);
            ASSERT_EQ(changed, reported.count(id) == 1) << "seed " << GetParam() << " step " << stepNo
                                                        << " node " << id << " change not reported";
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Seeds, SignalSolverRandom, ::testing::Range(1u, 41u));

// ============================================================================
// Cost
// ============================================================================

TEST(SignalSolver, LargeGridCost) {
    // A 100 x 100 sheet of conductors driven by a clock: the whole sheet settles each time
    // the clock turns, which is the worst case for one region.
    SignalSolver s(MAX);
    constexpr int N = 100;
    std::vector<SignalNodeId> grid(N * N);
    for (auto& c : grid) c = s.addConductor();
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            auto c = grid[y * N + x];
            if (x + 1 < N) { s.link(c, grid[y * N + x + 1], 1); s.link(grid[y * N + x + 1], c, 1); }
            if (y + 1 < N) { s.link(c, grid[(y + 1) * N + x], 1); s.link(grid[(y + 1) * N + x], c, 1); }
        }
    }
    auto clock = component(s, SignalOp::Invert, 1);
    s.connect(clock, clock, SignalPort::A);
    for (int i = 0; i < N; i += 10) s.link(clock, grid[i * N + i], 0);
    s.step();

    constexpr int STEPS = 200;
    auto t0 = std::chrono::steady_clock::now();
    uint64_t settled = 0;
    for (int i = 0; i < STEPS; ++i) {
        s.step();
        settled += s.lastStats().conductorsSettled;
    }
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[signal] %d conductors, %d steps: %.1f us/step, %.1f ns/conductor\n", N * N, STEPS,
                us / STEPS, 1000.0 * us / static_cast<double>(std::max<uint64_t>(settled, 1)));
    EXPECT_EQ(settled, static_cast<uint64_t>(N * N) * STEPS) << "the clock turns every step";
    EXPECT_LT(us / STEPS, 20000.0);
}
