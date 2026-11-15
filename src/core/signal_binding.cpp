#include "finevox/core/signal_binding.hpp"

#include "finevox/core/block_properties.hpp"
#include "finevox/core/block_type.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace finevox {

namespace {

// North, east, south, west: the order of a conductor's join properties
constexpr Face HORIZONTAL[4] = {Face::NegZ, Face::PosX, Face::PosZ, Face::NegX};

BlockCoord offset(BlockCoord p, Face f) { return p.neighbor(f); }
BlockCoord offset(BlockCoord p, int dx, int dy, int dz) { return BlockCoord(p.x + dx, p.y + dy, p.z + dz); }

/// The face of a that looks at b, for neighbours
std::optional<Face> faceToward(BlockCoord a, BlockCoord b) {
    int dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    if (std::abs(dx) + std::abs(dy) + std::abs(dz) != 1) return std::nullopt;
    if (dx == -1) return Face::NegX;
    if (dx == 1) return Face::PosX;
    if (dy == -1) return Face::NegY;
    if (dy == 1) return Face::PosY;
    if (dz == -1) return Face::NegZ;
    return Face::PosZ;
}

bool isDriver(const SignalBlockSpec* s) {
    return s && (s->role == SignalRole::Source || s->role == SignalRole::Component);
}

SignalFrom fromKind(const SignalBlockSpec* s) {
    if (!s) return SIGNAL_FROM_RELAY;
    switch (s->role) {
        case SignalRole::Conductor: return SIGNAL_FROM_CONDUCTOR;
        case SignalRole::Source: return SIGNAL_FROM_SOURCE;
        case SignalRole::Component: return SIGNAL_FROM_COMPONENT;
        default: return 0;
    }
}

bool sameComponent(const SignalComponentSpec& a, const SignalComponentSpec& b) {
    return a.op == b.op && a.delay == b.delay && a.minPulse == b.minPulse && a.fullOutput == b.fullOutput;
}

}  // namespace

// ============================================================================
// Inbox: filled from any thread, under World's lock
// ============================================================================

void SignalBinding::Inbox::blockChanged(BlockCoord pos) {
    std::lock_guard lock(mutex);
    blocks.push_back(pos);
}

void SignalBinding::Inbox::blocksMoved(std::span<const BlockMove> moved) {
    std::lock_guard lock(mutex);
    moves.insert(moves.end(), moved.begin(), moved.end());
}

void SignalBinding::Inbox::columnAdded(ColumnPos pos) {
    std::lock_guard lock(mutex);
    added.push_back(pos);
}

void SignalBinding::Inbox::columnRemoved(ColumnPos pos) {
    std::lock_guard lock(mutex);
    removed.push_back(pos);
}

// ============================================================================
// Lifecycle
// ============================================================================

SignalBinding::SignalBinding(World& world, SignalLevel maxLevel)
    : world_(world), solver_(maxLevel), inbox_(std::make_shared<Inbox>()) {}

SignalBinding::~SignalBinding() {
    detach();
}

void SignalBinding::onAttach(GameSession& session, EventBus& eventBus) {
    (void)session;
    (void)eventBus;
    attach();
}

void SignalBinding::onDetach() {
    // The session's world goes before whoever holds this, so let go of it now
    detach();
}

void SignalBinding::detach() {
    if (!attached_) return;
    attached_ = false;
    world_.removeBlockObserver(inbox_.get());
}

void SignalBinding::attach() {
    if (attached_) return;
    attached_ = true;
    world_.addBlockObserver(inbox_);
    std::vector<ColumnPos> columns;
    world_.forEachColumn([&columns](ColumnPos pos, const ChunkColumn&) { columns.push_back(pos); });
    std::lock_guard lock(inbox_->mutex);
    inbox_->added.insert(inbox_->added.end(), columns.begin(), columns.end());
}

void SignalBinding::tick(float dt) {
    (void)dt;
    step();
}

void SignalBinding::step() {
    showResults();
    takeChanges();
    results_ = solver_.step();
}

