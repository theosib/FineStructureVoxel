#include "finevox/script/inventory_bridge.hpp"
#include "finevox/core/inventory.hpp"
#include "finevox/core/item_stack.hpp"
#include "finevox/core/label_registry.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/crafting_helper.hpp"
#include "finevox/core/recipe_registry.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <finescript/value.h>
#include <finescript/map_data.h>
#include <finescript/native_function.h>

#include <set>

namespace finevox::script {

InventoryBridge::InventoryBridge() = default;
InventoryBridge::~InventoryBridge() = default;

void InventoryBridge::registerOwner(const std::string& name,
                                     DataContainer& dc,
                                     NameRegistry& registry) {
    std::lock_guard lock(mutex_);
    owners_[name] = {&dc, &registry};
}

void InventoryBridge::unregisterOwner(const std::string& name) {
    std::lock_guard lock(mutex_);
    owners_.erase(name);
}

InventoryBridge::OwnerEntry* InventoryBridge::findOwner(const std::string& name) {
    auto it = owners_.find(name);
    return (it != owners_.end()) ? &it->second : nullptr;
}

// Helper: convert ItemStack to finescript Value (map with =type, =count, etc.)
static finescript::Value itemStackToValue(const ItemStack& stack) {
    if (stack.isEmpty()) return finescript::Value::nil();

    auto& si = StringInterner::global();
    auto result = finescript::Value::map();
    auto& m = result.asMap();
    m.set(si.intern("type"), finescript::Value::string(std::string(stack.type.name())));
    m.set(si.intern("count"), finescript::Value::integer(stack.count));
    if (stack.durability != 0) {
        m.set(si.intern("durability"), finescript::Value::integer(stack.durability));
    }
    return result;
}

// Helper: extract string arg (supports both string and symbol values)
static std::string extractString(const finescript::Value& v) {
    if (v.isString()) return v.asString();
    if (v.isSymbol()) return std::string(StringInterner::global().lookup(v.asSymbol()));
    return {};
}

void InventoryBridge::registerNativeFunctions(finescript::ScriptEngine& engine) {
    // ========================================================================
    // L — Label lookup function
    // ========================================================================
    // {L "key"} → string
    // {L "key" arg0 arg1} → formatted string
    engine.registerFunction("L",
        [](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.empty() || !args[0].isString())
                return finescript::Value::nil();

            auto& labels = LabelRegistry::global();
            const std::string& key = args[0].asString();

            if (args.size() == 1) {
                return finescript::Value::string(std::string(labels.get(key)));
            }

            // Format with positional args
            std::vector<std::string> fmtArgs;
            fmtArgs.reserve(args.size() - 1);
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i].isString()) {
                    fmtArgs.push_back(args[i].asString());
                } else if (args[i].isInt()) {
                    fmtArgs.push_back(std::to_string(args[i].asInt()));
                } else if (args[i].isFloat()) {
                    fmtArgs.push_back(std::to_string(args[i].asFloat()));
                } else {
                    fmtArgs.emplace_back();
                }
            }
            return finescript::Value::string(labels.format(key, fmtArgs));
        });

    // ========================================================================
    // inv_get — Read a slot
    // ========================================================================
    // {inv_get "owner" "section" slot_index} → item map or nil
    engine.registerFunction("inv_get",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 3) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::nil();
            int32_t slot = static_cast<int32_t>(args[2].asInt());

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto* child = entry->dc->getChild(section);
            if (!child) return finescript::Value::nil();

            InventoryView view(*child, *entry->registry);
            return itemStackToValue(view.getSlot(slot));
        });

    // ========================================================================
    // inv_count — Count items of a type across all slots
    // ========================================================================
    // {inv_count "owner" "section" "item_type"} → int
    engine.registerFunction("inv_count",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 3) return finescript::Value::integer(0);
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            std::string typeName = extractString(args[2]);

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::integer(0);

            auto* child = entry->dc->getChild(section);
            if (!child) return finescript::Value::integer(0);

            InventoryView view(*child, *entry->registry);
            return finescript::Value::integer(view.countItem(ItemTypeId::fromName(typeName)));
        });

    // ========================================================================
    // inv_type — Get item type name at a slot
    // ========================================================================
    // {inv_type "owner" "section" slot_index} → string or nil
    engine.registerFunction("inv_type",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 3) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::nil();
            int32_t slot = static_cast<int32_t>(args[2].asInt());

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto* child = entry->dc->getChild(section);
            if (!child) return finescript::Value::nil();

            InventoryView view(*child, *entry->registry);
            auto stack = view.getSlot(slot);
            if (stack.isEmpty()) return finescript::Value::nil();
            return finescript::Value::string(std::string(stack.type.name()));
        });

    // ========================================================================
    // inv_size — Get number of slots in a section
    // ========================================================================
    // {inv_size "owner" "section"} → int
    engine.registerFunction("inv_size",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 2) return finescript::Value::integer(0);
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::integer(0);

            auto* child = entry->dc->getChild(section);
            if (!child) return finescript::Value::integer(0);

            InventoryView view(*child, *entry->registry);
            return finescript::Value::integer(view.slotCount());
        });

    // ========================================================================
    // inv_set — Set a slot to an item
    // ========================================================================
    // {inv_set "owner" "section" slot_index {=type "stone" =count 5}} → nil
    engine.registerFunction("inv_set",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::nil();
            int32_t slot = static_cast<int32_t>(args[2].asInt());

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto& child = entry->dc->getOrCreateChild(section);
            InventoryView view(child, *entry->registry);

            if (args[3].isNil()) {
                view.clearSlot(slot);
            } else if (args[3].isMap()) {
                auto& si = StringInterner::global();
                auto& m = args[3].asMap();
                auto typeVal = m.get(si.intern("type"));
                auto countVal = m.get(si.intern("count"));

                ItemStack stack;
                if (typeVal.isString()) {
                    stack.type = ItemTypeId::fromName(typeVal.asString());
                }
                stack.count = countVal.isInt() ? static_cast<int32_t>(countVal.asInt()) : 1;

                auto durVal = m.get(si.intern("durability"));
                if (durVal.isInt()) {
                    stack.durability = static_cast<int32_t>(durVal.asInt());
                }
                view.setSlot(slot, stack);
            }
            return finescript::Value::nil();
        });

    // ========================================================================
    // inv_clear — Clear a slot
    // ========================================================================
    // {inv_clear "owner" "section" slot_index} → nil
    engine.registerFunction("inv_clear",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 3) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::nil();
            int32_t slot = static_cast<int32_t>(args[2].asInt());

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto* child = entry->dc->getChild(section);
            if (!child) return finescript::Value::nil();

            InventoryView view(*child, *entry->registry);
            view.clearSlot(slot);
            return finescript::Value::nil();
        });

    // ========================================================================
    // inv_add — Add items to an inventory section (finds first available slot)
    // ========================================================================
    // {inv_add "owner" "section" "item_type" count} → int (remainder)
    engine.registerFunction("inv_add",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::integer(0);
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            std::string typeName = extractString(args[2]);
            int32_t count = args[3].isInt() ? static_cast<int32_t>(args[3].asInt()) : 1;

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::integer(count);

            auto& child = entry->dc->getOrCreateChild(section);
            InventoryView view(child, *entry->registry);
            int32_t remainder = view.addItem(ItemTypeId::fromName(typeName), count);
            return finescript::Value::integer(remainder);
        });

    // ========================================================================
    // inv_take — Take items from a specific slot
    // ========================================================================
    // {inv_take "owner" "section" slot_index count} → item map (what was taken)
    engine.registerFunction("inv_take",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::nil();
            int32_t slot = static_cast<int32_t>(args[2].asInt());
            int32_t count = args[3].isInt() ? static_cast<int32_t>(args[3].asInt()) : 1;

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto* child = entry->dc->getChild(section);
            if (!child) return finescript::Value::nil();

            InventoryView view(*child, *entry->registry);
            ItemStack taken = view.takeItem(slot, count);
            return itemStackToValue(taken);
        });

    // ========================================================================
    // inv_swap — Swap two slots (can be in different sections/owners)
    // ========================================================================
    // {inv_swap "owner_a" "sec_a" slot_a "owner_b" "sec_b" slot_b} → bool
    engine.registerFunction("inv_swap",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 6) return finescript::Value::boolean(false);
            std::string ownerA = extractString(args[0]);
            std::string secA = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::boolean(false);
            int32_t slotA = static_cast<int32_t>(args[2].asInt());

            std::string ownerB = extractString(args[3]);
            std::string secB = extractString(args[4]);
            if (!args[5].isInt()) return finescript::Value::boolean(false);
            int32_t slotB = static_cast<int32_t>(args[5].asInt());

            std::lock_guard lock(mutex_);
            auto* entryA = findOwner(ownerA);
            auto* entryB = findOwner(ownerB);
            if (!entryA || !entryB) return finescript::Value::boolean(false);

            auto* childA = entryA->dc->getChild(secA);
            auto* childB = entryB->dc->getChild(secB);
            if (!childA || !childB) return finescript::Value::boolean(false);

            InventoryView viewA(*childA, *entryA->registry);
            InventoryView viewB(*childB, *entryB->registry);

            // Cross-inventory swap: read both, write both
            ItemStack stackA = viewA.getSlot(slotA);
            ItemStack stackB = viewB.getSlot(slotB);
            viewA.setSlot(slotA, stackB);
            viewB.setSlot(slotB, stackA);
            return finescript::Value::boolean(true);
        });

    // ========================================================================
    // inv_move — Move items between slots (partial transfer supported)
    // ========================================================================
    // {inv_move "src_owner" "src_sec" src_slot "dst_owner" "dst_sec" dst_slot count}
    //   → int (remainder that couldn't be moved)
    engine.registerFunction("inv_move",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 7) return finescript::Value::integer(0);
            std::string srcOwner = extractString(args[0]);
            std::string srcSec = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::integer(0);
            int32_t srcSlot = static_cast<int32_t>(args[2].asInt());

            std::string dstOwner = extractString(args[3]);
            std::string dstSec = extractString(args[4]);
            if (!args[5].isInt()) return finescript::Value::integer(0);
            int32_t dstSlot = static_cast<int32_t>(args[5].asInt());
            int32_t count = args[6].isInt() ? static_cast<int32_t>(args[6].asInt()) : 64;

            std::lock_guard lock(mutex_);
            auto* srcEntry = findOwner(srcOwner);
            auto* dstEntry = findOwner(dstOwner);
            if (!srcEntry || !dstEntry) return finescript::Value::integer(count);

            auto* srcChild = srcEntry->dc->getChild(srcSec);
            auto* dstChild = dstEntry->dc->getChild(dstSec);
            if (!srcChild || !dstChild) return finescript::Value::integer(count);

            InventoryView srcView(*srcChild, *srcEntry->registry);
            InventoryView dstView(*dstChild, *dstEntry->registry);

            // Take from source
            ItemStack srcStack = srcView.getSlot(srcSlot);
            if (srcStack.isEmpty()) return finescript::Value::integer(count);

            int32_t toMove = std::min(count, srcStack.count);
            ItemStack dstStack = dstView.getSlot(dstSlot);

            if (dstStack.isEmpty()) {
                // Destination empty: place items there
                ItemStack moved;
                moved.type = srcStack.type;
                moved.count = toMove;
                dstView.setSlot(dstSlot, moved);

                srcStack.count -= toMove;
                if (srcStack.count <= 0) {
                    srcView.clearSlot(srcSlot);
                } else {
                    srcView.setSlot(srcSlot, srcStack);
                }
                return finescript::Value::integer(count - toMove);
            } else if (dstStack.canStackWith(srcStack)) {
                // Same type: merge
                int32_t space = 64 - dstStack.count;  // TODO: use maxStackSize from ItemType
                int32_t moved = std::min(toMove, space);
                if (moved <= 0) return finescript::Value::integer(count);

                dstStack.count += moved;
                dstView.setSlot(dstSlot, dstStack);

                srcStack.count -= moved;
                if (srcStack.count <= 0) {
                    srcView.clearSlot(srcSlot);
                } else {
                    srcView.setSlot(srcSlot, srcStack);
                }
                return finescript::Value::integer(count - moved);
            }
            // Different types, can't merge
            return finescript::Value::integer(count);
        });

    // ========================================================================
    // inv_init — Initialize an inventory section with a given slot count
    // ========================================================================
    // {inv_init "owner" "section" slot_count} → nil
    engine.registerFunction("inv_init",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 3) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt()) return finescript::Value::nil();
            int32_t slotCount = static_cast<int32_t>(args[2].asInt());

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto& child = entry->dc->getOrCreateChild(section);
            InventoryView view(child, *entry->registry);
            view.setSlotCount(slotCount);
            return finescript::Value::nil();
        });

    // ========================================================================
    // item_icon — Get icon info for an item type
    // ========================================================================
    // {item_icon "stone"} → {=texture "block_atlas" =uv0 [u0 v0] =uv1 [u1 v1]}
    // Returns nil if no icon is available
    engine.registerFunction("item_icon",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.empty()) return finescript::Value::nil();
            std::string typeName = extractString(args[0]);
            if (typeName.empty()) return finescript::Value::nil();

            if (!iconLookup_) return finescript::Value::nil();

            auto info = iconLookup_(typeName);
            if (!info) return finescript::Value::nil();

            auto& si = StringInterner::global();
            auto result = finescript::Value::map();
            auto& m = result.asMap();
            m.set(si.intern("texture"), finescript::Value::string(info->textureName));

            auto uv0 = finescript::Value::array({
                finescript::Value::number(info->u0),
                finescript::Value::number(info->v0)
            });
            auto uv1 = finescript::Value::array({
                finescript::Value::number(info->u1),
                finescript::Value::number(info->v1)
            });
            m.set(si.intern("uv0"), std::move(uv0));
            m.set(si.intern("uv1"), std::move(uv1));
            return result;
        });

    // ========================================================================
    // craft_find — Find a matching recipe for a crafting grid
    // ========================================================================
    // {craft_find "owner" "grid_section" grid_width grid_height "station"}
    //   → {=recipe "recipe_name" =output "item_type" =count N} or nil
    engine.registerFunction("craft_find",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string gridSection = extractString(args[1]);
            if (!args[2].isInt() || !args[3].isInt()) return finescript::Value::nil();
            int32_t gridWidth = static_cast<int32_t>(args[2].asInt());
            int32_t gridHeight = static_cast<int32_t>(args[3].asInt());

            StationTypeId station = EMPTY_STATION;
            if (args.size() > 4) {
                std::string stationName = extractString(args[4]);
                if (!stationName.empty() && stationName != "none") {
                    station = StationTypeId::fromName(stationName);
                }
            }

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto* child = entry->dc->getChild(gridSection);
            if (!child) return finescript::Value::nil();

            InventoryView view(*child, *entry->registry);

            // Read grid slots as ItemTypeId array
            std::vector<ItemTypeId> gridSlots(gridWidth * gridHeight);
            for (int32_t i = 0; i < gridWidth * gridHeight; ++i) {
                auto stack = view.getSlot(i);
                gridSlots[i] = stack.isEmpty() ? EMPTY_ITEM_TYPE : stack.type;
            }

            auto result = CraftingHelper::findRecipe(
                gridSlots.data(), gridWidth, gridHeight, station);

            if (!result.recipe) return finescript::Value::nil();

            auto& si = StringInterner::global();
            auto val = finescript::Value::map();
            auto& m = val.asMap();
            m.set(si.intern("recipe"), finescript::Value::string(
                std::string(result.recipe->id.name())));
            m.set(si.intern("output"), finescript::Value::string(
                std::string(result.recipe->outputItem.name())));
            m.set(si.intern("count"), finescript::Value::integer(
                result.recipe->outputCount));
            m.set(si.intern("mirrored"), finescript::Value::boolean(result.mirrored));
            return val;
        });

    // ========================================================================
    // craft_execute — Execute crafting: consume ingredients, return output
    // ========================================================================
    // {craft_execute "owner" "grid_section" grid_width grid_height "station"}
    //   → {=type "item_type" =count N} or nil (if no matching recipe)
    engine.registerFunction("craft_execute",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::nil();
            std::string owner = extractString(args[0]);
            std::string gridSection = extractString(args[1]);
            if (!args[2].isInt() || !args[3].isInt()) return finescript::Value::nil();
            int32_t gridWidth = static_cast<int32_t>(args[2].asInt());
            int32_t gridHeight = static_cast<int32_t>(args[3].asInt());

            StationTypeId station = EMPTY_STATION;
            if (args.size() > 4) {
                std::string stationName = extractString(args[4]);
                if (!stationName.empty() && stationName != "none") {
                    station = StationTypeId::fromName(stationName);
                }
            }

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::nil();

            auto* child = entry->dc->getChild(gridSection);
            if (!child) return finescript::Value::nil();

            InventoryView view(*child, *entry->registry);

            // Read grid slots
            std::vector<ItemTypeId> gridSlots(gridWidth * gridHeight);
            for (int32_t i = 0; i < gridWidth * gridHeight; ++i) {
                auto stack = view.getSlot(i);
                gridSlots[i] = stack.isEmpty() ? EMPTY_ITEM_TYPE : stack.type;
            }

            auto match = CraftingHelper::findRecipe(
                gridSlots.data(), gridWidth, gridHeight, station);

            if (!match.recipe) return finescript::Value::nil();

            // Execute: consume one of each ingredient from the grid
            if (!CraftingHelper::executeCraft(match, view, gridWidth, gridHeight)) {
                return finescript::Value::nil();
            }

            // Return the crafted output as an item map
            auto& si = StringInterner::global();
            auto result = finescript::Value::map();
            auto& m = result.asMap();
            m.set(si.intern("type"), finescript::Value::string(
                std::string(match.recipe->outputItem.name())));
            m.set(si.intern("count"), finescript::Value::integer(
                match.recipe->outputCount));
            return result;
        });

    // ========================================================================
    // craft_fill — Fill crafting grid from bag for a given recipe
    // ========================================================================
    // {craft_fill "recipe_name" "owner" "grid_section" "bag_section" grid_width grid_height}
    //   → true if filled, false if missing ingredients
    engine.registerFunction("craft_fill",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 6) return finescript::Value::boolean(false);
            std::string recipeName = extractString(args[0]);
            std::string owner = extractString(args[1]);
            std::string gridSection = extractString(args[2]);
            std::string bagSection = extractString(args[3]);
            if (!args[4].isInt() || !args[5].isInt()) return finescript::Value::boolean(false);
            int32_t gridWidth = static_cast<int32_t>(args[4].asInt());
            int32_t gridHeight = static_cast<int32_t>(args[5].asInt());

            auto& registry = RecipeRegistry::global();
            const auto* recipe = registry.getRecipe(recipeName);
            if (!recipe) return finescript::Value::boolean(false);

            // Only shaped and shapeless recipes
            if (recipe->isSmelting()) return finescript::Value::boolean(false);

            std::lock_guard lock(mutex_);
            auto* entry = findOwner(owner);
            if (!entry) return finescript::Value::boolean(false);

            auto* gridChild = entry->dc->getChild(gridSection);
            auto* bagChild = entry->dc->getChild(bagSection);
            if (!gridChild || !bagChild) return finescript::Value::boolean(false);

            InventoryView gridView(*gridChild, *entry->registry);
            InventoryView bagView(*bagChild, *entry->registry);

            int32_t gridSize = gridWidth * gridHeight;

            // First, return any items currently in the grid back to the bag
            for (int32_t i = 0; i < gridSize; ++i) {
                auto stack = gridView.getSlot(i);
                if (!stack.isEmpty()) {
                    bagView.addItem(stack.type, stack.count);
                    gridView.clearSlot(i);
                }
            }

            const auto& items = recipe->isShaped()
                ? recipe->pattern : recipe->ingredients;

            // Check if the recipe fits in the grid
            if (recipe->isShaped()) {
                if (recipe->width > gridWidth || recipe->height > gridHeight)
                    return finescript::Value::boolean(false);
            } else {
                if (static_cast<int32_t>(items.size()) > gridSize)
                    return finescript::Value::boolean(false);
            }

            // Collect needed items (count how many of each exact type)
            struct Need {
                ItemTypeId type;
                int32_t gridSlot;
            };
            std::vector<Need> needs;

            if (recipe->isShaped()) {
                for (int32_t y = 0; y < recipe->height; ++y) {
                    for (int32_t x = 0; x < recipe->width; ++x) {
                        int32_t pi = y * recipe->width + x;
                        if (pi >= static_cast<int32_t>(items.size())) continue;
                        const auto& im = items[pi];
                        if (im.isEmpty()) continue;

                        int32_t gridSlot = y * gridWidth + x;
                        if (im.isExact()) {
                            auto itemId = std::get<ItemMatch::Exact>(im.match).item;
                            needs.push_back({itemId, gridSlot});
                        }
                        // Tagged ingredients: find first matching item in bag
                        else if (im.isTagged()) {
                            bool found = false;
                            for (int32_t bi = 0; bi < bagView.slotCount(); ++bi) {
                                auto stack = bagView.getSlot(bi);
                                if (!stack.isEmpty() && im.matches(stack.type)) {
                                    needs.push_back({stack.type, gridSlot});
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) return finescript::Value::boolean(false);
                        }
                    }
                }
            } else {
                // Shapeless: place sequentially
                for (int32_t i = 0; i < static_cast<int32_t>(items.size()); ++i) {
                    const auto& im = items[i];
                    if (im.isEmpty()) continue;

                    if (im.isExact()) {
                        auto itemId = std::get<ItemMatch::Exact>(im.match).item;
                        needs.push_back({itemId, i});
                    } else if (im.isTagged()) {
                        bool found = false;
                        for (int32_t bi = 0; bi < bagView.slotCount(); ++bi) {
                            auto stack = bagView.getSlot(bi);
                            if (!stack.isEmpty() && im.matches(stack.type)) {
                                needs.push_back({stack.type, i});
                                found = true;
                                break;
                            }
                        }
                        if (!found) return finescript::Value::boolean(false);
                    }
                }
            }

            // Check bag has all needed items
            // Count needed per type
            std::unordered_map<uint32_t, int32_t> needed;
            for (const auto& n : needs) {
                needed[n.type.id]++;
            }
            for (const auto& [typeId, count] : needed) {
                ItemTypeId type{InternedId(typeId)};
                if (bagView.countItem(type) < count) {
                    return finescript::Value::boolean(false);
                }
            }

            // Take from bag and place in grid
            for (const auto& n : needs) {
                bagView.removeItem(n.type, 1);
                gridView.setSlot(n.gridSlot, ItemStack{n.type, 1});
            }

            return finescript::Value::boolean(true);
        });

    // ========================================================================
    // craft_recipes — List available recipes for a station
    // ========================================================================
    // {craft_recipes "station"} → array of {=recipe =output =count}
    // {craft_recipes} → all hand-crafting recipes
    engine.registerFunction("craft_recipes",
        [](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            StationTypeId station = EMPTY_STATION;
            if (!args.empty()) {
                std::string stationName = extractString(args[0]);
                if (!stationName.empty() && stationName != "none") {
                    station = StationTypeId::fromName(stationName);
                }
            }

            auto& registry = RecipeRegistry::global();
            auto recipes = registry.getRecipesForStation(station);

            auto& si = StringInterner::global();
            std::vector<finescript::Value> entries;
            entries.reserve(recipes.size());

            // Helper to get a display name from an ItemMatch
            auto matchName = [](const ItemMatch& im) -> std::string {
                return std::visit([](const auto& v) -> std::string {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, ItemMatch::Empty>) {
                        return "";
                    } else if constexpr (std::is_same_v<T, ItemMatch::Exact>) {
                        return std::string(v.item.name());
                    } else if constexpr (std::is_same_v<T, ItemMatch::Tagged>) {
                        return "#" + std::string(v.tag.name());
                    }
                }, im.match);
            };

            for (const auto* recipe : recipes) {
                if (recipe->isSmelting()) continue;  // Skip smelting

                auto entry = finescript::Value::map();
                auto& m = entry.asMap();
                m.set(si.intern("recipe"), finescript::Value::string(
                    std::string(recipe->id.name())));
                m.set(si.intern("output"), finescript::Value::string(
                    std::string(recipe->outputItem.name())));
                m.set(si.intern("count"), finescript::Value::integer(
                    recipe->outputCount));
                m.set(si.intern("category"), finescript::Value::string(
                    recipe->category));

                // Collect unique ingredient names
                std::vector<finescript::Value> ingredientList;
                const auto& items = recipe->isShaped()
                    ? recipe->pattern : recipe->ingredients;
                std::set<std::string> seen;
                for (const auto& im : items) {
                    std::string name = matchName(im);
                    if (!name.empty() && seen.insert(name).second) {
                        ingredientList.push_back(finescript::Value::string(name));
                    }
                }
                m.set(si.intern("ingredients"),
                    finescript::Value::array(std::move(ingredientList)));

                entries.push_back(std::move(entry));
            }

            return finescript::Value::array(std::move(entries));
        });

    // ========================================================================
    // build_inv_grid — Build a grid of inventory slot button widgets
    // ========================================================================
    // {build_inv_grid "owner" "section" rows cols slot_size} → array of widgets
    // Each slot is a button with id "slot_{section}_{index}"
    // Arranged in a grid with same_line between columns, newlines between rows
    engine.registerFunction("build_inv_grid",
        [this](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 5) return finescript::Value::array(std::vector<finescript::Value>{});
            std::string owner = extractString(args[0]);
            std::string section = extractString(args[1]);
            if (!args[2].isInt() || !args[3].isInt()) return finescript::Value::array(std::vector<finescript::Value>{});
            int32_t rows = static_cast<int32_t>(args[2].asInt());
            int32_t cols = static_cast<int32_t>(args[3].asInt());
            float slotSize = args[4].isNumeric()
                ? static_cast<float>(args[4].asNumber()) : 48.0f;

            // Optional on_click handler (arg 5)
            finescript::Value clickHandler = finescript::Value::nil();
            if (args.size() > 5 && args[5].isCallable()) {
                clickHandler = args[5];
            }

            auto& si = StringInterner::global();
            uint32_t symType = si.intern("type");
            uint32_t symButton = si.intern("button");
            uint32_t symSameLine = si.intern("same_line");
            uint32_t symLabel = si.intern("label");
            uint32_t symId = si.intern("id");
            uint32_t symWidth = si.intern("width");
            uint32_t symHeight = si.intern("height");
            uint32_t symOnClick = si.intern("on_click");
            uint32_t symOffset = si.intern("offset");

            std::vector<finescript::Value> widgets;
            widgets.reserve(rows * cols * 2);

            for (int32_t row = 0; row < rows; ++row) {
                for (int32_t col = 0; col < cols; ++col) {
                    int32_t index = row * cols + col;
                    std::string slotId = "slot_" + section + "_"
                                         + std::to_string(index);

                    // Add same_line spacer between columns
                    if (col > 0) {
                        auto spacer = finescript::Value::map();
                        spacer.asMap().set(symType,
                            finescript::Value::symbol(symSameLine));
                        widgets.push_back(std::move(spacer));
                    }

                    // Read current slot contents for label
                    std::string label;
                    {
                        std::lock_guard lock(mutex_);
                        auto* entry = findOwner(owner);
                        if (entry) {
                            auto* child = entry->dc->getChild(section);
                            if (child) {
                                InventoryView view(*child, *entry->registry);
                                auto stack = view.getSlot(index);
                                if (!stack.isEmpty()) {
                                    label = std::string(stack.type.name());
                                    if (stack.count > 1) {
                                        label += "\n"
                                            + std::to_string(stack.count);
                                    }
                                }
                            }
                        }
                    }

                    // Create button widget
                    auto btn = finescript::Value::map();
                    auto& m = btn.asMap();
                    m.set(symType, finescript::Value::symbol(symButton));
                    m.set(symLabel, finescript::Value::string(label));
                    m.set(symId, finescript::Value::string(slotId));
                    m.set(symWidth, finescript::Value::number(slotSize));
                    m.set(symHeight, finescript::Value::number(slotSize));

                    // Attach click handler if provided
                    if (clickHandler.isCallable()) {
                        auto ownerVal = finescript::Value::string(owner);
                        auto secVal = finescript::Value::string(section);
                        auto idxVal = finescript::Value::integer(index);
                        auto handler = clickHandler;
                        auto fn = std::make_shared<
                            finescript::SimpleLambdaFunction>(
                            [handler, ownerVal, secVal, idxVal](
                                finescript::ExecutionContext& innerCtx,
                                const std::vector<finescript::Value>&)
                                -> finescript::Value
                            {
                                std::vector<finescript::Value> callArgs;
                                callArgs.push_back(ownerVal);
                                callArgs.push_back(secVal);
                                callArgs.push_back(idxVal);
                                innerCtx.engine().callFunction(
                                    handler, std::move(callArgs), innerCtx);
                                return finescript::Value::nil();
                            });
                        m.set(symOnClick,
                            finescript::Value::nativeFunction(fn));
                    }

                    widgets.push_back(std::move(btn));
                }
            }

            return finescript::Value::array(std::move(widgets));
        });

    // ========================================================================
    // build_recipe_list — Build a widget list showing available recipes
    // ========================================================================
    // {build_recipe_list "station" "grid_section" grid_w grid_h} → array of widgets
    // {build_recipe_list} → hand-crafting recipes (no fill on click)
    engine.registerFunction("build_recipe_list",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            StationTypeId station = EMPTY_STATION;
            std::string stationStr;
            std::string gridSection = "craft_grid";
            int32_t gridW = 2, gridH = 2;

            if (!args.empty()) {
                stationStr = extractString(args[0]);
                if (!stationStr.empty() && stationStr != "none") {
                    station = StationTypeId::fromName(stationStr);
                }
            }
            if (args.size() > 1) gridSection = extractString(args[1]);
            if (args.size() > 2 && args[2].isInt()) gridW = static_cast<int32_t>(args[2].asInt());
            if (args.size() > 3 && args[3].isInt()) gridH = static_cast<int32_t>(args[3].asInt());

            auto& registry = RecipeRegistry::global();
            auto recipes = registry.getRecipesForStation(station);
            // If a station is specified, also include hand-crafting recipes
            if (station != EMPTY_STATION) {
                auto handRecipes = registry.getRecipesForStation(EMPTY_STATION);
                recipes.insert(recipes.end(), handRecipes.begin(), handRecipes.end());
            }

            auto& si = StringInterner::global();
            uint32_t symType = si.intern("type");
            uint32_t symText = si.intern("text");
            uint32_t symSeparator = si.intern("separator");
            uint32_t symOnClick = si.intern("on_click");

            // Helper to get a display name from an ItemMatch
            auto matchName = [](const ItemMatch& im) -> std::string {
                return std::visit([](const auto& v) -> std::string {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, ItemMatch::Empty>) {
                        return "";
                    } else if constexpr (std::is_same_v<T, ItemMatch::Exact>) {
                        return std::string(v.item.name());
                    } else if constexpr (std::is_same_v<T, ItemMatch::Tagged>) {
                        return "#" + std::string(v.tag.name());
                    }
                }, im.match);
            };

            std::vector<finescript::Value> widgets;

            for (const auto* recipe : recipes) {
                if (recipe->isSmelting()) continue;

                // Check if recipe fits in the grid
                if (recipe->isShaped() &&
                    (recipe->width > gridW || recipe->height > gridH)) {
                    continue;  // Skip recipes that don't fit
                }

                std::string recipeName = std::string(recipe->id.name());

                // Output line: "output_name x count"
                std::string outputText = std::string(recipe->outputItem.name());
                if (recipe->outputCount > 1) {
                    outputText += " x" + std::to_string(recipe->outputCount);
                }
                auto outputWidget = finescript::Value::map();
                auto& ow = outputWidget.asMap();
                ow.set(symType, finescript::Value::symbol(si.intern("button")));
                ow.set(si.intern("label"), finescript::Value::string(outputText));

                // Attach on_click that calls craft_fill
                auto fn = std::make_shared<finescript::SimpleLambdaFunction>(
                    [recipeName, gridSection, gridW, gridH](
                        finescript::ExecutionContext& innerCtx,
                        const std::vector<finescript::Value>&) -> finescript::Value
                    {
                        std::vector<finescript::Value> fillArgs;
                        fillArgs.push_back(finescript::Value::string(recipeName));
                        fillArgs.push_back(finescript::Value::string("player"));
                        fillArgs.push_back(finescript::Value::string(gridSection));
                        fillArgs.push_back(finescript::Value::string("bag"));
                        fillArgs.push_back(finescript::Value::integer(gridW));
                        fillArgs.push_back(finescript::Value::integer(gridH));
                        auto craftFillFn = innerCtx.get("craft_fill");
                        if (!craftFillFn.isNil()) {
                            innerCtx.engine().callFunction(
                                craftFillFn, std::move(fillArgs), innerCtx);
                        }
                        return finescript::Value::nil();
                    });
                ow.set(symOnClick, finescript::Value::nativeFunction(fn));
                widgets.push_back(std::move(outputWidget));

                // Ingredients line
                const auto& items = recipe->isShaped()
                    ? recipe->pattern : recipe->ingredients;
                std::set<std::string> seen;
                std::string ingredientText = "  ";
                bool first = true;
                for (const auto& im : items) {
                    std::string name = matchName(im);
                    if (!name.empty() && seen.insert(name).second) {
                        if (!first) ingredientText += ", ";
                        ingredientText += name;
                        first = false;
                    }
                }
                auto ingredientWidget = finescript::Value::map();
                auto& iw = ingredientWidget.asMap();
                iw.set(symType, finescript::Value::symbol(si.intern("text_disabled")));
                iw.set(symText, finescript::Value::string(ingredientText));
                widgets.push_back(std::move(ingredientWidget));

                // Separator
                auto sep = finescript::Value::map();
                sep.asMap().set(symType, finescript::Value::symbol(symSeparator));
                widgets.push_back(std::move(sep));
            }

            return finescript::Value::array(std::move(widgets));
        });
}

void InventoryBridge::setIconLookup(IconLookup lookup) {
    iconLookup_ = std::move(lookup);
}

}  // namespace finevox::script
