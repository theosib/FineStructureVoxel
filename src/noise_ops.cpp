/**
 * @file noise_ops.cpp
 * @brief Composable noise operations and convenience factories
 *
 * Design: [27-world-generation.md] Section 27.2.4, 27.2.5
 */

#include "finevox/noise_ops.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace finevox {

// ============================================================================
// FBM (Fractal Brownian Motion)
// ============================================================================

FBMNoise2D::FBMNoise2D(std::unique_ptr<Noise2D> base, int octaves,
                         float lacunarity, float persistence)
    : base_(std::move(base)), octaves_(octaves),
      lacunarity_(lacunarity), persistence_(persistence) {
}

float FBMNoise2D::evaluate(float x, float z) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves_; ++i) {
        value += base_->evaluate(x * frequency, z * frequency) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= persistence_;
        frequency *= lacunarity_;
    }

    return value / maxAmplitude;
}

FBMNoise3D::FBMNoise3D(std::unique_ptr<Noise3D> base, int octaves,
                         float lacunarity, float persistence)
    : base_(std::move(base)), octaves_(octaves),
      lacunarity_(lacunarity), persistence_(persistence) {
}

float FBMNoise3D::evaluate(float x, float y, float z) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves_; ++i) {
        value += base_->evaluate(x * frequency, y * frequency, z * frequency) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= persistence_;
        frequency *= lacunarity_;
    }

    return value / maxAmplitude;
}

// ============================================================================
// Ridged multi-fractal
// ============================================================================

RidgedNoise2D::RidgedNoise2D(std::unique_ptr<Noise2D> base, int octaves,
                               float lacunarity, float gain)
    : base_(std::move(base)), octaves_(octaves),
      lacunarity_(lacunarity), gain_(gain) {
    // Precompute max possible accumulated value (assumes signal=1 each octave)
    float w = 1.0f;
    maxValue_ = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        maxValue_ += w;
        w = std::clamp(w * gain, 0.0f, 1.0f);
    }
}

float RidgedNoise2D::evaluate(float x, float z) const {
    float value = 0.0f;
    float weight = 1.0f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves_; ++i) {
        float signal = base_->evaluate(x * frequency, z * frequency);
        signal = 1.0f - std::abs(signal);  // Create ridge
        signal *= signal;                    // Sharpen
        signal *= weight;                    // Weight by previous octave

        weight = std::clamp(signal * gain_, 0.0f, 1.0f);
        value += signal;
        frequency *= lacunarity_;
    }

    // Normalize [0, maxValue_] to [-1, 1]
    return value * 2.0f / maxValue_ - 1.0f;
}

RidgedNoise3D::RidgedNoise3D(std::unique_ptr<Noise3D> base, int octaves,
                               float lacunarity, float gain)
    : base_(std::move(base)), octaves_(octaves),
      lacunarity_(lacunarity), gain_(gain) {
    float w = 1.0f;
    maxValue_ = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        maxValue_ += w;
        w = std::clamp(w * gain, 0.0f, 1.0f);
    }
}

float RidgedNoise3D::evaluate(float x, float y, float z) const {
    float value = 0.0f;
    float weight = 1.0f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves_; ++i) {
        float signal = base_->evaluate(x * frequency, y * frequency, z * frequency);
        signal = 1.0f - std::abs(signal);
        signal *= signal;
        signal *= weight;

        weight = std::clamp(signal * gain_, 0.0f, 1.0f);
        value += signal;
        frequency *= lacunarity_;
    }

    return value * 2.0f / maxValue_ - 1.0f;
}

// ============================================================================
// Billow noise
// ============================================================================

BillowNoise2D::BillowNoise2D(std::unique_ptr<Noise2D> base, int octaves,
                               float lacunarity, float persistence)
    : base_(std::move(base)), octaves_(octaves),
      lacunarity_(lacunarity), persistence_(persistence) {
}

float BillowNoise2D::evaluate(float x, float z) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves_; ++i) {
        float signal = std::abs(base_->evaluate(x * frequency, z * frequency));
        value += signal * amplitude;
        maxAmplitude += amplitude;
        amplitude *= persistence_;
        frequency *= lacunarity_;
    }

    // Map from [0, 1] to [-1, 1]
    return (value / maxAmplitude) * 2.0f - 1.0f;
}