// ============================================================================
// Queries
// ============================================================================

SignalBinding::Entry* SignalBinding::entryAt(BlockCoord pos) {
    auto it = entries_.find(pos);
    return it == entries_.end() ? nullptr : &it->second;
}

const SignalBinding::Entry* SignalBinding::entryAt(BlockCoord pos) const {
    auto it = entries_.find(pos);
    return it == entries_.end() ? nullptr : &it->second;
}

SignalLevel SignalBinding::levelAt(BlockCoord pos) const {
    const Entry* e = entryAt(pos);
    return e && e->node != NO_SIGNAL_NODE ? solver_.level(e->node) : SignalLevel{0};
}

SignalLevel SignalBinding::relayLevelAt(BlockCoord pos, InternedId cls) const {
    const Entry* e = entryAt(pos);
    if (!e) return 0;
    const auto& classes = SignalBlockRegistry::global().relayClasses();
    for (size_t i = 0; i < classes.size() && i < e->relays.size(); ++i) {
        if (classes[i] == cls) return solver_.level(e->relays[i]);
    }
    return 0;
}

void SignalBinding::setSourceLevel(BlockCoord pos, SignalLevel level) {
    // Kept by position, so it holds for a block not taken in yet (a world just loaded)
    sourceLevels_[pos] = level;
    Entry* e = entryAt(pos);
    if (e && e->spec && e->spec->role == SignalRole::Source) solver_.setSource(e->node, level);
}

const SignalBlockSpec* SignalBinding::specOf(BlockTypeId type) const {
    if (type.isAir()) return nullptr;
    const SignalBlockSpec* s = SignalBlockRegistry::global().get(type);
    return s && s->role != SignalRole::None ? s : nullptr;
}

bool SignalBinding::isRelayBlock(BlockTypeId type) const {
    if (!opaqueRelays_ || type.isAir() || specOf(type)) return false;
    const BlockType& bt = BlockRegistry::global().getType(type);
    return bt.isOpaque() && !bt.hasCustomMesh();
}

bool SignalBinding::opaqueAt(BlockCoord pos) const {
    BlockTypeId type = world_.getBlock(pos);
    if (type.isAir()) return false;
    const BlockType& bt = BlockRegistry::global().getType(type);
    return bt.isOpaque() && !bt.hasCustomMesh();
}

SignalFaces SignalBinding::effectiveOut(const Entry& e, SignalFaces faces) const {
    if (faces & SIGNAL_POINTS) faces = static_cast<SignalFaces>((faces & SIGNAL_ALL_FACES) | e.points);
    return faces;
}

// ============================================================================
// Taking in changes
// ============================================================================

void SignalBinding::takeChanges() {
    std::vector<BlockCoord> blocks;
    std::vector<ColumnPos> added, removed;
    std::vector<BlockMove> moves;
    {
        std::lock_guard lock(inbox_->mutex);
        blocks.swap(inbox_->blocks);
        added.swap(inbox_->added);
        removed.swap(inbox_->removed);
        moves.swap(inbox_->moves);
    }
    for (ColumnPos pos : removed) dropColumn(pos);
    const Carried carried = carryMoves(moves);

    CoordSet changedBlocks(blocks.begin(), blocks.end());
    CoordSet changed;
    for (ColumnPos pos : added) scanColumn(pos, changed);
    changed.insert(changedBlocks.begin(), changedBlocks.end());

    CoordSet toRefresh;
    for (BlockCoord pos : changed) consider(pos, toRefresh);
    if (!toRefresh.empty()) refresh(toRefresh);
    landMoves(carried);

    // Changes the world made (not columns arriving) are what watchers see
    endPulses();
    triggerWatchers(changedBlocks);

    for (auto it = toRestore_.begin(); it != toRestore_.end();) {
        const Entry* e = entryAt(it->first);
        if (e && e->spec && e->spec->role == SignalRole::Component) {
            solver_.setComponentState(e->node, it->second);
            newlyMade_.erase(it->first);
            it = toRestore_.erase(it);
        } else {
            ++it;
        }
    }
}

