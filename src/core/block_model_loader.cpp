#include "finevox/core/block_model_loader.hpp"
#include "finevox/core/resource_locator.hpp"
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace finevox {

// ============================================================================
// BlockModelLoader Implementation
// ============================================================================

void BlockModelLoader::setFileResolver(FileResolver resolver) {
    resolver_ = std::move(resolver);
    parser_.setIncludeResolver([this](const std::string& path) -> std::string {
        if (resolver_) {
            // ConfigParser calls with raw path; add .model extension for model includes
            std::string pathWithExt = ensureExtension(path, ".model");
            return resolver_(pathWithExt);
        }
        return path;
    });
}

std::optional<BlockModel> BlockModelLoader::loadModel(const std::string& path) {
    // Cycle detection
    if (loadingStack_.count(path) > 0) {
        lastError_ = "Circular include detected: " + path;
        return std::nullopt;
    }
    loadingStack_.insert(path);

    auto cleanup = [this, &path]() {
        loadingStack_.erase(path);
    };

    auto doc = parser_.parseFile(path);
    if (!doc) {
        lastError_ = "Failed to parse model file: " + path;
        cleanup();
        return std::nullopt;
    }

    BlockModel model;
    std::string baseDir = getDirectory(path);

    // Process includes first (inherit properties)
    for (auto* entry : doc->getAll("include")) {
        std::string includePath = resolveFile(
            ensureExtension(entry->value.asStringOwned(), ".model"),
            baseDir
        );

        auto included = loadModel(includePath);
        if (included) {
            // Inherit from included model (later values override)
            model = std::move(*included);
        }
    }

    // Load geometry if specified
    if (auto* geomEntry = doc->get("geometry")) {
        std::string geomPath = resolveFile(
            ensureExtension(geomEntry->value.asStringOwned(), ".geom"),
            baseDir
        );

        auto geometry = loadGeometry(geomPath);
        if (geometry) {
            model.setGeometry(std::move(*geometry));
        }
    }

    // Load collision if specified
    if (auto* collEntry = doc->get("collision")) {
        std::string value = collEntry->value.asStringOwned();

        if (value == "none") {
            model.setCollision(CollisionShape::NONE);
        } else if (value == "full") {
            model.setCollision(CollisionShape::FULL_BLOCK);
        } else {
            std::string collPath = resolveFile(
                ensureExtension(value, ".collision"),
                baseDir
            );

            auto collision = loadCollision(collPath);
            if (collision) {
                model.setCollision(std::move(*collision));
            }
        }
    }

    // Load hit shape if specified
    if (auto* hitEntry = doc->get("hit")) {
        std::string value = hitEntry->value.asStringOwned();

        if (value == "none") {
            model.setHit(CollisionShape::NONE);
        } else if (value == "full") {
            model.setHit(CollisionShape::FULL_BLOCK);
        } else if (value != "inherit") {
            std::string hitPath = resolveFile(
                ensureExtension(value, ".collision"),
                baseDir
            );

            auto hit = loadCollision(hitPath);
            if (hit) {
                model.setHit(std::move(*hit));
            }
        }
        // "inherit" means use collision (default behavior)
    }

    // Parse rotation constraint
    if (auto* rotEntry = doc->get("rotations")) {
        std::string value = rotEntry->value.asStringOwned();
        RotationSet set = parseRotationSet(value);

        if (set == RotationSet::Custom) {
            // Parse explicit indices
            std::vector<uint8_t> indices;
            std::istringstream iss(value);
            int idx;
            while (iss >> idx) {
                if (idx >= 0 && idx < 24) {
                    indices.push_back(static_cast<uint8_t>(idx));
                }
            }
            if (!indices.empty()) {
                model.setRotations(std::move(indices));
            }
        } else {
            model.setRotations(set);
        }
    }

    // Parse other properties
    if (auto* hardEntry = doc->get("hardness")) {
        model.setHardness(hardEntry->value.asFloat(1.0f));
    }

    if (auto* texEntry = doc->get("texture")) {
        model.setTexture(texEntry->value.asStringOwned());
    }

    if (auto* soundEntry = doc->get("sounds")) {
        model.setSounds(soundEntry->value.asStringOwned());
    }

    if (auto* lightEntry = doc->get("light-emission")) {
        model.setLightEmission(static_cast<uint8_t>(lightEntry->value.asInt(0)));
    }

    if (auto* attenEntry = doc->get("light-attenuation")) {
        model.setLightAttenuation(static_cast<uint8_t>(attenEntry->value.asInt(15)));
    }

    cleanup();
    return model;
}

std::optional<BlockGeometry> BlockModelLoader::loadGeometry(const std::string& path) {
    auto doc = parser_.parseFile(path);
    if (!doc) {
        lastError_ = "Failed to parse geometry file: " + path;
        return std::nullopt;
    }

    return parseGeometryFromDocument(*doc);
}

std::optional<CollisionShape> BlockModelLoader::loadCollision(const std::string& path) {
    auto doc = parser_.parseFile(path);
    if (!doc) {
        lastError_ = "Failed to parse collision file: " + path;
        return std::nullopt;
    }

    return parseCollisionFromDocument(*doc);
}

BlockGeometry BlockModelLoader::parseGeometryFromDocument(const ConfigDocument& doc) {
    BlockGeometry geometry;
    int nextCustomIndex = 6;

    // Parse solid-faces first to know which faces are solid
    std::unordered_set<int> solidFaces;
    if (auto* solidEntry = doc.get("solid-faces")) {
        solidFaces = parseSolidFaces(solidEntry->value.asString());
    }

    // Parse all face entries
    for (auto* entry : doc.getAll("face")) {
        auto face = parseFaceEntry(*entry, nextCustomIndex);
        if (face && face->isValid()) {
            // Mark as solid if in solid-faces list
            if (solidFaces.count(face->faceIndex) > 0) {
                face->isSolid = true;
            }
            geometry.addFace(std::move(*face));
        }
    }

    return geometry;
}

CollisionShape BlockModelLoader::parseCollisionFromDocument(const ConfigDocument& doc) {
    CollisionShape shape;

    // Parse all box entries
    for (auto* entry : doc.getAll("box")) {
        if (entry->dataLines.size() >= 2) {
            // Two lines: min corner and max corner
            const auto& minLine = entry->dataLines[0];
            const auto& maxLine = entry->dataLines[1];

            if (minLine.size() >= 3 && maxLine.size() >= 3) {
                AABB box(
                    minLine[0], minLine[1], minLine[2],
                    maxLine[0], maxLine[1], maxLine[2]
                );
                shape.addBox(box);
            }
        } else if (entry->dataLines.size() == 1 && entry->dataLines[0].size() >= 6) {
            // Single line with 6 values: min_x min_y min_z max_x max_y max_z
            const auto& line = entry->dataLines[0];
            AABB box(
                line[0], line[1], line[2],
                line[3], line[4], line[5]
            );
            shape.addBox(box);
        }
    }

    return shape;
}

std::optional<BlockGeometry> BlockModelLoader::parseGeometryFromString(const std::string& content) {
    ConfigDocument doc = parser_.parseString(content);
    return parseGeometryFromDocument(doc);
}

std::optional<CollisionShape> BlockModelLoader::parseCollisionFromString(const std::string& content) {
    ConfigDocument doc = parser_.parseString(content);
    return parseCollisionFromDocument(doc);
}

std::optional<BlockModel> BlockModelLoader::parseModelFromString(const std::string& content) {
    ConfigDocument doc = parser_.parseString(content);

    BlockModel model;

    // Load geometry if specified (requires resolver to be set)
    if (const ConfigEntry* geomEntry = doc.get("geometry")) {
        std::string geomPath = ensureExtension(geomEntry->value.asStringOwned(), ".geom");

        if (resolver_) {
            std::string resolvedContent = resolver_(geomPath);
            if (!resolvedContent.empty()) {
                auto geometry = parseGeometryFromString(resolvedContent);
                if (geometry) {
                    model.setGeometry(std::move(*geometry));
                }
            }
        }
    }

    // Load collision if specified
    if (const ConfigEntry* collEntry = doc.get("collision")) {
        std::string value = collEntry->value.asStringOwned();

        if (value == "none") {
            model.setCollision(CollisionShape::NONE);
        } else if (value == "full") {
            model.setCollision(CollisionShape::FULL_BLOCK);
        } else if (resolver_) {
            std::string collPath = ensureExtension(value, ".collision");
            std::string resolvedContent = resolver_(collPath);
            if (!resolvedContent.empty()) {
                auto collision = parseCollisionFromString(resolvedContent);
                if (collision) {
                    model.setCollision(std::move(*collision));
                }
            }
        }
    }

    // Parse rotation constraint
    if (const ConfigEntry* rotEntry = doc.get("rotations")) {
        std::string value = rotEntry->value.asStringOwned();
        RotationSet set = parseRotationSet(value);

        if (set == RotationSet::Custom) {
            std::vector<uint8_t> indices;
            std::istringstream iss(value);
            int idx;
            while (iss >> idx) {
                if (idx >= 0 && idx < 24) {
                    indices.push_back(static_cast<uint8_t>(idx));
                }
            }
            if (!indices.empty()) {
                model.setRotations(std::move(indices));
            }
        } else {
            model.setRotations(set);
        }
    }

    // Parse other properties
    if (const ConfigEntry* hardEntry = doc.get("hardness")) {
        model.setHardness(hardEntry->value.asFloat(1.0f));
    }

    if (const ConfigEntry* texEntry = doc.get("texture")) {
        model.setTexture(texEntry->value.asStringOwned());
    }

    if (const ConfigEntry* soundEntry = doc.get("sounds")) {
        model.setSounds(soundEntry->value.asStringOwned());
    }

    if (const ConfigEntry* lightEntry = doc.get("light-emission")) {
        model.setLightEmission(static_cast<uint8_t>(lightEntry->value.asInt(0)));
    }

    if (const ConfigEntry* attenEntry = doc.get("light-attenuation")) {
        model.setLightAttenuation(static_cast<uint8_t>(attenEntry->value.asInt(15)));
    }

    return model;
}

std::optional<FaceGeometry> BlockModelLoader::parseFaceEntry(
    const ConfigEntry& entry,
    int& nextCustomIndex
) {
    if (!entry.hasSuffix()) {
        lastError_ = "Face entry missing suffix (face name)";
        return std::nullopt;
    }

    FaceGeometry face;
    face.name = entry.suffix;

    // Parse face index from name
    int idx = parseFaceName(entry.suffix);
    if (idx >= 0) {
        face.faceIndex = idx;
    } else {
        // Custom face name - assign next available index
        face.faceIndex = nextCustomIndex++;
    }

    // Parse vertices from data lines
    for (const auto& dataLine : entry.dataLines) {
        auto vertex = parseVertex(dataLine);
        if (vertex) {
            face.vertices.push_back(*vertex);
        }
    }

    if (face.vertices.size() < 3) {
        lastError_ = "Face '" + entry.suffix + "' has fewer than 3 vertices";
        return std::nullopt;
    }

    return face;
}

std::optional<ModelVertex> BlockModelLoader::parseVertex(const std::vector<float>& data) {
    if (data.size() < 3) {
        return std::nullopt;
    }

    ModelVertex v;
    v.position = glm::vec3(data[0], data[1], data[2]);

    // UV coordinates are optional
    if (data.size() >= 5) {
        v.uv = glm::vec2(data[3], data[4]);
    } else {
        // Default UV based on position (simple projection)
        // This is a fallback - proper UVs should be specified
        v.uv = glm::vec2(data[0], data[2]);
    }

    return v;
}

std::unordered_set<int> BlockModelLoader::parseSolidFaces(std::string_view value) {
    std::unordered_set<int> result;

    // Split by spaces and commas
    std::string str(value);
    std::istringstream iss(str);
    std::string token;

    while (iss >> token) {
        // Remove commas
        if (!token.empty() && token.back() == ',') {
            token.pop_back();
        }

        int idx = parseFaceName(token);
        if (idx >= 0) {
            result.insert(idx);
        }
    }

    return result;
}

std::string BlockModelLoader::resolveFile(const std::string& ref, const std::string& basePath) {
    if (resolver_) {
        // Try resolver first
        std::string resolved = resolver_(ref);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    // Fall back to relative path resolution
    if (!basePath.empty() && !ref.empty() && ref[0] != '/') {
        return basePath + "/" + ref;
    }

    return ref;
}

bool BlockModelLoader::hasExtension(const std::string& path, const std::string& ext) {
    if (path.size() < ext.size()) {
        return false;
    }
    return path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

std::string BlockModelLoader::ensureExtension(const std::string& path, const std::string& ext) {
    // Check if already has any extension
    size_t dotPos = path.rfind('.');
    size_t slashPos = path.rfind('/');

    if (dotPos != std::string::npos &&
        (slashPos == std::string::npos || dotPos > slashPos)) {
        // Has an extension
        return path;
    }

    return path + ext;
}

std::string BlockModelLoader::getDirectory(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

// ============================================================================
// Factory function
// ============================================================================

BlockModelLoader createBlockModelLoader(ResourceLocator& locator) {
    BlockModelLoader loader;
    loader.setFileResolver([&locator](const std::string& path) -> std::string {
        auto resolved = locator.resolve(path);
        return resolved.string();
    });
    return loader;
}

}  // namespace finevox
