#pragma once

/**
 * @file block_model_transform.hpp
 * @brief Operations on a block's look: turning it, combining looks, and noticing a plain cube
 *
 * These let one shape serve many blocks. A block state's variant names another model and a
 * turn; the loader bakes the turn in here, once, so the mesher draws the result as it would
 * any other shape. Textures stay with their faces: a turned face keeps its texture, and with
 * lock-uv its UVs are taken from where it ends up instead.
 */

#include "finevox/core/block_model.hpp"

#include <string>
#include <vector>

namespace finevox {

/// The six faces of a unit cube as geometry, each naming the texture the model draws there.
/// Solid, so they occlude like a cube's.
[[nodiscard]] BlockGeometry cubeGeometry(const BlockModel& model);

/// A copy of `model` turned `xDegrees` about X, then `yDegrees` about Y (multiples of 90,
/// clockwise looking from the positive end of the axis). Geometry, collision and hit shapes
/// all turn. A cube becomes geometry first, and is a cube again if it still is one.
[[nodiscard]] BlockModel turnedModel(const BlockModel& model, int xDegrees, int yDegrees,
                                     bool lockUV);

/// If `model`'s geometry is exactly a unit cube's six faces, each showing a whole texture,
/// possibly turned, set those as its cube textures and drop the geometry. A cube draws with
/// merged faces and ambient occlusion; the same faces as geometry would get neither.
/// Returns whether it did.
bool drawAsCubeIfPlain(BlockModel& model);

/// Several looks drawn together, as one: every face of each, and the union of their shapes
[[nodiscard]] BlockModel combinedLook(const std::vector<BlockModel>& parts);

/// A texture name with a turn added: `name@90` plus 90 is `name@180`. 0 drops the suffix.
[[nodiscard]] std::string turnedTextureName(const std::string& texture, int degrees);

/// Split `name@180` into `name` and 180 (0 when there's no suffix)
void splitTextureTurn(const std::string& texture, std::string& base, int& degrees);

}  // namespace finevox