void SignalBinding::startPulse(BlockCoord pos, const Entry& e, uint64_t ends) {
    auto [it, fresh] = watchersOn_.try_emplace(pos, ends);
    if (!fresh) return;  // already pulsing: a change during a pulse is part of it
    solver_.setSource(e.node, e.spec->watchLevel);
}

void SignalBinding::triggerWatchers(const CoordSet& changed) {
    const uint64_t next = solver_.stepCount() + 1;
    for (BlockCoord pos : changed) {
        for (int f = 0; f < 6; ++f) {
            BlockCoord w = offset(pos, static_cast<Face>(f));
            const Entry* e = entryAt(w);
            if (!e || !e->spec || e->spec->role != SignalRole::Source || !e->spec->watch) continue;
            // w's face toward pos
            if (!hasSignalFace(e->spec->watch, oppositeFace(static_cast<Face>(f)))) continue;
            startPulse(w, *e, next + e->spec->watchPulse);
        }
    }
}

SignalBinding::Carried SignalBinding::carryMoves(const std::vector<BlockMove>& moves) {
    Carried out;
    if (moves.empty()) return out;
    // Read every old cell before writing any new one: a move's cells overlap
    struct Taken {
        BlockCoord to;
        std::optional<SignalComponentState> state;
        std::optional<SignalLevel> source;
        std::optional<uint64_t> pulse;
        bool watcher = false;
    };
    std::vector<Taken> taken;
    taken.reserve(moves.size());
    for (const BlockMove& m : moves) {
        Taken t{m.to};
        if (const Entry* e = entryAt(m.from); e && e->spec && e->node != NO_SIGNAL_NODE) {
            if (e->spec->role == SignalRole::Component) {
                SignalComponentState s = solver_.componentState(e->node);
                if (!s.settled()) t.state = std::move(s);
            }
            t.watcher = e->spec->role == SignalRole::Source && e->spec->watch;
        }
        if (auto it = toRestore_.find(m.from); it != toRestore_.end()) t.state = it->second;
        if (auto it = sourceLevels_.find(m.from); it != sourceLevels_.end()) t.source = it->second;
        if (auto it = watchersOn_.find(m.from); it != watchersOn_.end()) t.pulse = it->second;
        taken.push_back(std::move(t));
    }
    // Every cell a move touched is built afresh, even where the same kind of block lands (its
    // node is the one that left)
    for (const BlockMove& m : moves) {
        for (BlockCoord pos : {m.from, m.to}) {
            removeEntry(pos);
            toRestore_.erase(pos);
            sourceLevels_.erase(pos);
            watchersOn_.erase(pos);
            ownWrites_.erase(pos);
        }
    }
    for (Taken& t : taken) {
        if (t.state) toRestore_[t.to] = std::move(*t.state);
        if (t.source) sourceLevels_[t.to] = *t.source;
        if (t.pulse) out.pulses.emplace_back(t.to, *t.pulse);
        else if (t.watcher) out.watchers.push_back(t.to);
    }
    return out;
}

void SignalBinding::landMoves(const Carried& carried) {
    auto watcherAt = [&](BlockCoord pos) -> const Entry* {
        const Entry* e = entryAt(pos);
        return e && e->spec && e->spec->role == SignalRole::Source && e->spec->watch ? e : nullptr;
    };
    for (const auto& [pos, ends] : carried.pulses) {
        if (const Entry* e = watcherAt(pos)) startPulse(pos, *e, ends);
    }
    // A watcher that moved sees a different view: that's a change
    const uint64_t next = solver_.stepCount() + 1;
    for (BlockCoord pos : carried.watchers) {
        if (const Entry* e = watcherAt(pos)) startPulse(pos, *e, next + e->spec->watchPulse);
    }
}

