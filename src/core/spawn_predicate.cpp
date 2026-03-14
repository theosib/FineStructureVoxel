#include "finevox/core/spawn_predicate.hpp"
#include "finevox/core/spawn_rule.hpp"

namespace finevox {

SpawnPredicateRegistry& SpawnPredicateRegistry::global() {
    static SpawnPredicateRegistry instance;
    return instance;
}

void SpawnPredicateRegistry::registerPredicate(std::string_view name, SpawnPredicate pred) {
    predicates_[std::string(name)] = std::move(pred);
}

void SpawnPredicateRegistry::unregisterPredicate(std::string_view name) {
    predicates_.erase(std::string(name));
}

bool SpawnPredicateRegistry::hasPredicate(std::string_view name) const {
    return predicates_.find(std::string(name)) != predicates_.end();
}

bool SpawnPredicateRegistry::evaluateAll(const SpawnRule& rule, const SpawnContext& ctx) const {
    for (const auto& predName : rule.customPredicates) {
        auto it = predicates_.find(predName);
        if (it == predicates_.end()) {
            // Unknown predicate — fail closed (deny spawn)
            return false;
        }
        if (!it->second(rule, ctx)) {
            return false;
        }
    }
    return true;
}

void SpawnPredicateRegistry::clear() {
    predicates_.clear();
}

}  // namespace finevox