BillowNoise3D::BillowNoise3D(std::unique_ptr<Noise3D> base, int octaves,
                               float lacunarity, float persistence)
    : base_(std::move(base)), octaves_(octaves),
      lacunarity_(lacunarity), persistence_(persistence) {
}

float BillowNoise3D::evaluate(float x, float y, float z) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves_; ++i) {
        float signal = std::abs(base_->evaluate(x * frequency, y * frequency, z * frequency));
        value += signal * amplitude;
        maxAmplitude += amplitude;
        amplitude *= persistence_;
        frequency *= lacunarity_;
    }

    return (value / maxAmplitude) * 2.0f - 1.0f;
}

// ============================================================================
// Domain warping
// ============================================================================

DomainWarp2D::DomainWarp2D(std::unique_ptr<Noise2D> source,
                             std::unique_ptr<Noise2D> warpX,
                             std::unique_ptr<Noise2D> warpZ,
                             float warpStrength)
    : source_(std::move(source)), warpX_(std::move(warpX)),
      warpZ_(std::move(warpZ)), warpStrength_(warpStrength) {
}

float DomainWarp2D::evaluate(float x, float z) const {
    float wx = warpX_->evaluate(x, z) * warpStrength_;
    float wz = warpZ_->evaluate(x, z) * warpStrength_;
    return source_->evaluate(x + wx, z + wz);
}

DomainWarp3D::DomainWarp3D(std::unique_ptr<Noise3D> source,
                             std::unique_ptr<Noise3D> warpX,
                             std::unique_ptr<Noise3D> warpY,
                             std::unique_ptr<Noise3D> warpZ,
                             float warpStrength)
    : source_(std::move(source)), warpX_(std::move(warpX)),
      warpY_(std::move(warpY)), warpZ_(std::move(warpZ)),
      warpStrength_(warpStrength) {
}

float DomainWarp3D::evaluate(float x, float y, float z) const {
    float wx = warpX_->evaluate(x, y, z) * warpStrength_;
    float wy = warpY_->evaluate(x, y, z) * warpStrength_;
    float wz = warpZ_->evaluate(x, y, z) * warpStrength_;
    return source_->evaluate(x + wx, y + wy, z + wz);
}

// ============================================================================
// Utility adapters
// ============================================================================

ScaledNoise2D::ScaledNoise2D(std::unique_ptr<Noise2D> source,
                               float frequencyX, float frequencyZ,
                               float amplitude, float offset)
    : source_(std::move(source)), freqX_(frequencyX), freqZ_(frequencyZ),
      amplitude_(amplitude), offset_(offset) {
}

float ScaledNoise2D::evaluate(float x, float z) const {
    return source_->evaluate(x * freqX_, z * freqZ_) * amplitude_ + offset_;
}

ScaledNoise3D::ScaledNoise3D(std::unique_ptr<Noise3D> source,
                               float frequencyX, float frequencyY, float frequencyZ,
                               float amplitude, float offset)
    : source_(std::move(source)), freqX_(frequencyX), freqY_(frequencyY),
      freqZ_(frequencyZ), amplitude_(amplitude), offset_(offset) {
}

float ScaledNoise3D::evaluate(float x, float y, float z) const {
    return source_->evaluate(x * freqX_, y * freqY_, z * freqZ_) * amplitude_ + offset_;
}

ClampedNoise2D::ClampedNoise2D(std::unique_ptr<Noise2D> source,
                                 float minVal, float maxVal)
    : source_(std::move(source)), minVal_(minVal), maxVal_(maxVal) {
}

float ClampedNoise2D::evaluate(float x, float z) const {
    return std::clamp(source_->evaluate(x, z), minVal_, maxVal_);
}

CombinedNoise2D::CombinedNoise2D(std::unique_ptr<Noise2D> a,
                                   std::unique_ptr<Noise2D> b,
                                   CombineOp op, float blendFactor)
    : a_(std::move(a)), b_(std::move(b)), op_(op), blendFactor_(blendFactor) {
}

