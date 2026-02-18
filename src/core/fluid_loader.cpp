#include "finevox/core/fluid_loader.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/config_parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace finevox {

// Helper: parse space-separated floats "r g b a" → glm::vec4
static glm::vec4 parseVec4(std::string_view str, glm::vec4 defaultVal = glm::vec4{0}) {
    std::string tmp{str};
    std::istringstream iss{tmp};
    glm::vec4 v = defaultVal;
    iss >> v.x >> v.y >> v.z >> v.w;
    return v;
}

std::optional<FluidType> FluidLoader::loadFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);

    if (doc.empty()) return std::nullopt;

    FluidType ft;

    for (const auto& entry : doc.entries()) {
        const auto& key = entry.key;
        const auto& val = entry.value;

        // Identity
        if (key == "name") {
            ft.name = val.asStringOwned();
        }
        // Flow
        else if (key == "spread_decay") {
            ft.spreadDecay = val.asInt(ft.spreadDecay);
        } else if (key == "flow_speed") {
            ft.flowSpeed = val.asInt(ft.flowSpeed);
        } else if (key == "source_formation") {
            ft.sourceFormation = val.asBool(ft.sourceFormation);
        } else if (key == "source_formation_count") {
            ft.sourceFormationCount = val.asInt(ft.sourceFormationCount);
        } else if (key == "slope_preference") {
            ft.slopePreference = val.asFloat(ft.slopePreference);
        } else if (key == "max_level") {
            ft.maxLevel = val.asInt(ft.maxLevel);
        }
        // Infiltration
        else if (key == "infiltrates_non_full") {
            ft.infiltratesNonFull = val.asBool(ft.infiltratesNonFull);
        } else if (key == "infiltrates_below") {
            ft.infiltratesBelow = val.asBool(ft.infiltratesBelow);
        }
        // Physics
        else if (key == "density") {
            ft.density = val.asFloat(ft.density);
        } else if (key == "viscosity") {
            ft.viscosity = val.asFloat(ft.viscosity);
        } else if (key == "buoyancy_factor") {
            ft.buoyancyFactor = val.asFloat(ft.buoyancyFactor);
        } else if (key == "flow_force") {
            ft.flowForce = val.asFloat(ft.flowForce);
        } else if (key == "can_displace") {
            ft.canDisplace = val.asBool(ft.canDisplace);
        }
        // Visual
        else if (key == "opaque") {
            ft.opaque = val.asBool(ft.opaque);
        } else if (key == "tint_color") {
            ft.tintColor = parseVec4(val.asString(), ft.tintColor);
        } else if (key == "texture") {
            ft.texture = val.asStringOwned();
        } else if (key == "surface_tex_speed") {
            ft.surfaceTexSpeed = val.asFloat(ft.surfaceTexSpeed);
        }
        // Light
        else if (key == "light_emission") {
            ft.lightEmission = static_cast<uint8_t>(val.asInt(ft.lightEmission));
        } else if (key == "light_attenuation") {
            ft.lightAttenuation = static_cast<uint8_t>(val.asInt(ft.lightAttenuation));
        } else if (key == "custom_attenuation") {
            ft.customAttenuation = val.asBool(ft.customAttenuation);
        } else if (key == "attenuation_base") {
            ft.attenuationBase = val.asFloat(ft.attenuationBase);
        }
        // Sound
        else if (key == "sounds") {
            ft.soundSet = SoundSetId::fromName(val.asString());
        }
        // Damage
        else if (key == "contact_damage") {
            ft.contactDamage = val.asFloat(ft.contactDamage);
        } else if (key == "submersion_damage") {
            ft.submersionDamage = val.asFloat(ft.submersionDamage);
        }
        // Fog
        else if (key == "underwater_fog_color") {
            ft.underwaterFogColor = parseVec4(val.asString(), ft.underwaterFogColor);
        } else if (key == "underwater_fog_density") {
            ft.underwaterFogDensity = val.asFloat(ft.underwaterFogDensity);
        }
        // Container
        else if (key == "units_per_source") {
            ft.unitsPerSource = val.asInt(ft.unitsPerSource);
        }
    }

    if (ft.name.empty()) return std::nullopt;
    return ft;
}

std::optional<FluidType> FluidLoader::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromString(content);
}

size_t FluidLoader::loadDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".fluid") continue;

        auto ft = loadFromFile(entry.path().string());
        if (ft) {
            auto name = ft->name;
            if (FluidRegistry::global().registerType(name, std::move(*ft))) {
                ++count;
            }
        }
    }

    return count;
}

}  // namespace finevox
