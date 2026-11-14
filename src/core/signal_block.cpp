#include "finevox/core/signal_block.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace finevox {

namespace {

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream iss(normalized);
    std::string w;
    while (iss >> w) out.push_back(w);
    return out;
}

std::optional<Face> absoluteFace(std::string_view name) {
    if (name == "west") return Face::NegX;
    if (name == "east") return Face::PosX;
    if (name == "down") return Face::NegY;
    if (name == "up") return Face::PosY;
    if (name == "north") return Face::NegZ;
    if (name == "south") return Face::PosZ;
    return std::nullopt;
}

// Clockwise seen from above
Face clockwise(Face f) {
    switch (f) {
        case Face::NegZ: return Face::PosX;
        case Face::PosX: return Face::PosZ;
        case Face::PosZ: return Face::NegX;
        case Face::NegX: return Face::NegZ;
        default: return f;
    }
}

struct Parser {
    const SignalKeys& keys;
    std::optional<Face> front;
    std::string error;

    const std::string* get(const std::string& key) const {
        auto it = keys.find(key);
        return it == keys.end() ? nullptr : &it->second;
    }

    SignalFaces faces(const std::string& key) {
        const std::string* text = get(key);
        return text ? facesOf(*text, key) : SignalFaces{0};
    }

    SignalFaces facesOf(const std::string& text, const std::string& key) {
        SignalFaces out = 0;
        for (const std::string& w : words(text)) {
            if (w == "all") {
                out |= SIGNAL_ALL_FACES;
            } else if (w == "sides") {
                out |= signalFace(Face::NegX) | signalFace(Face::PosX) | signalFace(Face::NegZ) |
                       signalFace(Face::PosZ);
            } else if (w == "points") {
                out |= SIGNAL_POINTS;
            } else if (auto f = absoluteFace(w)) {
                out |= signalFace(*f);
            } else if (w == "front" || w == "back" || w == "left" || w == "right") {
                if (!front) {
                    error = key + ": '" + w + "' needs a facing property";
                    continue;
                }
                Face f = *front;
                if (w == "back") f = oppositeFace(f);
                const bool horizontal = f != Face::PosY && f != Face::NegY;
                if (w == "right" || w == "left") {
                    if (!horizontal) continue;  // no left or right when facing up or down
                    f = clockwise(f);
                    if (w == "left") f = oppositeFace(f);
                }
                out |= signalFace(f);
            } else {
                error = key + ": unknown face '" + w + "'";
            }
        }
        return out;
    }

    SignalFrom from(const std::string& key) {
        const std::string* text = get(key);
        if (!text) return SIGNAL_FROM_ALL;
        SignalFrom out = 0;
        for (const std::string& w : words(*text)) {
            if (w == "conductor") out |= SIGNAL_FROM_CONDUCTOR;
            else if (w == "source") out |= SIGNAL_FROM_SOURCE;
            else if (w == "component") out |= SIGNAL_FROM_COMPONENT;
            else if (w == "relay") out |= SIGNAL_FROM_RELAY;
            else if (w == "all") out |= SIGNAL_FROM_ALL;
            else error = key + ": unknown kind '" + w + "'";
        }
        return out;
    }

    int number(const std::string& key, int fallback, int lo, int hi) {
        const std::string* text = get(key);
        if (!text) return fallback;
        char* end = nullptr;
        long v = std::strtol(text->c_str(), &end, 10);
        if (end == text->c_str()) {
            error = key + ": not a number";
            return fallback;
        }
        return static_cast<int>(std::clamp<long>(v, lo, hi));
    }

    bool flag(const std::string& key) {
        const std::string* text = get(key);
        return text && (*text == "true" || *text == "yes" || *text == "1");
    }
};

std::optional<SignalOp> opNamed(std::string_view name) {
    if (name == "buffer") return SignalOp::Buffer;
    if (name == "invert") return SignalOp::Invert;
    if (name == "compare") return SignalOp::Compare;
    if (name == "subtract") return SignalOp::Subtract;
    if (name == "max") return SignalOp::Max;
    if (name == "min") return SignalOp::Min;
    if (name == "parity") return SignalOp::Parity;
    if (name == "latch") return SignalOp::Latch;
    if (name == "toggle") return SignalOp::Toggle;
    return std::nullopt;
}

}  // namespace

bool SignalBlockSpec::readsClass(InternedId cls) const {
    return reads.empty() || std::find(reads.begin(), reads.end(), cls) != reads.end();
}

SignalFaces SignalBlockSpec::powers(InternedId cls) const {
    for (const auto& [c, faces] : power) {
        if (c == cls) return faces;
    }
    return 0;
}