float CombinedNoise2D::evaluate(float x, float z) const {
    float va = a_->evaluate(x, z);
    float vb = b_->evaluate(x, z);

    switch (op_) {
        case CombineOp::Add:      return va + vb;
        case CombineOp::Multiply: return va * vb;
        case CombineOp::Min:      return std::min(va, vb);
        case CombineOp::Max:      return std::max(va, vb);
        case CombineOp::Lerp:     return va + blendFactor_ * (vb - va);
    }
    return va;  // unreachable
}

MappedNoise2D::MappedNoise2D(std::unique_ptr<Noise2D> source,
                               std::function<float(float)> mapFunc)
    : source_(std::move(source)), mapFunc_(std::move(mapFunc)) {
}

float MappedNoise2D::evaluate(float x, float z) const {
    return mapFunc_(source_->evaluate(x, z));
}

// ============================================================================
// NoiseFactory convenience functions
// ============================================================================

namespace NoiseFactory {

std::unique_ptr<Noise2D> perlinFBM(uint64_t seed, int octaves, float frequency) {
    auto base = std::make_unique<PerlinNoise2D>(seed);
    auto fbm = std::make_unique<FBMNoise2D>(std::move(base), octaves);
    return std::make_unique<ScaledNoise2D>(std::move(fbm), frequency, frequency);
}

std::unique_ptr<Noise2D> simplexFBM(uint64_t seed, int octaves, float frequency) {
    auto base = std::make_unique<OpenSimplex2D>(seed);
    auto fbm = std::make_unique<FBMNoise2D>(std::move(base), octaves);
    return std::make_unique<ScaledNoise2D>(std::move(fbm), frequency, frequency);
}

std::unique_ptr<Noise2D> ridgedMountains(uint64_t seed, float frequency) {
    auto base = std::make_unique<PerlinNoise2D>(seed);
    auto ridged = std::make_unique<RidgedNoise2D>(std::move(base), 6);
    return std::make_unique<ScaledNoise2D>(std::move(ridged), frequency, frequency);
}

std::unique_ptr<Noise2D> warpedTerrain(uint64_t seed, float frequency) {
    auto source = std::make_unique<PerlinNoise2D>(seed);
    auto sourceFbm = std::make_unique<FBMNoise2D>(std::move(source), 6);
    auto sourceScaled = std::make_unique<ScaledNoise2D>(
        std::move(sourceFbm), frequency, frequency);

    auto warpXBase = std::make_unique<PerlinNoise2D>(
        NoiseHash::deriveSeed(seed, 100));
    auto warpXFbm = std::make_unique<FBMNoise2D>(std::move(warpXBase), 4);
    auto warpX = std::make_unique<ScaledNoise2D>(
        std::move(warpXFbm), frequency * 2.0f, frequency * 2.0f);

    auto warpZBase = std::make_unique<PerlinNoise2D>(
        NoiseHash::deriveSeed(seed, 200));
    auto warpZFbm = std::make_unique<FBMNoise2D>(std::move(warpZBase), 4);
    auto warpZ = std::make_unique<ScaledNoise2D>(
        std::move(warpZFbm), frequency * 2.0f, frequency * 2.0f);

    return std::make_unique<DomainWarp2D>(
        std::move(sourceScaled), std::move(warpX), std::move(warpZ), 50.0f);
}

std::unique_ptr<Noise3D> perlinFBM3D(uint64_t seed, int octaves, float frequency) {
    auto base = std::make_unique<PerlinNoise3D>(seed);
    auto fbm = std::make_unique<FBMNoise3D>(std::move(base), octaves);
    return std::make_unique<ScaledNoise3D>(
        std::move(fbm), frequency, frequency, frequency);
}

std::unique_ptr<Noise3D> simplexFBM3D(uint64_t seed, int octaves, float frequency) {
    auto base = std::make_unique<OpenSimplex3D>(seed);
    auto fbm = std::make_unique<FBMNoise3D>(std::move(base), octaves);
    return std::make_unique<ScaledNoise3D>(
        std::move(fbm), frequency, frequency, frequency);
}

}  // namespace NoiseFactory

}  // namespace finevox
