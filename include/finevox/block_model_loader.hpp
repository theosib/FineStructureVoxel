#pragma once

/**
 * @file block_model_loader.hpp
 * @brief Loader for block model spec files
 *
 * Design: [19-block-models.md]
 *
 * Uses existing ConfigParser to parse .model, .geom, and .collision files.
 * The file format is the same YAML-like format used by ConfigParser:
 *
 * .model file:
 *   include: base/stairs
 *   geometry: shapes/stairs_faces
 *   collision: shapes/stairs_collision
 *   texture: blocks/oak_planks
 *   rotations: horizontal-flip
 *   hardness: 1.5
 *
 * .geom file:
 *   face:bottom:
 *       0 0 1  0 1
 *       0 0 0  0 0
 *       1 0 0  1 0
 *       1 0 1  1 1
 *   face:top:
 *       0 1 0  0 0
 *       ...
 *   solid-faces: bottom
 *
 * .collision file:
 *   box:
 *       0 0 0
 *       1 0.5 1
 */

#include "finevox/block_model.hpp"
#include "finevox/config_parser.hpp"
#include <string>
#include <optional>
#include <functional>
#include <unordered_set>

namespace finevox {

// Forward declaration
class ResourceLocator;

/**
 * @brief Loader for block model specification files
 *
 * Wraps ConfigParser to load .model, .geom, and .collision files
 * and converts them to BlockModel, BlockGeometry, and CollisionShape.
 */
class BlockModelLoader {
public:
    /// File resolver callback (logical path → filesystem path)
    using FileResolver = std::function<std::string(const std::string&)>;

    BlockModelLoader() = default;

    /**
     * @brief Set the file resolver for include directives and file references
     * @param resolver Function that converts logical paths to filesystem paths
     */
    void setFileResolver(FileResolver resolver);

    /**
     * @brief Load a complete block model from a .model file
     * @param path Filesystem path to the .model file
     * @return Loaded BlockModel, or nullopt on error
     */
    [[nodiscard]] std::optional<BlockModel> loadModel(const std::string& path);

    /**
     * @brief Load geometry from a .geom file
     * @param path Filesystem path to the .geom file
     * @return Loaded BlockGeometry, or nullopt on error
     */
    [[nodiscard]] std::optional<BlockGeometry> loadGeometry(const std::string& path);

    /**
     * @brief Load collision shape from a .collision file
     * @param path Filesystem path to the .collision file
     * @return Loaded CollisionShape, or nullopt on error
     */
    [[nodiscard]] std::optional<CollisionShape> loadCollision(const std::string& path);

    /**
     * @brief Parse geometry from a ConfigDocument
     * @param doc Parsed config document
     * @return BlockGeometry constructed from the document
     */
    [[nodiscard]] BlockGeometry parseGeometryFromDocument(const ConfigDocument& doc);

    /**
     * @brief Parse collision shape from a ConfigDocument
     * @param doc Parsed config document
     * @return CollisionShape constructed from the document
     */
    [[nodiscard]] CollisionShape parseCollisionFromDocument(const ConfigDocument& doc);

    /**
     * @brief Parse geometry from a string (for testing)
     * @param content String containing geometry definition
     * @return BlockGeometry if parsing succeeds
     */
    [[nodiscard]] std::optional<BlockGeometry> parseGeometryFromString(const std::string& content);

    /**
     * @brief Parse collision from a string (for testing)
     * @param content String containing collision definition
     * @return CollisionShape if parsing succeeds
     */
    [[nodiscard]] std::optional<CollisionShape> parseCollisionFromString(const std::string& content);

    /**
     * @brief Parse model from a string (for testing)
     * @param content String containing model definition
     * @return BlockModel if parsing succeeds
     */
    [[nodiscard]] std::optional<BlockModel> parseModelFromString(const std::string& content);

    /**
     * @brief Get the last error message
     */
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    FileResolver resolver_;
    ConfigParser parser_;
    mutable std::string lastError_;
    std::unordered_set<std::string> loadingStack_;  // For cycle detection

    /**
     * @brief Parse a face entry into FaceGeometry
     * @param entry ConfigEntry with suffix as face name and dataLines as vertices
     * @param nextCustomIndex Next available index for custom faces
     * @return Parsed FaceGeometry, or nullopt on error
     */
    [[nodiscard]] std::optional<FaceGeometry> parseFaceEntry(
        const ConfigEntry& entry,
        int& nextCustomIndex);

    /**
     * @brief Parse vertex data line (x y z [u v])
     * @param data Float array from config (3 or 5 elements)
     * @return Parsed ModelVertex, or nullopt on error
     */
    [[nodiscard]] std::optional<ModelVertex> parseVertex(const std::vector<float>& data);

    /**
     * @brief Parse solid-faces directive
     * @param value Space-separated list of face names
     * @return Set of face indices that are solid
     */
    [[nodiscard]] std::unordered_set<int> parseSolidFaces(std::string_view value);

    /**
     * @brief Resolve a file reference to a filesystem path
     * @param ref File reference (may be relative or logical)
     * @param basePath Base path for relative resolution
     * @return Resolved filesystem path
     */
    [[nodiscard]] std::string resolveFile(const std::string& ref, const std::string& basePath);

    /**
     * @brief Check if an extension matches a type
     */
    [[nodiscard]] static bool hasExtension(const std::string& path, const std::string& ext);

    /**
     * @brief Add default extension if none present
     */
    [[nodiscard]] static std::string ensureExtension(const std::string& path, const std::string& ext);

    /**
     * @brief Get directory part of a path
     */
    [[nodiscard]] static std::string getDirectory(const std::string& path);
};

/**
 * @brief Create a BlockModelLoader with ResourceLocator integration
 */
BlockModelLoader createBlockModelLoader(ResourceLocator& locator);

}  // namespace finevox