void SignalBinding::endPulses() {
    const uint64_t next = solver_.stepCount() + 1;
    for (auto it = watchersOn_.begin(); it != watchersOn_.end();) {
        if (it->second > next) { ++it; continue; }
        const Entry* e = entryAt(it->first);
        if (e && e->spec && e->spec->role == SignalRole::Source) solver_.setSource(e->node, e->spec->level);
        it = watchersOn_.erase(it);
    }
}

std::vector<SignalBinding::SavedComponent> SignalBinding::saveComponents() const {
    std::vector<SavedComponent> out;
    for (const auto& [pos, e] : entries_) {
        if (!e.spec || e.spec->role != SignalRole::Component) continue;
        SignalComponentState s = solver_.componentState(e.node);
        if (!s.settled()) out.push_back({pos, std::move(s)});
    }
    // Not yet taken in: keep them for next time
    for (const auto& [pos, s] : toRestore_) out.push_back({pos, s});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return CoordLess{}(a.pos, b.pos); });
    return out;
}

void SignalBinding::restoreComponents(std::vector<SavedComponent> saved) {
    for (auto& s : saved) toRestore_[s.pos] = std::move(s.state);
}

void SignalBinding::scanColumn(ColumnPos colPos, CoordSet& changed) {
    ChunkColumn* column = world_.getColumn(colPos);
    if (!column) return;
    column->forEachSubChunk([&](int32_t chunkY, SubChunk& sub) {
        bool any = false;
        for (BlockTypeId t : sub.palette().entries()) {
            if (specOf(t)) { any = true; break; }
        }
        if (!any) return;
        for (int32_t i = 0; i < SubChunk::VOLUME; ++i) {
            if (!specOf(sub.getBlock(i))) continue;
            changed.insert(ChunkPos(colPos.x, chunkY, colPos.z).toWorld(static_cast<uint16_t>(i)));
        }
    });
}

void SignalBinding::dropColumn(ColumnPos colPos) {
    std::vector<BlockCoord> gone;
    for (const auto& [pos, e] : entries_) {
        if ((pos.x >> 4) == colPos.x && (pos.z >> 4) == colPos.z) gone.push_back(pos);
    }
    for (BlockCoord pos : gone) removeEntry(pos);
}

void SignalBinding::consider(BlockCoord pos, CoordSet& toRefresh) {
    BlockTypeId type = world_.getBlock(pos);
    const SignalBlockSpec* spec = specOf(type);
    Entry* e = entryAt(pos);
    if (e && e->type == type) return;
    // A script's level holds until the block changes
    if (e && !ownWrites_.count(pos)) sourceLevels_.erase(pos);
    // Its own showing, coming back: nothing to do but note the type
    if (auto own = ownWrites_.find(pos); own != ownWrites_.end()) {
        bool mine = own->second == type;
        ownWrites_.erase(own);
        if (mine && e && e->spec && spec && e->spec->sameShape(*spec)) {
            e->type = type;
            e->spec = spec;
            return;
        }
    }
    if (e && e->spec && spec && e->spec->sameShape(*spec)) {
        const SignalBlockSpec* before = e->spec;
        e->type = type;
        e->spec = spec;
        applyParams(*e, before);
        // Whatever wrote it may have written over what's shown (a no-op when it didn't)
        if (spec->role == SignalRole::Conductor) showJoins(pos, *e);
        if (!spec->showEdges) newlyMade_.insert(pos);
        return;
    }

    bool near = spec || e;
    for (int dx = -1; dx <= 1 && !near; ++dx) {
        for (int dy = -1; dy <= 1 && !near; ++dy) {
            for (int dz = -1; dz <= 1 && !near; ++dz) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) == 0 ||
                    std::abs(dx) + std::abs(dy) + std::abs(dz) == 3) continue;
                near = entryAt(offset(pos, dx, dy, dz)) != nullptr;
            }
        }
    }
    if (!near) return;
    // The block, its faces and its edges: everything whose links or joins it can change
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) == 3) continue;
                toRefresh.insert(offset(pos, dx, dy, dz));
            }
        }
    }
}

