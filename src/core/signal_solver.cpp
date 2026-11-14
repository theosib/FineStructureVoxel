#include "finevox/core/signal_solver.hpp"

#include <algorithm>
#include <cstddef>

namespace finevox {

namespace {

constexpr uint32_t NO_REGION = ~uint32_t{0};

SignalLevel lessLoss(SignalLevel level, SignalLevel loss) {
    return level > loss ? static_cast<SignalLevel>(level - loss) : SignalLevel{0};
}

template <typename T, typename Pred>
void eraseFirst(std::vector<T>& v, Pred pred) {
    auto it = std::find_if(v.begin(), v.end(), pred);
    if (it != v.end()) v.erase(it);
}

}  // namespace

SignalSolver::SignalSolver(SignalLevel maxLevel) : maxLevel_(maxLevel) {
    buckets_.resize(static_cast<size_t>(maxLevel_) + 1);
}

// ============================================================================
// Building the graph
// ============================================================================

SignalNodeId SignalSolver::allocate(SignalOp op) {
    SignalNodeId id;
    if (!freeIds_.empty()) {
        id = freeIds_.back();
        freeIds_.pop_back();
    } else {
        id = static_cast<SignalNodeId>(nodes_.size());
        nodes_.emplace_back();
        affectedMark_.push_back(0);
        floodMark_.push_back(0);
        evaluateMark_.push_back(0);
        changedMark_.push_back(0);
        levelAtStart_.push_back(0);
        settled_.push_back(0);
    }
    Node& n = nodes_[id];
    n.alive = true;
    n.op = op;
    n.level = 0;
    n.region = NO_REGION;
    return id;
}

SignalSolver::Node& SignalSolver::node(SignalNodeId id) { return nodes_[id]; }
const SignalSolver::Node& SignalSolver::node(SignalNodeId id) const { return nodes_[id]; }

bool SignalSolver::exists(SignalNodeId id) const {
    return id < nodes_.size() && nodes_[id].alive;
}

SignalLevel SignalSolver::level(SignalNodeId id) const {
    return exists(id) ? nodes_[id].level : SignalLevel{0};
}

SignalOp SignalSolver::op(SignalNodeId id) const {
    return exists(id) ? nodes_[id].op : SignalOp::Source;
}

SignalNodeId SignalSolver::addSource(SignalLevel level) {
    SignalNodeId id = allocate(SignalOp::Source);
    if (level > 0) setSource(id, level);
    return id;
}

SignalNodeId SignalSolver::addConductor() {
    SignalNodeId id = allocate(SignalOp::Conductor);
    markAffected(id);
    return id;
}

SignalNodeId SignalSolver::addComponent(const SignalComponentSpec& spec) {
    SignalNodeId id = allocate(spec.op);
    if (!isSignalComponent(spec.op)) {
        nodes_[id].op = SignalOp::Buffer;
    }
    setSpec(id, spec);
    return id;
}

void SignalSolver::setSpec(SignalNodeId id, const SignalComponentSpec& spec) {
    if (!exists(id) || !isSignalComponent(node(id).op) || !isSignalComponent(spec.op)) return;
    Node& n = node(id);
    n.spec = spec;
    n.spec.delay = std::max<uint16_t>(spec.delay, 1);
    if (n.op != spec.op) {
        n.op = spec.op;
        n.state = false;
    }
    markEvaluate(id);
}

void SignalSolver::dropConnections(SignalNodeId id) {
    Node& n = node(id);
    const bool conductor = n.op == SignalOp::Conductor;

    for (const Link& l : n.in) {
        Node& from = node(l.node);
        eraseFirst(from.out, [id](const Link& x) { return x.node == id; });
        if (from.op == SignalOp::Conductor) markAffected(l.node);
    }
    for (const Link& l : n.out) {
        Node& to = node(l.node);
        eraseFirst(to.in, [id](const Link& x) { return x.node == id; });
        if (conductor) markAffected(l.node);
        else markRegionDirty(l.node);
    }
    for (SignalNodeId r : n.readers) {
        if (!exists(r)) continue;
        for (auto& port : node(r).ports) {
            port.erase(std::remove(port.begin(), port.end(), id), port.end());
        }
        markEvaluate(r);
    }
    for (auto& port : n.ports) {
        for (SignalNodeId src : port) {
            if (exists(src)) eraseFirst(node(src).readers, [id](SignalNodeId x) { return x == id; });
        }
    }
    n.in.clear();
    n.out.clear();
    n.readers.clear();
    for (auto& port : n.ports) port.clear();
    if (conductor) markAffected(id);
    else if (isSignalComponent(n.op)) markEvaluate(id);
}

void SignalSolver::isolate(SignalNodeId id) {
    if (!exists(id)) return;
    dropConnections(id);
}

void SignalSolver::presetLevel(SignalNodeId id, SignalLevel level) {
    if (!exists(id) || node(id).op == SignalOp::Conductor) return;
    Node& n = node(id);
    level = std::min(level, maxLevel_);
    n.level = level;
    if (isSignalComponent(n.op)) {
        n.pending.clear();
        n.lineLevel = level;
        n.lastTarget = level;
        n.state = level > 0;
        markEvaluate(id);
    }
    for (const Link& l : n.out) markRegionDirty(l.node);
}

void SignalSolver::removeNode(SignalNodeId id) {
    if (!exists(id)) return;
    Node& n = node(id);
    const bool conductor = n.op == SignalOp::Conductor;
    dropConnections(id);
    if (conductor && n.region != NO_REGION && regions_[n.region].alive) {
        Region& r = regions_[n.region];
        eraseFirst(r.members, [id](SignalNodeId x) { return x == id; });
        if (r.members.empty()) {
            r.alive = false;
            freeRegions_.push_back(n.region);
        }
    }

    uint32_t generation = n.generation + 1;
    n = Node{};
    n.generation = generation;
    freeIds_.push_back(id);
}

void SignalSolver::link(SignalNodeId from, SignalNodeId to, SignalLevel loss) {
    if (from == to || !exists(from) || !exists(to) || node(to).op != SignalOp::Conductor) return;
    Node& t = node(to);
    Node& f = node(from);
    auto in = std::find_if(t.in.begin(), t.in.end(), [from](const Link& x) { return x.node == from; });
    if (in != t.in.end()) {
        in->loss = loss;
        for (Link& l : f.out) {
            if (l.node == to) l.loss = loss;
        }
    } else {
        t.in.push_back({from, loss});
        f.out.push_back({to, loss});
    }
    if (f.op == SignalOp::Conductor) {
        markAffected(from);
        markAffected(to);
    } else {
        markRegionDirty(to);
    }
}

void SignalSolver::unlink(SignalNodeId from, SignalNodeId to) {
    if (!exists(from) || !exists(to)) return;
    Node& t = node(to);
    auto in = std::find_if(t.in.begin(), t.in.end(), [from](const Link& x) { return x.node == from; });
    if (in == t.in.end()) return;
    t.in.erase(in);
    eraseFirst(node(from).out, [to](const Link& x) { return x.node == to; });
    if (node(from).op == SignalOp::Conductor) {
        markAffected(from);
        markAffected(to);
    } else {
        markRegionDirty(to);
    }
}

void SignalSolver::connect(SignalNodeId from, SignalNodeId component, SignalPort port) {
    if (!exists(from) || !exists(component) || !isSignalComponent(node(component).op)) return;
    node(component).ports[static_cast<size_t>(port)].push_back(from);
    node(from).readers.push_back(component);
    markEvaluate(component);
}

void SignalSolver::disconnect(SignalNodeId from, SignalNodeId component, SignalPort port) {
    if (!exists(from) || !exists(component)) return;
    auto& list = node(component).ports[static_cast<size_t>(port)];
    auto it = std::find(list.begin(), list.end(), from);
    if (it == list.end()) return;
    list.erase(it);
    eraseFirst(node(from).readers, [component](SignalNodeId x) { return x == component; });
    markEvaluate(component);
}

void SignalSolver::setSource(SignalNodeId id, SignalLevel level) {
    if (!exists(id) || node(id).op != SignalOp::Source) return;
    sourceSets_.push_back({id, node(id).generation, std::min(level, maxLevel_)});
}

// ============================================================================
// Marks. Everything marked between steps, or during one, belongs to step_ + 1.
// ============================================================================

void SignalSolver::markAffected(SignalNodeId id) {
    uint64_t now = step_ + 1;
    if (affectedMark_[id] == now) return;
    affectedMark_[id] = now;
    affected_.push_back(id);
}

void SignalSolver::markRegionDirty(SignalNodeId conductor) {
    uint32_t r = node(conductor).region;
    if (r == NO_REGION) return;  // new: the reflood settles it
    uint64_t now = step_ + 1;
    if (regionDirtyMark_.size() <= r) regionDirtyMark_.resize(r + 1, 0);
    if (regionDirtyMark_[r] == now) return;
    regionDirtyMark_[r] = now;
    dirtyRegions_.push_back(r);
}

void SignalSolver::markEvaluate(SignalNodeId id) {
    uint64_t now = step_ + 1;
    if (evaluateMark_[id] == now) return;
    evaluateMark_[id] = now;
    toEvaluate_.push_back(id);
}

void SignalSolver::noteLevelChange(SignalNodeId id, SignalLevel oldLevel) {
    uint64_t now = step_ + 1;
    if (changedMark_[id] != now) {
        changedMark_[id] = now;
        levelAtStart_[id] = oldLevel;
        changedNodes_.push_back(id);
    }
}

void SignalSolver::wake(SignalNodeId id, uint64_t due) {
    wakeups_[due].push_back({id, node(id).generation});
}

// ============================================================================
// Stepping
// ============================================================================

const std::vector<SignalChange>& SignalSolver::step() {
    const uint64_t now = step_ + 1;
    stats_ = {};
    changes_.clear();

    // 1. Regions whose shape may have changed are rebuilt (and so settled below)
    refloodRegions();

    // 2. Components output what is due. Hold is read from the levels as they stand.
    while (!wakeups_.empty() && wakeups_.begin()->first <= now) {
        auto due = std::move(wakeups_.begin()->second);
        wakeups_.erase(wakeups_.begin());
        std::sort(due.begin(), due.end());
        due.erase(std::unique(due.begin(), due.end()), due.end());
        for (auto [id, generation] : due) {
            if (exists(id) && node(id).generation == generation) fireComponent(id);
        }
    }

    // 3. Sources take their new levels
    auto sets = std::move(sourceSets_);
    sourceSets_.clear();
    for (const SourceSet& s : sets) {
        if (exists(s.node) && node(s.node).generation == s.generation) setOutput(s.node, s.level);
    }

    // 4. Conductors settle
    for (size_t i = 0; i < dirtyRegions_.size(); ++i) {
        uint32_t r = dirtyRegions_[i];
        if (r < regions_.size() && regions_[r].alive) settleRegion(r);
    }
    dirtyRegions_.clear();

    // 5. Components read the settled levels
    for (SignalNodeId id : changedNodes_) {
        if (!exists(id)) continue;
        for (SignalNodeId r : node(id).readers) markEvaluate(r);
    }
    for (size_t i = 0; i < toEvaluate_.size(); ++i) {
        SignalNodeId id = toEvaluate_[i];
        if (exists(id) && isSignalComponent(node(id).op)) evaluateComponent(id);
    }
    toEvaluate_.clear();

    for (SignalNodeId id : changedNodes_) {
        if (exists(id) && node(id).level != levelAtStart_[id]) changes_.push_back({id, node(id).level});
    }
    changedNodes_.clear();
    std::sort(changes_.begin(), changes_.end(),
              [](const SignalChange& a, const SignalChange& b) { return a.node < b.node; });

    step_ = now;
    return changes_;
}

void SignalSolver::refloodRegions() {
    if (affected_.empty()) return;
    const uint64_t now = step_ + 1;
    std::vector<uint32_t> killed;
    std::vector<SignalNodeId> stack;

    for (SignalNodeId start : affected_) {
        if (!exists(start) || node(start).op != SignalOp::Conductor) continue;
        if (floodMark_[start] == now) continue;

        uint32_t r;
        if (!freeRegions_.empty()) {
            r = freeRegions_.back();
            freeRegions_.pop_back();
        } else {
            r = static_cast<uint32_t>(regions_.size());
            regions_.emplace_back();
        }
        Region& region = regions_[r];
        region.alive = true;
        region.members.clear();

        stack.push_back(start);
        floodMark_[start] = now;
        while (!stack.empty()) {
            SignalNodeId id = stack.back();
            stack.pop_back();
            Node& n = node(id);
            if (n.region != NO_REGION && n.region != r) killed.push_back(n.region);
            n.region = r;
            regions_[r].members.push_back(id);
            auto visit = [&](const Link& l) {
                if (node(l.node).op == SignalOp::Conductor && floodMark_[l.node] != now) {
                    floodMark_[l.node] = now;
                    stack.push_back(l.node);
                }
            };
            for (const Link& l : n.in) visit(l);
            for (const Link& l : n.out) visit(l);
        }
        if (regionDirtyMark_.size() <= r) regionDirtyMark_.resize(r + 1, 0);
        regionDirtyMark_[r] = now;
        dirtyRegions_.push_back(r);
    }
    affected_.clear();

    // Every member of a region a flood reached was flooded with it, so the old region is
    // empty now. Freed only after the floods, so no new region took a slot still in use.
    std::sort(killed.begin(), killed.end());
    killed.erase(std::unique(killed.begin(), killed.end()), killed.end());
    for (uint32_t r : killed) {
        if (!regions_[r].alive) continue;
        bool stillUsed = false;
        for (SignalNodeId m : regions_[r].members) {
            if (exists(m) && node(m).region == r) { stillUsed = true; break; }
        }
        if (stillUsed) continue;  // the slot was reused by one of this pass's floods
        regions_[r].alive = false;
        regions_[r].members.clear();
        freeRegions_.push_back(r);
    }
}

void SignalSolver::settleRegion(uint32_t r) {
    const auto& members = regions_[r].members;
    ++stats_.regionsSettled;
    stats_.conductorsSettled += static_cast<uint32_t>(members.size());

    // Seed each conductor with what reaches it from outside the region
    for (SignalNodeId m : members) {
        SignalLevel best = 0;
        for (const Link& l : node(m).in) {
            const Node& from = node(l.node);
            if (from.op != SignalOp::Conductor) best = std::max(best, lessLoss(from.level, l.loss));
        }
        settled_[m] = best;
        if (best > 0) buckets_[best].push_back(m);
    }
    // Strongest first: a conductor is final once its bucket is reached
    for (int lvl = maxLevel_; lvl > 0; --lvl) {
        auto& bucket = buckets_[lvl];
        while (!bucket.empty()) {
            SignalNodeId m = bucket.back();
            bucket.pop_back();
            if (settled_[m] != lvl) continue;
            for (const Link& l : node(m).out) {
                SignalLevel v = lessLoss(static_cast<SignalLevel>(lvl), l.loss);
                if (v > settled_[l.node]) {
                    settled_[l.node] = v;
                    buckets_[v].push_back(l.node);
                }
            }
        }
    }
    for (SignalNodeId m : members) {
        Node& n = node(m);
        if (n.level != settled_[m]) {
            noteLevelChange(m, n.level);
            n.level = settled_[m];
        }
    }
}

void SignalSolver::setOutput(SignalNodeId id, SignalLevel level) {
    Node& n = node(id);
    if (n.level == level) return;
    noteLevelChange(id, n.level);
    n.level = level;
    for (const Link& l : n.out) markRegionDirty(l.node);
}

SignalLevel SignalSolver::portMax(const Node& n, SignalPort port) const {
    SignalLevel best = 0;
    for (SignalNodeId src : n.ports[static_cast<size_t>(port)]) best = std::max(best, node(src).level);
    return best;
}

bool SignalSolver::held(const Node& n) const {
    return portMax(n, SignalPort::Hold) > 0;
}

void SignalSolver::fireComponent(SignalNodeId id) {
    const uint64_t now = step_ + 1;
    Node& n = node(id);
    ++stats_.componentsFired;

    bool popped = false;
    size_t done = 0;
    while (done < n.pending.size() && n.pending[done].due <= now) {
        n.lineLevel = n.pending[done].level;
        ++done;
        popped = true;
    }
    n.pending.erase(n.pending.begin(), n.pending.begin() + static_cast<std::ptrdiff_t>(done));

    if (held(n)) {
        // Frozen: what arrives is dropped
        if (popped) n.lineLevel = n.level;
        if (n.pending.empty()) n.lastTarget = n.level;
        return;
    }

    SignalLevel out;
    if (n.lineLevel > 0) {
        out = n.lineLevel;
    } else if (now < n.holdUntil) {
        out = n.level;
        wake(id, n.holdUntil);
    } else {
        out = 0;
    }
    if (n.level == 0 && out > 0) n.holdUntil = now + n.spec.minPulse;
    setOutput(id, out);
}

void SignalSolver::evaluateComponent(SignalNodeId id) {
    const uint64_t now = step_ + 1;
    Node& n = node(id);
    ++stats_.componentsEvaluated;

    const SignalLevel a = portMax(n, SignalPort::A);
    if (held(n)) {
        // Frozen: nothing is computed and nothing on its way gets out
        n.prevA = a;
        n.pending.clear();
        n.lineLevel = n.level;
        n.lastTarget = n.level;
        return;
    }
    const SignalLevel b = portMax(n, SignalPort::B);
    const auto& portA = n.ports[static_cast<size_t>(SignalPort::A)];

    SignalLevel target = 0;
    switch (n.op) {
        case SignalOp::Buffer: target = a; break;
        case SignalOp::Invert: target = a > 0 ? 0 : maxLevel_; break;
        case SignalOp::Compare: target = a >= b ? a : 0; break;
        case SignalOp::Subtract: target = lessLoss(a, b); break;
        case SignalOp::Max: target = a; break;
        case SignalOp::Min: {
            if (!portA.empty()) {
                target = maxLevel_;
                for (SignalNodeId src : portA) target = std::min(target, node(src).level);
            }
            break;
        }
        case SignalOp::Parity: {
            size_t on = 0;
            for (SignalNodeId src : portA) on += node(src).level > 0 ? 1 : 0;
            target = (on & 1) ? maxLevel_ : 0;
            break;
        }
        case SignalOp::Latch:
            if (b > 0) n.state = false;
            else if (a > 0) n.state = true;
            target = n.state ? maxLevel_ : 0;
            break;
        case SignalOp::Toggle:
            if (a > 0 && n.prevA == 0) n.state = !n.state;
            target = n.state ? maxLevel_ : 0;
            break;
        case SignalOp::Source:
        case SignalOp::Conductor:
            return;
    }
    n.prevA = a;
    if (n.spec.fullOutput && target > 0) target = maxLevel_;

    if (target == n.lastTarget) return;
    uint64_t due = now + n.spec.delay;
    if (!n.pending.empty()) due = std::max(due, n.pending.back().due);
    n.pending.push_back({due, target});
    n.lastTarget = target;
    wake(id, due);
}

}  // namespace finevox