bool SignalBlockSpec::sameShape(const SignalBlockSpec& o) const {
    return role == o.role && group == o.group && loss == o.loss && climb == o.climb &&
           in == o.in && side == o.side && hold == o.hold && out == o.out && power == o.power &&
           reads == o.reads && joinFaces == o.joinFaces;
}

std::optional<SignalBlockSpec> parseSignalSpec(
    const SignalKeys& keys,
    const std::function<std::optional<std::string>(std::string_view)>& property,
    std::string* error) {
    auto roleIt = keys.find("signal");
    if (roleIt == keys.end()) return std::nullopt;

    Parser p{keys, std::nullopt, {}};
    const std::string* facingProp = p.get("signal-facing");
    if (property) {
        if (auto value = property(facingProp ? *facingProp : std::string("facing"))) {
            p.front = absoluteFace(*value);
        }
    }

    SignalBlockSpec spec;
    const std::string& role = roleIt->second;
    if (role == "source") spec.role = SignalRole::Source;
    else if (role == "conductor") spec.role = SignalRole::Conductor;
    else if (role == "component") spec.role = SignalRole::Component;
    else if (role == "sink") spec.role = SignalRole::Sink;
    else if (role == "none") spec.role = SignalRole::None;
    else p.error = "signal: unknown role '" + role + "'";

    if (const std::string* g = p.get("signal-group")) spec.group = StringInterner::global().intern(*g);
    spec.loss = static_cast<SignalLevel>(p.number("signal-loss", 1, 0, 255));
    spec.climb = p.flag("signal-climb");
    spec.level = static_cast<SignalLevel>(p.number("signal-level", 0, 0, 255));

    if (const std::string* opName = p.get("signal-op")) {
        if (auto op = opNamed(*opName)) spec.component.op = *op;
        else p.error = "signal-op: unknown op '" + *opName + "'";
    }
    spec.component.delay = static_cast<uint16_t>(p.number("signal-delay", 1, 1, 65535));
    spec.component.minPulse = static_cast<uint16_t>(p.number("signal-min-pulse", 0, 0, 65535));
    spec.component.fullOutput = p.flag("signal-full");

    spec.in = {p.faces("signal-in"), p.from("signal-in-from")};
    spec.side = {p.faces("signal-side"), p.from("signal-side-from")};
    spec.hold = {p.faces("signal-hold"), p.from("signal-hold-from")};
    spec.out = p.faces("signal-out");
    spec.joinFaces = p.faces("signal-join-faces");
    spec.showEdges = p.flag("signal-show-edges");

    for (const auto& [key, value] : keys) {
        if (key.rfind("signal-power:", 0) == 0) {
            InternedId cls = StringInterner::global().intern(key.substr(13));
            spec.power.emplace_back(cls, p.facesOf(value, key));
        }
    }
    std::sort(spec.power.begin(), spec.power.end());
    if (const std::string* r = p.get("signal-reads")) {
        for (const std::string& w : words(*r)) spec.reads.push_back(StringInterner::global().intern(w));
        std::sort(spec.reads.begin(), spec.reads.end());
    }

    if (const std::string* s = p.get("signal-show")) spec.show = *s;
    if (const std::string* j = p.get("signal-joins")) {
        auto w = words(*j);
        if (w.size() == 4) std::copy(w.begin(), w.end(), spec.joins.begin());
        else p.error = "signal-joins: needs four properties, north east south west";
    }
    if (const std::string* v = p.get("signal-join-values")) {
        auto w = words(*v);
        if (w.size() == 3) std::copy(w.begin(), w.end(), spec.joinValues.begin());
        else p.error = "signal-join-values: needs three values: not joined, joined, joined up";
    }

    if (error) *error = p.error;
    return spec;
}

// ============================================================================
// Registry
// ============================================================================

SignalBlockRegistry& SignalBlockRegistry::global() {
    static SignalBlockRegistry registry;
    return registry;
}

void SignalBlockRegistry::set(BlockTypeId type, SignalBlockSpec spec) {
    auto addClass = [this](InternedId cls) {
        if (std::find(relayClasses_.begin(), relayClasses_.end(), cls) == relayClasses_.end()) {
            relayClasses_.push_back(cls);
            std::sort(relayClasses_.begin(), relayClasses_.end());
        }
    };
    for (const auto& [cls, faces] : spec.power) addClass(cls);
    for (InternedId cls : spec.reads) addClass(cls);
    specs_[type.id] = std::move(spec);
}

const SignalBlockSpec* SignalBlockRegistry::get(BlockTypeId type) const {
    auto it = specs_.find(type.id);
    return it == specs_.end() ? nullptr : &it->second;
}

void SignalBlockRegistry::clear() {
    specs_.clear();
    relayClasses_.clear();
}

}  // namespace finevox
