#include "finevox/core/particle_type.hpp"

#include <charconv>
#include <string>

namespace finevox {

// ============================================================================
// BillboardMode parse/name
// ============================================================================

BillboardMode parseBillboardMode(std::string_view s) {
    if (s == "camera" || s == "camera_facing" || s == "face_camera") {
        return BillboardMode::CameraFacing;
    }
    if (s == "velocity" || s == "velocity_stretched" || s == "stretched") {
        return BillboardMode::VelocityStretched;
    }
    if (s == "axis" || s == "axis_locked" || s == "y_axis") {
        return BillboardMode::AxisLocked;
    }
    return BillboardMode::CameraFacing;
}

std::string_view billboardModeName(BillboardMode m) {
    switch (m) {
        case BillboardMode::CameraFacing:      return "camera";
        case BillboardMode::VelocityStretched: return "velocity";
        case BillboardMode::AxisLocked:        return "axis";
    }
    return "camera";
}

// ============================================================================
// EmitterShape parse/name
// ============================================================================

EmitterShape parseEmitterShape(std::string_view s) {
    if (s == "point")  return EmitterShape::Point;
    if (s == "sphere") return EmitterShape::Sphere;
    if (s == "box")    return EmitterShape::Box;
    if (s == "cone")   return EmitterShape::Cone;
    if (s == "line")   return EmitterShape::Line;
    return EmitterShape::Point;
}

std::string_view emitterShapeName(EmitterShape s) {
    switch (s) {
        case EmitterShape::Point:  return "point";
        case EmitterShape::Sphere: return "sphere";
        case EmitterShape::Box:    return "box";
        case EmitterShape::Cone:   return "cone";
        case EmitterShape::Line:   return "line";
    }
    return "point";
}

// ============================================================================
// Range::parse — "a" or "a-b" (matches LootTableLoader::parseRange)
// ============================================================================

namespace {

std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

float parseFloatSafe(std::string_view s) {
    s = trimView(s);
    if (s.empty()) return 0.0f;
    try { return std::stof(std::string(s)); }
    catch (...) { return 0.0f; }
}

} // anonymous namespace

Range Range::parse(std::string_view s) {
    s = trimView(s);
    if (s.empty()) return Range{0.0f, 0.0f};

    // Skip leading '-' on negative numbers when looking for split dash
    size_t start = 0;
    if (s[0] == '-') start = 1;
    auto dash = s.find('-', start);
    if (dash != std::string_view::npos) {
        float lo = parseFloatSafe(s.substr(0, dash));
        float hi = parseFloatSafe(s.substr(dash + 1));
        return Range{lo, hi};
    }
    float v = parseFloatSafe(s);
    return Range{v, v};
}

} // namespace finevox
