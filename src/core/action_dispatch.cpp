#include "finevox/core/action_dispatch.hpp"

namespace finevox {

void ActionDispatch::registerAction(const std::string& name, ActionHandler handler) {
    handlers_[name] = std::move(handler);
}

void ActionDispatch::unregisterAction(const std::string& name) {
    handlers_.erase(name);
}

bool ActionDispatch::hasAction(const std::string& name) const {
    return handlers_.find(name) != handlers_.end();
}

bool ActionDispatch::dispatch(const std::string& actionName, const ActionArgs& args) {
    auto it = handlers_.find(actionName);
    if (it == handlers_.end()) return false;
    it->second(args);
    return true;
}

bool ActionDispatch::dispatch(const std::string& actionName) {
    return dispatch(actionName, ActionArgs{});
}

void ActionDispatch::clear() {
    handlers_.clear();
}

}  // namespace finevox
