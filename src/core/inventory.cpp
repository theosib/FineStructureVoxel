#include "finevox/core/inventory.hpp"
#include "finevox/core/item_registry.hpp"

namespace finevox {

InventoryView::InventoryView(DataContainer& dc, NameRegistry& registry)
    : dc_(dc), registry_(registry) {}

int32_t InventoryView::slotCount() const {
    return dc_.get<int32_t>("size", 0);
}

void InventoryView::setSlotCount(int32_t count) {
    dc_.set("size", static_cast<int64_t>(count));
}

ItemStack InventoryView::getSlot(int32_t index) const {
    ItemStack stack;
    auto key = slotKey(index);
    const auto* slotDC = dc_.getChild(key);
    if (!slotDC) return stack;

    auto persistentId = static_cast<NameRegistry::PersistentId>(
        slotDC->get<int64_t>("t", 0));
    if (persistentId == NameRegistry::EMPTY_ID) return stack;

    std::string_view name = registry_.getName(persistentId);
    if (name.empty()) return stack;

    stack.type = ItemTypeId::fromName(name);
    stack.count = slotDC->get<int32_t>("c", 0);
    stack.durability = slotDC->get<int32_t>("d", 0);

    const auto* metaDC = slotDC->getChild("m");
    if (metaDC) {
        stack.metadata = metaDC->clone();
    }

    return stack;
}

void InventoryView::setSlot(int32_t index, const ItemStack& stack) {
    auto key = slotKey(index);

    if (stack.isEmpty()) {
        dc_.remove(key);
        return;
    }

    auto& slotDC = dc_.getOrCreateChild(key);
    slotDC.clear();

    auto persistentId = registry_.getOrAssign(stack.type.name());
    slotDC.set("t", static_cast<int64_t>(persistentId));
    slotDC.set("c", static_cast<int64_t>(stack.count));

    if (stack.durability != 0) {
        slotDC.set("d", static_cast<int64_t>(stack.durability));
    }

    if (stack.metadata) {
        slotDC.set("m", stack.metadata->clone());
    }
}

void InventoryView::clearSlot(int32_t index) {
    dc_.remove(slotKey(index));
}

int32_t InventoryView::addItem(ItemTypeId type, int32_t count) {
    if (type.isEmpty() || count <= 0) return count;

    int32_t maxStack = getMaxStackSize(type);
    int32_t remaining = count;
    int32_t slots = slotCount();

    // First pass: fill existing stacks of this type
    for (int32_t i = 0; i < slots && remaining > 0; ++i) {
        auto stack = getSlot(i);
        if (stack.type == type && stack.count < maxStack &&
            stack.durability == 0 && !stack.metadata) {
            int32_t space = maxStack - stack.count;
            int32_t toAdd = (remaining < space) ? remaining : space;
            stack.count += toAdd;
            remaining -= toAdd;
            setSlot(i, stack);
        }
    }

    // Second pass: fill empty slots
    for (int32_t i = 0; i < slots && remaining > 0; ++i) {
        auto stack = getSlot(i);
        if (stack.isEmpty()) {
            int32_t toAdd = (remaining < maxStack) ? remaining : maxStack;
            ItemStack newStack;
            newStack.type = type;
            newStack.count = toAdd;
            remaining -= toAdd;
            setSlot(i, newStack);
        }
    }

    return remaining;
}

ItemStack InventoryView::takeItem(int32_t slotIndex, int32_t count) {
    if (slotIndex < 0 || slotIndex >= slotCount() || count <= 0) {
        return ItemStack{};
    }

    auto stack = getSlot(slotIndex);
    if (stack.isEmpty()) return ItemStack{};

    auto taken = stack.split(count);
    setSlot(slotIndex, stack);
    return taken;
}

void InventoryView::swapSlots(int32_t a, int32_t b) {
    if (a == b) return;
    int32_t slots = slotCount();
    if (a < 0 || a >= slots || b < 0 || b >= slots) return;

    auto stackA = getSlot(a);
    auto stackB = getSlot(b);
    setSlot(a, stackB);
    setSlot(b, stackA);
}

int32_t InventoryView::countItem(ItemTypeId type) const {
    int32_t total = 0;
    int32_t slots = slotCount();
    for (int32_t i = 0; i < slots; ++i) {
        auto stack = getSlot(i);
        if (stack.type == type) {
            total += stack.count;
        }
    }
    return total;
}

bool InventoryView::hasItem(ItemTypeId type, int32_t count) const {
    return countItem(type) >= count;
}

int32_t InventoryView::removeItem(ItemTypeId type, int32_t count) {
    int32_t remaining = count;
    int32_t slots = slotCount();

    for (int32_t i = 0; i < slots && remaining > 0; ++i) {
        auto stack = getSlot(i);
        if (stack.type == type) {
            int32_t toRemove = (remaining < stack.count) ? remaining : stack.count;
            stack.count -= toRemove;
            remaining -= toRemove;
            if (stack.count <= 0) {
                stack.clear();
            }
            setSlot(i, stack);
        }
    }

    return count - remaining;
}

bool InventoryView::isEmpty() const {
    int32_t slots = slotCount();
    for (int32_t i = 0; i < slots; ++i) {
        if (!getSlot(i).isEmpty()) return false;
    }
    return true;
}

bool InventoryView::isFull() const {
    int32_t slots = slotCount();
    for (int32_t i = 0; i < slots; ++i) {
        auto stack = getSlot(i);
        if (stack.isEmpty()) return false;
        int32_t maxStack = getMaxStackSize(stack.type);
        if (stack.count < maxStack) return false;
    }
    return true;
}

std::string InventoryView::slotKey(int32_t index) {
    return std::to_string(index);
}

int32_t InventoryView::getMaxStackSize(ItemTypeId type) {
    const auto* itemType = ItemRegistry::global().getType(type);
    if (itemType) return itemType->maxStackSize;
    return 64;  // Default
}

}  // namespace finevox