void SignalBinding::applyParams(Entry& e, const SignalBlockSpec* before) {
    if (!e.spec || e.node == NO_SIGNAL_NODE) return;
    if (e.spec->role == SignalRole::Source) {
        // A script's level stands until the block's own level changes
        if (!before || before->level != e.spec->level) solver_.setSource(e.node, e.spec->level);
    } else if (e.spec->role == SignalRole::Component) {
        if (!before || !sameComponent(before->component, e.spec->component)) {
            solver_.setSpec(e.node, e.spec->component);
        }
    }
}

// ============================================================================
// Rebuilding a neighbourhood
// ============================================================================

void SignalBinding::removeEntry(BlockCoord pos) {
    auto it = entries_.find(pos);
    if (it == entries_.end()) return;
    if (it->second.node != NO_SIGNAL_NODE) {
        nodeAt_.erase(it->second.node);
        solver_.removeNode(it->second.node);
    }
    for (SignalNodeId r : it->second.relays) {
        if (r != NO_SIGNAL_NODE) solver_.removeNode(r);
    }
    entries_.erase(it);
    newlyMade_.erase(pos);
}

bool SignalBinding::rebuildEntry(BlockCoord pos) {
    BlockTypeId type = world_.getBlock(pos);
    const SignalBlockSpec* spec = specOf(type);
    Entry* e = entryAt(pos);

    if (spec) {
        if (e && e->spec && e->node != NO_SIGNAL_NODE && e->spec->sameShape(*spec)) {
            const SignalBlockSpec* before = e->spec;
            e->type = type;
            e->spec = spec;
            solver_.isolate(e->node);
            applyParams(*e, before);
            return true;
        }
        removeEntry(pos);
        Entry made;
        made.type = type;
        made.spec = spec;
        switch (spec->role) {
            case SignalRole::Source: {
                auto set = sourceLevels_.find(pos);
                made.node = solver_.addSource(set != sourceLevels_.end() ? set->second : spec->level);
                break;
            }
            case SignalRole::Conductor:
            case SignalRole::Sink: made.node = solver_.addConductor(); break;
            case SignalRole::Component: made.node = solver_.addComponent(spec->component); break;
            case SignalRole::None: return false;
        }
        // A component the world shows as on starts on, so a loaded circuit doesn't flicker
        if (spec->role == SignalRole::Component && !spec->show.empty()) {
            auto shown = BlockPropertyRegistry::global().property(type.name(), spec->show);
            if (shown) {
                SignalLevel level = 0;
                if (*shown == "true") level = solver_.maxLevel();
                else if (!shown->empty() && std::isdigit(static_cast<unsigned char>((*shown)[0]))) {
                    level = static_cast<SignalLevel>(std::min<long>(std::strtol(shown->c_str(), nullptr, 10), 255));
                }
                if (level > 0) solver_.presetLevel(made.node, level);
            }
        }
        nodeAt_[made.node] = pos;
        if (!spec->showEdges) newlyMade_.insert(pos);
        entries_[pos] = std::move(made);
        return true;
    }

    const auto& classes = SignalBlockRegistry::global().relayClasses();
    bool relay = !classes.empty() && isRelayBlock(type);
    if (relay) {
        relay = false;
        for (int f = 0; f < 6 && !relay; ++f) relay = specOf(world_.getBlock(offset(pos, static_cast<Face>(f)))) != nullptr;
    }
    if (!relay) {
        removeEntry(pos);
        return false;
    }
    if (e && !e->spec) {
        e->type = type;
        for (SignalNodeId r : e->relays) solver_.isolate(r);
        return true;
    }
    removeEntry(pos);
    Entry made;
    made.type = type;
    for (size_t i = 0; i < classes.size(); ++i) made.relays.push_back(solver_.addConductor());
    entries_[pos] = std::move(made);
    return true;
}

