#include "finevox/core/entity_type_def.hpp"
#include <algorithm>

namespace finevox {

AIType parseAIType(std::string_view str) {
    if (str == "passive")  return AIType::Passive;
    if (str == "hostile")  return AIType::Hostile;
    if (str == "neutral")  return AIType::Neutral;
    if (str == "none")     return AIType::None;
    return AIType::Passive;  // Default
}

std::string_view aiTypeName(AIType type) {
    switch (type) {
        case AIType::Passive:  return "passive";
        case AIType::Hostile:  return "hostile";
        case AIType::Neutral:  return "neutral";
        case AIType::None:     return "none";
    }
    return "passive";
}

}  // namespace finevox