void SignalBinding::computeJoins(BlockCoord pos, Entry& e) {
    e.joined = 0;
    e.joinedUp = 0;
    const SignalBlockSpec& me = *e.spec;
    auto sameGroup = [&](const Entry* n) {
        return n && n->spec && n->spec->role == SignalRole::Conductor && n->spec->group == me.group;
    };
    for (int i = 0; i < 4; ++i) {
        Face h = HORIZONTAL[i];
        BlockCoord q = offset(pos, h);
        const Entry* n = entryAt(q);
        bool joined = sameGroup(n);
        if (!joined && me.climb) {
            const Entry* up = entryAt(offset(q, Face::PosY));
            const Entry* down = entryAt(offset(q, Face::NegY));
            if (sameGroup(up) && up->spec->climb && !opaqueAt(offset(pos, Face::PosY))) {
                joined = true;
                e.joinedUp |= static_cast<uint8_t>(1u << i);
            } else if (sameGroup(down) && down->spec->climb && !opaqueAt(q)) {
                joined = true;
            }
        }
        if (!joined && n && isDriver(n->spec)) {
            Face back = oppositeFace(h);
            const SignalBlockSpec& ns = *n->spec;
            joined = hasSignalFace(ns.out, back) || hasSignalFace(ns.in.faces, back) ||
                     hasSignalFace(ns.side.faces, back) || hasSignalFace(ns.hold.faces, back);
        }
        if (joined) e.joined |= static_cast<uint8_t>(1u << i);
    }
    // Where it points: everywhere when it joins nothing, along its line when it joins on one
    // axis only, nowhere when it turns
    constexpr uint8_t NS = 0b0101, EW = 0b1010;
    uint8_t dirs = 0;
    if (e.joined == 0) dirs = 0b1111;
    else if ((e.joined & ~NS) == 0) dirs = NS;
    else if ((e.joined & ~EW) == 0) dirs = EW;
    e.points = 0;
    for (int i = 0; i < 4; ++i) {
        if (dirs & (1u << i)) e.points |= signalFace(HORIZONTAL[i]);
    }
}

void SignalBinding::linkFrom(BlockCoord a, BlockCoord b) {
    const Entry* A = entryAt(a);
    const Entry* B = entryAt(b);
    if (!A || !B) return;
    auto towardB = faceToward(a, b);
    if (!towardB) return;
    const Face d = *towardB;
    const Face back = oppositeFace(d);
    const SignalBlockSpec* as = A->spec;
    const SignalBlockSpec* bs = B->spec;
    const auto& classes = SignalBlockRegistry::global().relayClasses();
    const bool aDrives = isDriver(as) && hasSignalFace(as->out, d);
    const bool aConductor = as && as->role == SignalRole::Conductor;
    const bool aRelay = !as;

    if (!bs) {
        // b is a relay: powered by what powers into it
        if (aRelay || (as && as->role == SignalRole::Sink)) return;
        for (size_t i = 0; i < classes.size() && i < B->relays.size(); ++i) {
            SignalFaces faces = as->powers(classes[i]);
            if (aConductor) faces = effectiveOut(*A, faces);
            if (hasSignalFace(faces, d)) solver_.link(A->node, B->relays[i], 0);
        }
        return;
    }

    switch (bs->role) {
        case SignalRole::Conductor:
            if (aConductor) {
                if (as->group == bs->group && d != Face::PosY && d != Face::NegY) {
                    solver_.link(A->node, B->node, as->loss);
                }
            } else if (aDrives) {
                solver_.link(A->node, B->node, 0);
            } else if (aRelay) {
                for (size_t i = 0; i < classes.size() && i < A->relays.size(); ++i) {
                    if (bs->readsClass(classes[i])) solver_.link(A->relays[i], B->node, 0);
                }
            }
            break;
        case SignalRole::Sink: {
            if (as && as->role == SignalRole::Sink) {
                if (as->group == bs->group && hasSignalFace(as->joinFaces, d) && hasSignalFace(bs->joinFaces, back)) {
                    solver_.link(A->node, B->node, 0);
                }
                break;
            }
            if (!hasSignalFace(bs->in.faces, back) || !(bs->in.from & fromKind(as))) break;
            if (aConductor) {
                if (hasSignalFace(effectiveOut(*A, as->out), d)) solver_.link(A->node, B->node, 0);
            } else if (aDrives) {
                solver_.link(A->node, B->node, 0);
            } else if (aRelay) {
                for (size_t i = 0; i < classes.size() && i < A->relays.size(); ++i) {
                    if (bs->readsClass(classes[i])) solver_.link(A->relays[i], B->node, 0);
                }
            }
            break;
        }
        case SignalRole::Component: {
            const std::pair<const SignalPortSpec*, SignalPort> ports[] = {
                {&bs->in, SignalPort::A}, {&bs->side, SignalPort::B}, {&bs->hold, SignalPort::Hold}};
            const bool aOffers = isDriver(as) && hasSignalFace(as->offer, d);
            for (const auto& [port, which] : ports) {
                if (!hasSignalFace(port->faces, back) || !(port->from & fromKind(as))) continue;
                if (aConductor || aDrives || aOffers) {
                    solver_.connect(A->node, B->node, which);
                } else if (aRelay) {
                    for (size_t i = 0; i < classes.size() && i < A->relays.size(); ++i) {
                        if (bs->readsClass(classes[i])) solver_.connect(A->relays[i], B->node, which);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void SignalBinding::linkClimb(BlockCoord a, BlockCoord b) {
    const Entry* A = entryAt(a);
    const Entry* B = entryAt(b);
    if (!A || !B || !A->spec || !B->spec) return;
    const SignalBlockSpec& as = *A->spec;
    const SignalBlockSpec& bs = *B->spec;
    if (as.role != SignalRole::Conductor || bs.role != SignalRole::Conductor || as.group != bs.group ||
        !as.climb || !bs.climb) {
        return;
    }
    // Up: nothing solid over a; down: nothing solid beside a, over b
    bool up = b.y > a.y;
    BlockCoord cut = up ? offset(a, Face::PosY) : BlockCoord(b.x, a.y, b.z);
    if (opaqueAt(cut)) return;
    solver_.link(A->node, B->node, as.loss);
}

void SignalBinding::refresh(const CoordSet& positions) {
    for (BlockCoord pos : positions) rebuildEntry(pos);
    for (BlockCoord pos : positions) {
        Entry* e = entryAt(pos);
        if (e && e->spec && e->spec->role == SignalRole::Conductor) computeJoins(pos, *e);
    }

    CoordLess less;
    for (BlockCoord q : positions) {
        if (!entryAt(q)) continue;
        for (int f = 0; f < 6; ++f) {
            BlockCoord r = offset(q, static_cast<Face>(f));
            if (positions.count(r) && less(r, q)) continue;
            linkFrom(q, r);
            linkFrom(r, q);
        }
        for (Face h : HORIZONTAL) {
            for (int dy : {-1, 1}) {
                BlockCoord r = offset(offset(q, h), 0, dy, 0);
                if (positions.count(r) && less(r, q)) continue;
                linkClimb(q, r);
                linkClimb(r, q);
            }
        }
    }

    for (BlockCoord pos : positions) {
        Entry* e = entryAt(pos);
        if (e && e->spec && e->spec->role == SignalRole::Conductor) showJoins(pos, *e);
        if (e && e->spec && !e->spec->showHold.empty()) showHold(pos, *e);
    }
}

// ============================================================================
// Showing
// ============================================================================

void SignalBinding::showResults() {
    std::vector<std::pair<BlockCoord, SignalLevel>> notices;
    for (const SignalChange& c : results_) {
        // Components whose hold this feeds may show it
        for (SignalNodeId r : solver_.readersOf(c.node)) {
            auto at = nodeAt_.find(r);
            if (at == nodeAt_.end()) continue;
            const Entry* re = entryAt(at->second);
            if (re && re->spec && !re->spec->showHold.empty()) showHold(at->second, *re);
        }
        auto it = nodeAt_.find(c.node);
        if (it == nodeAt_.end()) continue;
        newlyMade_.erase(it->second);
        if (const Entry* e = entryAt(it->second)) {
            showLevel(it->second, *e);
            if (e->spec && e->spec->notify) notices.emplace_back(it->second, c.level);
        }
    }
    results_.clear();
    // After the showing, so what the game sees is up to date; in position order, the same every run
    if (changeCallback_) {
        std::sort(notices.begin(), notices.end(),
                  [](const auto& a, const auto& b) { return CoordLess{}(a.first, b.first); });
        for (const auto& [pos, level] : notices) changeCallback_(pos, level);
    }
    for (BlockCoord pos : newlyMade_) {
        if (const Entry* e = entryAt(pos)) {
            showLevel(pos, *e);
            if (e->spec && !e->spec->showHold.empty()) showHold(pos, *e);
        }
    }
    newlyMade_.clear();
}

void SignalBinding::showLevel(BlockCoord pos, const Entry& e) {
    if (!e.spec || e.spec->show.empty() || e.node == NO_SIGNAL_NODE) return;
    const SignalLevel level = solver_.level(e.node);
    std::string family = BlockPropertyRegistry::familyOf(e.type.name());
    if (auto colon = family.find(':'); colon != std::string::npos) family = family.substr(colon + 1);
    const auto* props = BlockPropertyRegistry::global().properties(family);
    if (!props) return;
    for (const BlockProperty& p : *props) {
        if (p.name != e.spec->show) continue;
        // A number shows the level, as near as the values go; anything else shows on or off
        long best = -1;
        bool numeric = !p.values.empty();
        for (const std::string& v : p.values) {
            if (v.empty() || !std::isdigit(static_cast<unsigned char>(v[0]))) { numeric = false; break; }
            long n = std::strtol(v.c_str(), nullptr, 10);
            if (n <= level && n > best) best = n;
        }
        if (numeric && best >= 0) setProperty(pos, p.name, std::to_string(best));
        else if (!numeric) setProperty(pos, p.name, level > 0 ? "true" : "false");
        return;
    }
}

void SignalBinding::showHold(BlockCoord pos, const Entry& e) {
    if (e.node == NO_SIGNAL_NODE) return;
    setProperty(pos, e.spec->showHold, solver_.portLevel(e.node, SignalPort::Hold) > 0 ? "true" : "false");
}

void SignalBinding::showJoins(BlockCoord pos, const Entry& e) {
    const SignalBlockSpec& s = *e.spec;
    for (int i = 0; i < 4; ++i) {
        if (s.joins[i].empty()) continue;
        const std::string& value = (e.joinedUp & (1u << i)) ? s.joinValues[2]
                                 : (e.joined & (1u << i))   ? s.joinValues[1]
                                                            : s.joinValues[0];
        if (!value.empty()) setProperty(pos, s.joins[i], value);
    }
}

void SignalBinding::setProperty(BlockCoord pos, const std::string& prop, const std::string& value) {
    BlockTypeId current = world_.getBlock(pos);
    std::string key = std::to_string(current.id) + "|" + prop + "|" + value;
    auto it = withCache_.find(key);
    if (it == withCache_.end()) {
        BlockTypeId next = current;
        if (auto name = BlockPropertyRegistry::global().withProperty(current.name(), prop, value)) {
            next = BlockTypeId::fromName(*name);
        }
        it = withCache_.emplace(std::move(key), next).first;
    }
    if (it->second != current) writeBlock(pos, it->second);
}

void SignalBinding::writeBlock(BlockCoord pos, BlockTypeId type) {
    BlockTypeId old = world_.getBlock(pos);
    ownWrites_[pos] = type;
    world_.setBlock(pos, type);
    if (world_.lightEngine()) world_.enqueueLightingUpdateWithRemesh(pos, old, type);
    if (auto* mq = world_.meshRebuildQueue()) {
        for (const ChunkPos& affected : world_.getAffectedSubChunks(pos)) {
            mq->push(affected, MeshRebuildRequest::immediate());
        }
    }
}

}  // namespace finevox
